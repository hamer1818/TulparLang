#include "import_resolve.hpp"

#include <filesystem>
#include <system_error>

namespace tulpar {
namespace imports {

namespace {

namespace fs = std::filesystem;

bool regular_file(const std::string &p) {
  std::error_code ec;
  return fs::is_regular_file(fs::path(p), ec) && !ec;
}

bool is_absolute(const std::string &p) {
  if (p.empty()) return false;
  if (p[0] == '/' || p[0] == '\\') return true;
  // Windows surucu harfi: `C:/x`, `C:\x`.
  return p.size() >= 2 && p[1] == ':' &&
         ((p[0] >= 'A' && p[0] <= 'Z') || (p[0] >= 'a' && p[0] <= 'z'));
}

std::string join(const std::string &dir, const std::string &name) {
  if (dir.empty()) return name;
  const char last = dir.back();
  if (last == '/' || last == '\\') return dir + name;
  return dir + "/" + name;
}

}  // namespace

std::string dir_of(const std::string &path) {
  const size_t slash = path.find_last_of("/\\");
  if (slash == std::string::npos) return "";
  if (slash == 0) return path.substr(0, 1);
  return path.substr(0, slash);
}

std::string identity(const std::string &path) {
  std::error_code ec;
  fs::path c = fs::weakly_canonical(fs::path(path), ec);
  if (ec) return path;
  return c.generic_string();
}

Resolution resolve_disk(const std::string &name, const std::string &from_dir) {
  Resolution r;
  if (name.empty()) return r;
  auto hit = [&](const std::string &p, int slot) {
    r.found = true;
    r.path = p;
    r.dir = dir_of(p);
    r.slot = slot;
  };
  if (!from_dir.empty() && !is_absolute(name)) {
    const std::string c1 = join(from_dir, name + ".tpr");
    const std::string c2 = join(from_dir, name);
    if (regular_file(c1)) hit(c1, 1);
    else if (regular_file(c2)) hit(c2, 2);
  }
  // Eski kural (calisma dizini). Yeni kural kazandiysa yalniz belirsizlik
  // denetimi icin bakilir.
  std::string legacy;
  int legacy_slot = 0;
  if (regular_file(name)) { legacy = name; legacy_slot = 3; }
  else if (regular_file(name + ".tpr")) { legacy = name + ".tpr"; legacy_slot = 4; }
  if (r.found) {
    if (!legacy.empty() && identity(legacy) != identity(r.path)) r.shadowed = legacy;
    return r;
  }
  if (!legacy.empty()) {
    hit(legacy, legacy_slot);
    return r;
  }
  const std::string c5 = "tulpar_modules/" + name + "/" + name + ".tpr";
  const std::string c6 = "tulpar_modules/" + name + ".tpr";
  if (regular_file(c5)) hit(c5, 5);
  else if (regular_file(c6)) hit(c6, 6);
  return r;
}

}  // namespace imports
}  // namespace tulpar
