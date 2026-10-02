// Yerel eklenti kaydi — bkz. extensions.hpp (tasarim, bulunma sirasi, ABI).
#include "extensions.hpp"

#include "../common/localization.hpp"
#include "../common/platform.h"
#include "../pkg/manifest.hpp"

extern "C" {
#include "../../runtime/cJSON.h"
}

#include <cerrno>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <set>
#include <sstream>
#include <sys/stat.h>

#if PLATFORM_WINDOWS
#include <direct.h>
#include <stdlib.h>
#else
#include <unistd.h>
#endif

namespace tulpar {
namespace ext {

using tulpar::i18n::tr_en;

namespace {

std::vector<std::string> g_cli_paths;
std::vector<Extension> g_exts;
std::vector<Function> g_funcs;
std::set<std::string> g_seen_manifests;  // mutlak bildirim yollari
std::set<std::string> g_seen_tomls;
bool g_default_loaded = false;
// Calisan programa (TULPAR_EXT_PATH) aktarilacak, cozulmus girdiler: --ext
// ve tulpar.toml yollari, verildikleri sirayla. Ortamdan gelenler burada
// DEGIL — ortam degiskeninin kendi ham degeri aynen korunur.
struct ChildEntry {
  std::string path;
  bool from_cli;
};
std::vector<ChildEntry> g_child_entries;

const char *kManifestName = "tulpar-ext.json";

bool is_dir(const std::string &p) {
  struct stat st;
  return stat(p.c_str(), &st) == 0 && (st.st_mode & S_IFMT) == S_IFDIR;
}
bool is_file(const std::string &p) {
  struct stat st;
  return stat(p.c_str(), &st) == 0 && (st.st_mode & S_IFMT) == S_IFREG;
}
long long mtime_of(const std::string &p) {
  struct stat st;
  if (stat(p.c_str(), &st) != 0) return 0;
  return (long long)st.st_mtime;
}
bool is_abs(const std::string &p) {
  if (p.empty()) return false;
  if (p[0] == '/' || p[0] == '\\') return true;
#if PLATFORM_WINDOWS
  if (p.size() > 1 && p[1] == ':') return true;
#endif
  return false;
}
std::string cwd() {
  char buf[4096];
#if PLATFORM_WINDOWS
  if (_getcwd(buf, sizeof buf)) return buf;
#else
  if (getcwd(buf, sizeof buf)) return buf;
#endif
  return ".";
}
std::string join(const std::string &a, const std::string &b) {
  if (a.empty() || is_abs(b)) return b;
  const char last = a.back();
  if (last == '/' || last == '\\') return a + b;
  return a + "/" + b;
}
// Mutlak + normal yol (sembolik baglar cozulur, boylece ayni dizin iki
// yazimla iki kez yuklenmez). Yol yoksa yalniz mutlaklastirilir.
std::string canonical(const std::string &p) {
#if PLATFORM_WINDOWS
  char buf[4096];
  if (_fullpath(buf, p.c_str(), sizeof buf)) {
    std::string s = buf;
    for (char &c : s)
      if (c == '\\') c = '/';
    return s;
  }
  return join(cwd(), p);
#else
  char buf[PATH_MAX];
  if (realpath(p.c_str(), buf)) return buf;
  return join(cwd(), p);
#endif
}
std::string dirname_of(const std::string &p) {
  const size_t s = p.find_last_of("/\\");
  if (s == std::string::npos) return ".";
  if (s == 0) return p.substr(0, 1);
  return p.substr(0, s);
}
bool read_file(const std::string &p, std::string &out) {
  std::ifstream in(p, std::ios::binary);
  if (!in) return false;
  std::stringstream ss;
  ss << in.rdbuf();
  out = ss.str();
  return true;
}
bool is_ident(const std::string &s) {
  if (s.empty()) return false;
  for (size_t i = 0; i < s.size(); i++) {
    const unsigned char c = (unsigned char)s[i];
    const bool alpha = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
    const bool digit = c >= '0' && c <= '9';
    if (!(alpha || (i > 0 && digit))) return false;
  }
  return true;
}
std::string trim(const std::string &s) {
  size_t a = 0, b = s.size();
  while (a < b && (s[a] == ' ' || s[a] == '\t')) a++;
  while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t')) b--;
  return s.substr(a, b - a);
}

bool parse_type(const std::string &t, bool allow_void, CType &out) {
  if (t == "i32") out = CType::I32;
  else if (t == "i64" || t == "int") out = CType::I64;
  else if (t == "f32") out = CType::F32;
  else if (t == "f64" || t == "float") out = CType::F64;
  else if (t == "bool") out = CType::Bool;
  else if (t == "str") out = CType::Str;
  else if (allow_void && t == "void") out = CType::Void;
  else return false;
  return true;
}

// "manifest: <yol>: <ileti>" — her bildirim hatasi bu kalipla basilir.
std::string merr(const std::string &path, const std::string &msg) {
  return std::string(tr_en("eklenti bildirimi", "extension manifest")) + " " +
         path + ": " + msg;
}

// cJSON hata konumundan satir numarasi.
int error_line(const std::string &src) {
  const char *e = cJSON_GetErrorPtr();
  if (!e) return 0;
  const char *b = src.c_str();
  if (e < b || e > b + src.size()) return 0;
  int line = 1;
  for (const char *p = b; p < e; p++)
    if (*p == '\n') line++;
  return line;
}

bool str_array(cJSON *arr, const std::string &path, const char *what,
               std::vector<std::string> &out, std::string &err) {
  if (!arr) return true;
  if (!cJSON_IsArray(arr)) {
    err = merr(path, std::string(what) + tr_en(" bir dizgi dizisi olmali",
                                               " must be an array of strings"));
    return false;
  }
  cJSON *it = nullptr;
  cJSON_ArrayForEach(it, arr) {
    if (!cJSON_IsString(it) || !it->valuestring) {
      err = merr(path, std::string(what) + tr_en(" bir dizgi dizisi olmali",
                                                 " must be an array of strings"));
      return false;
    }
    out.push_back(it->valuestring);
  }
  return true;
}

bool parse_link(cJSON *obj, const std::string &path, const std::string &dir,
                const char *key, LinkSpec &out, std::string &err) {
  if (!cJSON_IsObject(obj)) {
    err = merr(path, std::string("link.") + key +
                         tr_en(" bir nesne olmali", " must be an object"));
    return false;
  }
  out.present = true;
  std::vector<std::string> dirs;
  if (!str_array(cJSON_GetObjectItemCaseSensitive(obj, "lib_dirs"), path,
                 "link.lib_dirs", dirs, err) ||
      !str_array(cJSON_GetObjectItemCaseSensitive(obj, "libs"), path,
                 "link.libs", out.libs, err) ||
      !str_array(cJSON_GetObjectItemCaseSensitive(obj, "flags"), path,
                 "link.flags", out.flags, err))
    return false;
  for (const auto &d : dirs) out.lib_dirs.push_back(is_abs(d) ? d : join(dir, d));
  for (const auto &l : out.libs) {
    if (l.empty() || l.find_first_of(" \t\"'") != std::string::npos) {
      err = merr(path, std::string(tr_en("gecersiz kitaplik adi: '",
                                         "invalid library name: '")) +
                           l + "'");
      return false;
    }
  }
  cJSON *g = cJSON_GetObjectItemCaseSensitive(obj, "group");
  if (g) {
    if (!cJSON_IsBool(g)) {
      err = merr(path, tr_en("link.group true/false olmali",
                             "link.group must be true/false"));
      return false;
    }
    out.group = cJSON_IsTrue(g);
  }
  return true;
}

bool parse_manifest(const std::string &mpath, const char *origin,
                    std::string &err) {
  std::string src;
  if (!read_file(mpath, src)) {
    err = merr(mpath, tr_en("okunamadi", "cannot be read"));
    return false;
  }
  cJSON *root = cJSON_ParseWithLength(src.c_str(), src.size());
  if (!root) {
    const int line = error_line(src);
    err = merr(mpath, std::string(tr_en("gecerli JSON degil", "is not valid JSON")) +
                          (line > 0 ? " (" + std::string(tr_en("satir ", "line ")) +
                                          std::to_string(line) + ")"
                                    : std::string()));
    return false;
  }
  struct Free {
    cJSON *r;
    ~Free() { cJSON_Delete(r); }
  } guard{root};

  if (!cJSON_IsObject(root)) {
    err = merr(mpath, tr_en("ust duzey bir JSON nesnesi olmali",
                            "top level must be a JSON object"));
    return false;
  }
  cJSON *ver = cJSON_GetObjectItemCaseSensitive(root, "tulpar_ext");
  if (!cJSON_IsNumber(ver) || ver->valueint != 1) {
    err = merr(mpath, tr_en("\"tulpar_ext\": 1 alani yok ya da desteklenmeyen surum "
                            "(bu derleyici surum 1'i okur)",
                            "missing \"tulpar_ext\": 1 or unsupported version "
                            "(this compiler reads version 1)"));
    return false;
  }
  cJSON *jname = cJSON_GetObjectItemCaseSensitive(root, "name");
  if (!cJSON_IsString(jname) || !is_ident(jname->valuestring)) {
    err = merr(mpath, tr_en("\"name\" bir tanimlayici olmali (harf, rakam, _)",
                            "\"name\" must be an identifier (letters, digits, _)"));
    return false;
  }
  const std::string name = jname->valuestring;
  for (const auto &e : g_exts) {
    // Ayni ad daha onceki (daha yuksek oncelikli) bir yoldan geldi: o
    // kazanir. Hata degil — `--ext` ile TULPAR_EXT_PATH'teki eski kopyayi
    // ezmek tam olarak bu.
    if (e.name == name) return true;
  }

  Extension ex;
  ex.name = name;
  ex.manifest_path = mpath;
  ex.dir = dirname_of(mpath);
  ex.origin = origin;
  if (cJSON *v = cJSON_GetObjectItemCaseSensitive(root, "version"); cJSON_IsString(v))
    ex.version = v->valuestring;
  if (cJSON *d = cJSON_GetObjectItemCaseSensitive(root, "description"); cJSON_IsString(d))
    ex.description = d->valuestring;

  // modules: { "<import adi>": "<dosya>" }
  if (cJSON *mods = cJSON_GetObjectItemCaseSensitive(root, "modules")) {
    if (!cJSON_IsObject(mods)) {
      err = merr(mpath, tr_en("\"modules\" { \"ad\": \"dosya.tpr\" } nesnesi olmali",
                              "\"modules\" must be an object { \"name\": \"file.tpr\" }"));
      return false;
    }
    cJSON *m = nullptr;
    cJSON_ArrayForEach(m, mods) {
      if (!cJSON_IsString(m) || !m->string || !*m->string) {
        err = merr(mpath, tr_en("\"modules\" degerleri dosya yolu (dizgi) olmali",
                                "\"modules\" values must be file paths (strings)"));
        return false;
      }
      Module mod;
      mod.name = m->string;
      mod.path = is_abs(m->valuestring) ? m->valuestring : join(ex.dir, m->valuestring);
      if (!is_file(mod.path)) {
        err = merr(mpath, std::string(tr_en("modul dosyasi yok: ", "module file not found: ")) +
                              mod.path);
        return false;
      }
      for (const auto &e : g_exts)
        for (const auto &om : e.modules)
          if (om.name == mod.name) {
            err = merr(mpath, std::string(tr_en("modul adi '", "module name '")) + mod.name +
                                  tr_en("' baska bir eklentide de var: ", "' is also provided by extension ") +
                                  e.name + " (" + e.manifest_path + ")");
            return false;
          }
      ex.modules.push_back(mod);
    }
  }

  // link: { "linux": {...}, "macos": {...}, "windows": {...}, "android": {...}, "web": {...} }
  if (cJSON *link = cJSON_GetObjectItemCaseSensitive(root, "link")) {
    if (!cJSON_IsObject(link)) {
      err = merr(mpath, tr_en("\"link\" bir nesne olmali", "\"link\" must be an object"));
      return false;
    }
    cJSON *pl = nullptr;
    cJSON_ArrayForEach(pl, link) {
      int idx = -1;
      for (int i = 0; i < kPlatformCount; i++)
        if (pl->string && strcmp(pl->string, platform_key((Platform)i)) == 0) idx = i;
      if (idx < 0) {
        err = merr(mpath, std::string(tr_en("bilinmeyen link platformu '", "unknown link platform '")) +
                              (pl->string ? pl->string : "") +
                              "' (linux, macos, windows, android, web)");
        return false;
      }
      if (!parse_link(pl, mpath, ex.dir, pl->string, ex.link[idx], err)) return false;
    }
  }

  const int ext_index = (int)g_exts.size();
  std::vector<Function> fns;
  cJSON *jf = cJSON_GetObjectItemCaseSensitive(root, "functions");
  if (jf && !cJSON_IsArray(jf)) {
    err = merr(mpath, tr_en("\"functions\" bir dizi olmali", "\"functions\" must be an array"));
    return false;
  }
  int fi = 0;
  cJSON *f = nullptr;
  cJSON_ArrayForEach(f, jf) {
    fi++;
    const std::string where = "functions[" + std::to_string(fi - 1) + "]";
    cJSON *fn = cJSON_GetObjectItemCaseSensitive(f, "name");
    if (!cJSON_IsObject(f) || !cJSON_IsString(fn) || !is_ident(fn->valuestring)) {
      err = merr(mpath, where + tr_en(": \"name\" bir tanimlayici olmali",
                                      ": \"name\" must be an identifier"));
      return false;
    }
    Function F;
    F.name = fn->valuestring;
    F.ext_index = ext_index;
    cJSON *sym = cJSON_GetObjectItemCaseSensitive(f, "symbol");
    F.symbol = cJSON_IsString(sym) ? sym->valuestring : F.name;
    if (!is_ident(F.symbol)) {
      err = merr(mpath, where + " (" + F.name + tr_en("): \"symbol\" bir C tanimlayicisi olmali",
                                                      "): \"symbol\" must be a C identifier"));
      return false;
    }
    if (cJSON *d = cJSON_GetObjectItemCaseSensitive(f, "doc"); cJSON_IsString(d)) F.doc = d->valuestring;
    cJSON *ret = cJSON_GetObjectItemCaseSensitive(f, "returns");
    const std::string rs = cJSON_IsString(ret) ? ret->valuestring : "void";
    if (!parse_type(rs, true, F.ret)) {
      err = merr(mpath, where + " (" + F.name + "): " +
                            tr_en("bilinmeyen donus tipi '", "unknown return type '") + rs +
                            "' (void, i32, i64, int, f32, f64, float, bool, str)");
      return false;
    }
    cJSON *ps = cJSON_GetObjectItemCaseSensitive(f, "params");
    if (ps && !cJSON_IsArray(ps)) {
      err = merr(mpath, where + " (" + F.name + tr_en("): \"params\" bir dizi olmali",
                                                      "): \"params\" must be an array"));
      return false;
    }
    cJSON *p = nullptr;
    cJSON_ArrayForEach(p, ps) {
      // "ad: tip" ya da yalniz "tip"
      if (!cJSON_IsString(p)) {
        err = merr(mpath, where + " (" + F.name + tr_en("): parametre \"ad: tip\" dizgisi olmali",
                                                        "): a parameter must be a \"name: type\" string"));
        return false;
      }
      std::string s = p->valuestring, pname, ptype;
      const size_t colon = s.find(':');
      if (colon == std::string::npos) {
        ptype = trim(s);
        pname = "a" + std::to_string(F.params.size());
      } else {
        pname = trim(s.substr(0, colon));
        ptype = trim(s.substr(colon + 1));
      }
      CType t;
      if (!parse_type(ptype, false, t)) {
        err = merr(mpath, where + " (" + F.name + "): " +
                              tr_en("bilinmeyen parametre tipi '", "unknown parameter type '") + ptype +
                              "' (i32, i64, int, f32, f64, float, bool, str)");
        return false;
      }
      F.params.push_back(t);
      F.param_names.push_back(pname);
    }
    if (F.params.size() > 16) {
      err = merr(mpath, where + " (" + F.name + tr_en("): en fazla 16 parametre",
                                                      "): at most 16 parameters"));
      return false;
    }
    for (const auto &o : g_funcs)
      if (o.name == F.name) {
        err = merr(mpath, std::string(tr_en("fonksiyon '", "function '")) + F.name +
                              tr_en("' baska bir eklentide de tanimli: ", "' is also defined by extension ") +
                              g_exts[o.ext_index].name + " (" + g_exts[o.ext_index].manifest_path + ")");
        return false;
      }
    for (const auto &o : fns)
      if (o.name == F.name) {
        err = merr(mpath, std::string(tr_en("fonksiyon '", "function '")) + F.name +
                              tr_en("' iki kez tanimli", "' is defined twice"));
        return false;
      }
    fns.push_back(F);
  }
  ex.function_count = (int)fns.size();
  g_exts.push_back(ex);
  for (auto &F : fns) g_funcs.push_back(F);
  return true;
}

void split_path_list(const char *s, std::vector<std::string> &out) {
#if PLATFORM_WINDOWS
  const char sep = ';';
#else
  const char sep = ':';
#endif
  std::string cur;
  for (const char *p = s; *p; p++) {
    if (*p == sep) {
      if (!cur.empty()) out.push_back(cur);
      cur.clear();
    } else {
      cur += *p;
    }
  }
  if (!cur.empty()) out.push_back(cur);
}

bool load_toml(const std::string &toml_path, std::string &err) {
  const std::string canon = canonical(toml_path);
  if (!g_seen_tomls.insert(canon).second) return true;
  Manifest m;
  std::string merr_s;
  // Bozuk bir tulpar.toml'u ana surucu zaten "(ignoring)" diye bildiriyor;
  // burada ikinci kez soylemiyoruz.
  if (!manifest_load(toml_path, m, merr_s)) return true;
  const std::string base = dirname_of(canon);
  for (const auto &p : m.ext_paths) {
    if (!load_path(is_abs(p) ? p : join(base, p), "tulpar.toml", err)) {
      err += std::string("\n  (") + toml_path + " [ext] paths)";
      return false;
    }
  }
  return true;
}

bool load_cli_and_env(std::string &err) {
  for (const auto &p : g_cli_paths)
    if (!load_path(p, "--ext", err)) return false;
  if (const char *env = getenv("TULPAR_EXT_PATH"); env && *env) {
    std::vector<std::string> ps;
    split_path_list(env, ps);
    for (const auto &p : ps)
      if (!load_path(p, "TULPAR_EXT_PATH", err)) return false;
  }
  return true;
}

}  // namespace

const char *ctype_name(CType t) {
  switch (t) {
  case CType::Void: return "void";
  case CType::I32: return "i32";
  case CType::I64: return "i64";
  case CType::F32: return "f32";
  case CType::F64: return "f64";
  case CType::Bool: return "bool";
  case CType::Str: return "str";
  }
  return "?";
}

const char *platform_key(Platform p) {
  switch (p) {
  case Platform::Linux: return "linux";
  case Platform::MacOS: return "macos";
  case Platform::Windows: return "windows";
  case Platform::Android: return "android";
  case Platform::Web: return "web";
  }
  return "?";
}

Platform host_platform() {
#if PLATFORM_WINDOWS
  return Platform::Windows;
#elif PLATFORM_MACOS
  return Platform::MacOS;
#else
  return Platform::Linux;
#endif
}

std::string Function::signature() const {
  std::string s = name + "(";
  for (size_t i = 0; i < params.size(); i++) {
    if (i) s += ", ";
    s += param_names[i] + ": " + ctype_name(params[i]);
  }
  s += ")";
  if (ret != CType::Void) s += std::string(": ") + ctype_name(ret);
  return s;
}

int take_cli_args(int argc, char **argv) {
  bool build_mode = false;
  bool past_script = false;
  int w = 1;
  for (int r = 1; r < argc; r++) {
    const char *a = argv[r];
    if (!past_script || build_mode) {
      if (strcmp(a, "--ext") == 0) {
        if (r + 1 >= argc) {
          fprintf(stderr, "%s\n",
                  tr_en("--ext bir eklenti dizini ister: --ext <dizin>",
                        "--ext needs an extension directory: --ext <dir>"));
          return -1;
        }
        g_cli_paths.push_back(argv[++r]);
        continue;
      }
      if (strncmp(a, "--ext=", 6) == 0) {
        g_cli_paths.push_back(a + 6);
        continue;
      }
      if (strcmp(a, "build") == 0 || strcmp(a, "--build") == 0 ||
          strcmp(a, "--aot") == 0 || strcmp(a, "typecheck") == 0 ||
          strcmp(a, "analyze") == 0 || strcmp(a, "doc") == 0)
        build_mode = true;
      const size_t n = strlen(a);
      if (a[0] != '-' && n > 4 && strcmp(a + n - 4, ".tpr") == 0) past_script = true;
    }
    argv[w++] = argv[r];
  }
  argv[w] = nullptr;
  return w;
}

bool load_path(const std::string &path, const char *origin, std::string &err) {
  std::string mpath;
  if (is_dir(path)) {
    mpath = join(path, kManifestName);
    if (!is_file(mpath)) {
      err = std::string(tr_en("eklenti bulunamadi: ", "extension not found: ")) + path + " (" +
            origin + "): " + tr_en("dizinde tulpar-ext.json yok", "no tulpar-ext.json in the directory");
      return false;
    }
  } else if (is_file(path)) {
    mpath = path;
  } else {
    err = std::string(tr_en("eklenti bulunamadi: ", "extension not found: ")) + path + " (" +
          origin + "): " + tr_en("boyle bir dizin ya da dosya yok", "no such directory or file");
    return false;
  }
  mpath = canonical(mpath);
  // Calisan program icin girdi: dizin verildiyse dizin (eklentiler kaynak
  // dosyalarini dizinin altinda arar), dogrudan bir bildirim verildiyse
  // ad `tulpar-ext.json` degilse bildirimin kendisi — ic ice bir `tulpar`
  // o girdiyi yine yukleyebilsin diye.
  if (strcmp(origin, "--ext") == 0 || strcmp(origin, "tulpar.toml") == 0) {
    const std::string d = dirname_of(mpath);
    const std::string e = (is_dir(path) || mpath == join(d, kManifestName)) ? d : mpath;
    g_child_entries.push_back({e, strcmp(origin, "--ext") == 0});
  }
  if (!g_seen_manifests.insert(mpath).second) return true;
  return parse_manifest(mpath, origin, err);
}

bool load_default(std::string &err) {
  if (g_default_loaded) return true;
  g_default_loaded = true;
  if (!load_cli_and_env(err)) return false;
  if (is_file("tulpar.toml")) return load_toml("tulpar.toml", err);
  return true;
}

bool load_for_document_dir(const std::string &dir, std::string &err) {
  if (!g_default_loaded) {
    g_default_loaded = true;
    if (!load_cli_and_env(err)) return false;
  }
  std::string d = canonical(dir.empty() ? "." : dir);
  for (int guard = 0; guard < 64; guard++) {
    const std::string t = join(d, "tulpar.toml");
    if (is_file(t)) return load_toml(t, err);
    const std::string up = dirname_of(d);
    if (up == d || up.empty() || up == ".") break;
    d = up;
  }
  return true;
}

std::string child_ext_path() {
#if PLATFORM_WINDOWS
  const char sep = ';';
#else
  const char sep = ':';
#endif
  std::vector<std::string> parts;
  std::set<std::string> seen;
  auto add = [&](const std::string &p) {
    if (!p.empty() && seen.insert(p).second) parts.push_back(p);
  };
  for (const auto &e : g_child_entries)
    if (e.from_cli) add(e.path);
  if (const char *env = getenv("TULPAR_EXT_PATH"); env && *env) {
    std::vector<std::string> ps;
    split_path_list(env, ps);
    for (const auto &p : ps) add(p);
  }
  for (const auto &e : g_child_entries)
    if (!e.from_cli) add(e.path);
  std::string out;
  for (const auto &p : parts) {
    if (!out.empty()) out += sep;
    out += p;
  }
  return out;
}

void export_child_env() {
  const std::string v = child_ext_path();
  if (v.empty()) return;
  const char *cur = getenv("TULPAR_EXT_PATH");
  if (cur && v == cur) return;
#if PLATFORM_WINDOWS
  _putenv_s("TULPAR_EXT_PATH", v.c_str());
#else
  setenv("TULPAR_EXT_PATH", v.c_str(), 1);
#endif
}

const std::vector<Extension> &extensions() { return g_exts; }
const std::vector<Function> &functions() { return g_funcs; }

const Function *find_function(const char *name) {
  if (!name || g_funcs.empty()) return nullptr;
  for (const auto &f : g_funcs)
    if (f.name == name) return &f;
  return nullptr;
}

const Extension *module_owner(const char *name, std::string *path) {
  if (!name) return nullptr;
  for (const auto &e : g_exts)
    for (const auto &m : e.modules)
      if (m.name == name) {
        if (path) *path = m.path;
        return &e;
      }
  return nullptr;
}

bool read_module(const char *name, std::string &src, std::string &path,
                 std::string &dir, int *ext_index) {
  if (!name) return false;
  for (size_t i = 0; i < g_exts.size(); i++)
    for (const auto &m : g_exts[i].modules)
      if (m.name == name) {
        if (!read_file(m.path, src)) return false;
        path = m.path;
        dir = dirname_of(m.path);
        if (ext_index) *ext_index = (int)i;
        return true;
      }
  return false;
}

std::string unresolved_import_hint(const char *name) {
  std::string s;
  if (g_exts.empty()) {
    s = std::string(tr_en("  ipucu: '", "  hint: '")) + (name ? name : "") +
        tr_en("' bir yerel eklentinin moduluyse eklenti dizinini verin: "
              "--ext <dizin>, TULPAR_EXT_PATH ya da tulpar.toml [ext] paths "
              "(su an hicbir eklenti yuklu degil).",
              "' is a native extension module, pass the extension directory: "
              "--ext <dir>, TULPAR_EXT_PATH or tulpar.toml [ext] paths "
              "(no extension is loaded right now).");
  } else {
    s = tr_en("  yuklu eklentiler (hicbiri bu modulu sunmuyor):",
              "  loaded extensions (none provides this module):");
    for (const auto &e : g_exts) {
      s += "\n    " + e.name + " (" + e.manifest_path + "):";
      if (e.modules.empty()) s += tr_en(" modul yok", " no modules");
      for (const auto &m : e.modules) s += " " + m.name;
    }
  }
  return s;
}

void mark_used(int ext_index) {
  if (ext_index >= 0 && ext_index < (int)g_exts.size()) g_exts[ext_index].used = true;
}
void reset_used() {
  for (auto &e : g_exts) e.used = false;
}
bool any_used() {
  for (const auto &e : g_exts)
    if (e.used) return true;
  return false;
}

bool link_flags(Platform target, const char *abi, std::string &out,
                std::string &err) {
  out.clear();
  for (const auto &e : g_exts) {
    if (!e.used) continue;
    const LinkSpec &L = e.link[(int)target];
    if (!L.present) {
      err = std::string(tr_en("eklenti '", "extension '")) + e.name +
            tr_en("' bu hedef icin link bilgisi vermiyor: ",
                  "' has no link section for this target: ") +
            platform_key(target) + " (" + e.manifest_path + " link." + platform_key(target) + ")";
      return false;
    }
    auto subst = [&](std::string s) {
      if (!abi) return s;
      size_t p;
      while ((p = s.find("{abi}")) != std::string::npos) s.replace(p, 5, abi);
      return s;
    };
    for (const auto &d : L.lib_dirs) out += " -L\"" + subst(d) + "\"";
    // macOS ld64 arsivleri tekrar tarar; grup bayragi orada yok.
    const bool grp = L.group && target != Platform::MacOS && !L.libs.empty();
    if (grp) out += " -Wl,--start-group";
    for (const auto &l : L.libs) out += " -l" + l;
    if (grp) out += " -Wl,--end-group";
    for (const auto &f : L.flags) out += " " + subst(f);
  }
  return true;
}

long long newest_mtime() {
  long long best = 0;
  auto take = [&](long long t) {
    if (t > best) best = t;
  };
  for (const auto &e : g_exts) {
    take(mtime_of(e.manifest_path));
    for (const auto &m : e.modules) take(mtime_of(m.path));
    const LinkSpec &L = e.link[(int)host_platform()];
    for (const auto &d : L.lib_dirs)
      for (const auto &l : L.libs) {
        take(mtime_of(join(d, "lib" + l + ".a")));
        take(mtime_of(join(d, l + ".lib")));
      }
  }
  return best;
}

}  // namespace ext
}  // namespace tulpar
