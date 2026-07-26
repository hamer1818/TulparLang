#ifndef TULPAR_PKG_MANIFEST_HPP
#define TULPAR_PKG_MANIFEST_HPP

#include <map>
#include <string>
#include <vector>

namespace tulpar {

// Tulpar package manifest. Stored on disk as `tulpar.toml`. The format
// is a deliberately tiny TOML subset — string-only values, top-level
// keys, and a single `[dependencies]` table. We don't pull in a real
// TOML parser because the format is fixed by us; the surface keeps
// evolving and a hand-written parser is easier to grow than a vendored
// dep we'd need to fork.
//
// Example file:
//
//   name = "my-api"
//   version = "0.1.0"
//   description = "Tulpar HTTP API example"
//   author = "Hamza"
//   license = "MIT"
//
//   [dependencies]
//   wings = "^0.2.0"
//   sqlite_helpers = "0.1.0"
//
//   [binaries]
//   mytool = true
//
//   [release.binaries]
//   linux-x64 = "https://example.com/releases/mytool-linux-x64"
struct Manifest {
    std::string name;
    std::string version;
    std::string description;
    std::string author;
    std::string license;
    // Registry URL used for `name = "1.2.3"` style version specs.
    // Empty means "no registry configured", in which case versioned
    // specs without `path:` or `url:` prefix are an error at install
    // time. Override via `[registry]\nurl = "..."` in tulpar.toml.
    std::string registry_url;
    // dep name -> version requirement (as written in the manifest).
    // Order is preserved via a parallel vector so `tulpar pkg list`
    // shows them in the order the user wrote them.
    std::vector<std::pair<std::string, std::string>> dependencies;
    // Top-level `strict = true` flips the typeinfer pre-pass into
    // exit-blocking mode for `tulpar` / `tulpar build` / `tulpar --vm`
    // when run from the project root. CLI `--strict` and env
    // `TULPAR_STRICT=1` still take precedence (in that order).
    bool strict_typecheck = false;
    // `[release.binaries]` — THIS package's own prebuilt binaries, one
    // per supported platform id ("linux-x64", "windows-x64",
    // "macos-universal" — same ids `tulpar update` uses), pointing at a
    // plain HTTPS download URL (e.g. a GitHub Release asset). Only
    // meaningful when publishing a binary-shipping package; consumers
    // read it back from the registry, not from their own tulpar.toml.
    std::vector<std::pair<std::string, std::string>> release_binaries;
    // `[binaries]` — consumer-side opt-in. Dependency names for which
    // `tulpar pkg install` should ALSO fetch a prebuilt binary (into
    // `tulpar_modules/<name>/bin/`) alongside the vendored .tpr source,
    // set via `tulpar pkg add <name> --binary`. Absence = source-only
    // (the default — no extra network round-trip).
    std::vector<std::string> binary_opt_in;
    // `[android]` — app identity for `tulpar build --target=android/--apk`.
    // All optional; the android pipeline falls back to its historical
    // defaults when a key is absent:
    //   package      = "com.example.mygame"   (default dev.tulparlang.game)
    //   name         = "My Game"              (launcher label; default: output base)
    //   icon         = "icon.png"             (path; default: none → system icon)
    //   orientation  = "landscape"|"portrait"|"sensor" (default landscape)
    //   version_code = "2"                    (integer string; default "1")
    //   version_name = "1.1"                  (default "1.0")
    //   assets       = "assets"               (dir copied into the APK's
    //                  assets/; raylib reads it via AAssetManager with the
    //                  same relative paths — load_texture("top.png"). Env
    //                  TULPAR_ANDROID_ASSETS overrides, mirroring the web
    //                  target's TULPAR_WEB_ASSETS.)
    std::string android_package;
    std::string android_label;
    std::string android_icon;
    std::string android_orientation;
    std::string android_version_code;
    std::string android_version_name;
    std::string android_assets;
    std::string android_splash_color;   // "#RRGGBB" — açılış/splash arka planı

    // [build] — `tulpar build` bunları CLI bayrağı verilmediğinde kullanır.
    // target: "desktop" (varsayılan) | "web" | "android" | "apk" | "aab".
    // entry:  varsayılan kaynak .tpr (manifest dizinine göreli / CWD).
    // output: varsayılan çıktı adı.
    std::string build_target;
    std::string build_entry;
    std::string build_output;

    // Round-trip serialise this manifest back to the TOML subset we
    // accept on input. Idempotent — reading a manifest, serialising it,
    // and re-reading produces the same struct.
    std::string to_toml() const;
};

// Parse `tulpar.toml` text. On failure the `out_err` string is set
// (line-numbered when possible) and the function returns false.
bool manifest_parse(const std::string &source, Manifest &out,
                    std::string &out_err);

// Convenience: read manifest from a file. Sets `out_err` on I/O or
// parse failure.
bool manifest_load(const std::string &path, Manifest &out,
                   std::string &out_err);

// Atomic write: writes to `path + ".tmp"` then renames. Returns false
// on filesystem error.
bool manifest_save(const std::string &path, const Manifest &manifest,
                   std::string &out_err);

}  // namespace tulpar

#endif  // TULPAR_PKG_MANIFEST_HPP
