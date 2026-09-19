// Faz 3: golge haritasi gercekten karartiyor mu? Bu test cihazda kosar ve
// GOLGE ACIK / KAPALI iki kareyi karsilastirir. Boyle bir kapi olmadan
// "golge var" iddiasi yalniz gelistiricinin GPU'sunda dogrulanir: boru
// hattinin depthBias birimi surucuye baglidir ve Mali-G72'de golgeyi
// TAMAMEN yok etti (NVIDIA'da dogruydu) — Tuzaklar 8q.
#include <algorithm>
#include <cmath>
#include <cstring>
#include <cstdio>
#include <cstdlib>

#include "content/gltf.hpp"
#include "core/memory/arena.hpp"
#include "platform/time.hpp"
#include "renderer/renderer.hpp"
#include "rhi/device.hpp"
#include "rhi/offscreen.hpp"
#include "rhi/shaders/mesh_frag_spv.h"
#include "rhi/shaders/mesh_vert_spv.h"
#include "rhi/tile_budget.hpp"
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
  // PBR malzeme de ayni karede cizilir: set 1 binding 1 (malzeme UBO'su, cihazin
  // hizasina yuvarlanmis ofset) ve mesh.frag'in ozellestirme sabiti bu kapinin
  // DOGRULAMA KATMANI altinda kosmali — yoksa PBR yolu hicbir yerde denetlenmez.
  renderer::PbrParams bp_pbr;
  bp_pbr.metallic = 1.0f;
  bp_pbr.roughness = 0.3f;
  bp_pbr.emissive = {0.1f, 0.05f, 0.0f};
  renderer::MaterialHandle mh_pbr = ren.create_material(th, {1, 1, 1}, bp_pbr);
  CHECK(mh_pbr.valid());
  // DOKULU PBR malzeme de ayni karede cizilir: set 1'in binding 2/3/4
  // descriptor yazimlari ve shader'in ORM/normal/isima dallari DOGRULAMA
  // KATMANI altinda kosmali. Yoksa doku basina kanallarin tamami — uc sampler,
  // uc descriptor yazimi — hicbir yerde denetlenmez.
  static uint8_t orm_px[8 * 8 * 4], nrm_px[8 * 8 * 4], emi_px[8 * 8 * 4];
  for (uint32_t i = 0; i < 8 * 8; i++) {
    orm_px[i * 4] = 200; orm_px[i * 4 + 1] = 90; orm_px[i * 4 + 2] = 40; orm_px[i * 4 + 3] = 255;
    nrm_px[i * 4] = 150; nrm_px[i * 4 + 1] = 120; nrm_px[i * 4 + 2] = 240; nrm_px[i * 4 + 3] = 255;
    emi_px[i * 4] = 40; emi_px[i * 4 + 1] = 30; emi_px[i * 4 + 2] = 20; emi_px[i * 4 + 3] = 255;
  }
  renderer::PbrTextures bp_tex;
  bp_tex.orm = ren.create_texture(orm_px, 8, 8, true, false);     // veri: DOGRUSAL
  bp_tex.normal = ren.create_texture(nrm_px, 8, 8, true, false);  // veri: DOGRUSAL
  bp_tex.emissive = ren.create_texture(emi_px, 8, 8, true, true); // renk: sRGB
  bp_tex.normal_scale = 1.0f;
  bp_tex.occlusion_strength = 1.0f;
  renderer::MaterialHandle mh_pbr_tex = ren.create_material(th, {1, 1, 1}, bp_pbr, bp_tex);
  CHECK(mh_pbr_tex.valid());
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
    ren.draw(cube, mh_pbr, Mat4::translate({-3.0f, 1.2f, 1.5f}) * Mat4::scale({1.6f, 1.6f, 1.6f}), {1, 1, 1});
    ren.draw(cube, mh_pbr_tex, Mat4::translate({3.0f, 1.2f, 1.5f}) * Mat4::scale({1.6f, 1.6f, 1.6f}), {1, 1, 1});
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
  // Sonda ARTIK ORTAK (test::arm_rules_missing, govdesi test_main.cpp): uc yeni
  // Mali kapisi bu bloku kopyalayip KORUMAYI atlayinca CI Linux'ta dordu
  // birden kirmizi dondu (2026-09-16). Tek yer = bir daha ayrisamaz.
  if (test::arm_rules_missing(dev)) {
    // Katman Arm kurallarini bilmiyor (eski surum: VK_EXT_layer_settings /
    // validate_best_practices_arm yok). Kapi OLCEMIYOR: sessiz yesil degil,
    // gorunur atlama. (CI'daki apt katmani bu durumda; yerel/telefon 1.4.357.)
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

// Kademeli golge (CSM) gercekten YAKIN ALANI keskinlestiriyor mu? Olcum
// referansa yakinliktir: ayni sahne uc kurulumla cizilir —
//   (R) referans: tek kademe, 2048 tile (ince texel, "dogru" cevap)
//   (K) kaba:     tek kademe, 256 tile  (ayni hacim, 8x kaba texel)
//   (C) kademeli: 3 kademe, 256 tile    (yakin kademe hacmin 1/9'u = ince)
// Kapi: C, referansa K'dan BELIRGIN daha yakin olmali. Kontrol K'nin kendisi:
// kaba kurulum gercekten bozuluyor mu (yoksa test hicbir sey olcmez).
// Not: hepsi ayni bellek sinifinda degil — amac kalite/oran, mutlak bellek degil.
ENGINE_TEST(renderer_cascades_sharpen_near_shadows) {
  if (!loader_ok()) { skip("Vulkan loader yok"); return; }
  static SystemArena sys;
  if (!sys.reserve(192u << 20, "cascade_test")) { CHECK(false); return; }
  Device dev;
  DeviceConfig dc;
  if (!dev.init(sys, g_api, dc)) { skip("Vulkan cihazi yok"); return; }
  if (test::gpu_is_virtual(dev.caps().device_name)) { dev.shutdown(); skip("sanal GPU (CI macOS): piksel kapisi gercek cihazda"); return; }

  const uint32_t W = 256, H = 256;
  OffscreenConfig oc;
  oc.srgb = true;
  oc.width = W; oc.height = H;
  OffscreenResult ores;
  OffscreenTarget *off = offscreen_create(dev, sys, oc, &ores);
  if (!off) { CHECK(false); dev.shutdown(); return; }

  static uint8_t px[3][W * H * 4];
  uint32_t casc_reported[3] = {0, 0, 0};
  // Genis dunya (yaricap 45): tek kademe kaba texel demektir. Ince ayrinti icin
  // ince direkler; golgeleri yakin kademede net, kaba haritada erimis cikar.
  const struct { uint32_t cascades, size; } kSetup[3] = {{1, 2048}, {1, 256}, {3, 256}};
  bool all_ok = true;
  for (int pass = 0; pass < 3 && all_ok; pass++) {
    renderer::Renderer ren;
    renderer::RendererConfig rc;
    rc.shadow_size = kSetup[pass].size;
    rc.shadow_cascades = kSetup[pass].cascades;
    rc.frames_in_flight = 1;
    if (!ren.init(dev, sys, offscreen_render_pass(off), rc)) { CHECK(false); all_ok = false; break; }
    casc_reported[pass] = ren.shadow().cascades;
    renderer::Vertex v[24];
    uint32_t idx[36];
    uint32_t n = renderer::Renderer::cube(v, idx);
    renderer::MeshHandle cube = ren.create_mesh(v, 24, idx, n);
    n = renderer::Renderer::plane(v, idx);
    renderer::MeshHandle plane = ren.create_mesh(v, 4, idx, n);
    ren.set_light(normalize(Vec3{1.0f, 1.4f, 0.0f}), {0.10f, 0.10f, 0.12f}, 0.9f);
    ren.set_shadow_volume({0, 1.0f, 0}, 45.0f, 120.0f); // genis hacim: tek kademede kaba
    ren.set_shadow_focus({0, 0.5f, 0});                 // yakin kademe kameranin baktigi yerde
    ren.set_camera(Mat4::look_at({0, 5.0f, 7.0f}, {0, 0.5f, 0}, {0, 1, 0}),
                   Mat4::perspective(1.0f, (float)W / (float)H, 0.1f, 100.0f));
    ren.begin_frame(0);
    ren.draw(plane, Mat4::scale({40, 1, 40}), {0.8f, 0.8f, 0.8f});
    for (int i = -2; i <= 2; i++) // ince direkler: kaba texel bunlari yutar
      ren.draw(cube, Mat4::translate({(float)i * 1.2f, 1.0f, 0}) * Mat4::scale({0.16f, 2.0f, 0.16f}), {0.9f, 0.3f, 0.2f});
    Rec rr{&ren};
    const bool ok = offscreen_render_custom(off, oc, rec_main, &rr, &ores, rec_shadow);
    CHECK(ok);
    if (ok) std::memcpy(px[pass], ores.pixels, sizeof px[0]);
    else all_ok = false;
    ren.shutdown();
  }
  if (!all_ok) { offscreen_destroy(off); dev.shutdown(); return; }
  CHECK(casc_reported[0] == 1 && casc_reported[2] == 3);

  // Referanstan sapma: mutlak parlaklik farkinin toplami (sadece ZEMIN bolgesi;
  // direklerin kendisi her kurulumda ayni ciziliyor).
  auto deviation = [&](int a) {
    uint64_t sum = 0;
    for (uint32_t i = 0; i < W * H; i++) {
      const int la = px[a][i * 4] + px[a][i * 4 + 1] + px[a][i * 4 + 2];
      const int lr = px[0][i * 4] + px[0][i * 4 + 1] + px[0][i * 4 + 2];
      sum += (uint64_t)(la > lr ? la - lr : lr - la);
    }
    return sum;
  };
  const uint64_t d_coarse = deviation(1), d_casc = deviation(2);
  std::printf("    [bilgi] referanstan sapma: kaba(1x256) %llu, kademeli(3x256) %llu (%.2fx daha yakin)\n",
              (unsigned long long)d_coarse, (unsigned long long)d_casc,
              d_casc ? (double)d_coarse / (double)d_casc : 0.0);
  // KONTROL: kaba kurulum gercekten bozulmus olmali (yoksa karsilastirma bos).
  CHECK(d_coarse > (uint64_t)W * H / 8);
  // KAPI: kademeli, kabanin en fazla yarisi kadar sapmali.
  CHECK(d_casc * 2 < d_coarse);
  offscreen_destroy(off);
  dev.shutdown();
}

// Paketlenmis vertex formati (plan Faz 3 "packed format bit butcesi"):
// normal oktahedral SNORM16x2, UV yarim hassasiyet -> 32 bayt yerine 20.
// Uc sey olculur: (1) kodek hassasiyeti (aci hatasi), KONTROL olarak ayni
// kodlamanin 8-bitlik surumu belirgin kotu olmali — yoksa esik bir sey
// olcmuyordur; (2) GPU'daki vertex bayti gercekten 20/vertex; (3) GPU'nun
// cozmesi CPU'nun kodlamasiyla ORTUSUYOR: bilinen isikla aydinlatilan kupun
// yuz parlakliklari analitik Lambert degerine oturmali (kodlama/cozme kayarsa
// yuzler yanlis parlar).
ENGINE_TEST(renderer_packed_vertex_keeps_normals) {
  // (1) Kodek: kure uzerinde duzgun dagilmis yonlerde en buyuk aci hatasi.
  double worst16 = 0.0, worst8 = 0.0;
  for (int i = 0; i < 2000; i++) {
    const float u = (float)((i * 7919) % 1000) / 1000.0f * 2.0f - 1.0f;
    const float phi = (float)((i * 5077) % 1000) / 1000.0f * 6.2831853f;
    const float r = std::sqrt(std::fmax(0.0f, 1.0f - u * u));
    const Vec3 n{r * std::cos(phi), u, r * std::sin(phi)};
    int16_t e[2];
    renderer::Renderer::encode_normal(n, e);
    const Vec3 d = renderer::Renderer::decode_normal(e);
    worst16 = std::fmax(worst16, std::acos((double)std::fmin(1.0f, dot(n, d))) * 57.2957795);
    // KONTROL: ayni kodlama 8 bite kirpilirsa (snorm8) hata buyumeli.
    int16_t e8[2] = {(int16_t)((e[0] / 256) * 256), (int16_t)((e[1] / 256) * 256)};
    const Vec3 d8 = renderer::Renderer::decode_normal(e8);
    worst8 = std::fmax(worst8, std::acos((double)std::fmin(1.0f, dot(n, d8))) * 57.2957795);
  }
  std::printf("    [bilgi] normal kodek en buyuk aci hatasi: 16 bit %.3f derece, 8 bit %.3f derece\n", worst16, worst8);
  CHECK(worst16 < 0.25);
  CHECK(worst8 > 4.0 * worst16); // kontrol: esik gercekten hassasiyet olcuyor
  CHECK(renderer::Renderer::gpu_vertex_bytes() == 20 && renderer::Renderer::author_vertex_bytes() == 32);

  if (!loader_ok()) { skip("Vulkan loader yok"); return; }
  static SystemArena sys;
  if (!sys.reserve(96u << 20, "packed_test")) { CHECK(false); return; }
  Device dev;
  DeviceConfig dc;
  if (!dev.init(sys, g_api, dc)) { skip("Vulkan cihazi yok"); return; }
  if (test::gpu_is_virtual(dev.caps().device_name)) { dev.shutdown(); skip("sanal GPU (CI macOS): piksel kapisi gercek cihazda"); return; }
  const uint32_t W = 128, H = 128;
  OffscreenConfig oc;
  oc.srgb = false; // dogrusal cikti: parlaklik analitik degerle DOGRUDAN karsilastirilir
  oc.width = W; oc.height = H;
  OffscreenResult ores;
  OffscreenTarget *off = offscreen_create(dev, sys, oc, &ores);
  if (!off) { CHECK(false); dev.shutdown(); return; }
  renderer::Renderer ren;
  renderer::RendererConfig rc;
  rc.frames_in_flight = 1;
  rc.shadow_size = 0; // golge yok: yalniz normal/aydinlatma olculsun
  if (!ren.init(dev, sys, offscreen_render_pass(off), rc)) { CHECK(false); offscreen_destroy(off); dev.shutdown(); return; }
  renderer::Vertex v[24];
  uint32_t idx[36];
  const uint32_t n = renderer::Renderer::cube(v, idx);
  renderer::MeshHandle cube = ren.create_mesh(v, 24, idx, n);
  CHECK(cube.valid());
  // (2) Bellek: 24 vertex x 20 bayt.
  std::printf("    [bilgi] GPU vertex bayti: %llu (24 vertex x %u); yazar duzeni %u bayt olurdu\n",
              (unsigned long long)ren.vertex_bytes(), renderer::Renderer::gpu_vertex_bytes(),
              renderer::Renderer::author_vertex_bytes());
  CHECK(ren.vertex_bytes() == 24ull * renderer::Renderer::gpu_vertex_bytes());

  // (3) Piksel: isik +X'ten; ortam 0, diffuse 1 -> +X yuzu tam parlak, +Y yuzu koyu.
  const Vec3 L = normalize(Vec3{1, 0, 0});
  ren.set_light(L, {0.0f, 0.0f, 0.0f}, 1.0f);
  ren.set_shadows_enabled(false);
  ren.set_camera(Mat4::look_at({3.0f, 2.2f, 3.0f}, {0, 0, 0}, {0, 1, 0}),
                 Mat4::perspective(0.9f, 1.0f, 0.1f, 50.0f));
  ren.begin_frame(0);
  ren.draw(cube, Mat4::scale({2, 2, 2}), {1.0f, 1.0f, 1.0f});
  Rec rr{&ren};
  const bool ok = offscreen_render_custom(off, oc, rec_main, &rr, &ores, rec_shadow);
  CHECK(ok);
  if (ok) {
    // Kamera (3, 2.2, 3): +X, +Y ve +Z yuzleri gorunur. Analitik Lambert:
    // +X: dot(+X, L) = 1 -> beyaz; +Y ve +Z: dot = 0 -> siyah.
    uint32_t bright = 0, dark = 0, mid = 0;
    for (uint32_t i = 0; i < W * H; i++) {
      const uint8_t *p = ores.pixels + i * 4;
      const int lum = (p[0] + p[1] + p[2]) / 3;
      if (lum > 240) bright++;
      else if (lum < 12) dark++;
      else if (lum > 40 && lum < 200) mid++; // ne tam isikli ne tam golgede
    }
    std::printf("    [bilgi] kup yuzleri: parlak %u, koyu %u, ara ton %u piksel\n", bright, dark, mid);
    CHECK(bright > 300);  // +X yuzu tam aydinlik (normal dogru cozuldu)
    CHECK(dark > 300);    // +Y/+Z yuzleri karanlik
    // Ara ton az olmali: normal kaymasi olsaydi duz yuzler ara tonlara dagilirdi.
    CHECK(mid * 10 < bright);
  }
  ren.shutdown();
  offscreen_destroy(off);
  dev.shutdown();
}

// ===========================================================================
// PBR (metallic-roughness) — Cook-Torrance mikroyuzey modeli
// ===========================================================================
// Olcum kuresi: UV kuresi (yaricap 1), normal = konum. Kup ve duzlemden farkli
// olarak TUM normal yonlerini gosterir; hem parlama lobu hem de silueti (grazing
// aci) tek karede olculebilir. Sarim kup ile ayni sozlesmede: (i,j)->(i,j+1)->
// (i+1,j+1) disaridan bakildiginda CCW (T x B = sin(theta) * P, yani disa dogru).
namespace {
constexpr uint32_t kSphRings = 32, kSphSegs = 48;
constexpr uint32_t kSphVerts = (kSphRings + 1) * (kSphSegs + 1);
constexpr uint32_t kSphIdx = kSphRings * kSphSegs * 6;
uint32_t make_sphere(renderer::Vertex *v, uint32_t *idx) {
  uint32_t vi = 0;
  for (uint32_t i = 0; i <= kSphRings; i++) {
    const float th = 3.14159265f * (float)i / (float)kSphRings;
    for (uint32_t j = 0; j <= kSphSegs; j++) {
      const float ph = 6.28318531f * (float)j / (float)kSphSegs;
      const Vec3 p{std::sin(th) * std::cos(ph), std::cos(th), std::sin(th) * std::sin(ph)};
      v[vi++] = {p, p, {(float)j / (float)kSphSegs, (float)i / (float)kSphRings}};
    }
  }
  uint32_t ii = 0;
  for (uint32_t i = 0; i < kSphRings; i++)
    for (uint32_t j = 0; j < kSphSegs; j++) {
      const uint32_t a = i * (kSphSegs + 1) + j, b = a + 1, c = b + kSphSegs + 1, d = a + kSphSegs + 1;
      idx[ii++] = a; idx[ii++] = b; idx[ii++] = c;
      idx[ii++] = a; idx[ii++] = c; idx[ii++] = d;
    }
  return ii;
}
// sRGB bayt -> DOGRUSAL. Hedef sRGB bicimli oldugu icin geri okunan bayt
// kodludur; enerji toplami dogrusal uzayda yapilmali (yoksa "enerji" olcumu
// aslinda kodlama egrisini olcer).
float px_linear(uint8_t b) {
  const float c = (float)b / 255.0f;
  return c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f);
}
// Dogrusal -> yazar sRGB: istenen DOGRUSAL albedoyu malzeme rengine cevirir
// (create_material girdiyi srgb_to_linear'dan geciriyor).
float to_srgb(float c) {
  return c <= 0.0031308f ? c * 12.92f : 1.055f * std::pow(c, 1.0f / 2.4f) - 0.055f;
}
struct PbrRig {
  renderer::Renderer ren;
  rhi::OffscreenTarget *off = nullptr;
  rhi::OffscreenConfig oc;
  renderer::MeshHandle sphere;
  renderer::MaterialHandle mat;
  bool ok = false;
};
// Kure + tek yonlu isik + siyah ortam: olculen her seyin kaynagi TEK terim.
bool pbr_rig_init(PbrRig &r, Device &dev, SystemArena &sys, uint32_t W, uint32_t H, renderer::NdfMode ndf) {
  r.oc = rhi::OffscreenConfig{};
  r.oc.srgb = true;
  r.oc.width = W; r.oc.height = H;
  r.oc.clear[0] = r.oc.clear[1] = r.oc.clear[2] = 0;
  rhi::OffscreenResult ores;
  r.off = rhi::offscreen_create(dev, sys, r.oc, &ores);
  if (!r.off) return false;
  renderer::RendererConfig rc;
  rc.frames_in_flight = 1;
  rc.shadow_size = 0;
  rc.pbr_ndf = ndf;
  if (!r.ren.init(dev, sys, rhi::offscreen_render_pass(r.off), rc)) return false;
  static renderer::Vertex sv[kSphVerts];
  static uint32_t si[kSphIdx];
  const uint32_t n = make_sphere(sv, si);
  r.sphere = r.ren.create_mesh(sv, kSphVerts, si, n);
  r.ren.set_render_size(W, H);
  r.ren.set_shadows_enabled(false);
  r.ok = r.sphere.valid();
  return r.ok;
}
void pbr_rig_free(PbrRig &r) {
  r.ren.shutdown();
  if (r.off) rhi::offscreen_destroy(r.off);
  r.off = nullptr;
}
// Tek kare cizer ve pikselleri kopyalar. Isik yonu kameraya dogru egik: parlama
// lobu diskin icinde kalir, siluet halkasi da gorunur.
bool pbr_frame(PbrRig &r, uint8_t *dst, uint32_t W, uint32_t H, float sun) {
  r.ren.set_light(normalize(Vec3{0.35f, 0.45f, 1.0f}), {0, 0, 0}, sun);
  r.ren.set_camera(Mat4::look_at({0, 0, 3.2f}, {0, 0, 0}, {0, 1, 0}),
                   Mat4::perspective(1.0f, (float)W / (float)H, 0.1f, 50.0f));
  r.ren.begin_frame(0);
  r.ren.clear_point_lights();
  r.ren.draw(r.sphere, r.mat, Mat4::identity(), {1, 1, 1});
  rhi::OffscreenResult ores;
  Rec rr{&r.ren};
  if (!rhi::offscreen_render_custom(r.off, r.oc, rec_main, &rr, &ores, rec_shadow)) return false;
  std::memcpy(dst, ores.pixels, (size_t)W * H * 4);
  return true;
}
// Karedeki TOPLAM dogrusal enerji (butun kanallar, butun pikseller).
double frame_energy(const uint8_t *px, uint32_t W, uint32_t H) {
  double e = 0;
  for (uint32_t i = 0; i < W * H; i++)
    for (int c = 0; c < 3; c++) e += (double)px_linear(px[i * 4 + c]);
  return e;
}
} // namespace

// PBR enerji kapisi: GGX DOGRU normalize edilmis mi?
// Kurulum: yaricap 1 kure, metallic 1 + albedo 1 (yani F0 = 1, Fresnel sabit)
// -> olculen sey SAF D*V. Ortam siyah, nokta isik yok, golge yok: karedeki
// butun enerji tek yonlu isigin spekuler lobundan gelir.
// IDDIA: puruzluluk lobu YAYAR, enerji URETMEZ. Dogru normalize edilmis GGX'te
// puruzluluk taranirken toplam dogrusal enerji dar bir bantta kalir.
// KONTROL: ayni tarama NdfMode::Unnormalized ile (a^2 payi dusurulmus) kosar —
// enerji puruzlulukle patlamali, yani kapinin esigini ACIKCA asmali. Kontrol
// gecerse kapi hicbir sey olcmuyor demektir.
ENGINE_TEST(renderer_pbr_conserves_energy_across_roughness) {
  if (!loader_ok()) { skip("Vulkan loader yok"); return; }
  static SystemArena sys;
  if (!sys.reserve(192u << 20, "pbr_energy")) { CHECK(false); return; }
  Device dev;
  DeviceConfig dc;
  if (!dev.init(sys, g_api, dc)) { skip("Vulkan cihazi yok"); return; }
  if (test::gpu_is_virtual(dev.caps().device_name)) { dev.shutdown(); skip("sanal GPU (CI macOS): piksel kapisi gercek cihazda"); return; }
  const uint32_t W = 256, H = 256;
  static const float rough[4] = {0.35f, 0.5f, 0.7f, 1.0f};
  static uint8_t px[W * H * 4];
  double e[2][4] = {};
  bool ran[2] = {};
  // Gunes olcegi: en dar lobun tepesi kirpilmasin (kirpma enerji SILER ve
  // olcumu asagi cekerdi). 0.035 -> r=0.35'te tepe ~0.6 dogrusal.
  const float sun = 0.035f;
  for (int mode = 0; mode < 2; mode++) {
    PbrRig r;
    if (!pbr_rig_init(r, dev, sys, W, H, mode == 0 ? renderer::NdfMode::Ggx : renderer::NdfMode::Unnormalized)) {
      CHECK(false); pbr_rig_free(r); break;
    }
    renderer::PbrParams pp;
    pp.metallic = 1.0f;      // dagilimli terim yok
    pp.roughness = rough[0];
    r.mat = r.ren.create_material(r.ren.default_texture(), {1, 1, 1}, pp); // albedo 1 -> F0 = 1
    bool all = r.mat.valid();
    for (int i = 0; i < 4 && all; i++) {
      pp.roughness = rough[i];
      all = r.ren.set_material_pbr(r.mat, pp) && pbr_frame(r, px, W, H, sun);
      if (all) e[mode][i] = frame_energy(px, W, H);
    }
    ran[mode] = all;
    pbr_rig_free(r);
  }
  dev.shutdown();
  CHECK(ran[0] && ran[1]);
  if (!(ran[0] && ran[1])) return;
  auto span = [](const double *v) {
    double lo = v[0], hi = v[0];
    for (int i = 1; i < 4; i++) { if (v[i] < lo) lo = v[i]; if (v[i] > hi) hi = v[i]; }
    return lo > 0 ? hi / lo : 1e9;
  };
  const double s_ok = span(e[0]), s_bad = span(e[1]);
  for (int mode = 0; mode < 2; mode++)
    std::printf("    [bilgi] %-28s enerji r=0.35 %.0f  r=0.50 %.0f  r=0.70 %.0f  r=1.00 %.0f  -> en buyuk/en kucuk %.2fx\n",
                mode == 0 ? "GGX (urun)" : "normalize YOK (kontrol)", e[mode][0], e[mode][1], e[mode][2], e[mode][3],
                span(e[mode]));
  // Esik: 8 bit sRGB hedefte nicemleme + tek lobun ornekleme hatasi zaten
  // birkac on yuzde getirir; 3.0 bunun uzerinde ama kontrolun cok altinda.
  bool energy_stable = s_ok < 3.0;
  CHECK(energy_stable);
  // Kontrol GERCEKTEN dusmeli: ayni esikte kalmamali ve urunden belirgin buyuk.
  bool control_breaks = s_bad >= 3.0 && s_bad > s_ok * 3.0;
  CHECK(control_breaks);
  std::printf("    [bilgi] kapi: urun %.2fx (< 3.0 gerekli), kontrol %.2fx (>= 3.0 ve urunun 3 katindan buyuk olmali)\n",
              s_ok, s_bad);
}

// PBR Fresnel kapisi: metal ile dielektrik AYRISIYOR mu?
// F0 = mix(0.16*reflectance^2, albedo, metallic). Kirmizi bir albedoda:
//   metal      -> parlama KIRMIZI (F0 = albedo)
//   dielektrik -> parlama BEYAZ   (F0 = %4 renksiz), kirmizi yalniz dagilimlida
// Olcum: en parlak 300 pikselin dogrusal kromasi (R-B)/(R+G+B).
// KONTROL: ayni olcum NdfMode::Unnormalized ile — lob 256 kat parlar, her sey
// beyaza DOYAR ve krom farki COKER. Yani kapi gercekten Fresnel'i olcuyor,
// "iki resim farkli" demiyor.
ENGINE_TEST(renderer_pbr_metal_dielectric_split_in_fresnel) {
  if (!loader_ok()) { skip("Vulkan loader yok"); return; }
  static SystemArena sys;
  if (!sys.reserve(192u << 20, "pbr_fresnel")) { CHECK(false); return; }
  Device dev;
  DeviceConfig dc;
  if (!dev.init(sys, g_api, dc)) { skip("Vulkan cihazi yok"); return; }
  if (test::gpu_is_virtual(dev.caps().device_name)) { dev.shutdown(); skip("sanal GPU (CI macOS): piksel kapisi gercek cihazda"); return; }
  const uint32_t W = 256, H = 256;
  static uint8_t px[W * H * 4];
  // Hedef DOGRUSAL albedo: guclu kirmizi. Malzeme rengi yazar sRGB'sinde verilir.
  const Vec3 lin{0.787f, 0.100f, 0.010f};
  const Vec3 col{to_srgb(lin.x), to_srgb(lin.y), to_srgb(lin.z)};
  const float sun = 0.018f; // metalin kirmizi tepesi (~50x) kirpilmasin
  // En parlak N pikselin dogrusal kromasi.
  auto chroma = [](const uint8_t *p, uint32_t n_px, uint32_t top) {
    static uint32_t ord[W * H];
    for (uint32_t i = 0; i < n_px; i++) ord[i] = i;
    for (uint32_t a = 0; a < top; a++) { // kismi secim: en parlak `top` yeter
      uint32_t best = a;
      for (uint32_t b = a + 1; b < n_px; b++) {
        const int sb = p[ord[b] * 4] + p[ord[b] * 4 + 1] + p[ord[b] * 4 + 2];
        const int sa = p[ord[best] * 4] + p[ord[best] * 4 + 1] + p[ord[best] * 4 + 2];
        if (sb > sa) best = b;
      }
      const uint32_t t = ord[a]; ord[a] = ord[best]; ord[best] = t;
    }
    double r = 0, g = 0, b = 0;
    for (uint32_t a = 0; a < top; a++) {
      r += px_linear(p[ord[a] * 4]); g += px_linear(p[ord[a] * 4 + 1]); b += px_linear(p[ord[a] * 4 + 2]);
    }
    const double s = r + g + b;
    return s > 1e-9 ? (r - b) / s : 0.0;
  };
  double ch[2][2] = {}; // [mode][0 = metal, 1 = dielektrik]
  bool ran[2] = {};
  for (int mode = 0; mode < 2; mode++) {
    PbrRig r;
    if (!pbr_rig_init(r, dev, sys, W, H, mode == 0 ? renderer::NdfMode::Ggx : renderer::NdfMode::Unnormalized)) {
      CHECK(false); pbr_rig_free(r); break;
    }
    bool all = true;
    for (int k = 0; k < 2 && all; k++) {
      renderer::PbrParams pp;
      pp.metallic = k == 0 ? 1.0f : 0.0f;
      pp.roughness = 0.25f;
      pp.reflectance = 0.5f; // dielektrik F0 = %4
      r.mat = r.ren.create_material(r.ren.default_texture(), col, pp);
      all = r.mat.valid() && pbr_frame(r, px, W, H, sun);
      if (all) ch[mode][k] = chroma(px, W * H, 300);
    }
    ran[mode] = all;
    pbr_rig_free(r);
  }
  dev.shutdown();
  CHECK(ran[0] && ran[1]);
  if (!(ran[0] && ran[1])) return;
  const double d_ok = ch[0][0] - ch[0][1], d_bad = ch[1][0] - ch[1][1];
  std::printf("    [bilgi] GGX (urun)            : parlama kromasi metal %.3f, dielektrik %.3f -> fark %.3f\n",
              ch[0][0], ch[0][1], d_ok);
  std::printf("    [bilgi] normalize YOK (kontrol): parlama kromasi metal %.3f, dielektrik %.3f -> fark %.3f\n",
              ch[1][0], ch[1][1], d_bad);
  bool fresnel_splits = d_ok > 0.30; // metalin parlamasi albedoyla renklenmis
  CHECK(fresnel_splits);
  bool control_collapses = d_bad < d_ok * 0.5; // doyma ayrimi siliyor
  CHECK(control_collapses);
}

// Geriye uyumluluk (A/B): PBR alanlari VERILMEMIS bir malzeme (metallic 0,
// roughness 1, reflectance 0.5) bugunku Lambert goruntusune ne kadar yakin?
// roughness 1'de GGX D = 1/PI'ye duser; geriye yalniz dielektrigin %4'luk
// Fresnel'i ve analitik ortam DFG terimi kalir. Bu kapi o FARKI OLCER (sifir
// oldugunu iddia etmez) ve olcumun kendisinin kor olmadigini gosterir.
// KONTROL: ayni A/B, PBR tarafi roughness 0.1 (parlak) ile — fark BUYUMELI.
ENGINE_TEST(renderer_pbr_default_material_stays_near_lambert) {
  if (!loader_ok()) { skip("Vulkan loader yok"); return; }
  static SystemArena sys;
  if (!sys.reserve(192u << 20, "pbr_ab")) { CHECK(false); return; }
  Device dev;
  DeviceConfig dc;
  if (!dev.init(sys, g_api, dc)) { skip("Vulkan cihazi yok"); return; }
  if (test::gpu_is_virtual(dev.caps().device_name)) { dev.shutdown(); skip("sanal GPU (CI macOS): piksel kapisi gercek cihazda"); return; }
  const uint32_t W = 256, H = 256;
  static uint8_t lam[W * H * 4], pbr_def[W * H * 4], pbr_shiny[W * H * 4];
  const Vec3 col{0.80f, 0.78f, 0.75f};
  // Gercekci sahne aydinlatmasi: gunes + gorunur ortam (GI sozlesmesi).
  const float sun = 0.9f;
  bool ok = true;
  {
    PbrRig r;
    ok = pbr_rig_init(r, dev, sys, W, H, renderer::NdfMode::Ggx);
    if (ok) {
      // Ortam SIFIR degil: ortam spekuleri (split-sum DFG) de A/B'ye girsin.
      auto shot = [&](uint8_t *dst) {
        r.ren.set_light(normalize(Vec3{0.35f, 0.45f, 1.0f}), {0.12f, 0.13f, 0.16f}, sun);
        r.ren.set_camera(Mat4::look_at({0, 0, 3.2f}, {0, 0, 0}, {0, 1, 0}),
                         Mat4::perspective(1.0f, (float)W / (float)H, 0.1f, 50.0f));
        r.ren.begin_frame(0);
        r.ren.clear_point_lights();
        r.ren.draw(r.sphere, r.mat, Mat4::identity(), {1, 1, 1});
        rhi::OffscreenResult ores;
        Rec rr{&r.ren};
        if (!rhi::offscreen_render_custom(r.off, r.oc, rec_main, &rr, &ores, rec_shadow)) return false;
        std::memcpy(dst, ores.pixels, (size_t)W * H * 4);
        return true;
      };
      r.mat = r.ren.create_material(r.ren.default_texture(), col);        // Lambert (bugunku yol)
      ok = r.mat.valid() && shot(lam);
      renderer::PbrParams pp;                                             // varsayilan: metallic 0, roughness 1
      if (ok) { r.mat = r.ren.create_material(r.ren.default_texture(), col, pp); ok = r.mat.valid() && shot(pbr_def); }
      pp.roughness = 0.1f;                                                // KONTROL: parlak
      if (ok) { r.mat = r.ren.create_material(r.ren.default_texture(), col, pp); ok = r.mat.valid() && shot(pbr_shiny); }
    }
    pbr_rig_free(r);
  }
  dev.shutdown();
  CHECK(ok);
  if (!ok) return;
  auto cmp = [&](const uint8_t *a, const uint8_t *b, uint32_t *diff_px, int *max_ch, double *mean) {
    uint32_t d = 0; int mx = 0; double sum = 0;
    for (uint32_t i = 0; i < W * H; i++) {
      int worst = 0;
      for (int c = 0; c < 3; c++) {
        const int q = std::abs((int)a[i * 4 + c] - (int)b[i * 4 + c]);
        if (q > worst) worst = q;
        sum += q;
      }
      if (worst) d++;
      if (worst > mx) mx = worst;
    }
    *diff_px = d; *max_ch = mx; *mean = sum / (double)(W * H * 3);
  };
  uint32_t d0 = 0, d1 = 0; int m0 = 0, m1 = 0; double a0 = 0, a1 = 0;
  cmp(lam, pbr_def, &d0, &m0, &a0);
  cmp(lam, pbr_shiny, &d1, &m1, &a1);
  std::printf("    [bilgi] A/B Lambert vs PBR(varsayilan m=0 r=1): %u piksel farkli (%.1f%%), en buyuk kanal %d, ortalama %.2f\n",
              d0, 100.0f * (float)d0 / (float)(W * H), m0, a0);
  std::printf("    [bilgi] KONTROL Lambert vs PBR(r=0.10 parlak):  %u piksel farkli (%.1f%%), en buyuk kanal %d, ortalama %.2f\n",
              d1, 100.0f * (float)d1 / (float)(W * H), m1, a1);
  bool close_to_lambert = m0 <= 24 && a0 < 2.0; // gorunur ama kucuk: %4 Fresnel + ortam DFG
  CHECK(close_to_lambert);
  bool measurement_sees_material = m1 > m0 * 2 && m1 > 40; // olcum kor degil (olculen 63)
  CHECK(measurement_sees_material);
}

// ===========================================================================
// Stokastik tile isiklandirma (PLAN EK A.1) — VARSAYILAN KAPALI
// ===========================================================================
// Olcum sahnesi: genis zemin + ustunde ORTUSEN 24 nokta isik. Ortam ve gunes
// SIFIR, golge kapali: karedeki her fotonun kaynagi nokta isiklardir, yani
// stokastik yolun hatasi baska bir terimin arkasina saklanamaz.
namespace {
constexpr uint32_t kStochLights = 24;
void stoch_place_lights(renderer::Renderer &ren) {
  ren.clear_point_lights();
  for (uint32_t i = 0; i < kStochLights; i++) {
    const float ang = 0.83f * (float)i;
    const float rad = 1.6f + 0.55f * (float)(i % 5);
    renderer::PointLight L;
    L.pos = {std::cos(ang) * rad, 0.8f + 0.4f * (float)(i % 3), std::sin(ang) * rad};
    L.radius = (i % 6) == 0 ? 6.0f : 3.5f;
    L.color = {0.45f + 0.55f * (float)((i * 7) % 5) / 4.0f, 0.45f + 0.55f * (float)((i * 3) % 4) / 3.0f,
               0.45f + 0.55f * (float)((i * 5) % 6) / 5.0f};
    // Siddet MERDIVENI: her 6 isiktan biri baskin, gerisi zayif. Gercek
    // kullanim boyle (bir tile'da 12 isik var, 2-3'u onemli); butun isiklarin
    // esit oldugu sahne stokastik yontemin en kotu halidir ve tipik degildir.
    L.intensity = 1.6f / (1.0f + 1.6f * (float)(i % 6));
    ren.add_point_light(L);
  }
}
struct StochRig {
  renderer::Renderer ren;
  rhi::OffscreenTarget *off = nullptr;
  rhi::OffscreenConfig oc;
  renderer::MeshHandle plane, cube;
};
bool stoch_rig_init(StochRig &r, Device &dev, SystemArena &sys, uint32_t W, uint32_t H, uint32_t budget, uint32_t keep,
                    uint32_t phases = 128, bool compensate = true) {
  r.oc = rhi::OffscreenConfig{};
  r.oc.srgb = true;
  r.oc.width = W; r.oc.height = H;
  r.oc.clear[0] = r.oc.clear[1] = r.oc.clear[2] = 0;
  rhi::OffscreenResult ores;
  r.off = rhi::offscreen_create(dev, sys, r.oc, &ores);
  if (!r.off) return false;
  renderer::RendererConfig rc;
  rc.frames_in_flight = 1;
  rc.shadow_size = 0;
  rc.stochastic_lights = budget;
  rc.stochastic_keep = keep;
  rc.stochastic_phases = phases;
  rc.stochastic_compensate = compensate;
  if (!r.ren.init(dev, sys, rhi::offscreen_render_pass(r.off), rc)) return false;
  renderer::Vertex v[24];
  uint32_t idx[36];
  uint32_t n = renderer::Renderer::plane(v, idx);
  r.plane = r.ren.create_mesh(v, 4, idx, n);
  n = renderer::Renderer::cube(v, idx);
  r.cube = r.ren.create_mesh(v, 24, idx, n);
  r.ren.set_render_size(W, H);
  r.ren.set_shadows_enabled(false);
  return r.plane.valid() && r.cube.valid();
}
void stoch_rig_free(StochRig &r) {
  r.ren.shutdown();
  if (r.off) rhi::offscreen_destroy(r.off);
  r.off = nullptr;
}
bool stoch_frame(StochRig &r, uint8_t *dst, uint32_t W, uint32_t H) {
  r.ren.set_light({0, 1, 0}, {0, 0, 0}, 0.0f); // yalniz nokta isiklar
  r.ren.set_camera(Mat4::look_at({0, 7.0f, 7.0f}, {0, 0, 0}, {0, 1, 0}),
                   Mat4::perspective(1.0f, (float)W / (float)H, 0.1f, 60.0f));
  stoch_place_lights(r.ren); // isiklar begin_frame'DEN ONCE: kume atamasi orada yapilir
  r.ren.begin_frame(0);
  r.ren.draw(r.plane, Mat4::scale({24, 1, 24}), {0.85f, 0.85f, 0.85f});
  r.ren.draw(r.cube, Mat4::translate({1.4f, 0.5f, -0.6f}), {0.9f, 0.9f, 0.9f});
  rhi::OffscreenResult ores;
  Rec rr{&r.ren};
  if (!rhi::offscreen_render_custom(r.off, r.oc, rec_main, &rr, &ores, rec_shadow)) return false;
  std::memcpy(dst, ores.pixels, (size_t)W * H * 4);
  return true;
}
double mean_linear(const uint8_t *px, uint32_t W, uint32_t H) {
  double s = 0;
  for (uint32_t i = 0; i < W * H; i++)
    for (int c = 0; c < 3; c++) s += (double)px_linear(px[i * 4 + c]);
  return s / (double)(W * H * 3);
}
} // namespace

// KAPALI YOL: stokastik yapilandirma BIT BIT eski goruntuyu vermeli.
// Iki iddia olculur:
//  (1) budget = 0  -> referans (yol tamamen elenir, ozellestirme sabiti 0)
//  (2) budget = 32 -> yol ACIK ama hicbir kumede butce asilmiyor; tahmin edici
//      ornek almaz, agirlik 1 olur. Sonuc referansla BAYT BAYT ayni cikmali —
//      yani "acik" olmanin kendisi goruntuye hicbir sey katmiyor.
// KONTROL: ayni karsilastirma butce 3 ile — fark SIFIRDAN BUYUK olmali, yoksa
// karsilastirma bayt dizisini degil kendi kendini olcuyordur.
ENGINE_TEST(renderer_stochastic_lighting_is_bit_identical_when_unused) {
  if (!loader_ok()) { skip("Vulkan loader yok"); return; }
  static SystemArena sys;
  if (!sys.reserve(256u << 20, "stoch_off")) { CHECK(false); return; }
  Device dev;
  DeviceConfig dc;
  if (!dev.init(sys, g_api, dc)) { skip("Vulkan cihazi yok"); return; }
  if (test::gpu_is_virtual(dev.caps().device_name)) { dev.shutdown(); skip("sanal GPU (CI macOS): piksel kapisi gercek cihazda"); return; }
  const uint32_t W = 192, H = 192;
  static uint8_t ref[W * H * 4], wide[W * H * 4], tight[W * H * 4];
  static const uint32_t budget[3] = {0, 32, 3};
  uint8_t *dst[3] = {ref, wide, tight};
  uint32_t ev[3] = {}, ev_full[3] = {};
  bool ok = true;
  for (int i = 0; i < 3 && ok; i++) {
    StochRig r;
    ok = stoch_rig_init(r, dev, sys, W, H, budget[i], 2) && stoch_frame(r, dst[i], W, H);
    if (ok) {
      ev[i] = r.ren.stats().clusters.lights_evaluated;
      ev_full[i] = r.ren.stats().clusters.lights_evaluated_full;
      CHECK(r.ren.stochastic_lighting() == (budget[i] > 0));
    }
    stoch_rig_free(r);
  }
  dev.shutdown();
  CHECK(ok);
  if (!ok) return;
  auto bytes_differ = [&](const uint8_t *a, const uint8_t *b) {
    uint32_t d = 0;
    for (uint32_t i = 0; i < W * H * 4; i++) if (a[i] != b[i]) d++;
    return d;
  };
  const uint32_t d_wide = bytes_differ(ref, wide), d_tight = bytes_differ(ref, tight);
  std::printf("    [bilgi] kapali(butce 0) isik-kume degerlendirmesi %u; butce 32 -> %u (tam %u); butce 3 -> %u (tam %u)\n",
              ev[0], ev[1], ev_full[1], ev[2], ev_full[2]);
  std::printf("    [bilgi] referansa gore farkli BAYT: butce 32 -> %u (0 bekleniyor), KONTROL butce 3 -> %u (> 0 bekleniyor)\n",
              d_wide, d_tight);
  bool identical_when_unused = d_wide == 0 && ev[1] == ev[0];
  CHECK(identical_when_unused);
  bool comparison_can_see_a_difference = d_tight > 0; // kontrol: karsilastirma kor degil
  CHECK(comparison_can_see_a_difference);
}

// ACIK YOL: degerlendirilen isik sayisi belirgin dusmeli, parlaklik ise
// referansa yakin kalmali (telafi agirligi enerjiyi geri veriyor mu?).
// KONTROL: ornek sayisi 1'e indirilince (butce 1, capa 0 -> tek isik, ham
// telafi) hata BUYUMELI. Buyumezse kapi hicbir sey olcmuyor demektir.
ENGINE_TEST(renderer_stochastic_lighting_keeps_brightness_with_fewer_lights) {
  if (!loader_ok()) { skip("Vulkan loader yok"); return; }
  static SystemArena sys;
  if (!sys.reserve(256u << 20, "stoch_on")) { CHECK(false); return; }
  Device dev;
  DeviceConfig dc;
  if (!dev.init(sys, g_api, dc)) { skip("Vulkan cihazi yok"); return; }
  if (test::gpu_is_virtual(dev.caps().device_name)) { dev.shutdown(); skip("sanal GPU (CI macOS): piksel kapisi gercek cihazda"); return; }
  const uint32_t W = 192, H = 192;
  const int kFrames = 24; // tahmin edici ZAMANSAL ortalamada yansiz: tek kare gurultulu bir ornektir
  static uint8_t img[W * H * 4];
  static uint8_t ref0[W * H * 4];
  // [0] referans (tam degerlendirme), [1] urun, [2] KONTROL: ornekle ama TELAFI ETME
  static const uint32_t budget[3] = {0, 4, 4};
  static const bool comp[3] = {true, true, false};
  double mean[3] = {}, lo[3] = {}, hi[3] = {};
  uint32_t ev[3] = {}, evf[3] = {};
  bool ok = true;
  for (int i = 0; i < 3 && ok; i++) {
    StochRig r;
    ok = stoch_rig_init(r, dev, sys, W, H, budget[i], 2, 128, comp[i]);
    double acc = 0;
    lo[i] = 1e9; hi[i] = 0;
    for (int f = 0; f < kFrames && ok; f++) {
      ok = stoch_frame(r, img, W, H);
      if (!ok) break;
      const double m = mean_linear(img, W, H);
      acc += m;
      if (m < lo[i]) lo[i] = m;
      if (m > hi[i]) hi[i] = m;
      if (i == 0 && f == 0) std::memcpy(ref0, img, sizeof ref0);
    }
    if (ok) {
      mean[i] = acc / (double)kFrames;
      ev[i] = r.ren.stats().clusters.lights_evaluated;
      evf[i] = r.ren.stats().clusters.lights_evaluated_full;
    }
    stoch_rig_free(r);
  }
  dev.shutdown();
  CHECK(ok);
  if (!ok) return;
  // Olcum gecerlilik kontrolu: referans DOYMUS olmamali. Kirpma enerjiyi siler
  // ve "sapma" sayisi aslinda kirpmayi olcmeye baslar (bu depoda tekrarlayan
  // bir tuzak sinifi: olcum kendi kurulumunu olcer).
  uint32_t sat = 0;
  for (uint32_t i = 0; i < W * H; i++)
    if (ref0[i * 4] >= 254 && ref0[i * 4 + 1] >= 254 && ref0[i * 4 + 2] >= 254) sat++;
  std::printf("    [bilgi] referansta doymus piksel: %u (%%%.2f) — olcum ancak dusukken gecerli\n", sat,
              100.0f * (float)sat / (float)(W * H));
  bool not_saturated = sat * 100 < W * H; // %1'den az
  CHECK(not_saturated);
  const double err_ok = std::abs(mean[1] - mean[0]) / mean[0];
  const double err_bad = std::abs(mean[2] - mean[0]) / mean[0];
  const double load = (double)ev[1] / (double)(evf[1] ? evf[1] : 1u);
  std::printf("    [bilgi] referans        : %d karenin ortalamasi %.5f (kare araligi %.5f..%.5f), %u degerlendirme\n",
              kFrames, mean[0], lo[0], hi[0], ev[0]);
  std::printf("    [bilgi] urun (butce 4)  : ortalama %.5f (aralik %.5f..%.5f), sapma %%%.2f, %u/%u degerlendirme (yuk %%%.1f)\n",
              mean[1], lo[1], hi[1], 100.0 * err_ok, ev[1], evf[1], 100.0 * load);
  std::printf("    [bilgi] KONTROL (telafi YOK): ortalama %.5f (aralik %.5f..%.5f), sapma %%%.2f\n", mean[2], lo[2],
              hi[2], 100.0 * err_bad);
  bool brightness_kept = err_ok < 0.16;
  CHECK(brightness_kept);
  bool work_dropped = load < 0.60; // degerlendirme yuku belirgin dusmeli
  CHECK(work_dropped);
  // KONTROL: telafi kapaliyken ayni secim kareyi belirgin KARARTMALI. Aksi
  // halde "parlaklik korundu" cumlesi telafiyi degil, sahnenin sansini olcer.
  bool control_is_worse = err_bad > err_ok * 2.5 && err_bad > 0.30;
  CHECK(control_is_worse);
}

// ZAMANSAL KARARLILIK: duran bir sahnede kare kare titreme.
// Mekanizma (cluster.hpp): capa (en onemli `keep` isik her kare) + katmanli
// donme (kuyruk esit araliklarla, desen kare basina 1 kayar) + secimin TILE
// basina olmasi. Olcum: ardisik kareler arasi ortalama mutlak piksel degisimi.
//   referans (kapali)      -> TAM SIFIR olmali (duran sahne, deterministik yol)
//   urun (butce 4, capa 2) -> kucuk
//   KONTROL (butce 1, capa 0, capa YOK) -> belirgin BUYUK
// Kontrol capasiz kosar: "titreme kucuk" iddiasinin capadan geldigini gosterir.
ENGINE_TEST(renderer_stochastic_lighting_is_temporally_stable) {
  if (!loader_ok()) { skip("Vulkan loader yok"); return; }
  static SystemArena sys;
  if (!sys.reserve(256u << 20, "stoch_temporal")) { CHECK(false); return; }
  Device dev;
  DeviceConfig dc;
  if (!dev.init(sys, g_api, dc)) { skip("Vulkan cihazi yok"); return; }
  if (test::gpu_is_virtual(dev.caps().device_name)) { dev.shutdown(); skip("sanal GPU (CI macOS): piksel kapisi gercek cihazda"); return; }
  const uint32_t W = 192, H = 192;
  const int kFrames = 8;
  static uint8_t a[W * H * 4], b[W * H * 4];
  static const uint32_t budget[3] = {0, 4, 4};
  static const uint32_t keep[3] = {2, 2, 0};
  static const uint32_t phases[3] = {128, 128, 2}; // KONTROL: kaba donme (desen her karede yarim dilim atlar)
  double flick_mean[3] = {};
  int flick_max[3] = {};
  uint32_t big[3] = {};
  float wmax_[3] = {};
  bool ok = true;
  for (int i = 0; i < 3 && ok; i++) {
    StochRig r;
    ok = stoch_rig_init(r, dev, sys, W, H, budget[i], keep[i], phases[i]);
    double acc = 0;
    int mx = 0;
    uint32_t nbig = 0;
    for (int f = 0; f < kFrames && ok; f++) {
      ok = stoch_frame(r, f % 2 ? b : a, W, H);
      if (!ok || f == 0) continue;
      const uint8_t *p = f % 2 ? b : a, *q = f % 2 ? a : b;
      for (uint32_t k = 0; k < W * H * 4; k++) {
        if ((k & 3u) == 3u) continue; // alfa
        const int d = std::abs((int)p[k] - (int)q[k]);
        acc += d;
        if (d > 16) nbig++;
        if (d > mx) mx = d;
      }
    }
    flick_mean[i] = acc / (double)(W * H * 3 * (kFrames - 1));
    flick_max[i] = mx;
    big[i] = nbig;
    wmax_[i] = r.ren.stats().clusters.max_weight;
    stoch_rig_free(r);
  }
  dev.shutdown();
  CHECK(ok);
  if (!ok) return;
  static const char *nm[3] = {"kapali (referans)", "urun (128 faz, capa 2)", "KONTROL (2 faz, CAPA YOK)"};
  for (int i = 0; i < 3; i++)
    std::printf("    [bilgi] %-28s titreme: ortalama %.3f bayt/kanal, en buyuk %d, >16 degisen %u (%%%.2f), en buyuk agirlik %.1f\n",
                nm[i], flick_mean[i], flick_max[i], big[i], 100.0f * (float)big[i] / (float)(W * H * 3 * (kFrames - 1)),
                (double)wmax_[i]);
  bool reference_is_frozen = flick_mean[0] == 0.0 && flick_max[0] == 0;
  CHECK(reference_is_frozen); // referans titriyorsa olcum baska bir seyi olcuyor
  // Olcut DAYANIKLI: ortalama + "gozle gorulur degisen kanal orani". En buyuk
  // degisim 110 bin pikselin 7 gecisindeki TEK uc degerdir; ona esik koymak
  // kapiyi gurultuye baglar (bu depoda tekrarlayan tuzak) — bilgi olarak basilir.
  bool stable = flick_mean[1] < 5.0 && (double)big[1] / (double)(W * H * 3 * (kFrames - 1)) < 0.08;
  CHECK(stable);
  bool control_flickers_far_more = flick_mean[2] > flick_mean[1] * 4.0 && big[2] > big[1] * 4;
  CHECK(control_flickers_far_more); // kaba donme + capasiz kontrol kapiyi DUSURMELI
}

// ===========================================================================
// PBR DOKULARI (set 1, binding 2/3/4) — ORM / normal haritasi / isima
// ===========================================================================
namespace {
// Tekduze RGBA dokusu (mip yok: olcum dokunun kendi degerini gormeli, mip
// suzmesinin ortalamasini degil).
renderer::TextureHandle solid_tex(renderer::Renderer &ren, uint8_t r, uint8_t g, uint8_t b, bool srgb) {
  static uint8_t px[8 * 8 * 4];
  for (uint32_t i = 0; i < 8 * 8; i++) { px[i * 4] = r; px[i * 4 + 1] = g; px[i * 4 + 2] = b; px[i * 4 + 3] = 255; }
  return ren.create_texture(px, 8, 8, false, srgb);
}
// Iki kare arasinda: kac piksel farkli, en buyuk kanal farki, kanal basina ortalama.
void frame_diff(const uint8_t *a, const uint8_t *b, uint32_t n, uint32_t *diff_px, int *max_ch, double *mean) {
  uint32_t d = 0; int mx = 0; double sum = 0;
  for (uint32_t i = 0; i < n; i++) {
    int worst = 0;
    for (int c = 0; c < 3; c++) {
      const int q = std::abs((int)a[i * 4 + c] - (int)b[i * 4 + c]);
      if (q > worst) worst = q;
      sum += q;
    }
    if (worst) d++;
    if (worst > mx) mx = worst;
  }
  *diff_px = d; *max_ch = mx; *mean = sum / (double)(n * 3);
}
} // namespace

// PBR DOKU KAPILARININ PAYLASTIGI CIHAZ (Tuzaklar 8al): her vkCreateInstance
// NVIDIA ICD'sini dlopen'liyor ve glibc'nin "static TLS surplus" alani geri
// verilmiyor; belli sayidan sonra loader "Found no drivers!" der ve SONRADAN
// kosan BASKA kapilar sessizce "Vulkan cihazi yok" diye atlanir. Olculdu: bu
// dosyaya 5 yeni GPU kapisi eklemek 25 kapiyi ATLANDI'ya dusurdu (takim yine
// yesil gorunuyordu — "gecen sayi ayni, ATLANDI artti" belirtisi). Bu yuzden
// bes kapi TEK instance/cihaz paylasir ve o cihaz surec boyunca YASAR.
//
// AYRI VkApi TABLOSU (Tuzaklar 8an): VkApi cihaz seviyesi giris noktalarini
// tutar ve her Device::init onlari YENIDEN YAZAR. Bu dosyadaki oteki kapilar
// g_api ile kendi cihazlarini acip kapatiyor; paylasilan cihaz ayni tabloyu
// kullansaydi ilk yabanci init'ten sonra isaretcileri cop olurdu.
namespace {
VkApi g_api_pbr;
struct PbrOrtakGpu {
  SystemArena sys;
  Device dev;
  bool denendi = false;
  bool ok = false;
};
PbrOrtakGpu &pbr_ortak_gpu() {
  static PbrOrtakGpu g;
  if (!g.denendi) {
    g.denendi = true;
    if (!vk_api_load(g_api_pbr)) return g;
    if (!g.sys.reserve(768u << 20, "pbr_ortak")) return g;
    DeviceConfig dc;
    g.ok = g.dev.init(g.sys, g_api_pbr, dc);
  }
  return g;
}
} // namespace

// --- ORM KANAL ESLEMESI -----------------------------------------------------
// glTF 2.0: metallicRoughness dokusunda G = ROUGHNESS, B = METALLIC ve degerler
// CARPANLARLA CARPILIR. cgltf bunu SOYLEMEZ (saf ayristirici) — yani esleme
// "ezberden" yazilan tam olarak o sinifta bir sey. Burada REFERANS zaten
// kapisi olan CARPAN yoludur: metallic=1, roughness=0.2, doku yok.
//   URUN  : carpanlar 1/1 + ORM dokusu (G=51 -> 0.2, B=255 -> 1.0)  -> referansa ESIT olmali
//   KONTROL: ayni doku, G ve B TAKAS edilmis (G=255, B=51)          -> kapiyi DUSURMELI
// Kontrol gecerse kapi kanal sirasini hic olcmuyor demektir.
ENGINE_TEST(renderer_orm_texture_channels_match_factors) {
  PbrOrtakGpu &g = pbr_ortak_gpu();
  if (!g.ok) { skip("Vulkan cihazi yok (paylasilan PBR cihazi)"); return; }
  Device &dev = g.dev;
  static SystemArena sys;
  if (!sys.reserve(192u << 20, "orm_channels")) { CHECK(false); return; }
  if (test::gpu_is_virtual(dev.caps().device_name)) { skip("sanal GPU (CI macOS): piksel kapisi gercek cihazda"); return; }
  const uint32_t W = 256, H = 256;
  static uint8_t ref[W * H * 4], urun[W * H * 4], kontrol[W * H * 4];
  bool ok = true;
  {
    PbrRig r;
    ok = pbr_rig_init(r, dev, sys, W, H, renderer::NdfMode::Ggx);
    if (ok) {
      const Vec3 col{0.80f, 0.78f, 0.75f};
      renderer::TextureHandle white = r.ren.default_texture();
      // ORM dokulari DOGRUSAL (UNORM) yuklenir: 51/255 = 0.2 tam olarak.
      renderer::TextureHandle orm = solid_tex(r.ren, 153, 51, 255, false);  // R occ, G rough 0.2, B metal 1
      renderer::TextureHandle orm_swap = solid_tex(r.ren, 153, 255, 51, false); // KONTROL: G/B takas
      renderer::PbrParams ref_p;
      ref_p.metallic = 1.0f;
      ref_p.roughness = 0.2f; // REFERANS: carpan yolu (kendi kapisi olan yol)
      r.mat = r.ren.create_material(white, col, ref_p);
      ok = r.mat.valid() && pbr_frame(r, ref, W, H, 0.9f);
      renderer::PbrParams tex_p; // carpanlar 1/1: doku degeri dogrudan gecsin
      tex_p.metallic = 1.0f;
      tex_p.roughness = 1.0f;
      renderer::PbrTextures t;
      t.orm = orm;
      if (ok) { r.mat = r.ren.create_material(white, col, tex_p, t); ok = r.mat.valid() && pbr_frame(r, urun, W, H, 0.9f); }
      t.orm = orm_swap;
      if (ok) { r.mat = r.ren.create_material(white, col, tex_p, t); ok = r.mat.valid() && pbr_frame(r, kontrol, W, H, 0.9f); }
      CHECK(orm.valid() && orm_swap.valid());
    }
    pbr_rig_free(r);
  }
  // dev.shutdown() YOK: cihaz PAYLASILIYOR (Tuzaklar 8al) ve surec boyunca yasar.
  CHECK(ok);
  if (!ok) return;
  uint32_t d0 = 0, d1 = 0; int m0 = 0, m1 = 0; double a0 = 0, a1 = 0;
  frame_diff(ref, urun, W * H, &d0, &m0, &a0);
  frame_diff(ref, kontrol, W * H, &d1, &m1, &a1);
  std::printf("    [bilgi] URUN  ORM dokusu (G=51 rough, B=255 metal) vs carpan referansi: %u piksel (%.2f%%), en buyuk kanal %d, ortalama %.3f\n",
              d0, 100.0f * (float)d0 / (float)(W * H), m0, a0);
  std::printf("    [bilgi] KONTROL G/B TAKAS (G=255, B=51)            vs ayni referans:    %u piksel (%.2f%%), en buyuk kanal %d, ortalama %.3f\n",
              d1, 100.0f * (float)d1 / (float)(W * H), m1, a1);
  bool mapping_is_g_rough_b_metal = m0 <= 2 && a0 < 0.05; // UNORM8 yuvarlamasi disinda ayni
  CHECK(mapping_is_g_rough_b_metal);
  bool control_breaks_it = m1 > 40 && a1 > a0 * 20.0; // takas kapiyi acikca dusurmeli
  CHECK(control_breaks_it);
}

// --- NORMAL HARITASI (turevden teget uzayi) ---------------------------------
// Kurulum: kure + tek yonlu isik. Olculen sey aydinlatmanin normal haritasiyla
// DEGISMESI. Uc kare:
//   A = normal haritasi YOK                       (referans)
//   B = DUZ normal haritasi (128,128,255)         -> A ile AYNI olmali (KONTROL)
//   C = +X'e egik harita (204,128,229), olcek 1   -> A'dan ACIKCA farkli olmali
//   D = ayni egik harita, olcek 0                 -> A'ya geri DONMELI (KONTROL)
// B gecmezse "normal haritasi yolu dokusuz malzemeyi bozuyor" demektir; C
// gecmezse yol hic calismiyordur; D gecmezse normalTexture.scale akmiyordur.
ENGINE_TEST(renderer_normal_map_tilts_lighting) {
  PbrOrtakGpu &g = pbr_ortak_gpu();
  if (!g.ok) { skip("Vulkan cihazi yok (paylasilan PBR cihazi)"); return; }
  Device &dev = g.dev;
  static SystemArena sys;
  if (!sys.reserve(192u << 20, "normal_map")) { CHECK(false); return; }
  if (test::gpu_is_virtual(dev.caps().device_name)) { skip("sanal GPU (CI macOS): piksel kapisi gercek cihazda"); return; }
  const uint32_t W = 256, H = 256;
  static uint8_t pa[W * H * 4], pb[W * H * 4], pc[W * H * 4], pd[W * H * 4];
  bool ok = true;
  {
    PbrRig r;
    ok = pbr_rig_init(r, dev, sys, W, H, renderer::NdfMode::Ggx);
    if (ok) {
      const Vec3 col{0.80f, 0.78f, 0.75f};
      renderer::TextureHandle white = r.ren.default_texture();
      // Normal haritalari DOGRUSAL (UNORM): texel oldugu gibi gelir, 2x-1 ile acilir.
      renderer::TextureHandle flat = solid_tex(r.ren, 128, 128, 255, false);
      renderer::TextureHandle tilt = solid_tex(r.ren, 204, 128, 229, false); // nx=+0.6, nz=0.8
      renderer::PbrParams pp;
      pp.metallic = 0.0f;
      pp.roughness = 0.45f; // yumusak parlama: egilme siluette degil, tonlamada gorunur
      r.mat = r.ren.create_material(white, col, pp);
      ok = r.mat.valid() && pbr_frame(r, pa, W, H, 0.9f);
      renderer::PbrTextures t;
      t.normal = flat;
      t.normal_scale = 1.0f;
      if (ok) { r.mat = r.ren.create_material(white, col, pp, t); ok = r.mat.valid() && pbr_frame(r, pb, W, H, 0.9f); }
      t.normal = tilt;
      if (ok) { r.mat = r.ren.create_material(white, col, pp, t); ok = r.mat.valid() && pbr_frame(r, pc, W, H, 0.9f); }
      t.normal_scale = 0.0f; // KONTROL: olcek 0 -> xy sifirlanir, geometrik normale doner
      if (ok) { r.mat = r.ren.create_material(white, col, pp, t); ok = r.mat.valid() && pbr_frame(r, pd, W, H, 0.9f); }
      CHECK(flat.valid() && tilt.valid());
    }
    pbr_rig_free(r);
  }
  // dev.shutdown() YOK: cihaz PAYLASILIYOR (Tuzaklar 8al) ve surec boyunca yasar.
  CHECK(ok);
  if (!ok) return;
  uint32_t db = 0, dc_ = 0, dd = 0; int mb = 0, mc = 0, md = 0; double ab = 0, ac = 0, ad = 0;
  frame_diff(pa, pb, W * H, &db, &mb, &ab);
  frame_diff(pa, pc, W * H, &dc_, &mc, &ac);
  frame_diff(pa, pd, W * H, &dd, &md, &ad);
  std::printf("    [bilgi] KONTROL duz harita (128,128,255): %u piksel (%.2f%%), en buyuk kanal %d, ortalama %.3f\n",
              db, 100.0f * (float)db / (float)(W * H), mb, ab);
  std::printf("    [bilgi] URUN    egik harita (204,128,229): %u piksel (%.2f%%), en buyuk kanal %d, ortalama %.3f\n",
              dc_, 100.0f * (float)dc_ / (float)(W * H), mc, ac);
  std::printf("    [bilgi] KONTROL egik harita + olcek 0:     %u piksel (%.2f%%), en buyuk kanal %d, ortalama %.3f\n",
              dd, 100.0f * (float)dd / (float)(W * H), md, ad);
  // Duz haritanin artigi SIFIR DEGIL, cunku (128,128,255) UNORM8'de tam duz
  // degil: 128/255 = 0.501961 -> xy = +0.003922, yani normal 0.225 derece egik.
  // Bu DOKUNUN KODLAMA hatasidir, teget cercevesinin degil — KANIT ucuncu
  // olcumdedir: olcek 0'da doku katkisi TAMAMEN kalkar, yani kare referansa
  // duz haritadan bile DAHA YAKIN olmali.
  //
  // ⚠️ BU OLCUT BIR KEZ COK SERT YAZILDI (2026-09-16'da duzeltildi): eskiden
  // `md == 0 && ad == 0.0`, yani BIT BIT esitlik isteniyordu. O, bizim
  // renderer'imizin degil SURUCU DERLEYICISININ bir ozelligini sinaviyordu:
  // referans kare "normal dokusu YOK" dalindan, olcek 0 karesi "normal dokusu
  // VAR, olcek 0" dalindan geliyor. Iki dal aritmetik olarak ayni sonucu
  // verir ama METIN olarak farklidir; NVIDIA ikisini ayni koda indirgiyor,
  // lavapipe (CI Linux) indirgemiyor ve kapi orada kirmizi donuyordu.
  // Surucunun optimize edicisi bizim sozlesmemiz degil.
  //
  // Yerine gecen olcut ayni seyi ayirt ediyor ve surucuden BAGIMSIZ:
  //   * teget cercevesi bozuk olsaydi olcek 0 referansa donmezdi (ad > ab),
  //   * olcek hic okunmasaydi olcek 0 karesi EGIK kare gibi olurdu (ad ~ ac).
  // Bit esitlik yine RAPORLANIYOR (bazi surucularde saglaniyor), ama hukum
  // degil: sayi var, iddia yok.
  const double kFlatTiltDeg = 0.225; // atan(0.003922)
  std::printf("    [bilgi] duz haritanin kodlama egimi %.3f derece; olcek 0 artigi %u piksel "
              "(ortalama %.4f), urun/duz orani %.1f kat; bit bit esit: %s\n",
              kFlatTiltDeg, dd, ad, ab > 0 ? ac / ab : 999.0, (md == 0 && ad == 0.0) ? "EVET" : "hayir");
  bool flat_map_is_a_noop = mb <= 16 && ab < 0.5 && ac > ab * 20.0;
  CHECK(flat_map_is_a_noop);
  bool tilt_changes_lighting = mc > 24 && ac > 3.0;
  CHECK(tilt_changes_lighting);
  // Olcek 0: doku katkisi yok -> artik duz haritanınkinden BUYUK OLAMAZ ve
  // urun sinyalinden en az 20 kat kucuk olmali (oran olcutu gurultuye bagli
  // degil, tipki ustteki duz-harita olcutu gibi).
  bool scale_zero_returns_to_geometric = ad <= ab && md <= mb && ac > ad * 20.0;
  CHECK(scale_zero_returns_to_geometric);
}

// --- ISIMA DOKUSU -----------------------------------------------------------
// glTF: emissiveTexture, emissiveFactor ile CARPILIR. Siyah ortam + sifir gunes:
// karedeki her foton isimadandir, yani olculen sey yalnizca bu terim.
// KONTROL: beyaz isima dokusu (255,255,255) yalniz carpan yoluyla AYNI olmali;
// siyah doku ise kareyi SONDURMELI.
ENGINE_TEST(renderer_emissive_texture_multiplies_factor) {
  PbrOrtakGpu &g = pbr_ortak_gpu();
  if (!g.ok) { skip("Vulkan cihazi yok (paylasilan PBR cihazi)"); return; }
  Device &dev = g.dev;
  static SystemArena sys;
  if (!sys.reserve(192u << 20, "emissive_tex")) { CHECK(false); return; }
  if (test::gpu_is_virtual(dev.caps().device_name)) { skip("sanal GPU (CI macOS): piksel kapisi gercek cihazda"); return; }
  const uint32_t W = 192, H = 192;
  static uint8_t ref[W * H * 4], beyaz[W * H * 4], siyah[W * H * 4];
  bool ok = true;
  {
    PbrRig r;
    ok = pbr_rig_init(r, dev, sys, W, H, renderer::NdfMode::Ggx);
    if (ok) {
      renderer::TextureHandle white = r.ren.default_texture();
      renderer::TextureHandle e_white = solid_tex(r.ren, 255, 255, 255, true); // isima dokusu RENKTIR: sRGB
      renderer::TextureHandle e_black = solid_tex(r.ren, 0, 0, 0, true);
      renderer::PbrParams pp;
      pp.emissive = {0.6f, 0.3f, 0.15f};
      auto shot = [&](uint8_t *dst) {
        r.ren.set_light(normalize(Vec3{0.35f, 0.45f, 1.0f}), {0, 0, 0}, 0.0f); // yalniz isima
        r.ren.set_camera(Mat4::look_at({0, 0, 3.2f}, {0, 0, 0}, {0, 1, 0}),
                         Mat4::perspective(1.0f, (float)W / (float)H, 0.1f, 50.0f));
        r.ren.begin_frame(0);
        r.ren.clear_point_lights();
        r.ren.draw(r.sphere, r.mat, Mat4::identity(), {1, 1, 1});
        rhi::OffscreenResult ores;
        Rec rr{&r.ren};
        if (!rhi::offscreen_render_custom(r.off, r.oc, rec_main, &rr, &ores, rec_shadow)) return false;
        std::memcpy(dst, ores.pixels, (size_t)W * H * 4);
        return true;
      };
      r.mat = r.ren.create_material(white, {1, 1, 1}, pp);
      ok = r.mat.valid() && shot(ref);
      renderer::PbrTextures t;
      t.emissive = e_white;
      if (ok) { r.mat = r.ren.create_material(white, {1, 1, 1}, pp, t); ok = r.mat.valid() && shot(beyaz); }
      t.emissive = e_black;
      if (ok) { r.mat = r.ren.create_material(white, {1, 1, 1}, pp, t); ok = r.mat.valid() && shot(siyah); }
      CHECK(e_white.valid() && e_black.valid());
    }
    pbr_rig_free(r);
  }
  // dev.shutdown() YOK: cihaz PAYLASILIYOR (Tuzaklar 8al) ve surec boyunca yasar.
  CHECK(ok);
  if (!ok) return;
  uint32_t d0 = 0, d1 = 0; int m0 = 0, m1 = 0; double a0 = 0, a1 = 0;
  frame_diff(ref, beyaz, W * H, &d0, &m0, &a0);
  frame_diff(ref, siyah, W * H, &d1, &m1, &a1);
  // Referansin kendisi SIYAH OLMAMALI: yoksa asagidaki "siyah doku sonduruyor"
  // olcumu hicbir sey olcmez (0'dan 0'a fark sifirdir).
  double ref_mean = 0;
  for (uint32_t i = 0; i < W * H; i++) for (int c = 0; c < 3; c++) ref_mean += ref[i * 4 + c];
  ref_mean /= (double)(W * H * 3);
  std::printf("    [bilgi] referans (yalniz isima carpani) ortalama parlaklik %.2f\n", ref_mean);
  std::printf("    [bilgi] KONTROL beyaz isima dokusu: %u piksel (%.2f%%), en buyuk kanal %d, ortalama %.3f\n",
              d0, 100.0f * (float)d0 / (float)(W * H), m0, a0);
  std::printf("    [bilgi] URUN    siyah isima dokusu: %u piksel (%.2f%%), en buyuk kanal %d, ortalama %.3f\n",
              d1, 100.0f * (float)d1 / (float)(W * H), m1, a1);
  CHECK(ref_mean > 5.0);
  bool white_texture_is_identity = m0 <= 2 && a0 < 0.05;
  CHECK(white_texture_is_identity);
  bool black_texture_extinguishes = a1 > ref_mean * 0.5; // isima gercekten carpiliyor
  CHECK(black_texture_extinguishes);
}

// --- RENK UZAYI, GPU TARAFI -------------------------------------------------
// ORM dokusu DOGRUSAL (UNORM) yuklenmeli. sRGB yuklenirse donanim texel'i
// cozer: 128/255 = 0.502 yerine shader 0.216 gorur — yani puruzluluk sessizce
// yer degistirir ve GORUNTU "biraz yanlis" olur, hicbir sey kizarmaz.
// URUN : ORM dogrusal yuklenir -> carpan referansiyla (roughness 0.502) ESIT
// KONTROL: AYNI baytlar sRGB yuklenir -> kapi DUSMELI
ENGINE_TEST(renderer_texture_colorspace_shifts_roughness) {
  PbrOrtakGpu &g = pbr_ortak_gpu();
  if (!g.ok) { skip("Vulkan cihazi yok (paylasilan PBR cihazi)"); return; }
  Device &dev = g.dev;
  static SystemArena sys;
  if (!sys.reserve(192u << 20, "tex_colorspace")) { CHECK(false); return; }
  if (test::gpu_is_virtual(dev.caps().device_name)) { skip("sanal GPU (CI macOS): piksel kapisi gercek cihazda"); return; }
  const uint32_t W = 256, H = 256;
  static uint8_t ref[W * H * 4], lin[W * H * 4], srgb_[W * H * 4];
  const float kTexel = 128.0f / 255.0f; // dogrusal okumada shader'in gordugu sayi
  bool ok = true;
  {
    PbrRig r;
    ok = pbr_rig_init(r, dev, sys, W, H, renderer::NdfMode::Ggx);
    if (ok) {
      const Vec3 col{0.80f, 0.78f, 0.75f};
      renderer::TextureHandle white = r.ren.default_texture();
      renderer::TextureHandle orm_lin = solid_tex(r.ren, 255, 128, 255, false);  // URUN: dogrusal
      renderer::TextureHandle orm_srgb = solid_tex(r.ren, 255, 128, 255, true);  // KONTROL: ayni baytlar, sRGB
      renderer::PbrParams ref_p;
      ref_p.metallic = 1.0f;
      ref_p.roughness = kTexel; // REFERANS: dokunun dogrusal degeri, carpan yolundan
      r.mat = r.ren.create_material(white, col, ref_p);
      ok = r.mat.valid() && pbr_frame(r, ref, W, H, 0.9f);
      renderer::PbrParams tex_p;
      tex_p.metallic = 1.0f;
      tex_p.roughness = 1.0f;
      renderer::PbrTextures t;
      t.orm = orm_lin;
      if (ok) { r.mat = r.ren.create_material(white, col, tex_p, t); ok = r.mat.valid() && pbr_frame(r, lin, W, H, 0.9f); }
      t.orm = orm_srgb;
      if (ok) { r.mat = r.ren.create_material(white, col, tex_p, t); ok = r.mat.valid() && pbr_frame(r, srgb_, W, H, 0.9f); }
      CHECK(orm_lin.valid() && orm_srgb.valid());
    }
    pbr_rig_free(r);
  }
  // dev.shutdown() YOK: cihaz PAYLASILIYOR (Tuzaklar 8al) ve surec boyunca yasar.
  CHECK(ok);
  if (!ok) return;
  uint32_t d0 = 0, d1 = 0; int m0 = 0, m1 = 0; double a0 = 0, a1 = 0;
  frame_diff(ref, lin, W * H, &d0, &m0, &a0);
  frame_diff(ref, srgb_, W * H, &d1, &m1, &a1);
  std::printf("    [bilgi] URUN    ORM DOGRUSAL (texel %.3f): %u piksel (%.2f%%), en buyuk kanal %d, ortalama %.3f\n",
              (double)kTexel, d0, 100.0f * (float)d0 / (float)(W * H), m0, a0);
  std::printf("    [bilgi] KONTROL ayni baytlar sRGB (GPU 0.216 gorur): %u piksel (%.2f%%), en buyuk kanal %d, ortalama %.3f\n",
              d1, 100.0f * (float)d1 / (float)(W * H), m1, a1);
  bool linear_upload_matches = m0 <= 2 && a0 < 0.05;
  CHECK(linear_upload_matches);
  bool srgb_upload_is_visibly_wrong = m1 > 20 && a1 > a0 * 20.0;
  CHECK(srgb_upload_is_visibly_wrong); // dusmezse renk uzayi hic olculmuyor
}

// --- BUTCE: set 1 ne kadar buyudu, bedeli ne? -------------------------------
// "Once olc, sonra ekle, sonra tekrar olc" kapisi. Degisiklikten ONCE (HEAD
// 45e1070) set 1'de 2 baglama vardi (albedo sampler + 32 baytlik malzeme UBO'su)
// ve malzeme basina 1 combined-image-sampler descriptor'i. SONRA 5 baglama
// (albedo + UBO + ORM + normal + isima) ve malzeme basina 4 sampler.
//
// TBDR'da onemli olan uc sayi ve bu kapinin iddiasi:
//   1) TILE BUTCESI (eklenti adedi, bit/piksel renk) DEGISMEZ — sampler
//      attachment degildir. Olculur, uydurulmaz.
//   2) Malzeme UBO'su 32 -> 64 bayt buyudu ama GPU'da BEDAVA: adim boyu zaten
//      cihazin minUniformBufferOffsetAlignment'ina yuvarlaniyordu. Kapi bunu
//      iddia etmez, HESAPLAR: yuvarlanmis(32) ile yuvarlanmis(64) esit mi?
//      Esit degilse (ornegin hizasi 32 olan bir cihaz) kapi bunu SOYLER.
//   3) Ornekleme maliyeti yalniz dokuyu GERCEKTEN kullanan malzemede olusur.
//      Olcum: ayni sahne, ayni gecis, GPU zaman damgasi ile iki kurulum —
//      (A) dokusuz malzeme (5 descriptor bagli, 3'u kullanilmiyor),
//      (B) ORM + normal + isima gercekten orneklenen malzeme.
//      B > A cikmiyorsa olcum kordur ve kapi bunu yazar (gizli yesil yok).
ENGINE_TEST(renderer_pbr_texture_budget) {
  PbrOrtakGpu &g = pbr_ortak_gpu();
  if (!g.ok) { skip("Vulkan cihazi yok (paylasilan PBR cihazi)"); return; }
  Device &dev = g.dev;
  static SystemArena sys;
  if (!sys.reserve(256u << 20, "pbr_budget")) { CHECK(false); return; }

  // (1) Tile butcesi: ana gecisin eklentileri. Sampler eklemek burayi
  // DEGISTIRMEZ; degistirseydi zaten gecis yaratimi hata verirdi.
  const VkFormat main_pass[2] = {VK_FORMAT_R8G8B8A8_SRGB, VK_FORMAT_D24_UNORM_S8_UINT};
  TileBudget tb = tile_budget(main_pass, 2);
  std::printf("    [bilgi] tile butcesi (ana gecis): %u eklenti, %u bit/piksel renk (sinir %u / %u), derinlik %u bit -> %s\n",
              tb.attachments, tb.color_bits, kTileMaxAttachments, kTileMaxColorBits, tb.depth_bits, tb.ok ? "TAMAM" : tb.error);
  bool tile_unchanged = tb.ok && tb.attachments == 1 && tb.color_bits == 32;
  CHECK(tile_unchanged);
  // POZITIF KONTROL: butce denetiminin kendisi calisiyor mu? 5 x RGBA16F = 320
  // bit/piksel REDDEDILMELI; edilmiyorsa yukaridaki "TAMAM" bir sey olcmuyor.
  const VkFormat sisman[5] = {VK_FORMAT_R16G16B16A16_SFLOAT, VK_FORMAT_R16G16B16A16_SFLOAT,
                              VK_FORMAT_R16G16B16A16_SFLOAT, VK_FORMAT_R16G16B16A16_SFLOAT,
                              VK_FORMAT_R16G16B16A16_SFLOAT};
  bool budget_check_bites = !tile_budget(sisman, 5).ok;
  CHECK(budget_check_bites);

  const uint32_t W = 1024, H = 1024;
  OffscreenConfig oc;
  oc.srgb = true;
  oc.width = W; oc.height = H;
  OffscreenResult ores;
  OffscreenTarget *off = offscreen_create(dev, sys, oc, &ores);
  if (!off) { CHECK(false); std::printf("    [bilgi] offscreen: %s\n", ores.error); return; }
  renderer::Renderer ren;
  renderer::RendererConfig rc;
  rc.frames_in_flight = 1;
  rc.max_draws = 4096;
  rc.shadow_size = 0; // olculen sey FRAGMENT maliyeti olsun
  bool ren_ok = ren.init(dev, sys, offscreen_render_pass(off), rc);
  CHECK(ren_ok);
  if (!ren_ok) { offscreen_destroy(off); return; }

  // (2) Descriptor / UBO butcesi
  VkPhysicalDeviceProperties2 pp{};
  pp.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
  dev.api().vkGetPhysicalDeviceProperties2(dev.physical(), &pp);
  const uint32_t align = (uint32_t)pp.properties.limits.minUniformBufferOffsetAlignment;
  const uint32_t stride_once = align ? (32u + align - 1) / align * align : 32u; // ONCE: 32 baytlik blok
  const uint32_t stride_sonra = ren.material_ubo_stride();                      // SONRA: 64 baytlik blok
  std::printf("    [bilgi] set 1 baglamalari: ONCE 2 (albedo + 32B UBO) -> SONRA %u (albedo + 64B UBO + ORM + normal + isima)\n",
              renderer::Renderer::kMaterialBindings);
  std::printf("    [bilgi] malzeme basina descriptor: ONCE 1 sampler + 1 UBO -> SONRA %u sampler + 1 UBO\n",
              renderer::Renderer::kMaterialBindings - 1);
  std::printf("    [bilgi] malzeme UBO'su: 32 -> %u bayt; cihaz UBO hizasi %u -> ADIM BOYU %u -> %u bayt (%s)\n",
              renderer::Renderer::kMaterialUboBytes, align, stride_once, stride_sonra,
              stride_once == stride_sonra ? "buyume GPU'da BEDAVA" : "ADIM BOYU BUYUDU: bellek bedeli var");
  CHECK(stride_sonra >= renderer::Renderer::kMaterialUboBytes);
  // Fragment asamasinda toplam sampled-image: set 0'da golge (1) + set 1'de 4.
  const uint32_t need_sampled = 1 + (renderer::Renderer::kMaterialBindings - 1);
  std::printf("    [bilgi] fragment asamasi sampled-image: %u gerekiyor, cihaz siniri %u\n", need_sampled,
              pp.properties.limits.maxPerStageDescriptorSampledImages);
  CHECK(need_sampled <= pp.properties.limits.maxPerStageDescriptorSampledImages);

  // (3) Ornekleme maliyeti: ayni sahne, iki malzeme kurulumu, GPU zaman damgasi.
  renderer::Vertex v[24];
  uint32_t idx[36];
  uint32_t n = renderer::Renderer::plane(v, idx);
  renderer::MeshHandle plane = ren.create_mesh(v, 4, idx, n);
  CHECK(plane.valid());
  renderer::TextureHandle albedo = solid_tex(ren, 200, 190, 180, true);
  renderer::TextureHandle orm = solid_tex(ren, 200, 90, 40, false);
  renderer::TextureHandle nrm = solid_tex(ren, 150, 120, 240, false);
  renderer::TextureHandle emi = solid_tex(ren, 40, 30, 20, true);
  renderer::PbrParams pbr;
  pbr.metallic = 0.5f;
  pbr.roughness = 0.5f;
  renderer::MaterialHandle mat_plain = ren.create_material(albedo, {1, 1, 1}, pbr);
  renderer::PbrTextures t;
  t.orm = orm; t.normal = nrm; t.emissive = emi; t.normal_scale = 1.0f; t.occlusion_strength = 1.0f;
  renderer::MaterialHandle mat_tex = ren.create_material(albedo, {1, 1, 1}, pbr, t);
  CHECK(mat_plain.valid() && mat_tex.valid());
  ren.set_render_size(W, H);
  ren.set_shadows_enabled(false);
  ren.set_light(normalize(Vec3{0.35f, 0.45f, 1.0f}), {0.1f, 0.1f, 0.12f}, 0.9f);
  // Kamera duzleme cok yakin ve tepeden: ekranin tamami dolar, yani olculen sey
  // fragment maliyetidir (vertex/cizim sayisi degil).
  ren.set_camera(Mat4::look_at({0, 1.2f, 0.001f}, {0, 0, 0}, {0, 1, 0}),
                 Mat4::perspective(1.2f, (float)W / (float)H, 0.05f, 50.0f));
  Rec rr{&ren};
  // --- (3a) DETERMINIK ONCE/SONRA: shader'daki doku ornekleme komutu -------
  // Zamanlama makineye baglidir; BU sayi degildir. Depoya GIREN SPIR-V'de doku
  // ornekleme komutlarini sayiyoruz (OpImageSample* = 87..90). Olculdu:
  //   ONCE (HEAD 45e1070): 2 = 1 duz (albedo) + 1 dref (golge PCF)
  //   SONRA              : 5 = 4 duz (albedo + ORM + normal + isima) + 1 dref
  // Golge PCF 3x3 dongusu ACILMIYOR, o yuzden tek komut sayilir — bu da
  // "sayilan sey komut, ornek degil" demek; kapinin iddiasi tam olarak bu.
  // KONTROL: ayni sayac mesh.vert'te 0 vermeli (vertex shader'inda doku yok).
  // Vermezse sayac ornekleme komutunu degil "her komutu" sayiyordur.
  auto sample_ops = [](const uint32_t *spv, uint32_t bytes, uint32_t *dref) -> uint32_t {
    *dref = 0;
    const uint32_t words = bytes / 4;
    if (words < 5 || spv[0] != 0x07230203u) return 0xFFFFFFFFu;
    uint32_t n = 0;
    for (uint32_t w = 5; w < words;) {
      const uint32_t len = spv[w] >> 16, op = spv[w] & 0xFFFFu;
      if (len == 0) break;
      // OpImageSample{Implicit,Explicit}Lod = 87/88, ...Dref... = 89/90
      if (op >= 87u && op <= 90u) { n++; if (op >= 89u) (*dref)++; }
      w += len;
    }
    return n;
  };
  uint32_t frag_dref = 0, vert_dref = 0;
  const uint32_t frag_samples = sample_ops(mesh_frag_spv, mesh_frag_spv_size, &frag_dref);
  const uint32_t vert_samples = sample_ops(mesh_vert_spv, mesh_vert_spv_size, &vert_dref);
  std::printf("    [bilgi] mesh.frag SPIR-V doku ornekleme komutu: ONCE 2 (albedo + golge) -> SONRA %u (%u duz + %u dref/golge)\n",
              frag_samples, frag_samples - frag_dref, frag_dref);
  std::printf("    [bilgi] KONTROL mesh.vert doku ornekleme komutu: %u (vertex shader'inda doku yok -> 0 olmali)\n",
              vert_samples);
  CHECK(vert_samples == 0); // sayac gercekten ornekleme komutunu sayiyor mu
  // 4 duz (albedo, ORM, normal, isima) + 1 dref (golge). Sayi tutmuyorsa ya bir
  // sampler shader'a hic girmemis ya da beklenmedik bir ornekleme eklenmis.
  bool three_new_samplers_are_in_the_shader = frag_samples == 5 && frag_dref == 1;
  CHECK(three_new_samplers_are_in_the_shader);

  // --- (3b) Ornekleme maliyeti (ZAMANLAMA; makineye bagli) ----------------
  // offscreen_render_custom (rhi/, bu dilimin yazma alaninda degil) GPU zaman
  // damgasi yazmiyor — yalniz offscreen_render_frame yaziyor. Gonderim SENKRON
  // oldugu icin duvar suresi GPU calismasini kapsar, ama SABIT bir gonderim +
  // geri okuma maliyeti de kapsar; hizli bir GPU'da o sabit kisim fragment
  // isini bogar. Bu yuzden ortu sayisi SABIT DEGIL, KALIBRE edilir: bos kareye
  // gore en az 3 kat yavaslayana kadar ikiye katlanir. Kalibrasyon tutmazsa
  // sonuc KIRMIZI degil GORUNUR ATLAMA olur — urun kusurlu degil, olcum bu
  // makinede ayirt edemiyor (PSO kapisiyla ayni ders).
  const int kWarm = 3, kMeas = 9;
  auto medyan = [&](int layers, renderer::MaterialHandle mh, bool *ok_out) -> uint64_t {
    static uint64_t ns[kMeas];
    int got = 0;
    for (int f = 0; f < kWarm + kMeas; f++) {
      ren.begin_frame(0);
      ren.clear_point_lights();
      for (int k = 0; k < layers; k++) // ayni pikselleri ust uste: fragment maliyeti belirginlessin
        ren.draw(plane, mh, Mat4::scale({40, 1, 40}), {1, 1, 1});
      const uint64_t t0 = platform::now_ns();
      if (!offscreen_render_custom(off, oc, rec_main, &rr, &ores, rec_shadow)) { *ok_out = false; return 0; }
      const uint64_t dt = platform::now_ns() - t0;
      if (f >= kWarm) ns[got++] = dt;
    }
    std::sort(ns, ns + got);
    return ns[got / 2];
  };
  bool run_ok = true;
  const uint64_t empty_ns = medyan(0, mat_tex, &run_ok); // sabit gonderim + geri okuma maliyeti
  int layers = 32;
  uint64_t tex_ns = 0;
  while (run_ok && layers <= 2048) {
    tex_ns = medyan(layers, mat_tex, &run_ok);
    if (!run_ok || tex_ns > empty_ns * 3) break;
    layers *= 2;
  }
  CHECK(run_ok);
  if (!run_ok) { ren.shutdown(); offscreen_destroy(off); return; }
  std::printf("    [makine] %s\n", dev.caps().device_name);
  std::printf("    [bilgi] kalibrasyon: bos kare %.3f ms; %d kat ortude dokulu kare %.3f ms (%.2f kat)\n",
              (double)empty_ns / 1e6, layers, (double)tex_ns / 1e6,
              empty_ns ? (double)tex_ns / (double)empty_ns : 0.0);
  // SANAL GPU'DA ZAMANLAMA BOLUMU OLCULMEZ. Tile butcesi (yukarisi) analitik ve
  // her yerde gecerli; ama "uc sampler eklemek kareyi pahalilastirir mi" sorusu
  // DUVAR SAATIYLE olculuyor ve Apple Paravirtual (CI macOS) o olcumu tasimiyor:
  // ayni cihaz GPU zaman damgalarini 0.000 ms bildiriyor, yani fragment maliyeti
  // zaman ekseninde GORUNMUYOR. Olculdu: dokulu yol dokusuzdan HIZLI cikti
  // (b > a dustu), ki fiziksel olarak anlamsiz — olcum gurultuyu okuyordu.
  // Ayni koruma test.hpp'de bu cihaz icin zaten belgeli (piksel kapilari).
  if (test::gpu_is_virtual(dev.caps().device_name)) {
    skip("sanal GPU (Apple Paravirtual, CI macOS): ornekleme maliyetinin ZAMAN olcumu gercek cihazda");
  } else if (tex_ns <= empty_ns * 3) {
    std::printf("    [bilgi] 2048 kat ortude bile kare suresi sabit maliyetin 3 katina cikmadi: bu makinede duvar saati "
                "fragment maliyetine KOR\n");
    skip("ornekleme maliyeti bu makinede olculemiyor (duvar saati sabit gonderim maliyetine bogulu)");
  } else {
    const uint64_t plain_ns = medyan(layers, mat_plain, &run_ok);
    const uint64_t hi_ns = medyan(layers * 4 <= 4096 ? layers * 4 : 4096, mat_tex, &run_ok);
    CHECK(run_ok);
    if (run_ok) {
      const double a = (double)plain_ns / 1e6, b = (double)tex_ns / 1e6, cc = (double)hi_ns / 1e6;
      std::printf("    [bilgi] kare suresi (medyan %d kare, %ux%u, %d kat ortu): dokusuz %.3f ms -> ORM+normal+isima %.3f ms "
                  "(%+.3f ms, %%%+.1f)\n", kMeas, W, H, layers, a, b, b - a, a > 0 ? 100.0 * (b - a) / a : 0.0);
      std::printf("    [bilgi] KONTROL ayni dokulu malzeme %d kat ortu: %.3f ms (%.2f kat) — olcum fragment maliyetini goruyor mu\n",
                  layers * 4 <= 4096 ? layers * 4 : 4096, cc, b > 0 ? cc / b : 0.0);
      // KONTROLUN ISLEVI: dort kat ortude sure ACIKCA artmali. Artmiyorsa olcum
      // fragment maliyetine kordur ve "dokular +x ms" sayisi da bir sey soylemez.
      if (cc > b * 1.5) {
        CHECK(b > a); // dokulu yol dokusuzdan pahali OLMALI (uc sampler bedava degil)
      } else {
        std::printf("    [bilgi] dort kat ortu artisi sureyi 1.5 kat buyutmedi: doku farki gurultunun icinde\n");
        skip("ornekleme maliyetinin farki bu makinede gurultunun altinda");
      }
    }
  }
  ren.shutdown();
  offscreen_destroy(off);
  // dev.shutdown() YOK: cihaz PAYLASILIYOR (Tuzaklar 8al) ve surec boyunca yasar.
}

// --- UCTAN UCA: glTF dokulari malzemeye YUKLENIYOR mu? ----------------------
// Ayri ayri kapilar var: okuma (content_gltf_pbr_textures_reach_material),
// golgeleme (renderer_orm_*/normal_*/emissive_*). Aradaki HALKA —
// upload_model'in ModelMaterial'i renderer::PbrTextures'a cevirip
// create_material'a vermesi — hicbirinde olculmuyordu; o halka kopuk olsaydi
// butun oteki kapilar yine yesil kalirdi (elle kurulan dokularla kosuyorlar).
// Kapi gercek varligi yukler, tutamaclari okur ve bir kare cizer.
// KONTROL: ayni malzemenin dokulari BOSALTILIR (set_material_textures{}) ve
// ayni kare yeniden cizilir — goruntu DEGISMELIDIR. Degismiyorsa dokular
// zaten hicbir ise yaramiyordu.
ENGINE_TEST(renderer_gltf_pbr_textures_upload_to_material) {
  PbrOrtakGpu &g = pbr_ortak_gpu();
  if (!g.ok) { skip("Vulkan cihazi yok (paylasilan PBR cihazi)"); return; }
  Device &dev = g.dev;
  char path[1024];
  {
    const char *dir = std::getenv("TULPAR_ENGINE_ASSETS");
    if (dir && *dir) std::snprintf(path, sizeof path, "%s/pbr_plane.gltf", dir);
    else std::snprintf(path, sizeof path, "%s/tests/assets/pbr_plane.gltf", ENGINE_SOURCE_DIR);
    FILE *f = std::fopen(path, "rb");
    if (!f) { skip("pbr_plane.gltf yok (make_test_gltf.py)"); return; }
    std::fclose(f);
  }
  if (test::gpu_is_virtual(dev.caps().device_name)) { skip("sanal GPU (CI macOS): piksel kapisi gercek cihazda"); return; }
  static SystemArena sys;
  if (!sys.reserve(192u << 20, "gltf_pbr_upload")) { CHECK(false); return; }
  content::Model m;
  bool ok = content::gltf_load(sys, path, &m);
  if (!ok) std::printf("    [bilgi] yukleme hatasi: %s\n", m.error);
  CHECK(ok);
  if (!ok) return;

  const uint32_t W = 256, H = 256;
  static uint8_t with_tex[W * H * 4], without_tex[W * H * 4];
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
  bool ren_ok = ren.init(dev, sys, offscreen_render_pass(off), rc);
  CHECK(ren_ok);
  if (!ren_ok) { offscreen_destroy(off); return; }
  content::UploadedModel up;
  bool up_ok = content::upload_model(ren, sys, m, &up);
  // Normal haritasi CPU'da mip'lenip yuklendi mi? `build_normal_mips` dogru
  // calissa bile YUKLEME YOLU onu hic cagirmazsa goruntu eski (blit, birim
  // olmayan normaller) davranista kalirdi ve fonksiyonu dogrudan olcen kapi
  // (content_normal_map_mips_stay_unit_length) bunu goremezdi.
  std::printf("    [bilgi] CPU'da yeniden normallestirilmis mip zinciriyle yuklenen doku: %u (normal haritasi 1 adet)\n",
              up.normal_mip_textures);
  CHECK(up.normal_mip_textures == 1);
  CHECK(up_ok);
  if (!up_ok) { ren.shutdown(); offscreen_destroy(off); return; }
  CHECK(up.material_count == 1);
  const renderer::MaterialHandle mh = up.materials[0];
  const renderer::PbrTextures t = ren.material_textures(mh);
  std::printf("    [bilgi] yuklenen malzeme: ORM %s, normal %s, isima %s | normal olcegi %.2f, occlusion gucu %.2f\n",
              t.orm.valid() ? "VAR" : "yok", t.normal.valid() ? "VAR" : "yok", t.emissive.valid() ? "VAR" : "yok",
              (double)t.normal_scale, (double)t.occlusion_strength);
  bool handles_arrived = t.orm.valid() && t.normal.valid() && t.emissive.valid() && t.normal_scale > 0.74f &&
                         t.normal_scale < 0.76f && t.occlusion_strength > 0.59f && t.occlusion_strength < 0.61f;
  CHECK(handles_arrived);

  Rec rr{&ren};
  auto shot = [&](uint8_t *dst) {
    ren.set_light(normalize(Vec3{0.35f, 0.9f, 0.45f}), {0.10f, 0.11f, 0.13f}, 0.9f);
    ren.set_camera(Mat4::look_at({0, 1.6f, 1.9f}, {0, 0, 0}, {0, 1, 0}),
                   Mat4::perspective(1.0f, (float)W / (float)H, 0.05f, 50.0f));
    ren.set_render_size(W, H);
    ren.set_shadows_enabled(false);
    ren.begin_frame(0);
    ren.clear_point_lights();
    content::draw_model(ren, m, up, Mat4::identity());
    OffscreenResult r2;
    if (!offscreen_render_custom(off, oc, rec_main, &rr, &r2, rec_shadow)) return false;
    std::memcpy(dst, r2.pixels, (size_t)W * H * 4);
    return true;
  };
  bool drew = shot(with_tex);
  CHECK(drew);
  // KONTROL: dokulari bosalt (carpanlar aynen kalir) ve ayni kareyi ciz.
  if (drew) {
    CHECK(ren.set_material_textures(mh, renderer::PbrTextures{}));
    drew = shot(without_tex);
    CHECK(drew);
  }
  ren.shutdown();
  offscreen_destroy(off);
  // dev.shutdown() YOK: cihaz PAYLASILIYOR (Tuzaklar 8al).
  if (!drew) return;
  uint32_t d = 0; int mx = 0; double mean = 0;
  frame_diff(with_tex, without_tex, W * H, &d, &mx, &mean);
  std::printf("    [bilgi] dokulu vs dokusuz ayni kare: %u piksel farkli (%.1f%%), en buyuk kanal %d, ortalama %.2f\n",
              d, 100.0f * (float)d / (float)(W * H), mx, mean);
  bool textures_actually_change_the_frame = mx > 24 && mean > 2.0;
  CHECK(textures_actually_change_the_frame);
}

// --- KAPI: prosedurel ilkeller disa mi bakiyor? (PR #331) -------------------
// #331 renderer.hpp'ye sphere/capsule/cylinder/cone/quad/torus BILDIRIMLERINI
// ekledi ama TANIM yazmadi; tanimlar bu depoda yazildi. Tanim yazmanin en
// sinsi hatasi TERS SARIM: derleme gecer, test gecer, ikili calisir — yalniz
// yuzeyler ice bakar ve isik yanlis olur. Ekran goruntusune bakmadan bunu
// yakalamanin yolu her ucgenin GEOMETRIK normalini (kenarlarin capraz carpimi)
// tepe normalleriyle karsilastirmak.
//
// Olculenler: (1) hicbir ucgen ters degil, (2) tepe normalleri birim,
// (3) tepe/indeks sayilari basliktaki formullerle ve tampon ustleriyle
// tutarli, (4) noktalar beklenen yuzeyde. Sonda POZITIF KONTROL: tek bir
// ucgen bilerek ters cevrilince kapi onu GORMELI — gormezse kapi hicbir sey
// olcmuyordur.
namespace {

struct PrimCheck {
  uint32_t idx_count = 0;
  uint32_t vert_count = 0;
  uint32_t tris = 0;
  uint32_t flipped = 0;
  uint32_t degenerate = 0;
  float worst_unit_err = 0.0f;
};

PrimCheck prim_check(const renderer::Vertex *v, const uint32_t *idx, uint32_t n_idx) {
  PrimCheck s;
  s.idx_count = n_idx;
  for (uint32_t i = 0; i < n_idx; i++)
    if (idx[i] + 1u > s.vert_count) s.vert_count = idx[i] + 1u;
  for (uint32_t i = 0; i + 2 < n_idx; i += 3) {
    const Vec3 p0 = v[idx[i]].pos, p1 = v[idx[i + 1]].pos, p2 = v[idx[i + 2]].pos;
    const Vec3 g = cross(p1 - p0, p2 - p0);
    // ESIK 1e-7: birim olcekli bir mesh'te float capraz carpimi kutup
    // civarinda 1e-9'a kadar gurultu uretebiliyor (olculdu). 1e-9 esigi o
    // sivri ucgenleri GERCEK sayiyor ve gurultulu normallerini "ters" diye
    // raporluyordu. Uretici artik kutbu tam sifirliyor; esik yine de paye
    // birakiyor ki parametre degisince kapi yanlis alarm vermesin.
    if (length(g) < 1e-7f) { s.degenerate++; continue; }
    s.tris++;
    const Vec3 avg = v[idx[i]].nrm + v[idx[i + 1]].nrm + v[idx[i + 2]].nrm;
    if (length(avg) < 1e-9f) continue;
    if (dot(normalize(g), normalize(avg)) < 0.0f) s.flipped++;
  }
  for (uint32_t k = 0; k < s.vert_count; k++) {
    const float e = std::fabs(length(v[k].nrm) - 1.0f);
    if (e > s.worst_unit_err) s.worst_unit_err = e;
  }
  return s;
}

} // namespace

ENGINE_TEST(renderer_procedural_primitives_face_outward) {
  using namespace renderer;
  static Vertex v[Renderer::kPrimitiveMaxVerts];
  static uint32_t idx[Renderer::kPrimitiveMaxIndices];

  struct Row { const char *ad; uint32_t idx_bekle; uint32_t vert_bekle; };
  uint32_t n = 0;
  PrimCheck s;
  uint32_t toplam_ucgen = 0, toplam_ters = 0;
  float en_kotu_birim = 0.0f;

  // --- kure: her nokta yaricap 0.5 uzerinde -------------------------------
  n = Renderer::sphere(v, idx);
  s = prim_check(v, idx, n);
  CHECK(n == 3072 && s.vert_count == 561);
  CHECK(s.flipped == 0);
  {
    float en_kotu_r = 0.0f;
    for (uint32_t k = 0; k < s.vert_count; k++) {
      const float e = std::fabs(length(v[k].pos) - 0.5f);
      if (e > en_kotu_r) en_kotu_r = e;
    }
    CHECK(en_kotu_r < 1e-5f);
    std::printf("    [bilgi] kure: %u ucgen, ters %u, yaricap sapmasi %.2e\n",
                s.tris, s.flipped, (double)en_kotu_r);
  }
  toplam_ucgen += s.tris; toplam_ters += s.flipped;
  if (s.worst_unit_err > en_kotu_birim) en_kotu_birim = s.worst_unit_err;

  // --- kapsul: Y uzanimi 2*(half_height+radius) ---------------------------
  n = Renderer::capsule(v, idx);
  s = prim_check(v, idx, n);
  CHECK(n == 3264 && s.vert_count == 594);
  CHECK(s.flipped == 0);
  {
    float ymin = 1e9f, ymax = -1e9f;
    for (uint32_t k = 0; k < s.vert_count; k++) {
      if (v[k].pos.y < ymin) ymin = v[k].pos.y;
      if (v[k].pos.y > ymax) ymax = v[k].pos.y;
    }
    CHECK(std::fabs((ymax - ymin) - 2.0f) < 1e-4f);
    std::printf("    [bilgi] kapsul: %u ucgen, ters %u, Y uzanimi %.4f (2.0 bekleniyor)\n",
                s.tris, s.flipped, (double)(ymax - ymin));
  }
  toplam_ucgen += s.tris; toplam_ters += s.flipped;
  if (s.worst_unit_err > en_kotu_birim) en_kotu_birim = s.worst_unit_err;

  // --- silindir -----------------------------------------------------------
  n = Renderer::cylinder(v, idx);
  s = prim_check(v, idx, n);
  CHECK(n == 384 && s.vert_count == 134);
  CHECK(s.flipped == 0);
  toplam_ucgen += s.tris; toplam_ters += s.flipped;
  if (s.worst_unit_err > en_kotu_birim) en_kotu_birim = s.worst_unit_err;

  // --- koni ---------------------------------------------------------------
  n = Renderer::cone(v, idx);
  s = prim_check(v, idx, n);
  CHECK(n == 192 && s.vert_count == 100);
  CHECK(s.flipped == 0);
  toplam_ucgen += s.tris; toplam_ters += s.flipped;
  if (s.worst_unit_err > en_kotu_birim) en_kotu_birim = s.worst_unit_err;

  // --- dortgen: +Z'ye bakar, plane()'den (XZ, +Y) AYRI bir ilkel ----------
  n = Renderer::quad(v, idx);
  s = prim_check(v, idx, n);
  CHECK(n == 6 && s.vert_count == 4);
  CHECK(s.flipped == 0);
  CHECK(v[0].nrm.z > 0.99f && std::fabs(v[0].nrm.y) < 1e-6f);
  toplam_ucgen += s.tris; toplam_ters += s.flipped;
  if (s.worst_unit_err > en_kotu_birim) en_kotu_birim = s.worst_unit_err;

  // --- simit: her nokta ana cemberden r_tube kadar uzakta -----------------
  n = Renderer::torus(v, idx);
  s = prim_check(v, idx, n);
  CHECK(n == 3072 && s.vert_count == 561);
  CHECK(s.flipped == 0);
  {
    float en_kotu_t = 0.0f;
    for (uint32_t k = 0; k < s.vert_count; k++) {
      const Vec3 p = v[k].pos;
      const Vec3 duz{p.x, 0.0f, p.z};
      const Vec3 merkez = (length(duz) > 1e-6f) ? normalize(duz) * 0.5f : Vec3{0.5f, 0, 0};
      const float e = std::fabs(length(p - merkez) - 0.2f);
      if (e > en_kotu_t) en_kotu_t = e;
    }
    CHECK(en_kotu_t < 1e-5f);
    std::printf("    [bilgi] simit: %u ucgen, ters %u, tup yaricap sapmasi %.2e\n",
                s.tris, s.flipped, (double)en_kotu_t);
  }
  toplam_ucgen += s.tris; toplam_ters += s.flipped;
  if (s.worst_unit_err > en_kotu_birim) en_kotu_birim = s.worst_unit_err;

  CHECK(toplam_ters == 0);
  CHECK(en_kotu_birim < 1e-5f);
  std::printf("    [bilgi] 6 ilkel: %u ucgen, ters %u, normal birimlik sapmasi %.2e\n",
              toplam_ucgen, toplam_ters, (double)en_kotu_birim);

  // --- POZITIF KONTROL ----------------------------------------------------
  // Tek bir ucgeni bilerek ters cevir: kapi tam 1 ters saymali. Saymazsa
  // yukaridaki butun "ters 0" sonuclari hicbir sey olcmuyor demektir.
  n = Renderer::sphere(v, idx);
  const uint32_t t = idx[3]; idx[3] = idx[4]; idx[4] = t;
  const PrimCheck kontrol = prim_check(v, idx, n);
  CHECK(kontrol.flipped == 1);
  std::printf("    [bilgi] POZITIF KONTROL (bir ucgen ters cevrildi): kapi %u ters gordu (1 bekleniyor)\n",
              kontrol.flipped);
}
