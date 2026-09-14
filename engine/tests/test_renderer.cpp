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
