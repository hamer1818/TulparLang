#ifndef TULPAR_PKG_TPKG_HPP
#define TULPAR_PKG_TPKG_HPP

#include <string>
#include <vector>

namespace tulpar {

// `.tpkg` is the multi-file package archive format used by the Tulpar
// registry. It's a single JSON document so the existing cJSON
// dependency covers the parser — no new vendor for tar/zip — and the
// registry server side stays trivially small (Astro Pages Functions
// can stream a JSON blob without any binary handling).
//
// Wire format:
//
//   {
//     "tpkg": 1,                    // schema version, integer
//     "name": "demo",
//     "version": "1.0.0",
//     "entry": "demo.tpr",          // optional; defaults to <name>.tpr
//     "files": [
//       { "path": "demo.tpr",       "content": "// source\n..." },
//       { "path": "lib/util.tpr",   "content": "..." }
//     ]
//   }
//
// On install, `pkg_cli` extracts each file relative to
// `tulpar_modules/<name>/`. The bundle's entry point is the file the
// import resolver picks up via `tulpar_modules/<name>/<name>.tpr`,
// which means an `entry` of `<name>.tpr` is the no-op default.
// Sibling imports (`import "util"` from `tulpar_modules/<name>/
// demo.tpr`) work via the resolver's bundle-local probe slot.
//
// Security: paths are validated to forbid leading `/`, drive letters,
// `..` components, and null bytes — extraction never writes outside
// `tulpar_modules/<name>/`. Schema version mismatch fails the parse
// rather than silently accepting unknown fields.

struct TpkgFile {
    std::string path;     // POSIX-style relative path inside the bundle
    std::string content;  // file body (UTF-8 source)
};

struct Tpkg {
    int version = 0;
    std::string name;
    std::string pkg_version;     // "1.0.0"
    std::string entry;           // empty = default to <name>.tpr
    std::vector<TpkgFile> files;
};

// Parse a `.tpkg` JSON body. Sets `out_err` and returns false on any
// malformed shape. Caller should already have a verified body
// (lockfile sha256 path); this layer only handles structure.
bool tpkg_parse(const std::string &body, Tpkg &out, std::string &out_err);

// Write a parsed bundle to `dest_dir`. `dest_dir` must already exist.
// Subdirectories implied by file paths are created on demand. Existing
// files at the same paths are overwritten (atomic replace per file).
bool tpkg_extract(const Tpkg &pkg, const std::string &dest_dir,
                  std::string &out_err);

}  // namespace tulpar

#endif
