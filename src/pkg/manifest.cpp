#include "manifest.hpp"

#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>

namespace tulpar {

namespace {

std::string strip(const std::string &s) {
    size_t start = 0;
    while (start < s.size() && (s[start] == ' ' || s[start] == '\t')) start++;
    size_t end = s.size();
    while (end > start && (s[end - 1] == ' ' || s[end - 1] == '\t' ||
                            s[end - 1] == '\r')) end--;
    return s.substr(start, end - start);
}

// Parse a `"…"` TOML basic string. Supports the escape sequences the
// manifest format actually uses (`\\`, `\"`, `\n`, `\t`); others are
// passed through. Returns false if the string isn't well-formed.
bool parse_quoted_string(const std::string &line, size_t start,
                         std::string &out, size_t &consumed) {
    if (start >= line.size() || line[start] != '"') return false;
    std::string r;
    size_t i = start + 1;
    while (i < line.size()) {
        char c = line[i];
        if (c == '"') {
            consumed = i + 1;
            out = std::move(r);
            return true;
        }
        if (c == '\\' && i + 1 < line.size()) {
            char nx = line[i + 1];
            if (nx == 'n') { r.push_back('\n'); i += 2; continue; }
            if (nx == 't') { r.push_back('\t'); i += 2; continue; }
            if (nx == '\\') { r.push_back('\\'); i += 2; continue; }
            if (nx == '"') { r.push_back('"'); i += 2; continue; }
            // Unknown escape — keep verbatim so users notice via a
            // round-trip diff.
            r.push_back(c);
            r.push_back(nx);
            i += 2;
            continue;
        }
        r.push_back(c);
        i++;
    }
    return false;  // unterminated
}

std::string escape_for_toml(const std::string &s) {
    std::string out;
    out.reserve(s.size() + 2);
    for (char c : s) {
        switch (c) {
            case '\\': out += "\\\\"; break;
            case '"':  out += "\\\""; break;
            case '\n': out += "\\n";  break;
            case '\t': out += "\\t";  break;
            default:   out += c;      break;
        }
    }
    return out;
}

}  // namespace

bool manifest_parse(const std::string &source, Manifest &out,
                    std::string &out_err) {
    out = Manifest{};
    enum Section { SEC_TOP, SEC_DEPS, SEC_REGISTRY };
    Section section = SEC_TOP;

    std::istringstream in(source);
    std::string raw;
    int lineno = 0;
    while (std::getline(in, raw)) {
        lineno++;
        std::string line = strip(raw);
        if (line.empty() || line[0] == '#') continue;

        if (line[0] == '[' && line.back() == ']') {
            std::string name = strip(line.substr(1, line.size() - 2));
            if (name == "dependencies") {
                section = SEC_DEPS;
            } else if (name == "registry") {
                section = SEC_REGISTRY;
            } else {
                out_err = "line " + std::to_string(lineno) +
                          ": unknown section [" + name + "]";
                return false;
            }
            continue;
        }

        size_t eq = line.find('=');
        if (eq == std::string::npos) {
            out_err = "line " + std::to_string(lineno) +
                      ": expected `key = value`";
            return false;
        }
        std::string key = strip(line.substr(0, eq));
        std::string val_part = strip(line.substr(eq + 1));
        // strip trailing line comment ("foo" # bar)
        if (val_part.size() && val_part[0] == '"') {
            std::string val;
            size_t consumed = 0;
            if (!parse_quoted_string(val_part, 0, val, consumed)) {
                out_err = "line " + std::to_string(lineno) +
                          ": malformed string for key '" + key + "'";
                return false;
            }
            // anything after `consumed` must be whitespace or `# …`
            std::string rest = strip(val_part.substr(consumed));
            if (!rest.empty() && rest[0] != '#') {
                out_err = "line " + std::to_string(lineno) +
                          ": trailing garbage after string value";
                return false;
            }

            if (section == SEC_TOP) {
                if (key == "name") out.name = val;
                else if (key == "version") out.version = val;
                else if (key == "description") out.description = val;
                else if (key == "author") out.author = val;
                else if (key == "license") out.license = val;
                else {
                    out_err = "line " + std::to_string(lineno) +
                              ": unknown top-level key '" + key + "'";
                    return false;
                }
            } else if (section == SEC_DEPS) {
                out.dependencies.push_back({key, val});
            } else if (section == SEC_REGISTRY) {
                if (key == "url") out.registry_url = val;
                else {
                    out_err = "line " + std::to_string(lineno) +
                              ": [registry] key '" + key +
                              "' is not recognised (only 'url' for now)";
                    return false;
                }
            }
        } else {
            out_err = "line " + std::to_string(lineno) +
                      ": only string values are supported (got: '" +
                      val_part + "')";
            return false;
        }
    }
    return true;
}

bool manifest_load(const std::string &path, Manifest &out,
                   std::string &out_err) {
    std::ifstream f(path, std::ios::binary);
    if (!f) {
        out_err = "cannot open '" + path + "'";
        return false;
    }
    std::stringstream ss;
    ss << f.rdbuf();
    return manifest_parse(ss.str(), out, out_err);
}

std::string Manifest::to_toml() const {
    std::string out;
    auto kv = [&](const char *k, const std::string &v) {
        if (v.empty()) return;
        out += k;
        out += " = \"";
        out += escape_for_toml(v);
        out += "\"\n";
    };
    kv("name", name);
    kv("version", version);
    kv("description", description);
    kv("author", author);
    kv("license", license);

    if (!registry_url.empty()) {
        out += "\n[registry]\n";
        out += "url = \"";
        out += escape_for_toml(registry_url);
        out += "\"\n";
    }

    if (!dependencies.empty()) {
        out += "\n[dependencies]\n";
        for (const auto &[k, v] : dependencies) {
            out += k;
            out += " = \"";
            out += escape_for_toml(v);
            out += "\"\n";
        }
    }
    return out;
}

bool manifest_save(const std::string &path, const Manifest &manifest,
                   std::string &out_err) {
    std::string tmp_path = path + ".tmp";
    {
        std::ofstream out(tmp_path, std::ios::binary | std::ios::trunc);
        if (!out) {
            out_err = "cannot write '" + tmp_path + "'";
            return false;
        }
        std::string body = manifest.to_toml();
        out.write(body.data(), (std::streamsize)body.size());
    }
    std::remove(path.c_str());
    if (std::rename(tmp_path.c_str(), path.c_str()) != 0) {
        out_err = "rename to '" + path + "' failed";
        return false;
    }
    return true;
}

}  // namespace tulpar
