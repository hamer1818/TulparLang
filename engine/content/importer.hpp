// L6 CONTENT — Ice aktarma onbellegi (PLAN Faz 6: "importer + content hash cache").
//
// Bir ice aktarma (glTF -> model olcumu, PNG -> ASTC/.ktx2) pahalidir ve
// SAF'tir: ayni girdi + ayni ayar -> ayni urun. Bu yuzden anahtar olarak
// girdinin BAYTLARI + ayar bloku + ice aktarici surumu ozetlenir (FNV-1a 64);
// urun `<onbellek>/<anahtar16><uzanti>` dosyasina yazilir, yaninda bir damga
// (anahtar + boyut + urun ozeti) durur. Ikinci calistirmada damga dogrulanir
// ve IS ATLANIR (sayac: ImportCacheStats).
//
// Girdi dosyasi ya da ayar degisirse anahtar degisir -> isabet YOK (kapinin
// kontrolu budur). Urun kesilmis/bozulmussa damga tutmaz, isabet sayilmaz:
// onbellek sessizce yanlis veri dondurmez.
//
// Onbellek dizini: cagiranin verdigi, yoksa $TULPAR_ONBELLEK, yoksa
// ".tulpar_onbellek". Dizin yoksa olusturulur.
#pragma once
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <sys/stat.h>

#include "content/hash.hpp"

namespace tulpar::engine::content {

constexpr uint32_t kImportStampMagic = 0x4C424F54u; // "TOBL"
constexpr uint32_t kImportStampVersion = 1;
constexpr uint32_t kImportDirLen = 512;  // onbellek dizini
constexpr uint32_t kImportPathLen = 640; // dizin + "/" + 16 haneli anahtar + uzanti
constexpr uint32_t kImportExtLen = 16;

struct ImportStamp {
  uint32_t magic, version;
  uint64_t key, size, hash;
};

struct ImportCacheStats {
  uint32_t hits = 0;    // is atlandi
  uint32_t misses = 0;  // is yapildi
  uint32_t stores = 0;  // urun onbellege yazildi
  uint32_t rejects = 0; // damga/urun tutmadi (bozuk ya da eksik urun)
  uint64_t hit_bytes = 0, stored_bytes = 0;
};

struct ImportCache {
  char dir[kImportDirLen] = {0};
  bool enabled = false;
  ImportCacheStats stats;
};

// Urun kaydi: `path` her zaman dolar (isabet yoksa cagiran oraya yazar).
struct ImportProduct {
  uint64_t key = 0, size = 0, hash = 0;
  bool hit = false;
  char path[kImportPathLen] = {0};
  char stamp[kImportPathLen + kImportExtLen] = {0};
};

namespace detail {
inline bool import_mkdir_p(const char *dir) {
  char tmp[kImportDirLen];
  const size_t n = std::strlen(dir);
  if (n == 0 || n >= sizeof tmp) return false;
  std::memcpy(tmp, dir, n + 1);
  for (size_t i = 1; i <= n; i++) {
    if (tmp[i] != '/' && tmp[i] != 0) continue;
    const char c = tmp[i];
    tmp[i] = 0;
    struct stat st;
    if (::stat(tmp, &st) != 0 && ::mkdir(tmp, 0777) != 0) return false;
    tmp[i] = c;
  }
  struct stat st;
  return ::stat(dir, &st) == 0 && (st.st_mode & S_IFDIR) != 0;
}
inline bool import_file_size(const char *path, uint64_t *out) {
  struct stat st;
  if (::stat(path, &st) != 0 || st.st_size < 0) return false;
  *out = (uint64_t)st.st_size;
  return true;
}
} // namespace detail

// Dosyanin baytlarinin ozeti (parca parca; ayirma yok). 0 = okunamadi.
inline uint64_t import_hash_file(const char *path, uint64_t seed = kFnvSeed) {
  FILE *f = std::fopen(path, "rb");
  if (!f) return 0;
  uint8_t buf[16 << 10];
  uint64_t h = seed;
  size_t got;
  while ((got = std::fread(buf, 1, sizeof buf, f)) > 0) h = content_fnv1a(buf, got, h);
  std::fclose(f);
  return h ? h : 1; // 0 yalniz "okunamadi" demek: bos dosya da gecerli anahtar
}

// Anahtar: girdi baytlari + ayar bloku + ice aktarici surumu. 0 = girdi yok.
inline uint64_t import_key(const char *input_path, const void *settings, size_t settings_size, uint32_t importer_version) {
  const uint64_t fh = import_hash_file(input_path);
  if (!fh) return 0;
  uint64_t h = content_fnv1a(&importer_version, sizeof importer_version, fh);
  if (settings && settings_size) h = content_fnv1a(settings, settings_size, h);
  return h ? h : 1;
}

inline bool import_cache_init(ImportCache &c, const char *dir = nullptr) {
  const char *d = dir && *dir ? dir : std::getenv("TULPAR_ONBELLEK");
  if (!d || !*d) d = ".tulpar_onbellek";
  if (std::strlen(d) + 32 >= sizeof c.dir) return false;
  std::snprintf(c.dir, sizeof c.dir, "%s", d);
  c.stats = ImportCacheStats{};
  c.enabled = detail::import_mkdir_p(c.dir);
  return c.enabled;
}

// Urunun onbellekteki yolunu kurar; damgayi dogrular. Donus: ISABET (is atlanabilir).
inline bool import_lookup(ImportCache &c, uint64_t key, const char *ext, ImportProduct *out) {
  *out = ImportProduct{};
  out->key = key;
  if (!c.enabled || !key) return false;
  char ext_buf[kImportExtLen];
  std::snprintf(ext_buf, sizeof ext_buf, "%s", ext ? ext : "");
  std::snprintf(out->path, sizeof out->path, "%s/%016llx%s", c.dir, (unsigned long long)key, ext_buf);
  std::snprintf(out->stamp, sizeof out->stamp, "%s.damga", out->path);
  FILE *f = std::fopen(out->stamp, "rb");
  if (!f) { c.stats.misses++; return false; }
  ImportStamp st{};
  const size_t got = std::fread(&st, 1, sizeof st, f);
  std::fclose(f);
  uint64_t size = 0;
  if (got != sizeof st || st.magic != kImportStampMagic || st.version != kImportStampVersion || st.key != key) {
    c.stats.rejects++;
    c.stats.misses++;
    return false;
  }
  if (!detail::import_file_size(out->path, &size) || size != st.size || import_hash_file(out->path) != st.hash) {
    c.stats.rejects++; // urun eksik / kesik / bozuk: isabet sayilmaz
    c.stats.misses++;
    return false;
  }
  out->size = size;
  out->hash = st.hash;
  out->hit = true;
  c.stats.hits++;
  c.stats.hit_bytes += size;
  return true;
}

// Cagiran urunu `p.path`e yazdiktan sonra: damgalar (sonraki calistirma atlar).
inline bool import_store(ImportCache &c, ImportProduct &p) {
  if (!c.enabled || !p.key || !p.path[0]) return false;
  uint64_t size = 0;
  if (!detail::import_file_size(p.path, &size)) return false;
  ImportStamp st{};
  st.magic = kImportStampMagic;
  st.version = kImportStampVersion;
  st.key = p.key;
  st.size = size;
  st.hash = import_hash_file(p.path);
  FILE *f = std::fopen(p.stamp, "wb");
  if (!f) return false;
  const bool ok = std::fwrite(&st, 1, sizeof st, f) == sizeof st;
  std::fclose(f);
  if (!ok) return false;
  p.size = st.size;
  p.hash = st.hash;
  c.stats.stores++;
  c.stats.stored_bytes += size;
  return true;
}

// Onbellekteki urunu hedefe kopyalar (isabette cikti dosyasini uretmek icin).
inline bool import_copy(const char *src, const char *dst) {
  if (!src || !dst) return false;
  FILE *in = std::fopen(src, "rb");
  if (!in) return false;
  FILE *out = std::fopen(dst, "wb");
  if (!out) { std::fclose(in); return false; }
  uint8_t buf[16 << 10];
  bool ok = true;
  size_t got;
  while (ok && (got = std::fread(buf, 1, sizeof buf, in)) > 0) ok = std::fwrite(buf, 1, got, out) == got;
  std::fclose(in);
  std::fclose(out);
  return ok;
}

} // namespace tulpar::engine::content
