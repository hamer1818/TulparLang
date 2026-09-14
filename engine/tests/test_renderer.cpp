// Faz 3: golge haritasi gercekten karartiyor mu? Bu test cihazda kosar ve
// GOLGE ACIK / KAPALI iki kareyi karsilastirir. Boyle bir kapi olmadan
// "golge var" iddiasi yalniz gelistiricinin GPU'sunda dogrulanir: boru
// hattinin depthBias birimi surucuye baglidir ve Mali-G72'de golgeyi
// TAMAMEN yok etti (NVIDIA'da dogruydu) — Tuzaklar 8q.
#include <cmath>
#include <cstring>
#include <cstdio>

#include "core/memory/arena.hpp"
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

struct Rec { renderer::Renderer *r; };
void rec_main(VkCommandBuffer cb, void *u) { static_cast<Rec *>(u)->r->record(cb); }
void rec_shadow(VkCommandBuffer cb, void *u) { static_cast<Rec *>(u)->r->record_shadow(cb); }
} // namespace

ENGINE_TEST(renderer_shadow_map_actually_darkens) {
  if (!loader_ok()) { skip("Vulkan loader yok"); return; }
  static SystemArena sys;
  if (!sys.reserve(96u << 20, "renderer_test")) { CHECK(false); return; }
  Device dev;
  DeviceConfig dc;
  if (!dev.init(sys, g_api, dc)) { skip("Vulkan cihazi yok"); return; }
  if (test::gpu_is_virtual(dev.caps().device_name)) { dev.shutdown(); skip("sanal GPU (Apple Paravirtual, CI macOS): piksel kapisi gercek cihazda olculur"); return; }

  const uint32_t W = 256, H = 256;
  OffscreenConfig oc;
  oc.srgb = true; // ekranla ayni yol
  oc.width = W; oc.height = H;
  OffscreenResult ores;
  OffscreenTarget *off = offscreen_create(dev, sys, oc, &ores);
  if (!off) { CHECK(false); std::printf("    [bilgi] offscreen: %s\n", ores.error); dev.shutdown(); return; }

  renderer::Renderer ren;
  renderer::RendererConfig rc;
  rc.shadow_size = 1024;
  rc.frames_in_flight = 1;
  bool ren_ok = ren.init(dev, sys, offscreen_render_pass(off), rc);
  CHECK(ren_ok);
  if (!ren_ok) { offscreen_destroy(off); dev.shutdown(); return; }
  renderer::ShadowInfo sh = ren.shadow();
  std::printf("    [bilgi] golge hedefi: %ux%u format=%d dogrusal_suzme=%d\n", sh.size, sh.size, (int)sh.format,
              (int)sh.linear_filter);
  CHECK(sh.enabled);

  renderer::Vertex v[24];
  uint32_t idx[36];
  uint32_t n = renderer::Renderer::cube(v, idx);
  renderer::MeshHandle cube = ren.create_mesh(v, 24, idx, n);
  n = renderer::Renderer::plane(v, idx);
  renderer::MeshHandle plane = ren.create_mesh(v, 4, idx, n);
  CHECK(cube.valid() && plane.valid());

  // Isik yandan-yukaridan: golge zemine YANA duser, kup kendi golgesini gizlemez.
  Vec3 light = normalize(Vec3{1.0f, 1.4f, 0.0f});
  ren.set_light(light, {0.10f, 0.10f, 0.12f}, 0.9f);
  ren.set_shadow_volume({0, 1.0f, 0}, 9.0f, 40.0f);
  ren.set_camera(Mat4::look_at({0, 7.0f, 9.0f}, {0, 0.5f, 0}, {0, 1, 0}),
                 Mat4::perspective(1.0f, (float)W / (float)H, 0.1f, 100.0f));

  static uint8_t on_px[W * H * 4], off_px[W * H * 4];
  Rec rr{&ren};
  for (int pass = 0; pass < 2; pass++) {
    ren.set_shadows_enabled(pass == 0);
    ren.begin_frame(0);
    ren.draw(plane, Mat4::scale({16, 1, 16}), {0.8f, 0.8f, 0.8f});
    ren.draw(cube, Mat4::translate({0, 2.5f, 0}) * Mat4::scale({2.4f, 2.4f, 2.4f}), {0.9f, 0.3f, 0.2f});
    bool ok = offscreen_render_custom(off, oc, rec_main, &rr, &ores, rec_shadow);
    CHECK(ok);
    if (!ok) { std::printf("    [bilgi] kare: %s\n", ores.error); break; }
    std::memcpy(pass == 0 ? on_px : off_px, ores.pixels, sizeof on_px);
  }

  // Fark eden pikseller = golge. Hepsi KOYULASMIS olmali (golge aydinlatmaz).
  uint32_t diff = 0, darker = 0, lighter = 0;
  for (uint32_t i = 0; i < W * H; i++) {
    int a = on_px[i * 4] + on_px[i * 4 + 1] + on_px[i * 4 + 2];
    int b = off_px[i * 4] + off_px[i * 4 + 1] + off_px[i * 4 + 2];
    if (a == b) continue;
    diff++;
    if (a < b - 8) darker++;
    else if (a > b + 8) lighter++;
  }
  std::printf("    [bilgi] golge acik/kapali farki: %u piksel (%.2f%%), koyulasan %u, acilan %u\n", diff,
              100.0f * (float)diff / (float)(W * H), darker, lighter);
  bool has_shadow = darker > (W * H) / 200; // en az %0.5 piksel koyulasmali
  CHECK(has_shadow);                        // golge hic dusmuyorsa kapi kirmizi
  bool no_brightening = lighter == 0;
  CHECK(no_brightening); // golge aydinlatiyorsa isaret/egilim ters

  // NEGATIF KONTROL: egilim asiri buyurse golge KACAR. Mali-G72'de surucuye
  // bagli depthBias tam olarak bunu yapmisti. Bu gecis kapinin o arizaya
  // DUYARLI oldugunu gosterir; olmazsa test her zaman yesil kalirdi.
  ren.set_shadow_bias(0.0f, 6.0f); // 6 metre kaydirma: golge tamamen kacmali
  ren.set_shadows_enabled(true);
  ren.begin_frame(0);
  ren.draw(plane, Mat4::scale({16, 1, 16}), {0.8f, 0.8f, 0.8f});
  ren.draw(cube, Mat4::translate({0, 2.5f, 0}) * Mat4::scale({2.4f, 2.4f, 2.4f}), {0.9f, 0.3f, 0.2f});
  uint32_t escaped_dark = 0;
  if (offscreen_render_custom(off, oc, rec_main, &rr, &ores, rec_shadow)) {
    for (uint32_t i = 0; i < W * H; i++) {
      int a = ores.pixels[i * 4] + ores.pixels[i * 4 + 1] + ores.pixels[i * 4 + 2];
      int b = off_px[i * 4] + off_px[i * 4 + 1] + off_px[i * 4 + 2];
      if (a < b - 8) escaped_dark++;
    }
  }
  std::printf("    [bilgi] negatif kontrol (6 m kaydirma): koyulasan %u (normalde %u)\n", escaped_dark, darker);
  bool control_fires = escaped_dark * 4 < darker; // golge en az 4 kat azalmali
  CHECK(control_fires);

  ren.shutdown();
  offscreen_destroy(off);
  dev.shutdown();
}

// Kume atamasi (CPU): ortadaki isik orta tile'i isaretler, koseleri isaretlemez;
// kamera arkasindaki isik hicbir seyi isaretlemez; dev isik her seyi isaretler.
#include "renderer/cluster.hpp"
ENGINE_TEST(renderer_cluster_assignment_is_conservative_and_local) {
  using namespace renderer;
  ClusterGrid g;
  g.znear = 0.1f; g.zfar = 100.0f;
  static uint32_t masks[16 * 9 * 24];
  Mat4 view = Mat4::look_at({0, 0, 0}, {0, 0, -1}, {0, 1, 0});
  Mat4 proj = Mat4::perspective(1.2f, 16.0f / 9.0f, g.znear, g.zfar);
  PointLight L[3];
  L[0] = {{0, 0, -10}, 1.0f, {1, 1, 1}, 1.0f};      // onde, kucuk
  L[1] = {{0, 0, +10}, 1.0f, {1, 1, 1}, 1.0f};      // arkada
  L[2] = {{0, 0, -5}, 1000.0f, {1, 1, 1}, 1.0f};    // dev: her yer
  ClusterStats st;
  cluster_assign(view, proj, L, 1, g, masks, &st);
  uint32_t s = cluster_slice_of(g, 10.0f);
  bool center = (masks[(s * g.y + g.y / 2) * g.x + g.x / 2] & 1u) != 0;
  bool corner = (masks[(s * g.y + 0) * g.x + 0] & 1u) != 0;
  bool near_slice = (masks[(0 * g.y + g.y / 2) * g.x + g.x / 2] & 1u) != 0;
  CHECK(center);
  CHECK(!corner);
  CHECK(!near_slice);
  CHECK(st.lights_visible == 1);
  std::printf("    [bilgi] kucuk isik: %u kume (%u toplam), dilim %u\n", st.clusters_touched, g.count(), s);
  cluster_assign(view, proj, L + 1, 1, g, masks, &st);
  bool none = st.clusters_touched == 0 && st.lights_visible == 0;
  CHECK(none);
  cluster_assign(view, proj, L + 2, 1, g, masks, &st);
  bool all = st.clusters_touched == g.count();
  CHECK(all);
  // Dilim formulu tekduze ve sinirli
  bool mono = cluster_slice_of(g, 0.1f) == 0 && cluster_slice_of(g, 100.0f) == g.z - 1 &&
              cluster_slice_of(g, 1.0f) < cluster_slice_of(g, 10.0f);
  CHECK(mono);
}

// Kumelenmis nokta isik gercekten aydinlatiyor mu? Karanlik sahne (ambient ~0,
// yonlu isik 0) + kirmizi nokta isik: isigin altinda kirmizi pikseller olmali,
// isik kaldirilinca (POZITIF KONTROL) olmamali; gorus disina konan isik da
// hicbir seyi aydinlatmamali (kume atamasi ekran uzayinda dogru).
ENGINE_TEST(renderer_point_light_lights_only_near_pixels) {
  if (!loader_ok()) { skip("Vulkan loader yok"); return; }
  static SystemArena sys;
  if (!sys.reserve(96u << 20, "pl_test")) { CHECK(false); return; }
  Device dev;
  DeviceConfig dc;
  if (!dev.init(sys, g_api, dc)) { skip("Vulkan cihazi yok"); return; }
  if (test::gpu_is_virtual(dev.caps().device_name)) { dev.shutdown(); skip("sanal GPU (Apple Paravirtual, CI macOS): piksel kapisi gercek cihazda olculur"); return; }
  const uint32_t W = 256, H = 256;
  OffscreenConfig oc;
  oc.srgb = true; // ekranla ayni yol
  oc.width = W; oc.height = H;
  OffscreenResult ores;
  OffscreenTarget *off = offscreen_create(dev, sys, oc, &ores);
  if (!off) { CHECK(false); dev.shutdown(); return; }
  renderer::Renderer ren;
  renderer::RendererConfig rc;
  rc.shadow_size = 0;
  rc.frames_in_flight = 1;
  bool ren_ok = ren.init(dev, sys, offscreen_render_pass(off), rc);
  CHECK(ren_ok);
  if (!ren_ok) { offscreen_destroy(off); dev.shutdown(); return; }
  ren.set_render_size(W, H);
  renderer::Vertex v[24];
  uint32_t idx[36];
  uint32_t n = renderer::Renderer::plane(v, idx);
  renderer::MeshHandle plane = ren.create_mesh(v, 4, idx, n);
  ren.set_light({0, 1, 0}, {0.02f, 0.02f, 0.02f}, 0.0f); // karanlik: yalniz nokta isik
  ren.set_camera(Mat4::look_at({0, 6.0f, 0.01f}, {0, 0, 0}, {0, 1, 0}), Mat4::perspective(1.0f, 1.0f, 0.1f, 50.0f));
  Rec rr{&ren};
  auto render_count_red = [&](bool with_light, Vec3 pos) {
    ren.clear_point_lights();
    if (with_light) ren.add_point_light(renderer::PointLight{pos, 2.5f, {1.0f, 0.05f, 0.05f}, 6.0f});
    ren.begin_frame(0);
    ren.draw(plane, Mat4::scale({12, 1, 12}), {1, 1, 1});
    if (!offscreen_render_custom(off, oc, rec_main, &rr, &ores, rec_shadow)) return (uint32_t)0xFFFFFFFFu;
    uint32_t red = 0;
    for (uint32_t i = 0; i < W * H; i++) {
      const uint8_t *p = ores.pixels + i * 4;
      if (p[0] > 60 && p[0] > p[1] * 3 && p[0] > p[2] * 3) red++;
    }
    return red;
  };
  uint32_t lit = render_count_red(true, {0, 0.8f, 0});
  uint32_t dark = render_count_red(false, {0, 0.8f, 0});
  uint32_t offscreen_light = render_count_red(true, {40.0f, 0.8f, 40.0f});
  renderer::ClusterStats cs = ren.stats().clusters;
  std::printf("    [bilgi] kirmizi piksel: isikli %u, isiksiz %u, gorus disi isik %u; kume: %u dokunuldu\n", lit, dark,
              offscreen_light, cs.clusters_touched);
  bool has_light = lit > 500 && lit < W * H / 2; // aydinlatir ama tum ekrani degil (sonum)
  CHECK(has_light);
  bool control = dark == 0;
  CHECK(control);
  bool culled = offscreen_light == 0;
  CHECK(culled);
  ren.shutdown();
  offscreen_destroy(off);
  dev.shutdown();
}

// 2B arayuz + font: metin gercekten piksel uretiyor mu? Bos metin (POZITIF
// KONTROL) hicbir sey cizmemeli; genislik olcumu tekduze.
#include "content/font.hpp"
ENGINE_TEST(renderer_ui_text_draws_pixels) {
  char path[1024];
  const char *adir = std::getenv("TULPAR_ENGINE_ASSETS");
  if (adir && *adir) std::snprintf(path, sizeof path, "%s/DejaVuSans.ttf", adir);
  else std::snprintf(path, sizeof path, "%s/assets/fonts/DejaVuSans.ttf", ENGINE_SOURCE_DIR);
  if (FILE *f = std::fopen(path, "rb")) std::fclose(f); else { skip("font yok (assets/fonts/DejaVuSans.ttf)"); return; }
  if (!loader_ok()) { skip("Vulkan loader yok"); return; }
  static SystemArena sys;
  if (!sys.reserve(96u << 20, "ui_test")) { CHECK(false); return; }
  Device dev;
  DeviceConfig dc;
  if (!dev.init(sys, g_api, dc)) { skip("Vulkan cihazi yok"); return; }
  const uint32_t W = 256, H = 128;
  OffscreenConfig oc;
  oc.srgb = true; // ekranla ayni yol
  oc.width = W; oc.height = H;
  OffscreenResult ores;
  OffscreenTarget *off = offscreen_create(dev, sys, oc, &ores);
  if (!off) { CHECK(false); dev.shutdown(); return; }
  renderer::Renderer ren;
  renderer::RendererConfig rc;
  rc.shadow_size = 0;
  rc.frames_in_flight = 1;
  bool ren_ok = ren.init(dev, sys, offscreen_render_pass(off), rc);
  CHECK(ren_ok);
  if (!ren_ok) { offscreen_destroy(off); dev.shutdown(); return; }
  content::Font font;
  bool fok = font.load(sys, ren, path, 24.0f);
  CHECK(fok);
  if (!fok) { ren.shutdown(); offscreen_destroy(off); dev.shutdown(); return; }
  float wT = font.text_width("T"), wTulpar = font.text_width("Tulpar"), wTr = font.text_width("Şığ");
  bool widths = wT > 0 && wTulpar > 3 * wT && wTr > 0;
  CHECK(widths);
  ren.set_camera(Mat4::identity(), Mat4::identity());
  (void)0;
  auto bright = [&](const char *text) {
    ren.begin_frame(0);
    ren.ui_begin((float)W, (float)H);
    if (text) font.draw(ren, 8, 40, text, renderer::Renderer::rgba(255, 255, 255));
    struct Ctx { renderer::Renderer *r; } cx{&ren};
    auto rec = [](VkCommandBuffer cb, void *u) { auto *c = static_cast<Ctx *>(u); c->r->record(cb); c->r->ui_record(cb); };
    if (!offscreen_render_custom(off, oc, rec, &cx, &ores, rec_shadow)) return (uint32_t)0xFFFFFFFFu;
    uint32_t n = 0;
    for (uint32_t i = 0; i < W * H; i++) if (ores.pixels[i * 4] > 128) n++;
    return n;
  };
  uint32_t with = bright("Tulpar Engine ğüşİ"), without = bright(nullptr), rect_only = 0;
  { // duz kutu da cizilebilmeli (beyaz texel)
    ren.begin_frame(0);
    ren.ui_begin((float)W, (float)H);
    ren.ui_set_atlas(font.atlas());
    ren.ui_rect(10, 10, 50, 20, renderer::Renderer::rgba(255, 255, 255));
    struct Ctx { renderer::Renderer *r; } cx{&ren};
    auto rec = [](VkCommandBuffer cb, void *u) { auto *c = static_cast<Ctx *>(u); c->r->record(cb); c->r->ui_record(cb); };
    if (offscreen_render_custom(off, oc, rec, &cx, &ores, rec_shadow))
      for (uint32_t i = 0; i < W * H; i++) if (ores.pixels[i * 4] > 128) rect_only++;
  }
  std::printf("    [bilgi] font: T=%.1f Tulpar=%.1f Sig=%.1f px; parlak piksel metinli %u, bos %u, kutu %u\n", wT, wTulpar, wTr,
              with, without, rect_only);
  bool draws = with > 300;
  CHECK(draws);
  bool control = without == 0;
  CHECK(control);
  bool rect_ok = rect_only >= 50 * 20 - 40 && rect_only <= 50 * 20 + 40;
  CHECK(rect_ok);
  ren.shutdown();
  offscreen_destroy(off);
  dev.shutdown();
}

// Mali "en iyi uygulama" kapisi — PerfDoc'un (Arm, arsivlendi) ardili: Khronos
// dogrulama katmaninin BestPractices + Arm satici kurallari. Tam bir kare
// (golge gecisi + doku + nokta isik + UI) kaydedilir; Arm kimlikli uyari 0
// olmali. Genel BestPractices kimlikleri RAPORLANIR (bilgi; her biri ayri karar).
// POZITIF KONTROL: 4 KiB'lik vkAllocateMemory katmanin "small-allocation"
// uyarisini tetiklemeli — tetiklemiyorsa katman denetlemiyor, kapi bos.
ENGINE_TEST(renderer_mali_best_practices_gate) {
  if (!loader_ok()) { skip("Vulkan loader yok"); return; }
  static SystemArena sys;
  if (!sys.reserve(96u << 20, "renderer_bp")) { CHECK(false); return; }
  Device dev;
  DeviceConfig dc;
  dc.validation = true;
  dc.best_practices = true;
  if (!dev.init(sys, g_api, dc)) { skip("Vulkan cihazi yok"); return; }
  if (!dev.caps().validation_layer) {
    skip("VK_LAYER_KHRONOS_validation yok — Mali en iyi uygulama denetimi kosmadi");
    dev.shutdown();
    return;
  }
  CHECK(dev.caps().best_practices);
  CHECK(dev.caps().debug_messenger); // mesaj kanali yoksa asagidaki 0'lar olcum degil

  const uint32_t W = 128, H = 128;
  OffscreenConfig oc;
  oc.srgb = true; // ekranla ayni yol
  oc.width = W; oc.height = H;
  OffscreenResult ores;
  OffscreenTarget *off = offscreen_create(dev, sys, oc, &ores);
  if (!off) { CHECK(false); std::printf("    [bilgi] offscreen: %s\n", ores.error); dev.shutdown(); return; }
  renderer::Renderer ren;
  renderer::RendererConfig rc; // gercek yapilandirma (golge 2048): kucuk golge
  rc.frames_in_flight = 1;     // "small-dedicated-allocation" raporu verir, gercek degil
  bool ren_ok = ren.init(dev, sys, offscreen_render_pass(off), rc);
  CHECK(ren_ok);
  if (!ren_ok) { offscreen_destroy(off); dev.shutdown(); return; }

  renderer::Vertex v[24];
  uint32_t idx[36];
  uint32_t n = renderer::Renderer::cube(v, idx);
  renderer::MeshHandle cube = ren.create_mesh(v, 24, idx, n);
  n = renderer::Renderer::plane(v, idx);
  renderer::MeshHandle plane = ren.create_mesh(v, 4, idx, n);
  static uint8_t tex[16 * 16 * 4];
  for (uint32_t i = 0; i < 16 * 16; i++) { uint8_t c = ((i % 16) / 8 + (i / 16) / 8) % 2 ? 230 : 40; tex[i * 4] = tex[i * 4 + 1] = tex[i * 4 + 2] = c; tex[i * 4 + 3] = 255; }
  renderer::TextureHandle th = ren.create_texture(tex, 16, 16, true);
  renderer::MaterialHandle mh = ren.create_material(th, {1, 1, 1});
  ren.set_light(normalize(Vec3{1.0f, 1.4f, 0.0f}), {0.10f, 0.10f, 0.12f}, 0.9f);
  ren.set_shadow_volume({0, 1.0f, 0}, 9.0f, 40.0f);
  ren.set_camera(Mat4::look_at({0, 7.0f, 9.0f}, {0, 0.5f, 0}, {0, 1, 0}),
                 Mat4::perspective(1.0f, (float)W / (float)H, 0.1f, 100.0f));
  ren.set_render_size(W, H);
  Rec rr{&ren};
  for (int frame = 0; frame < 3; frame++) {
    ren.begin_frame(0);
    ren.clear_point_lights();
    ren.add_point_light({{2, 1.5f, 0}, 6.0f, {1, 0.2f, 0.2f}, 4.0f});
    ren.draw(plane, mh, Mat4::scale({16, 1, 16}), {0.8f, 0.8f, 0.8f});
    ren.draw(cube, Mat4::translate({0, 2.5f, 0}) * Mat4::scale({2.4f, 2.4f, 2.4f}), {0.9f, 0.3f, 0.2f});
    ren.ui_begin((float)W, (float)H, 0.0f);
    ren.ui_rect(4, 4, 40, 12, renderer::Renderer::rgba(255, 255, 255, 200));
    bool ok = offscreen_render_custom(off, oc, rec_main, &rr, &ores, rec_shadow);
    CHECK(ok);
    if (!ok) { std::printf("    [bilgi] kare: %s\n", ores.error); break; }
  }
  const uint32_t bp_all = dev.best_practice_warnings(), bp_arm = dev.best_practice_arm_warnings();
  std::printf("    [bilgi] BestPractices: %u uyari (%u Arm), %u benzersiz kimlik, %u dogrulama hatasi\n", bp_all, bp_arm,
              dev.best_practice_id_count(), dev.validation_errors());
  for (uint32_t i = 0; i < dev.best_practice_id_count(); i++) {
    const Device::BpId &b = dev.best_practice_id(i);
    std::printf("    [bilgi]   %s x%u%s\n", b.name, b.count, b.arm ? "  <- Mali" : "");
  }
  CHECK(dev.validation_errors() == 0);
  // "sparse-index-buffer": katmanin taramasi alt-ayirma OFFSET'ini atliyor (VVL
  // issue 45) — blok basini indeks sanip %0.00 der. Ayni kural CPU'da, dogru
  // offsetle olculur (Renderer::sparse_mesh_count); o 0 ise katmanin bu kimligi
  // sahte pozitiftir ve ACIKCA dusulur. Baska hicbir kimlik dusulmez.
  const uint32_t sparse_layer = dev.best_practice_count("sparse-index-buffer");
  std::printf("    [bilgi] seyrek indeks: CPU olcumu %u mesh, katman %u uyari%s\n", ren.sparse_mesh_count(), sparse_layer,
              sparse_layer && ren.sparse_mesh_count() == 0 ? " (katman sahte pozitifi: alt-ayirma offset'i, VVL 45)" : "");
  CHECK(ren.sparse_mesh_count() == 0);
  const uint32_t bp_arm_effective = ren.sparse_mesh_count() == 0 ? bp_arm - sparse_layer : bp_arm;
  CHECK(bp_arm_effective == 0); // Mali kurali ihlali = kapi kirmizi

  // POZITIF KONTROL: Arm kurali gercekten acik mi? LOD kirpan sampler
  // (minLod=maxLod=0) "BestPractices-Arm-vkCreateSampler-lod-clamping" vermeli.
  // Vermezse Arm denetimi kapali demektir ve yukaridaki 0, hicbir seyi olcmuyor.
  {
    VkSamplerCreateInfo si{};
    si.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    si.magFilter = si.minFilter = VK_FILTER_NEAREST;
    si.maxLod = 0.0f;
    VkSampler smp = VK_NULL_HANDLE;
    const uint32_t before = dev.best_practice_arm_warnings();
    if (dev.api().vkCreateSampler(dev.handle(), &si, nullptr, &smp) == VK_SUCCESS) dev.api().vkDestroySampler(dev.handle(), smp, nullptr);
    const uint32_t after = dev.best_practice_arm_warnings();
    std::printf("    [bilgi] pozitif kontrol (LOD kirpan sampler): Arm uyarisi %u -> %u\n", before, after);
    if (after == before) {
      // Katman Arm kurallarini bilmiyor (eski surum: VK_EXT_layer_settings /
      // validate_best_practices_arm yok). Kapi OLCEMIYOR: sessiz yesil degil,
      // gorunur atlama. (CI'daki apt katmani bu durumda; yerel/telefon 1.4.357.)
      ren.shutdown();
      offscreen_destroy(off);
      dev.shutdown();
      skip("dogrulama katmani Arm BestPractices kurallarini tanimiyor (surum) — Mali kapisi olculemedi");
      return;
    }
    bool control_fires = after > before;
    CHECK(control_fires);
  }
  ren.shutdown();
  offscreen_destroy(off);
  dev.shutdown();
}

// Renk uzayi (Filament PBR tarifi): dokular sRGB bicimli (ornekleme dogrusal),
// aydinlatma dogrusal, hedef sRGB bicimli (donanim kodlar). Kapi: gri doku
// degerleri {32,128,200,255} isiksiz duz yuzeyde AYNEN geri okunmali (gidis-donus
// birim). POZITIF KONTROL: UNORM hedefe kodlamadan yazinca degerler DOGRUSAL cikar
// (128 -> ~55); olcum buna duyarli. Yedek yol (UNORM hedef + shader kodlama) da birim.
static void srgb_scene(renderer::Renderer &ren, renderer::MeshHandle plane, renderer::MaterialHandle mat, uint32_t W,
                       uint32_t H) {
  ren.set_light({0, 1, 0}, {1, 1, 1}, 0.0f); // yalniz ambient = albedo
  ren.set_shadows_enabled(false);
  ren.set_camera(Mat4::look_at({0, 5.0f, 0}, {0, 0, 0}, {0, 0, -1}), Mat4::ortho(-8, 8, -8, 8, 0.1f, 20.0f));
  ren.set_render_size(W, H);
  ren.begin_frame(0);
  ren.clear_point_lights();
  ren.draw(plane, mat, Mat4::scale({16, 1, 16}), {1, 1, 1});
}
static void srgb_quadrants(const uint8_t *px, uint32_t W, uint32_t H, int out[4]) {
  const uint32_t xs[2] = {W / 4, 3 * W / 4}, ys[2] = {H / 4, 3 * H / 4};
  int k = 0;
  for (int j = 0; j < 2; j++)
    for (int i = 0; i < 2; i++) out[k++] = px[(ys[j] * W + xs[i]) * 4];
  for (int a = 0; a < 4; a++) // sirala (yonelim onemsiz)
    for (int b = a + 1; b < 4; b++)
      if (out[b] < out[a]) { int t = out[a]; out[a] = out[b]; out[b] = t; }
}
ENGINE_TEST(renderer_srgb_roundtrip_is_identity) {
  if (!loader_ok()) { skip("Vulkan loader yok"); return; }
  static SystemArena sys;
  if (!sys.reserve(96u << 20, "renderer_srgb")) { CHECK(false); return; }
  Device dev;
  DeviceConfig dc;
  if (!dev.init(sys, g_api, dc)) { skip("Vulkan cihazi yok"); return; }
  if (test::gpu_is_virtual(dev.caps().device_name)) { dev.shutdown(); skip("sanal GPU (Apple Paravirtual, CI macOS): piksel kapisi gercek cihazda olculur"); return; }
  const uint32_t W = 64, H = 64;
  static const uint8_t vals[4] = {32, 128, 200, 255};
  // 64x64, dort tekduze ceyrek: ornekleme noktalari kenardan uzak, suzme karismaz.
  static uint8_t tex[64 * 64 * 4];
  for (uint32_t y = 0; y < 64; y++)
    for (uint32_t x = 0; x < 64; x++) {
      const uint8_t c = vals[(y / 32) * 2 + (x / 32)];
      uint8_t *p = tex + (y * 64 + x) * 4;
      p[0] = p[1] = p[2] = c; p[3] = 255;
    }
  // Uc yapilandirma: (0) sRGB hedef + donanim kodlama [urun], (1) UNORM hedef +
  // kodlama YOK [pozitif kontrol: yanlis], (2) UNORM hedef + shader kodlama [yedek].
  int q[3][4] = {};
  bool ran[3] = {};
  for (int cfg = 0; cfg < 3; cfg++) {
    OffscreenConfig oc;
    oc.width = W; oc.height = H;
    oc.srgb = cfg == 0;
    OffscreenResult ores;
    OffscreenTarget *off = offscreen_create(dev, sys, oc, &ores);
    if (!off) { CHECK(false); std::printf("    [bilgi] offscreen: %s\n", ores.error); break; }
    renderer::Renderer ren;
    renderer::RendererConfig rc;
    rc.frames_in_flight = 1;
    rc.shadow_size = 0;
    rc.srgb_target = cfg != 2;
    if (!ren.init(dev, sys, offscreen_render_pass(off), rc)) { CHECK(false); offscreen_destroy(off); break; }
    renderer::Vertex v[4];
    uint32_t idx[6];
    uint32_t n = renderer::Renderer::plane(v, idx);
    renderer::MeshHandle plane = ren.create_mesh(v, 4, idx, n);
    renderer::MaterialHandle mat = ren.create_material(ren.create_texture(tex, 64, 64, false, true), {1, 1, 1});
    srgb_scene(ren, plane, mat, W, H);
    Rec rr{&ren};
    if (offscreen_render_custom(off, oc, rec_main, &rr, &ores, rec_shadow)) {
      srgb_quadrants(ores.pixels, W, H, q[cfg]);
      ran[cfg] = true;
    }
    ren.shutdown();
    offscreen_destroy(off);
  }
  dev.shutdown();
  static const char *names[3] = {"sRGB hedef (urun)", "UNORM + kodlama yok (kontrol)", "UNORM + shader kodlama (yedek)"};
  for (int cfg = 0; cfg < 3; cfg++)
    std::printf("    [bilgi] %-32s -> %3d %3d %3d %3d (beklenen 32 128 200 255%s)\n", names[cfg], q[cfg][0], q[cfg][1],
                q[cfg][2], q[cfg][3], cfg == 1 ? " DEGIL: dogrusal ~4 55 147 255" : "");
  CHECK(ran[0] && ran[1] && ran[2]);
  auto near4 = [&](const int *a, int tol) {
    for (int i = 0; i < 4; i++) if (std::abs(a[i] - (int)vals[i]) > tol) return false;
    return true;
  };
  bool product_identity = near4(q[0], 2);
  CHECK(product_identity);
  bool fallback_identity = near4(q[2], 3);
  CHECK(fallback_identity);
  bool control_is_linear = q[1][1] < 70 && q[1][2] < 165; // 128->55, 200->147: kodlanmamis
  CHECK(control_is_linear);
}
