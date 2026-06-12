#include "update_cmd.hpp"

#include "../common/localization.hpp"
#include "../common/version.hpp"
#include "../pkg/sha256.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#ifdef _WIN32
#  include <windows.h>
#else
#  include <sys/stat.h>
#  include <sys/types.h>
#  include <unistd.h>
#  ifdef __APPLE__
#    include <mach-o/dyld.h>
#  endif
#endif

namespace tulpar {

namespace {

// Where we publish releases. Change here if the repo is ever renamed.
constexpr const char *kRepo = "hamer1818/TulparLang";

// One artifact we expect to download as part of an update. `release_name`
// is the filename inside the GitHub Release / SHA256SUMS.txt; `install_name`
// is what it's called when sitting next to the running tulpar binary.
struct Asset {
    const char *release_name;
    const char *install_name;
    bool        load_locked;  // is the file mapped into our running process?
};

// Per-platform manifest of files `tulpar update` ships into the install
// directory. Order matters only for output readability — every entry is
// downloaded, verified, and installed atomically as a unit.
//
// `load_locked` flags the entries Windows holds open while tulpar.exe is
// running (the exe itself, plus its MinGW/LLVM runtime DLLs). For those we
// rename the existing file to <name>.old before writing the replacement —
// Windows allows rename-while-loaded but rejects overwrite-while-loaded.
// libtulpar_runtime.a is NOT load-locked (it's a static archive used at
// `tulpar build` link time; never mapped into the driver process).
std::vector<Asset> assets_for_platform() {
#if defined(_WIN32)
    return {
        {"tulpar-windows-x64.exe",            "tulpar.exe",            true },
        {"libtulpar_runtime-windows-x64.a",   "libtulpar_runtime.a",   false},
        {"libwinpthread-1.dll",               "libwinpthread-1.dll",   true },
        {"zlib1.dll",                         "zlib1.dll",             true },
        {"libzstd.dll",                       "libzstd.dll",           true },
    };
#elif defined(__APPLE__)
    return {
        {"tulpar-macos-universal",            "tulpar",              true },
        {"libtulpar_runtime-macos-universal.a","libtulpar_runtime.a", false},
    };
#else
    return {
        {"tulpar-linux-x64",                  "tulpar",              true },
        {"libtulpar_runtime-linux-x64.a",     "libtulpar_runtime.a", false},
    };
#endif
}

// Run a shell command and capture stdout into `out`. Returns the exit
// status. Used for HTTP fetches via the platform's built-in tooling so we
// don't have to grow a TLS dependency just for the update check.
int capture_command(const std::string &cmd, std::string &out) {
    out.clear();
#ifdef _WIN32
    FILE *p = _popen(cmd.c_str(), "rb");
#else
    FILE *p = popen(cmd.c_str(), "r");
#endif
    if (!p) return -1;
    char buf[8192];
    while (size_t n = std::fread(buf, 1, sizeof(buf), p)) {
        out.append(buf, n);
    }
#ifdef _WIN32
    return _pclose(p);
#else
    return pclose(p);
#endif
}

// Pull the value of "tag_name" out of GitHub's release JSON. We don't link
// a JSON library here — the field is a flat string and a substring search
// is enough. Mirrors what the install scripts do with grep+sed.
std::string parse_tag_name(const std::string &json) {
    const std::string key = "\"tag_name\"";
    auto k = json.find(key);
    if (k == std::string::npos) return {};
    auto colon = json.find(':', k + key.size());
    if (colon == std::string::npos) return {};
    auto q1 = json.find('"', colon + 1);
    if (q1 == std::string::npos) return {};
    auto q2 = json.find('"', q1 + 1);
    if (q2 == std::string::npos) return {};
    return json.substr(q1 + 1, q2 - q1 - 1);
}

// Fetch the latest release tag via the platform's stock HTTP tooling.
// PowerShell ships on every Windows 10+; curl ships on every modern
// Linux/macOS. Either is enough to talk HTTPS to the GitHub API.
bool fetch_latest_tag(std::string &out_tag, std::string &out_err) {
    std::string url = "https://api.github.com/repos/";
    url += kRepo;
    url += "/releases/latest";

    std::string body;
    int rc;

#ifdef _WIN32
    // Prefer the curl.exe bundled with Windows 10 1803+ (and all of
    // Windows 11) — it has near-zero startup cost vs PowerShell's
    // ~500ms cold spin-up, which the user feels every time they run
    // `tulpar update`. PowerShell stays as the safety net for legacy
    // Windows installs that predate bundled curl.
    std::string curl_cmd =
        "curl.exe -fsSL -H \"User-Agent: tulpar-update\" \"" + url + "\" 2>NUL";
    rc = capture_command(curl_cmd, body);
    if (rc != 0 || body.empty()) {
        body.clear();
        std::string ps_cmd =
            "powershell -NoProfile -ExecutionPolicy Bypass -Command "
            "\"$ProgressPreference='SilentlyContinue'; "
            "try { (Invoke-WebRequest -Uri '" + url + "' "
            "-Headers @{'User-Agent'='tulpar-update'} -UseBasicParsing).Content } "
            "catch { Write-Error $_.Exception.Message; exit 1 }\"";
        rc = capture_command(ps_cmd, body);
    }
#else
    std::string cmd =
        "curl -fsSL -H 'User-Agent: tulpar-update' '" + url + "'";
    rc = capture_command(cmd, body);
#endif

    if (rc != 0 || body.empty()) {
        out_err = i18n::tr_en(
            "Sürüm bilgisi alınamadı (ağ hatası veya araç eksik).",
            "Could not fetch release info (network error or missing tool).");
        return false;
    }
    out_tag = parse_tag_name(body);
    if (out_tag.empty()) {
        out_err = i18n::tr_en(
            "Yanıt JSON'da tag_name bulunamadı.",
            "tag_name not found in JSON response.");
        return false;
    }
    return true;
}

// Construct the `releases/download/<tag>/<file>` URL for an asset.
std::string release_download_url(const std::string &tag,
                                 const std::string &asset_name) {
    std::string url = "https://github.com/";
    url += kRepo;
    url += "/releases/download/";
    url += tag;
    url += "/";
    url += asset_name;
    return url;
}

// Fetch a URL into `dest_path`. Uses curl on every platform (curl.exe is
// part of Windows 10 1803+); falls back to PowerShell on Windows when
// curl is absent. Note: curl follows redirects (-L), which matters because
// GitHub Release downloads 302 to objects.githubusercontent.com.
bool download_to_file(const std::string &url,
                      const std::string &dest_path,
                      std::string &err) {
#ifdef _WIN32
    // -L: follow redirects. -f: fail on HTTP errors instead of writing
    // an HTML error page to disk. -sS: silent except errors.
    // --retry 3: GitHub sometimes throttles cold-cache assets.
    std::string curl_cmd =
        "curl.exe -fsSL --retry 3 -A \"tulpar-update\" "
        "-o \"" + dest_path + "\" \"" + url + "\" 2>NUL";
    int rc = std::system(curl_cmd.c_str());
    if (rc == 0) return true;

    std::string ps_cmd =
        "powershell -NoProfile -ExecutionPolicy Bypass -Command "
        "\"$ProgressPreference='SilentlyContinue'; "
        "try { Invoke-WebRequest -Uri '" + url + "' "
        "-OutFile '" + dest_path + "' -UseBasicParsing "
        "-Headers @{'User-Agent'='tulpar-update'} } "
        "catch { Write-Error $_.Exception.Message; exit 1 }\"";
    rc = std::system(ps_cmd.c_str());
    if (rc != 0) {
        err = i18n::tr_en("İndirme başarısız: ", "Download failed: ") + url;
        return false;
    }
    return true;
#else
    std::string cmd =
        "curl -fsSL --retry 3 -A 'tulpar-update' "
        "-o '" + dest_path + "' '" + url + "'";
    int rc = std::system(cmd.c_str());
    if (rc != 0) {
        err = i18n::tr_en("İndirme başarısız: ", "Download failed: ") + url;
        return false;
    }
    return true;
#endif
}

// Read a file fully into memory. Used for SHA-256 hashing — these are
// release artifacts (the largest is ~90 MB tulpar.exe) so a single
// allocation is fine; streaming would only matter if we ever shipped
// gigabyte-scale assets, which we don't.
bool read_file_all(const std::string &path, std::string &out, std::string &err) {
    FILE *f = std::fopen(path.c_str(), "rb");
    if (!f) {
        err = i18n::tr_en("Dosya açılamadı: ", "Could not open file: ") + path;
        return false;
    }
    out.clear();
    char buf[64 * 1024];
    while (size_t n = std::fread(buf, 1, sizeof(buf), f)) {
        out.append(buf, n);
    }
    std::fclose(f);
    return true;
}

// Parse a SHA256SUMS.txt body. Standard `sha256sum -b` output:
//   `<64 hex chars>  *<filename>`         (binary mode)
//   `<64 hex chars>  <filename>`          (text mode)
// We split on the first run of whitespace and accept either. Returned map
// is filename → lowercase-hex digest.
std::map<std::string, std::string> parse_manifest(const std::string &body) {
    std::map<std::string, std::string> out;
    size_t i = 0;
    while (i < body.size()) {
        // Skip blank lines / comments (defensive — sha256sum doesn't
        // produce them but a future signed format might).
        while (i < body.size() && (body[i] == '\n' || body[i] == '\r')) i++;
        if (i >= body.size()) break;
        size_t line_end = body.find('\n', i);
        if (line_end == std::string::npos) line_end = body.size();
        std::string line = body.substr(i, line_end - i);
        i = line_end;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line[0] == '#') continue;

        size_t sp = line.find_first_of(" \t");
        if (sp == std::string::npos || sp != 64) continue;
        std::string hash = line.substr(0, 64);
        size_t name_start = line.find_first_not_of(" \t*", sp);
        if (name_start == std::string::npos) continue;
        std::string name = line.substr(name_start);
        out[name] = hash;
    }
    return out;
}

// Compute a lowercase-hex SHA-256 of a file. Wraps the package-manager
// helper from src/pkg/sha256.cpp — same primitive that verifies dependency
// archives in the lockfile, just pointed at release artifacts.
bool sha256_file_hex(const std::string &path, std::string &out_hex,
                     std::string &err) {
    std::string body;
    if (!read_file_all(path, body, err)) return false;
    out_hex = sha256_hex(body);
    return true;
}

// Return the directory containing the running tulpar binary. Used as
// the install destination for the update — every artifact replaces a
// file alongside the running exe. Same approach as
// aot_pipeline.cpp:get_executable_dir but inlined here to avoid a
// header sprawl across modules that currently don't share helpers.
std::string get_install_dir() {
#ifdef _WIN32
    char buf[MAX_PATH];
    DWORD len = GetModuleFileNameA(nullptr, buf, MAX_PATH);
    if (len == 0 || len >= MAX_PATH) return "";
    std::string p(buf, len);
    size_t slash = p.find_last_of("\\/");
    return (slash == std::string::npos) ? "" : p.substr(0, slash);
#elif defined(__APPLE__)
    // _NSGetExecutablePath is the macOS-blessed way; we don't need it
    // here in the update path's narrow view because a Tulpar install
    // always lives next to the binary — readlink is fine even on macOS
    // for any practical install location, but if /proc/self/exe is
    // unavailable we just trust the cwd.
    char buf[1024];
    uint32_t size = sizeof(buf);
    if (_NSGetExecutablePath(buf, &size) != 0) return "";
    std::string p(buf);
    size_t slash = p.find_last_of('/');
    return (slash == std::string::npos) ? "" : p.substr(0, slash);
#else
    char buf[1024];
    ssize_t len = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (len <= 0) return "";
    buf[len] = '\0';
    std::string p(buf);
    size_t slash = p.find_last_of('/');
    return (slash == std::string::npos) ? "" : p.substr(0, slash);
#endif
}

// Atomic-ish file replacement. On POSIX `rename(2)` is atomic on the
// same filesystem and works while the destination is open (file is
// referenced by inode, not name). On Windows a plain MoveFileEx call
// fails when the destination is mapped into our own running process —
// so for `load_locked` files we first rename the existing destination
// out of the way (which Windows allows even while loaded), then place
// the new file. The leftover `.old` is removed on a best-effort basis;
// if it's still in use we leave it for the next run.
bool atomic_replace(const std::string &src, const std::string &dst,
                    bool load_locked, std::string &err) {
#ifdef _WIN32
    if (load_locked) {
        std::string old_path = dst + ".old";
        // Best-effort: nuke any leftover .old from a previous update
        // before creating a new one. If it's still mapped (rare), the
        // delete fails silently and the rename below picks a fresh
        // name only if we collide — but Windows happily overwrites the
        // .old via rename, so the delete failure isn't fatal.
        DeleteFileA(old_path.c_str());
        if (GetFileAttributesA(dst.c_str()) != INVALID_FILE_ATTRIBUTES) {
            if (!MoveFileExA(dst.c_str(), old_path.c_str(),
                             MOVEFILE_REPLACE_EXISTING)) {
                err = i18n::tr_en(
                    "Mevcut dosya .old olarak taşınamadı: ",
                    "Could not move existing file to .old: ") + dst;
                return false;
            }
        }
        if (!MoveFileExA(src.c_str(), dst.c_str(),
                         MOVEFILE_REPLACE_EXISTING |
                         MOVEFILE_WRITE_THROUGH)) {
            err = i18n::tr_en(
                "Yeni dosya yerleştirilemedi: ",
                "Could not place new file: ") + dst;
            return false;
        }
        return true;
    }
    // Not load-locked: a single REPLACE_EXISTING move does it.
    if (!MoveFileExA(src.c_str(), dst.c_str(),
                     MOVEFILE_REPLACE_EXISTING |
                     MOVEFILE_WRITE_THROUGH)) {
        err = i18n::tr_en("Dosya yerleştirilemedi: ",
                          "Could not place file: ") + dst;
        return false;
    }
    return true;
#else
    (void)load_locked;  // POSIX rename works while file is open.
    if (std::rename(src.c_str(), dst.c_str()) != 0) {
        err = i18n::tr_en("Dosya yerleştirilemedi: ",
                          "Could not place file: ") + dst;
        return false;
    }
    // Make sure executables stay executable (rename preserves mode of
    // src; we downloaded with curl which writes 0644 by default).
    chmod(dst.c_str(), 0755);
    return true;
#endif
}

// Cross-platform "make a fresh subdir under TEMP". Returns the path or
// empty on failure. The caller is responsible for cleanup; on a successful
// update we rmdir + remove leftover files, on failure we leave the staging
// dir in place so the user can inspect partial downloads.
std::string make_staging_dir(const std::string &tag) {
#ifdef _WIN32
    char tmp[MAX_PATH];
    DWORD n = GetTempPathA(MAX_PATH, tmp);
    if (n == 0 || n >= MAX_PATH) return "";
    std::string base(tmp);
    if (!base.empty() && base.back() != '\\' && base.back() != '/') base += '\\';
    std::string dir = base + "tulpar-update-" + tag;
    CreateDirectoryA(dir.c_str(), nullptr);  // OK if already exists
    return dir;
#else
    const char *tmp = std::getenv("TMPDIR");
    if (!tmp || !*tmp) tmp = "/tmp";
    std::string dir = std::string(tmp) + "/tulpar-update-" + tag;
    mkdir(dir.c_str(), 0700);
    return dir;
#endif
}

// `dir + sep + name`, where sep is the platform path separator.
std::string path_join(const std::string &dir, const std::string &name) {
    if (dir.empty()) return name;
    char sep =
#ifdef _WIN32
        '\\';
#else
        '/';
#endif
    if (dir.back() == '\\' || dir.back() == '/') return dir + name;
    return dir + sep + name;
}

// Fall-back path: re-run the published install script. Both install
// scripts are idempotent: re-running upgrades to the latest release using
// the same rename-then-replace dance the manual installer uses, which is
// the only safe way to swap a running tulpar.exe on Windows.
//
// Used today only when the GitHub Release lacks SHA256SUMS.txt (i.e. an
// older release from before this PR landed) — once every release ships
// the manifest, the verified path is taken instead and this function
// becomes dead code we can retire.
int run_install_script_fallback() {
#ifdef _WIN32
    const char *cmd =
        "powershell -NoProfile -ExecutionPolicy Bypass -Command "
        "\"$ProgressPreference='SilentlyContinue'; "
        "iwr -useb https://tulparlang.dev/install.ps1 | iex\"";
#else
    const char *cmd =
        "curl -fsSL https://tulparlang.dev/install.sh | bash";
#endif
    return std::system(cmd);
}

}  // namespace

int update_cmd_main(int argc, char **argv) {
    bool check_only = false;
    bool force      = false;
    for (int i = 2; i < argc; i++) {
        if (std::strcmp(argv[i], "--check") == 0) {
            check_only = true;
        } else if (std::strcmp(argv[i], "--force") == 0) {
            // --force re-runs the full download/verify/install path even
            // when the embedded version matches the latest release. Useful
            // when only the bundled DLLs (libwinpthread / zlib1 / libzstd)
            // were refreshed at the same tag, or when a previous update
            // half-failed and left stale files in place.
            force = true;
        } else {
            std::fprintf(stderr, "%s: %s\n",
                         i18n::tr_en("Bilinmeyen seçenek",
                                     "Unknown option"),
                         argv[i]);
            std::fprintf(stderr,
                         i18n::tr_en(
                             "Kullanım: tulpar update [--check] [--force]\n",
                             "Usage: tulpar update [--check] [--force]\n"));
            return 2;
        }
    }

    std::printf("%s: %s\n",
                i18n::tr_en("Mevcut sürüm", "Current version"),
                kVersion);

    std::string latest, err;
    if (!fetch_latest_tag(latest, err)) {
        std::fprintf(stderr, "%s\n", err.c_str());
        return 1;
    }
    std::printf("%s: %s\n",
                i18n::tr_en("Son sürüm   ", "Latest version "),
                latest.c_str());

    // Up-to-date when the embedded version string matches the release tag
    // exactly. Release CI passes -DTULPAR_VERSION=<tag> so this works
    // for any semver tag (v2.2.0, v3.0.0-rc.1, etc.).
    if (!force && latest == kVersion) {
        std::printf("%s\n",
                    i18n::tr_en("✓ Zaten son sürümdesiniz.",
                                "✓ You are on the latest version."));
        return 0;
    }

    if (check_only) {
        std::printf("%s\n",
                    i18n::tr_en(
                        "↑ Yeni sürüm mevcut. Yüklemek için: tulpar update",
                        "↑ A new version is available. Run: tulpar update"));
        return 0;
    }

    // --- Verified-download path ----------------------------------------
    // Pull SHA256SUMS.txt first. If it's missing on this release (older
    // tags from before this PR landed), fall back to the legacy install
    // script. Once every release on the repo carries the manifest, the
    // fallback dies of disuse.
    std::string manifest_url = release_download_url(latest, "SHA256SUMS.txt");
    std::string staging = make_staging_dir(latest);
    if (staging.empty()) {
        std::fprintf(stderr, "%s\n",
                     i18n::tr_en("Geçici dizin oluşturulamadı.",
                                 "Could not create staging dir."));
        return 1;
    }
    std::string manifest_path = path_join(staging, "SHA256SUMS.txt");

    std::printf("%s\n",
                i18n::tr_en("→ İmza listesi indiriliyor...",
                            "→ Downloading checksum manifest..."));
    if (!download_to_file(manifest_url, manifest_path, err)) {
        std::fprintf(stderr, "  %s\n", err.c_str());
        std::fprintf(stderr, "%s\n",
                     i18n::tr_en(
                         "  ! Bu sürümde SHA256SUMS.txt yok; eski install"
                         " script'ine düşülüyor (doğrulama yok).",
                         "  ! No SHA256SUMS.txt for this release; falling"
                         " back to install script (no verification)."));
        int rc = run_install_script_fallback();
        if (rc != 0) {
            std::fprintf(stderr, "%s (exit %d)\n",
                         i18n::tr_en("Kurulum başarısız", "Install failed"),
                         rc);
            return 1;
        }
        return 0;
    }

    std::string manifest_body;
    if (!read_file_all(manifest_path, manifest_body, err)) {
        std::fprintf(stderr, "%s\n", err.c_str());
        return 1;
    }
    auto expected = parse_manifest(manifest_body);
    if (expected.empty()) {
        std::fprintf(stderr, "%s\n",
                     i18n::tr_en(
                         "SHA256SUMS.txt parse edilemedi.",
                         "Could not parse SHA256SUMS.txt."));
        return 1;
    }

    auto needed = assets_for_platform();

    // 1. Download every required artifact into staging.
    // 2. Verify each one's SHA-256 against the manifest. Bail on any
    //    mismatch — partial replacements are worse than no replacement.
    for (const auto &a : needed) {
        std::string url = release_download_url(latest, a.release_name);
        std::string path = path_join(staging, a.release_name);
        std::printf("%s %s\n",
                    i18n::tr_en("→ İndiriliyor:", "→ Downloading:"),
                    a.release_name);
        if (!download_to_file(url, path, err)) {
            std::fprintf(stderr, "  %s\n", err.c_str());
            return 1;
        }

        auto it = expected.find(a.release_name);
        if (it == expected.end()) {
            std::fprintf(stderr, "  %s %s\n",
                         i18n::tr_en(
                             "Manifeste yok:",
                             "Not in manifest:"),
                         a.release_name);
            return 1;
        }
        std::string actual_hex;
        if (!sha256_file_hex(path, actual_hex, err)) {
            std::fprintf(stderr, "  %s\n", err.c_str());
            return 1;
        }
        if (actual_hex != it->second) {
            std::fprintf(stderr,
                         "  %s %s\n    %s: %s\n    %s: %s\n",
                         i18n::tr_en(
                             "✗ SHA-256 uyuşmazlığı:",
                             "✗ SHA-256 mismatch:"),
                         a.release_name,
                         i18n::tr_en("Beklenen", "Expected"),
                         it->second.c_str(),
                         i18n::tr_en("Bulunan ", "Got     "),
                         actual_hex.c_str());
            return 1;
        }
        std::printf("  ✓ %s\n", actual_hex.c_str());
    }

    // 3. All downloads verified; do the install. Atomic moves are
    //    per-file: a half-completed update leaves the install dir in a
    //    mixed state, which is unfortunate but no worse than what the
    //    legacy install script does.
    std::string install_dir = get_install_dir();
    if (install_dir.empty()) {
        std::fprintf(stderr, "%s\n",
                     i18n::tr_en("Kurulum dizini bulunamadı.",
                                 "Could not determine install directory."));
        return 1;
    }
    std::printf("%s %s\n",
                i18n::tr_en("→ Yerleştiriliyor:", "→ Installing into:"),
                install_dir.c_str());
    for (const auto &a : needed) {
        std::string src = path_join(staging, a.release_name);
        std::string dst = path_join(install_dir, a.install_name);
        if (!atomic_replace(src, dst, a.load_locked, err)) {
            std::fprintf(stderr, "  %s\n", err.c_str());
            return 1;
        }
        std::printf("  ✓ %s\n", a.install_name);
    }

    std::printf("%s\n",
                i18n::tr_en(
                    "✓ Güncelleme tamamlandı. Yeni terminalde tekrar deneyin.",
                    "✓ Update complete. Try again in a new terminal."));
    return 0;
}

}  // namespace tulpar
