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
