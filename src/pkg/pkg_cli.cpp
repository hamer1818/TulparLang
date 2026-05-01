#include "pkg_cli.hpp"
#include "manifest.hpp"
#include "../common/http_fetch.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
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
    // resolved URL so re-installing the same manifest is byte-stable
    // even if the registry's `latest` pointer moves later.
    std::vector<std::pair<std::string, std::string>> lock_entries;

    int installed = 0;
    int skipped = 0;
    for (const auto &[name, spec] : m.dependencies) {
        // `url:` spec — fetch a single .tpr file over plain HTTP and
        // install it as `tulpar_modules/<name>/<name>.tpr`. No TLS,
        // no archives, no checksum: simplest end-to-end registry-ish
        // path for now. Real semver registry comes after TLS lands.
        if (spec.rfind("url:", 0) == 0) {
            std::string url = spec.substr(4);
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
            fs::path dest_dir = modules_dir / name;
            std::error_code ec2;
            fs::remove_all(dest_dir, ec2);
            fs::create_directories(dest_dir, ec2);
            fs::path target = dest_dir / (name + ".tpr");
            std::ofstream out(target, std::ios::binary | std::ios::trunc);
            if (!out) {
                std::fprintf(stderr, "tulpar pkg install: %s: cannot write '%s'\n",
                             name.c_str(), target.string().c_str());
                return 1;
            }
            out.write(body.data(), (std::streamsize)body.size());
            std::fprintf(stdout, "  + %s -> %s (%zu bytes from %s)\n",
                         name.c_str(), target.string().c_str(), body.size(),
                         url.c_str());
            lock_entries.push_back({name, url});
            installed++;
            continue;
        }

        // Registry version spec — anything that isn't `path:` / `url:`
        // and the manifest declares a `[registry] url`. Resolution rule
        // is intentionally simple: the registry MUST expose package
        // sources at `<registry>/<name>/<version>.tpr`. Real semver
        // ranges (`^0.2.0`, `>=1.0,<2`) are out of scope; the version
        // string is taken literally as the URL fragment.
        if (spec.rfind("path:", 0) != 0) {
            if (m.registry_url.empty()) {
                std::fprintf(stdout,
                             "  - %s (%s) — no `[registry] url` configured; "
                             "use `path:./dir` or `url:http://...` for now\n",
                             name.c_str(), spec.c_str());
                skipped++;
                continue;
            }
            std::string url = m.registry_url;
            if (!url.empty() && url.back() == '/') url.pop_back();
            url += "/";
            url += name;
            url += "/";
            url += spec;
            url += ".tpr";

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
            fs::path dest_dir = modules_dir / name;
            std::error_code ec2;
            fs::remove_all(dest_dir, ec2);
            fs::create_directories(dest_dir, ec2);
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
                         "  + %s@%s -> %s (%zu bytes from registry)\n",
                         name.c_str(), spec.c_str(),
                         target.string().c_str(), body.size());
            lock_entries.push_back({name, url});
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
        // so the spec IS the lock.
        lock_entries.push_back({name, spec});
        installed++;
    }

    // Write tulpar.lock — same TOML subset as the manifest, just one
    // [resolved] table mapping `name = "fully-resolved-url-or-spec"`.
    if (!lock_entries.empty()) {
        std::string lock_body = "# tulpar.lock — auto-generated by `tulpar pkg install`.\n";
        lock_body += "# DO NOT EDIT. Commit alongside tulpar.toml so re-installs are reproducible.\n\n";
        lock_body += "[resolved]\n";
        for (const auto &[k, v] : lock_entries) {
            lock_body += k;
            lock_body += " = \"";
            for (char c : v) {
                if (c == '\\' || c == '"') lock_body.push_back('\\');
                lock_body.push_back(c);
            }
            lock_body += "\"\n";
        }
        std::ofstream lf(kLockFile, std::ios::binary | std::ios::trunc);
        if (lf) {
            lf.write(lock_body.data(), (std::streamsize)lock_body.size());
        }
    }

    std::fprintf(stdout, "installed: %d, skipped: %d\n", installed, skipped);
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
        "\n"
        "The manifest format is a small TOML subset (string values only,\n"
        "top-level keys + a single [dependencies] table). Registry\n"
        "fetching is not wired up yet; for now use `path:./local/dir`\n"
        "as the version spec to vendor a sibling package.\n");
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

    std::fprintf(stderr, "tulpar pkg: unknown command '%s'\n\n", sub);
    print_usage();
    return 2;
}

}  // namespace tulpar
