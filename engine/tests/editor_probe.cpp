#include "tests/editor_probe.hpp"

#include <cstdio>
#include <cstring>

#include "app/editor_ui.hpp"
#include "core/memory/arena.hpp"
#include "renderer/renderer.hpp"
#include "rhi/device.hpp"
#include "rhi/offscreen.hpp"
#include "rhi/vk_api.hpp"

#include <imgui.h>

namespace tulpar::engine::test {
namespace {
rhi::VkApi g_api;
struct Rec { renderer::Renderer *r; app::EditorUi *ui; };
void rec_main(VkCommandBuffer cb, void *u) { auto *c = static_cast<Rec *>(u); c->r->record(cb); c->r->ui_record(cb); c->ui->record(cb); }
void rec_shadow(VkCommandBuffer cb, void *u) { static_cast<Rec *>(u)->r->record_shadow(cb); }
constexpr uint32_t kMaxW = 1920, kMaxH = 1080;
uint8_t g_pixels[kMaxW * kMaxH * 4];
void fail(EditorProbe &p, const char *what) { std::snprintf(p.err, sizeof p.err, "%s", what); }
} // namespace

ProbeStatus editor_probe_render(EditorProbe &p) {
  p.vertices = p.indices = 0;
  p.pixels = nullptr;
  p.err[0] = 0;
  if (!p.draw) { fail(p, "draw geri cagrisi yok"); return ProbeStatus::Fail; }
  if (p.width < 1 || p.height < 1 || p.width > kMaxW || p.height > kMaxH) { fail(p, "olcu 1..1920x1080 disinda"); return ProbeStatus::Fail; }
  if (!rhi::vk_api_load(g_api)) return ProbeStatus::NoVulkan;
  // Arena her sondada SIFIRDAN: cihaz/offscreen/renderer ayirmalari arenadan
  // gelir ve arena geri vermez — bir kez ayirip tekrar kullanmak 30. sondada
  // "arena TASTI" ile cokuyordu (olculdu 2026-09-17, tum editor kapilari).
  static SystemArena sys;
  sys.release();
  if (!sys.reserve(64u << 20, "editor_probe")) { fail(p, "arena"); return ProbeStatus::Fail; }
  rhi::Device dev;
  rhi::DeviceConfig dc;
  if (!dev.init(sys, g_api, dc)) return ProbeStatus::NoVulkan;
  rhi::OffscreenConfig oc;
  oc.srgb = true;
  oc.width = p.width;
  oc.height = p.height;
  rhi::OffscreenResult ores;
  rhi::OffscreenTarget *off = rhi::offscreen_create(dev, sys, oc, &ores);
  if (!off) { fail(p, ores.error); dev.shutdown(); return ProbeStatus::Fail; }
  renderer::Renderer ren;
  renderer::RendererConfig rc;
  rc.frames_in_flight = 1;
  rc.shadow_size = 0;
  rc.srgb_target = true;
  oc.clear[0] = 0x12; oc.clear[1] = 0x15; oc.clear[2] = 0x1A; oc.clear[3] = 255; // editorun dok zemini (kBg0)
  ProbeStatus status = ProbeStatus::Ok;
  if (!ren.init(dev, sys, rhi::offscreen_render_pass(off), rc)) { fail(p, "renderer init"); rhi::offscreen_destroy(off); dev.shutdown(); return ProbeStatus::Fail; }
  app::EditorUi ui;
  char fpath[1024];
  std::snprintf(fpath, sizeof fpath, "%s/assets/fonts/DejaVuSans.ttf", ENGINE_SOURCE_DIR);
  if (!ui.init(dev, rhi::offscreen_render_pass(off), 1, 2, p.with_font ? fpath : nullptr, p.font_px)) {
    fail(p, ui.last_error());
    ren.shutdown(); rhi::offscreen_destroy(off); dev.shutdown();
    return ProbeStatus::Fail;
  }
  ren.set_camera(Mat4::look_at({0, 3, 6}, {0, 0, 0}, {0, 1, 0}), Mat4::perspective(1.0f, (float)p.width / (float)p.height, 0.1f, 50.0f));
  ren.set_render_size(p.width, p.height);
  Rec rr{&ren, &ui};
  const uint32_t frames = p.frames ? p.frames : 1;
  for (uint32_t f = 0; f < frames; f++) {
    ui.begin_frame(nullptr, (float)p.width, (float)p.height, 1.0f / 60.0f);
    p.draw(p.ctx, f);
    ui.end_frame();
    p.vertices = ui.stats().vertices;
    p.indices = ui.stats().indices;
    ren.begin_frame(0);
    if (!rhi::offscreen_render_custom(off, oc, rec_main, &rr, &ores, rec_shadow)) { fail(p, ores.error); status = ProbeStatus::Fail; break; }
  }
  if (status == ProbeStatus::Ok) {
    std::memcpy(g_pixels, ores.pixels, (size_t)p.width * p.height * 4);
    p.pixels = g_pixels;
    if (p.out_ppm && !rhi::write_ppm(p.out_ppm, g_pixels, p.width, p.height)) fail(p, "ppm yazilamadi");
  }
  ui.shutdown();
  ren.shutdown();
  rhi::offscreen_destroy(off);
  dev.shutdown();
  return status;
}

void probe_pixel(const EditorProbe &p, uint32_t x, uint32_t y, uint8_t out[4]) {
  out[0] = out[1] = out[2] = out[3] = 0;
  if (!p.pixels || x >= p.width || y >= p.height) return;
  std::memcpy(out, p.pixels + ((size_t)y * p.width + x) * 4, 4);
}

uint32_t probe_diff(const uint8_t *a, const uint8_t *b, uint32_t n) {
  uint32_t d = 0;
  for (uint32_t i = 0; i < n; i++)
    if (a[i * 4] != b[i * 4] || a[i * 4 + 1] != b[i * 4 + 1] || a[i * 4 + 2] != b[i * 4 + 2]) d++;
  return d;
}
} // namespace tulpar::engine::test
