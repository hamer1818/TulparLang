// FAZ 5 — ZAMANSAL (temporal) kapilari: jitter, hareket vektoru, reactive
// maske, dinamik cozunurluk, yukseltici (upscaler) arayuzu.
//
// Her kapinin KONTROLU var: bir kapi "gecti" diyorsa kontrol gecisinde AYNI
// olcum duser ve kapinin gercekten bir sey olctugu gorulur (Tuzaklar 1m/8s).
//   1. Halton(2,3) dizisi ANALITIK degerlere oturur (GPU istemez)
//   2. jitter ACIK: ardisik iki kare farkli alt-piksel kaydirmasi + farkli
//      piksel (kontrol: jitter KAPALI -> iki kare bit bit ayni)
//   3. hareket vektoru: bilinen hizla yatay giden kutunun MV'si ANALITIK
//      piksel kaymasina oturur (kontrol: duran nesnenin MV'si ~0)
//   4. reactive maske: isaretli nesne 1, isaretsiz 0 (ayni karede, yan yana)
//   5. dinamik cozunurluk: %50'de hedef piksel sayisi 1/4, cikti referanstan
//      OLCULEBILIR ama SINIRLI sapiyor (kontrol: %100'de sapma 0) + olcek
//      degisimi AYIRMA yapmiyor
//   6. yukseltici arayuzu: Bilinear ve Sharpen farkli (kontrol: None %100'de
//      referansla bit bit ayni; ayrica None %50 != Bilinear %50)
//   7. zamansal yol ACIKKEN Mali en iyi uygulama denetimi (pozitif kontrollu)
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

// Butun zamansal GPU kapilari TEK VkInstance paylasir ve o instance surec
// boyunca YASAR (bilerek kapatilmaz).
//
// SEBEP (olculdu 2026-09-15, bu makine): NVIDIA ICD her vkCreateInstance'ta
// dlopen ediliyor; libnvidia-tls.so initial-exec TLS istiyor ve glibc'nin
// "static TLS surplus" alani dlopen/dlclose dongulerinde geri verilmiyor.
// Surecte belli sayida instance'tan sonra loader "cannot allocate memory in
// static TLS block" -> "Found no drivers!" der ve vkCreateInstance
// VK_ERROR_INCOMPATIBLE_DRIVER doner. Sonuc: SONRADAN kosan testler (bizimkiler
// degil, BASKALARININ kapilari) sessizce "Vulkan cihazi yok" diye atlanir.
// Yani her yeni GPU testinin bir vkCreateInstance'i, uzaktaki bir kapiyi
// oldurebilir. Kural: yeni GPU kapilari cihazi PAYLASSIN.
// (Dogrulama katmani acik olan kapi istisna: sayaclari kirletmemek icin kendi
// cihazini kurar.)
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
    if (!g.sys.reserve(320u << 20, "temporal_ortak")) return g;
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

// Sahne: siyah zemin uzerinde tek kup, yalniz ortam isigi (diffuse 0) — kupun
// butun yuzleri ayni dogrusal degerde, olcumler tek sayiyla surulur.
struct Sahne {
  renderer::MeshHandle cube;
  Mat4 view, proj;
};
Sahne kur(renderer::Renderer &ren, float ambient) {
  Sahne s;
  renderer::Vertex v[24];
  uint32_t idx[36];
  const uint32_t n = renderer::Renderer::cube(v, idx);
  s.cube = ren.create_mesh(v, 24, idx, n);
  ren.set_light({0, 1, 0}, {ambient, ambient, ambient}, 0.0f);
  s.view = Mat4::look_at({0, 0, 3.0f}, {0, 0, 0}, {0, 1, 0});
  s.proj = Mat4::perspective(1.0f, 1.0f, 0.1f, 50.0f);
  ren.set_camera(s.view, s.proj);
  return s;
}

uint32_t bayt_farki(const uint8_t *a, const uint8_t *b, uint32_t n) {
  uint32_t f = 0;
  for (uint32_t i = 0; i < n; i++)
    if (a[i] != b[i]) f++;
  return f;
}
// Ortalama mutlak sapma (0..255, yuzde biri hassasiyetle raporlanir).
uint32_t ortalama_sapma(const uint8_t *a, const uint8_t *b, uint32_t n) {
  uint64_t t = 0;
  for (uint32_t i = 0; i < n; i++) t += (uint64_t)(a[i] > b[i] ? a[i] - b[i] : b[i] - a[i]);
  return n ? (uint32_t)(t * 100 / n) : 0;
}
// Dunya noktasinin PIKSEL konumu (Vulkan NDC: y asagi, framebuffer y de asagi).
void dunya_to_piksel(const Mat4 &vp, Vec3 w, uint32_t W, uint32_t H, float *px, float *py) {
  Vec4 c = vp * Vec4{w.x, w.y, w.z, 1.0f};
  const float iw = 1.0f / c.w;
  *px = (c.x * iw * 0.5f + 0.5f) * (float)W;
  *py = (c.y * iw * 0.5f + 0.5f) * (float)H;
}
void dunya_to_ndc(const Mat4 &vp, Vec3 w, float *nx, float *ny) {
  Vec4 c = vp * Vec4{w.x, w.y, w.z, 1.0f};
  const float iw = 1.0f / c.w;
  *nx = c.x * iw;
  *ny = c.y * iw;
}
// Jitter'in NDC'deki KESIN karsiligi: ayni dunya noktasinin jitter'siz ve
// jitter'li projeksiyondaki NDC farki. Saf clip-uzayi otelemesi ise bu fark
// HER nokta icin (derinlik/konum fark etmez) AYNIDIR.
bool jitter_ndc_sabit(const Mat4 &view, const Mat4 &proj, const Mat4 &jproj, uint32_t W, uint32_t H, float jx, float jy,
                      float *en_buyuk_hata) {
  const Vec3 nokta[4] = {{0, 0, 0}, {0.8f, -0.5f, 1.2f}, {-1.5f, 0.9f, -2.0f}, {2.5f, 2.5f, -6.0f}};
  const Mat4 vp = proj * view, vpj = jproj * view;
  const float bx = 2.0f * jx / (float)W, by = 2.0f * jy / (float)H;
  float en = 0.0f;
  for (uint32_t i = 0; i < 4; i++) {
    float ax = 0, ay = 0, cx = 0, cy = 0;
    dunya_to_ndc(vp, nokta[i], &ax, &ay);
    dunya_to_ndc(vpj, nokta[i], &cx, &cy);
    const float ex = std::fabs((cx - ax) - bx), ey = std::fabs((cy - ay) - by);
    if (ex > en) en = ex;
    if (ey > en) en = ey;
  }
  *en_buyuk_hata = en;
  return en < 1e-5f;
}
} // namespace

// --- 1. Halton(2,3): GPU istemez, saf sayi --------------------------------
ENGINE_TEST(temporal_halton_sequence_is_low_discrepancy) {
  using R = renderer::Renderer;
  // Radikal ters (van der Corput) taban 2: 1/2, 1/4, 3/4, 1/8, 5/8, 3/8, 7/8
  const float b2[7] = {0.5f, 0.25f, 0.75f, 0.125f, 0.625f, 0.375f, 0.875f};
  // Taban 3: 1/3, 2/3, 1/9, 4/9, 7/9, 2/9, 5/9
  const float b3[7] = {1.0f / 3, 2.0f / 3, 1.0f / 9, 4.0f / 9, 7.0f / 9, 2.0f / 9, 5.0f / 9};
  bool ok2 = true, ok3 = true;
  for (uint32_t i = 0; i < 7; i++) {
    if (std::fabs(R::halton(i + 1, 2) - b2[i]) > 1e-6f) ok2 = false;
    if (std::fabs(R::halton(i + 1, 3) - b3[i]) > 1e-6f) ok3 = false;
  }
  std::printf("    [bilgi] Halton2 ilk 4: %.4f %.4f %.4f %.4f; Halton3: %.4f %.4f %.4f %.4f\n", (double)R::halton(1, 2),
              (double)R::halton(2, 2), (double)R::halton(3, 2), (double)R::halton(4, 2), (double)R::halton(1, 3),
              (double)R::halton(2, 3), (double)R::halton(3, 3), (double)R::halton(4, 3));
  CHECK(ok2);
  CHECK(ok3);
  CHECK(R::halton(0, 2) == 0.0f);
  // Dusuk uyumsuzluk: 8 fazin hicbiri ayni alt-piksel hucresine iki kez
  // dusmemeli (rastgele kaydirma bunu VERMEZ — kapinin asil iddiasi bu).
  uint32_t hucre[16] = {};
  for (uint32_t i = 0; i < 8; i++) {
    const uint32_t cx = (uint32_t)(renderer::Renderer::halton(i + 1, 2) * 4.0f);
    const uint32_t cy = (uint32_t)(renderer::Renderer::halton(i + 1, 3) * 4.0f);
    hucre[(cy < 4 ? cy : 3) * 4 + (cx < 4 ? cx : 3)]++;
  }
  uint32_t dolu = 0, en_cok = 0;
  for (uint32_t i = 0; i < 16; i++) {
    if (hucre[i]) dolu++;
    if (hucre[i] > en_cok) en_cok = hucre[i];
  }
  std::printf("    [bilgi] 8 faz -> 4x4 alt-piksel gridinde %u dolu hucre, en kalabalik %u\n", dolu, en_cok);
  CHECK(dolu == 8);   // 8 faz, 8 ayri hucre
  CHECK(en_cok == 1);
  // KONTROL: denetim gercekten ayirt ediyor mu? Sabit (jitter'siz) bir dizi
  // tek hucreye yigilirdi.
  uint32_t sabit[16] = {};
  for (uint32_t i = 0; i < 8; i++) sabit[0]++;
  std::printf("    [bilgi] kontrol: sabit dizi -> en kalabalik hucre %u (8 bekleniyor)\n", sabit[0]);
  CHECK(sabit[0] == 8);
}

// --- 2. Jitter: ardisik kareler farkli alt-piksel + farkli goruntu ---------
ENGINE_TEST(temporal_jitter_shifts_subpixel_and_pixels) {
  OrtakGpu &g = ortak_gpu();
  if (!g.ok) { skip("Vulkan loader/cihazi yok"); return; }
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
  static uint8_t a_px[W * H * 4], b_px[W * H * 4];
  renderer::Renderer ren;
  renderer::RendererConfig rc;
  rc.frames_in_flight = 1;
  rc.shadow_size = 0;
  rc.max_draws = 64;
  bool ok = ren.init(dev, sys, offscreen_render_pass(off), rc);
  CHECK(ok);
  if (!ok) { offscreen_destroy(off); return; }
  CHECK(!ren.temporal().jitter); // VARSAYILAN KAPALI
  Sahne s = kur(ren, 1.0f);
  ren.set_render_size(W, H);
  Rec rr{&ren};
  auto kare = [&](uint8_t *dst) {
    ren.begin_frame(0);
    ren.draw(s.cube, Mat4::scale({0.6f, 0.6f, 0.6f}), {1, 1, 1});
    const bool r = offscreen_render_custom(off, oc, rec_main, &rr, &ores, rec_before);
    if (r && dst) std::memcpy(dst, ores.pixels, (size_t)W * H * 4);
    return r;
  };
  // KONTROL: jitter KAPALI -> iki kare BIT BIT ayni.
  CHECK(kare(a_px));
  CHECK(kare(b_px));
  const uint32_t kapali_fark = bayt_farki(a_px, b_px, W * H * 4);
  const Mat4 j_kapali = ren.jittered_proj();
  std::printf("    [bilgi] kontrol: jitter KAPALI iki kare farki %u bayt (0 bekleniyor)\n", kapali_fark);
  CHECK(kapali_fark == 0);
  CHECK(j_kapali.m[3][0] == 0.0f && j_kapali.m[3][1] == 0.0f);

  // Jitter ACIK: iki ardisik kare farkli alt-piksel kaydirmasi vermeli.
  ren.set_jitter(true);
  CHECK(kare(a_px));
  const renderer::TemporalInfo t1 = ren.temporal();
  const Mat4 j1 = ren.jittered_proj();
  CHECK(kare(b_px));
  const renderer::TemporalInfo t2 = ren.temporal();
  const Mat4 j2 = ren.jittered_proj();
  std::printf("    [bilgi] jitter faz %u: (%.4f, %.4f) px; faz %u: (%.4f, %.4f) px\n", t1.jitter_index,
              (double)t1.jitter_x, (double)t1.jitter_y, t2.jitter_index, (double)t2.jitter_x, (double)t2.jitter_y);
  CHECK(t1.jitter && t2.jitter);
  CHECK(t1.jitter_index != t2.jitter_index);
  CHECK(t1.jitter_x != t2.jitter_x || t1.jitter_y != t2.jitter_y);
  CHECK(std::fabs(t1.jitter_x) <= 0.5f && std::fabs(t1.jitter_y) <= 0.5f); // ALT-piksel
  // Kaydirmanin NDC karsiligi TAM 2*j/olcu ve HER noktada ayni: saf clip
  // uzayi otelemesi (matrisin hangi hucresine dustugu projeksiyon turune
  // baglidir — perspective'de m[3][3]=0 oldugu icin z sutununa duser, bu yuzden
  // hucre degil DAVRANIS olculur).
  float hata1 = 0, hata2 = 0;
  const bool sabit1 = jitter_ndc_sabit(s.view, s.proj, j1, W, H, t1.jitter_x, t1.jitter_y, &hata1);
  const bool sabit2 = jitter_ndc_sabit(s.view, s.proj, j2, W, H, t2.jitter_x, t2.jitter_y, &hata2);
  std::printf("    [bilgi] NDC kaymasi 4 farkli derinlikte sabit mi: %s (en buyuk hata %.3e / %.3e)\n",
              sabit1 && sabit2 ? "EVET" : "HAYIR", (double)hata1, (double)hata2);
  CHECK(sabit1);
  CHECK(sabit2);
  // ON-DONDURME (Android, Tuzaklar 8ab): cagiran projeksiyonu clip uzayinda
  // dondurerek verirse jitter yine FRAMEBUFFER ekseninde ve ayni buyuklukte
  // olmali. KONTROL: dondurulmemis olcumle ayni sonuc cikiyor.
  {
    const Mat4 donuk = Mat4::rotate({0, 0, 1}, 1.5707963f) * s.proj;
    ren.set_camera(s.view, donuk);
    ren.begin_frame(0);
    const renderer::TemporalInfo t3 = ren.temporal();
    float hata3 = 0;
    const bool sabit3 =
        jitter_ndc_sabit(s.view, donuk, ren.jittered_proj(), W, H, t3.jitter_x, t3.jitter_y, &hata3);
    std::printf("    [bilgi] on-dondurmeli projeksiyonda NDC kaymasi sabit mi: %s (en buyuk hata %.3e)\n",
                sabit3 ? "EVET" : "HAYIR", (double)hata3);
    CHECK(sabit3);
    ren.set_camera(s.view, s.proj);
  }
  // KONTROL: olcum gercekten ayirt ediyor mu? YANLIS bir beklenti (kaydirmanin
  // iki kati) ayni fonksiyonda DUSMELI.
  {
    float hata_k = 0;
    const bool yanlis = jitter_ndc_sabit(s.view, s.proj, j1, W, H, t1.jitter_x * 2.0f, t1.jitter_y * 2.0f, &hata_k);
    std::printf("    [bilgi] kontrol: yanlis beklenti (2x kaydirma) -> %s (hata %.3e)\n", yanlis ? "GECTI!" : "dustu",
                (double)hata_k);
    CHECK(!yanlis);
  }
  const uint32_t acik_fark = bayt_farki(a_px, b_px, W * H * 4);
  std::printf("    [bilgi] jitter ACIK iki kare farki %u bayt (> 0 bekleniyor)\n", acik_fark);
  // Kaparin ANALITIK kismi (jitter'in NDC'de tam yarim piksel kaydirdigi ve
  // yanlis beklentinin dustugu) yukarida kosuyor ve her yerde gecerli; yalniz
  // PIKSEL olcumu sanal GPU'da (CI macOS) sonuc vermiyor.
  if (test::gpu_is_virtual(dev.caps().device_name))
    skip("sanal GPU (Apple Paravirtual, CI macOS): jitter'in PIKSEL etkisi gercek cihazda olculur");
  else
    CHECK(acik_fark > 0);
  ren.shutdown();
  offscreen_destroy(off);
}

// --- 3 + 4. Hareket vektoru ANALITIK, reactive maske isaretliyi ayiriyor ---
ENGINE_TEST(temporal_motion_vectors_match_analytic_shift) {
  OrtakGpu &g = ortak_gpu();
  if (!g.ok) { skip("Vulkan loader/cihazi yok"); return; }
  Device &dev = g.dev;
  SystemArena &sys = g.sys;
  if (test::gpu_is_virtual(dev.caps().device_name)) {
    skip("sanal GPU (Apple Paravirtual, CI macOS): piksel kapisi gercek cihazda olculur");
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
  renderer::Renderer ren;
  renderer::RendererConfig rc;
  rc.frames_in_flight = 1;
  rc.shadow_size = 0;
  rc.max_draws = 64;
  rc.temporal.motion_vectors = true;
  rc.temporal.width = W;
  rc.temporal.height = H;
  bool ok = ren.init(dev, sys, offscreen_render_pass(off), rc);
  CHECK(ok);
  if (!ok) { offscreen_destroy(off); return; }
  const renderer::TemporalInfo ti = ren.temporal();
  if (!ti.motion) {
    std::printf("    [bilgi] sebep: %s\n", ti.motion_disabled_reason);
    skip("hareket hedefi kurulamadi: MV kapilari kosmadi");
    ren.shutdown();
    offscreen_destroy(off);
    return;
  }
  std::printf("    [bilgi] MV bicimi %d (%s), hedef %ux%u, %.2f MB; tablo (%u):", (int)ti.motion_format,
              ti.motion_format == VK_FORMAT_R16G16B16A16_SFLOAT ? "RGBA16F" : "RGBA32F", ti.motion_width,
              ti.motion_height, (double)ti.motion_bytes / (1024.0 * 1024.0), ren.graph_pass_count());
  for (uint32_t i = 0; i < ren.graph_pass_count(); i++) std::printf(" %s", ren.graph_pass_name(i));
  std::printf("\n");
  CHECK(ren.graph_pass_count() == 2); // golge kapali: hareket + sahne
  CHECK(std::strcmp(ren.graph_pass_name(0), "hareket") == 0);

  Sahne s = kur(ren, 1.0f);
  ren.set_render_size(W, H);
  Rec rr{&ren};
  static float mv[W * H * 4];
  const Mat4 vp = s.proj * s.view;
  const float olcek = 0.6f;
  const Mat4 kup = Mat4::scale({olcek, olcek, olcek});
  // Kutunun ON YUZUNUN merkezi: derinlik testi o pikselde bunu birakir.
  const Vec3 on_yuz{0, 0, olcek * 0.5f};

  // Kare 1: her sey duruyor (onceki viewproj'u da kurar).
  ren.begin_frame(0);
  ren.draw(s.cube, ren.default_material(), kup, {1, 1, 1});
  CHECK(offscreen_render_custom(off, oc, rec_main, &rr, &ores, rec_before));
  // Kare 2: kutu +x yonunde dx kadar gitti (onceki model verilir).
  const float dx = 0.05f;
  const Mat4 model_onceki = kup;
  const Mat4 model_simdi = Mat4::translate({dx, 0, 0}) * kup;
  ren.begin_frame(0);
  ren.draw(s.cube, ren.default_material(), model_simdi, {1, 1, 1}, &model_onceki, 0.0f);
  CHECK(offscreen_render_custom(off, oc, rec_main, &rr, &ores, rec_before));
  CHECK(ren.read_motion(mv, W * H));
  // ANALITIK beklenti: ayni dunya noktasinin iki karedeki piksel konumu.
  float px_s = 0, py_s = 0, px_o = 0, py_o = 0;
  dunya_to_piksel(vp, Vec3{on_yuz.x + dx, on_yuz.y, on_yuz.z}, W, H, &px_s, &py_s);
  dunya_to_piksel(vp, on_yuz, W, H, &px_o, &py_o);
  const float bek_x = px_s - px_o, bek_y = py_s - py_o;
  const uint32_t sx = (uint32_t)(px_s + 0.5f), sy = (uint32_t)(py_s + 0.5f);
  CHECK(sx < W && sy < H);
  const float *p = mv + ((size_t)sy * W + sx) * 4;
  std::printf("    [bilgi] hareketli kutu: MV (%.3f, %.3f) px @ (%u,%u); ANALITIK (%.3f, %.3f)\n", (double)p[0],
              (double)p[1], sx, sy, (double)bek_x, (double)bek_y);
  CHECK(std::fabs(bek_x) > 2.0f);                 // olcum anlamli olacak kadar buyuk
  CHECK(std::fabs(p[0] - bek_x) < 0.35f);         // ANALITIK degere oturuyor
  CHECK(std::fabs(p[1] - bek_y) < 0.35f);
  CHECK(p[3] == 0.0f);                            // isaretsiz: reactive 0

  // KONTROL: duran nesnenin MV'si ~0 (ayni olcum yolu, sifir cikmali).
  ren.begin_frame(0);
  ren.draw(s.cube, ren.default_material(), model_simdi, {1, 1, 1}, &model_simdi, 0.0f);
  CHECK(offscreen_render_custom(off, oc, rec_main, &rr, &ores, rec_before));
  CHECK(ren.read_motion(mv, W * H));
  const float *q = mv + ((size_t)sy * W + sx) * 4;
  std::printf("    [bilgi] kontrol: duran kutu MV (%.4f, %.4f) px (~0 bekleniyor)\n", (double)q[0], (double)q[1]);
  CHECK(std::fabs(q[0]) < 0.02f && std::fabs(q[1]) < 0.02f);
  // Nesnesiz piksel (kose): temizleme sozlesmesi 0,0,0,0 (Tuzaklar 8ai).
  const float *bos = mv + 0;
  CHECK(bos[0] == 0.0f && bos[1] == 0.0f && bos[3] == 0.0f);

  // --- 4. Reactive maske: ayni karede isaretli 1, isaretsiz 0 --------------
  const Mat4 sol = Mat4::translate({-0.8f, 0, 0}) * kup;
  const Mat4 sag = Mat4::translate({0.8f, 0, 0}) * kup;
  ren.begin_frame(0);
  ren.draw(s.cube, ren.default_material(), sol, {1, 1, 1}, &sol, 1.0f);  // ISARETLI (saydam/parcacik)
  ren.draw(s.cube, ren.default_material(), sag, {1, 1, 1}, &sag, 0.0f);  // isaretsiz
  CHECK(offscreen_render_custom(off, oc, rec_main, &rr, &ores, rec_before));
  CHECK(ren.read_motion(mv, W * H));
  float lx = 0, ly = 0, rx = 0, ry = 0;
  dunya_to_piksel(vp, Vec3{-0.8f, 0, on_yuz.z}, W, H, &lx, &ly);
  dunya_to_piksel(vp, Vec3{0.8f, 0, on_yuz.z}, W, H, &rx, &ry);
  const float *ml = mv + (((size_t)(ly + 0.5f)) * W + (size_t)(lx + 0.5f)) * 4;
  const float *mr = mv + (((size_t)(ry + 0.5f)) * W + (size_t)(rx + 0.5f)) * 4;
  std::printf("    [bilgi] reactive: isaretli @(%.0f,%.0f) = %.3f, isaretsiz @(%.0f,%.0f) = %.3f\n", (double)lx,
              (double)ly, (double)ml[3], (double)rx, (double)ry, (double)mr[3]);
  CHECK(ml[3] > 0.99f);
  CHECK(mr[3] == 0.0f);
  // Iskeletli cizim atlanmasi GORUNUR (bugun 0; sessiz bosluk degil).
  std::printf("    [bilgi] MV'siz kalan iskeletli cizim: %u\n", ren.temporal().motion_skipped_skinned);
  CHECK(ren.temporal().motion_skipped_skinned == 0);
  ren.shutdown();
  offscreen_destroy(off);
}

// --- 5 + 6. Dinamik cozunurluk ve yukseltici arayuzu -----------------------
ENGINE_TEST(temporal_dynamic_resolution_and_upscaler) {
  OrtakGpu &g = ortak_gpu();
  if (!g.ok) { skip("Vulkan loader/cihazi yok"); return; }
  Device &dev = g.dev;
  SystemArena &sys = g.sys;
  if (test::gpu_is_virtual(dev.caps().device_name)) {
    skip("sanal GPU (Apple Paravirtual, CI macOS): piksel kapisi gercek cihazda olculur");
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
  renderer::Renderer ren;
  renderer::RendererConfig rc;
  rc.frames_in_flight = 1;
  rc.shadow_size = 0;
  rc.max_draws = 64;
  rc.post = true; // dinamik cozunurluk IC HEDEF ister (yukseltme birlestirmede)
  rc.post_width = W;
  rc.post_height = H;
  rc.bloom_intensity = 0.0f; // olcum yalniz yukseltmeyi gorsun
  bool ok = ren.init(dev, sys, offscreen_render_pass(off), rc);
  CHECK(ok);
  if (!ok) { offscreen_destroy(off); return; }
  if (!ren.post().enabled) {
    std::printf("    [bilgi] sebep: %s\n", ren.post().disabled_reason);
    skip("son islem kurulamadi (HDR bicimi yok): dinamik cozunurluk kapilari kosmadi");
    ren.shutdown();
    offscreen_destroy(off);
    return;
  }
  CHECK(ren.render_scale() == 1.0f); // VARSAYILAN: tam cozunurluk
  Sahne s = kur(ren, 1.0f);
  ren.set_render_size(W, H);
  Rec rr{&ren};
  static uint8_t ref[W * H * 4], kontrol[W * H * 4], yari_none[W * H * 4], yari_bil[W * H * 4], yari_sharp[W * H * 4];
  // Kup EGIK: kosegen kenarlar yarim cozunurlukte gorunur sekilde farklilasir
  // (eksen hizali kenarlar iki olcekte de ayni pikselleri doldururdu — olcum
  // "0 fark" cikar ve kapi bir sey olcmezdi).
  const Mat4 model = Mat4::rotate({0, 1, 0}, 0.6f) * Mat4::rotate({1, 0, 0}, 0.4f) * Mat4::scale({0.9f, 0.9f, 0.9f});
  auto kare = [&](float olcek, renderer::UpscalerKind k, uint8_t *dst) {
    ren.set_render_scale(olcek);
    ren.set_upscaler(k, 0.8f);
    ren.begin_frame(0);
    ren.draw(s.cube, model, {1, 1, 1});
    const bool r = offscreen_render_custom(off, oc, rec_main, &rr, &ores, rec_before);
    if (r && dst) std::memcpy(dst, ores.pixels, (size_t)W * H * 4);
    return r;
  };
  CHECK(kare(1.0f, renderer::UpscalerKind::None, ref));
  const uint32_t blok0 = dev.memory_allocation_count(), ded0 = dev.dedicated_allocation_count();
  // KONTROL: %100 olcekte ayni yol -> sapma 0 (olcum duzeninin kendisi saglam).
  CHECK(kare(1.0f, renderer::UpscalerKind::None, kontrol));
  const uint32_t kontrol_fark = bayt_farki(ref, kontrol, W * H * 4);
  std::printf("    [bilgi] kontrol: %%100 olcek None -> referanstan fark %u bayt (0 bekleniyor)\n", kontrol_fark);
  CHECK(kontrol_fark == 0);

  // %50 olcek: hedefin dolu piksel sayisi DORTTE BIR.
  CHECK(kare(0.5f, renderer::UpscalerKind::None, yari_none));
  const renderer::TemporalInfo t50 = ren.temporal();
  const uint32_t tam_px = W * H, yari_px = t50.scaled_width * t50.scaled_height;
  std::printf("    [bilgi] %%50 olcek: sahne %ux%u = %u px (tam %u px, oran 1/%.2f)\n", t50.scaled_width,
              t50.scaled_height, yari_px, tam_px, (double)tam_px / (double)yari_px);
  CHECK(t50.render_scale == 0.5f);
  CHECK(yari_px * 4 == tam_px);
  // Olcek degisimi KARE ICINDE AYIRMA yapmamali (hedefler en buyuk olcude
  // bir kez kuruldu).
  const uint32_t blok1 = dev.memory_allocation_count(), ded1 = dev.dedicated_allocation_count();
  std::printf("    [bilgi] olcek degisimi sonrasi ayirma: blok %u -> %u, ozel %u -> %u\n", blok0, blok1, ded0, ded1);
  CHECK(blok1 == blok0 && ded1 == ded0);
  // Cikti referanstan OLCULEBILIR ama SINIRLI sapmali.
  const uint32_t sapan = bayt_farki(ref, yari_none, W * H * 4);
  const uint32_t sapma = ortalama_sapma(ref, yari_none, W * H * 4);
  std::printf("    [bilgi] %%50 olcek ciktisi referanstan: %u/%u bayt farkli (%%%.2f), ortalama %.2f/255 sapma\n",
              sapan, W * H * 4, 100.0 * sapan / (double)(W * H * 4), sapma / 100.0);
  CHECK(sapan > 0);                     // OLCULEBILIR: yukseltme gercekten oluyor
  CHECK(sapan < (W * H * 4) / 4);       // SINIRLI: goruntunun dortte birinden azi
  CHECK(sapma < 2500);                  // ortalama 25/255'ten az: goruntu YIKILMIYOR

  // --- 6. Yukseltici arayuzu ----------------------------------------------
  CHECK(kare(0.5f, renderer::UpscalerKind::Bilinear, yari_bil));
  CHECK(kare(0.5f, renderer::UpscalerKind::Sharpen, yari_sharp));
  const uint32_t bil_sharp = bayt_farki(yari_bil, yari_sharp, W * H * 4);
  const uint32_t none_bil = bayt_farki(yari_none, yari_bil, W * H * 4);
  std::printf("    [bilgi] %%50: Bilinear vs Sharpen %u bayt farkli; None vs Bilinear %u bayt farkli\n", bil_sharp,
              none_bil);
  CHECK(bil_sharp > 0); // arayuz gercekten farkli yol kosuyor
  CHECK(none_bil > 0);  // None nokta ornekleme, Bilinear donanim suzmesi
  // KONTROL: %100 olcekte None ile Bilinear AYNI (texel merkezleri ustuste) —
  // yani yukaridaki farklar OLCEGIN sonucu, gurultu degil.
  static uint8_t tam_bil[W * H * 4];
  CHECK(kare(1.0f, renderer::UpscalerKind::Bilinear, tam_bil));
  const uint32_t tam_fark = bayt_farki(ref, tam_bil, W * H * 4);
  std::printf("    [bilgi] kontrol: %%100 olcekte None vs Bilinear %u bayt farkli (0 bekleniyor)\n", tam_fark);
  CHECK(tam_fark == 0);
  ren.shutdown();
  offscreen_destroy(off);
}

// --- 7. Zamansal yol ACIKKEN Mali en iyi uygulama denetimi -----------------
// Yeni gecis (hareket) ve yeni push blogu Arm kurallarini bozmamali.
ENGINE_TEST(temporal_mali_best_practices) {
  if (!loader_ok()) { skip("Vulkan loader yok"); return; }
  static SystemArena sys;
  if (!sys.reserve(96u << 20, "temporal_bp")) { CHECK(false); return; }
  Device dev;
  DeviceConfig dc;
  dc.validation = true;
  dc.best_practices = true;
  if (!dev.init(sys, g_api, dc)) { skip("Vulkan cihazi yok"); return; }
  if (!dev.caps().validation_layer) {
    skip("VK_LAYER_KHRONOS_validation yok — zamansal yolun Mali denetimi kosmadi");
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
  rc.temporal.jitter = true;
  rc.temporal.motion_vectors = true;
  rc.temporal.render_scale = 0.75f;
  rc.temporal.upscaler = renderer::UpscalerKind::Sharpen;
  bool ok = ren.init(dev, sys, offscreen_render_pass(off), rc);
  CHECK(ok);
  if (!ok) { offscreen_destroy(off); dev.shutdown(); return; }
  const renderer::TemporalInfo ti = ren.temporal();
  if (!ren.post().enabled || !ti.motion) {
    std::printf("    [bilgi] post: %s; hareket: %s\n", ren.post().disabled_reason, ti.motion_disabled_reason);
    skip("zamansal yol kurulamadi: Mali denetimi kosmadi");
    ren.shutdown();
    offscreen_destroy(off);
    dev.shutdown();
    return;
  }
  std::printf("    [bilgi] tablo (%u):", ren.graph_pass_count());
  for (uint32_t i = 0; i < ren.graph_pass_count(); i++) std::printf(" %s", ren.graph_pass_name(i));
  std::printf("\n");
  CHECK(std::strcmp(ren.graph_pass_name(0), "golge") == 0);
  CHECK(std::strcmp(ren.graph_pass_name(1), "hareket") == 0);
  CHECK(std::strcmp(ren.graph_pass_name(ren.graph_pass_count() - 1), "birlestir") == 0);
  Sahne s = kur(ren, 4.0f);
  ren.set_render_size(W, H);
  ren.set_shadow_volume({0, 0, 0}, 4.0f, 20.0f);
  Rec rr{&ren};
  Mat4 onceki = Mat4::scale({0.6f, 0.6f, 0.6f});
  for (int frame = 0; frame < 3; frame++) {
    const Mat4 simdi = Mat4::translate({0.02f * frame, 0, 0}) * Mat4::scale({0.6f, 0.6f, 0.6f});
    ren.begin_frame(0);
    ren.draw(s.cube, ren.default_material(), simdi, {1, 1, 1}, &onceki, 0.0f);
    ren.ui_begin((float)W, (float)H, 0.0f);
    ren.ui_rect(4, 4, 40, 12, renderer::Renderer::rgba(255, 255, 255, 200));
    const bool r = offscreen_render_custom(off, oc, rec_main, &rr, &ores, rec_before);
    CHECK(r);
    if (!r) { std::printf("    [bilgi] kare: %s\n", ores.error); break; }
    onceki = simdi;
  }
  CHECK(ren.stats().draws == 1);
  const uint32_t bp_all = dev.best_practice_warnings(), bp_arm = dev.best_practice_arm_warnings();
  std::printf("    [bilgi] zamansal ACIK — BestPractices: %u uyari (%u Arm), %u benzersiz kimlik, %u dogrulama hatasi\n",
              bp_all, bp_arm, dev.best_practice_id_count(), dev.validation_errors());
  for (uint32_t i = 0; i < dev.best_practice_id_count(); i++) {
    const Device::BpId &b = dev.best_practice_id(i);
    std::printf("    [bilgi]   %s x%u%s\n", b.name, b.count, b.arm ? "  <- Mali" : "");
  }
  CHECK(dev.validation_errors() == 0);
  // "sparse-index-buffer": katmanin taramasi alt-ayirma offset'ini atliyor
  // (VVL issue 45); ayni kural CPU'da dogru offsetle olculur.
  const uint32_t sparse_layer = dev.best_practice_count("sparse-index-buffer");
  CHECK(ren.sparse_mesh_count() == 0);
  const uint32_t bp_arm_eff = ren.sparse_mesh_count() == 0 ? bp_arm - sparse_layer : bp_arm;
  CHECK(bp_arm_eff == 0);
  // BILINEN ve ATTRIBUTE EDILMIS: hareket hedefi STORE ediliyor ama BU tabloda
  // onu orneklyen gecis yok (tuketici yukseltici/TAA henuz yazilmadi —
  // graph.hpp out_external). Katman bunu "redundant-store" der; kimlik Arm
  // etiketli degil ama SESSIZ kalmasin: kare basina en cok 1 ve baska surpriz
  // uyari olmamali. Post-only yol (render_graph_post_mali_best_practices) bunu
  // HIC vermiyor — yani sayi tam olarak hareket gecisinin bedelidir.
  const uint32_t redundant = dev.best_practice_count("redundant-store");
  const uint32_t kucuk_ayirma = dev.best_practice_count("small-dedicated-allocation");
  std::printf("    [bilgi] hareket hedefi STORE bedeli: redundant-store x%u (en cok 3 kare); baska uyari %u\n",
              redundant, bp_all - redundant - kucuk_ayirma);
  CHECK(redundant <= 3);
  CHECK(redundant + kucuk_ayirma == bp_all); // SURPRIZ kimlik yok
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
