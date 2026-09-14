// Faz 3/6: glTF yukleme + dokulu cizim. Varlik: tests/assets/checker_cube.gltf
// (make_test_gltf.py, belirlenimli). Cihazda: TULPAR_ENGINE_ASSETS dizini
// (android_run.sh push eder); yoksa GORUNUR atlanir.
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "content/gltf.hpp"
#include "core/memory/arena.hpp"
#include "renderer/renderer.hpp"
#include "rhi/device.hpp"
#include "rhi/offscreen.hpp"
#include "rhi/vk_api.hpp"
#include "tests/test.hpp"

using namespace tulpar::engine;
using namespace tulpar::engine::test;

namespace {
bool asset_path(char *buf, size_t n, const char *name) {
  const char *dir = std::getenv("TULPAR_ENGINE_ASSETS");
  if (dir && *dir) std::snprintf(buf, n, "%s/%s", dir, name);
  else std::snprintf(buf, n, "%s/tests/assets/%s", ENGINE_SOURCE_DIR, name);
  FILE *f = std::fopen(buf, "rb");
  if (!f) return false;
  std::fclose(f);
  return true;
}
rhi::VkApi g_api;
struct Rec { renderer::Renderer *r; };
void rec_main(VkCommandBuffer cb, void *u) { static_cast<Rec *>(u)->r->record(cb); }
void rec_shadow(VkCommandBuffer cb, void *u) { static_cast<Rec *>(u)->r->record_shadow(cb); }
// Yatay komsuya gore keskin gecis sayisi: dama dokusu cok, duz renk az.
uint32_t count_edges(const uint8_t *px, uint32_t w, uint32_t h) {
  uint32_t n = 0;
  for (uint32_t y = 0; y < h; y++)
    for (uint32_t x = 1; x < w; x++) {
      const uint8_t *a = px + (y * w + x) * 4, *b = a - 4;
      int d = (a[0] - b[0]) * (a[0] - b[0]) + (a[1] - b[1]) * (a[1] - b[1]) + (a[2] - b[2]) * (a[2] - b[2]);
      if (d > 60 * 60) n++;
    }
  return n;
}
} // namespace

ENGINE_TEST(content_gltf_loads_checker_cube) {
  char path[1024];
  if (!asset_path(path, sizeof path, "checker_cube.gltf")) { skip("varlik yok (tests/assets ya da TULPAR_ENGINE_ASSETS)"); return; }
  static SystemArena sys;
  if (!sys.reserve(32u << 20, "content_test")) { CHECK(false); return; }
  content::Model m;
  bool ok = content::gltf_load(sys, path, &m);
  if (!ok) std::printf("    [bilgi] yukleme hatasi: %s\n", m.error);
  CHECK(ok);
  if (!ok) return;
  bool counts = m.mesh_count == 1 && m.material_count == 1 && m.image_count == 1 && m.instance_count == 1;
  CHECK(counts);
  bool geom = m.meshes[0].vertex_count == 24 && m.meshes[0].index_count == 36 && m.meshes[0].material == 0;
  CHECK(geom);
  bool img = m.images[0].width == 64 && m.images[0].height == 64 && m.materials[0].image == 0;
  CHECK(img);
  // Dama: (0,0) turuncu, (8,0) lacivert
  const uint8_t *p0 = m.images[0].rgba, *p8 = m.images[0].rgba + 8 * 4;
  bool checker = p0[0] > 200 && p0[2] < 60 && p8[0] < 60 && p8[2] > 90;
  CHECK(checker);
  // Dugum translation (0,0.5,0): sinirlar y 0..1
  bool bounds = m.bounds_min.y > -0.01f && m.bounds_min.y < 0.01f && m.bounds_max.y > 0.99f && m.bounds_max.y < 1.01f;
  CHECK(bounds);
  // uv 0..1 icinde
  bool uv_ok = true;
  for (uint32_t i = 0; i < m.meshes[0].vertex_count; i++) {
    Vec2 uv = m.meshes[0].verts[i].uv;
    if (uv.x < 0 || uv.x > 1 || uv.y < 0 || uv.y > 1) uv_ok = false;
  }
  CHECK(uv_ok);
  std::printf("    [bilgi] glTF: %u mesh, %u malzeme, %u goruntu (%ux%u), %u instance; sinir y %.2f..%.2f\n", m.mesh_count,
              m.material_count, m.image_count, m.images[0].width, m.images[0].height, m.instance_count, m.bounds_min.y,
              m.bounds_max.y);
}

ENGINE_TEST(content_textured_cube_renders_checker) {
  char path[1024];
  if (!asset_path(path, sizeof path, "checker_cube.gltf")) { skip("varlik yok"); return; }
  if (!rhi::vk_api_load(g_api)) { skip("Vulkan loader yok"); return; }
  static SystemArena sys;
  if (!sys.reserve(96u << 20, "content_render")) { CHECK(false); return; }
  rhi::Device dev;
  rhi::DeviceConfig dc;
  if (!dev.init(sys, g_api, dc)) { skip("Vulkan cihazi yok"); return; }
  const uint32_t W = 256, H = 256;
  rhi::OffscreenConfig oc;
  oc.srgb = true; // ekranla ayni yol
  oc.width = W; oc.height = H;
  rhi::OffscreenResult ores;
  rhi::OffscreenTarget *off = rhi::offscreen_create(dev, sys, oc, &ores);
  if (!off) { CHECK(false); dev.shutdown(); return; }
  renderer::Renderer ren;
  renderer::RendererConfig rc;
  rc.shadow_size = 0; // golge yok: yalniz doku olculsun
  rc.frames_in_flight = 1;
  bool ren_ok = ren.init(dev, sys, rhi::offscreen_render_pass(off), rc);
  CHECK(ren_ok);
  if (!ren_ok) { rhi::offscreen_destroy(off); dev.shutdown(); return; }

  content::Model m;
  bool loaded = content::gltf_load(sys, path, &m);
  CHECK(loaded);
  content::UploadedModel up;
  bool uploaded = loaded && content::upload_model(ren, sys, m, &up);
  CHECK(uploaded);
  if (!uploaded) { ren.shutdown(); rhi::offscreen_destroy(off); dev.shutdown(); return; }
  bool mips = ren.stats().textures == 2; // varsayilan beyaz + dama
  CHECK(mips);

  ren.set_light(normalize(Vec3{0.3f, 1.0f, 0.5f}), {0.35f, 0.35f, 0.35f}, 0.7f);
  ren.set_camera(Mat4::look_at({1.6f, 1.6f, 1.6f}, {0, 0.5f, 0}, {0, 1, 0}),
                 Mat4::perspective(0.9f, 1.0f, 0.1f, 50.0f));
  Rec rr{&ren};
  static uint8_t tex_px[W * H * 4], flat_px[W * H * 4];
  // 1) dokulu
  ren.begin_frame(0);
  content::draw_model(ren, m, up, Mat4::identity());
  bool f1 = rhi::offscreen_render_custom(off, oc, rec_main, &rr, &ores, rec_shadow);
  CHECK(f1);
  if (f1) std::memcpy(tex_px, ores.pixels, sizeof tex_px);
  // 2) POZITIF KONTROL: ayni mesh, varsayilan (beyaz) malzeme -> kenar az
  ren.begin_frame(0);
  ren.draw(up.meshes[0], ren.default_material(), m.instances[0].world, {0.9f, 0.6f, 0.3f});
  bool f2 = rhi::offscreen_render_custom(off, oc, rec_main, &rr, &ores, rec_shadow);
  CHECK(f2);
  if (f2) std::memcpy(flat_px, ores.pixels, sizeof flat_px);
  uint32_t e_tex = count_edges(tex_px, W, H), e_flat = count_edges(flat_px, W, H);
  std::printf("    [bilgi] keskin gecis: dokulu %u, duz %u (dama en az 4x fazla olmali)\n", e_tex, e_flat);
  bool textured = e_tex > 4 * e_flat && e_tex > 200;
  CHECK(textured);
  // Renkler dokudan: kupte hem turuncu hem lacivert piksel olmali
  uint32_t orange = 0, navy = 0;
  for (uint32_t i = 0; i < W * H; i++) {
    const uint8_t *p = tex_px + i * 4;
    if (p[0] > 120 && p[2] < 70 && p[0] > p[1]) orange++;
    if (p[2] > 45 && p[0] < 40 && p[2] > p[0]) navy++;
  }
  std::printf("    [bilgi] turuncu %u, lacivert %u piksel\n", orange, navy);
  bool both = orange > 300 && navy > 300;
  CHECK(both);
  ren.shutdown();
  rhi::offscreen_destroy(off);
  dev.shutdown();
}
