// L6 CONTENT — Blok adreslenebilir varlik arsivi (.tpak): PLAN Faz 6
// "IO: pack mmap, ASTC dogrudan, IO thread'inde ayirma yok".
//
// Bir pack birden cok varligi (sahne blob'u, model, .ktx2 doku, ham veri) tek
// dosyada toplar. Sozlesme:
//   * Her blok 16 bayt hizali, dolgu 0 -> ayni girdiler ayni bayt (belirlenimli;
//     dizin ad ozetine gore sirali, ekleme sirasi sonucu DEGISTIRMEZ).
//   * Okuma yolu mmap: `pack_data` dosyanin icine isaretci doner, KOPYA YOK,
//     ayirma YOK (kapi AllocGate ile 0 olcer). Dosyadan gelen ASTC bloklari
//     dogrudan GPU'ya gider (bkz. ktx2_parse_inplace).
//   * Butunluk iki katmanli: acilista baslik + dizin + ad tablosu ozeti
//     dogrulanir (ucuz; tum dosyayi okumak mmap'i bosa cikarirdi), her blok
//     icin ayrica FNV-1a tutulur ve talep uzerine sinanir (pack_verify /
//     pack_verify_all). Kesik dosya, bozuk magic/surum/endian, sinir disi blok
//     ve sirasiz dizin reddedilir.
//
// Delta yama (bkz. pack_patch_create / pack_patch_apply): iki pack surumu
// arasinda yalniz degisen bloklar tasinir; yamanin kendisi de bir pack'tir.
//
// Not: mmap edilen bellek SALT OKUNUR. Icine yazacak tuketiciler (ornegin
// Detour navmesh baglantilari) veriyi once arenaya kopyalar.
#pragma once
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include "content/hash.hpp"
#include "core/memory/arena.hpp"

namespace tulpar::engine::content {

constexpr uint32_t kPackMagic = 0x4B415054u; // "TPAK" (LE)
constexpr uint32_t kPackVersion = 1;
constexpr uint32_t kPackEndian = 0x01020304u;
constexpr uint32_t kPackAlign = 16;
constexpr uint32_t kPackMaxName = 255;

// Varlik turu: yalniz bilgi/filtre icin; okuyucu icerigi yorumlamaz.
enum PackType : uint32_t {
  kPackRaw = 0,
  kPackScene = 1,   // .sahneb
  kPackModel = 2,   // .gltf / .glb
  kPackTexture = 3, // .ktx2
  kPackNav = 4,     // bake edilmis navmesh
  kPackAudio = 5,
};

struct PackHeader { // 64 bayt
  uint32_t magic, version, endian, header_size;
  uint64_t total_size;
  uint64_t hash; // FNV-1a: [hash alanindan sonrasi .. dizin+ad tablosu sonu)
  uint32_t entry_count, entry_offset;
  uint32_t name_offset, name_size;
  uint32_t data_offset, reserved0;
  uint64_t reserved1;
};
struct PackEntry { // 48 bayt
  uint64_t name_hash;    // content_fnv1a_str(ad) — dizin buna gore sirali
  uint64_t offset, size; // blok: 16 hizali ofset, gercek boyut
  uint64_t content_hash; // blogun FNV-1a'si
  uint32_t type;         // PackType
  uint32_t name;         // ad tablosu ofseti (NUL sonlu)
  uint32_t name_len, flags;
};
static_assert(sizeof(PackHeader) == 64 && sizeof(PackEntry) == 48, "pack kayit boyutlari sabit (dosya formati)");

struct PackError {
  char msg[128] = {0};
};

// Acilmis pack. Isaretciler ya mmap alanina ya cagiranin verdigi tampona bakar.
struct PackFile {
  const uint8_t *base = nullptr;
  size_t size = 0;
  const PackHeader *h = nullptr;
  const PackEntry *entries = nullptr;
  const char *names = nullptr;
  void *map = nullptr; // mmap tabani (pack_close munmap eder); 0 = bellekten acildi
  size_t map_size = 0;
  uint32_t count() const { return h ? h->entry_count : 0; }
  const char *name(const PackEntry &e) const { return names + e.name; }
};

namespace detail {
inline size_t pack_align(size_t v) { return (v + kPackAlign - 1) & ~(size_t)(kPackAlign - 1); }
inline bool pack_fail(PackError *e, const char *what) {
  if (e) std::snprintf(e->msg, sizeof e->msg, "pack: %s", what);
  return false;
}
} // namespace detail

// --- Yazici (arac tarafi; belirlenimli) -------------------------------------
// Veri KOPYALANMAZ: `add` isaretciyi saklar, cagiranin bellegi `save`/`write`
// bitene kadar yasamali.
class PackWriter {
public:
  bool init(Arena &a, uint32_t max_entries, uint32_t max_name_bytes = 8192) {
    items_ = a.alloc_array_zeroed<Item>(max_entries);
    names_ = a.alloc_array_zeroed<char>(max_name_bytes);
    cap_ = items_ && names_ ? max_entries : 0;
    name_cap_ = names_ ? max_name_bytes : 0;
    count_ = 0;
    name_used_ = 0;
    return cap_ != 0;
  }
  // Ad ozetine gore SIRALI ekler: ekleme sirasi cikti baytlarini degistirmez.
  // false: kapasite, gecersiz ad ya da ayni ad iki kez.
  bool add(const char *name, uint32_t type, const void *data, size_t size) {
    const size_t nl = name && *name ? std::strlen(name) : 0;
    if (!nl || nl > kPackMaxName || count_ >= cap_ || name_used_ + nl + 1 > name_cap_) return false;
    if (!data && size) return false;
    const uint64_t nh = content_fnv1a_str(name);
    uint32_t at = 0;
    while (at < count_ && less(items_[at], nh, name)) at++;
    if (at < count_ && items_[at].name_hash == nh && std::strcmp(names_ + items_[at].name, name) == 0) return false;
    for (uint32_t i = count_; i > at; i--) items_[i] = items_[i - 1];
    Item it;
    it.name_hash = nh;
    it.name = (uint32_t)name_used_;
    it.name_len = (uint32_t)nl;
    it.type = type;
    it.data = data;
    it.size = size;
    std::memcpy(names_ + name_used_, name, nl + 1);
    name_used_ += nl + 1;
    items_[at] = it;
    count_++;
    return true;
  }
  // Dosyayi scratch arenaya okur ve ekler (arac yolu).
  bool add_file(Arena &scratch, const char *name, uint32_t type, const char *path) {
    FILE *f = std::fopen(path, "rb");
    if (!f) return false;
    std::fseek(f, 0, SEEK_END);
    const long sz = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    if (sz <= 0) { std::fclose(f); return sz == 0 && add(name, type, "", 0); }
    uint8_t *buf = scratch.alloc_array<uint8_t>((size_t)sz);
    if (!buf) { std::fclose(f); return false; }
    const size_t got = std::fread(buf, 1, (size_t)sz, f);
    std::fclose(f);
    if (got != (size_t)sz) return false;
    return add(name, type, buf, got);
  }
  uint32_t count() const { return count_; }
  size_t size() const { return layout(); }
  // snprintf gibi: gereken boyutu doner, cap yetmezse yazmaz.
  size_t write(void *buf, size_t cap) const {
    const size_t need = layout();
    if (!buf || cap < need) return need;
    uint8_t *b = static_cast<uint8_t *>(buf);
    std::memset(b, 0, need); // dolgu 0: belirlenimli baytlar
    PackHeader h{};
    h.magic = kPackMagic;
    h.version = kPackVersion;
    h.endian = kPackEndian;
    h.header_size = sizeof(PackHeader);
    h.entry_count = count_;
    h.entry_offset = sizeof(PackHeader);
    size_t o = detail::pack_align(sizeof(PackHeader) + sizeof(PackEntry) * (size_t)count_);
    h.name_offset = (uint32_t)o;
    h.name_size = (uint32_t)(name_used_ ? name_used_ : 1);
    o = detail::pack_align(o + h.name_size);
    h.data_offset = (uint32_t)o;
    auto *ents = reinterpret_cast<PackEntry *>(b + h.entry_offset);
    // Ad tablosu DIZIN sirasinda yazilir (ekleme sirasinda degil): iki ayri
    // sirayla eklenen ayni varlik kumesi ayni baytlari vermeli.
    char *names_out = reinterpret_cast<char *>(b + h.name_offset);
    uint32_t noff = 0;
    for (uint32_t i = 0; i < count_; i++) {
      const Item &it = items_[i];
      PackEntry e{};
      e.name_hash = it.name_hash;
      e.offset = o;
      e.size = it.size;
      e.content_hash = content_fnv1a(it.data, it.size);
      e.type = it.type;
      e.name = noff;
      e.name_len = it.name_len;
      std::memcpy(names_out + noff, names_ + it.name, (size_t)it.name_len + 1);
      noff += it.name_len + 1;
      if (it.size) std::memcpy(b + o, it.data, it.size);
      o = detail::pack_align(o + it.size);
      ents[i] = e;
    }
    h.total_size = need;
    std::memcpy(b, &h, sizeof h);
    // Dosya ozeti yalniz BASLIK + DIZIN + AD TABLOSU'nu kapsar: acilista tum
    // dosyayi okumak mmap'in tembelligini bosa cikarirdi. Bloklarin butunlugu
    // kayit basina content_hash ile, talep uzerine dogrulanir.
    const size_t from = offsetof(PackHeader, entry_count);
    reinterpret_cast<PackHeader *>(b)->hash = content_fnv1a(b + from, h.data_offset - from);
    return need;
  }
  bool save(Arena &scratch, const char *path) const {
    const size_t need = layout();
    void *buf = scratch.alloc(need, kPackAlign);
    if (!buf) return false;
    write(buf, need);
    FILE *f = std::fopen(path, "wb");
    if (!f) return false;
    const bool ok = std::fwrite(buf, 1, need, f) == need;
    std::fclose(f);
    return ok;
  }

private:
  struct Item {
    uint64_t name_hash;
    const void *data;
    size_t size;
    uint32_t name, name_len, type, pad;
  };
  bool less(const Item &a, uint64_t nh, const char *name) const {
    if (a.name_hash != nh) return a.name_hash < nh;
    return std::strcmp(names_ + a.name, name) < 0; // ozet cakismasinda ad sirasi
  }
  size_t layout() const {
    size_t o = detail::pack_align(sizeof(PackHeader) + sizeof(PackEntry) * (size_t)count_);
    o = detail::pack_align(o + (name_used_ ? name_used_ : 1));
    for (uint32_t i = 0; i < count_; i++) o = detail::pack_align(o + items_[i].size);
    return o;
  }
  Item *items_ = nullptr;
  char *names_ = nullptr;
  uint32_t cap_ = 0, count_ = 0;
  size_t name_cap_ = 0, name_used_ = 0;
};

// --- Okuyucu ----------------------------------------------------------------
// Bellekteki pack: dogrular, gorunum kurar. Ayirma yok, kopya yok.
inline bool pack_open_memory(const void *data, size_t size, PackFile *out, PackError *err) {
  *out = PackFile{};
  if (!data) return detail::pack_fail(err, "veri yok");
  if ((uintptr_t)data % kPackAlign != 0) return detail::pack_fail(err, "veri 16 hizali degil");
  if (size < sizeof(PackHeader)) return detail::pack_fail(err, "baslik icin cok kisa");
  const auto *h = static_cast<const PackHeader *>(data);
  if (h->magic != kPackMagic) return detail::pack_fail(err, "magic uyusmuyor (pack degil)");
  if (h->version != kPackVersion) return detail::pack_fail(err, "desteklenmeyen pack surumu");
  if (h->endian != kPackEndian) return detail::pack_fail(err, "bayt sirasi uyusmuyor");
  if (h->header_size != sizeof(PackHeader)) return detail::pack_fail(err, "baslik boyutu uyusmuyor");
  if (h->total_size != size) return detail::pack_fail(err, "toplam boyut dosya boyutuyla uyusmuyor (kesik ya da fazla)");
  if (size % kPackAlign != 0) return detail::pack_fail(err, "toplam boyut 16 hizali degil");
  const uint64_t dir_end = (uint64_t)h->entry_offset + (uint64_t)h->entry_count * sizeof(PackEntry);
  if (h->entry_offset != sizeof(PackHeader) || dir_end > size) return detail::pack_fail(err, "dizin sinir disi");
  if (h->name_offset % kPackAlign != 0 || (uint64_t)h->name_offset + h->name_size > size || h->name_size == 0)
    return detail::pack_fail(err, "ad tablosu sinir disi");
  if (h->data_offset % kPackAlign != 0 || h->data_offset > size || h->data_offset < (uint64_t)h->name_offset + h->name_size)
    return detail::pack_fail(err, "veri bolumu ofseti gecersiz");
  const uint8_t *b = static_cast<const uint8_t *>(data);
  const size_t from = offsetof(PackHeader, entry_count);
  // Ozet: baslik + dizin + ad tablosu (bloklar kayit basina dogrulanir).
  if (content_fnv1a(b + from, h->data_offset - from) != h->hash) return detail::pack_fail(err, "dizin ozeti uyusmuyor (bozuk pack)");
  const char *names = reinterpret_cast<const char *>(b + h->name_offset);
  if (names[h->name_size - 1] != 0) return detail::pack_fail(err, "ad tablosu NUL ile bitmiyor");
  const auto *ents = reinterpret_cast<const PackEntry *>(b + h->entry_offset);
  for (uint32_t i = 0; i < h->entry_count; i++) {
    const PackEntry &e = ents[i];
    if (e.offset % kPackAlign != 0) return detail::pack_fail(err, "blok 16 hizali degil");
    if (e.offset < h->data_offset || e.offset + e.size > size) return detail::pack_fail(err, "blok sinir disi");
    if (e.name >= h->name_size || (uint64_t)e.name + e.name_len >= h->name_size) return detail::pack_fail(err, "ad tablosu disi");
    if (names[e.name + e.name_len] != 0) return detail::pack_fail(err, "ad NUL sonlu degil");
    if (content_fnv1a_str(names + e.name) != e.name_hash) return detail::pack_fail(err, "ad ozeti uyusmuyor");
    if (i && !(ents[i - 1].name_hash < e.name_hash ||
               (ents[i - 1].name_hash == e.name_hash && std::strcmp(names + ents[i - 1].name, names + e.name) < 0)))
      return detail::pack_fail(err, "dizin sirali degil (belirlenimlilik)");
  }
  out->base = b;
  out->size = size;
  out->h = h;
  out->entries = ents;
  out->names = names;
  return true;
}

// Dosyayi mmap'ler ve dogrular. Okuma yolunda ayirma yok.
inline bool pack_open(const char *path, PackFile *out, PackError *err) {
  *out = PackFile{};
  const int fd = ::open(path, O_RDONLY);
  if (fd < 0) {
    if (err) std::snprintf(err->msg, sizeof err->msg, "pack acilamadi: %.100s", path);
    return false;
  }
  struct stat st;
  if (::fstat(fd, &st) != 0 || st.st_size <= 0) {
    ::close(fd);
    return detail::pack_fail(err, "dosya boyutu gecersiz");
  }
  const size_t n = (size_t)st.st_size;
  void *m = ::mmap(nullptr, n, PROT_READ, MAP_PRIVATE, fd, 0);
  ::close(fd); // esleme fd'den bagimsiz yasar
  if (m == MAP_FAILED) return detail::pack_fail(err, "mmap basarisiz");
  if (!pack_open_memory(m, n, out, err)) {
    ::munmap(m, n);
    return false;
  }
  out->map = m;
  out->map_size = n;
  return true;
}

inline void pack_close(PackFile *p) {
  if (p->map) ::munmap(p->map, p->map_size);
  *p = PackFile{};
}

// Ad -> kayit (ikili arama; dizin ad ozetine gore sirali). Yok: nullptr.
inline const PackEntry *pack_find(const PackFile &p, const char *name) {
  if (!p.h || !name || !*name) return nullptr;
  const uint64_t nh = content_fnv1a_str(name);
  uint32_t lo = 0, hi = p.h->entry_count;
  while (lo < hi) {
    const uint32_t mid = lo + (hi - lo) / 2;
    if (p.entries[mid].name_hash < nh) lo = mid + 1;
    else hi = mid;
  }
  for (uint32_t i = lo; i < p.h->entry_count && p.entries[i].name_hash == nh; i++)
    if (std::strcmp(p.name(p.entries[i]), name) == 0) return &p.entries[i];
  return nullptr;
}
// Blogun ta kendisi (mmap icine isaretci; kopya yok).
inline const void *pack_data(const PackFile &p, const PackEntry &e) { return p.base + e.offset; }
inline const void *pack_get(const PackFile &p, const char *name, size_t *size, uint32_t *type = nullptr) {
  const PackEntry *e = pack_find(p, name);
  if (!e) { if (size) *size = 0; return nullptr; }
  if (size) *size = (size_t)e->size;
  if (type) *type = e->type;
  return pack_data(p, *e);
}
// Blok butunlugu (talep uzerine: acilis yalniz dizin ozetini dogrular).
inline bool pack_verify(const PackFile &p, const PackEntry &e) {
  return content_fnv1a(pack_data(p, e), (size_t)e.size) == e.content_hash;
}
inline bool pack_verify_all(const PackFile &p, uint32_t *bad_index = nullptr) {
  for (uint32_t i = 0; i < p.count(); i++)
    if (!pack_verify(p, p.entries[i])) {
      if (bad_index) *bad_index = i;
      return false;
    }
  return true;
}


// --- Delta yama ---------------------------------------------------------------
// Blok adreslenebilirligin karsiligi: iki pack surumu arasinda yalniz DEGISEN
// bloklar tasinir. Yama dosyasinin kendisi bir pack'tir (ayni dogrulama, ayni
// belirlenimlilik) ve su girdileri tasir:
//   "__taban"      : eski pack'in dizin ozeti (8 bayt) — yanlis tabana uygulama
//                    denemesi reddedilir.
//   "__yeni_dizin" : yeni pack'in basligi + dizini + ad tablosu (veri bolumune
//                    kadarki ilk bayt blogu) — hedef yerlesimin ta kendisi.
//   "blok/<ozet16>": eskide BULUNMAYAN bloklarin icerigi.
// Uygulama: hedef yerlesim manifesto'dan kurulur, her blok ya yamadan ya da
// ESKI pack'ten (icerik ozetiyle eslesen blok) kopyalanir; sonuc yeni pack ile
// BAYT ESITTIR (dolgu 0, ofsetler manifestodan).
constexpr const char *kPackPatchBase = "__taban";
constexpr const char *kPackPatchManifest = "__yeni_dizin";

namespace detail {
// Eski pack'te ayni icerige (ozet + boyut) sahip blok var mi?
inline const PackEntry *pack_find_content(const PackFile &p, uint64_t content_hash, uint64_t size) {
  for (uint32_t i = 0; i < p.count(); i++)
    if (p.entries[i].content_hash == content_hash && p.entries[i].size == size) return &p.entries[i];
  return nullptr;
}
inline void pack_block_name(char *buf, size_t n, uint64_t content_hash) {
  std::snprintf(buf, n, "blok/%016llx", (unsigned long long)content_hash);
}
} // namespace detail

// Yama yaz. out_changed / out_bytes: tasinan blok sayisi ve bayti (olcum).
inline bool pack_patch_create(Arena &scratch, const PackFile &old_pack, const PackFile &new_pack, const char *out_path,
                              uint32_t *out_changed, uint64_t *out_bytes) {
  if (!old_pack.h || !new_pack.h) return false;
  PackWriter w;
  if (!w.init(scratch, new_pack.count() + 2, 32u * (new_pack.count() + 2) + 64)) return false;
  uint64_t *base = scratch.alloc_array<uint64_t>(1);
  if (!base) return false;
  *base = old_pack.h->hash;
  if (!w.add(kPackPatchBase, kPackRaw, base, sizeof(uint64_t))) return false;
  if (!w.add(kPackPatchManifest, kPackRaw, new_pack.base, new_pack.h->data_offset)) return false;
  uint32_t changed = 0;
  uint64_t bytes = 0;
  for (uint32_t i = 0; i < new_pack.count(); i++) {
    const PackEntry &e = new_pack.entries[i];
    if (detail::pack_find_content(old_pack, e.content_hash, e.size)) continue; // eskide var: tasima
    char name[32];
    detail::pack_block_name(name, sizeof name, e.content_hash);
    if (!w.add(name, e.type, pack_data(new_pack, e), (size_t)e.size)) continue; // ayni icerik iki kez: bir kere yeter
    changed++;
    bytes += e.size;
  }
  if (out_changed) *out_changed = changed;
  if (out_bytes) *out_bytes = bytes;
  return w.save(scratch, out_path);
}

// Yamayi uygula: eski pack + yama -> yeni pack dosyasi (bayt esit).
inline bool pack_patch_apply(Arena &scratch, const PackFile &old_pack, const PackFile &patch, const char *out_path, PackError *err) {
  size_t bs = 0;
  const void *bp = pack_get(patch, kPackPatchBase, &bs);
  if (!bp || bs != sizeof(uint64_t)) return detail::pack_fail(err, "yamada taban kimligi yok");
  uint64_t base_hash = 0;
  std::memcpy(&base_hash, bp, sizeof base_hash);
  if (!old_pack.h || base_hash != old_pack.h->hash) return detail::pack_fail(err, "yama bu tabana ait degil (dizin ozeti tutmuyor)");
  size_t ms = 0;
  const uint8_t *man = static_cast<const uint8_t *>(pack_get(patch, kPackPatchManifest, &ms));
  if (!man || ms < sizeof(PackHeader)) return detail::pack_fail(err, "yamada yeni dizin yok");
  const auto *nh = reinterpret_cast<const PackHeader *>(man);
  if (nh->magic != kPackMagic || nh->version != kPackVersion || nh->endian != kPackEndian || nh->header_size != sizeof(PackHeader))
    return detail::pack_fail(err, "yeni dizin basligi gecersiz");
  if (nh->data_offset != ms || nh->entry_offset != sizeof(PackHeader)) return detail::pack_fail(err, "yeni dizin kesik");
  if ((uint64_t)nh->entry_offset + (uint64_t)nh->entry_count * sizeof(PackEntry) > ms ||
      (uint64_t)nh->name_offset + nh->name_size > ms)
    return detail::pack_fail(err, "yeni dizin sinir disi");
  if (nh->total_size < ms || nh->total_size > (1ull << 32)) return detail::pack_fail(err, "yeni pack boyutu gecersiz");
  uint8_t *out = static_cast<uint8_t *>(scratch.alloc((size_t)nh->total_size, kPackAlign));
  if (!out) return detail::pack_fail(err, "arena dolu");
  std::memset(out, 0, (size_t)nh->total_size); // dolgu 0: bayt esitlik
  std::memcpy(out, man, ms);
  const auto *ents = reinterpret_cast<const PackEntry *>(man + nh->entry_offset);
  for (uint32_t i = 0; i < nh->entry_count; i++) {
    const PackEntry &e = ents[i];
    if (e.offset < ms || e.offset + e.size > nh->total_size) return detail::pack_fail(err, "yeni dizin blogu sinir disi");
    char name[32];
    detail::pack_block_name(name, sizeof name, e.content_hash);
    size_t got = 0;
    const void *src = pack_get(patch, name, &got);
    if (!src || got != e.size) {
      const PackEntry *oe = detail::pack_find_content(old_pack, e.content_hash, e.size);
      if (!oe) return detail::pack_fail(err, "yama eksik: blok ne yamada ne eski pack'te");
      src = pack_data(old_pack, *oe);
    }
    std::memcpy(out + e.offset, src, (size_t)e.size);
  }
  PackFile check;
  if (!pack_open_memory(out, (size_t)nh->total_size, &check, err)) return false;
  uint32_t bad = 0;
  if (!pack_verify_all(check, &bad)) return detail::pack_fail(err, "uygulanan pack blok ozetini tutturmuyor");
  FILE *f = std::fopen(out_path, "wb");
  if (!f) return detail::pack_fail(err, "yeni pack yazilamadi");
  const bool ok = std::fwrite(out, 1, (size_t)nh->total_size, f) == (size_t)nh->total_size;
  std::fclose(f);
  return ok ? true : detail::pack_fail(err, "yazma eksik");
}

} // namespace tulpar::engine::content
