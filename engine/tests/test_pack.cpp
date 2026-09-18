// FAZ 6 — Icerik dagitimi kapilari: blok adreslenebilir pack (.tpak) + ice
// aktarma onbellegi + ASTC'nin pack icinden KOPYASIZ yolu.
//
// Her kapinin KONTROLU var (bir kapi "gecti" diyorsa kontrol gecisinde ayni
// olcum duser — Tuzaklar 1m):
//   1. pack gidis-donus: yazilan her varlik mmap'ten BAYT BAYT ayni cikar,
//      bloklar 16 hizali; KONTROL: farkli EKLEME SIRASI ayni baytlari verir
//      (belirlenimlilik), tek bayt degisince ozet degisir.
//   2. okuma yolunda AYIRMA YOK (AllocGate 0); KONTROL: pencere icinde bilerek
//      yapilan tek `new` sayaci artirir (kapi gercekten olcuyor).
//   3. bozulma reddi: kesik dosya, bozuk dizin (ozet), sinir disi blok, sirasiz
//      dizin; blok icerigi bozulunca pack_verify duser — KONTROL: bozulmamis
//      kopya hem oncesinde hem sonrasinda acilir/dogrulanir.
//   4. onbellek: ilk gecis IS YAPAR, ikincisi ATLAR (sayac + is fonksiyonu
//      cagri sayaci); KONTROL: girdi dosyasi degisince ve ayar degisince
//      atlamaz, urun bozulunca isabet sayilmaz.
//   5. delta yama: yalniz DEGISEN bloklar tasinir, yama + eski pack -> yeni pack
//      BAYT ESIT; KONTROL: degisiklik yoksa 0 blok, her sey degistiyse butun
//      bloklar tasinir, yanlis tabana uygulama reddedilir.
//   6. ASTC dogrudan: pack icindeki .ktx2 kopyasiz ayristirilir (seviye
//      isaretcileri mmap ARALIGINDA), pikseller dosyadan yuklemeyle ayni
//      (PSNR sonsuz); KONTROL: bozuk veri reddedilir, kaydirilmis goruntu
//      dusuk PSNR verir. GPU varsa ayni goruntu dogrudan yuklenir.
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <unistd.h>

#include <stb_image.h>

#include "content/importer.hpp"
#include "content/ktx2.hpp"
#include "content/pack.hpp"
#include "core/memory/alloc_gate.hpp"
#include "core/memory/arena.hpp"
#include "renderer/renderer.hpp"
#include "rhi/device.hpp"
#include "rhi/offscreen.hpp"
#include "rhi/vk_api.hpp"
#include "tests/test.hpp"

using namespace tulpar::engine;
using namespace tulpar::engine::content;
using namespace tulpar::engine::test;

namespace {
SystemArena &arena() {
  static SystemArena sys;
  if (sys.capacity() == 0) sys.reserve(96u << 20, "pack_test");
  return sys;
}
const char *asset_path(char *buf, size_t n, const char *name) {
  const char *adir = std::getenv("TULPAR_ENGINE_ASSETS");
  if (adir && *adir) std::snprintf(buf, n, "%s/%s", adir, name);
  else std::snprintf(buf, n, "%s/tests/assets/%s", ENGINE_SOURCE_DIR, name);
  return buf;
}
// Belirlenimli sahte varlik verisi (her ad icin farkli desen).
void fill_pattern(uint8_t *p, uint32_t n, uint32_t seed) {
  for (uint32_t i = 0; i < n; i++) p[i] = (uint8_t)((i * 31u + seed * 7u + (i >> 5)) & 0xFF);
}
bool tmp_dir_make(char *buf, size_t n, const char *stem) {
  std::snprintf(buf, n, "%s/%s_XXXXXX", tmp_dir(), stem);
  return mkdtemp(buf) != nullptr;
}
// Testin urettigi dosyalar tek tek silinir; dizin bosaldiysa kalkar.
void rm_dir(const char *dir) { ::rmdir(dir); }
uint64_t file_size(const char *p) {
  FILE *f = std::fopen(p, "rb");
  if (!f) return 0;
  std::fseek(f, 0, SEEK_END);
  const long n = std::ftell(f);
  std::fclose(f);
  return n > 0 ? (uint64_t)n : 0;
}
bool write_file(const char *path, const void *data, size_t n) {
  FILE *f = std::fopen(path, "wb");
  if (!f) return false;
  const bool ok = std::fwrite(data, 1, n, f) == n;
  std::fclose(f);
  return ok;
}
rhi::VkApi g_api;
} // namespace

ENGINE_TEST(pack_roundtrip_is_byte_exact_and_deterministic) {
  Arena &a = arena();
  const size_t mark = a.mark();
  // Uc varlik: farkli boyut (hizalama dolgusu olussun), farkli tur.
  const uint32_t n0 = 1000, n1 = 16, n2 = 5003;
  uint8_t *d0 = a.alloc_array<uint8_t>(n0), *d1 = a.alloc_array<uint8_t>(n1), *d2 = a.alloc_array<uint8_t>(n2);
  CHECK(d0 && d1 && d2);
  if (!d0 || !d1 || !d2) return;
  fill_pattern(d0, n0, 1);
  fill_pattern(d1, n1, 2);
  fill_pattern(d2, n2, 3);

  PackWriter w;
  CHECK(w.init(a, 8));
  CHECK(w.add("sahne/ana.sahneb", kPackScene, d0, n0));
  CHECK(w.add("doku/dama.ktx2", kPackTexture, d1, n1));
  CHECK(w.add("model/kup.gltf", kPackModel, d2, n2));
  CHECK(!w.add("model/kup.gltf", kPackModel, d2, n2)); // ayni ad iki kez: reddedilir
  CHECK(!w.add("", kPackRaw, d1, n1));
  CHECK(w.count() == 3);

  const size_t need = w.size();
  void *buf = a.alloc(need, kPackAlign);
  CHECK(buf && w.write(buf, need) == need);
  CHECK(need % kPackAlign == 0);
  char tmpl[512];
  tmp_template(tmpl, sizeof tmpl, "tpak");
  const int fd = mkstemp(tmpl);
  CHECK(fd >= 0);
  if (fd < 0) return;
  close(fd);
  CHECK(w.save(a, tmpl));
  CHECK(file_size(tmpl) == need);

  // mmap ile ac: her varlik bayt bayt ayni, blok 16 hizali, isaretci esleme icinde.
  PackFile pf;
  PackError err;
  const bool opened = pack_open(tmpl, &pf, &err);
  if (!opened) std::printf("    [bilgi] pack_open: %s\n", err.msg);
  CHECK(opened);
  if (!opened) { unlink(tmpl); return; }
  CHECK(pf.count() == 3 && pf.size == need);
  struct Item { const char *name; const uint8_t *data; uint32_t size; uint32_t type; };
  const Item items[3] = {{"sahne/ana.sahneb", d0, n0, kPackScene}, {"doku/dama.ktx2", d1, n1, kPackTexture}, {"model/kup.gltf", d2, n2, kPackModel}};
  bool same = true, aligned = true, inside = true, typed = true;
  for (const Item &it : items) {
    const PackEntry *e = pack_find(pf, it.name);
    if (!e) { same = false; continue; }
    const uint8_t *p = static_cast<const uint8_t *>(pack_data(pf, *e));
    if (e->size != it.size || std::memcmp(p, it.data, it.size) != 0) same = false;
    if (e->offset % kPackAlign != 0) aligned = false;
    if (p < pf.base || p + e->size > pf.base + pf.size) inside = false;
    if (e->type != it.type) typed = false;
  }
  CHECK(same && aligned && inside && typed);
  CHECK(pack_find(pf, "yok.bin") == nullptr);
  size_t sz = 0;
  uint32_t ty = 0;
  CHECK(pack_get(pf, "doku/dama.ktx2", &sz, &ty) != nullptr && sz == n1 && ty == kPackTexture);
  CHECK(pack_verify_all(pf));
  const uint64_t hash_ref = pf.h->hash;
  pack_close(&pf);

  // KONTROL 1: ayni varliklar TERS sirada eklenince ayni baytlar (belirlenimli).
  PackWriter w2;
  CHECK(w2.init(a, 8));
  CHECK(w2.add("model/kup.gltf", kPackModel, d2, n2));
  CHECK(w2.add("doku/dama.ktx2", kPackTexture, d1, n1));
  CHECK(w2.add("sahne/ana.sahneb", kPackScene, d0, n0));
  void *buf2 = a.alloc(need, kPackAlign);
  CHECK(buf2 && w2.write(buf2, need) == need);
  CHECK(std::memcmp(buf, buf2, need) == 0);
  // KONTROL 2: tek bayt degisince blok ozeti ve dosya degisir.
  d2[7] ^= 0x01;
  void *buf3 = a.alloc(need, kPackAlign);
  CHECK(buf3 && w2.write(buf3, need) == need);
  CHECK(std::memcmp(buf, buf3, need) != 0);
  d2[7] ^= 0x01;
  std::printf("    [bilgi] pack %zu bayt, 3 varlik, dizin ozeti %016llx; ters sirada ayni bayt, tek bit farkinda farkli\n", need,
              (unsigned long long)hash_ref);
  unlink(tmpl);
  a.reset_to(mark);
}

ENGINE_TEST(pack_read_path_allocates_nothing) {
  Arena &a = arena();
  const size_t mark = a.mark();
  const uint32_t n = 4096;
  uint8_t *d = a.alloc_array<uint8_t>(n);
  CHECK(d);
  if (!d) return;
  fill_pattern(d, n, 11);
  PackWriter w;
  CHECK(w.init(a, 4));
  CHECK(w.add("blok.bin", kPackRaw, d, n));
  char tmpl[512];
  tmp_template(tmpl, sizeof tmpl, "tpak_alloc");
  const int fd = mkstemp(tmpl);
  CHECK(fd >= 0);
  if (fd < 0) return;
  close(fd);
  CHECK(w.save(a, tmpl));

  PackFile pf;
  PackError err;
  uint64_t sum = 0;
  AllocGate::begin_frame();
  const bool ok = pack_open(tmpl, &pf, &err);
  if (ok) {
    for (int i = 0; i < 64; i++) {
      const PackEntry *e = pack_find(pf, "blok.bin");
      const uint8_t *p = static_cast<const uint8_t *>(pack_data(pf, *e));
      sum += p[i] + (uint64_t)e->size;
    }
    if (!pack_verify_all(pf)) sum = 0;
    pack_close(&pf);
  }
  const uint64_t allocs = AllocGate::end_frame();
  CHECK(ok && sum != 0);
  CHECK(allocs == 0); // ac + bul + oku + dogrula + kapat: tek ayirma yok
  // KONTROL: kapi gercekten sayiyor mu? Ayni pencerede bilerek bir ayirma.
  AllocGate::begin_frame();
  int *probe = new int(7);
  escape(probe);
  delete probe;
  const uint64_t ctrl = AllocGate::end_frame();
  std::printf("    [bilgi] okuma yolu %llu ayirma (kontrol: bilerek yapilan ayirma %llu)\n", (unsigned long long)allocs,
              (unsigned long long)ctrl);
  CHECK(ctrl >= 1);
  unlink(tmpl);
  a.reset_to(mark);
}

ENGINE_TEST(pack_open_rejects_corruption) {
  Arena &a = arena();
  const size_t mark = a.mark();
  const uint32_t n = 2048;
  uint8_t *d = a.alloc_array<uint8_t>(n);
  CHECK(d);
  if (!d) return;
  fill_pattern(d, n, 5);
  PackWriter w;
  CHECK(w.init(a, 4));
  CHECK(w.add("a.bin", kPackRaw, d, n));
  CHECK(w.add("b.bin", kPackRaw, d, n / 2));
  const size_t need = w.size();
  uint8_t *good = static_cast<uint8_t *>(a.alloc(need, kPackAlign));
  uint8_t *bad = static_cast<uint8_t *>(a.alloc(need, kPackAlign));
  CHECK(good && bad && w.write(good, need) == need);
  if (!good || !bad) return;
  PackFile pf;
  PackError err;
  auto reset = [&] { std::memcpy(bad, good, need); };
  auto hdr = [&] { return reinterpret_cast<PackHeader *>(bad); };
  // Pozitif kontrol: kopya oldugu gibi acilir ve dogrulanir.
  reset();
  CHECK(pack_open_memory(bad, need, &pf, &err) && pack_verify_all(pf));
  // 1) magic
  reset(); hdr()->magic ^= 1u;
  CHECK(!pack_open_memory(bad, need, &pf, &err) && std::strstr(err.msg, "magic"));
  // 2) surum
  reset(); hdr()->version = 42;
  CHECK(!pack_open_memory(bad, need, &pf, &err) && std::strstr(err.msg, "surum"));
  // 3) endian
  reset(); hdr()->endian = 0x04030201u;
  CHECK(!pack_open_memory(bad, need, &pf, &err) && std::strstr(err.msg, "bayt sirasi"));
  // 4) kesik dosya
  reset();
  CHECK(!pack_open_memory(bad, need - kPackAlign, &pf, &err) && std::strstr(err.msg, "boyut"));
  // 5) hizasiz isaretci
  reset();
  CHECK(!pack_open_memory(bad + 4, need, &pf, &err) && std::strstr(err.msg, "hizali"));
  // 6) dizin bozulmasi (ad ozeti / sira / sinir) — dizin ozeti yakalar
  reset();
  { auto *e = reinterpret_cast<PackEntry *>(bad + hdr()->entry_offset); e[0].offset += 16; }
  CHECK(!pack_open_memory(bad, need, &pf, &err) && std::strstr(err.msg, "ozet"));
  // 7) ozet yeniden hesaplanmis sinir disi blok (ozet tek savunma degil)
  auto rehash = [&] {
    const size_t from = offsetof(PackHeader, entry_count);
    hdr()->hash = content_fnv1a(bad + from, hdr()->data_offset - from);
  };
  reset();
  { auto *e = reinterpret_cast<PackEntry *>(bad + hdr()->entry_offset); e[0].size = (uint64_t)need; }
  rehash();
  CHECK(!pack_open_memory(bad, need, &pf, &err) && std::strstr(err.msg, "sinir disi"));
  // 8) dizin sirasi bozulmus (belirlenimlilik sozlesmesi)
  reset();
  {
    auto *e = reinterpret_cast<PackEntry *>(bad + hdr()->entry_offset);
    const PackEntry t = e[0]; e[0] = e[1]; e[1] = t;
  }
  rehash();
  CHECK(!pack_open_memory(bad, need, &pf, &err) && std::strstr(err.msg, "sirali"));
  // 9) BLOK icerigi bozuk: acilir (dizin saglam) ama pack_verify duser.
  reset();
  bad[reinterpret_cast<PackEntry *>(bad + hdr()->entry_offset)[0].offset + 3] ^= 0x80;
  uint32_t bad_i = 0xFFFFFFFFu;
  CHECK(pack_open_memory(bad, need, &pf, &err));
  CHECK(!pack_verify_all(pf, &bad_i) && bad_i == 0);
  // Son: bozulmamis kopya hala acilir ve dogrulanir.
  reset();
  CHECK(pack_open_memory(bad, need, &pf, &err) && pack_verify_all(pf));
  std::printf("    [bilgi] 9 bozulma reddedildi (son: %s); blok bozulmasi pack_verify ile yakalandi\n", err.msg);
  a.reset_to(mark);
}

ENGINE_TEST(import_cache_skips_repeated_work) {
  char dir[512];
  CHECK(tmp_dir_make(dir, sizeof dir, "onbellek"));
  char in_path[640];
  std::snprintf(in_path, sizeof in_path, "%s/girdi.bin", dir);
  uint8_t in_bytes[512], product[256];
  fill_pattern(in_bytes, sizeof in_bytes, 21);
  fill_pattern(product, sizeof product, 22);
  CHECK(write_file(in_path, in_bytes, sizeof in_bytes));

  struct Settings { uint32_t version, quality, flags, pad; };
  Settings st{1, 60, 1, 0};

  ImportCache cache;
  char cdir[640];
  std::snprintf(cdir, sizeof cdir, "%s/onbellek", dir);
  CHECK(import_cache_init(cache, cdir));

  // Isin ta kendisi: kac kez KOSTU? (onbellek "atladi" iddiasinin olcumu)
  uint32_t work_runs = 0;
  auto run_import = [&](const Settings &s) {
    const uint64_t key = import_key(in_path, &s, sizeof s, 3 /* ice aktarici surumu */);
    ImportProduct p;
    if (import_lookup(cache, key, ".urun", &p)) return p; // ISABET: is yok
    work_runs++;                                          // IS: urunu uret
    write_file(p.path, product, sizeof product);
    import_store(cache, p);
    return p;
  };

  const ImportProduct p1 = run_import(st);
  CHECK(!p1.hit && work_runs == 1 && cache.stats.misses == 1 && cache.stats.stores == 1);
  const ImportProduct p2 = run_import(st);
  CHECK(p2.hit && work_runs == 1 && cache.stats.hits == 1); // IS ATLANDI
  CHECK(p2.key == p1.key && p2.size == sizeof product);
  // KONTROL 1: girdi dosyasi degisti -> anahtar degisir, is yeniden kosar.
  in_bytes[3] ^= 0xFF;
  CHECK(write_file(in_path, in_bytes, sizeof in_bytes));
  const ImportProduct p3 = run_import(st);
  CHECK(!p3.hit && work_runs == 2 && p3.key != p1.key);
  in_bytes[3] ^= 0xFF;
  CHECK(write_file(in_path, in_bytes, sizeof in_bytes));
  CHECK(run_import(st).hit && work_runs == 2); // eski icerik geri geldi: yine isabet
  // KONTROL 2: ayar degisti -> anahtar degisir, is yeniden kosar.
  Settings st2 = st;
  st2.quality = 90;
  const ImportProduct p4 = run_import(st2);
  CHECK(!p4.hit && work_runs == 3 && p4.key != p1.key);
  CHECK(run_import(st2).hit && work_runs == 3);
  // KONTROL 3: urun bozulursa isabet SAYILMAZ (sessiz yanlis veri yok).
  uint8_t broken[200];
  fill_pattern(broken, sizeof broken, 23);
  CHECK(write_file(p1.path, broken, sizeof broken)); // boyut + ozet tutmaz
  const uint32_t rejects_before = cache.stats.rejects;
  const ImportProduct p5 = run_import(st);
  CHECK(!p5.hit && work_runs == 4 && cache.stats.rejects == rejects_before + 1);
  CHECK(run_import(st).hit); // yeniden uretildi, damgalandi
  std::printf("    [bilgi] onbellek: %u isabet / %u iska / %u red; is fonksiyonu %u kez kostu (8 cagri)\n", cache.stats.hits,
              cache.stats.misses, cache.stats.rejects, work_runs);
  // Temizlik: test dizinini bosalt.
  char cmdpath[700];
  std::snprintf(cmdpath, sizeof cmdpath, "%s.damga", p1.path);
  unlink(p1.path); unlink(cmdpath);
  std::snprintf(cmdpath, sizeof cmdpath, "%s.damga", p3.path);
  unlink(p3.path); unlink(cmdpath);
  std::snprintf(cmdpath, sizeof cmdpath, "%s.damga", p4.path);
  unlink(p4.path); unlink(cmdpath);
  unlink(in_path);
  rm_dir(cdir);
  rm_dir(dir);
}

ENGINE_TEST(pack_astc_loads_without_copy_and_matches_file) {
  Arena &a = arena();
  const size_t mark = a.mark();
  char kpath[640], ppath[640];
  asset_path(kpath, sizeof kpath, "checker_64.ktx2");
  asset_path(ppath, sizeof ppath, "checker_64.png");
  // (1) Dosyadan (kopyalayan yol) — referans.
  Ktx2Image from_file;
  const bool file_ok = ktx2_load(a, kpath, &from_file);
  if (!file_ok) { skip("checker_64.ktx2 yok (tests/assets ya da TULPAR_ENGINE_ASSETS)"); a.reset_to(mark); return; }
  // (2) Pack'e koy, mmap'le ac, KOPYASIZ ayristir.
  PackWriter w;
  CHECK(w.init(a, 4));
  CHECK(w.add_file(a, "dama.ktx2", kPackTexture, kpath));
  char tmpl[512];
  tmp_template(tmpl, sizeof tmpl, "tpak_astc");
  const int fd = mkstemp(tmpl);
  CHECK(fd >= 0);
  if (fd < 0) { a.reset_to(mark); return; }
  close(fd);
  CHECK(w.save(a, tmpl));
  PackFile pf;
  PackError perr;
  const bool opened = pack_open(tmpl, &pf, &perr);
  if (!opened) std::printf("    [bilgi] pack_open: %s\n", perr.msg);
  CHECK(opened);
  if (!opened) { unlink(tmpl); a.reset_to(mark); return; }
  size_t blob_size = 0;
  const uint8_t *blob = static_cast<const uint8_t *>(pack_get(pf, "dama.ktx2", &blob_size));
  CHECK(blob && blob_size == file_size(kpath));
  Ktx2Image mapped;
  AllocGate::begin_frame();
  const bool parsed = ktx2_parse_inplace(blob, blob_size, &mapped);
  const uint64_t allocs = AllocGate::end_frame();
  if (!parsed) std::printf("    [bilgi] kopyasiz ayristirma: %s\n", mapped.error);
  CHECK(parsed && allocs == 0);
  if (!parsed) { pack_close(&pf); unlink(tmpl); a.reset_to(mark); return; }
  // Kopyasizlik OLCUMU: seviye isaretcileri mmap araliginin ICINDE.
  bool inside = true, same_levels = true;
  CHECK(mapped.levels == from_file.levels && mapped.vk_format == from_file.vk_format && mapped.astc);
  for (uint32_t i = 0; i < mapped.levels && i < from_file.levels; i++) {
    if (mapped.level_data[i] < pf.base || mapped.level_data[i] + mapped.level_size[i] > pf.base + pf.size) inside = false;
    if (mapped.level_size[i] != from_file.level_size[i] ||
        std::memcmp(mapped.level_data[i], from_file.level_data[i], mapped.level_size[i]) != 0)
      same_levels = false;
  }
  CHECK(inside && same_levels);
  // Piksel: iki yolun cozumu AYNI (PSNR sonsuz), kaynak PNG'ye gore kaliteli.
  uint8_t *dec_map = nullptr, *dec_file = nullptr;
  CHECK(astc_decode_rgba(a, mapped.level_data[0], mapped.level_size[0], mapped.width, mapped.height, mapped.block_w, mapped.block_h,
                         mapped.srgb, &dec_map));
  CHECK(astc_decode_rgba(a, from_file.level_data[0], from_file.level_size[0], from_file.width, from_file.height, from_file.block_w,
                         from_file.block_h, from_file.srgb, &dec_file));
  double psnr_paths = 0, psnr_src = 0, psnr_ctrl = 0;
  if (dec_map && dec_file) {
    psnr_paths = rgba_psnr(dec_map, dec_file, mapped.width, mapped.height);
    int pw = 0, ph = 0, comp = 0;
    // stb dosya yolunu gormuyor (STBI_NO_STDIO): PNG'yi arenaya okuyup cozeriz.
    stbi_uc *png = nullptr;
    const uint64_t png_size = file_size(ppath);
    uint8_t *png_file = png_size ? a.alloc_array<uint8_t>((uint32_t)png_size) : nullptr;
    if (png_file) {
      FILE *pf2 = std::fopen(ppath, "rb");
      if (pf2) {
        const size_t got = std::fread(png_file, 1, (size_t)png_size, pf2);
        std::fclose(pf2);
        png = stbi_load_from_memory(png_file, (int)got, &pw, &ph, &comp, 4);
      }
    }
    if (png && (uint32_t)pw == mapped.width) {
      psnr_src = rgba_psnr(png, dec_map, mapped.width, mapped.height);
      // KONTROL: 8 piksel kaydirilmis dama dusuk PSNR verir (olcum kor degil).
      uint8_t *shift = a.alloc_array<uint8_t>(mapped.width * mapped.height * 4);
      if (shift) {
        for (uint32_t y = 0; y < mapped.height; y++)
          for (uint32_t x = 0; x < mapped.width; x++)
            std::memcpy(shift + (y * mapped.width + x) * 4, png + (y * mapped.width + ((x + 8) % mapped.width)) * 4, 4);
        psnr_ctrl = rgba_psnr(png, shift, mapped.width, mapped.height);
      }
      stbi_image_free(png);
    }
  }
  std::printf("    [bilgi] ASTC dogrudan: %ux%u, %u seviye, seviye0 %u B; iki yol PSNR %.1f dB, kaynaga gore %.1f dB "
              "(kontrol kaydirilmis %.1f dB), ayirma %llu\n",
              mapped.width, mapped.height, mapped.levels, mapped.level_size[0], psnr_paths, psnr_src, psnr_ctrl,
              (unsigned long long)allocs);
  CHECK(psnr_paths > 900.0); // bit bit ayni
  CHECK(psnr_src > 35.0);
  CHECK(psnr_ctrl > 0.0 && psnr_ctrl < 20.0);
  // KONTROL: bozuk veri reddedilir (kopyasiz yol da dogrulama yapar).
  uint8_t *copy = a.alloc_array<uint8_t>((uint32_t)blob_size);
  Ktx2Image junk;
  if (copy) {
    std::memcpy(copy, blob, blob_size);
    copy[1] ^= 0xFF; // KTX2 kimligi
    CHECK(!ktx2_parse_inplace(copy, blob_size, &junk));
    std::memcpy(copy, blob, blob_size);
    CHECK(!ktx2_parse_inplace(copy, 64, &junk)); // kesik
  }

  // (3) GPU: ayni kopyasiz goruntu dogrudan yuklenebiliyor mu?
  if (!rhi::vk_api_load(g_api)) { skip("Vulkan loader yok (GPU yuklemesi atlandi)"); }
  else {
    rhi::Device dev;
    rhi::DeviceConfig dc;
    if (!dev.init(a, g_api, dc)) skip("Vulkan cihazi yok (GPU yuklemesi atlandi)");
    else {
      rhi::OffscreenConfig oc;
      oc.srgb = true; oc.width = 64; oc.height = 64;
      rhi::OffscreenResult ores;
      rhi::OffscreenTarget *off = rhi::offscreen_create(dev, a, oc, &ores);
      renderer::Renderer ren;
      renderer::RendererConfig rc;
      rc.frames_in_flight = 1;
      rc.shadow_size = 0;
      if (off && ren.init(dev, a, rhi::offscreen_render_pass(off), rc)) {
        Ktx2UploadInfo ui;
        const renderer::TextureHandle th = ktx2_upload(ren, dev.caps(), a, mapped, &ui);
        CHECK(th.valid());
        std::printf("    [bilgi] GPU yuklemesi (pack isaretcisinden): %s, %u seviye\n", ui.hardware ? "donanim ASTC" : "CPU cozumu",
                    ui.levels);
        dev.api().vkDeviceWaitIdle(dev.handle());
        ren.shutdown();
      } else CHECK(false);
      if (off) rhi::offscreen_destroy(off);
      dev.shutdown();
    }
  }
  pack_close(&pf);
  unlink(tmpl);
  a.reset_to(mark);
}


ENGINE_TEST(pack_delta_patch_rebuilds_new_pack) {
  Arena &a = arena();
  const size_t mark = a.mark();
  const uint32_t n = 8000;
  uint8_t *b0 = a.alloc_array<uint8_t>(n), *b1 = a.alloc_array<uint8_t>(n), *b1v2 = a.alloc_array<uint8_t>(n),
          *b2 = a.alloc_array<uint8_t>(n / 2);
  CHECK(b0 && b1 && b1v2 && b2);
  if (!b0 || !b1 || !b1v2 || !b2) return;
  fill_pattern(b0, n, 31);
  fill_pattern(b1, n, 32);
  fill_pattern(b1v2, n, 33); // ayni ad, DEGISMIS icerik
  fill_pattern(b2, n / 2, 34);

  char p_old[512];
  char p_new[560], p_patch[560], p_out[560];
  tmp_template(p_old, sizeof p_old, "tpak_v1");
  const int fd = mkstemp(p_old);
  CHECK(fd >= 0);
  if (fd < 0) return;
  close(fd);
  std::snprintf(p_new, sizeof p_new, "%s.yeni", p_old);
  std::snprintf(p_patch, sizeof p_patch, "%s.yama", p_old);
  std::snprintf(p_out, sizeof p_out, "%s.kurulan", p_old);

  PackWriter w1;
  CHECK(w1.init(a, 8));
  CHECK(w1.add("ortak.bin", kPackRaw, b0, n));     // degismeyecek
  CHECK(w1.add("degisen.bin", kPackRaw, b1, n));   // degisecek
  CHECK(w1.add("silinen.bin", kPackRaw, b2, n / 2)); // yeni surumde yok
  CHECK(w1.save(a, p_old));
  PackWriter w2;
  CHECK(w2.init(a, 8));
  CHECK(w2.add("ortak.bin", kPackRaw, b0, n));
  CHECK(w2.add("degisen.bin", kPackRaw, b1v2, n));
  CHECK(w2.add("eklenen.bin", kPackTexture, b2, n / 2)); // icerik eskide VAR (ad farkli)
  CHECK(w2.save(a, p_new));

  PackFile old_pack, new_pack;
  PackError err;
  CHECK(pack_open(p_old, &old_pack, &err) && pack_open(p_new, &new_pack, &err));
  uint32_t changed = 0;
  uint64_t moved = 0;
  CHECK(pack_patch_create(a, old_pack, new_pack, p_patch, &changed, &moved));
  // "ortak.bin" ve "eklenen.bin" icerigi eskide var (ozet esleser) -> yalniz 1 blok tasinir.
  CHECK(changed == 1 && moved == n);
  const uint64_t patch_bytes = file_size(p_patch), new_bytes = file_size(p_new);
  CHECK(patch_bytes > 0 && patch_bytes < new_bytes);

  // Uygula: sonuc yeni pack ile BAYT ESIT.
  PackFile patch;
  CHECK(pack_open(p_patch, &patch, &err));
  const bool applied = pack_patch_apply(a, old_pack, patch, p_out, &err);
  if (!applied) std::printf("    [bilgi] yama: %s\n", err.msg);
  CHECK(applied);
  bool identical = false;
  if (applied && file_size(p_out) == new_bytes) {
    uint8_t *x = a.alloc_array<uint8_t>((uint32_t)new_bytes), *y = a.alloc_array<uint8_t>((uint32_t)new_bytes);
    FILE *fa = std::fopen(p_out, "rb"), *fb = std::fopen(p_new, "rb");
    if (x && y && fa && fb) {
      identical = std::fread(x, 1, (size_t)new_bytes, fa) == (size_t)new_bytes &&
                  std::fread(y, 1, (size_t)new_bytes, fb) == (size_t)new_bytes && std::memcmp(x, y, (size_t)new_bytes) == 0;
    }
    if (fa) std::fclose(fa);
    if (fb) std::fclose(fb);
  }
  CHECK(identical);
  // KONTROL 1: yanlis tabana uygulama reddedilir.
  CHECK(!pack_patch_apply(a, new_pack, patch, p_out, &err) && std::strstr(err.msg, "taban"));
  // p_patch AYNI YOLA yeniden yazilacak; once ESLEMEYI KAPAT.
  // Windows'ta bellege eslenmis bir dosya KILITLIDIR: uzerine yazmak
  // ERROR_USER_MAPPED_FILE ile duser (POSIX'te serbesttir ve eski esleme
  // eski icerigi gostermeye devam eder). Test bunu yapmadigi icin Windows'ta
  // iki kontrol dusuyordu — urun tarafinda da ayni kural gecerli: bir pack'i
  // yerinde guncellemek istiyorsan once pack_close.
  pack_close(&patch);
  // KONTROL 2: degisiklik yoksa 0 blok tasinir; her sey degistiyse hepsi tasinir.
  uint32_t same_changed = 99, all_changed = 0;
  uint64_t same_bytes = 1, all_bytes = 0;
  CHECK(pack_patch_create(a, new_pack, new_pack, p_patch, &same_changed, &same_bytes));
  CHECK(same_changed == 0 && same_bytes == 0);
  const uint64_t empty_patch = file_size(p_patch);
  // Taban olarak ILGISIZ icerik: yeni pack'in HICBIR blogu eskide yok.
  uint8_t *c0 = a.alloc_array<uint8_t>(n), *c1 = a.alloc_array<uint8_t>(n);
  CHECK(c0 && c1);
  if (!c0 || !c1) return;
  fill_pattern(c0, n, 41);
  fill_pattern(c1, n, 42);
  PackWriter w3;
  CHECK(w3.init(a, 8));
  CHECK(w3.add("ortak.bin", kPackRaw, c0, n));
  CHECK(w3.add("degisen.bin", kPackRaw, c1, n));
  char p_all[560];
  std::snprintf(p_all, sizeof p_all, "%s.hepsi", p_old);
  CHECK(w3.save(a, p_all));
  PackFile all_pack;
  CHECK(pack_open(p_all, &all_pack, &err));
  CHECK(pack_patch_create(a, all_pack, new_pack, p_patch, &all_changed, &all_bytes));
  std::printf("    [bilgi] delta yama: yeni pack %llu B, yama %llu B (1/%u blok, %llu B tasindi); kontrol: fark yok -> %u blok %llu B "
              "(yama %llu B), her sey farkli -> %u blok %llu B\n",
              (unsigned long long)new_bytes, (unsigned long long)patch_bytes, new_pack.count(), (unsigned long long)moved, same_changed,
              (unsigned long long)same_bytes, (unsigned long long)empty_patch, all_changed, (unsigned long long)all_bytes);
  CHECK(all_changed == new_pack.count() && all_bytes == (uint64_t)n * 2u + n / 2u);
  pack_close(&all_pack);
  pack_close(&patch);
  pack_close(&old_pack);
  pack_close(&new_pack);
  unlink(p_old); unlink(p_new); unlink(p_patch); unlink(p_out); unlink(p_all);
  a.reset_to(mark);
}
