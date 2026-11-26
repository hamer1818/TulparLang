// `tulpar bindgen` — C basligindan tulpar-ext.json (bkz. bindgen_cmd.hpp).
//
// Ayristirici bilerek KUCUK: C on islemcisini calistirmaz (makro acmaz),
// yalniz bildirim sozdizimini tanir. Bunun bedeli acikca raporlanir: tanimadigi
// her sey "atlandi: <gerekce>" satiri olur. Buyuk harfli tanimsiz tanimlayicilar
// (SDL_DECLSPEC, SDLCALL, ZEXPORT, SQLITE_API) gurultu makrosu sayilir ve
// atilir — boyle bir makro aslinda bir TIP ise (nadiren), fonksiyon "bilinmeyen
// tip" yerine yanlis imzayla degil, tipi eksik kaldigi icin yine atlanir.
#include "bindgen_cmd.hpp"

#include "../typeinfer/typeinfer.hpp"

#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace tulpar {
namespace {

enum class TK { Ident, Punct, Str, Num, Ellipsis };
struct Tok {
  TK k;
  std::string s;
};

// --- 1. on isleme: devam satirlari, yorumlar, # satirlari ------------------
bool is_id_char(char c) {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' || (c >= '0' && c <= '9');
}

std::string strip_source(const std::string &in, std::map<std::string, std::string> *defines,
                         std::vector<std::pair<std::string, bool>> *includes = nullptr) {
  std::string joined;
  joined.reserve(in.size());
  for (size_t i = 0; i < in.size(); i++) {
    if (in[i] == '\\' && i + 1 < in.size() && in[i + 1] == '\n') { i++; continue; }
    if (in[i] == '\\' && i + 2 < in.size() && in[i + 1] == '\r' && in[i + 2] == '\n') { i += 2; continue; }
    joined += in[i];
  }
  std::string out;
  out.reserve(joined.size());
  for (size_t i = 0; i < joined.size(); i++) {
    char c = joined[i];
    if (c == '/' && i + 1 < joined.size() && joined[i + 1] == '/') {
      while (i < joined.size() && joined[i] != '\n') i++;
      out += '\n';
      continue;
    }
    if (c == '/' && i + 1 < joined.size() && joined[i + 1] == '*') {
      i += 2;
      while (i + 1 < joined.size() && !(joined[i] == '*' && joined[i + 1] == '/')) {
        if (joined[i] == '\n') out += '\n';
        i++;
      }
      i++;
      out += ' ';
      continue;
    }
    if (c == '"' || c == '\'') {
      char q = c;
      out += c;
      for (i++; i < joined.size() && joined[i] != q; i++) {
        if (joined[i] == '\\' && i + 1 < joined.size()) out += joined[i++];
        out += joined[i];
      }
      out += q;
      continue;
    }
    out += c;
  }
  // # ile baslayan satirlar (on islemci yonergeleri) atilir. Govdesi yalniz
  // tanimlayicilardan olusan nesne-bicimli `#define AD govde` satirlari (en cok
  // 4 kelime; bos govde dahil) TOKEN YERINE KOYMA olarak toplanir:
  // `#define z_off_t off_t`, `#define ZEXPORT`, `#define z_const const`.
  // Fonksiyon-bicimli makro, sayi/ifade govdesi dikkate alinmaz.
  std::string res;
  std::istringstream ss(out);
  std::string line;
  while (std::getline(ss, line)) {
    size_t p = line.find_first_not_of(" \t");
    if (p != std::string::npos && line[p] == '#') {
      size_t q = line.find_first_not_of(" \t", p + 1);
      if (q != std::string::npos && line.compare(q, 7, "include") == 0 && includes) {
        size_t a = line.find_first_of("<\"", q + 7);
        if (a != std::string::npos) {
          char close = line[a] == '<' ? '>' : '"';
          size_t b = line.find(close, a + 1);
          if (b != std::string::npos) includes->push_back({line.substr(a + 1, b - a - 1), close == '>'});
        }
      }
      if (q != std::string::npos && line.compare(q, 6, "define") == 0 && defines) {
        size_t a = line.find_first_not_of(" \t", q + 6);
        size_t b = a;
        while (b < line.size() && is_id_char(line[b])) b++;
        if (a != std::string::npos && b > a && (b >= line.size() || line[b] != '(')) {
          std::string nm = line.substr(a, b - a), body;
          std::istringstream bs(line.substr(b));
          std::string w;
          int n = 0;
          bool ok = true;
          while (bs >> w) {
            for (char ch : w) if (!is_id_char(ch)) ok = false;
            if (!ok || ++n > 4) { ok = false; break; }
            body += (body.empty() ? "" : " ") + w;
          }
          if (ok && !(nm.size() >= 2 && nm.compare(nm.size() - 2, 2, "_H") == 0 && body.empty()))
            (*defines)[nm] = body;
        }
      }
      res += '\n';
      continue;
    }
    res += line + '\n';
  }
  return res;
}

bool is_id_start(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_'; }
bool is_id(char c) { return is_id_start(c) || (c >= '0' && c <= '9'); }

std::vector<Tok> tokenize(const std::string &s) {
  std::vector<Tok> t;
  for (size_t i = 0; i < s.size();) {
    char c = s[i];
    if (c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v') { i++; continue; }
    if (is_id_start(c)) {
      size_t j = i;
      while (j < s.size() && is_id(s[j])) j++;
      t.push_back({TK::Ident, s.substr(i, j - i)});
      i = j;
      continue;
    }
    if (c >= '0' && c <= '9') {
      size_t j = i;
      while (j < s.size() && (is_id(s[j]) || s[j] == '.')) j++;
      t.push_back({TK::Num, s.substr(i, j - i)});
      i = j;
      continue;
    }
    if (c == '"' || c == '\'') {
      char q = c;
      size_t j = i + 1;
      while (j < s.size() && s[j] != q) j += (s[j] == '\\') ? 2 : 1;
      t.push_back({TK::Str, s.substr(i, j + 1 - i)});
      i = j + 1;
      continue;
    }
    if (c == '.' && i + 2 < s.size() && s[i + 1] == '.' && s[i + 2] == '.') {
      t.push_back({TK::Ellipsis, "..."});
      i += 3;
      continue;
    }
    t.push_back({TK::Punct, std::string(1, c)});
    i++;
  }
  return t;
}

bool is_punct(const Tok &t, char c) { return t.k == TK::Punct && t.s.size() == 1 && t.s[0] == c; }

// --- 2. gurultu: nitelikler, extern "C", ozel bildirim belirtecleri --------
const std::set<std::string> &noise_words() {
  static const std::set<std::string> w = {
      "extern", "inline", "__inline", "__inline__", "__extension__", "restrict",
      "__restrict", "__restrict__", "volatile", "__volatile__", "register", "_Noreturn",
      "__THROW", "__THROWNL", "__wur", "__cdecl", "__stdcall", "__fastcall", "WINAPI",
      "__nonnull", "_Nullable", "_Nonnull", "_Null_unspecified", "__nullable", "__nonnull_",
      "__BEGIN_DECLS", "__END_DECLS", "__LEAF", "__COLD"};
  return w;
}
// `ad ( ... )` bicimli gurultu: argumaniyla birlikte atilir.
const std::set<std::string> &noise_calls() {
  static const std::set<std::string> w = {
      "__attribute__", "__attribute", "__declspec", "__asm__", "__asm", "asm",
      "__nonnull", "_Static_assert", "static_assert", "__deprecated_msg",
      "__attr_access", "__attr_dealloc", "__fortified_attr_access", "__REDIRECT",
      "__REDIRECT_NTH", "__API_AVAILABLE", "__OSX_AVAILABLE", "API_AVAILABLE",
      "__alignas_is_defined", "_Alignas", "alignas", "__typeof__", "typeof"};
  return w;
}

size_t skip_parens(const std::vector<Tok> &t, size_t i) {  // t[i] == '(' -> eslesen ')' sonrasi
  int d = 0;
  for (; i < t.size(); i++) {
    if (is_punct(t[i], '(')) d++;
    else if (is_punct(t[i], ')') && --d == 0) return i + 1;
  }
  return t.size();
}

bool all_caps_macro(const std::string &s);
std::vector<Tok> drop_noise(const std::vector<Tok> &in) {
  std::vector<Tok> out;
  std::vector<bool> transparent;  // extern "C" { ... } kapanisini at
  for (size_t i = 0; i < in.size(); i++) {
    const Tok &x = in[i];
    if (x.k == TK::Ident && x.s == "extern" && i + 1 < in.size() && in[i + 1].k == TK::Str) {
      if (i + 2 < in.size() && is_punct(in[i + 2], '{')) {
        transparent.push_back(true);
        i += 2;
      } else {
        i += 1;
      }
      continue;
    }
    if (is_punct(x, '{')) transparent.push_back(false);
    if (is_punct(x, '}') && !transparent.empty()) {
      bool tr = transparent.back();
      transparent.pop_back();
      if (tr) continue;
    }
    if (x.k == TK::Ident && noise_calls().count(x.s) && i + 1 < in.size() && is_punct(in[i + 1], '(')) {
      i = skip_parens(in, i + 1) - 1;
      continue;
    }
    if (x.k == TK::Ident && (x.s.compare(0, 11, "__attribute") == 0 || x.s.compare(0, 7, "__attr_") == 0)) {
      if (i + 1 < in.size() && is_punct(in[i + 1], '(')) i = skip_parens(in, i + 1) - 1;
      continue;
    }
    // `( SDLCALL *Ad)` — cagri kurali makrosu parantez ile yildiz arasinda.
    if (x.k == TK::Ident && all_caps_macro(x.s) && !out.empty() && is_punct(out.back(), '(') &&
        i + 1 < in.size() && is_punct(in[i + 1], '*'))
      continue;
    if (x.k == TK::Ident && noise_words().count(x.s)) {
      if (x.s == "__nonnull" && i + 1 < in.size() && is_punct(in[i + 1], '(')) i = skip_parens(in, i + 1) - 1;
      continue;
    }
    out.push_back(x);
  }
  return out;
}

// --- 3. tipler -------------------------------------------------------------
enum class K { Void, I32, I64, F32, F64, Bool8, CStr, Ptr, StructVal, Fn, Bad };
struct FnSig;
struct CT {
  K k = K::Bad;
  std::string why;       // Bad icin
  int fn = -1;           // Fn icin g_sigs indeksi
};
struct FnSig {
  CT ret;
  std::vector<CT> params;
  bool variadic = false;
};

struct Alias {
  std::vector<Tok> toks;  // typedef'in tip belirteci (yildizlar dahil)
  int fn = -1;            // fonksiyon isaretcisi typedef'i
  bool fn_is_ptr = true;  // typedef ret (*Ad)(...) (true) / typedef ret Ad(...) (false)
};

struct Ctx {
  std::map<std::string, Alias> aliases;
  std::set<std::string> structs, enums;
  std::vector<FnSig> sigs;
  std::set<std::string> macro_noise;
};

bool all_caps_macro(const std::string &s) {
  if (s.size() < 2) return false;
  bool has_alpha = false;
  for (char c : s) {
    if (c >= 'A' && c <= 'Z') has_alpha = true;
    else if (!(c == '_' || (c >= '0' && c <= '9'))) return false;
  }
  return has_alpha;
}

const std::set<std::string> &type_words() {
  static const std::set<std::string> w = {"off64_t", "loff_t", "mode_t", "pid_t", "uid_t", "gid_t",
      "void", "char", "short", "int", "long", "signed", "unsigned", "float", "double",
      "_Bool", "bool", "size_t", "ssize_t", "ptrdiff_t", "intptr_t", "uintptr_t",
      "int8_t", "int16_t", "int32_t", "int64_t", "uint8_t", "uint16_t", "uint32_t",
      "uint64_t", "wchar_t", "off_t", "time_t", "intmax_t", "uintmax_t", "FILE",
      "va_list", "__builtin_va_list", "const", "struct", "union", "enum", "char16_t",
      "char32_t", "__int64", "__int32"};
  return w;
}

bool is_type_word(const Ctx &c, const std::string &s) {
  return type_words().count(s) || c.aliases.count(s) || c.structs.count(s) || c.enums.count(s);
}

CT bad(const std::string &w) { CT r; r.k = K::Bad; r.why = w; return r; }

CT resolve(Ctx &c, std::vector<Tok> toks, int depth = 0);

int parse_fn_sig(Ctx &c, const std::vector<Tok> &ret, const std::vector<Tok> &params, int depth);

// Tip belirteci (adsiz) -> CT. Yildizlar ve [] isaretci sayilir.
CT resolve(Ctx &c, std::vector<Tok> toks, int depth) {
  if (depth > 16) return bad("typedef zinciri cok derin");
  int stars = 0;
  bool is_const = false;
  std::vector<std::string> words;
  for (size_t i = 0; i < toks.size(); i++) {
    const Tok &t = toks[i];
    if (is_punct(t, '*')) { stars++; continue; }
    if (is_punct(t, '[')) {
      stars++;
      while (i < toks.size() && !is_punct(toks[i], ']')) i++;
      continue;
    }
    if (t.k != TK::Ident) {
      if (t.k == TK::Num) continue;
      return bad("tanimlanamayan tip sozdizimi '" + t.s + "'");
    }
    if (t.s == "const") { is_const = true; continue; }
    if (!is_type_word(c, t.s) && all_caps_macro(t.s)) { c.macro_noise.insert(t.s); continue; }
    words.push_back(t.s);
  }
  if (words.empty()) return bad("tip yok");
  const std::string &w0 = words[0];
  if (w0 == "struct" || w0 == "union") {
    if (stars > 0) { CT r; r.k = K::Ptr; return r; }
    return bad(std::string("struct deger gecisi desteklenmiyor (K304): ") +
               (words.size() > 1 ? words[1] : "?") + " — isaretciyle (`ptr`) gecir");
  }
  if (w0 == "enum") {
    CT r; r.k = stars ? K::Ptr : K::I32; return r;
  }
  if (words.size() == 1 && c.aliases.count(w0)) {
    const Alias &a = c.aliases[w0];
    if (a.fn >= 0) {
      int eff = stars + (a.fn_is_ptr ? 1 : 0);
      if (eff == 1) { CT r; r.k = K::Fn; r.fn = a.fn; return r; }
      if (eff > 1) { CT r; r.k = K::Ptr; return r; }
      return bad("fonksiyon tipi isaretcisiz kullanilmis: " + w0);
    }
    std::vector<Tok> sub = a.toks;
    for (int s = 0; s < stars; s++) sub.push_back({TK::Punct, "*"});
    if (is_const) sub.insert(sub.begin(), {TK::Ident, "const"});
    return resolve(c, sub, depth + 1);
  }
  if (words.size() == 1 && (c.structs.count(w0) || w0 == "FILE")) {
    if (stars > 0) { CT r; r.k = K::Ptr; return r; }
    return bad("struct deger gecisi desteklenmiyor (K304): " + w0 + " — isaretciyle (`ptr`) gecir");
  }
  if (words.size() == 1 && c.enums.count(w0)) { CT r; r.k = stars ? K::Ptr : K::I32; return r; }
  for (const auto &w : words)
    if (!type_words().count(w)) return bad("bilinmeyen tip '" + w + "'");
  int longs = 0;
  bool has_char = false, has_float = false, has_double = false, has_void = false,
       has_bool = false, wide = false, vlist = false;
  for (const auto &w : words) {
    if (w == "long") longs++;
    else if (w == "char") has_char = true;
    else if (w == "float") has_float = true;
    else if (w == "double") has_double = true;
    else if (w == "void") has_void = true;
    else if (w == "_Bool" || w == "bool") has_bool = true;
    else if (w == "va_list" || w == "__builtin_va_list") vlist = true;
    else if (w == "int64_t" || w == "uint64_t" || w == "size_t" || w == "ssize_t" ||
             w == "ptrdiff_t" || w == "intptr_t" || w == "uintptr_t" || w == "off_t" ||
             w == "time_t" || w == "intmax_t" || w == "uintmax_t" || w == "__int64" ||
             w == "off64_t" || w == "loff_t")
      wide = true;
  }
  if (vlist) return bad("va_list desteklenmiyor");
  if (stars > 0) {
    CT r;
    // `const char *` dizgi; yazilabilir `char *` cikis tamponu olabilir -> ptr.
    r.k = (has_char && stars == 1 && words.size() == 1 && is_const) ? K::CStr : K::Ptr;
    return r;
  }
  CT r;
  if (has_void) r.k = K::Void;
  else if (has_double) r.k = longs ? K::Bad : K::F64;
  else if (has_float) r.k = K::F32;
  else if (has_bool) r.k = K::Bool8;
  else if (wide || longs >= 1) r.k = K::I64;
  else r.k = K::I32;
  if (r.k == K::Bad) r.why = "long double desteklenmiyor";
  return r;
}

// Parametre listesini `,` ile bol (derinlik 0).
std::vector<std::vector<Tok>> split_commas(const std::vector<Tok> &t) {
  std::vector<std::vector<Tok>> out(1);
  int d = 0;
  for (const auto &x : t) {
    if (is_punct(x, '(') || is_punct(x, '[')) d++;
    if (is_punct(x, ')') || is_punct(x, ']')) d--;
    if (d == 0 && is_punct(x, ',')) { out.emplace_back(); continue; }
    out.back().push_back(x);
  }
  if (out.size() == 1 && out[0].empty()) out.clear();
  return out;
}

// Tek parametre: (tip, ad). Fonksiyon isaretcisi parametresini de cozer.
CT parse_param(Ctx &c, std::vector<Tok> p, std::string &name, int depth) {
  name.clear();
  // ret ( * ad ) ( params )
  for (size_t i = 0; i + 1 < p.size(); i++) {
    if (is_punct(p[i], '(') && is_punct(p[i + 1], '*')) {
      size_t close = skip_parens(p, i);
      for (size_t j = i + 2; j + 1 < close; j++)
        if (p[j].k == TK::Ident) name = p[j].s;
      if (close < p.size() && is_punct(p[close], '(')) {
        size_t e = skip_parens(p, close);
        std::vector<Tok> ret(p.begin(), p.begin() + (long)i);
        std::vector<Tok> ps(p.begin() + (long)close + 1, p.begin() + (long)e - 1);
        int s = parse_fn_sig(c, ret, ps, depth + 1);
        if (s < 0) return bad("geri cagri imzasi cozulemedi");
        CT r; r.k = K::Fn; r.fn = s; return r;
      }
      return bad("tanimlanamayan isaretci bildirimi");
    }
  }
  // Ad: sondaki tanimlayici, tip kelimesi degilse ve tek basina degilse.
  int idents = 0;
  for (const auto &x : p) if (x.k == TK::Ident && x.s != "const") idents++;
  size_t last = p.size();
  for (size_t i = p.size(); i-- > 0;) {
    if (is_punct(p[i], ']')) { while (i > 0 && !is_punct(p[i], '[')) i--; continue; }
    if (p[i].k == TK::Ident) { last = i; break; }
    break;
  }
  if (last < p.size() && idents >= 2 && !type_words().count(p[last].s) &&
      !(c.aliases.count(p[last].s) && idents == 1)) {
    // `struct X` ikilisinin X'i ad degil.
    bool after_tag = last > 0 && p[last - 1].k == TK::Ident &&
                     (p[last - 1].s == "struct" || p[last - 1].s == "enum" || p[last - 1].s == "union");
    if (!after_tag) {
      name = p[last].s;
      p.erase(p.begin() + (long)last);
    }
  }
  return resolve(c, p, depth);
}

int parse_fn_sig(Ctx &c, const std::vector<Tok> &ret, const std::vector<Tok> &params, int depth) {
  if (depth > 8) return -1;
  FnSig s;
  s.ret = resolve(c, ret, depth);
  auto parts = split_commas(params);
  if (parts.size() == 1 && parts[0].size() == 1 && parts[0][0].k == TK::Ident && parts[0][0].s == "void")
    parts.clear();
  for (auto &pt : parts) {
    if (pt.size() == 1 && pt[0].k == TK::Ellipsis) { s.variadic = true; continue; }
    std::string nm;
    s.params.push_back(parse_param(c, pt, nm, depth));
  }
  c.sigs.push_back(s);
  return (int)c.sigs.size() - 1;
}

// --- 4. bildirim tipine -> bildirim metni -----------------------------------
const char *k_name(K k) {
  switch (k) {
  case K::Void: return "void";
  case K::I32: return "i32";
  case K::I64: return "i64";
  case K::F32: return "f32";
  case K::F64: return "f64";
  case K::Bool8: return "cbool";
  case K::CStr: return "str";
  case K::Ptr: return "ptr";
  default: return "?";
  }
}

// Geri cagri imzasi -> "fn(a, b): r" ya da gerekce.
bool cb_text(const Ctx &c, int si, std::string &out, std::string &why) {
  const FnSig &s = c.sigs[(size_t)si];
  if (s.variadic) { why = "variadic geri cagri"; return false; }
  if (s.params.size() > 8) { why = "geri cagri 8'den fazla parametre"; return false; }
  out = "fn(";
  for (size_t i = 0; i < s.params.size(); i++) {
    const CT &p = s.params[i];
    if (p.k == K::Bad) { why = "geri cagri parametresi: " + p.why; return false; }
    if (p.k == K::Fn || p.k == K::Void) { why = "geri cagri parametresi desteklenmiyor"; return false; }
    if (i) out += ", ";
    out += k_name(p.k);
  }
  out += ")";
  const CT &r = s.ret;
  if (r.k == K::Bad) { why = "geri cagri donusu: " + r.why; return false; }
  if (r.k == K::CStr) { why = "geri cagri dizgi donduremez"; return false; }
  if (r.k == K::Fn) { why = "geri cagri fonksiyon donduremez"; return false; }
  if (r.k != K::Void) out += std::string(": ") + k_name(r.k);
  return true;
}

std::string json_esc(const std::string &s) {
  std::string o;
  for (char ch : s) {
    if (ch == '"' || ch == '\\') { o += '\\'; o += ch; }
    else if (ch == '\n') o += "\\n";
    else if ((unsigned char)ch < 0x20) o += ' ';
    else o += ch;
  }
  return o;
}

std::string toks_text(const std::vector<Tok> &t, size_t a, size_t b) {
  std::string s;
  for (size_t i = a; i < b && i < t.size(); i++) {
    const std::string &x = t[i].s;
    if (!s.empty() && !(t[i].k == TK::Punct && (x == "," || x == ")" || x == "*" || x == "[" || x == "]")) &&
        s.back() != '(' && s.back() != '*')
      s += ' ';
    s += x;
  }
  return s;
}

struct Out {
  std::string name, sym, doc;
  std::vector<std::string> params;
  std::string ret;
};

void usage() {
  std::fprintf(stderr,
               "kullanim / usage: tulpar bindgen <baslik.h> [--name <ad>] [--lib <kitaplik>]...\n"
               "                  [--prefix <onek>]... [-D AD=TIP]... [-I dizin]... [--no-includes]\n"
               "                  [-o <tulpar-ext.json>]\n"
               "  --prefix: yalniz bu onekle baslayan fonksiyonlar / only functions with this prefix\n"
               "  --lib:    link kitapligi (-l<ad>), linux/macos/windows bolumlerine yazilir\n"
               "  --lib-dir: kitaplik dizini (bildirimin dizinine gore)\n"
               "  -D:       makroyu tip olarak tanit (`-D z_off_t=long`; bos = sil)\n"
               "  -o:       cikti (varsayilan stdout) / output (default stdout)\n");
}

}  // namespace

int bindgen_cmd_main(int argc, char **argv) {
  std::string header, out_path, name;
  std::vector<std::string> libs, prefixes, lib_dirs;
  std::map<std::string, std::string> user_defs;
  std::vector<std::string> inc_dirs;
  bool follow_includes = true;
  for (int i = 2; i < argc; i++) {
    std::string a = argv[i];
    auto need = [&](const char *opt) -> const char * {
      if (i + 1 >= argc) { std::fprintf(stderr, "bindgen: %s deger ister\n", opt); return nullptr; }
      return argv[++i];
    };
    if (a == "--name") { const char *v = need("--name"); if (!v) return 2; name = v; }
    else if (a == "--lib") { const char *v = need("--lib"); if (!v) return 2; libs.push_back(v); }
    else if (a == "--lib-dir") { const char *v = need("--lib-dir"); if (!v) return 2; lib_dirs.push_back(v); }
    else if (a == "--prefix") { const char *v = need("--prefix"); if (!v) return 2; prefixes.push_back(v); }
    else if (a == "-o") { const char *v = need("-o"); if (!v) return 2; out_path = v; }
    else if (a == "--no-includes") follow_includes = false;
    else if (a == "-I" || a.compare(0, 2, "-I") == 0) {
      if (a.size() > 2) inc_dirs.push_back(a.substr(2));
      else { const char *v = need("-I"); if (!v) return 2; inc_dirs.push_back(v); }
    }
    else if (a == "-D" || a.compare(0, 2, "-D") == 0) {
      std::string d = a.size() > 2 ? a.substr(2) : std::string(need("-D") ? argv[i] : "");
      if (d.empty()) return 2;
      size_t eq = d.find('=');
      user_defs[d.substr(0, eq)] = eq == std::string::npos ? "" : d.substr(eq + 1);
    }
    else if (a == "-h" || a == "--help") { usage(); return 0; }
    else if (!a.empty() && a[0] == '-') { std::fprintf(stderr, "bindgen: bilinmeyen secenek %s\n", a.c_str()); usage(); return 2; }
    else if (header.empty()) header = a;
    else { usage(); return 2; }
  }
  if (header.empty()) { usage(); return 2; }
  std::ifstream f(header, std::ios::binary);
  if (!f) { std::fprintf(stderr, "bindgen: '%s' acilamadi / cannot open\n", header.c_str()); return 2; }
  std::stringstream buf;
  buf << f.rdbuf();
  if (name.empty()) {
    std::string b = header;
    size_t sl = b.find_last_of("/\\");
    if (sl != std::string::npos) b = b.substr(sl + 1);
    size_t dot = b.find('.');
    if (dot != std::string::npos) b = b.substr(0, dot);
    for (char &ch : b) if (!is_id(ch)) ch = '_';
    name = b;
  }

  std::map<std::string, std::string> defs;
  std::vector<std::pair<std::string, bool>> incs;
  std::string src = strip_source(buf.str(), &defs, &incs);
  // #include takibi — YALNIZ TIPLER ICIN (SDL_timer.h'in Uint32'si SDL_stdinc.h'te).
  // Icerilen dosyalarin metni ana basligin ONUNE konur; ayirac tokeninden
  // once gelen fonksiyon bildirimleri baglanmaz. Arama: basligin dizini, -I,
  // acili ayraclar icin ayrica /usr/local/include ve /usr/include.
  std::string types_prefix;
  if (follow_includes) {
    std::string hdir = header;
    size_t sl = hdir.find_last_of("/\\");
    hdir = sl == std::string::npos ? std::string(".") : hdir.substr(0, sl);
    std::set<std::string> visited;
    std::vector<std::pair<std::pair<std::string, bool>, std::string>> stack;  // (inc, from_dir)
    for (auto it = incs.rbegin(); it != incs.rend(); ++it) stack.push_back({*it, hdir});
    int budget = 400;
    while (!stack.empty() && budget-- > 0) {
      auto top = stack.back();
      stack.pop_back();
      std::vector<std::string> dirs;
      if (!top.first.second) dirs.push_back(top.second);
      dirs.push_back(hdir);
      for (const auto &d : inc_dirs) dirs.push_back(d);
      dirs.push_back("/usr/local/include");
      dirs.push_back("/usr/include");
      for (const auto &d : dirs) {
        std::string cand = d + "/" + top.first.first;
        std::ifstream inc(cand, std::ios::binary);
        if (!inc) continue;
        if (visited.count(cand)) break;
        visited.insert(cand);
        std::stringstream ib;
        ib << inc.rdbuf();
        std::vector<std::pair<std::string, bool>> sub;
        std::map<std::string, std::string> subdefs;
        std::string txt = strip_source(ib.str(), &subdefs, &sub);
        for (const auto &kv : subdefs) if (!defs.count(kv.first)) defs[kv.first] = kv.second;
        types_prefix = txt + "\n" + types_prefix;  // derindeki once
        std::string cdir = cand.substr(0, cand.find_last_of('/'));
        for (auto it = sub.rbegin(); it != sub.rend(); ++it) stack.push_back({*it, cdir});
        break;
      }
    }
  }
  for (const auto &kv : user_defs) defs[kv.first] = kv.second;  // -D en son, kazanir
  std::vector<Tok> raw = tokenize(types_prefix);
  raw.push_back({TK::Ident, "__bindgen_ana__"});
  raw.push_back({TK::Punct, ";"});
  {
    std::vector<Tok> ana = tokenize(src);
    raw.insert(raw.end(), ana.begin(), ana.end());
  }
  // Yerine koyma: en cok 4 tur (A -> B -> C zinciri); kendine donen ad durur.
  for (int round = 0; round < 4; round++) {
    bool changed = false;
    std::vector<Tok> nx;
    for (const auto &t : raw) {
      // Yerlesik tip kelimeleri makroyla EZILMEZ: #if dallari degerlendirilmedigi
      // icin SDL_stdinc.h'in `#define bool unsigned char` yedegi gibi bir dal
      // `bool`u sessizce baska bir ABI'ye cevirirdi (olculdu: SDL_RemoveTimer).
      auto it = (t.k == TK::Ident && !type_words().count(t.s)) ? defs.find(t.s) : defs.end();
      if (it == defs.end() || it->second == t.s) { nx.push_back(t); continue; }
      changed = true;
      std::istringstream bs(it->second);
      std::string w;
      while (bs >> w) nx.push_back({TK::Ident, w});
    }
    raw.swap(nx);
    if (!changed) break;
  }
  std::vector<Tok> toks = drop_noise(raw);
  Ctx c;
  std::vector<Out> outs;
  std::vector<std::pair<std::string, std::string>> skipped;
  std::set<std::string> seen;
  int inline_defs = 0;
  bool in_main = false;

  // Ust duzey bildirimlere bol. `{` govdeleri: tip govdesi ise atlanir ve
  // bildirim devam eder; fonksiyon govdesi (satir ici tanim) ise bildirim atilir.
  std::vector<Tok> decl;
  for (size_t i = 0; i < toks.size(); i++) {
    const Tok &t = toks[i];
    if (is_punct(t, '{')) {
      int d = 0;
      size_t j = i;
      std::vector<Tok> body;
      for (; j < toks.size(); j++) {
        if (is_punct(toks[j], '{')) d++;
        else if (is_punct(toks[j], '}') && --d == 0) break;
        else if (d == 1) body.push_back(toks[j]);
      }
      bool fn_body = !decl.empty() && is_punct(decl.back(), ')');
      if (fn_body) {
        inline_defs++;
        decl.clear();
        i = j;
        // `static inline f() {...}` sonrasi `;` gelmez.
        continue;
      }
      // enum govdesi: sabitler tamsayi; tip adi asagida kaydedilir.
      decl.push_back({TK::Ident, "__govde__"});
      i = j;
      continue;
    }
    if (!is_punct(t, ';')) { decl.push_back(t); continue; }
    std::vector<Tok> d = decl;
    decl.clear();
    if (d.empty()) continue;
    if (d.size() == 1 && d[0].k == TK::Ident && d[0].s == "__bindgen_ana__") { in_main = true; continue; }

    // typedef
    if (d[0].k == TK::Ident && d[0].s == "typedef") {
      std::vector<Tok> rest(d.begin() + 1, d.end());
      // typedef ret (*Ad)(params)  /  typedef ret Ad(params)
      size_t lp = rest.size();
      for (size_t k = 0; k < rest.size(); k++) if (is_punct(rest[k], '(')) { lp = k; break; }
      if (lp < rest.size()) {
        bool star = lp + 1 < rest.size() && is_punct(rest[lp + 1], '*');
        if (star) {
          size_t close = skip_parens(rest, lp);
          std::string an;
          for (size_t k = lp + 2; k + 1 < close; k++) if (rest[k].k == TK::Ident) an = rest[k].s;
          if (!an.empty() && !type_words().count(an) && close < rest.size() && is_punct(rest[close], '(')) {
            size_t e = skip_parens(rest, close);
            std::vector<Tok> ret(rest.begin(), rest.begin() + (long)lp);
            std::vector<Tok> ps(rest.begin() + (long)close + 1, rest.begin() + (long)e - 1);
            Alias a;
            a.fn = parse_fn_sig(c, ret, ps, 0);
            a.fn_is_ptr = true;
            c.aliases[an] = a;
          }
        } else if (lp > 0 && rest[lp - 1].k == TK::Ident && !type_words().count(rest[lp - 1].s)) {
          std::string an = rest[lp - 1].s;
          size_t e = skip_parens(rest, lp);
          std::vector<Tok> ret(rest.begin(), rest.begin() + (long)lp - 1);
          std::vector<Tok> ps(rest.begin() + (long)lp + 1, rest.begin() + (long)e - 1);
          Alias a;
          a.fn = parse_fn_sig(c, ret, ps, 0);
          a.fn_is_ptr = false;
          c.aliases[an] = a;
        }
        continue;
      }
      // typedef <tip> Ad1, *Ad2;  (virgullu typedef: her ad ayri)
      auto parts = split_commas(rest);
      if (parts.empty() || parts[0].empty() || parts[0].back().k != TK::Ident) continue;
      std::vector<Tok> base(parts[0].begin(), parts[0].end() - 1);
      std::vector<Tok> base_clean;  // yildizsiz taban tip
      bool is_enum = false, is_struct = false;
      int first_stars = 0;
      for (const auto &x : base) {
        if (x.k == TK::Ident && x.s == "__govde__") continue;
        if (is_punct(x, '*')) { first_stars++; continue; }
        if (x.k == TK::Ident && x.s == "enum") is_enum = true;
        if (x.k == TK::Ident && (x.s == "struct" || x.s == "union")) is_struct = true;
        base_clean.push_back(x);
      }
      for (size_t pi = 0; pi < parts.size(); pi++) {
        const std::vector<Tok> &p = parts[pi];
        if (p.empty() || p.back().k != TK::Ident) continue;
        std::string an = p.back().s;
        int stars = 0;
        if (pi == 0) stars = first_stars;
        else for (const auto &x : p) if (is_punct(x, '*')) stars++;
        if (is_enum && stars == 0) { c.enums.insert(an); continue; }
        if (is_struct && stars == 0) { c.structs.insert(an); continue; }
        Alias a;
        a.toks = base_clean;
        for (int k = 0; k < stars; k++) a.toks.push_back({TK::Punct, "*"});
        bool self = a.toks.size() == 1 && a.toks[0].s == an;
        // `typedef __SIZE_TYPE__ size_t;` gibi libc ic tanimlari yerlesik tip
        // kelimelerini EZMEZ (makro govdesi bilinmediginden size_t bozulurdu).
        if (!self && !type_words().count(an)) c.aliases[an] = a;
      }
      continue;
    }
    // struct/enum/union X { ... } ;  (yalniz tip bildirimi)
    if (d[0].k == TK::Ident && (d[0].s == "struct" || d[0].s == "union" || d[0].s == "enum")) {
      bool has_paren = false;
      for (const auto &x : d) if (is_punct(x, '(')) has_paren = true;
      if (d.size() >= 2 && d[1].k == TK::Ident && d[1].s != "__govde__") {
        if (d[0].s == "enum") c.enums.insert(d[1].s);
        else c.structs.insert(d[1].s);
      }
      if (!has_paren) continue;  // degisken ya da yalniz tip
    }
    bool is_static = false;
    for (const auto &x : d) if (x.k == TK::Ident && x.s == "static") is_static = true;
    if (is_static) continue;

    // Fonksiyon bildirimi: ilk derinlik-0 `(` ve ondan onceki ad.
    size_t lp = d.size();
    for (size_t k = 0; k < d.size(); k++) if (is_punct(d[k], '(')) { lp = k; break; }
    if (lp == d.size() || lp == 0) continue;  // degisken
    if (is_punct(d[lp - 1], ')') || (lp + 1 < d.size() && is_punct(d[lp + 1], '*'))) {
      // ret (*f)(...) degiskeni / fonksiyon isaretcisi donduren fonksiyon
      std::string nm;
      for (size_t k = lp; k < d.size() && !nm.size(); k++) if (d[k].k == TK::Ident) nm = d[k].s;
      if (in_main && !nm.empty() && !seen.count(nm)) {
        bool any_prefix = prefixes.empty();
        for (const auto &p : prefixes) if (nm.compare(0, p.size(), p) == 0) any_prefix = true;
        if (any_prefix) skipped.push_back({nm, "fonksiyon isaretcisi degiskeni ya da fonksiyon donduren fonksiyon"});
        seen.insert(nm);
      }
      continue;
    }
    if (d[lp - 1].k != TK::Ident) continue;
    std::string fname = d[lp - 1].s;
    size_t e = skip_parens(d, lp);
    std::vector<Tok> ret(d.begin(), d.begin() + (long)lp - 1);
    std::vector<Tok> ps(d.begin() + (long)lp + 1, d.begin() + (long)e - 1);
    // zlib tarzi `f OF((args))`: tek buyuk harfli makro + cift parantez.
    if (e < d.size() && d[e].k == TK::Ident && e + 1 < d.size() && is_punct(d[e + 1], '(')) {
      // ad OF ((...)) degil, ad (...) MAKRO(...) — sonraki makro atilir.
    }
    if (fname == "OF" || fname == "_Z_OF") continue;
    if (!in_main) continue;  // icerilen baslik: yalniz tipleri alinir
    if (ret.empty()) continue;  // makro cagrisi gibi duran sey
    bool any_prefix = prefixes.empty();
    for (const auto &p : prefixes) if (fname.compare(0, p.size(), p) == 0) any_prefix = true;
    if (!any_prefix) continue;
    if (seen.count(fname)) continue;
    seen.insert(fname);

    std::string proto = toks_text(d, 0, e);
    // zlib: `ZEXTERN int ZEXPORT f OF((...))` -> ps tek bir `( ... )` ise ac.
    if (ps.size() >= 2 && is_punct(ps.front(), '(') && is_punct(ps.back(), ')') &&
        skip_parens(ps, 0) == ps.size())
      ps = std::vector<Tok>(ps.begin() + 1, ps.end() - 1);
    int si = parse_fn_sig(c, ret, ps, 0);
    const FnSig &s = c.sigs[(size_t)si];
    std::string why;
    Out o;
    o.sym = fname;
    o.name = fname;
    o.doc = "bindgen: " + proto;
    if (s.variadic) why = "variadic (...) desteklenmiyor";
    if (why.empty() && s.ret.k == K::Bad) why = "donus: " + s.ret.why;
    if (why.empty() && s.ret.k == K::Fn) why = "fonksiyon isaretcisi donusu desteklenmiyor";
    if (why.empty() && s.params.size() > 16) why = "16'dan fazla parametre";
    if (why.empty() && typeinfer_is_builtin_name(fname.c_str()))
      why = "Tulpar yerlesigiyle ayni ad";
    if (why.empty()) {
      o.ret = k_name(s.ret.k);
      // Parametre adlari icin parametreleri yeniden bol.
      auto parts = split_commas(ps);
      if (parts.size() == 1 && parts[0].size() == 1 && parts[0][0].s == "void") parts.clear();
      for (size_t pi = 0; pi < s.params.size() && why.empty(); pi++) {
        const CT &p = s.params[pi];
        std::string pn;
        {
          Ctx tmp = c;  // ad cikarimi icin (tip tekrar cozulur, sonuc ayni)
          if (pi < parts.size()) parse_param(tmp, parts[pi], pn, 0);
        }
        if (pn.empty() || !is_id_start(pn[0])) pn = "a" + std::to_string(pi);
        if (p.k == K::Bad) { why = "parametre " + std::to_string(pi + 1) + ": " + p.why; break; }
        if (p.k == K::Void) { why = "void parametre"; break; }
        if (p.k == K::Fn) {
          std::string t, w;
          if (!cb_text(c, p.fn, t, w)) { why = "parametre " + std::to_string(pi + 1) + ": " + w; break; }
          o.params.push_back(pn + ": " + t);
          continue;
        }
        o.params.push_back(pn + ": " + k_name(p.k));
      }
    }
    if (!why.empty()) { skipped.push_back({fname, why}); continue; }
    outs.push_back(o);
  }

  std::ostringstream js;
  js << "{\n  \"tulpar_ext\": 1,\n  \"name\": \"" << json_esc(name) << "\",\n"
     << "  \"version\": \"0.0.0\",\n"
     << "  \"description\": \"tulpar bindgen " << json_esc(header) << "\",\n"
     << "  \"functions\": [\n";
  for (size_t i = 0; i < outs.size(); i++) {
    const Out &o = outs[i];
    js << "    {\"name\": \"" << json_esc(o.name) << "\", \"params\": [";
    for (size_t k = 0; k < o.params.size(); k++)
      js << (k ? ", " : "") << "\"" << json_esc(o.params[k]) << "\"";
    js << "], \"returns\": \"" << o.ret << "\", \"doc\": \"" << json_esc(o.doc) << "\"}"
       << (i + 1 < outs.size() ? ",\n" : "\n");
  }
  js << "  ]";
  if (!libs.empty()) {
    js << ",\n  \"link\": {\n";
    const char *plats[] = {"linux", "macos", "windows"};
    for (int p = 0; p < 3; p++) {
      js << "    \"" << plats[p] << "\": {";
      if (!lib_dirs.empty()) {
        js << "\"lib_dirs\": [";
        for (size_t k = 0; k < lib_dirs.size(); k++) js << (k ? ", " : "") << "\"" << json_esc(lib_dirs[k]) << "\"";
        js << "], ";
      }
      js << "\"libs\": [";
      for (size_t k = 0; k < libs.size(); k++) js << (k ? ", " : "") << "\"" << json_esc(libs[k]) << "\"";
      js << "]}" << (p < 2 ? ",\n" : "\n");
    }
    js << "  }";
  }
  js << "\n}\n";

  if (out_path.empty()) {
    std::fputs(js.str().c_str(), stdout);
  } else {
    std::ofstream of(out_path, std::ios::binary);
    if (!of) { std::fprintf(stderr, "bindgen: '%s' yazilamadi\n", out_path.c_str()); return 2; }
    of << js.str();
  }
  for (const auto &s : skipped)
    std::fprintf(stderr, "  atlandi  %s: %s\n", s.first.c_str(), s.second.c_str());
  if (!c.macro_noise.empty()) {
    std::string m;
    for (const auto &x : c.macro_noise) m += (m.empty() ? "" : " ") + x;
    std::fprintf(stderr, "  not      gurultu makrosu sayilip atilan buyuk harfli adlar: %s\n", m.c_str());
  }
  std::fprintf(stderr,
               "bindgen: %zu fonksiyon baglandi, %zu atlandi (gerekceleri yukarida)%s / %zu bound, "
               "%zu skipped\n",
               outs.size(), skipped.size(),
               inline_defs ? (", " + std::to_string(inline_defs) + " satir ici tanim yok sayildi").c_str() : "",
               outs.size(), skipped.size());
  std::fprintf(stderr, "  not      `long` LP64 varsayimiyla i64 (Windows'ta 32 bit); uint32 donusleri i32 "
                       "(2^31 ustu negatif gorunur)\n");
  return outs.empty() ? 1 : 0;
}

}  // namespace tulpar
