#include "pkg_cli.hpp"
#include "manifest.hpp"
#include "sha256.hpp"
#include "tpkg.hpp"
#include "../common/http_fetch.hpp"

extern "C" {
#include "../../runtime/cJSON.h"
}

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>
#include <sys/stat.h>

namespace fs = std::filesystem;

namespace tulpar {

namespace {

constexpr const char *kManifestFile = "tulpar.toml";
constexpr const char *kLockFile = "tulpar.lock";

bool file_exists(const char *path) {
    struct stat st;
    return stat(path, &st) == 0;
}

// ---------------------------------------------------------------------------
// Semver range support (client-side).
//
// Versions inside `tulpar.toml` and `tulpar pkg add foo@<spec>` can now be:
//   * exact:  "1.2.3"   — must match a published version verbatim
//   * caret:  "^1.2.3"  — >=1.2.3, <2.0.0  (compatible-within-major)
//   * tilde:  "~1.2.3"  — >=1.2.3, <1.3.0  (compatible-within-minor)
//   * any:    "*", ""   — highest published version wins
//
// Pre-release / build metadata is intentionally ignored on both sides
// of the comparison: the integer parse stops at the first non-digit so
// `1.0.0-rc1` parses to (1,0,0) and is treated equal to `1.0.0`. This
// mirrors the registry's own `_semver_compare` helper (tulpar-be PR
// #16) so client and server agree on what "equal" means until full
// pre-release semantics are introduced.

struct SemVer {
    int major = 0;
    int minor = 0;
    int patch = 0;
    bool ok = false;
};

SemVer parse_semver(const std::string &s) {
    SemVer r;
    int parts[3] = {0, 0, 0};
    int idx = 0;
    size_t i = 0;
    while (idx < 3 && i < s.size()) {
        if (s[i] < '0' || s[i] > '9') break;
        int v = 0;
        while (i < s.size() && s[i] >= '0' && s[i] <= '9') {
            v = v * 10 + (s[i] - '0');
            i++;
        }
        parts[idx++] = v;
        if (i < s.size() && s[i] == '.') {
            i++;
        } else {
            break;
        }
    }
    if (idx == 0) return r;
    r.major = parts[0];
    r.minor = parts[1];
    r.patch = parts[2];
    r.ok = true;
    return r;
}

int cmp_semver(const SemVer &a, const SemVer &b) {
    if (a.major != b.major) return a.major < b.major ? -1 : 1;
    if (a.minor != b.minor) return a.minor < b.minor ? -1 : 1;
    if (a.patch != b.patch) return a.patch < b.patch ? -1 : 1;
    return 0;
}

bool is_range_spec(const std::string &spec) {
    if (spec.empty()) return true; // empty = "*"
    if (spec == "*" || spec == "latest") return true;
    if (spec[0] == '^' || spec[0] == '~') return true;
    return false;
}

// Test a candidate against a spec. Spec already filtered through
// `is_range_spec` true; exact matches are handled by the caller's
// fast path before this helper is reached.
bool match_range(const std::string &spec, const std::string &version) {
    SemVer v = parse_semver(version);
    if (!v.ok) return false;
    if (spec.empty() || spec == "*" || spec == "latest") return true;

    char op = spec[0];
    SemVer base = parse_semver(spec.substr(1));
    if (!base.ok) return false;
    if (cmp_semver(v, base) < 0) return false; // below floor

    if (op == '^') {
        // < (base.major + 1).0.0; treats 0.x specially for safety —
        // 0.x bumps are breaking under semver convention, so `^0.2.3`
        // means `>=0.2.3, <0.3.0` not `<1.0.0`.
        if (base.major > 0) {
            return v.major == base.major;
        }
        if (base.minor > 0) {
            return v.major == 0 && v.minor == base.minor;
        }
        return v.major == 0 && v.minor == 0 && v.patch == base.patch;
    }
    if (op == '~') {
        // <(base.major).(base.minor + 1).0
        return v.major == base.major && v.minor == base.minor;
    }
    return false;
}

// Pull `GET <registry>/v1/packages/<name>` and parse the `versions` array.
// Returns empty vector + err on failure; caller decides whether to
// fall back to verbatim-spec install.
bool fetch_versions(const std::string &registry_url, const std::string &name,
                    std::vector<std::string> &out, std::string &err) {
    std::string url = registry_url;
    if (!url.empty() && url.back() == '/') url.pop_back();
    url += "/v1/packages/";
    url += name;

    std::string body;
    int status = 0;
    if (!tulpar::http_fetch_url(url, body, status, err)) {
        return false;
    }
    if (status < 200 || status >= 300) {
        err = "HTTP " + std::to_string(status);
        return false;
    }
    cJSON *doc = cJSON_Parse(body.c_str());
    if (!doc) {
        err = "registry response is not valid JSON";
        return false;
    }
    cJSON *vers = cJSON_GetObjectItem(doc, "versions");
    if (!cJSON_IsArray(vers)) {
        cJSON_Delete(doc);
        err = "registry response missing `versions` array";
        return false;
    }
    cJSON *it = nullptr;
    cJSON_ArrayForEach(it, vers) {
        if (cJSON_IsString(it) && it->valuestring) {
            out.emplace_back(it->valuestring);
        }
    }
    cJSON_Delete(doc);
    return true;
}

// Pick the highest-comparing available version that satisfies `spec`.
// Returns "" when none match (caller surfaces a helpful error).
std::string pick_best_match(const std::string &spec,
                            const std::vector<std::string> &available) {
    std::string best;
    SemVer best_v;
    for (const auto &v : available) {
        if (!match_range(spec, v)) continue;
        SemVer cand = parse_semver(v);
        if (!cand.ok) continue;
        if (best.empty() || cmp_semver(cand, best_v) > 0) {
            best = v;
            best_v = cand;
        }
    }
    return best;
}

int cmd_init(int argc, char **argv) {
    const char *name = (argc > 0) ? argv[0] : nullptr;
    if (file_exists(kManifestFile)) {
        std::fprintf(stderr,
                     "tulpar pkg init: '%s' already exists; refusing to "
                     "overwrite. Edit it directly or remove it first.\n",
                     kManifestFile);
        return 1;
    }

    Manifest m;
    m.name = name ? name : "my-tulpar-package";
    m.version = "0.1.0";
    m.description = "A Tulpar package.";
    m.license = "MIT";

    std::string err;
    if (!manifest_save(kManifestFile, m, err)) {
        std::fprintf(stderr, "tulpar pkg init: %s\n", err.c_str());
        return 1;
    }
    std::fprintf(stdout, "Created %s\n", kManifestFile);
    return 0;
}

int cmd_list() {
    Manifest m;
    std::string err;
    if (!manifest_load(kManifestFile, m, err)) {
        std::fprintf(stderr, "tulpar pkg list: %s\n", err.c_str());
        return 1;
    }
    std::fprintf(stdout, "%s %s\n",
                 m.name.empty() ? "(unnamed)" : m.name.c_str(),
                 m.version.empty() ? "(no-version)" : m.version.c_str());
    if (!m.description.empty()) {
        std::fprintf(stdout, "  %s\n", m.description.c_str());
    }
    if (m.dependencies.empty()) {
        std::fprintf(stdout, "no dependencies\n");
    } else {
        std::fprintf(stdout, "dependencies (%zu):\n", m.dependencies.size());
        for (const auto &[k, v] : m.dependencies) {
            std::fprintf(stdout, "  %-24s %s\n", k.c_str(), v.c_str());
        }
    }
    return 0;
}

// Split `name@version` into (name, version). If no @, version defaults
// to "*" so the manifest at least records the dep was wanted.
void split_spec(const std::string &spec, std::string &name,
                std::string &version) {
    size_t at = spec.find('@');
    if (at == std::string::npos) {
        name = spec;
        version = "*";
    } else {
        name = spec.substr(0, at);
        version = spec.substr(at + 1);
        if (version.empty()) version = "*";
    }
}

int cmd_add(int argc, char **argv) {
    if (argc < 1) {
        std::fprintf(stderr, "Usage: tulpar pkg add <name>[@<version>]\n");
        return 2;
    }
    Manifest m;
    std::string err;
    if (!manifest_load(kManifestFile, m, err)) {
        std::fprintf(stderr, "tulpar pkg add: %s "
                              "(run `tulpar pkg init` first)\n", err.c_str());
        return 1;
    }
    std::string name, version;
    split_spec(argv[0], name, version);
    if (name.empty()) {
        std::fprintf(stderr, "tulpar pkg add: empty package name\n");
        return 2;
    }
    bool replaced = false;
    for (auto &dep : m.dependencies) {
        if (dep.first == name) {
            dep.second = version;
            replaced = true;
            break;
        }
    }
    if (!replaced) {
        m.dependencies.push_back({name, version});
    }
    if (!manifest_save(kManifestFile, m, err)) {
        std::fprintf(stderr, "tulpar pkg add: %s\n", err.c_str());
        return 1;
    }
    std::fprintf(stdout, "%s %s = \"%s\"\n",
                 replaced ? "updated" : "added",
                 name.c_str(), version.c_str());
    return 0;
}

// `tulpar pkg install` walks the manifest's [dependencies] table and
// vendors each dep into `tulpar_modules/<name>/`. The current MVP
// only handles `path:` deps; other version specs (semver from a
// registry) print a "not implemented" line so users know the registry
// is on the roadmap but not wired up yet.
//
// Path-spec format: `path:./relative/dir` or `path:/absolute/dir`. The
// referenced directory is copied recursively under
// `tulpar_modules/<name>/`. The entry-point file `<name>.tpr` inside
// it is what `import "<name>"` will load (resolution lives in
// aot_pipeline.cpp).
int cmd_install() {
    Manifest m;
    std::string err;
    if (!manifest_load(kManifestFile, m, err)) {
        std::fprintf(stderr, "tulpar pkg install: %s\n", err.c_str());
        return 1;
    }
    if (m.dependencies.empty()) {
        std::fprintf(stdout, "no dependencies to install\n");
        return 0;
    }

    fs::path modules_dir = "tulpar_modules";
    std::error_code ec;
    fs::create_directories(modules_dir, ec);
    if (ec) {
        std::fprintf(stderr,
                     "tulpar pkg install: cannot create '%s': %s\n",
                     modules_dir.string().c_str(), ec.message().c_str());
        return 1;
    }

    // Lockfile assembly. Each registry / url install records the
    // resolved URL + a SHA-256 of the downloaded body so re-installing
    // the same manifest is byte-stable even if the registry's `latest`
    // pointer moves later, AND so we can detect tampered registries
    // serving different content under the same URL.
    struct LockEntry { std::string name, url, sha256; };
    std::vector<LockEntry> lock_entries;

    // Read the existing lockfile if present so subsequent installs can
    // skip the network round-trip when `tulpar_modules/<name>/<name>.tpr`
    // already exists and matches the recorded sha256. Format is
    //
    //   [resolved]
    //   <name> = "<url>"
    //   [checksums]
    //   <name> = "<sha256-hex>"
    //
    // Older clients without the `[checksums]` table just see a simple
    // url map and ignore the new section, so the format is backward
    // compatible.
    std::map<std::string, std::string> prev_resolved;
    std::map<std::string, std::string> prev_sha;
    if (file_exists(kLockFile)) {
        std::ifstream lf(kLockFile);
        std::string section;
        for (std::string line; std::getline(lf, line);) {
            // strip leading whitespace + trailing CR/whitespace
            size_t lo = line.find_first_not_of(" \t");
            size_t hi = line.find_last_not_of(" \t\r");
            if (lo == std::string::npos) continue;
            std::string l = line.substr(lo, hi - lo + 1);
            if (l.empty() || l[0] == '#') continue;
            if (l.front() == '[' && l.back() == ']') {
                section = l.substr(1, l.size() - 2);
                continue;
            }
            size_t eq = l.find('=');
            if (eq == std::string::npos) continue;
            std::string k = l.substr(0, eq);
            std::string v = l.substr(eq + 1);
            // trim k
            size_t kend = k.find_last_not_of(" \t");
            if (kend != std::string::npos) k = k.substr(0, kend + 1);
            // trim v + strip surrounding quotes
            size_t vlo = v.find_first_not_of(" \t");
            size_t vhi = v.find_last_not_of(" \t\r");
            if (vlo == std::string::npos) continue;
            v = v.substr(vlo, vhi - vlo + 1);
            if (v.size() >= 2 && v.front() == '"' && v.back() == '"') {
                v = v.substr(1, v.size() - 2);
            }
            if (section == "resolved") prev_resolved[k] = v;
            else if (section == "checksums") prev_sha[k] = v;
        }
    }

    // Common helper: returns true if the existing on-disk content for
    // `<name>` matches the lockfile's recorded hash for `<name>`. When
    // it does, we can skip the network fetch entirely — same behavior
    // npm/cargo cache lookups offer.
    auto local_matches_lock = [&](const std::string &name,
                                  const std::string &expected_url) -> bool {
        auto rit = prev_resolved.find(name);
        auto sit = prev_sha.find(name);
        if (rit == prev_resolved.end() || sit == prev_sha.end()) return false;
        if (rit->second != expected_url) return false;
        fs::path target = modules_dir / name / (name + ".tpr");
        std::ifstream in(target, std::ios::binary);
        if (!in) return false;
        std::ostringstream buf;
        buf << in.rdbuf();
        std::string body = buf.str();
        return tulpar::sha256_hex(body) == sit->second;
    };

    int installed = 0;
    int skipped = 0;
    for (const auto &[name, spec] : m.dependencies) {
        // `url:` spec — fetch a single .tpr file over plain HTTP and
        // install it as `tulpar_modules/<name>/<name>.tpr`. No TLS,
        // no archives, no checksum: simplest end-to-end registry-ish
        // path for now. Real semver registry comes after TLS lands.
        if (spec.rfind("url:", 0) == 0) {
            std::string url = spec.substr(4);
            if (local_matches_lock(name, url)) {
                std::fprintf(stdout,
                             "  = %s (cached, sha256 matches lockfile)\n",
                             name.c_str());
                lock_entries.push_back({name, url, prev_sha[name]});
                installed++;
                continue;
            }
            std::string body, err;
            int status = 0;
            if (!tulpar::http_fetch_url(url, body, status, err)) {
                std::fprintf(stderr, "tulpar pkg install: %s: fetch failed: %s\n",
                             name.c_str(), err.c_str());
                return 1;
            }
            if (status < 200 || status >= 300) {
                std::fprintf(stderr, "tulpar pkg install: %s: HTTP %d from %s\n",
                             name.c_str(), status, url.c_str());
                return 1;
            }
            std::string body_sha = tulpar::sha256_hex(body);
            // Lockfile pin: when the lockfile already records this name
            // at this URL with a different sha256, refuse to overwrite —
            // the registry served different bytes under a previously
            // pinned URL (potentially malicious). User can recover by
            // editing tulpar.lock or removing the entry explicitly.
            auto sit = prev_sha.find(name);
            if (sit != prev_sha.end() && prev_resolved[name] == url &&
                sit->second != body_sha) {
                std::fprintf(stderr,
                             "tulpar pkg install: %s@%s: lockfile sha256 "
                             "mismatch (expected %s, got %s) — refusing to "
                             "overwrite. Delete tulpar.lock or fix the "
                             "registry URL.\n",
                             name.c_str(), url.c_str(),
                             sit->second.c_str(), body_sha.c_str());
                return 1;
            }
            fs::path dest_dir = modules_dir / name;
            std::error_code ec2;
            fs::remove_all(dest_dir, ec2);
            fs::create_directories(dest_dir, ec2);

            // .tpkg multi-file bundle (Plan 02 PR3): content-sniff
            // dispatch. We treat the body as a JSON archive when:
            //   - the URL ends in `.tpkg` (explicit), OR
            //   - the first non-whitespace byte is `{` AND the body
            //     contains the `"tpkg"` schema key.
            // Anything else (default `.tpr` body, code starting with
            // `func`, `import`, etc.) keeps the legacy single-file
            // write. Content-sniff is robust to registry endpoints
            // that serve via query strings rather than file extensions
            // (e.g. `/v1/package/bytes?name=X&version=Y`).
            bool is_bundle = false;
            if (url.size() >= 5 &&
                url.compare(url.size() - 5, 5, ".tpkg") == 0) {
                is_bundle = true;
            } else {
                size_t i = 0;
                while (i < body.size() &&
                       (body[i] == ' ' || body[i] == '\t' ||
                        body[i] == '\n' || body[i] == '\r')) i++;
                if (i < body.size() && body[i] == '{' &&
                    body.find("\"tpkg\"") != std::string::npos) {
                    is_bundle = true;
                }
            }
            if (is_bundle) {
                Tpkg pkg;
                std::string perr;
                if (!tpkg_parse(body, pkg, perr)) {
                    std::fprintf(stderr,
                                 "tulpar pkg install: %s: %s\n",
                                 name.c_str(), perr.c_str());
                    return 1;
                }
                if (!tpkg_extract(pkg, dest_dir.string(), perr)) {
                    std::fprintf(stderr,
                                 "tulpar pkg install: %s: %s\n",
                                 name.c_str(), perr.c_str());
                    return 1;
                }
                std::fprintf(stdout,
                             "  + %s (.tpkg, %zu files) -> %s "
                             "(%zu archive bytes, sha256 %.12s...)\n",
                             name.c_str(), pkg.files.size(),
                             dest_dir.string().c_str(), body.size(),
                             body_sha.c_str());
            } else {
                fs::path target = dest_dir / (name + ".tpr");
                std::ofstream out(target, std::ios::binary | std::ios::trunc);
                if (!out) {
                    std::fprintf(stderr, "tulpar pkg install: %s: cannot write '%s'\n",
                                 name.c_str(), target.string().c_str());
                    return 1;
                }
                out.write(body.data(), (std::streamsize)body.size());
                std::fprintf(stdout, "  + %s -> %s (%zu bytes from %s, sha256 %.12s...)\n",
                             name.c_str(), target.string().c_str(), body.size(),
                             url.c_str(), body_sha.c_str());
            }
            lock_entries.push_back({name, url, body_sha});
            installed++;
            continue;
        }

        // Registry version spec — anything that isn't `path:` / `url:`
        // and the manifest declares a `[registry] url`. The registry's
        // well-known shape (defined by tulpar-be / pkg.tulparlang.dev)
        // serves package source bytes at:
        //
        //   <registry>/v1/packages/<name>/versions/<version>/source
        //
        // The body is either a raw `.tpr` (single-file package) or a
        // `.tpkg` JSON archive (multi-file bundle). The same
        // content-sniff used by the `url:` branch above picks the
        // right extraction path.
        //
        // Range specs (`^1.2.3`, `~1.0.0`, `*`, `""`) get resolved up
        // front against the registry's `/v1/packages/<name>` listing,
        // and the highest matching version becomes the URL fragment.
        // Plain "1.2.3" stays exact — the lookup still hits the same
        // endpoint, just without the resolution step.
        if (spec.rfind("path:", 0) != 0) {
            if (m.registry_url.empty()) {
                std::fprintf(stdout,
                             "  - %s (%s) — no `[registry] url` configured; "
                             "use `path:./dir` or `url:http://...` for now\n",
                             name.c_str(), spec.c_str());
                skipped++;
                continue;
            }
            std::string resolved_version = spec;
            if (is_range_spec(spec)) {
                std::vector<std::string> available;
                std::string err;
                if (!fetch_versions(m.registry_url, name, available, err)) {
                    std::fprintf(stderr,
                                 "tulpar pkg install: %s@%s: range resolve "
                                 "failed: %s\n",
                                 name.c_str(), spec.c_str(), err.c_str());
                    return 1;
                }
                resolved_version = pick_best_match(spec, available);
                if (resolved_version.empty()) {
                    std::fprintf(stderr,
                                 "tulpar pkg install: %s@%s: no published "
                                 "version satisfies the range (registry has "
                                 "%zu version%s)\n",
                                 name.c_str(), spec.c_str(), available.size(),
                                 available.size() == 1 ? "" : "s");
                    return 1;
                }
                std::fprintf(stdout, "  > %s@%s -> resolved %s\n", name.c_str(),
                             spec.c_str(), resolved_version.c_str());
            }
            std::string url = m.registry_url;
            if (!url.empty() && url.back() == '/') url.pop_back();
            url += "/v1/packages/";
            url += name;
            url += "/versions/";
            url += resolved_version;
            url += "/source";

            if (local_matches_lock(name, url)) {
                std::fprintf(stdout,
                             "  = %s@%s (cached, sha256 matches lockfile)\n",
                             name.c_str(), spec.c_str());
                lock_entries.push_back({name, url, prev_sha[name]});
                installed++;
                continue;
            }

            std::string body, err;
            int status = 0;
            if (!tulpar::http_fetch_url(url, body, status, err)) {
                std::fprintf(stderr,
                             "tulpar pkg install: %s@%s: registry fetch "
                             "failed: %s\n",
                             name.c_str(), spec.c_str(), err.c_str());
                return 1;
            }
            if (status < 200 || status >= 300) {
                std::fprintf(stderr,
                             "tulpar pkg install: %s@%s: HTTP %d from %s\n",
                             name.c_str(), spec.c_str(), status, url.c_str());
                return 1;
            }
            std::string body_sha = tulpar::sha256_hex(body);
            auto sit = prev_sha.find(name);
            if (sit != prev_sha.end() && prev_resolved[name] == url &&
                sit->second != body_sha) {
                std::fprintf(stderr,
                             "tulpar pkg install: %s@%s: lockfile sha256 "
                             "mismatch (expected %s, got %s) — refusing to "
                             "overwrite. Delete tulpar.lock or fix the "
                             "registry URL.\n",
                             name.c_str(), spec.c_str(),
                             sit->second.c_str(), body_sha.c_str());
                return 1;
            }
            fs::path dest_dir = modules_dir / name;
            std::error_code ec2;
            fs::remove_all(dest_dir, ec2);
            fs::create_directories(dest_dir, ec2);

            // Content-sniff: registry bodies can be raw .tpr OR a
            // .tpkg JSON archive (e.g. multipkg). Same probe the `url:`
            // branch uses — first non-whitespace byte `{` plus a
            // `"tpkg"` schema key triggers the bundle path.
            bool is_bundle = false;
            {
                size_t i = 0;
                while (i < body.size() &&
                       (body[i] == ' ' || body[i] == '\t' ||
                        body[i] == '\n' || body[i] == '\r')) i++;
                if (i < body.size() && body[i] == '{' &&
                    body.find("\"tpkg\"") != std::string::npos) {
                    is_bundle = true;
                }
            }
            if (is_bundle) {
                Tpkg pkg;
                std::string perr;
                if (!tpkg_parse(body, pkg, perr)) {
                    std::fprintf(stderr,
                                 "tulpar pkg install: %s@%s: %s\n",
                                 name.c_str(), spec.c_str(), perr.c_str());
                    return 1;
                }
                if (!tpkg_extract(pkg, dest_dir.string(), perr)) {
                    std::fprintf(stderr,
                                 "tulpar pkg install: %s@%s: %s\n",
                                 name.c_str(), spec.c_str(), perr.c_str());
                    return 1;
                }
                std::fprintf(stdout,
                             "  + %s@%s (.tpkg, %zu files) -> %s "
                             "(%zu archive bytes, sha256 %.12s...)\n",
                             name.c_str(), spec.c_str(), pkg.files.size(),
                             dest_dir.string().c_str(), body.size(),
                             body_sha.c_str());
            } else {
                fs::path target = dest_dir / (name + ".tpr");
                std::ofstream out(target, std::ios::binary | std::ios::trunc);
                if (!out) {
                    std::fprintf(stderr,
                                 "tulpar pkg install: %s@%s: cannot write '%s'\n",
                                 name.c_str(), spec.c_str(),
                                 target.string().c_str());
                    return 1;
                }
                out.write(body.data(), (std::streamsize)body.size());
                std::fprintf(stdout,
                             "  + %s@%s -> %s (%zu bytes from registry, sha256 %.12s...)\n",
                             name.c_str(), spec.c_str(),
                             target.string().c_str(), body.size(),
                             body_sha.c_str());
            }
            lock_entries.push_back({name, url, body_sha});
            installed++;
            continue;
        }
        std::string raw = spec.substr(5);
        fs::path src(raw);
        if (!fs::exists(src, ec) || !fs::is_directory(src, ec)) {
            std::fprintf(stderr,
                         "tulpar pkg install: %s: '%s' is not an existing directory\n",
                         name.c_str(), raw.c_str());
            return 1;
        }

        fs::path dest = modules_dir / name;
        fs::remove_all(dest, ec);  // refresh on every install
        fs::create_directories(dest, ec);
        if (ec) {
            std::fprintf(stderr, "tulpar pkg install: %s: %s\n",
                         name.c_str(), ec.message().c_str());
            return 1;
        }

        // Copy `.tpr` files only — keeps vendoring deterministic and
        // small. If a package wants to ship `.so`/data files we'll
        // revisit, but `.tpr` is the only thing the AOT importer reads.
        int copied = 0;
        for (auto it = fs::recursive_directory_iterator(src, ec);
             !ec && it != fs::recursive_directory_iterator(); ++it) {
            if (!it->is_regular_file()) continue;
            const fs::path &p = it->path();
            if (p.extension() != ".tpr") continue;
            fs::path rel = fs::relative(p, src, ec);
            if (ec) break;
            fs::path target = dest / rel;
            fs::create_directories(target.parent_path(), ec);
            fs::copy_file(p, target, fs::copy_options::overwrite_existing, ec);
            if (ec) {
                std::fprintf(stderr,
                             "tulpar pkg install: %s: copy '%s' failed: %s\n",
                             name.c_str(), p.string().c_str(),
                             ec.message().c_str());
                return 1;
            }
            copied++;
        }
        std::fprintf(stdout, "  + %s -> %s (%d file%s)\n",
                     name.c_str(), dest.string().c_str(), copied,
                     copied == 1 ? "" : "s");
        // Lock entry for path: deps records the spec verbatim — the
        // resolution is just "look at the same directory next time",
        // so the spec IS the lock. No sha256: local files can change
        // freely, the lockfile-cache flow doesn't apply here.
        lock_entries.push_back({name, spec, ""});
        installed++;
    }

    // Write tulpar.lock — backward-compatible TOML with two tables:
    //   [resolved]   name = "<url-or-spec>"
    //   [checksums]  name = "<sha256-hex>"
    // Older clients only consume `[resolved]` and ignore the new
    // section. Path deps don't have a sha256 (local files can change
    // freely); their entry in `[checksums]` is omitted entirely.
    if (!lock_entries.empty()) {
        auto escape = [](const std::string &v) {
            std::string out;
            out.reserve(v.size());
            for (char c : v) {
                if (c == '\\' || c == '"') out.push_back('\\');
                out.push_back(c);
            }
            return out;
        };
        std::string lock_body =
            "# tulpar.lock — auto-generated by `tulpar pkg install`.\n"
            "# DO NOT EDIT. Commit alongside tulpar.toml so re-installs are reproducible.\n\n"
            "[resolved]\n";
        for (const auto &e : lock_entries) {
            lock_body += e.name;
            lock_body += " = \"";
            lock_body += escape(e.url);
            lock_body += "\"\n";
        }
        bool any_sha = false;
        for (const auto &e : lock_entries) {
            if (!e.sha256.empty()) { any_sha = true; break; }
        }
        if (any_sha) {
            lock_body += "\n[checksums]\n";
            for (const auto &e : lock_entries) {
                if (e.sha256.empty()) continue;
                lock_body += e.name;
                lock_body += " = \"";
                lock_body += escape(e.sha256);
                lock_body += "\"\n";
            }
        }
        std::ofstream lf(kLockFile, std::ios::binary | std::ios::trunc);
        if (lf) {
            lf.write(lock_body.data(), (std::streamsize)lock_body.size());
        }
    }

    std::fprintf(stdout, "installed: %d, skipped: %d\n", installed, skipped);
    return 0;
}

// Read a UTF-8 text file. Returns empty string on failure (caller treats
// as fatal — `cmd_publish` is the only consumer and it errors out
// explicitly when nothing comes back).
std::string read_file_text(const fs::path &p, std::string &out_err) {
    std::ifstream in(p, std::ios::binary);
    if (!in) {
        out_err = "cannot read '" + p.string() + "'";
        return {};
    }
    std::ostringstream buf;
    buf << in.rdbuf();
    return buf.str();
}

// Walk the project root looking for `.tpr` files to ship. Excludes
// `tulpar_modules/` (dependencies — not ours), the build artifacts
// `build*/`, and anything beginning with `.` (hidden files / .git).
// Returns POSIX-style relative paths (forward slashes), sorted, so
// the published bundle is byte-stable across runs and OSes — a
// requirement for the sha256 we send to the registry to mean anything.
std::vector<std::string> collect_project_sources(std::string &out_err) {
    fs::path root = fs::current_path();
    std::vector<std::string> result;
    std::error_code ec;
    for (auto it = fs::recursive_directory_iterator(
             root, fs::directory_options::skip_permission_denied, ec);
         !ec && it != fs::recursive_directory_iterator(); ++it) {
        const fs::path &p = it->path();
        std::string seg = p.filename().string();
        // Prune hidden dirs and the noisy ones before descending.
        if (it->is_directory()) {
            if (seg == "tulpar_modules" || seg == "build" ||
                seg == "build-linux" || seg == "build-macos" ||
                seg == "build-windows" || seg == "node_modules" ||
                (!seg.empty() && seg[0] == '.')) {
                it.disable_recursion_pending();
            }
            continue;
        }
        if (!it->is_regular_file()) continue;
        if (p.extension() != ".tpr") continue;
        fs::path rel = fs::relative(p, root, ec);
        if (ec) {
            out_err = "relative path failed for '" + p.string() + "'";
            return {};
        }
        // Normalise to POSIX separators — the bundle is JSON consumed
        // cross-platform, and the on-disk path validator in tpkg.cpp
        // accepts both, but keeping the wire form uniform avoids
        // surprises in the published JSON.
        std::string rels = rel.generic_string();
        result.push_back(std::move(rels));
    }
    std::sort(result.begin(), result.end());
    return result;
}

// Build the `source` payload to ship.
//
// Two shapes, picked automatically:
//   - exactly one `.tpr` and it equals `<name>.tpr` → raw `.tpr` body
//     (registry stores it verbatim; install side writes it straight to
//     `tulpar_modules/<name>/<name>.tpr`).
//   - anything else → `.tpkg` JSON bundle (matches `runtime/cJSON.h`-built
//     archives the install side already content-sniffs).
//
// `entry` defaults to `<name>.tpr`. If the user wrote their entry
// somewhere else (`src/foo.tpr`), we still set `entry` to `<name>.tpr`
// — the registry contract is that `<name>.tpr` exists at the bundle
// root, because that's how `import "<name>"` resolves on the consumer
// side. We *enforce* that by failing publish if no `<name>.tpr` is
// present in the collected files.
std::string build_publish_source(const std::string &name,
                                 const std::string &version,
                                 const std::vector<std::string> &files,
                                 std::string &out_err) {
    std::string entry_name = name + ".tpr";
    bool has_entry = false;
    for (const auto &f : files) {
        if (f == entry_name) { has_entry = true; break; }
    }
    if (!has_entry) {
        out_err = "no '" + entry_name + "' at the project root — every "
                  "published package must expose `<name>.tpr` as the "
                  "import entry point";
        return {};
    }

    if (files.size() == 1 && files[0] == entry_name) {
        // Single-file shape: raw `.tpr` body, no JSON envelope. Cheaper
        // to fetch and easier to read on the registry side.
        std::string err;
        std::string body = read_file_text(fs::path(entry_name), err);
        if (!err.empty()) { out_err = err; return {}; }
        return body;
    }

    // Multi-file shape: build a `.tpkg` JSON bundle. Use cJSON for
    // string escaping so embedded backslashes/quotes/newlines come
    // out wire-correct.
    cJSON *root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "tpkg", 1);
    cJSON_AddStringToObject(root, "name", name.c_str());
    cJSON_AddStringToObject(root, "version", version.c_str());
    cJSON_AddStringToObject(root, "entry", entry_name.c_str());
    cJSON *jfiles = cJSON_AddArrayToObject(root, "files");
    for (const auto &f : files) {
        std::string err;
        std::string content = read_file_text(fs::path(f), err);
        if (!err.empty()) {
            out_err = err;
            cJSON_Delete(root);
            return {};
        }
        cJSON *jf = cJSON_CreateObject();
        cJSON_AddStringToObject(jf, "path", f.c_str());
        cJSON_AddStringToObject(jf, "content", content.c_str());
        cJSON_AddItemToArray(jfiles, jf);
    }
    char *printed = cJSON_PrintUnformatted(root);
    if (!printed) {
        out_err = "tpkg JSON serialise failed";
        cJSON_Delete(root);
        return {};
    }
    std::string out(printed);
    std::free(printed);
    cJSON_Delete(root);
    return out;
}

// Resolve the registry root URL. Order:
//   1. `--registry <url>` flag
//   2. `[registry] url = "..."` in tulpar.toml
//   3. `TULPAR_REGISTRY` env
// Trailing slash trimmed for clean concatenation.
std::string resolve_registry(const std::string &flag_url,
                             const Manifest &m) {
    auto trim_slash = [](std::string u) {
        while (!u.empty() && u.back() == '/') u.pop_back();
        return u;
    };
    if (!flag_url.empty()) return trim_slash(flag_url);
    if (!m.registry_url.empty()) return trim_slash(m.registry_url);
    if (const char *env = std::getenv("TULPAR_REGISTRY")) {
        return trim_slash(std::string(env));
    }
    return {};
}

// Resolve the publish bearer token. Order:
//   1. `--token <tok>` flag
//   2. `TULPAR_PUBLISH_TOKEN` env
// We *don't* read the token from tulpar.toml — committing the publish
// secret next to the manifest would be the wrong default, and the env
// var is what every CI provider reaches for first.
std::string resolve_token(const std::string &flag_token) {
    if (!flag_token.empty()) return flag_token;
    if (const char *env = std::getenv("TULPAR_PUBLISH_TOKEN")) return env;
    return {};
}

// Split an HTTP/1.0 raw response (status line + headers + CRLF CRLF +
// body) into status code + body string. Returns false on a malformed
// response. The HTTP fetch helper already concatenates everything for
// us, so this is just slicing.
bool split_http_response(const std::string &full, int &out_status,
                         std::string &out_body) {
    size_t le = full.find('\n');
    if (le == std::string::npos) return false;
    std::string status_line = full.substr(0, le);
    if (!status_line.empty() && status_line.back() == '\r') status_line.pop_back();
    size_t sp = status_line.find(' ');
    if (sp == std::string::npos) return false;
    size_t sp2 = status_line.find(' ', sp + 1);
    std::string code = status_line.substr(
        sp + 1, (sp2 == std::string::npos ? status_line.size() : sp2) - sp - 1);
    out_status = std::atoi(code.c_str());
    size_t body_start = full.find("\r\n\r\n");
    if (body_start == std::string::npos) {
        body_start = full.find("\n\n");
        if (body_start != std::string::npos) body_start += 2;
        else body_start = full.size();
    } else {
        body_start += 4;
    }
    out_body = (body_start < full.size()) ? full.substr(body_start) : "";
    return true;
}

int cmd_publish(int argc, char **argv) {
    // Minimal flag parsing — only `--registry <url>`, `--token <tok>`,
    // `--dry-run` are supported. Anything unknown errors loudly so we
    // don't silently absorb a typo.
    std::string flag_registry;
    std::string flag_token;
    bool dry_run = false;
    for (int i = 0; i < argc; i++) {
        std::string a = argv[i];
        if (a == "--registry" && i + 1 < argc) {
            flag_registry = argv[++i];
        } else if (a == "--token" && i + 1 < argc) {
            flag_token = argv[++i];
        } else if (a == "--dry-run") {
            dry_run = true;
        } else {
            std::fprintf(stderr,
                         "tulpar pkg publish: unknown argument '%s'\n"
                         "Usage: tulpar pkg publish [--registry <url>] "
                         "[--token <tok>] [--dry-run]\n",
                         a.c_str());
            return 2;
        }
    }

    Manifest m;
    std::string err;
    if (!manifest_load(kManifestFile, m, err)) {
        std::fprintf(stderr, "tulpar pkg publish: %s\n", err.c_str());
        return 1;
    }
    if (m.name.empty() || m.version.empty()) {
        std::fprintf(stderr, "tulpar pkg publish: tulpar.toml must set both "
                             "`name` and `version` (got name='%s', version='%s')\n",
                     m.name.c_str(), m.version.c_str());
        return 1;
    }

    std::string registry = resolve_registry(flag_registry, m);
    if (registry.empty()) {
        std::fprintf(stderr,
                     "tulpar pkg publish: no registry configured. Set one of:\n"
                     "  - `--registry <url>` flag\n"
                     "  - `[registry]\\nurl = \"https://...\"` in tulpar.toml\n"
                     "  - `TULPAR_REGISTRY` env var\n");
        return 1;
    }

    std::string token = resolve_token(flag_token);
    if (token.empty() && !dry_run) {
        std::fprintf(stderr,
                     "tulpar pkg publish: no auth token. Pass `--token <tok>` "
                     "or set TULPAR_PUBLISH_TOKEN. (Use --dry-run to skip the "
                     "POST and just see what would ship.)\n");
        return 1;
    }

    std::vector<std::string> files = collect_project_sources(err);
    if (!err.empty()) {
        std::fprintf(stderr, "tulpar pkg publish: %s\n", err.c_str());
        return 1;
    }
    if (files.empty()) {
        std::fprintf(stderr, "tulpar pkg publish: no .tpr files found in "
                             "project root\n");
        return 1;
    }

    std::string source = build_publish_source(m.name, m.version, files, err);
    if (!err.empty()) {
        std::fprintf(stderr, "tulpar pkg publish: %s\n", err.c_str());
        return 1;
    }
    std::string sha = sha256_hex(source);

    std::fprintf(stdout, "publishing %s@%s\n", m.name.c_str(), m.version.c_str());
    std::fprintf(stdout, "  registry:    %s\n", registry.c_str());
    std::fprintf(stdout, "  files:       %zu\n", files.size());
    for (const auto &f : files) {
        std::fprintf(stdout, "    - %s\n", f.c_str());
    }
    std::fprintf(stdout, "  shape:       %s\n",
                 (files.size() == 1 && files[0] == m.name + ".tpr")
                     ? "raw .tpr"
                     : ".tpkg bundle");
    std::fprintf(stdout, "  source size: %zu bytes\n", source.size());
    std::fprintf(stdout, "  sha256:      %s\n", sha.c_str());

    if (dry_run) {
        std::fprintf(stdout, "dry-run: not posting to registry\n");
        return 0;
    }

    // Build the request JSON. cJSON handles the string escaping for us
    // — the `source` field will contain real newlines, quotes, and
    // backslashes that need to come out wire-clean.
    cJSON *body_json = cJSON_CreateObject();
    cJSON_AddStringToObject(body_json, "name", m.name.c_str());
    cJSON_AddStringToObject(body_json, "version", m.version.c_str());
    cJSON_AddStringToObject(body_json, "sha256", sha.c_str());
    cJSON_AddStringToObject(body_json, "source", source.c_str());
    cJSON_AddStringToObject(body_json, "description", m.description.c_str());
    char *printed = cJSON_PrintUnformatted(body_json);
    std::string body_str(printed ? printed : "");
    if (printed) std::free(printed);
    cJSON_Delete(body_json);

    std::string url = registry + "/v1/publish";
    std::string auth_hdr = "Authorization: Bearer " + token + "\r\n";
    std::string full, fetch_err;
    if (!tulpar::http_request_url("POST", url, body_str, full, fetch_err,
                                  auth_hdr)) {
        std::fprintf(stderr, "tulpar pkg publish: %s\n", fetch_err.c_str());
        return 1;
    }
    int status = 0;
    std::string resp_body;
    if (!split_http_response(full, status, resp_body)) {
        std::fprintf(stderr, "tulpar pkg publish: malformed HTTP response\n");
        return 1;
    }
    std::fprintf(stdout, "  HTTP %d\n", status);
    std::fprintf(stdout, "  %s\n", resp_body.c_str());
    if (status < 200 || status >= 300) {
        return 1;
    }
    return 0;
}

int cmd_remove(int argc, char **argv) {
    if (argc < 1) {
        std::fprintf(stderr, "Usage: tulpar pkg remove <name>\n");
        return 2;
    }
    Manifest m;
    std::string err;
    if (!manifest_load(kManifestFile, m, err)) {
        std::fprintf(stderr, "tulpar pkg remove: %s\n", err.c_str());
        return 1;
    }
    const std::string target = argv[0];
    size_t before = m.dependencies.size();
    m.dependencies.erase(
        std::remove_if(m.dependencies.begin(), m.dependencies.end(),
                       [&](const auto &p) { return p.first == target; }),
        m.dependencies.end());
    if (m.dependencies.size() == before) {
        std::fprintf(stderr, "tulpar pkg remove: '%s' not in dependencies\n",
                     target.c_str());
        return 1;
    }
    if (!manifest_save(kManifestFile, m, err)) {
        std::fprintf(stderr, "tulpar pkg remove: %s\n", err.c_str());
        return 1;
    }
    std::fprintf(stdout, "removed %s\n", target.c_str());
    return 0;
}

void print_usage() {
    std::fprintf(stderr,
        "Usage: tulpar pkg <command> [args]\n"
        "\n"
        "Commands:\n"
        "  init [name]            Create a tulpar.toml in the current dir.\n"
        "  list                   Show name, version and dependencies.\n"
        "  add <name>[@<ver>]     Add or update a dependency line.\n"
        "  remove <name>          Drop a dependency line.\n"
        "  install                Vendor every `path:` dependency into\n"
        "                         tulpar_modules/<name>/.\n"
        "  publish [flags]        Publish the current package to the\n"
        "                         configured registry.\n"
        "                           --registry <url>   override the registry root\n"
        "                           --token <tok>      override TULPAR_PUBLISH_TOKEN\n"
        "                           --dry-run          print the bundle plan, skip POST\n"
        "\n"
        "The manifest format is a small TOML subset (string values only,\n"
        "top-level keys + a single [dependencies] table). For `publish`,\n"
        "set the registry URL via `[registry]\\nurl = \"https://...\"` in\n"
        "tulpar.toml or via the TULPAR_REGISTRY env var, and the auth\n"
        "token via TULPAR_PUBLISH_TOKEN.\n");
}

}  // namespace

int pkg_cli_main(int argc, char **argv) {
    // argv[0] = "tulpar", argv[1] = "pkg", argv[2] = subcommand, …
    if (argc < 3) {
        print_usage();
        return 2;
    }
    const char *sub = argv[2];
    int sub_argc = argc - 3;
    char **sub_argv = argv + 3;

    if (std::strcmp(sub, "init") == 0)    return cmd_init(sub_argc, sub_argv);
    if (std::strcmp(sub, "list") == 0)    return cmd_list();
    if (std::strcmp(sub, "add") == 0)     return cmd_add(sub_argc, sub_argv);
    if (std::strcmp(sub, "remove") == 0)  return cmd_remove(sub_argc, sub_argv);
    if (std::strcmp(sub, "install") == 0) return cmd_install();
    if (std::strcmp(sub, "publish") == 0) return cmd_publish(sub_argc, sub_argv);

    std::fprintf(stderr, "tulpar pkg: unknown command '%s'\n\n", sub);
    print_usage();
    return 2;
}

}  // namespace tulpar
