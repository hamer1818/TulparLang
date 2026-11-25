// Sunucu yardimcilari: bcrypt, surec capinda paylasilan TTL'li sozluk,
// UUID v4, HTML kacisi, RFC 3339 zaman damgasi, icerik koklama, hex cozme,
// soket zaman asimi ve kapanis sinyali.
//
// NEDEN BURADA (hamza.tr tasimasi, 2026-11-25): bir Go API'sini Tulpar'a
// tasimak icin eksik kalan yerlesikler. Hepsi saf C++ (OpenSSL yok — TLS
// istemcisi runtime_net.cpp'de), ve hepsi ayni N-isaretci ABI'sini
// kullaniyor: `VMValue aot_X_ptr(VMValue *a, VMValue *b, ...)`. Codegen
// onlari `k_std_builtins` tablosundan (llvm_backend.cpp) bildirip cagiriyor,
// yani yeni bir satir = buraya fonksiyon + tabloya satir + typeinfer + LSP.
//
// AYRI DERLEME BIRIMI: runtime_net.cpp / runtime_db.cpp ile ayni sebep —
// bu nesne (ve 4 KB'lik Blowfish tablolari) yalniz program bu yerlesiklerden
// birini cagirdiginda ikiliye giriyor.
#include "../common/platform.h"
#include "../common/platform_sockets.h"
#include "vm.hpp"

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <mutex>
#include <random>
#include <string>
#include <unordered_map>

#if !defined(_WIN32)
#include <unistd.h>
#endif

extern "C" {
ObjString *aot_allocate_string(const char *chars, int length);
VMValue aot_to_string(VMValue value);
void aot_runtime_error(const char *msg);
}

namespace {

VMValue make_str(const std::string &s) {
  return VM_OBJ((Obj *)aot_allocate_string(s.data(), (int)s.size()));
}

VMValue empty_str() { return VM_OBJ((Obj *)aot_allocate_string("", 0)); }

// VMValue -> std::string. Dizgi degilse toString kurali (int 5 -> "5").
std::string as_text(const VMValue *v) {
  if (!v) return std::string();
  if (IS_STRING(*v)) {
    ObjString *s = AS_STRING(*v);
    return std::string(s->chars, (size_t)s->length);
  }
  VMValue t = aot_to_string(*v);
  if (!IS_STRING(t)) return std::string();
  ObjString *s = AS_STRING(t);
  return std::string(s->chars, (size_t)s->length);
}

int64_t as_int(const VMValue *v, int64_t fallback) {
  if (!v) return fallback;
  if (IS_INT(*v)) return AS_INT(*v);
  if (IS_FLOAT(*v)) return (int64_t)AS_FLOAT(*v);
  if (IS_BOOL(*v)) return AS_BOOL(*v) ? 1 : 0;
  return fallback;
}

// CSPRNG: password_hash / secure_token ile ayni kaynak (std::random_device —
// /dev/urandom, getrandom, BCryptGenRandom; mingw-w64 GCC 9+ de rand_s).
void random_bytes(uint8_t *out, size_t n) {
  std::random_device rd;
  size_t i = 0;
  while (i < n) {
    uint32_t w = rd();
    for (int k = 0; k < 4 && i < n; k++, i++) out[i] = (uint8_t)(w >> (8 * k));
  }
}

// ===========================================================================
// bcrypt ($2a$ / $2b$ / $2y$) — OpenBSD bcrypt.c ve golang.org/x/crypto/bcrypt
// ile ayni algoritma. Tablolar pi'den uretildi (bcrypt_tables.inc).
// ===========================================================================
#include "bcrypt_tables.inc"

struct Blowfish {
  uint32_t P[18];
  uint32_t S[4][256];
};

inline uint32_t bf_f(const Blowfish &c, uint32_t x) {
  return ((c.S[0][x >> 24] + c.S[1][(x >> 16) & 0xff]) ^ c.S[2][(x >> 8) & 0xff]) +
         c.S[3][x & 0xff];
}

inline void bf_encipher(const Blowfish &c, uint32_t &xl, uint32_t &xr) {
  uint32_t l = xl, r = xr;
  for (int i = 0; i < 16; i++) {
    l ^= c.P[i];
    r ^= bf_f(c, l);
    uint32_t t = l;
    l = r;
    r = t;
  }
  uint32_t t = l;
  l = r;
  r = t;
  r ^= c.P[16];
  l ^= c.P[17];
  xl = l;
  xr = r;
}

// Anahtar/tuz akisindan dongusel 32 bit big-endian kelime.
inline uint32_t bf_stream_word(const uint8_t *data, size_t len, size_t &pos) {
  uint32_t w = 0;
  for (int i = 0; i < 4; i++) {
    if (pos >= len) pos = 0;
    w = (w << 8) | data[pos++];
  }
  return w;
}

void bf_init(Blowfish &c) {
  memcpy(c.P, k_bf_init_p, sizeof(c.P));
  memcpy(c.S, k_bf_init_s, sizeof(c.S));
}

// Blowfish_expand0state: anahtari P'ye karistir, sonra sifirlari sifrele.
void bf_expand0(Blowfish &c, const uint8_t *key, size_t keylen) {
  size_t j = 0;
  for (int i = 0; i < 18; i++) c.P[i] ^= bf_stream_word(key, keylen, j);
  uint32_t l = 0, r = 0;
  for (int i = 0; i < 18; i += 2) {
    bf_encipher(c, l, r);
    c.P[i] = l;
    c.P[i + 1] = r;
  }
  for (int s = 0; s < 4; s++)
    for (int i = 0; i < 256; i += 2) {
      bf_encipher(c, l, r);
      c.S[s][i] = l;
      c.S[s][i + 1] = r;
    }
}

// Blowfish_expandstate: anahtar + tuz (EksBlowfishSetup'in ilk adimi).
void bf_expand_salt(Blowfish &c, const uint8_t *salt, size_t saltlen,
                    const uint8_t *key, size_t keylen) {
  size_t j = 0;
  for (int i = 0; i < 18; i++) c.P[i] ^= bf_stream_word(key, keylen, j);
  j = 0;
  uint32_t l = 0, r = 0;
  for (int i = 0; i < 18; i += 2) {
    l ^= bf_stream_word(salt, saltlen, j);
    r ^= bf_stream_word(salt, saltlen, j);
    bf_encipher(c, l, r);
    c.P[i] = l;
    c.P[i + 1] = r;
  }
  for (int s = 0; s < 4; s++)
    for (int i = 0; i < 256; i += 2) {
      l ^= bf_stream_word(salt, saltlen, j);
      r ^= bf_stream_word(salt, saltlen, j);
      bf_encipher(c, l, r);
      c.S[s][i] = l;
      c.S[s][i + 1] = r;
    }
}

const char k_bc64[] =
    "./ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789";

int bc64_index(char ch) {
  const char *p = strchr(k_bc64, ch);
  if (!p || ch == 0) return -1;
  return (int)(p - k_bc64);
}

std::string bc64_encode(const uint8_t *d, size_t n) {
  std::string out;
  size_t i = 0;
  while (i < n) {
    uint32_t c1 = d[i++];
    out += k_bc64[c1 >> 2];
    c1 = (c1 & 0x03) << 4;
    if (i >= n) {
      out += k_bc64[c1];
      break;
    }
    uint32_t c2 = d[i++];
    c1 |= c2 >> 4;
    out += k_bc64[c1];
    c1 = (c2 & 0x0f) << 2;
    if (i >= n) {
      out += k_bc64[c1];
      break;
    }
    c2 = d[i++];
    c1 |= c2 >> 6;
    out += k_bc64[c1];
    out += k_bc64[c2 & 0x3f];
  }
  return out;
}

// `n` bayti `s`'den coz; gecersiz karakterde false.
bool bc64_decode(const char *s, size_t slen, uint8_t *out, size_t n) {
  size_t o = 0, i = 0;
  while (o < n) {
    if (i >= slen) return false;
    int c1 = i < slen ? bc64_index(s[i]) : -1;
    int c2 = i + 1 < slen ? bc64_index(s[i + 1]) : -1;
    if (c1 < 0 || c2 < 0) return false;
    out[o++] = (uint8_t)((c1 << 2) | ((c2 & 0x30) >> 4));
    if (o >= n) break;
    int c3 = i + 2 < slen ? bc64_index(s[i + 2]) : -1;
    if (c3 < 0) return false;
    out[o++] = (uint8_t)(((c2 & 0x0f) << 4) | ((c3 & 0x3c) >> 2));
    if (o >= n) break;
    int c4 = i + 3 < slen ? bc64_index(s[i + 3]) : -1;
    if (c4 < 0) return false;
    out[o++] = (uint8_t)(((c3 & 0x03) << 6) | c4);
    i += 4;
  }
  return true;
}

// Ham bcrypt: 23 baytlik ozet.
void bcrypt_raw(const std::string &pw, const uint8_t salt[16], int cost,
                uint8_t out[23]) {
  // Anahtar = parola + NUL, ilk 72 bayt ($2b$ kurali; Go da boyle).
  std::string key = pw.substr(0, pw.size() > 72 ? 72 : pw.size());
  key.push_back('\0');
  const uint8_t *k = (const uint8_t *)key.data();
  size_t klen = key.size();

  Blowfish c;
  bf_init(c);
  bf_expand_salt(c, salt, 16, k, klen);
  uint64_t rounds = 1ull << cost;
  for (uint64_t r = 0; r < rounds; r++) {
    bf_expand0(c, k, klen);
    bf_expand0(c, salt, 16);
  }
  static const char magic[] = "OrpheanBeholderScryDoubt";
  uint32_t cdata[6];
  size_t j = 0;
  for (int i = 0; i < 6; i++)
    cdata[i] = bf_stream_word((const uint8_t *)magic, 24, j);
  for (int i = 0; i < 64; i++)
    for (int b = 0; b < 6; b += 2) bf_encipher(c, cdata[b], cdata[b + 1]);
  uint8_t full[24];
  for (int i = 0; i < 6; i++) {
    full[4 * i] = (uint8_t)(cdata[i] >> 24);
    full[4 * i + 1] = (uint8_t)(cdata[i] >> 16);
    full[4 * i + 2] = (uint8_t)(cdata[i] >> 8);
    full[4 * i + 3] = (uint8_t)cdata[i];
  }
  memcpy(out, full, 23);
  memset(&c, 0, sizeof(c));
}

// "$2b$12$<22 tuz><31 ozet>" — 60 karakter. Bozuksa false.
bool bcrypt_parse(const std::string &h, char &minor, int &cost,
                  uint8_t salt[16]) {
  if (h.size() != 60) return false;
  if (h[0] != '$' || h[1] != '2' || h[3] != '$' || h[6] != '$') return false;
  minor = h[2];
  if (minor != 'a' && minor != 'b' && minor != 'y') return false;
  if (h[4] < '0' || h[4] > '9' || h[5] < '0' || h[5] > '9') return false;
  cost = (h[4] - '0') * 10 + (h[5] - '0');
  if (cost < 4 || cost > 31) return false;
  for (size_t i = 7; i < 60; i++)
    if (bc64_index(h[i]) < 0) return false;
  return bc64_decode(h.data() + 7, 22, salt, 16);
}

std::string bcrypt_format(char minor, int cost, const uint8_t salt[16],
                          const uint8_t dig[23]) {
  char head[8];
  snprintf(head, sizeof(head), "$2%c$%02d$", minor, cost);
  return std::string(head) + bc64_encode(salt, 16) + bc64_encode(dig, 23);
}

// ===========================================================================
// Paylasilan TTL'li sozluk — listen_pool isci ipliklerinin hepsinin gordugu
// tek bellek ici depo. Tulpar global'i (json) iplikler arasinda kilitsiz
// paylasilir ve istek arenasiyla yasar; burasi std::string kopyasi tutar,
// 16 parcaya bolunmus kilitlerle yarissiz calisir.
// ===========================================================================
struct SharedEntry {
  std::string value;
  int64_t expires_ms;  // 0 = suresiz
};

struct SharedShard {
  std::mutex mu;
  std::unordered_map<std::string, SharedEntry> map;
  size_t next_sweep = 1024;
};

constexpr int kSharedShards = 16;
SharedShard g_shared[kSharedShards];

int64_t now_ms() {
  return (int64_t)std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::steady_clock::now().time_since_epoch())
      .count();
}

SharedShard &shard_for(const std::string &k) {
  return g_shared[std::hash<std::string>{}(k) % kSharedShards];
}

inline bool entry_live(const SharedEntry &e, int64_t now) {
  return e.expires_ms == 0 || e.expires_ms > now;
}

int64_t ttl_to_deadline(int64_t ttl_s, int64_t now) {
  if (ttl_s <= 0) return 0;
  return now + ttl_s * 1000;
}

// Buyume tetiklemeli supurme: boyut esigi astiginda suresi dolanlari sil,
// esigi yeni boyutun iki katina koy. Amortize O(1), bellek sinirli.
void maybe_sweep(SharedShard &sh, int64_t now) {
  if (sh.map.size() < sh.next_sweep) return;
  for (auto it = sh.map.begin(); it != sh.map.end();) {
    if (!entry_live(it->second, now))
      it = sh.map.erase(it);
    else
      ++it;
  }
  sh.next_sweep = sh.map.size() * 2 < 1024 ? 1024 : sh.map.size() * 2;
}

// ===========================================================================
// RFC 3339
// ===========================================================================
int64_t days_from_civil(int64_t y, unsigned m, unsigned d) {
  y -= m <= 2;
  const int64_t era = (y >= 0 ? y : y - 399) / 400;
  const unsigned yoe = (unsigned)(y - era * 400);
  const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + (int64_t)doe - 719468;
}

void civil_from_days(int64_t z, int64_t &y, unsigned &m, unsigned &d) {
  z += 719468;
  const int64_t era = (z >= 0 ? z : z - 146096) / 146097;
  const unsigned doe = (unsigned)(z - era * 146097);
  const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  y = (int64_t)yoe + era * 400;
  const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  const unsigned mp = (5 * doy + 2) / 153;
  d = doy - (153 * mp + 2) / 5 + 1;
  m = mp < 10 ? mp + 3 : mp - 9;
  y += m <= 2;
}

bool read_digits(const std::string &s, size_t &i, int n, int &out) {
  if (i + (size_t)n > s.size()) return false;
  int v = 0;
  for (int k = 0; k < n; k++) {
    char c = s[i + (size_t)k];
    if (c < '0' || c > '9') return false;
    v = v * 10 + (c - '0');
  }
  i += (size_t)n;
  out = v;
  return true;
}

std::string format_rfc3339(int64_t secs, const std::string &frac9) {
  int64_t days = secs / 86400;
  int64_t rem = secs % 86400;
  if (rem < 0) {
    rem += 86400;
    days -= 1;
  }
  int64_t y;
  unsigned mo, d;
  civil_from_days(days, y, mo, d);
  char buf[64];
  snprintf(buf, sizeof(buf), "%04lld-%02u-%02uT%02d:%02d:%02d", (long long)y,
           mo, d, (int)(rem / 3600), (int)((rem / 60) % 60), (int)(rem % 60));
  std::string out = buf;
  // Go RFC3339Nano: kesirdeki sondaki sifirlar atilir, hepsi sifirsa nokta da.
  std::string f = frac9;
  while (!f.empty() && f.back() == '0') f.pop_back();
  if (!f.empty()) out += "." + f;
  out += "Z";
  return out;
}

// "YYYY-MM-DD[ T]HH:MM[:SS[.frac]][Z|+HH:MM|+HHMM|+HH]" -> RFC 3339 UTC.
// Bolge yoksa UTC kabul edilir (SQLite CURRENT_TIMESTAMP boyle yazar).
bool parse_timestamp(const std::string &s, int64_t &secs, std::string &frac9) {
  size_t i = 0;
  int y, mo, d, h = 0, mi = 0, se = 0;
  if (!read_digits(s, i, 4, y) || i >= s.size() || s[i++] != '-') return false;
  if (!read_digits(s, i, 2, mo) || i >= s.size() || s[i++] != '-') return false;
  if (!read_digits(s, i, 2, d)) return false;
  if (mo < 1 || mo > 12 || d < 1 || d > 31) return false;
  frac9.clear();
  int off_s = 0;
  if (i < s.size()) {
    if (s[i] != 'T' && s[i] != 't' && s[i] != ' ') return false;
    i++;
    if (!read_digits(s, i, 2, h) || i >= s.size() || s[i++] != ':') return false;
    if (!read_digits(s, i, 2, mi)) return false;
    if (i < s.size() && s[i] == ':') {
      i++;
      if (!read_digits(s, i, 2, se)) return false;
      if (i < s.size() && (s[i] == '.' || s[i] == ',')) {
        i++;
        size_t st = i;
        while (i < s.size() && s[i] >= '0' && s[i] <= '9') i++;
        if (i == st) return false;
        frac9 = s.substr(st, i - st);
        if (frac9.size() > 9) frac9.resize(9);
        while (frac9.size() < 9) frac9.push_back('0');
      }
    }
    if (h > 23 || mi > 59 || se > 60) return false;
    if (i < s.size()) {
      char z = s[i];
      if (z == 'Z' || z == 'z') {
        i++;
      } else if (z == '+' || z == '-') {
        i++;
        int oh = 0, om = 0;
        if (!read_digits(s, i, 2, oh)) return false;
        if (i < s.size() && s[i] == ':') i++;
        if (i < s.size()) {
          if (!read_digits(s, i, 2, om)) return false;
        }
        if (oh > 23 || om > 59) return false;
        off_s = (oh * 3600 + om * 60) * (z == '-' ? -1 : 1);
      } else {
        return false;
      }
    }
    if (i != s.size()) return false;
  }
  secs = days_from_civil(y, (unsigned)mo, (unsigned)d) * 86400 + h * 3600 +
         mi * 60 + se - off_s;
  return true;
}

// ===========================================================================
// Icerik koklama — Go net/http DetectContentType'in (mimesniff) imza tablosu.
// Ilk 512 bayta bakilir; kimse eslesmezse ikili bayt var mi diye bakilir.
// ===========================================================================
bool is_ws(uint8_t c) {
  return c == '\t' || c == '\n' || c == '\x0c' || c == '\r' || c == ' ';
}

bool is_tt(uint8_t c) { return c == ' ' || c == '>'; }

bool has_prefix(const uint8_t *d, size_t n, const char *p, size_t pn) {
  return n >= pn && memcmp(d, p, pn) == 0;
}

// Maskeli imza: (d[i] & mask[i]) == pat[i].
bool masked(const uint8_t *d, size_t n, const uint8_t *mask, const uint8_t *pat,
            size_t pn, bool skip_ws) {
  size_t off = 0;
  if (skip_ws)
    while (off < n && is_ws(d[off])) off++;
  if (n - off < pn) return false;
  for (size_t i = 0; i < pn; i++)
    if ((d[off + i] & mask[i]) != pat[i]) return false;
  return true;
}

// HTML imzasi: bosluk atla, buyuk/kucuk harf duyarsiz etiket + bosluk ya da '>'.
bool html_sig(const uint8_t *d, size_t n, const char *tag) {
  size_t off = 0;
  while (off < n && is_ws(d[off])) off++;
  size_t tl = strlen(tag);
  if (n - off < tl + 1) return false;
  for (size_t i = 0; i < tl; i++) {
    uint8_t c = d[off + i];
    uint8_t t = (uint8_t)tag[i];
    if (t >= 'A' && t <= 'Z') c &= 0xDF;  // harfi buyut
    if (c != t) return false;
  }
  return is_tt(d[off + tl]);
}

std::string sniff(const uint8_t *d, size_t n) {
  if (n > 512) n = 512;
  static const char *html_tags[] = {
      "<!DOCTYPE HTML", "<HTML", "<HEAD", "<SCRIPT", "<IFRAME", "<H1", "<DIV",
      "<FONT", "<TABLE", "<A", "<STYLE", "<TITLE", "<B", "<BODY", "<BR", "<P",
      "<!--"};
  for (const char *t : html_tags)
    if (html_sig(d, n, t)) return "text/html; charset=utf-8";
  {
    size_t off = 0;
    while (off < n && is_ws(d[off])) off++;
    if (has_prefix(d + off, n - off, "<?xml", 5)) return "text/xml; charset=utf-8";
  }
  if (has_prefix(d, n, "%PDF-", 5)) return "application/pdf";
  if (has_prefix(d, n, "%!PS-Adobe-", 11)) return "application/postscript";
  if (has_prefix(d, n, "\xFE\xFF", 2)) return "text/plain; charset=utf-16be";
  if (has_prefix(d, n, "\xFF\xFE", 2)) return "text/plain; charset=utf-16le";
  if (has_prefix(d, n, "\xEF\xBB\xBF", 3)) return "text/plain; charset=utf-8";
  if (has_prefix(d, n, "\x00\x00\x01\x00", 4)) return "image/x-icon";
  if (has_prefix(d, n, "\x00\x00\x02\x00", 4)) return "image/x-icon";
  if (has_prefix(d, n, "BM", 2)) return "image/bmp";
  if (has_prefix(d, n, "GIF87a", 6) || has_prefix(d, n, "GIF89a", 6))
    return "image/gif";
  {
    static const uint8_t m[] = {0xFF, 0xFF, 0xFF, 0xFF, 0, 0, 0, 0,
                                0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    static const uint8_t p[] = {'R', 'I', 'F', 'F', 0, 0, 0, 0,
                                'W', 'E', 'B', 'P', 'V', 'P'};
    if (masked(d, n, m, p, sizeof(p), false)) return "image/webp";
  }
  if (has_prefix(d, n, "\x89PNG\x0D\x0A\x1A\x0A", 8)) return "image/png";
  if (has_prefix(d, n, "\xFF\xD8\xFF", 3)) return "image/jpeg";
  {
    static const uint8_t m[] = {0xFF, 0xFF, 0xFF, 0xFF, 0, 0, 0, 0,
                                0xFF, 0xFF, 0xFF, 0xFF};
    static const uint8_t p1[] = {'F', 'O', 'R', 'M', 0, 0, 0, 0,
                                 'A', 'I', 'F', 'F'};
    static const uint8_t p2[] = {'R', 'I', 'F', 'F', 0, 0, 0, 0,
                                 'W', 'A', 'V', 'E'};
    static const uint8_t p3[] = {'R', 'I', 'F', 'F', 0, 0, 0, 0,
                                 'A', 'V', 'I', ' '};
    if (masked(d, n, m, p1, 12, false)) return "audio/aiff";
    if (masked(d, n, m, p2, 12, false)) return "audio/wave";
    if (masked(d, n, m, p3, 12, false)) return "video/avi";
  }
  if (has_prefix(d, n, "ID3", 3)) return "audio/mpeg";
  if (has_prefix(d, n, "OggS\x00", 5)) return "application/ogg";
  if (has_prefix(d, n, "MThd\x00\x00\x00\x06", 8)) return "audio/midi";
  if (has_prefix(d, n, "\x1A\x45\xDF\xA3", 4)) return "video/webm";
  if (has_prefix(d, n, "wOFF", 4)) return "font/woff";
  if (has_prefix(d, n, "wOF2", 4)) return "font/woff2";
  if (has_prefix(d, n, "\x00\x01\x00\x00", 4)) return "font/ttf";
  if (has_prefix(d, n, "OTTO", 4)) return "font/otf";
  if (has_prefix(d, n, "ttcf", 4)) return "font/collection";
  if (has_prefix(d, n, "\x1F\x8B\x08", 3)) return "application/x-gzip";
  if (has_prefix(d, n, "PK\x03\x04", 4)) return "application/zip";
  if (has_prefix(d, n, "Rar!\x1A\x07\x00", 7) ||
      has_prefix(d, n, "Rar!\x1A\x07\x01\x00", 8))
    return "application/x-rar-compressed";
  if (has_prefix(d, n, "\x00\x61\x73\x6D", 4)) return "application/wasm";
  // MP4: kutu boyutu + "ftyp" + marka "mp4"/"iso"...
  if (n >= 12 && memcmp(d + 4, "ftyp", 4) == 0) {
    uint32_t box = ((uint32_t)d[0] << 24) | ((uint32_t)d[1] << 16) |
                   ((uint32_t)d[2] << 8) | d[3];
    if (box % 4 == 0 && box <= n) {
      for (size_t st = 8; st + 3 <= box; st += 4) {
        if (st == 12) continue;  // surum alani
        if (memcmp(d + st, "mp4", 3) == 0) return "video/mp4";
      }
    }
  }
  for (size_t i = 0; i < n; i++) {
    uint8_t c = d[i];
    if (c <= 0x08 || c == 0x0B || (c >= 0x0E && c <= 0x1A) ||
        (c >= 0x1C && c <= 0x1F))
      return "application/octet-stream";
  }
  return "text/plain; charset=utf-8";
}

// ===========================================================================
// Kapanis sinyali
// ===========================================================================
volatile std::sig_atomic_t g_shutdown_signal = 0;
std::once_flag g_signal_once;

void tulpar_on_shutdown_signal(int sig) {
  if (g_shutdown_signal != 0) {
    // Ikinci sinyal: kullanici gercekten cikmak istiyor (Ctrl+C iki kez).
    _exit(128 + sig);
  }
  g_shutdown_signal = sig;
}

void install_shutdown_handlers() {
#if defined(_WIN32)
  std::signal(SIGINT, tulpar_on_shutdown_signal);
  std::signal(SIGTERM, tulpar_on_shutdown_signal);
#else
  struct sigaction sa;
  memset(&sa, 0, sizeof(sa));
  sa.sa_handler = tulpar_on_shutdown_signal;
  sigemptyset(&sa.sa_mask);
  // SA_RESTART: baska ipliklerdeki recv/accept EINTR ile yarida kesilmesin;
  // kapanisi bekleyen dongu bayragi kendisi yokluyor.
  sa.sa_flags = SA_RESTART;
  sigaction(SIGTERM, &sa, nullptr);
  sigaction(SIGINT, &sa, nullptr);
#endif
}

}  // namespace

extern "C" {

// ---------------------------------------------------------------------------
// bcrypt_hash(parola: str, maliyet: int) -> str
// Maliyet 0 -> 10 (Go DefaultCost). 4..31 disi ya da 72 bayttan uzun parola
// calisma zamani hatasi: ikisi de programci hatasi ve sessizce "" donmek
// bos bir hash'i veritabanina yazdirirdi (Go da ikisinde hata donduruyor).
// ---------------------------------------------------------------------------
VMValue aot_bcrypt_hash_ptr(VMValue *pw, VMValue *costv) {
  std::string p = as_text(pw);
  int64_t cost = as_int(costv, 0);
  if (cost == 0) cost = 10;
  if (cost < 4 || cost > 31) {
    aot_runtime_error("bcrypt_hash: maliyet 4..31 araliginda olmali / cost must be 4..31");
    return empty_str();
  }
  if (p.size() > 72) {
    aot_runtime_error("bcrypt_hash: parola 72 bayttan uzun / password longer than 72 bytes");
    return empty_str();
  }
  uint8_t salt[16];
  random_bytes(salt, sizeof(salt));
  uint8_t dig[23];
  bcrypt_raw(p, salt, (int)cost, dig);
  return make_str(bcrypt_format('b', (int)cost, salt, dig));
}

// bcrypt_verify(parola, hash) -> bool. Bozuk hash hata degil, false.
// Yalniz 31 karakterlik ozet kismi sabit zamanda karsilastirilir (Go gibi):
// tuzun son karakterindeki kullanilmayan bitler yeniden kodlamada degisebilir.
VMValue aot_bcrypt_verify_ptr(VMValue *pw, VMValue *hashv) {
  if (!hashv || !IS_STRING(*hashv)) return VM_BOOL(0);
  std::string h = as_text(hashv);
  char minor;
  int cost;
  uint8_t salt[16];
  if (!bcrypt_parse(h, minor, cost, salt)) return VM_BOOL(0);
  uint8_t dig[23];
  bcrypt_raw(as_text(pw), salt, cost, dig);
  std::string mine = bc64_encode(dig, 23);
  if (mine.size() != 31) return VM_BOOL(0);
  unsigned diff = 0;
  for (size_t i = 0; i < 31; i++) diff |= (unsigned char)(mine[i] ^ h[29 + i]);
  return VM_BOOL(diff == 0);
}

// bcrypt_cost(hash) -> int; bozuk hash'te 0. Maliyet yukseltme icin
// ("eski hash 10, simdi 12 -> giriste yeniden hashle").
VMValue aot_bcrypt_cost_ptr(VMValue *hashv) {
  if (!hashv || !IS_STRING(*hashv)) return VM_INT(0);
  char minor;
  int cost;
  uint8_t salt[16];
  if (!bcrypt_parse(as_text(hashv), minor, cost, salt)) return VM_INT(0);
  return VM_INT(cost);
}

// ---------------------------------------------------------------------------
// Paylasilan sozluk. TTL saniye; <= 0 suresiz.
// ---------------------------------------------------------------------------
VMValue aot_shared_set_ptr(VMValue *k, VMValue *v, VMValue *ttl) {
  std::string key = as_text(k);
  SharedShard &sh = shard_for(key);
  int64_t now = now_ms();
  std::lock_guard<std::mutex> g(sh.mu);
  maybe_sweep(sh, now);
  sh.map[key] = SharedEntry{as_text(v), ttl_to_deadline(as_int(ttl, 0), now)};
  return VM_INT(0);
}

VMValue aot_shared_get_ptr(VMValue *k) {
  std::string key = as_text(k);
  SharedShard &sh = shard_for(key);
  int64_t now = now_ms();
  std::string out;
  {
    std::lock_guard<std::mutex> g(sh.mu);
    auto it = sh.map.find(key);
    if (it != sh.map.end() && entry_live(it->second, now)) out = it->second.value;
  }
  return make_str(out);
}

VMValue aot_shared_has_ptr(VMValue *k) {
  std::string key = as_text(k);
  SharedShard &sh = shard_for(key);
  int64_t now = now_ms();
  std::lock_guard<std::mutex> g(sh.mu);
  auto it = sh.map.find(key);
  return VM_BOOL(it != sh.map.end() && entry_live(it->second, now));
}

// Yoksa (ya da suresi dolmussa) yaz ve true; varsa dokunma, false.
// "Ayni IP+UA 12 saatte bir kez say" tekillestirmesinin tek adimi.
VMValue aot_shared_add_ptr(VMValue *k, VMValue *v, VMValue *ttl) {
  std::string key = as_text(k);
  SharedShard &sh = shard_for(key);
  int64_t now = now_ms();
  std::lock_guard<std::mutex> g(sh.mu);
  auto it = sh.map.find(key);
  if (it != sh.map.end() && entry_live(it->second, now)) return VM_BOOL(0);
  maybe_sweep(sh, now);
  sh.map[key] = SharedEntry{as_text(v), ttl_to_deadline(as_int(ttl, 0), now)};
  return VM_BOOL(1);
}

// Atomik +1. Kayit yoksa/suresi dolmussa 1 ile ve verilen TTL ile baslar;
// varsa sayaci arttirir, SURESINE DOKUNMAZ (sabit pencere). Yeni degeri doner.
VMValue aot_shared_incr_ptr(VMValue *k, VMValue *ttl) {
  std::string key = as_text(k);
  SharedShard &sh = shard_for(key);
  int64_t now = now_ms();
  std::lock_guard<std::mutex> g(sh.mu);
  auto it = sh.map.find(key);
  if (it == sh.map.end() || !entry_live(it->second, now)) {
    maybe_sweep(sh, now);
    sh.map[key] = SharedEntry{"1", ttl_to_deadline(as_int(ttl, 0), now)};
    return VM_INT(1);
  }
  int64_t n = strtoll(it->second.value.c_str(), nullptr, 10) + 1;
  it->second.value = std::to_string(n);
  return VM_INT(n);
}

VMValue aot_shared_del_ptr(VMValue *k) {
  std::string key = as_text(k);
  SharedShard &sh = shard_for(key);
  int64_t now = now_ms();
  std::lock_guard<std::mutex> g(sh.mu);
  auto it = sh.map.find(key);
  if (it == sh.map.end()) return VM_BOOL(0);
  bool live = entry_live(it->second, now);
  sh.map.erase(it);
  return VM_BOOL(live);
}

// Kalan saniye (yukari yuvarlanir); -1 suresiz, -2 yok (Redis TTL gibi).
VMValue aot_shared_ttl_ptr(VMValue *k) {
  std::string key = as_text(k);
  SharedShard &sh = shard_for(key);
  int64_t now = now_ms();
  std::lock_guard<std::mutex> g(sh.mu);
  auto it = sh.map.find(key);
  if (it == sh.map.end() || !entry_live(it->second, now)) return VM_INT(-2);
  if (it->second.expires_ms == 0) return VM_INT(-1);
  return VM_INT((it->second.expires_ms - now + 999) / 1000);
}

// ---------------------------------------------------------------------------
// Kucuk yardimcilar
// ---------------------------------------------------------------------------
VMValue aot_uuid_v4_ptr(void) {
  uint8_t b[16];
  random_bytes(b, sizeof(b));
  b[6] = (uint8_t)((b[6] & 0x0f) | 0x40);  // surum 4
  b[8] = (uint8_t)((b[8] & 0x3f) | 0x80);  // RFC 4122 varyanti
  static const char *hx = "0123456789abcdef";
  std::string out;
  out.reserve(36);
  for (int i = 0; i < 16; i++) {
    if (i == 4 || i == 6 || i == 8 || i == 10) out.push_back('-');
    out.push_back(hx[b[i] >> 4]);
    out.push_back(hx[b[i] & 0xf]);
  }
  return make_str(out);
}

// Go html.EscapeString ile ayni bes karakter.
VMValue aot_html_escape_ptr(VMValue *v) {
  std::string s = as_text(v);
  std::string out;
  out.reserve(s.size() + s.size() / 8);
  for (char c : s) {
    switch (c) {
      case '<': out += "&lt;"; break;
      case '>': out += "&gt;"; break;
      case '&': out += "&amp;"; break;
      case '\'': out += "&#39;"; break;
      case '"': out += "&#34;"; break;
      default: out.push_back(c);
    }
  }
  return make_str(out);
}

// rfc3339(x) -> str. x int ise Unix saniyesi; dizgiyse SQLite/ISO 8601
// zaman damgasi. Cikti her zaman UTC, Go'nun time.Time JSON bicimi
// (RFC3339Nano). Cozulemeyen girdi "".
VMValue aot_rfc3339_ptr(VMValue *v) {
  if (!v) return empty_str();
  if (IS_INT(*v)) return make_str(format_rfc3339(AS_INT(*v), ""));
  if (!IS_STRING(*v)) return empty_str();
  int64_t secs;
  std::string frac;
  if (!parse_timestamp(as_text(v), secs, frac)) return empty_str();
  return make_str(format_rfc3339(secs, frac));
}

VMValue aot_detect_content_type_ptr(VMValue *v) {
  std::string s = as_text(v);
  return make_str(sniff((const uint8_t *)s.data(), s.size()));
}

// hex_decode("48690a") -> "Hi\n" (ikili guvenli). Gecersiz girdi "".
VMValue aot_hex_decode_ptr(VMValue *v) {
  std::string s = as_text(v);
  if (s.size() % 2 != 0) return empty_str();
  auto nib = [](char c) -> int {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
  };
  std::string out;
  out.resize(s.size() / 2);
  for (size_t i = 0; i < out.size(); i++) {
    int hi = nib(s[2 * i]), lo = nib(s[2 * i + 1]);
    if (hi < 0 || lo < 0) return empty_str();
    out[i] = (char)((hi << 4) | lo);
  }
  return make_str(out);
}

// socket_timeout(fd, ms) -> bool: okuma + yazma zaman asimi. 0 = sinirsiz.
// SMTP gibi bir karsi tarafin takilmasi istegi sonsuza dek bekletmesin.
VMValue aot_socket_timeout_ptr(VMValue *fdv, VMValue *msv) {
  int64_t fd = as_int(fdv, -1);
  int64_t ms = as_int(msv, 0);
  if (fd < 0 || ms < 0) return VM_BOOL(0);
#if defined(_WIN32)
  DWORD t = (DWORD)ms;
  bool ok = setsockopt((SOCKET)fd, SOL_SOCKET, SO_RCVTIMEO, (const char *)&t,
                       sizeof(t)) == 0 &&
            setsockopt((SOCKET)fd, SOL_SOCKET, SO_SNDTIMEO, (const char *)&t,
                       sizeof(t)) == 0;
#else
  struct timeval tv;
  tv.tv_sec = (time_t)(ms / 1000);
  tv.tv_usec = (suseconds_t)((ms % 1000) * 1000);
  bool ok = setsockopt((int)fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) == 0 &&
            setsockopt((int)fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv)) == 0;
#endif
  return VM_BOOL(ok);
}

// shutdown_requested() -> bool. Ilk cagri SIGTERM/SIGINT isleyicisini kurar
// (docker stop SIGTERM gonderir). Ikinci sinyal sureci hemen sonlandirir.
VMValue aot_shutdown_requested_ptr(void) {
  std::call_once(g_signal_once, install_shutdown_handlers);
  return VM_BOOL(g_shutdown_signal != 0);
}

}  // extern "C"
