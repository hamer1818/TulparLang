// Editor arayuzu (Dear ImGui) motorun offscreen gecisine ciziyor mu? Pencere
// yok: bir pencere + metin + dugme cizilen kare, bos ImGui karesinden farkli
// pikseller vermeli (>500); bos kare (kontrol) 0 fark ve 0 vertex vermeli.
#include <cmath>
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

// Sahne veri modelinin donusum sirasi (T*Rz*Ry*Rx*S) ImGuizmo'nun
// Recompose/Decompose gelenegiyle ayni olmali; yoksa gizmo her karede
// Euler'i "duzeltir" ve varlik titrer. Kontrol: ters sira farkli matris verir.
#include "content/scene.hpp"
#include <ImGuizmo.h>
ENGINE_TEST(editor_scene_matrix_matches_gizmo_convention) {
  content::SceneEntity e{};
  e.pos = {1, 2, 3}; e.rot_deg = {30, 45, 60}; e.scale = {1, 2, 0.5f};
  const Mat4 ours = content::scene_entity_matrix(e);
  float g[16];
  ImGuizmo::RecomposeMatrixFromComponents(&e.pos.x, &e.rot_deg.x, &e.scale.x, g);
  float max_d = 0;
  for (int c = 0; c < 4; c++)
    for (int r = 0; r < 4; r++) {
      const float d = std::fabs(ours.m[c][r] - g[c * 4 + r]);
      if (d > max_d) max_d = d;
    }
  CHECK(max_d < 1e-5f);
  // Ayristir -> yeniden kur: ayni matris (Euler farkli olabilir, matris ayni).
  float p[3], r[3], s[3], g2[16];
  ImGuizmo::DecomposeMatrixToComponents(&ours.m[0][0], p, r, s);
  ImGuizmo::RecomposeMatrixFromComponents(p, r, s, g2);
  float max_d2 = 0;
  for (int i = 0; i < 16; i++) { const float d = std::fabs(g2[i] - (&ours.m[0][0])[i]); if (d > max_d2) max_d2 = d; }
  CHECK(max_d2 < 1e-4f);
  // Kontrol: sira ters (Rx*Ry*Rz) olsaydi fark buyuk olurdu.
  const float k = 3.14159265f / 180.0f;
  const Mat4 wrong = Mat4::translate(e.pos) * Mat4::rotate({1, 0, 0}, 30 * k) * Mat4::rotate({0, 1, 0}, 45 * k) *
                     Mat4::rotate({0, 0, 1}, 60 * k) * Mat4::scale(e.scale);
  float max_w = 0;
  for (int c = 0; c < 3; c++)
    for (int rr = 0; rr < 3; rr++) { const float d = std::fabs(wrong.m[c][rr] - g[c * 4 + rr]); if (d > max_w) max_w = d; }
  CHECK(max_w > 1e-2f);
  std::printf("    [bilgi] gizmo gelenegi: en buyuk fark %.2e (ayristir/kur %.2e), ters sira %.3f\n", max_d, max_d2, max_w);
}
