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

  const uint32_t W = 256, H = 256;
  OffscreenConfig oc;
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
  const uint32_t W = 256, H = 256;
  OffscreenConfig oc;
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
