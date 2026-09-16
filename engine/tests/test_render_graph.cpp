// Render graph / son islem (bloom) kapilari.
//
// Her kapinin KONTROLU var: bir kapi "gecti" diyorsa, kontrol gecisinde AYNI
// olcum duserek o kapinin gercekten bir sey olctugunu gosterir (Tuzaklar 1m/8s:
// olcmeyen kapi sahte yesildir).
//   1. post KAPALIYKEN cikti bugunku yolla bit bit ayni (kontrol: post acikken fark > 0)
//   2. bloom parlak alani cevresine YAYIYOR (kontrol: yogunluk 0 / esik cok yuksek -> yayilim 0)
//   3. bloom SECICI: esik altindaki nesne yayilmiyor (kontrol: esik dusurulunce yayiliyor)
//   4. gecis tablosu: PostInfo'nun verdigi sira beklenen sira (kontrol: bozuk tablo reddedilir)
//   5. bicim/bellek raporlanir; HDR bicimi yoksa GORUNUR atlanir (sessiz return yok)
//   6. post yolunda Mali en iyi uygulama denetimi (kontrol: LOD kirpan sampler uyari vermeli)
//   7. FAZ 9 GPU cull + dolayli cizim: cull ACIK/KAPALI piksel farki 0
//      (kontrol: kamera cevrilince hayatta kalan 0 ve ekran bos), eleme sayisi
//      (kontrol: hepsi icerideyken 0), dolayli cizim sayisi GPU sayacindan
//   8. cull yolunda Mali en iyi uygulama denetimi (pozitif kontrollu)
#include <cmath>
#include <cstdio>
#include <cstring>

#include "core/memory/arena.hpp"
#include "renderer/graph.hpp"
#include "renderer/renderer.hpp"
#include "rhi/device.hpp"
#include "rhi/offscreen.hpp"
#include "rhi/vk_api.hpp"
#include "tests/test.hpp"

using namespace tulpar::engine;
using namespace tulpar::engine::rhi;
using namespace tulpar::engine::test;

namespace {
VkApi g_api;
bool loader_ok() { return vk_api_load(g_api); }

// Dogrulama/BestPractices kapilari KENDI VkApi tablolariyla kosar.
//
// SEBEP (olculdu 2026-09-15, bu kapilar eklenirken COKME olarak): VkApi cihaz
// seviyesi giris noktalarini tutar ve vk_api_load_device() onlari o cihazin
// gonderim tablosuna gore YENIDEN YAZAR. Ayni VkApi ile ikinci bir cihaz
// acilirsa paylasilan cihazin isaretcileri ikincininkiyle degisir; ikinci cihaz
// kapaninca bu isaretciler cop olur ve SONRAKI cagri (ornegin offscreen'in
// vkCreateImage'i) gecersiz adrese atlar. Ayri tablo = ayri gonderim.
VkApi g_api_bp;
bool loader_bp_ok() { return vk_api_load(g_api_bp); }

// Dogrulama katmani ISTEMEYEN butun GPU kapilari TEK VkInstance paylasir ve o
// instance surec boyunca YASAR (bilerek kapatilmaz).
//
// SEBEP (olculdu 2026-09-15, bu makine): NVIDIA ICD her vkCreateInstance'ta
// dlopen ediliyor; libnvidia-tls.so initial-exec TLS istiyor ve glibc'nin
// "static TLS surplus" alani dlopen/dlclose dongulerinde geri verilmiyor.
// Belli sayida instance'tan sonra loader "cannot allocate memory in static TLS
// block" der ve vkCreateInstance VK_ERROR_INCOMPATIBLE_DRIVER doner: SONRADAN
// kosan BASKA testler sessizce "Vulkan cihazi yok" diye atlanir. Yani her yeni
// GPU kapisinin bir instance'i, uzaktaki bir kapiyi oldurebilir.
// (Dogrulama/BestPractices kapilari istisna: sayaclari kirletmemek icin kendi
// cihazlarini kurarlar.)
struct OrtakGpu {
  SystemArena sys;
  Device dev;
  bool denendi = false;
  bool ok = false;
};
OrtakGpu &ortak_gpu() {
  static OrtakGpu g;
  if (!g.denendi) {
    g.denendi = true;
    if (!loader_ok()) return g;
    if (!g.sys.reserve(384u << 20, "graph_ortak")) return g;
    DeviceConfig dc;
    g.ok = g.dev.init(g.sys, g_api, dc);
  }
  return g;
}

struct Rec {
  renderer::Renderer *r;
};
void rec_main(VkCommandBuffer cb, void *u) { static_cast<Rec *>(u)->r->record(cb); }
void rec_before(VkCommandBuffer cb, void *u) { static_cast<Rec *>(u)->r->record_shadow(cb); }
// Cagiranin gercek sirasi: birlestirme + UZERINE 2B arayuz. Bu yol ayrica
// birlestirmenin KENDI boru hatti duzenini baglamasinin UI'yi bozmadigini
// olcer (duzenler uyumsuz; UI kendi setini yeniden baglamali).
void rec_main_ui(VkCommandBuffer cb, void *u) {
  Rec *r = static_cast<Rec *>(u);
  r->r->record(cb);
  r->r->ui_record(cb);
}

// Sahne: SIYAH zemin uzerinde tek kucuk kup. Aydinlatma yalniz ortam isigi
// (diffuse 0) -> kupun butun yuzleri AYNI dogrusal degerde; parlaklik tek bir
// sayiyla (ambient) ayarlanir ve esik kapisi kesin olur.
struct Sahne {
  renderer::MeshHandle cube;
};
Sahne kur_sahne(renderer::Renderer &ren, float ambient) {
  Sahne s;
  renderer::Vertex v[24];
  uint32_t idx[36];
  const uint32_t n = renderer::Renderer::cube(v, idx);
  s.cube = ren.create_mesh(v, 24, idx, n);
  ren.set_light({0, 1, 0}, {ambient, ambient, ambient}, 0.0f); // yalniz ortam
  ren.set_camera(Mat4::look_at({0, 0, 3.0f}, {0, 0, 0}, {0, 1, 0}), Mat4::perspective(1.0f, 1.0f, 0.1f, 50.0f));
  return s;
}

// Nesnenin ekrandaki kutusunun DISINDA, halka bolgesinde kac piksel isikli?
// Bloom kapaliyken bu bolge zifiri siyah (temizleme rengi 0) olmali.
uint32_t halka_pikselleri(const uint8_t *px, uint32_t w, uint32_t h, uint32_t ic, uint32_t dis) {
  uint32_t n = 0;
  for (uint32_t y = 0; y < h; y++)
    for (uint32_t x = 0; x < w; x++) {
      const int dx = (int)x - (int)(w / 2), dy = (int)y - (int)(h / 2);
      const uint32_t r = (uint32_t)(dx < 0 ? -dx : dx) > (uint32_t)(dy < 0 ? -dy : dy) ? (uint32_t)(dx < 0 ? -dx : dx)
                                                                                      : (uint32_t)(dy < 0 ? -dy : dy);
      if (r < ic || r > dis) continue;
      const uint8_t *p = px + (y * w + x) * 4;
      if (p[0] > 2 || p[1] > 2 || p[2] > 2) n++;
    }
  return n;
}
// Halka bolgesinin ortalama parlakligi (0..765). Hale uzaklastikca SONMELI;
// sabit bir yikama (yanlis mip, sabit ekleme) bunu duz gosterirdi.
uint32_t halka_ortalama(const uint8_t *px, uint32_t w, uint32_t h, uint32_t ic, uint32_t dis) {
  uint64_t toplam = 0;
  uint32_t n = 0;
  for (uint32_t y = 0; y < h; y++)
    for (uint32_t x = 0; x < w; x++) {
      const int dx = (int)x - (int)(w / 2), dy = (int)y - (int)(h / 2);
      const uint32_t ax = (uint32_t)(dx < 0 ? -dx : dx), ay = (uint32_t)(dy < 0 ? -dy : dy);
      const uint32_t r = ax > ay ? ax : ay;
      if (r < ic || r > dis) continue;
      const uint8_t *p = px + (y * w + x) * 4;
      toplam += (uint64_t)p[0] + p[1] + p[2];
      n++;
    }
  return n ? (uint32_t)(toplam * 100 / n) : 0; // yuzde biri hassasiyetle
}
uint32_t merkez_pikselleri(const uint8_t *px, uint32_t w, uint32_t h, uint32_t ic) {
  uint32_t n = 0;
  for (uint32_t y = 0; y < h; y++)
    for (uint32_t x = 0; x < w; x++) {
      const int dx = (int)x - (int)(w / 2), dy = (int)y - (int)(h / 2);
      const uint32_t r = (uint32_t)(dx < 0 ? -dx : dx) > (uint32_t)(dy < 0 ? -dy : dy) ? (uint32_t)(dx < 0 ? -dx : dx)
                                                                                      : (uint32_t)(dy < 0 ? -dy : dy);
      if (r >= ic) continue;
      const uint8_t *p = px + (y * w + x) * 4;
      if (p[0] > 2 || p[1] > 2 || p[2] > 2) n++;
    }
  return n;
}
} // namespace

// --- 4. Gecis tablosu: derlenmis, sirali, tutarli --------------------------
// Bu kapi GPU istemez: tablo saf veridir ve tam da bu yuzden test edilebilir.
ENGINE_TEST(render_graph_table_is_compiled_and_ordered) {
  using namespace renderer;
  GraphPass g[kMaxGraphPasses];
  GraphDesc d;
  d.post = true;
  d.shadow = true;
  d.bloom_mips = 4;
  const uint32_t n = graph_build(d, g, kMaxGraphPasses);
  // golge + sahne + parlak + 3 indirgeme + 3 yukari + birlestirme = 10
  std::printf("    [bilgi] tablo (%u gecis):", n);
  for (uint32_t i = 0; i < n; i++) std::printf(" %s", g[i].name);
  std::printf("\n");
  CHECK(n == 10);
  const char *bekle[10] = {"golge",    "sahne_hdr", "parlak",  "indirge1", "indirge2",
                           "indirge3", "yukari2",   "yukari1", "yukari0",  "birlestir"};
  bool sira = n == 10;
  for (uint32_t i = 0; i < n && i < 10; i++)
    if (std::strcmp(g[i].name, bekle[i]) != 0) sira = false;
  CHECK(sira);
  CHECK(graph_validate(g, n) == nullptr);
  // TURETME gercekten yapildi mi: her girdinin ureticisi KENDINDEN ONCEKI bir
  // gecis, her ara ciktinin okuyucusu var, son gecis cagiranin hedefine yaziyor.
  bool turetildi = true;
  for (uint32_t i = 0; i < n; i++) {
    if (g[i].in0 != kResNone && g[i].producer0 == kResNone) turetildi = false;
    if (g[i].in1 != kResNone && g[i].producer1 == kResNone) turetildi = false;
    if (g[i].out != kResTarget && (!g[i].out_sampled || g[i].out_layout != GraphLayout::ShaderRead)) turetildi = false;
  }
  CHECK(turetildi);
  CHECK(g[n - 1].out == kResTarget);
  // Yukari zincir DOGRU esleniyor mu: yukari2 alt kaynagi indirgeme zincirinin
  // SON mip'i, yukari1'inki bir onceki YUKARI ciktisi.
  bool eslesme = g[6].in1 == kResDown && g[6].in1_level == 3 && g[7].in1 == kResUp && g[7].in1_level == 2 &&
                 g[8].in1 == kResUp && g[8].in1_level == 1;
  CHECK(eslesme);

  // post KAPALI: tablo yalniz golge + sahne, sahne dogrudan cagiranin hedefine.
  GraphDesc d2;
  d2.post = false;
  const uint32_t n2 = graph_build(d2, g, kMaxGraphPasses);
  CHECK(n2 == 2 && g[1].out == kResTarget && graph_validate(g, n2) == nullptr);

  // Faz 9: GPU cull gecisi tablonun BASINDA (golge de sahne de onun yazdigi
  // dolayli komutlari okur) ve ciktisi TAMPON oldugu icin out_external.
  GraphDesc d3;
  d3.post = false;
  d3.shadow = true;
  d3.cull = true;
  const uint32_t n3 = graph_build(d3, g, kMaxGraphPasses);
  std::printf("    [bilgi] cull ACIK tablo (%u gecis):", n3);
  for (uint32_t i = 0; i < n3; i++) std::printf(" %s", g[i].name);
  std::printf("\n");
  CHECK(n3 == 3);
  CHECK(std::strcmp(g[0].name, "cull") == 0 && g[0].kind == PassKind::Cull && g[0].out == kResCull);
  CHECK(g[0].out_external);
  CHECK(graph_validate(g, n3) == nullptr);
  // KONTROL: out_external gercekten tasiyici mi? Kaldirilinca AYNI tablo "olu
  // gecis" diye REDDEDILMELI; edilmezse yukaridaki nullptr bir sey olcmuyordur.
  g[0].out_external = false;
  const char *e3 = graph_validate(g, n3);
  std::printf("    [bilgi] kontrol: cull out_external kaldirildi -> \"%s\"\n", e3 ? e3 : "(kabul edildi!)");
  CHECK(e3 != nullptr);
  // Ust sinir: cull + golge + hareket + sahne + 6 mip bloom = 16 = kapasite.
  GraphDesc d4;
  d4.post = true;
  d4.shadow = true;
  d4.motion = true;
  d4.cull = true;
  d4.bloom_mips = kMaxBloomMips;
  const uint32_t n4 = graph_build(d4, g, kMaxGraphPasses);
  std::printf("    [bilgi] en genis tablo: %u gecis (kapasite %u)\n", n4, kMaxGraphPasses);
  CHECK(n4 == kMaxGraphPasses);
  CHECK(graph_validate(g, n4) == nullptr);

  // KONTROL: denetim gercekten denetliyor mu? Ureticisi olmayan girdi ve
  // okuyucusu olmayan cikti REDDEDILMELI; edilmezse yukaridaki nullptr bos.
  GraphPass bozuk[2];
  bozuk[0] = GraphPass{};
  bozuk[0].name = "sahne";
  bozuk[0].kind = PassKind::Scene;
  bozuk[0].out = kResHdr;
  bozuk[0].out_sampled = true; // turetilmis gibi doldurulur: hedef DIGER hata
  bozuk[0].out_layout = GraphLayout::ShaderRead;
  bozuk[1] = GraphPass{};
  bozuk[1].name = "birlestir";
  bozuk[1].kind = PassKind::Compose;
  bozuk[1].in0 = kResHdr;
  bozuk[1].producer0 = 0;
  bozuk[1].in1 = kResUp; // kimse uretmedi: ureticisiz girdi
  bozuk[1].out = kResTarget;
  const char *e1 = graph_validate(bozuk, 2);
  GraphPass olu[2];
  olu[0] = GraphPass{};
  olu[0].name = "olu";
  olu[0].kind = PassKind::Bright;
  olu[0].out = kResDown; // hic okunmuyor
  olu[1] = GraphPass{};
  olu[1].name = "birlestir";
  olu[1].kind = PassKind::Compose;
  olu[1].out = kResTarget;
  const char *e2 = graph_validate(olu, 2);
  std::printf("    [bilgi] kontrol: ureticisiz girdi -> \"%s\"; olu gecis -> \"%s\"\n", e1 ? e1 : "(kabul edildi!)",
              e2 ? e2 : "(kabul edildi!)");
  CHECK(e1 != nullptr);
  CHECK(e2 != nullptr);
}

// --- 1. post KAPALIYKEN cikti bugunku yolla BIT BIT ayni -------------------
ENGINE_TEST(render_graph_post_off_is_bit_identical) {
  if (!loader_ok()) { skip("Vulkan loader yok"); return; }
  OrtakGpu &g = ortak_gpu();
  if (!g.ok) { skip("Vulkan cihazi yok"); return; }
  Device &dev = g.dev;
  SystemArena &sys = g.sys;
  const uint32_t W = 192, H = 192;
  OffscreenConfig oc;
  oc.srgb = true;
  oc.width = W;
  oc.height = H;
  OffscreenResult ores;
  OffscreenTarget *off = offscreen_create(dev, sys, oc, &ores);
  if (!off) { CHECK(false); std::printf("    [bilgi] offscreen: %s\n", ores.error); return; }

  static uint8_t a_px[W * H * 4], b_px[W * H * 4], post_px[W * H * 4];
  renderer::Renderer ren;
  renderer::RendererConfig rc;
  rc.frames_in_flight = 1;
  rc.shadow_size = 512;
  rc.max_draws = 64;
  bool ok = ren.init(dev, sys, offscreen_render_pass(off), rc);
  CHECK(ok);
  if (!ok) { offscreen_destroy(off); return; }
  CHECK(!ren.post().enabled); // varsayilan KAPALI olmali
  CHECK(!ren.cull_recorded().enabled); // GPU cull da varsayilan KAPALI
  std::printf("    [bilgi] cull kapali sebebi: %s\n", ren.cull_recorded().disabled_reason);
  std::printf("    [bilgi] post kapali sebebi: %s\n", ren.post().disabled_reason);
  Sahne s = kur_sahne(ren, 1.0f);
  ren.set_render_size(W, H);
  ren.set_shadow_volume({0, 0, 0}, 4.0f, 20.0f);
  Rec rr{&ren};
  for (int i = 0; i < 2; i++) {
    ren.begin_frame(0);
    ren.draw(s.cube, Mat4::scale({0.6f, 0.6f, 0.6f}), {1, 1, 1});
    bool r = offscreen_render_custom(off, oc, rec_main, &rr, &ores, rec_before);
    CHECK(r);
    if (!r) { std::printf("    [bilgi] kare: %s\n", ores.error); break; }
    std::memcpy(i == 0 ? a_px : b_px, ores.pixels, sizeof a_px);
  }
  uint32_t fark = 0;
  for (uint32_t i = 0; i < sizeof a_px; i++)
    if (a_px[i] != b_px[i]) fark++;
  std::printf("    [bilgi] post KAPALI iki kare farki: %u bayt (0 bekleniyor)\n", fark);
  CHECK(fark == 0);
  ren.shutdown();

  // KONTROL: ayni sahne post ACIKKEN farkli olmali. Fark 0 cikarsa yukaridaki
  // "0" bir sey olcmuyor demektir (ornegin cizim hic degismiyordur).
  renderer::Renderer ren2;
  renderer::RendererConfig rc2 = rc;
  rc2.post = true;
  rc2.post_width = W;
  rc2.post_height = H;
  rc2.bloom_intensity = 0.8f;
  rc2.bloom_threshold = 0.2f;
  bool ok2 = ren2.init(dev, sys, offscreen_render_pass(off), rc2);
  CHECK(ok2);
  if (ok2) {
    if (!ren2.post().enabled) {
      skip("HDR bicimi yok: post kontrolu kosmadi");
      std::printf("    [bilgi] sebep: %s\n", ren2.post().disabled_reason);
    } else {
      Sahne s2 = kur_sahne(ren2, 1.0f);
      ren2.set_render_size(W, H);
      ren2.set_shadow_volume({0, 0, 0}, 4.0f, 20.0f);
      Rec rr2{&ren2};
      ren2.begin_frame(0);
      ren2.draw(s2.cube, Mat4::scale({0.6f, 0.6f, 0.6f}), {1, 1, 1});
      if (offscreen_render_custom(off, oc, rec_main, &rr2, &ores, rec_before)) {
        std::memcpy(post_px, ores.pixels, sizeof post_px);
        uint32_t fark2 = 0;
        for (uint32_t i = 0; i < sizeof a_px; i++)
          if (a_px[i] != post_px[i]) fark2++;
        std::printf("    [bilgi] kontrol: post ACIK fark %u bayt (> 0 bekleniyor)\n", fark2);
        CHECK(fark2 > 0);
      } else {
        CHECK(false);
      }
    }
    ren2.shutdown();
  }
  offscreen_destroy(off);
}

// --- 2 + 3 + 5. Bloom yayiyor, SECICI ve bicim/bellek raporlanir -----------
// Sahne: siyah zemin uzerinde tek kup, yalniz ortam isigi. Kupun DOGRUSAL
// parlakligi ambient'tir; esik kapisi bu tek sayiyla kesin olarak surulur.
ENGINE_TEST(render_graph_bloom_spreads_only_above_threshold) {
  if (!loader_ok()) { skip("Vulkan loader yok"); return; }
  OrtakGpu &g = ortak_gpu();
  if (!g.ok) { skip("Vulkan cihazi yok"); return; }
  Device &dev = g.dev;
  SystemArena &sys = g.sys;
  if (test::gpu_is_virtual(dev.caps().device_name)) {
    skip("sanal GPU (Apple Paravirtual, CI macOS): piksel kapisi gercek cihazda olculur");
    return;
  }
  const uint32_t W = 256, H = 256;
  OffscreenConfig oc;
  oc.srgb = true;
  oc.width = W;
  oc.height = H;
  OffscreenResult ores;
  OffscreenTarget *off = offscreen_create(dev, sys, oc, &ores);
  if (!off) { CHECK(false); std::printf("    [bilgi] offscreen: %s\n", ores.error); return; }
  renderer::Renderer ren;
  renderer::RendererConfig rc;
  rc.frames_in_flight = 1;
  rc.shadow_size = 0; // golge yok: olcum yalniz bloom'u gorsun
  rc.max_draws = 64;
  rc.post = true;
  rc.post_width = W;
  rc.post_height = H;
  rc.bloom_mips = 4;
  bool ok = ren.init(dev, sys, offscreen_render_pass(off), rc);
  CHECK(ok);
  if (!ok) { offscreen_destroy(off); return; }
  const renderer::PostInfo pi = ren.post();
  if (!pi.enabled) {
    // 5. kapi: sessiz kapanma YOK — sebep basilir, test GORUNUR atlanir.
    std::printf("    [bilgi] sebep: %s\n", pi.disabled_reason);
    skip("son islem kurulamadi (HDR bicimi/hedefi yok): bloom kapilari kosmadi");
    ren.shutdown();
    offscreen_destroy(off);
    return;
  }
  std::printf("    [bilgi] HDR bicimi %d (%s), hedef %ux%u, bloom %ux%u x %u mip, ic hedefler %.2f MB\n",
              (int)pi.hdr_format,
              pi.hdr_format == VK_FORMAT_B10G11R11_UFLOAT_PACK32 ? "B10G11R11, mobil ucuz" : "RGBA16F, yedek",
              pi.width, pi.height, pi.bloom_width, pi.bloom_height, pi.bloom_mips,
              (double)pi.target_bytes / (1024.0 * 1024.0));
  std::printf("    [bilgi] gecis tablosu (%u):", pi.pass_count);
  for (uint32_t i = 0; i < pi.pass_count; i++) std::printf(" %s", pi.pass_name[i]);
  std::printf("\n");
  bool bicim = pi.hdr_format == VK_FORMAT_B10G11R11_UFLOAT_PACK32 || pi.hdr_format == VK_FORMAT_R16G16B16A16_SFLOAT;
  CHECK(bicim);
  // Golge KAPALI oldugu icin tabloda golge gecisi yok: 9 gecis.
  CHECK(pi.pass_count == 9);
  CHECK(std::strcmp(pi.pass_name[0], "sahne_hdr") == 0);
  CHECK(std::strcmp(pi.pass_name[pi.pass_count - 1], "birlestir") == 0);

  Sahne s = kur_sahne(ren, 1.0f);
  CHECK(s.cube.valid());
  ren.set_render_size(W, H);
  Rec rr{&ren};
  struct Olcum {
    uint32_t halka = 0, merkez = 0, yakin = 0, uzak = 0;
  };
  // Kup ekranda r <= ~27 piksel; halka 36..90 arasi, yani nesnenin DISI.
  auto ciz = [&](float ambient, float esik, float yogunluk, Olcum *o) {
    ren.set_light({0, 1, 0}, {ambient, ambient, ambient}, 0.0f);
    ren.set_bloom(esik, yogunluk);
    ren.begin_frame(0);
    ren.draw(s.cube, Mat4::scale({0.6f, 0.6f, 0.6f}), {1, 1, 1});
    if (!offscreen_render_custom(off, oc, rec_main, &rr, &ores, rec_before)) return false;
    o->halka = halka_pikselleri(ores.pixels, W, H, 36, 90);
    o->merkez = merkez_pikselleri(ores.pixels, W, H, 20);
    o->yakin = halka_ortalama(ores.pixels, W, H, 36, 50);
    o->uzak = halka_ortalama(ores.pixels, W, H, 76, 90);
    return true;
  };

  // --- 2. Parlak nesne cevresini AYDINLATIR --------------------------------
  Olcum parlak, yogunluk_sifir, esik_yuksek;
  bool r1 = ciz(6.0f, 1.0f, 0.8f, &parlak);
  bool r2 = ciz(6.0f, 1.0f, 0.0f, &yogunluk_sifir); // KONTROL: bloom yok
  bool r3 = ciz(6.0f, 100.0f, 0.8f, &esik_yuksek);  // KONTROL: esik ustunde enerji yok
  CHECK(r1 && r2 && r3);
  std::printf("    [bilgi] parlak nesne (dogrusal 6.0): halka %u px (bloom), kontrol yogunluk0 %u, esik100 %u; nesne %u px\n",
              parlak.halka, yogunluk_sifir.halka, esik_yuksek.halka, parlak.merkez);
  CHECK(parlak.merkez > 500);   // nesne gercekten cizildi (yoksa "halka yok" bos olcum)
  CHECK(parlak.halka > 200);    // bloom YAYIYOR
  CHECK(yogunluk_sifir.halka == 0); // kontrol: yogunluk 0 -> yayilim yok
  CHECK(esik_yuksek.halka == 0);    // kontrol: esik 100 -> hicbir sey esigi gecmiyor
  // Hale nesneden uzaklastikca SONMELI: duz bir yikama (yanlis mip / sabit
  // ekleme) bu iki olcumu esitlerdi.
  std::printf("    [bilgi] hale sonumu: yakin halka %.2f, uzak halka %.2f (100 = 1 birim toplam RGB)\n",
              parlak.yakin / 100.0, parlak.uzak / 100.0);
  CHECK(parlak.yakin > parlak.uzak * 2);

  // --- 3. SECICI: esik altindaki nesne yayilmaz ----------------------------
  Olcum sonuk, sonuk_dusuk_esik;
  bool r4 = ciz(0.4f, 1.0f, 0.8f, &sonuk);             // dogrusal 0.4 < esik 1.0
  bool r5 = ciz(0.4f, 0.05f, 0.8f, &sonuk_dusuk_esik); // KONTROL: esik dusunce yayilmali
  CHECK(r4 && r5);
  std::printf("    [bilgi] sonuk nesne (dogrusal 0.4): halka %u px (esik 1.0), kontrol esik 0.05 -> %u px; nesne %u px\n",
              sonuk.halka, sonuk_dusuk_esik.halka, sonuk.merkez);
  CHECK(sonuk.merkez > 500); // nesne cizildi: "yayilmadi" bos olcum degil
  CHECK(sonuk.halka == 0);   // esik altinda: YAYILMAZ
  CHECK(sonuk_dusuk_esik.halka > 200); // kontrol: esik dusurulunce yayilir

  ren.shutdown();
  offscreen_destroy(off);
}

// --- 6. post yolunda Mali en iyi uygulama denetimi -------------------------
// Mevcut renderer_mali_best_practices_gate post KAPALIYKEN olcuyor; yeni
// gecisler (HDR + bloom zinciri) o kapinin disinda kalirdi. Ayni denetim, post
// ACIKKEN: Arm kimlikli uyari 0 olmali, dogrulama hatasi 0 olmali.
ENGINE_TEST(render_graph_post_mali_best_practices) {
  if (!loader_bp_ok()) { skip("Vulkan loader yok"); return; }
  static SystemArena sys;
  if (!sys.reserve(160u << 20, "graph_bp")) { CHECK(false); return; }
  Device dev;
  DeviceConfig dc;
  dc.validation = true;
  dc.best_practices = true;
  if (!dev.init(sys, g_api_bp, dc)) { skip("Vulkan cihazi yok"); return; }
  if (!dev.caps().validation_layer) {
    skip("VK_LAYER_KHRONOS_validation yok — post yolunun Mali denetimi kosmadi");
    dev.shutdown();
    return;
  }
  CHECK(dev.caps().best_practices);
  CHECK(dev.caps().debug_messenger); // mesaj kanali yoksa asagidaki 0'lar olcum degil
  const uint32_t W = 128, H = 128;
  OffscreenConfig oc;
  oc.srgb = true;
  oc.width = W;
  oc.height = H;
  OffscreenResult ores;
  OffscreenTarget *off = offscreen_create(dev, sys, oc, &ores);
  if (!off) { CHECK(false); std::printf("    [bilgi] offscreen: %s\n", ores.error); dev.shutdown(); return; }
  renderer::Renderer ren;
  renderer::RendererConfig rc;
  rc.frames_in_flight = 1;
  rc.shadow_size = 512;
  rc.max_draws = 64;
  rc.post = true;
  rc.post_width = W;
  rc.post_height = H;
  bool ok = ren.init(dev, sys, offscreen_render_pass(off), rc);
  CHECK(ok);
  if (!ok) { offscreen_destroy(off); dev.shutdown(); return; }
  if (!ren.post().enabled) {
    std::printf("    [bilgi] sebep: %s\n", ren.post().disabled_reason);
    skip("son islem kurulamadi: post yolunun Mali denetimi kosmadi");
    ren.shutdown();
    offscreen_destroy(off);
    dev.shutdown();
    return;
  }
  Sahne s = kur_sahne(ren, 4.0f);
  ren.set_render_size(W, H);
  ren.set_shadow_volume({0, 0, 0}, 4.0f, 20.0f);
  Rec rr{&ren};
  for (int frame = 0; frame < 3; frame++) {
    ren.begin_frame(0);
    ren.draw(s.cube, Mat4::scale({0.6f, 0.6f, 0.6f}), {1, 1, 1});
    ren.ui_begin((float)W, (float)H, 0.0f);
    ren.ui_rect(4, 4, 40, 12, renderer::Renderer::rgba(255, 255, 255, 200));
    bool r = offscreen_render_custom(off, oc, rec_main_ui, &rr, &ores, rec_before);
    CHECK(r);
    if (!r) { std::printf("    [bilgi] kare: %s\n", ores.error); break; }
  }
  // UI gercekten kaydedildi mi (yoksa "birlestirme UI'yi bozmuyor" bos iddia).
  std::printf("    [bilgi] UI kosesi: %u vertex, %u dusen; cizim %u\n", ren.ui_stats().vertices, ren.ui_stats().dropped,
              ren.stats().draws);
  CHECK(ren.ui_stats().vertices == 6);
  CHECK(ren.stats().draws == 1);
  const uint32_t bp_all = dev.best_practice_warnings(), bp_arm = dev.best_practice_arm_warnings();
  std::printf("    [bilgi] post ACIK — BestPractices: %u uyari (%u Arm), %u benzersiz kimlik, %u dogrulama hatasi\n",
              bp_all, bp_arm, dev.best_practice_id_count(), dev.validation_errors());
  for (uint32_t i = 0; i < dev.best_practice_id_count(); i++) {
    const Device::BpId &b = dev.best_practice_id(i);
    std::printf("    [bilgi]   %s x%u%s\n", b.name, b.count, b.arm ? "  <- Mali" : "");
  }
  CHECK(dev.validation_errors() == 0);
  // "sparse-index-buffer": katmanin taramasi alt-ayirma offset'ini atliyor (VVL
  // issue 45); ayni kural CPU'da dogru offsetle olculur ve 0 ise katmanin bu
  // kimligi sahte pozitiftir. Baska hicbir kimlik dusulmez.
  const uint32_t sparse_layer = dev.best_practice_count("sparse-index-buffer");
  CHECK(ren.sparse_mesh_count() == 0);
  const uint32_t bp_arm_effective = ren.sparse_mesh_count() == 0 ? bp_arm - sparse_layer : bp_arm;
  CHECK(bp_arm_effective == 0);

  // POZITIF KONTROL: Arm kurali gercekten acik mi? Kural TANINMIYORSA bu kapi
  // hicbir sey olcemez — ustteki `bp_arm_effective == 0` de 0 uyari ile BOSA
  // gecerdi. O yuzden sonuc CHECK degil GORUNUR ATLAMA (bkz. test.hpp).
  if (test::arm_rules_missing(dev)) {
    ren.shutdown();
    offscreen_destroy(off);
    dev.shutdown();
    skip(test::kArmRulesMissingReason);
    return;
  }
  ren.shutdown();
  offscreen_destroy(off);
  dev.shutdown();
}

// ===========================================================================
// 7. FAZ 9 — GPU cull + dolayli (indirect) cizim
// ---------------------------------------------------------------------------
// Kapilar ve KONTROLLERI:
//   a) cull ACIK ile KAPALI ayni sahnede piksel farki 0 (gorunen hicbir nesne
//      elenmedi) — KONTROL: kamera cevrilip sahne tamamen disari alininca
//      hayatta kalan 0 VE ekran cizimsiz kareyle bit bit ayni.
//   b) frustum disina konan N nesne icin culled == N — KONTROL: hepsi
//      icerideyken culled == 0 (yani sayac gercekten eleme sayiyor).
//   c) dolayli yol: GPU sayacindan okunan cizim sayisi beklenen hayatta kalan
//      sayisiyla esit; CPU'nun verdigi komut sayisi KUME sayisi kadar.
//   d) destek yoksa disabled_reason dolu ve GORUNUR atlama (sessiz return yok).
// ===========================================================================
namespace {
// Sahne: kucuk bir ZEMIN (golge alir) + ic tanesi kameranin onunde izgarada,
// dis tanesi frustum'un TAMAMEN disinda kup. Zemin ayri mesh oldugu icin kume
// sayisi 2 olur: coklu kume yolu da olculur. Zemin 4x4 ve merkezde — KONTROL
// kamerasi (arkaya bakan) icin de tamamen disarida kalir.
constexpr uint32_t kIcerideKup = 16, kDisaridaKup = 8;
struct KupSahne {
  renderer::MeshHandle cube, zemin;
};
KupSahne kur_kup_sahnesi(renderer::Renderer &ren) {
  KupSahne s;
  renderer::Vertex v[24];
  uint32_t idx[36];
  const uint32_t n = renderer::Renderer::cube(v, idx);
  s.cube = ren.create_mesh(v, 24, idx, n);
  renderer::Vertex pv[4];
  uint32_t pidx[6];
  const uint32_t pn = renderer::Renderer::plane(pv, pidx, 1.0f);
  s.zemin = ren.create_mesh(pv, 4, pidx, pn);
  ren.set_light({0.3f, 1.0f, 0.4f}, {0.25f, 0.26f, 0.30f}, 0.9f);
  ren.set_shadow_volume({0, 0, 0}, 3.0f, 30.0f);
  return s;
}
void kamera_onde(renderer::Renderer &ren) {
  ren.set_camera(Mat4::look_at({0, 0, 3.0f}, {0, 0, 0}, {0, 1, 0}), Mat4::perspective(1.0f, 1.0f, 0.1f, 50.0f));
}
// KONTROL kamerasi: tam ters yone bakar, sahnenin tamami disarida kalir.
void kamera_arkada(renderer::Renderer &ren) {
  ren.set_camera(Mat4::look_at({0, 0, 3.0f}, {0, 0, 30.0f}, {0, 1, 0}), Mat4::perspective(1.0f, 1.0f, 0.1f, 50.0f));
}
// Cizim sirasi: once zemin (kume 0), sonra kupler (kume 1). Dis kupler de ayni
// mesh+malzeme oldugu icin kume 1'in devamidir.
void ciz_kupler(renderer::Renderer &ren, const KupSahne &s, uint32_t ic, uint32_t dis) {
  const renderer::MeshHandle cube = s.cube;
  ren.draw(s.zemin, Mat4::translate({0.0f, -1.2f, 0.0f}) * Mat4::scale({4.0f, 1.0f, 4.0f}), {0.8f, 0.8f, 0.8f});
  for (uint32_t i = 0; i < ic; i++) {
    const float x = -0.9f + 0.6f * (float)(i % 4);
    const float y = -0.9f + 0.6f * (float)(i / 4);
    ren.draw(cube, Mat4::translate({x, y, 0.0f}) * Mat4::scale({0.25f, 0.25f, 0.25f}), {1, 1, 1});
  }
  // Frustum'un SAGINDA ve cok uzakta: yan duzlem de uzak duzlem de eler.
  for (uint32_t i = 0; i < dis; i++)
    ren.draw(cube, Mat4::translate({120.0f + 4.0f * (float)i, 0.0f, 0.0f}) * Mat4::scale({0.25f, 0.25f, 0.25f}),
             {1, 1, 1});
}
// OLCUM sahnesi: ic tanesi frustum icinde sik bir izgarada, dis tanesi cok
// uzakta. Kume sayisi yine 1 (tek mesh + tek malzeme): "CPU kac komut veriyor"
// sorusunun olcegi burada gorunur.
void ciz_olcek(renderer::Renderer &ren, const KupSahne &s, uint32_t ic, uint32_t dis) {
  for (uint32_t i = 0; i < ic; i++) {
    const float x = -1.0f + 2.0f * (float)(i % 32) / 31.0f;
    const float y = -1.0f + 2.0f * (float)((i / 32) % 16) / 15.0f;
    ren.draw(s.cube, Mat4::translate({x, y, 0.0f}) * Mat4::scale({0.1f, 0.1f, 0.1f}), {1, 1, 1});
  }
  for (uint32_t i = 0; i < dis; i++)
    ren.draw(s.cube, Mat4::translate({200.0f + 0.5f * (float)i, 0.0f, 0.0f}) * Mat4::scale({0.1f, 0.1f, 0.1f}),
             {1, 1, 1});
}
// CPU REFERANSI: ayni frustum testi (cull.hpp), ayni donusumler. GPU'nun
// verdigi sayi buna karsi olculur — ikisi ayrilirsa hangisinin yanildigi
// bellidir ve "16 bekliyorum, 16 geldi" tautolojisi olmaz.
// Yerel kure yaricaplari: kup [-0.5,0.5]^3 -> sqrt(3)/2, duzlem 1x1 -> sqrt(2)/2.
uint32_t beklenen_gorunur(const Mat4 &view, const Mat4 &proj, uint32_t ic, uint32_t dis) {
  const renderer::Frustum f = renderer::frustum_from_viewproj(proj * view);
  const float kup_r = 0.8660254f, duzlem_r = 0.7071068f;
  uint32_t n = 0;
  auto say = [&](const Mat4 &m, float r) {
    if (renderer::sphere_in_frustum(f, renderer::world_sphere_center(m, {0, 0, 0}),
                                    renderer::world_sphere_radius(m, r)))
      n++;
  };
  say(Mat4::translate({0.0f, -1.2f, 0.0f}) * Mat4::scale({4.0f, 1.0f, 4.0f}), duzlem_r);
  for (uint32_t i = 0; i < ic; i++) {
    const float x = -0.9f + 0.6f * (float)(i % 4);
    const float y = -0.9f + 0.6f * (float)(i / 4);
    say(Mat4::translate({x, y, 0.0f}) * Mat4::scale({0.25f, 0.25f, 0.25f}), kup_r);
  }
  for (uint32_t i = 0; i < dis; i++)
    say(Mat4::translate({120.0f + 4.0f * (float)i, 0.0f, 0.0f}) * Mat4::scale({0.25f, 0.25f, 0.25f}), kup_r);
  return n;
}
uint32_t bayt_farki(const uint8_t *a, const uint8_t *b, uint32_t n) {
  uint32_t f = 0;
  for (uint32_t i = 0; i < n; i++)
    if (a[i] != b[i]) f++;
  return f;
}
} // namespace

ENGINE_TEST(render_graph_gpu_cull_indirect_matches_cpu_path) {
  if (!loader_ok()) { skip("Vulkan loader yok"); return; }
  OrtakGpu &g = ortak_gpu();
  if (!g.ok) { skip("Vulkan cihazi yok"); return; }
  Device &dev = g.dev;
  SystemArena &sys = g.sys;
  if (test::gpu_is_virtual(dev.caps().device_name)) {
    skip("sanal GPU (Apple Paravirtual): piksel kapisi gercek cihazda olculur");
    return;
  }
  const uint32_t W = 192, H = 192;
  OffscreenConfig oc;
  oc.srgb = true;
  oc.width = W;
  oc.height = H;
  OffscreenResult ores;
  OffscreenTarget *off = offscreen_create(dev, sys, oc, &ores);
  if (!off) { CHECK(false); std::printf("    [bilgi] offscreen: %s\n", ores.error); return; }
  static uint8_t cpu_px[W * H * 4], gpu_px[W * H * 4], bos_px[W * H * 4], kontrol_px[W * H * 4];

  renderer::RendererConfig rc;
  rc.frames_in_flight = 1;
  rc.shadow_size = 512;
  rc.shadow_cascades = 3;
  rc.max_draws = 2048; // olcum gecisi 1024 cizim kullaniyor

  // --- Referans: bugunku CPU yolu (gpu_cull KAPALI) -------------------------
  {
    renderer::Renderer ren;
    bool ok = ren.init(dev, sys, offscreen_render_pass(off), rc);
    CHECK(ok);
    if (!ok) { offscreen_destroy(off); return; }
    const KupSahne sahne = kur_kup_sahnesi(ren);
    ren.set_render_size(W, H);
    kamera_onde(ren);
    Rec rr{&ren};
    ren.begin_frame(0);
    ciz_kupler(ren, sahne, kIcerideKup, kDisaridaKup);
    const bool r = offscreen_render_custom(off, oc, rec_main, &rr, &ores, rec_before);
    CHECK(r);
    if (r) std::memcpy(cpu_px, ores.pixels, sizeof cpu_px);
    std::printf("    [bilgi] CPU yolu: cizim %u, malzeme baglama %u\n", ren.stats().draws, ren.stats().material_binds);
    CHECK(ren.stats().draws == 1 + kIcerideKup + kDisaridaKup); // + zemin
    ren.shutdown();
  }

  // --- Dolayli yol (gpu_cull ACIK) -----------------------------------------
  renderer::RendererConfig rcc = rc;
  rcc.gpu_cull = true;
  rcc.gpu_cull_shadow = true;
  rcc.max_cull_batches = 32;
  renderer::Renderer ren;
  bool ok = ren.init(dev, sys, offscreen_render_pass(off), rcc);
  CHECK(ok);
  if (!ok) { offscreen_destroy(off); return; }
  const renderer::CullInfo ci0 = ren.cull_recorded();
  if (!ci0.enabled) {
    // d) Sessiz kapanma YOK: sebep basilir, kapi GORUNUR atlanir.
    std::printf("    [bilgi] sebep: %s\n", ci0.disabled_reason);
    skip("GPU cull kurulamadi: dolayli cizim kapilari kosmadi");
    ren.shutdown();
    offscreen_destroy(off);
    return;
  }
  std::printf("    [bilgi] graph tablosu (%u gecis):", ren.graph_pass_count());
  for (uint32_t i = 0; i < ren.graph_pass_count(); i++) std::printf(" %s", ren.graph_pass_name(i));
  std::printf("\n");
  CHECK(ren.graph_pass_count() == 3); // cull + golge + sahne
  CHECK(std::strcmp(ren.graph_pass_name(0), "cull") == 0);
  std::printf("    [bilgi] cull ACIK: golge cull %s%s, GPU tamponlari %.2f KB, zaman damgasi %s\n",
              ci0.shadow ? "acik" : "kapali", ci0.shadow ? "" : " (sebep yukarida)",
              (double)ci0.gpu_bytes / 1024.0, dev.caps().timestamps ? "var" : "yok");
  const KupSahne sahne = kur_kup_sahnesi(ren);
  ren.set_render_size(W, H);
  Rec rr{&ren};

  // (1) Cizimsiz kare: "bos ekran" referansi (kontrol kapisi bunu kullanir).
  kamera_onde(ren);
  ren.begin_frame(0);
  bool rb = offscreen_render_custom(off, oc, rec_main, &rr, &ores, rec_before);
  CHECK(rb);
  if (rb) std::memcpy(bos_px, ores.pixels, sizeof bos_px);
  {
    const renderer::CullInfo c = ren.cull();
    CHECK(c.counts_valid);
    CHECK(c.candidates == 0 && c.survived == 0 && c.batches == 0);
  }

  // (2) a + b + c: ayni sahne, dolayli yol.
  kamera_onde(ren);
  ren.begin_frame(0);
  ciz_kupler(ren, sahne, kIcerideKup, kDisaridaKup);
  bool rg = offscreen_render_custom(off, oc, rec_main, &rr, &ores, rec_before);
  CHECK(rg);
  if (rg) std::memcpy(gpu_px, ores.pixels, sizeof gpu_px);
  const renderer::CullInfo c1 = ren.cull();
  std::printf("    [bilgi] aday %u, kume %u, hayatta kalan %u, elenen %u, CPU yolunda %u, frustum %u\n", c1.candidates,
              c1.batches, c1.survived, c1.culled, c1.cpu_draws, c1.frusta);
  if (c1.timing)
    std::printf("    [bilgi] cull compute suresi: %.4f ms\n", (double)c1.compute_ms);
  else
    std::printf("    [bilgi] cull compute suresi OLCULEMEDI: %s\n",
                c1.timing_reason[0] ? c1.timing_reason : "(sebep yok)");
  std::printf("    [bilgi] golge (isik frustum'u): aday %u, hayatta kalan %u%s\n", c1.shadow_candidates,
              c1.shadow_survived, c1.shadow ? "" : " (golge cull KAPALI)");
  // GPU sayaci CPU referansiyla ayni mi (ayni frustum, ayni kure)?
  const uint32_t cpu_beklenen =
      beklenen_gorunur(Mat4::look_at({0, 0, 3.0f}, {0, 0, 0}, {0, 1, 0}), Mat4::perspective(1.0f, 1.0f, 0.1f, 50.0f),
                       kIcerideKup, kDisaridaKup);
  std::printf("    [bilgi] CPU referansi (cull.hpp frustum testi): %u gorunur; GPU: %u\n", cpu_beklenen, c1.survived);
  CHECK(c1.survived == cpu_beklenen);
  CHECK(c1.counts_valid);
  CHECK(c1.candidates == 1 + kIcerideKup + kDisaridaKup);
  CHECK(c1.batches == 2);      // zemin + kupler: iki mesh -> IKI dolayli komut
  CHECK(c1.cpu_draws == 0);    // hicbir cizim CPU yoluna dusmedi
  // b) Eleme gercekten oluyor:
  CHECK(c1.culled == kDisaridaKup);
  CHECK(c1.survived == 1 + kIcerideKup);
  // c) GPU'nun gercekten cizdigi ornek sayisi (cull() stats'i gunceller):
  CHECK(ren.stats().draws == 1 + kIcerideKup);
  // Golge kademeleri de ayni cull'dan gecti mi (3 kademe x aday):
  if (c1.shadow) {
    CHECK(c1.frusta == 4);
    CHECK(c1.shadow_candidates == (1 + kIcerideKup + kDisaridaKup) * 3);
    CHECK(c1.shadow_survived > 0);
    CHECK(c1.shadow_survived < c1.shadow_candidates); // uzaktakiler kademede de elendi
  }
  // a) Piksel farki 0:
  const uint32_t fark = bayt_farki(cpu_px, gpu_px, sizeof cpu_px);
  std::printf("    [bilgi] cull ACIK/KAPALI piksel farki: %u bayt (0 bekleniyor)\n", fark);
  CHECK(fark == 0);
  // Olcum bos olmasin: sahne gercekten cizildi mi (bos ekrandan farkli mi)?
  const uint32_t doluluk = bayt_farki(bos_px, gpu_px, sizeof bos_px);
  std::printf("    [bilgi] sahne dolulugu: %u bayt bos kareden farkli (> 0 bekleniyor)\n", doluluk);
  CHECK(doluluk > 0);

  // KONTROL 1 (b): hepsi iceride -> elenen 0. Sayac "her zaman N" demiyor.
  kamera_onde(ren);
  ren.begin_frame(0);
  ciz_kupler(ren, sahne, kIcerideKup, 0);
  CHECK(offscreen_render_custom(off, oc, rec_main, &rr, &ores, rec_before));
  const renderer::CullInfo c2 = ren.cull();
  std::printf("    [bilgi] kontrol (hepsi iceride): aday %u, hayatta kalan %u, elenen %u\n", c2.candidates, c2.survived,
              c2.culled);
  CHECK(c2.candidates == 1 + kIcerideKup && c2.survived == 1 + kIcerideKup && c2.culled == 0);

  // KONTROL 2 (a): kamera cevrildi -> hayatta kalan 0 ve ekran BOS.
  kamera_arkada(ren);
  ren.begin_frame(0);
  ciz_kupler(ren, sahne, kIcerideKup, kDisaridaKup);
  bool rk = offscreen_render_custom(off, oc, rec_main, &rr, &ores, rec_before);
  CHECK(rk);
  if (rk) std::memcpy(kontrol_px, ores.pixels, sizeof kontrol_px);
  const renderer::CullInfo c3 = ren.cull();
  const uint32_t bos_fark = bayt_farki(bos_px, kontrol_px, sizeof bos_px);
  std::printf("    [bilgi] kontrol (kamera cevrildi): aday %u, hayatta kalan %u, ekran farki %u bayt (0 bekleniyor)\n",
              c3.candidates, c3.survived, bos_fark);
  CHECK(c3.candidates == 1 + kIcerideKup + kDisaridaKup);
  CHECK(c3.survived == 0);
  CHECK(ren.stats().draws == 0);
  CHECK(bos_fark == 0);

  // --- OLCUM (kapi degil, bilgi): 1024 cizim, yarisi disarida ---------------
  // Buradaki asil sayi "kume": CPU bu karede 1024 vkCmdDrawIndexed yerine
  // KUME SAYISI kadar vkCmdDrawIndexedIndirect verir; hangi orneklerin
  // cizilecegine GPU karar verir.
  {
    const uint32_t ic = 512, dis = 512;
    kamera_onde(ren);
    ren.begin_frame(0);
    ciz_olcek(ren, sahne, ic, dis);
    CHECK(offscreen_render_custom(off, oc, rec_main, &rr, &ores, rec_before));
    const renderer::CullInfo c = ren.cull();
    std::printf("    [olcum] 1024 cizim: aday %u, kume %u (CPU'nun verdigi dolayli komut), hayatta kalan %u, elenen %u\n",
                c.candidates, c.batches, c.survived, c.culled);
    if (c.timing)
      std::printf("    [olcum] cull compute (kamera + 3 golge kademesi = %u frustum): %.4f ms\n", c.frusta,
                  (double)c.compute_ms);
    else
      std::printf("    [olcum] cull compute suresi OLCULEMEDI: %s\n", c.timing_reason);
    CHECK(c.candidates == ic + dis);
    CHECK(c.batches == 1);
    CHECK(c.survived == ic && c.culled == dis);
  }

  ren.shutdown();
  offscreen_destroy(off);
}

// --- 7b. cull + son islem (derlenmis graph): sahne ic HDR gecisine dolayli --
// Dolayli boru hatlari post ACIKKEN hdr_rp_'ye baglanir; bu kombinasyon ayri
// bir sessiz kirilma noktasi, kendi kapisi var.
ENGINE_TEST(render_graph_gpu_cull_with_post_is_pixel_identical) {
  if (!loader_ok()) { skip("Vulkan loader yok"); return; }
  OrtakGpu &g = ortak_gpu();
  if (!g.ok) { skip("Vulkan cihazi yok"); return; }
  Device &dev = g.dev;
  SystemArena &sys = g.sys;
  if (test::gpu_is_virtual(dev.caps().device_name)) {
    skip("sanal GPU (Apple Paravirtual): piksel kapisi gercek cihazda olculur");
    return;
  }
  const uint32_t W = 160, H = 160;
  OffscreenConfig oc;
  oc.srgb = true;
  oc.width = W;
  oc.height = H;
  OffscreenResult ores;
  OffscreenTarget *off = offscreen_create(dev, sys, oc, &ores);
  if (!off) { CHECK(false); std::printf("    [bilgi] offscreen: %s\n", ores.error); return; }
  static uint8_t a_px[W * H * 4], b_px[W * H * 4];
  renderer::RendererConfig rc;
  rc.frames_in_flight = 1;
  rc.shadow_size = 512;
  rc.max_draws = 128;
  rc.post = true;
  rc.post_width = W;
  rc.post_height = H;
  rc.bloom_intensity = 0.7f;
  rc.bloom_threshold = 0.3f;
  bool atlandi = false;
  for (int yol = 0; yol < 2 && !atlandi; yol++) {
    renderer::RendererConfig cfg = rc;
    cfg.gpu_cull = (yol == 1);
    cfg.max_cull_batches = 32;
    renderer::Renderer ren;
    bool ok = ren.init(dev, sys, offscreen_render_pass(off), cfg);
    CHECK(ok);
    if (!ok) break;
    if (!ren.post().enabled) {
      std::printf("    [bilgi] sebep: %s\n", ren.post().disabled_reason);
      skip("son islem kurulamadi: cull + post kapisi kosmadi");
      atlandi = true;
      ren.shutdown();
      break;
    }
    if (yol == 1 && !ren.cull_recorded().enabled) {
      std::printf("    [bilgi] sebep: %s\n", ren.cull_recorded().disabled_reason);
      skip("GPU cull kurulamadi: cull + post kapisi kosmadi");
      atlandi = true;
      ren.shutdown();
      break;
    }
    const KupSahne sahne = kur_kup_sahnesi(ren);
    ren.set_render_size(W, H);
    kamera_onde(ren);
    Rec rr{&ren};
    ren.begin_frame(0);
    ciz_kupler(ren, sahne, kIcerideKup, kDisaridaKup);
    const bool r = offscreen_render_custom(off, oc, rec_main, &rr, &ores, rec_before);
    CHECK(r);
    if (r) std::memcpy(yol == 0 ? a_px : b_px, ores.pixels, sizeof a_px);
    if (yol == 1) {
      const renderer::CullInfo c = ren.cull();
      std::printf("    [bilgi] post ACIK + cull: aday %u, kume %u, hayatta kalan %u, elenen %u\n", c.candidates,
                  c.batches, c.survived, c.culled);
      CHECK(c.counts_valid && c.survived == 1 + kIcerideKup && c.culled == kDisaridaKup);
    }
    ren.shutdown();
  }
  if (!atlandi) {
    const uint32_t fark = bayt_farki(a_px, b_px, sizeof a_px);
    std::printf("    [bilgi] post ACIK — cull ACIK/KAPALI piksel farki: %u bayt (0 bekleniyor)\n", fark);
    CHECK(fark == 0);
  }
  offscreen_destroy(off);
}

// --- 8. cull yolunda Mali en iyi uygulama denetimi -------------------------
// Compute gecisi + dolayli cizim, Arm kurallarina gore temiz mi? Kendi cihazi
// (dogrulama sayaclari kirlenmesin).
ENGINE_TEST(render_graph_gpu_cull_mali_best_practices) {
  if (!loader_bp_ok()) { skip("Vulkan loader yok"); return; }
  static SystemArena sys;
  if (!sys.reserve(160u << 20, "graph_cull_bp")) { CHECK(false); return; }
  Device dev;
  DeviceConfig dc;
  dc.validation = true;
  dc.best_practices = true;
  if (!dev.init(sys, g_api_bp, dc)) { skip("Vulkan cihazi yok"); return; }
  if (!dev.caps().validation_layer) {
    skip("VK_LAYER_KHRONOS_validation yok — cull yolunun Mali denetimi kosmadi");
    dev.shutdown();
    return;
  }
  CHECK(dev.caps().best_practices);
  CHECK(dev.caps().debug_messenger); // mesaj kanali yoksa asagidaki 0'lar olcum degil
  const uint32_t W = 128, H = 128;
  OffscreenConfig oc;
  oc.srgb = true;
  oc.width = W;
  oc.height = H;
  OffscreenResult ores;
  OffscreenTarget *off = offscreen_create(dev, sys, oc, &ores);
  if (!off) { CHECK(false); std::printf("    [bilgi] offscreen: %s\n", ores.error); dev.shutdown(); return; }
  renderer::Renderer ren;
  renderer::RendererConfig rc;
  rc.frames_in_flight = 1;
  rc.shadow_size = 512;
  rc.max_draws = 128;
  rc.gpu_cull = true;
  rc.max_cull_batches = 32;
  bool ok = ren.init(dev, sys, offscreen_render_pass(off), rc);
  CHECK(ok);
  if (!ok) { offscreen_destroy(off); dev.shutdown(); return; }
  if (!ren.cull_recorded().enabled) {
    std::printf("    [bilgi] sebep: %s\n", ren.cull_recorded().disabled_reason);
    skip("GPU cull kurulamadi: Mali denetimi kosmadi");
    ren.shutdown();
    offscreen_destroy(off);
    dev.shutdown();
    return;
  }
  const KupSahne sahne = kur_kup_sahnesi(ren);
  ren.set_render_size(W, H);
  Rec rr{&ren};
  for (int frame = 0; frame < 3; frame++) {
    kamera_onde(ren);
    ren.begin_frame(0);
    ciz_kupler(ren, sahne, kIcerideKup, kDisaridaKup);
    ren.ui_begin((float)W, (float)H, 0.0f);
    ren.ui_rect(4, 4, 40, 12, renderer::Renderer::rgba(255, 255, 255, 200));
    bool r = offscreen_render_custom(off, oc, rec_main_ui, &rr, &ores, rec_before);
    CHECK(r);
    if (!r) { std::printf("    [bilgi] kare: %s\n", ores.error); break; }
  }
  const renderer::CullInfo ci = ren.cull();
  std::printf("    [bilgi] cull ACIK — aday %u, kume %u, hayatta kalan %u, elenen %u\n", ci.candidates, ci.batches,
              ci.survived, ci.culled);
  CHECK(ci.counts_valid && ci.survived == 1 + kIcerideKup && ci.culled == kDisaridaKup);
  const uint32_t bp_all = dev.best_practice_warnings(), bp_arm = dev.best_practice_arm_warnings();
  std::printf("    [bilgi] BestPractices: %u uyari (%u Arm), %u benzersiz kimlik, %u dogrulama hatasi\n", bp_all, bp_arm,
              dev.best_practice_id_count(), dev.validation_errors());
  for (uint32_t i = 0; i < dev.best_practice_id_count(); i++) {
    const Device::BpId &b = dev.best_practice_id(i);
    std::printf("    [bilgi]   %s x%u%s\n", b.name, b.count, b.arm ? "  <- Mali" : "");
  }
  CHECK(dev.validation_errors() == 0);
  const uint32_t sparse_layer = dev.best_practice_count("sparse-index-buffer");
  CHECK(ren.sparse_mesh_count() == 0);
  const uint32_t bp_arm_effective = ren.sparse_mesh_count() == 0 ? bp_arm - sparse_layer : bp_arm;
  CHECK(bp_arm_effective == 0);
  // POZITIF KONTROL: Arm kurali gercekten acik mi? Kural TANINMIYORSA bu kapi
  // hicbir sey olcemez — ustteki `bp_arm_effective == 0` de 0 uyari ile BOSA
  // gecerdi. O yuzden sonuc CHECK degil GORUNUR ATLAMA (bkz. test.hpp).
  if (test::arm_rules_missing(dev)) {
    ren.shutdown();
    offscreen_destroy(off);
    dev.shutdown();
    skip(test::kArmRulesMissingReason);
    return;
  }
  ren.shutdown();
  offscreen_destroy(off);
  dev.shutdown();
}

// ============================================================================
// FAZ 4 — 2B ARAYUZ (UI) KAPILARI
//
// Plan satiri: "UI: SDF font atlasi, TEK BATCH cizim, retained layout (yalniz
// dirty'de hesap), OPAK ONCE / BLEND SONRA, tam ekran seffaf katman yasak".
// Kapi: "UI overdraw olculmus; UI kalemi kare butcesinde < 1,5 ms".
//
// Hepsi ORTAK cihazi kullanir (Tuzaklar 8al: her yeni VkInstance sonraki
// kapilari sessizce ATLANDI'ya dusurur).
//
// Kapilar ve KONTROLLERI:
//   U1 siralama piksel esdegeri  — kontrol: BlendFirst (bilerek yanlis) farkli piksel
//   U2 batch gruplamasi          — kontrol: ikinci atlas -> batch artar; ortusen atlas -> sira korunur
//   U3 overdraw OLCUMU           — kontrol: ayrik dortgenlerde oran ~1.0
//   U4 kare butcesi < 1.5 ms     — kontrol: yuk 10x artinca olcum artar
//   U5 retained yerlesim         — kontrol: icerik degisince yeniden uretilir + piksel ayni
//   U6 tam ekran harmanli uyari  — kontrol: yarim ekran / opak tam ekran uyarmaz
// ============================================================================
namespace {

// UI atlasi: (0,0) 2x2 texeli BEYAZ OPAK (ui_rect bunu ornekler, font yukleyici
// de ayni garantiyi verir) + [64,128)^2 bolgesi duz beyaz (glif yerine gecer).
// Kalan her sey saydam. Olcu 512: ui_rect'in sabit 0.5/512 uv'si texel (0,0)'in
// TAM merkezine dusuyor, yani suzme belirsizligi yok.
constexpr uint32_t kAtlasN = 512;
uint8_t *ui_atlas_pixels(uint8_t r, uint8_t gq, uint8_t b) {
  static uint8_t px[kAtlasN * kAtlasN * 4];
  std::memset(px, 0, sizeof px);
  for (uint32_t y = 0; y < 2; y++)
    for (uint32_t x = 0; x < 2; x++) {
      uint8_t *p = px + (y * kAtlasN + x) * 4;
      p[0] = p[1] = p[2] = p[3] = 255;
    }
  for (uint32_t y = 64; y < 128; y++)
    for (uint32_t x = 64; x < 128; x++) {
      uint8_t *p = px + (y * kAtlasN + x) * 4;
      p[0] = r; p[1] = gq; p[2] = b; p[3] = 255;
    }
  return px;
}
// Glif bolgesinin ICINDEN guvenli uv (kenar suzmesi karismasin).
constexpr float kGU0 = 80.0f / (float)kAtlasN, kGU1 = 112.0f / (float)kAtlasN;

// SDF atlasi: ALFA kanali [64,128)^2 bolgesindeki bir DAIRENIN isaretli mesafe
// alani. alfa = 0.5 + (R - d) / (2*yayilim) -> kenar tam 0.5'te, rampa
// 2*yayilim texel genisliginde. Bitmap atlas olsaydi bu rampa buyutunce
// BULANIKLASIRDI; SDF ornekleme onu her olcekte ~1 piksele keser.
constexpr float kSdfSpread = 8.0f, kSdfR = 24.0f;
uint8_t *ui_sdf_atlas_pixels() {
  static uint8_t px[kAtlasN * kAtlasN * 4];
  std::memset(px, 0, sizeof px);
  for (uint32_t y = 0; y < kAtlasN; y++)
    for (uint32_t x = 0; x < kAtlasN; x++) {
      const float dx = (float)x + 0.5f - 96.0f, dy = (float)y + 0.5f - 96.0f;
      const float d = std::sqrt(dx * dx + dy * dy);
      float a = 0.5f + (kSdfR - d) / (2.0f * kSdfSpread);
      if (a < 0) a = 0;
      if (a > 1) a = 1;
      uint8_t *p = px + (y * kAtlasN + x) * 4;
      p[0] = p[1] = p[2] = 255;
      p[3] = (uint8_t)(a * 255.0f + 0.5f);
    }
  return px;
}
// SDF dairesini SARAN uv (rampanin tamami gorunur olsun).
constexpr float kSU0 = 64.0f / (float)kAtlasN, kSU1 = 128.0f / (float)kAtlasN;

void rec_ui_overdraw(VkCommandBuffer cb, void *u) {
  Rec *r = static_cast<Rec *>(u);
  r->r->record(cb);            // subpass 1'e gecis (cizim yok)
  r->r->ui_record_overdraw(cb); // "her fragment +1" boru hatti
}

// Kapilarin ortak HUD'u: iki opak panel + uzerlerinde birer "glif" dortgeni +
// altta yari saydam bir serit. Panel A ve B, kendilerinden ONCE gelen harmanli
// dortgenlerle ORTUSMEZ -> ikisi de guvenle one alinabilir.
void hud_ciz(renderer::Renderer &ren, renderer::MaterialHandle atlas, uint32_t serit_alfa) {
  ren.ui_set_atlas(atlas);
  ren.ui_rect(10, 10, 60, 30, renderer::Renderer::rgba(40, 40, 60, 255));   // opak panel A
  ren.ui_quad(14, 14, 40, 20, kGU0, kGU0, kGU1, kGU1, renderer::Renderer::rgba(255, 255, 255, 255)); // "metin"
  ren.ui_rect(120, 10, 60, 30, renderer::Renderer::rgba(40, 40, 60, 255));  // opak panel B
  ren.ui_quad(124, 14, 40, 20, kGU0, kGU0, kGU1, kGU1, renderer::Renderer::rgba(255, 255, 255, 255));
  ren.ui_rect(10, 80, 200, 40, renderer::Renderer::rgba(220, 60, 60, serit_alfa)); // yari saydam serit
}

} // namespace

// --- U1: siralama piksel esdegeri -------------------------------------------
ENGINE_TEST(renderer_ui_opaque_first_is_pixel_identical) {
  if (!loader_ok()) { skip("Vulkan loader yok"); return; }
  OrtakGpu &g = ortak_gpu();
  if (!g.ok) { skip("Vulkan cihazi yok"); return; }
  Device &dev = g.dev;
  SystemArena &sys = g.sys;
  const uint32_t W = 256, H = 192;
  OffscreenConfig oc;
  oc.srgb = true;
  oc.width = W; oc.height = H;
  OffscreenResult ores;
  OffscreenTarget *off = offscreen_create(dev, sys, oc, &ores);
  if (!off) { CHECK(false); std::printf("    [bilgi] offscreen: %s\n", ores.error); return; }
  renderer::Renderer ren;
  renderer::RendererConfig rc;
  rc.frames_in_flight = 1;
  rc.shadow_size = 0;
  rc.max_draws = 8;
  bool ok = ren.init(dev, sys, offscreen_render_pass(off), rc);
  CHECK(ok);
  if (!ok) { offscreen_destroy(off); return; }
  renderer::TextureHandle at = ren.create_texture(ui_atlas_pixels(255, 255, 255), kAtlasN, kAtlasN, false, false);
  renderer::MaterialHandle atlas = ren.create_material(at, {1, 1, 1});
  CHECK(atlas.valid());
  ren.set_camera(Mat4::identity(), Mat4::identity());
  ren.set_render_size(W, H);

  static uint8_t src_px[W * H * 4], opq_px[W * H * 4], bad_px[W * H * 4];
  Rec rr{&ren};
  struct Sonuc { uint32_t batch, hoist, opak, harman; };
  Sonuc sr{}, so{}, sb{};
  auto kare = [&](renderer::UiSortMode m, uint8_t *out, Sonuc *st) {
    ren.begin_frame(0);
    ren.ui_set_sort_mode(m);
    ren.ui_begin((float)W, (float)H, 0.0f);
    hud_ciz(ren, atlas, 130);
    ren.ui_end();
    const bool r = offscreen_render_custom(off, oc, rec_main_ui, &rr, &ores, rec_before);
    CHECK(r);
    if (!r) { std::printf("    [bilgi] kare: %s\n", ores.error); return; }
    std::memcpy(out, ores.pixels, (size_t)W * H * 4);
    const renderer::UiStats &s = ren.ui_fetch_stats();
    *st = {s.batches, s.hoisted, s.opaque, s.blended};
  };
  kare(renderer::UiSortMode::Source, src_px, &sr);
  kare(renderer::UiSortMode::OpaqueFirst, opq_px, &so);
  kare(renderer::UiSortMode::BlendFirst, bad_px, &sb);

  uint32_t en_buyuk = 0, farkli = 0;
  for (uint32_t i = 0; i < W * H * 4; i++) {
    const int d = (int)src_px[i] - (int)opq_px[i];
    const uint32_t ad = (uint32_t)(d < 0 ? -d : d);
    if (ad) { farkli++; if (ad > en_buyuk) en_buyuk = ad; }
  }
  uint32_t kontrol_farkli = 0;
  for (uint32_t i = 0; i < W * H * 4; i++) if (src_px[i] != bad_px[i]) kontrol_farkli++;
  std::printf("    [bilgi] siralama: kaynak %u batch -> opak-once %u batch (%u one alindi, %u opak / %u harmanli)\n",
              sr.batch, so.batch, so.hoist, so.opak, so.harman);
  std::printf("    [bilgi] piksel: opak-once vs kaynak %u bayt farkli, en buyuk kanal farki %u (tolerans 1)\n", farkli,
              en_buyuk);
  std::printf("    [bilgi] kontrol (blend-once, BILEREK yanlis): %u bayt farkli, %u batch\n", kontrol_farkli, sb.batch);
  CHECK(en_buyuk <= 1);                 // gorsel olarak ayni
  CHECK(so.hoist == 2);                 // iki panel guvenle one alindi
  CHECK(so.batch < sr.batch);           // batch sayisi DUSTU
  CHECK(so.opak == 2 && so.harman == 3);
  CHECK(kontrol_farkli > 500);          // yanlis sira gercekten baska piksel veriyor

  // GUVENLIK KURALI: opak bir dortgen KENDINDEN ONCE gelen harmanli bir
  // dortgenin USTUNDEYSE one alinamaz (alinsa metin panelin ustunde kalirdi).
  // Burada panel, "metnin" TAM USTUNE cizilir: beklenen sonuc metnin GIZLENMESI.
  static uint8_t g_src[W * H * 4], g_opq[W * H * 4];
  uint32_t engellenen = 0, tasinan = 0;
  auto ustuste_kare = [&](renderer::UiSortMode m, uint8_t *out) {
    ren.begin_frame(0);
    ren.ui_set_sort_mode(m);
    ren.ui_begin((float)W, (float)H, 0.0f);
    ren.ui_set_atlas(atlas);
    ren.ui_quad(20, 20, 80, 40, kGU0, kGU0, kGU1, kGU1, renderer::Renderer::rgba(255, 255, 255, 255)); // "metin"
    ren.ui_rect(10, 10, 120, 60, renderer::Renderer::rgba(30, 30, 30, 255)); // USTUNE opak panel
    ren.ui_end();
    const bool r = offscreen_render_custom(off, oc, rec_main_ui, &rr, &ores, rec_before);
    CHECK(r);
    if (r) std::memcpy(out, ores.pixels, (size_t)W * H * 4);
    const renderer::UiStats &st = ren.ui_stats();
    engellenen = st.blocked_hoists;
    tasinan = st.hoisted;
  };
  ustuste_kare(renderer::UiSortMode::Source, g_src);
  ustuste_kare(renderer::UiSortMode::OpaqueFirst, g_opq);
  uint32_t g_fark = 0;
  for (uint32_t i = 0; i < W * H * 4; i++) if (g_src[i] != g_opq[i]) g_fark++;
  std::printf("    [bilgi] guvenlik: panel metnin USTUNDE -> one alinan %u, engellenen %u, piksel farki %u (0 bekleniyor)\n",
              tasinan, engellenen, g_fark);
  CHECK(tasinan == 0);     // ortusen opak dortgen one ALINMADI
  CHECK(engellenen == 1);  // ve bu sessizce degil, sayilarak oldu
  CHECK(g_fark == 0);
  ren.shutdown();
  offscreen_destroy(off);
}

// --- U2: tek batch + atlas gruplamasi ---------------------------------------
ENGINE_TEST(renderer_ui_batches_group_by_atlas) {
  if (!loader_ok()) { skip("Vulkan loader yok"); return; }
  OrtakGpu &g = ortak_gpu();
  if (!g.ok) { skip("Vulkan cihazi yok"); return; }
  Device &dev = g.dev;
  SystemArena &sys = g.sys;
  const uint32_t W = 256, H = 192;
  OffscreenConfig oc;
  oc.srgb = true;
  oc.width = W; oc.height = H;
  OffscreenResult ores;
  OffscreenTarget *off = offscreen_create(dev, sys, oc, &ores);
  if (!off) { CHECK(false); return; }
  renderer::Renderer ren;
  renderer::RendererConfig rc;
  rc.frames_in_flight = 1;
  rc.shadow_size = 0;
  rc.max_draws = 8;
  bool ok = ren.init(dev, sys, offscreen_render_pass(off), rc);
  CHECK(ok);
  if (!ok) { offscreen_destroy(off); return; }
  renderer::MaterialHandle A = ren.create_material(ren.create_texture(ui_atlas_pixels(255, 255, 255), kAtlasN, kAtlasN, false, false));
  renderer::MaterialHandle B = ren.create_material(ren.create_texture(ui_atlas_pixels(255, 120, 60), kAtlasN, kAtlasN, false, false));
  CHECK(A.valid() && B.valid() && A.id != B.id);
  ren.set_camera(Mat4::identity(), Mat4::identity());
  ren.set_render_size(W, H);
  Rec rr{&ren};
  const uint32_t N = 64;
  auto olc = [&](int kip) {
    ren.begin_frame(0);
    ren.ui_begin((float)W, (float)H, 0.0f);
    for (uint32_t i = 0; i < N; i++) {
      const float x = (float)(i % 16) * 8.0f, y = (float)(i / 16) * 8.0f;
      if (kip == 0) {                     // tek atlas
        ren.ui_set_atlas(A);
        ren.ui_quad(x, y, 6, 6, kGU0, kGU0, kGU1, kGU1, renderer::Renderer::rgba(255, 255, 255, 255));
      } else if (kip == 1) {              // iki atlas, UZAYDA AYRIK (sol/sag yari)
        ren.ui_set_atlas(i & 1 ? B : A);
        const float xx = (i & 1) ? 140.0f + x * 0.5f : x * 0.5f;
        ren.ui_quad(xx, y, 6, 6, kGU0, kGU0, kGU1, kGU1, renderer::Renderer::rgba(255, 255, 255, 255));
      } else {                            // iki atlas, UST USTE (siralama guvensiz)
        ren.ui_set_atlas(i & 1 ? B : A);
        ren.ui_quad(x, y, 6, 6, kGU0, kGU0, kGU1, kGU1, renderer::Renderer::rgba(255, 255, 255, 255));
      }
    }
    ren.ui_end();
    const bool r = offscreen_render_custom(off, oc, rec_main_ui, &rr, &ores, rec_before);
    CHECK(r);
    const renderer::UiStats &s = ren.ui_fetch_stats();
    return s;
  };
  const renderer::UiStats s0 = olc(0), s1 = olc(1), s2 = olc(2);
  std::printf("    [bilgi] batch: tek atlas %u dortgen -> %u cizim (atlas %u)\n", s0.quads, s0.batches, s0.atlas_groups);
  std::printf("    [bilgi] kontrol: iki atlas AYRIK bolgede -> %u cizim (atlasa gore siralandi: %d)\n", s1.batches,
              (int)s1.atlas_sorted);
  std::printf("    [bilgi] kontrol: iki atlas UST USTE -> %u cizim (siralama YAPILMADI: %d)\n", s2.batches,
              (int)(!s2.atlas_sorted));
  CHECK(s0.batches == 1);          // ayni atlastan N dortgen TEK batch
  CHECK(s0.atlas_groups == 1);
  CHECK(s1.batches == 2);          // farkli atlas -> batch ARTTI (ama atlas basina 1)
  CHECK(s1.atlas_sorted);
  CHECK(s2.batches == N);          // ortusen atlaslar: dogruluk > batch, sira korunur
  CHECK(!s2.atlas_sorted);
  ren.shutdown();
  offscreen_destroy(off);
}

// --- U3: overdraw OLCUMU (tahmin degil) -------------------------------------
// Yontem: ayni dortgen akisi "her fragment hedefe +1/255 EKLER" boru hattiyla
// UNORM bir hedefe cizilir; geri okunan R bayti o pikseldeki FRAGMENT SAYISIDIR.
// Yani sayiyi rasterlayici uretir, CPU'daki alan toplami degil.
ENGINE_TEST(renderer_ui_overdraw_is_measured) {
  if (!loader_ok()) { skip("Vulkan loader yok"); return; }
  OrtakGpu &g = ortak_gpu();
  if (!g.ok) { skip("Vulkan cihazi yok"); return; }
  Device &dev = g.dev;
  SystemArena &sys = g.sys;
  const uint32_t W = 256, H = 256;
  OffscreenConfig oc;
  oc.srgb = false;                       // UNORM: 1/255 toplami TAMSAYI kalir
  oc.width = W; oc.height = H;
  oc.clear[0] = oc.clear[1] = oc.clear[2] = 0; oc.clear[3] = 255;
  OffscreenResult ores;
  OffscreenTarget *off = offscreen_create(dev, sys, oc, &ores);
  if (!off) { CHECK(false); return; }
  renderer::Renderer ren;
  renderer::RendererConfig rc;
  rc.frames_in_flight = 1;
  rc.shadow_size = 0;
  rc.max_draws = 8;
  rc.srgb_target = false;
  bool ok = ren.init(dev, sys, offscreen_render_pass(off), rc);
  CHECK(ok);
  if (!ok) { offscreen_destroy(off); return; }
  renderer::MaterialHandle A = ren.create_material(ren.create_texture(ui_atlas_pixels(255, 255, 255), kAtlasN, kAtlasN, false, false));
  ren.set_camera(Mat4::identity(), Mat4::identity());
  ren.set_render_size(W, H);
  Rec rr{&ren};
  const uint32_t K = 10;
  auto olc = [&](bool ustuste) {
    ren.begin_frame(0);
    ren.ui_begin((float)W, (float)H, 0.0f);
    ren.ui_set_atlas(A);
    for (uint32_t i = 0; i < K; i++) {
      if (ustuste) ren.ui_quad(28, 28, 200, 200, kGU0, kGU0, kGU1, kGU1, renderer::Renderer::rgba(255, 255, 255, 255));
      else ren.ui_quad((float)(i * 20), 28, 20, 200, kGU0, kGU0, kGU1, kGU1, renderer::Renderer::rgba(255, 255, 255, 255));
    }
    ren.ui_end();
    const bool r = offscreen_render_custom(off, oc, rec_ui_overdraw, &rr, &ores, rec_before);
    CHECK(r);
    if (!r) { std::printf("    [bilgi] kare: %s\n", ores.error); return renderer::UiOverdraw{}; }
    const renderer::UiOverdraw o = renderer::Renderer::ui_overdraw_measure(ores.pixels, W, H);
    ren.ui_set_overdraw(o);
    return o;
  };
  const renderer::UiOverdraw ust = olc(true), ayrik = olc(false);
  std::printf("    [bilgi] overdraw (%u ustuste dortgen): %u fragment / %u piksel = %.2f, ekranin %.2f kati, doyan %u\n",
              K, ust.shaded, ust.covered, (double)ust.ratio, (double)ust.screen, ust.saturated);
  std::printf("    [bilgi] kontrol (%u AYRIK dortgen): %u fragment / %u piksel = %.2f\n", K, ayrik.shaded, ayrik.covered,
              (double)ayrik.ratio);
  CHECK(ust.saturated == 0);                                   // sayim eksiksiz
  CHECK(ust.covered == 200 * 200);                             // kaplama alani
  CHECK(ust.shaded == K * 200 * 200);                          // rasterlayicinin sayimi
  CHECK(ust.ratio > 9.9f && ust.ratio < 10.1f);
  CHECK(ayrik.ratio > 0.99f && ayrik.ratio < 1.01f);           // KONTROL: ayrikta ~1.0
  CHECK(ren.ui_stats().overdraw.shaded == ayrik.shaded);       // UiStats'a girdi
  ren.shutdown();
  offscreen_destroy(off);
}

// --- U4: kare butcesi — UI kalemi < 1.5 ms ----------------------------------
// Olcum sahnesi: 1280x720, YUK = 2000 opak kutu + 5000 glif olcusunde harmanli
// dortgen (toplam 7000 dortgen / 42000 vertex). Gercek TTF gerekmez: gliflerin
// GPU maliyeti "kucuk, harmanli, dokulu dortgen" olmasindan gelir ve atlas
// burada proseduraldir (varlik bagimliligi = ATLANDI riski).
// GPU suresi UI cizimlerinin ETRAFINA konan zaman damgalarindan (VkQueryPool),
// CPU suresi ui_begin..ui_end (uretim) + ui_record (sirala/yaz/kaydet).
ENGINE_TEST(renderer_ui_frame_budget_under_1_5_ms) {
  if (!loader_ok()) { skip("Vulkan loader yok"); return; }
  OrtakGpu &g = ortak_gpu();
  if (!g.ok) { skip("Vulkan cihazi yok"); return; }
  Device &dev = g.dev;
  SystemArena &sys = g.sys;
  const uint32_t W = 1280, H = 720;
  OffscreenConfig oc;
  oc.srgb = true;
  oc.width = W; oc.height = H;
  OffscreenResult ores;
  OffscreenTarget *off = offscreen_create(dev, sys, oc, &ores);
  if (!off) { CHECK(false); std::printf("    [bilgi] offscreen: %s\n", ores.error); return; }
  renderer::Renderer ren;
  renderer::RendererConfig rc;
  rc.frames_in_flight = 1;
  rc.shadow_size = 0;
  rc.max_draws = 8;
  rc.ui_max_vertices = 8192 * 6; // 8192 dortgen kapasitesi
  bool ok = ren.init(dev, sys, offscreen_render_pass(off), rc);
  CHECK(ok);
  if (!ok) { offscreen_destroy(off); return; }
  renderer::MaterialHandle A = ren.create_material(ren.create_texture(ui_atlas_pixels(255, 255, 255), kAtlasN, kAtlasN, false, false));
  ren.set_camera(Mat4::identity(), Mat4::identity());
  ren.set_render_size(W, H);
  Rec rr{&ren};
  struct Olcum { float gpu = 1e9f, cpu = 1e9f; uint32_t quads = 0, batches = 0, drops = 0; bool timed = false; };
  auto kosu = [&](uint32_t kutu, uint32_t glif, uint32_t kare_sayisi) {
    Olcum m;
    for (uint32_t f = 0; f < kare_sayisi; f++) {
      ren.begin_frame(0);
      ren.ui_begin((float)W, (float)H, 0.0f);
      ren.ui_set_atlas(A);
      for (uint32_t i = 0; i < kutu; i++) { // opak HUD kutulari (ayrik yerlesim)
        const float x = (float)(i % 50) * 25.0f, y = (float)(i / 50) * 17.0f;
        ren.ui_rect(x, y, 22, 14, renderer::Renderer::rgba(30, 40, 70, 255));
      }
      for (uint32_t i = 0; i < glif; i++) { // glif olcusunde harmanli dortgenler
        const float x = (float)(i % 128) * 10.0f, y = 240.0f + (float)(i / 128) * 17.0f;
        ren.ui_quad(x, y, 9, 16, kGU0, kGU0, kGU1, kGU1, renderer::Renderer::rgba(235, 235, 245, 255));
      }
      ren.ui_end();
      const bool r = offscreen_render_custom(off, oc, rec_main_ui, &rr, &ores, rec_before);
      CHECK(r);
      if (!r) { std::printf("    [bilgi] kare: %s\n", ores.error); break; }
      const renderer::UiStats &s = ren.ui_fetch_stats();
      const float cpu = s.cpu_gen_ms + s.cpu_build_ms;
      if (f == 0) { m.quads = s.quads; m.batches = s.batches; m.drops = s.dropped; }
      m.timed = s.gpu_timing;
      if (s.gpu_timing && s.gpu_ms < m.gpu) m.gpu = s.gpu_ms;
      if (cpu < m.cpu) m.cpu = cpu;
    }
    if (!m.timed) m.gpu = 0;
    return m;
  };
  const Olcum hafif = kosu(200, 500, 12);   // 1/10 yuk (KONTROL)
  const Olcum agir = kosu(2000, 5000, 12);  // asil yuk
  std::printf("    [bilgi] UI yuku 1280x720, %u dortgen (%u batch, %u dusen): GPU %.3f ms + CPU %.3f ms = %.3f ms\n",
              agir.quads, agir.batches, agir.drops, (double)agir.gpu, (double)agir.cpu, (double)(agir.gpu + agir.cpu));
  std::printf("    [bilgi] kontrol (1/10 yuk, %u dortgen): GPU %.3f ms + CPU %.3f ms = %.3f ms\n", hafif.quads,
              (double)hafif.gpu, (double)hafif.cpu, (double)(hafif.gpu + hafif.cpu));
  std::printf("    [bilgi] cihaz: %s (zaman damgasi %d, periyot %.1f ns)\n", dev.caps().device_name, (int)agir.timed,
              (double)dev.caps().timestamp_period_ns);
  CHECK(agir.drops == 0);
  CHECK(agir.quads == 7000);
  CHECK(agir.batches == 2); // opak kosu + harmanli kosu: TEK batch/kalem
  CHECK(agir.timed);        // zaman damgasi okunmadan "sure" iddiasi bos olur
  // KONTROL: olcum yuku gercekten goruyor mu? 10x yuk daha uzun surmeli.
  CHECK(agir.gpu + agir.cpu > hafif.gpu + hafif.cpu);
  if (dev.caps().device_type == VK_PHYSICAL_DEVICE_TYPE_CPU) {
    skip("yazilim rasterlayici (lavapipe): 1.5 ms butcesi GERCEK GPU'da olculur, sayi yukarida");
  } else {
    CHECK(agir.gpu + agir.cpu < 1.5f); // PLAN §4 kare butcesindeki UI kalemi
  }
  ren.shutdown();
  offscreen_destroy(off);
}

// --- U5: retained yerlesim --------------------------------------------------
ENGINE_TEST(renderer_ui_retained_block_skips_layout) {
  if (!loader_ok()) { skip("Vulkan loader yok"); return; }
  OrtakGpu &g = ortak_gpu();
  if (!g.ok) { skip("Vulkan cihazi yok"); return; }
  Device &dev = g.dev;
  SystemArena &sys = g.sys;
  const uint32_t W = 256, H = 192;
  OffscreenConfig oc;
  oc.srgb = true;
  oc.width = W; oc.height = H;
  OffscreenResult ores;
  OffscreenTarget *off = offscreen_create(dev, sys, oc, &ores);
  if (!off) { CHECK(false); return; }
  renderer::Renderer ren;
  renderer::RendererConfig rc;
  rc.frames_in_flight = 1;
  rc.shadow_size = 0;
  rc.max_draws = 8;
  bool ok = ren.init(dev, sys, offscreen_render_pass(off), rc);
  CHECK(ok);
  if (!ok) { offscreen_destroy(off); return; }
  renderer::MaterialHandle A = ren.create_material(ren.create_texture(ui_atlas_pixels(255, 255, 255), kAtlasN, kAtlasN, false, false));
  ren.set_camera(Mat4::identity(), Mat4::identity());
  ren.set_render_size(W, H);
  Rec rr{&ren};
  static uint8_t ilk_px[W * H * 4], tekrar_px[W * H * 4];
  uint32_t hesap_cagrisi = 0;
  auto kare = [&](uint64_t ozet, uint8_t *out) {
    ren.begin_frame(0);
    ren.ui_begin((float)W, (float)H, 0.0f);
    if (ren.ui_block_begin(/*id=*/7, ozet)) { // true = KIRLI: yerlesimi hesapla
      hesap_cagrisi++;
      hud_ciz(ren, A, (uint32_t)(ozet & 0xFF));
    }
    ren.ui_block_end();
    ren.ui_end();
    const bool r = offscreen_render_custom(off, oc, rec_main_ui, &rr, &ores, rec_before);
    CHECK(r);
    if (r && out) std::memcpy(out, ores.pixels, (size_t)W * H * 4);
    return ren.ui_fetch_stats();
  };
  const renderer::UiStats k1 = kare(0x1130, ilk_px);     // ilk kare: hesap
  const renderer::UiStats k2 = kare(0x1130, tekrar_px);  // ayni icerik: hesap YOK
  const renderer::UiStats k3 = kare(0x1140, nullptr);    // KONTROL: icerik degisti
  std::printf("    [bilgi] retained: 1. kare uretilen %u / yeniden kullanilan %u (blok yeniden %u)\n", k1.generated,
              k1.reused, k1.blocks_rebuilt);
  std::printf("    [bilgi] retained: 2. kare uretilen %u / yeniden kullanilan %u (blok onbellekten %u)\n", k2.generated,
              k2.reused, k2.blocks_reused);
  std::printf("    [bilgi] kontrol: icerik degisince uretilen %u (blok yeniden %u); yerlesim hesabi %u kez kostu\n",
              k3.generated, k3.blocks_rebuilt, hesap_cagrisi);
  CHECK(k1.generated == 5 && k1.reused == 0);
  CHECK(k2.generated == 0 && k2.reused == 5); // 2. karede DORTGEN URETILMEDI
  CHECK(k2.quads == k1.quads);                // ama ayni sayida dortgen cizildi
  CHECK(k3.generated == 5);                   // KONTROL: degisince yeniden hesap
  CHECK(hesap_cagrisi == 2);                  // 3 karede 2 hesap
  uint32_t fark = 0;
  for (uint32_t i = 0; i < W * H * 4; i++) if (ilk_px[i] != tekrar_px[i]) fark++;
  std::printf("    [bilgi] onbellekten cizilen kare ile hesaplanan kare farki: %u bayt (0 bekleniyor)\n", fark);
  CHECK(fark == 0); // yeniden kullanilan dortgenler AYNI goruntuyu veriyor
  ren.shutdown();
  offscreen_destroy(off);
}

// --- U6: tam ekran harmanli katman uyarisi ----------------------------------
ENGINE_TEST(renderer_ui_warns_on_fullscreen_blended_layer) {
  if (!loader_ok()) { skip("Vulkan loader yok"); return; }
  OrtakGpu &g = ortak_gpu();
  if (!g.ok) { skip("Vulkan cihazi yok"); return; }
  Device &dev = g.dev;
  SystemArena &sys = g.sys;
  const uint32_t W = 128, H = 128;
  OffscreenConfig oc;
  oc.srgb = true;
  oc.width = W; oc.height = H;
  OffscreenResult ores;
  OffscreenTarget *off = offscreen_create(dev, sys, oc, &ores);
  if (!off) { CHECK(false); return; }
  renderer::Renderer ren;
  renderer::RendererConfig rc;
  rc.frames_in_flight = 1;
  rc.shadow_size = 0;
  rc.max_draws = 8;
  bool ok = ren.init(dev, sys, offscreen_render_pass(off), rc);
  CHECK(ok);
  if (!ok) { offscreen_destroy(off); return; }
  renderer::MaterialHandle A = ren.create_material(ren.create_texture(ui_atlas_pixels(255, 255, 255), kAtlasN, kAtlasN, false, false));
  ren.set_camera(Mat4::identity(), Mat4::identity());
  ren.set_render_size(W, H);
  Rec rr{&ren};
  auto kare = [&](int kip) {
    ren.begin_frame(0);
    ren.ui_begin((float)W, (float)H, 0.0f);
    ren.ui_set_atlas(A);
    if (kip == 0) ren.ui_rect(0, 0, (float)W, (float)H, renderer::Renderer::rgba(0, 0, 0, 120));      // YASAK
    else if (kip == 1) ren.ui_rect(0, 0, (float)W / 2, (float)H, renderer::Renderer::rgba(0, 0, 0, 120)); // yarim
    else ren.ui_rect(0, 0, (float)W, (float)H, renderer::Renderer::rgba(0, 0, 0, 255));               // opak: sorun yok
    ren.ui_end();
    const bool r = offscreen_render_custom(off, oc, rec_main_ui, &rr, &ores, rec_before);
    CHECK(r);
    return ren.ui_stats().fullscreen_blended;
  };
  const uint32_t tam = kare(0);
  const char *uyari = ren.ui_warning();
  const uint32_t yarim = kare(1);
  const uint32_t opak = kare(2);
  std::printf("    [bilgi] tam ekran HARMANLI katman: %u uyari — \"%s\"\n", tam, uyari);
  std::printf("    [bilgi] kontrol: yarim ekran harmanli %u uyari, tam ekran OPAK %u uyari\n", yarim, opak);
  CHECK(tam == 1);
  CHECK(uyari && uyari[0]);
  CHECK(yarim == 0);
  CHECK(opak == 0); // opak tam ekran katman TBDR'da sorun degil (tile okunmaz)
  CHECK(ren.ui_warning()[0] == '\0'); // son karede uyari yok
  ren.shutdown();
  offscreen_destroy(off);
}

// --- U7: SDF metin ORNEKLEME yolu (istege bagli dilim) ----------------------
// Atlas URETIMI baska bir isin konusu; burada olculen sey ORNEKLEME yolu.
// Ayni SDF atlasi 4 kat buyutulerek iki kez cizilir:
//   SDF ACIK  -> kenar fwidth ile kesilir, gecis ~1-2 piksel
//   SDF KAPALI (KONTROL) -> alfa bilinear okunur, gecis onlarca piksel (bulanik)
// "Keskin" iddiasi, orta tonlu (ne zemin ne dolu) piksel SAYISIYLA olculur.
ENGINE_TEST(renderer_ui_sdf_sampling_keeps_edge_sharp) {
  if (!loader_ok()) { skip("Vulkan loader yok"); return; }
  OrtakGpu &g = ortak_gpu();
  if (!g.ok) { skip("Vulkan cihazi yok"); return; }
  Device &dev = g.dev;
  SystemArena &sys = g.sys;
  const uint32_t W = 256, H = 256;
  OffscreenConfig oc;
  oc.srgb = true;
  oc.width = W; oc.height = H;
  oc.clear[0] = oc.clear[1] = oc.clear[2] = 0; oc.clear[3] = 255;
  OffscreenResult ores;
  OffscreenTarget *off = offscreen_create(dev, sys, oc, &ores);
  if (!off) { CHECK(false); return; }
  renderer::Renderer ren;
  renderer::RendererConfig rc;
  rc.frames_in_flight = 1;
  rc.shadow_size = 0;
  rc.max_draws = 8;
  bool ok = ren.init(dev, sys, offscreen_render_pass(off), rc);
  CHECK(ok);
  if (!ok) { offscreen_destroy(off); return; }
  renderer::MaterialHandle A = ren.create_material(ren.create_texture(ui_sdf_atlas_pixels(), kAtlasN, kAtlasN, false, false));
  CHECK(A.valid());
  ren.set_camera(Mat4::identity(), Mat4::identity());
  ren.set_render_size(W, H);
  Rec rr{&ren};
  // Orta satirda "ne zemin (<=8) ne dolu (>=247)" piksel: gecis genisligi.
  auto gecis_pikselleri = [&](const uint8_t *px) {
    uint32_t n = 0;
    const uint8_t *row = px + (size_t)(H / 2) * W * 4;
    for (uint32_t x = 0; x < W; x++) if (row[x * 4] > 8 && row[x * 4] < 247) n++;
    return n;
  };
  auto kare = [&](bool sdf) {
    ren.begin_frame(0);
    ren.ui_begin((float)W, (float)H, 0.0f);
    ren.ui_set_atlas(A);
    ren.ui_set_sdf(sdf);
    ren.ui_quad(0, 0, (float)W, (float)H, kSU0, kSU0, kSU1, kSU1, renderer::Renderer::rgba(255, 255, 255, 255));
    ren.ui_end();
    const bool r = offscreen_render_custom(off, oc, rec_main_ui, &rr, &ores, rec_before);
    CHECK(r);
    if (!r) { std::printf("    [bilgi] kare: %s\n", ores.error); return (uint32_t)0; }
    const uint32_t n = gecis_pikselleri(ores.pixels);
    return n;
  };
  const uint32_t keskin = kare(true);
  const renderer::UiStats sdf_s = ren.ui_stats();
  const uint32_t bulanik = kare(false);
  const renderer::UiStats duz_s = ren.ui_stats();
  std::printf("    [bilgi] SDF ACIK: orta satirda %u gecis pikseli (%u SDF dortgeni, sebep \"%s\")\n", keskin,
              sdf_s.sdf_quads, sdf_s.sdf_reason);
  std::printf("    [bilgi] KONTROL SDF KAPALI (ayni atlas, bilinear alfa): %u gecis pikseli (%u SDF dortgeni)\n",
              bulanik, duz_s.sdf_quads);
  CHECK(sdf_s.sdf_quads == 1);
  CHECK(sdf_s.sdf_reason[0] == '\0'); // boru hatti gercekten kuruldu
  CHECK(duz_s.sdf_quads == 0);
  CHECK(keskin > 0);                  // hic kenar yoksa olcum bos olur
  CHECK(keskin <= 16);                // ~1-2 piksellik gecis (iki kenar)
  CHECK(bulanik > 4 * keskin);        // KONTROL: SDF'siz yol belirgin bulanik
  ren.shutdown();
  offscreen_destroy(off);
}
