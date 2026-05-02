#include "update_cmd.hpp"

#include "../common/localization.hpp"
#include "../common/version.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

namespace tulpar {

namespace {

// Where we publish releases. Change here if the repo is ever renamed.
constexpr const char *kRepo = "hamer1818/TulparLang";

// Run a shell command and capture stdout into `out`. Returns the exit
// status. Used for HTTP fetches via the platform's built-in tooling so we
// don't have to grow a TLS dependency just for the update check.
int capture_command(const std::string &cmd, std::string &out) {
    out.clear();
#ifdef _WIN32
    FILE *p = _popen(cmd.c_str(), "r");
#else
    FILE *p = popen(cmd.c_str(), "r");
#endif
    if (!p) return -1;
    char buf[1024];
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

// Re-run the published install script. Both install scripts are
// idempotent: re-running upgrades to the latest release using the same
// rename-then-replace dance the manual installer uses, which is the only
// safe way to swap a running tulpar.exe on Windows.
//
// The scripts are hosted by the website project (tulpar-lang-web's Astro
// `public/` folder) and served from the apex of tulparlang.dev. Single
// source of truth — this repo no longer carries its own copy.
int run_install_script() {
#ifdef _WIN32
    // $ProgressPreference='SilentlyContinue' kills the IWR progress bar
    // — without it, the iwr that fetches install.ps1 itself can spend
    // hundreds of ms re-rendering the bar on Windows PowerShell 5.1.
    // Defensive: install.ps1 sets this too, but only after it starts
    // executing. We need it active for the wrapping iwr as well.
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
    for (int i = 2; i < argc; i++) {
        if (std::strcmp(argv[i], "--check") == 0) {
            check_only = true;
        } else {
            std::fprintf(stderr, "%s: %s\n",
                         i18n::tr_en("Bilinmeyen seçenek",
                                     "Unknown option"),
                         argv[i]);
            std::fprintf(stderr,
                         i18n::tr_en("Kullanım: tulpar update [--check]\n",
                                     "Usage: tulpar update [--check]\n"));
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
    // even though our tags include a build counter (v2.1.0.NN).
    if (latest == kVersion) {
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

    std::printf("%s\n",
                i18n::tr_en("→ İndiriliyor ve yükleniyor...",
                            "→ Downloading and installing..."));
    int rc = run_install_script();
    if (rc != 0) {
        std::fprintf(stderr, "%s (exit %d)\n",
                     i18n::tr_en("Kurulum başarısız",
                                 "Install failed"),
                     rc);
        return 1;
    }
    std::printf("%s\n",
                i18n::tr_en(
                    "✓ Güncelleme tamamlandı. Yeni terminalde tekrar deneyin.",
                    "✓ Update complete. Try again in a new terminal."));
    return 0;
}

}  // namespace tulpar
