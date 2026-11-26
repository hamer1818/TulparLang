// Paket imzalama — bkz. imza.hpp.
#include "imza.hpp"

#include <cstring>
#include <vector>

#if defined(TULPAR_HAS_TLS)
#include <openssl/bio.h>
#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#endif

namespace tulpar {
namespace {

const char *kOnek = "ed25519:";

std::string b64_enc(const unsigned char *p, size_t n) {
  static const char *t = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::string o;
  for (size_t i = 0; i < n; i += 3) {
    unsigned v = (unsigned)p[i] << 16 | (i + 1 < n ? (unsigned)p[i + 1] << 8 : 0) |
                 (i + 2 < n ? p[i + 2] : 0);
    o += t[(v >> 18) & 63];
    o += t[(v >> 12) & 63];
    o += i + 1 < n ? t[(v >> 6) & 63] : '=';
    o += i + 2 < n ? t[v & 63] : '=';
  }
  return o;
}

bool b64_dec(const std::string &s, std::vector<unsigned char> &out) {
  out.clear();
  unsigned v = 0;
  int bits = 0;
  for (char c : s) {
    int d;
    if (c >= 'A' && c <= 'Z') d = c - 'A';
    else if (c >= 'a' && c <= 'z') d = c - 'a' + 26;
    else if (c >= '0' && c <= '9') d = c - '0' + 52;
    else if (c == '+') d = 62;
    else if (c == '/') d = 63;
    else if (c == '=') break;
    else if (c == '\n' || c == '\r' || c == ' ') continue;
    else return false;
    v = (v << 6) | (unsigned)d;
    bits += 6;
    if (bits >= 8) {
      bits -= 8;
      out.push_back((unsigned char)((v >> bits) & 0xFF));
    }
  }
  return true;
}

// "ed25519:<b64>" -> ham baytlar (beklenen boy).
bool parse_tagged(const std::string &text, size_t want, std::vector<unsigned char> &raw,
                  std::string &err, const char *what) {
  std::string t = text;
  while (!t.empty() && (t.back() == '\n' || t.back() == '\r' || t.back() == ' ')) t.pop_back();
  size_t a = t.find_first_not_of(" \t");
  if (a != std::string::npos) t = t.substr(a);
  if (t.compare(0, std::strlen(kOnek), kOnek) != 0) {
    err = std::string(what) + ": 'ed25519:' oneki yok / missing 'ed25519:' prefix";
    return false;
  }
  if (!b64_dec(t.substr(std::strlen(kOnek)), raw) || raw.size() != want) {
    err = std::string(what) + ": gecersiz base64 ya da boy (" + std::to_string(raw.size()) +
          " bayt, beklenen " + std::to_string(want) + ") / invalid encoding";
    return false;
  }
  return true;
}

#if defined(TULPAR_HAS_TLS)
std::string ssl_err() {
  unsigned long e = ERR_get_error();
  char buf[256];
  ERR_error_string_n(e, buf, sizeof buf);
  return e ? std::string(buf) : std::string("openssl");
}
EVP_PKEY *load_priv(const std::string &pem, std::string &err) {
  BIO *bio = BIO_new_mem_buf(pem.data(), (int)pem.size());
  EVP_PKEY *k = bio ? PEM_read_bio_PrivateKey(bio, nullptr, nullptr, nullptr) : nullptr;
  if (bio) BIO_free(bio);
  if (!k || EVP_PKEY_id(k) != EVP_PKEY_ED25519) {
    if (k) EVP_PKEY_free(k);
    err = "ozel anahtar okunamadi (PEM Ed25519 bekleniyor) / cannot read Ed25519 private key";
    return nullptr;
  }
  return k;
}
bool pub_text_of(EVP_PKEY *k, std::string &pub, std::string &err) {
  unsigned char raw[32];
  size_t n = sizeof raw;
  if (EVP_PKEY_get_raw_public_key(k, raw, &n) != 1 || n != 32) {
    err = "genel anahtar cikarilamadi: " + ssl_err();
    return false;
  }
  pub = std::string(kOnek) + b64_enc(raw, n);
  return true;
}
#endif

const char *kYok =
    "bu tulpar OpenSSL'siz derlenmis: Ed25519 imzasi yok / this tulpar was built without "
    "OpenSSL: no Ed25519 signatures";

}  // namespace

bool imza_mevcut() {
#if defined(TULPAR_HAS_TLS)
  return true;
#else
  return false;
#endif
}

bool imza_keygen(std::string &priv_pem, std::string &pub_text, std::string &err) {
#if defined(TULPAR_HAS_TLS)
  EVP_PKEY *k = nullptr;
  EVP_PKEY_CTX *c = EVP_PKEY_CTX_new_id(EVP_PKEY_ED25519, nullptr);
  bool ok = c && EVP_PKEY_keygen_init(c) == 1 && EVP_PKEY_keygen(c, &k) == 1;
  if (c) EVP_PKEY_CTX_free(c);
  if (!ok) { err = "anahtar uretilemedi: " + ssl_err(); return false; }
  BIO *bio = BIO_new(BIO_s_mem());
  ok = bio && PEM_write_bio_PrivateKey(bio, k, nullptr, nullptr, 0, nullptr, nullptr) == 1;
  if (ok) {
    char *p = nullptr;
    long n = BIO_get_mem_data(bio, &p);
    priv_pem.assign(p, (size_t)n);
  }
  if (bio) BIO_free(bio);
  ok = ok && pub_text_of(k, pub_text, err);
  EVP_PKEY_free(k);
  return ok;
#else
  (void)priv_pem; (void)pub_text;
  err = kYok;
  return false;
#endif
}

bool imza_pub_of(const std::string &priv_pem, std::string &pub_text, std::string &err) {
#if defined(TULPAR_HAS_TLS)
  EVP_PKEY *k = load_priv(priv_pem, err);
  if (!k) return false;
  bool ok = pub_text_of(k, pub_text, err);
  EVP_PKEY_free(k);
  return ok;
#else
  (void)priv_pem; (void)pub_text;
  err = kYok;
  return false;
#endif
}

bool imza_sign(const std::string &data, const std::string &priv_pem, std::string &sig_text,
               std::string &err) {
#if defined(TULPAR_HAS_TLS)
  EVP_PKEY *k = load_priv(priv_pem, err);
  if (!k) return false;
  EVP_MD_CTX *m = EVP_MD_CTX_new();
  unsigned char sig[64];
  size_t n = sizeof sig;
  bool ok = m && EVP_DigestSignInit(m, nullptr, nullptr, nullptr, k) == 1 &&
            EVP_DigestSign(m, sig, &n, (const unsigned char *)data.data(), data.size()) == 1 &&
            n == 64;
  if (m) EVP_MD_CTX_free(m);
  EVP_PKEY_free(k);
  if (!ok) { err = "imzalanamadi: " + ssl_err(); return false; }
  sig_text = std::string(kOnek) + b64_enc(sig, n) + "\n";
  return true;
#else
  (void)data; (void)priv_pem; (void)sig_text;
  err = kYok;
  return false;
#endif
}

bool imza_verify(const std::string &data, const std::string &sig_text, const std::string &pub_text,
                 std::string &err) {
  std::vector<unsigned char> sig, pub;
  if (!parse_tagged(sig_text, 64, sig, err, "imza / signature")) return false;
  if (!parse_tagged(pub_text, 32, pub, err, "genel anahtar / public key")) return false;
#if defined(TULPAR_HAS_TLS)
  EVP_PKEY *k = EVP_PKEY_new_raw_public_key(EVP_PKEY_ED25519, nullptr, pub.data(), pub.size());
  if (!k) { err = "genel anahtar yuklenemedi: " + ssl_err(); return false; }
  EVP_MD_CTX *m = EVP_MD_CTX_new();
  int r = (m && EVP_DigestVerifyInit(m, nullptr, nullptr, nullptr, k) == 1)
              ? EVP_DigestVerify(m, sig.data(), sig.size(), (const unsigned char *)data.data(),
                                 data.size())
              : -1;
  if (m) EVP_MD_CTX_free(m);
  EVP_PKEY_free(k);
  if (r != 1) {
    err = "imza dogrulanamadi (paket degistirilmis ya da baska anahtarla imzali) / signature "
          "does not verify";
    return false;
  }
  return true;
#else
  err = kYok;
  return false;
#endif
}

}  // namespace tulpar
