#include "tpkg.hpp"

#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>

extern "C" {
#include "../../runtime/cJSON.h"
}

namespace tulpar {

namespace fs = std::filesystem;

// Reject paths that could escape the install directory. Catches:
//   - leading `/` or `\` (absolute)
//   - drive letters on Windows (`C:`)
//   - any `..` component (parent-traversal)
//   - null bytes (zip-slip-style payload smuggling)
//   - empty path
static bool is_safe_relative_path(const std::string &p) {
    if (p.empty()) return false;
    if (p.find('\0') != std::string::npos) return false;
    if (p[0] == '/' || p[0] == '\\') return false;
    if (p.size() >= 2 && p[1] == ':') return false;  // C:, D:, ...
    // Tokenize on `/` or `\`. Reject any segment equal to `..`.
    size_t i = 0;
    while (i < p.size()) {
        size_t j = i;
        while (j < p.size() && p[j] != '/' && p[j] != '\\') j++;
        std::string seg = p.substr(i, j - i);
        if (seg == "..") return false;
        i = (j < p.size()) ? j + 1 : j;
    }
    return true;
}

bool tpkg_parse(const std::string &body, Tpkg &out, std::string &out_err) {
    cJSON *root = cJSON_ParseWithLength(body.data(), body.size());
    if (!root) {
        out_err = "tpkg: JSON parse error";
        return false;
    }
    auto cleanup = [&](cJSON *n) { cJSON_Delete(n); };

    cJSON *jver = cJSON_GetObjectItemCaseSensitive(root, "tpkg");
    if (!cJSON_IsNumber(jver)) {
        out_err = "tpkg: missing or non-integer 'tpkg' version";
        cleanup(root);
        return false;
    }
    out.version = jver->valueint;
    if (out.version != 1) {
        out_err = "tpkg: unsupported schema version " +
                  std::to_string(out.version) + " (expected 1)";
        cleanup(root);
        return false;
    }

    auto get_str = [&](const char *key, std::string &dst,
                       bool required) -> bool {
        cJSON *j = cJSON_GetObjectItemCaseSensitive(root, key);
        if (!cJSON_IsString(j) || j->valuestring == nullptr) {
            if (required) {
                out_err = std::string("tpkg: missing or non-string '") + key + "'";
                return false;
            }
            return true;
        }
        dst = j->valuestring;
        return true;
    };

    if (!get_str("name", out.name, true) ||
        !get_str("version", out.pkg_version, true) ||
        !get_str("entry", out.entry, false)) {
        cleanup(root);
        return false;
    }

    cJSON *jfiles = cJSON_GetObjectItemCaseSensitive(root, "files");
    if (!cJSON_IsArray(jfiles)) {
        out_err = "tpkg: 'files' must be an array";
        cleanup(root);
        return false;
    }
    cJSON *jfile = nullptr;
    cJSON_ArrayForEach(jfile, jfiles) {
        if (!cJSON_IsObject(jfile)) {
            out_err = "tpkg: each 'files' entry must be an object";
            cleanup(root);
            return false;
        }
        cJSON *jpath = cJSON_GetObjectItemCaseSensitive(jfile, "path");
        cJSON *jcontent = cJSON_GetObjectItemCaseSensitive(jfile, "content");
        if (!cJSON_IsString(jpath) || jpath->valuestring == nullptr) {
            out_err = "tpkg: file entry missing 'path'";
            cleanup(root);
            return false;
        }
        if (!cJSON_IsString(jcontent) || jcontent->valuestring == nullptr) {
            out_err = std::string("tpkg: file '") + jpath->valuestring +
                      "' missing 'content'";
            cleanup(root);
            return false;
        }
        if (!is_safe_relative_path(jpath->valuestring)) {
            out_err = std::string("tpkg: refusing unsafe path '") +
                      jpath->valuestring + "' (absolute, drive letter, or '..')";
            cleanup(root);
            return false;
        }
        TpkgFile f;
        f.path = jpath->valuestring;
        f.content = jcontent->valuestring;
        out.files.push_back(std::move(f));
    }

    cleanup(root);
    return true;
}

bool tpkg_extract(const Tpkg &pkg, const std::string &dest_dir,
                  std::string &out_err) {
    fs::path base = fs::path(dest_dir);
    std::error_code ec;
    if (!fs::exists(base, ec)) {
        if (!fs::create_directories(base, ec)) {
            out_err = "tpkg: cannot create '" + dest_dir + "'";
            return false;
        }
    }
    for (const auto &f : pkg.files) {
        // Re-validate at extract time too — the in-memory struct could
        // have been mutated between parse and extract on a long-running
        // path; cheap belt-and-suspenders.
        if (!is_safe_relative_path(f.path)) {
            out_err = "tpkg: unsafe path at extract '" + f.path + "'";
            return false;
        }
        fs::path target = base / fs::path(f.path);
        fs::path parent = target.parent_path();
        if (!parent.empty() && !fs::exists(parent, ec)) {
            if (!fs::create_directories(parent, ec)) {
                out_err = "tpkg: cannot create dir '" + parent.string() + "'";
                return false;
            }
        }
        std::ofstream out(target, std::ios::binary | std::ios::trunc);
        if (!out) {
            out_err = "tpkg: cannot write '" + target.string() + "'";
            return false;
        }
        out.write(f.content.data(), (std::streamsize)f.content.size());
        if (!out) {
            out_err = "tpkg: write failed for '" + target.string() + "'";
            return false;
        }
    }
    return true;
}

}  // namespace tulpar
