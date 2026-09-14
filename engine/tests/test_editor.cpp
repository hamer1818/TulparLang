// Editor arayuzu (Dear ImGui) motorun offscreen gecisine ciziyor mu? Pencere
// yok: bir pencere + metin + dugme cizilen kare, bos ImGui karesinden farkli
// pikseller vermeli (>500); bos kare (kontrol) 0 fark ve 0 vertex vermeli.
#include <cstdio>
#include <cstring>

#include "app/editor_ui.hpp"
#include "core/memory/arena.hpp"
#include "renderer/renderer.hpp"
#include "rhi/device.hpp"
#include "rhi/offscreen.hpp"
#include "rhi/vk_api.hpp"
#include "tests/test.hpp"

#include <imgui.h>

using namespace tulpar::engine;
using namespace tulpar::engine::test;

namespace {
rhi::VkApi g_api;
struct Rec { renderer::Renderer *r; app::EditorUi *ui; };
void rec_main(VkCommandBuffer cb, void *u) { auto *c = static_cast<Rec *>(u); c->r->record(cb); c->r->ui_record(cb); c->ui->record(cb); }
void rec_shadow(VkCommandBuffer cb, void *u) { static_cast<Rec *>(u)->r->record_shadow(cb); }
} // namespace

ENGINE_TEST(editor_imgui_draws_into_offscreen_pass) {
  if (!rhi::vk_api_load(g_api)) { skip("Vulkan loader yok"); return; }
  static SystemArena sys;
  if (!sys.reserve(64u << 20, "editor_test")) { CHECK(false); return; }
  rhi::Device dev;
  rhi::DeviceConfig dc;
  if (!dev.init(sys, g_api, dc)) { skip("Vulkan cihazi yok"); return; }
  const uint32_t W = 256, H = 256;
  rhi::OffscreenConfig oc;
  oc.srgb = true;
  oc.width = W; oc.height = H;
  rhi::OffscreenResult ores;
  rhi::OffscreenTarget *off = rhi::offscreen_create(dev, sys, oc, &ores);
  if (!off) { CHECK(false); dev.shutdown(); return; }
  renderer::Renderer ren;
  renderer::RendererConfig rc;
  rc.frames_in_flight = 1;
  rc.shadow_size = 0;
  if (!ren.init(dev, sys, rhi::offscreen_render_pass(off), rc)) { CHECK(false); rhi::offscreen_destroy(off); dev.shutdown(); return; }
  app::EditorUi ui;
  bool ok = ui.init(dev, rhi::offscreen_render_pass(off), 1, 2, nullptr, 0);
  if (!ok) std::printf("    [bilgi] editor ui: %s\n", ui.last_error());
  CHECK(ok);
  if (!ok) { ren.shutdown(); rhi::offscreen_destroy(off); dev.shutdown(); return; }
  ren.set_camera(Mat4::look_at({0, 3, 6}, {0, 0, 0}, {0, 1, 0}), Mat4::perspective(1.0f, 1.0f, 0.1f, 50.0f));
  ren.set_render_size(W, H);
  Rec rr{&ren, &ui};
  static uint8_t px[2][W * H * 4];
  uint32_t verts[2] = {0, 0};
  for (int pass = 0; pass < 2; pass++) {
    ui.begin_frame(nullptr, (float)W, (float)H, 1.0f / 60.0f);
    if (pass == 0) {
      ImGui::SetNextWindowPos(ImVec2(20, 20));
      ImGui::SetNextWindowSize(ImVec2(200, 120));
      ImGui::Begin("Tulpar");
      ImGui::Text("Editor testi");
      ImGui::Button("Dugme");
      ImGui::End();
    }
    ui.end_frame();
    verts[pass] = ui.stats().vertices;
    ren.begin_frame(0);
    if (!rhi::offscreen_render_custom(off, oc, rec_main, &rr, &ores, rec_shadow)) { CHECK(false); break; }
    std::memcpy(px[pass], ores.pixels, sizeof px[pass]);
  }
  uint32_t diff = 0;
  for (uint32_t i = 0; i < W * H; i++)
    if (px[0][i * 4] != px[1][i * 4] || px[0][i * 4 + 1] != px[1][i * 4 + 1] || px[0][i * 4 + 2] != px[1][i * 4 + 2]) diff++;
  std::printf("    [bilgi] ImGui: pencereli kare %u vertex, bos kare %u vertex; farkli piksel %u / %u\n", verts[0], verts[1], diff, W * H);
  CHECK(verts[0] > 0);
  CHECK(verts[1] == 0);   // kontrol: bos kare hicbir sey cizmez
  CHECK(diff > 500);      // pencere gercekten piksel yazdi
  ui.shutdown();
  ren.shutdown();
  rhi::offscreen_destroy(off);
  dev.shutdown();
}
