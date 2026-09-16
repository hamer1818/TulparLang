// Faz 3/6: glTF yukleme + dokulu cizim. Varlik: tests/assets/checker_cube.gltf
// (make_test_gltf.py, belirlenimli). Cihazda: TULPAR_ENGINE_ASSETS dizini
// (android_run.sh push eder); yoksa GORUNUR atlanir.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "content/gltf.hpp"
#include "content/ktx2.hpp"

#include <stb_image.h>
#include "content/cluster_dag.hpp"
#include "content/font.hpp"
#include "content/meshopt.hpp"
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
  if (test::gpu_is_virtual(dev.caps().device_name)) { dev.shutdown(); skip("sanal GPU (Apple Paravirtual, CI macOS): piksel kapisi gercek cihazda olculur"); return; }
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

// meshoptimizer (Faz 6 dilimi): yuklemede vertex cache/overdraw/fetch iyilesir
// (ACMR kotulesmez, overdraw kotulesmez), ayrik LOD'lar hedefe iner (%50/%25,
// bagil hata siniri altinda) ve LOD2 uzaktan ayni siluetti verir (offscreen
// kapli piksel farki < %3). POZITIF KONTROL: hata siniri cok kucukken
// sadelestirici hedefe INEMEZ (sinir gercekten calisiyor). Kamera uzakligina
// gore secim: yakin LOD0, uzak LOD2.
ENGINE_TEST(content_meshopt_lods_keep_silhouette) {
  static SystemArena sys;
  if (!sys.reserve(96u << 20, "content_lod")) { CHECK(false); return; }
  char path[512];
  const char *env = std::getenv("TULPAR_ENGINE_ASSETS");
  if (env) std::snprintf(path, sizeof path, "%s/lod_sphere.gltf", env);
  else std::snprintf(path, sizeof path, "%s/tests/assets/lod_sphere.gltf", ENGINE_SOURCE_DIR);
  static content::Model m;
  bool loaded = content::gltf_load(sys, path, &m);
  if (!loaded) std::printf("    [bilgi] glTF: %s\n", m.error);
  CHECK(loaded);
  if (!loaded || m.mesh_count == 0) return;
  const content::ModelMesh &mm = m.meshes[0];
  std::printf("    [bilgi] kure: %u vertex (yuklenen %u), %u ucgen; ACMR %.3f -> %.3f, overdraw %.3f -> %.3f\n",
              mm.vertex_count, m.opt.vertices_before, mm.index_count / 3, m.opt.acmr_before, m.opt.acmr_after,
              m.opt.overdraw_before, m.opt.overdraw_after);
  std::printf("    [bilgi] LOD: %u -> %u (hata %.4f) -> %u (hata %.4f)\n", mm.index_count, mm.lod_index_count[0], mm.lod_error[0],
              mm.lod_index_count[1], mm.lod_error[1]);
  CHECK(m.opt.acmr_after <= m.opt.acmr_before + 1e-4f);
  CHECK(m.opt.overdraw_after <= m.opt.overdraw_before + 0.02f);
  CHECK(mm.vertex_count <= m.opt.vertices_before);
  CHECK(mm.lod_index_count[0] > 0 && mm.lod_index_count[0] <= mm.index_count * 55 / 100);
  CHECK(mm.lod_index_count[1] > 0 && mm.lod_index_count[1] <= mm.index_count * 30 / 100);
  CHECK(mm.lod_error[0] <= 0.05f && mm.lod_error[1] <= 0.05f);

  // POZITIF KONTROL: hata 0.0001 ile %25 hedefe inilemez.
  content::ModelMesh strict = mm;
  const float ratios[1] = {0.25f};
  CHECK(content::mesh_build_lods(sys, strict, ratios, 1, 0.0001f));
  std::printf("    [bilgi] pozitif kontrol (hata 0.0001): %u indeks (hedef %u; ulasilamamali)\n", strict.lod_index_count[0],
              mm.index_count / 4);
  bool bound_matters = strict.lod_index_count[0] == 0 || strict.lod_index_count[0] > mm.index_count * 30 / 100;
  CHECK(bound_matters);

  // Siluet: LOD0 ve LOD2 ayni kamerayla; kapli piksel sayisi %3 icinde.
  if (!rhi::vk_api_load(g_api)) { skip("Vulkan loader yok (siluet karsilastirmasi atlandi)"); return; }
  rhi::Device dev;
  rhi::DeviceConfig dc;
  if (!dev.init(sys, g_api, dc)) { skip("Vulkan cihazi yok"); return; }
  if (test::gpu_is_virtual(dev.caps().device_name)) { dev.shutdown(); skip("sanal GPU (Apple Paravirtual, CI macOS): piksel kapisi gercek cihazda olculur"); return; }
  const uint32_t W = 160, H = 160;
  rhi::OffscreenConfig oc;
  oc.srgb = true;
  oc.width = W; oc.height = H;
  oc.clear[0] = oc.clear[1] = oc.clear[2] = 0;
  rhi::OffscreenResult ores;
  rhi::OffscreenTarget *off = rhi::offscreen_create(dev, sys, oc, &ores);
  if (!off) { CHECK(false); dev.shutdown(); return; }
  renderer::Renderer ren;
  renderer::RendererConfig rc;
  rc.frames_in_flight = 1;
  rc.shadow_size = 0;
  if (!ren.init(dev, sys, rhi::offscreen_render_pass(off), rc)) { CHECK(false); rhi::offscreen_destroy(off); dev.shutdown(); return; }
  static content::UploadedModel up;
  CHECK(content::upload_model(ren, sys, m, &up));
  ren.set_light({0, 1, 0}, {1, 1, 1}, 0.0f);
  ren.set_camera(Mat4::look_at({0, 0, 4.0f}, {0, 0, 0}, {0, 1, 0}), Mat4::perspective(0.8f, 1.0f, 0.1f, 50.0f));
  ren.set_render_size(W, H);
  Rec rr{&ren};
  uint32_t covered[2] = {0, 0};
  uint32_t counts[2][content::kModelMaxLods + 1] = {};
  for (int pass = 0; pass < 2; pass++) {
    ren.begin_frame(0);
    content::ModelLod lod;
    lod.camera_pos = pass == 0 ? Vec3{0, 0, 4.0f} : Vec3{0, 0, 100.0f}; // 2. gecis: "uzak" -> LOD2 secilir
    content::draw_model(ren, m, up, Mat4::identity(), {1, 1, 1}, &lod, counts[pass]);
    if (!rhi::offscreen_render_custom(off, oc, rec_main, &rr, &ores, rec_shadow)) { CHECK(false); break; }
    for (uint32_t i = 0; i < W * H; i++) if (ores.pixels[i * 4] + ores.pixels[i * 4 + 1] + ores.pixels[i * 4 + 2] > 30) covered[pass]++;
  }
  std::printf("    [bilgi] siluet: LOD0 %u px (secim %u/%u/%u), LOD2 %u px (secim %u/%u/%u)\n", covered[0], counts[0][0], counts[0][1],
              counts[0][2], covered[1], counts[1][0], counts[1][1], counts[1][2]);
  CHECK(counts[0][0] == 1 && counts[1][2] == 1);
  const int diff = (int)covered[0] - (int)covered[1];
  bool silhouette_kept = covered[0] > W * H / 20 && (diff < 0 ? -diff : diff) * 100 < (int)covered[0] * 3;
  CHECK(silhouette_kept);
  ren.shutdown();
  rhi::offscreen_destroy(off);
  dev.shutdown();
}

// glTF iskelet + animasyon (kendi ACL-sinifi runtime'imiz; ozz vendored DEGIL —
// sampling/to_model zaten var, ozz yalniz karistirma/IK gerekince): 2 eklemli boru,
// "bend" klibi uc eklemi 1 s'de Z etrafinda 90 derece dondurur. Kapilar:
// (1) ice aktarma: 2 eklem (ebeveyn -1, 0), 1 klip ~1 s, mesh iskeletli;
// (2) CPU skinning: tepe vertex t=0'da (~0,2,0), t=1'de (~-1,1,0) — analitik;
// (3) GPU: offscreen t=0 (dik) ve t=1 (bukuk) siluetleri: bukukte genislik >2x,
//     tepe daha asagida. Iki durum farkli olmali (kontrol iki karede birden).
ENGINE_TEST(content_skinned_gltf_bends) {
  static SystemArena sys;
  if (!sys.reserve(96u << 20, "content_skin")) { CHECK(false); return; }
  char path[512];
  if (!asset_path(path, sizeof path, "skin_tube.gltf")) { CHECK(false); std::printf("    [bilgi] varlik yok: %s\n", path); return; }
  static content::Model m;
  bool loaded = content::gltf_load(sys, path, &m);
  if (!loaded) std::printf("    [bilgi] glTF: %s\n", m.error);
  CHECK(loaded);
  if (!loaded) return;
  std::printf("    [bilgi] iskelet %u (eklem %u: ebeveyn %d,%d), klip %u (%s %.2f s, %zu -> %zu bayt, sabit iz %u, donus hatasi %.3f deg)\n",
              m.skin_count, m.skin_count ? m.skins[0].joint_count : 0, m.skin_count ? m.skins[0].joints[0].parent : 9,
              m.skin_count && m.skins[0].joint_count > 1 ? m.skins[0].joints[1].parent : 9, m.clip_count,
              m.clip_count ? m.clips[0].name : "-", m.clip_count ? m.clips[0].duration : 0.f,
              m.clip_count ? m.clips[0].stats.raw_bytes : (size_t)0, m.clip_count ? m.clips[0].stats.compressed_bytes : (size_t)0,
              m.clip_count ? m.clips[0].stats.const_tracks : 0u, m.clip_count ? m.clips[0].stats.max_rot_error_deg : 0.f);
  CHECK(m.skin_count == 1 && m.skins[0].joint_count == 2);
  CHECK(m.skins[0].joints[0].parent == -1 && m.skins[0].joints[1].parent == 0);
  CHECK(m.clip_count == 1 && m.clips[0].duration > 0.99f && m.clips[0].duration < 1.01f);
  CHECK(m.mesh_count == 1 && m.meshes[0].skin == 0 && m.meshes[0].skin_verts != nullptr);
  if (m.skin_count != 1 || m.clip_count != 1 || m.mesh_count != 1 || m.meshes[0].skin != 0) return;

  // (2) CPU skinning: en yuksek vertex.
  static content::PoseScratch scratch;
  const content::ModelMesh &mm = m.meshes[0];
  uint32_t top = 0;
  for (uint32_t i = 1; i < mm.vertex_count; i++) if (mm.verts[i].pos.y > mm.verts[top].pos.y) top = i;
  auto skin_vertex = [&](float t) {
    content::ModelPose pose;
    CHECK(content::model_pose_evaluate(m, 0, t, scratch, &pose));
    const renderer::SkinnedVertex &sv = mm.skin_verts[top];
    Vec4 acc{0, 0, 0, 0};
    for (int c = 0; c < 4; c++) {
      const float w = (float)sv.weights[c] / 65535.0f;
      if (w <= 0) continue;
      Vec4 r = pose.skin_mats[0][sv.joints[c]] * Vec4{sv.pos.x, sv.pos.y, sv.pos.z, 1.0f};
      acc = acc + r * w;
    }
    return Vec3{acc.x, acc.y, acc.z};
  };
  const Vec3 p0 = skin_vertex(0.0f), p1 = skin_vertex(1.0f);
  std::printf("    [bilgi] tepe vertex: t=0 (%.3f, %.3f, %.3f) -> t=1 (%.3f, %.3f, %.3f); beklenen (~0,2,z) -> (~-1,1,z)\n", p0.x, p0.y,
              p0.z, p1.x, p1.y, p1.z);
  bool rest_ok = std::fabs(p0.y - 2.0f) < 0.01f && std::fabs(p0.x) < 0.2f;
  bool bent_ok = std::fabs(p1.x + 1.0f) < 0.2f && std::fabs(p1.y - 1.0f) < 0.2f;
  CHECK(rest_ok);
  CHECK(bent_ok);

  // (3) GPU skinning: siluet.
  if (!rhi::vk_api_load(g_api)) { skip("Vulkan loader yok (GPU skinning atlandi)"); return; }
  rhi::Device dev;
  rhi::DeviceConfig dc;
  if (!dev.init(sys, g_api, dc)) { skip("Vulkan cihazi yok"); return; }
  if (test::gpu_is_virtual(dev.caps().device_name)) { dev.shutdown(); skip("sanal GPU (Apple Paravirtual, CI macOS): piksel kapisi gercek cihazda olculur"); return; }
  const uint32_t W = 128, H = 128;
  rhi::OffscreenConfig oc;
  oc.srgb = true;
  oc.width = W; oc.height = H;
  oc.clear[0] = oc.clear[1] = oc.clear[2] = 0;
  rhi::OffscreenResult ores;
  rhi::OffscreenTarget *off = rhi::offscreen_create(dev, sys, oc, &ores);
  if (!off) { CHECK(false); dev.shutdown(); return; }
  renderer::Renderer ren;
  renderer::RendererConfig rc;
  rc.frames_in_flight = 1;
  rc.shadow_size = 512; // golge boru hatti da iskeletli olmali (cizim hatasi vermemeli)
  if (!ren.init(dev, sys, rhi::offscreen_render_pass(off), rc)) { CHECK(false); rhi::offscreen_destroy(off); dev.shutdown(); return; }
  static content::UploadedModel up;
  CHECK(content::upload_model(ren, sys, m, &up));
  ren.set_light({0, 1, 0}, {1, 1, 1}, 0.0f);
  ren.set_camera(Mat4::look_at({0, 1.0f, 6.0f}, {0, 1.0f, 0}, {0, 1, 0}), Mat4::ortho(-2, 2, -2, 2, 0.1f, 20.0f));
  ren.set_render_size(W, H);
  Rec rr{&ren};
  int minx[2] = {W, W}, maxx[2] = {-1, -1}, miny[2] = {H, H}, maxy[2] = {-1, -1};
  for (int pass = 0; pass < 2; pass++) {
    content::ModelPose pose;
    CHECK(content::model_pose_evaluate(m, 0, pass == 0 ? 0.0f : 1.0f, scratch, &pose));
    ren.begin_frame(0);
    content::draw_model(ren, m, up, Mat4::identity(), {1, 1, 1}, nullptr, nullptr, &pose);
    CHECK(ren.stats().dropped == 0);
    if (!rhi::offscreen_render_custom(off, oc, rec_main, &rr, &ores, rec_shadow)) { CHECK(false); break; }
    for (uint32_t y = 0; y < H; y++)
      for (uint32_t x = 0; x < W; x++) {
        const uint8_t *px = ores.pixels + (y * W + x) * 4;
        if (px[0] + px[1] + px[2] > 30) {
          if ((int)x < minx[pass]) minx[pass] = (int)x;
          if ((int)x > maxx[pass]) maxx[pass] = (int)x;
          if ((int)y < miny[pass]) miny[pass] = (int)y;
          if ((int)y > maxy[pass]) maxy[pass] = (int)y;
        }
      }
  }
  const int w0 = maxx[0] - minx[0] + 1, w1 = maxx[1] - minx[1] + 1;
  const int h0 = maxy[0] - miny[0] + 1, h1 = maxy[1] - miny[1] + 1;
  std::printf("    [bilgi] siluet: dik %dx%d (x %d..%d, y %d..%d), bukuk %dx%d (x %d..%d, y %d..%d)\n", w0, h0, minx[0], maxx[0],
              miny[0], maxy[0], w1, h1, minx[1], maxx[1], miny[1], maxy[1]);
  bool drawn = maxx[0] >= 0 && maxx[1] >= 0;
  CHECK(drawn);
  bool bends_wider = w1 > w0 * 2;          // ust yari sola yatar
  bool bends_shorter = h1 < h0;            // tepe asagi iner
  bool bends_left = minx[1] < minx[0] - 10; // sol kenar sola kayar (Z etrafinda + donus = sola)
  CHECK(bends_wider);
  CHECK(bends_shorter);
  CHECK(bends_left);
  ren.shutdown();
  rhi::offscreen_destroy(off);
  dev.shutdown();
}

// KTX2 + ASTC (Faz 6 doku yolu): engine_texpack'in urettigi checker_64.ktx2 (4x4
// sRGB, 7 mip) okunur; (1) CPU cozumu kaynak PNG'ye PSNR > 35 dB (belirlenimli,
// her platformda ayni); (2) GPU'ya yuklenir — ASTC LDR varsa donanim (Mali),
// yoksa CPU cozumu (masaustu) — 1:1 cizilip geri okunur, PSNR > 30 dB; hangi
// yol raporlanir. POZITIF KONTROLLER: kimligi bozuk dosya reddedilir; PSNR
// metrigi kaydirilmis goruntude dusuk (< 20 dB) — olcum ayirt edici.
ENGINE_TEST(content_ktx2_astc_decodes_and_uploads) {
  static SystemArena sys;
  if (!sys.reserve(64u << 20, "content_ktx2")) { CHECK(false); return; }
  char kpath[512], ppath[512];
  if (!asset_path(kpath, sizeof kpath, "checker_64.ktx2") || !asset_path(ppath, sizeof ppath, "checker_64.png")) { CHECK(false); return; }
  // Kaynak PNG
  FILE *f = std::fopen(ppath, "rb");
  CHECK(f != nullptr);
  if (!f) return;
  std::fseek(f, 0, SEEK_END); const long sz = std::ftell(f); std::fseek(f, 0, SEEK_SET);
  uint8_t *file = sys.alloc_array<uint8_t>((uint32_t)sz);
  const size_t got = std::fread(file, 1, (size_t)sz, f);
  std::fclose(f);
  int w = 0, h = 0, comp = 0;
  stbi_uc *png = stbi_load_from_memory(file, (int)got, &w, &h, &comp, 4);
  CHECK(png != nullptr && w == 64 && h == 64);
  if (!png) return;
  content::Ktx2Image img;
  bool ok = content::ktx2_load(sys, kpath, &img);
  if (!ok) std::printf("    [bilgi] ktx2: %s\n", img.error);
  CHECK(ok);
  if (!ok) return;
  std::printf("    [bilgi] ktx2: vkFormat %u, %ux%u, %u seviye, blok %ux%u, sRGB %d, seviye0 %u bayt\n", (unsigned)img.vk_format, img.width,
              img.height, img.levels, img.block_w, img.block_h, (int)img.srgb, img.level_size[0]);
  CHECK(img.vk_format == VK_FORMAT_ASTC_4x4_SRGB_BLOCK && img.width == 64 && img.levels == 7 && img.level_size[0] == 4096);
  // (1) CPU cozumu
  uint8_t *dec = nullptr;
  CHECK(content::astc_decode_rgba(sys, img.level_data[0], img.level_size[0], 64, 64, 4, 4, true, &dec));
  const double psnr_cpu = dec ? content::rgba_psnr(png, dec, 64, 64) : 0;
  // Kontrol: 8 px kaydirilmis dama (blok sinirlari kayar) dusuk PSNR vermeli.
  static uint8_t shifted[64 * 64 * 4];
  for (uint32_t y = 0; y < 64; y++) for (uint32_t x = 0; x < 64; x++) std::memcpy(shifted + (y * 64 + x) * 4, png + (y * 64 + ((x + 8) % 64)) * 4, 4);
  const double psnr_ctrl = content::rgba_psnr(png, shifted, 64, 64);
  std::printf("    [bilgi] CPU cozumu PSNR %.1f dB (kontrol: kaydirilmis %.1f dB)\n", psnr_cpu, psnr_ctrl);
  CHECK(psnr_cpu > 35.0);
  CHECK(psnr_ctrl < 20.0);
  // Bozuk kimlik reddedilir.
  uint8_t bad[96] = {0};
  content::Ktx2Image junk;
  CHECK(!content::ktx2_parse(sys, bad, sizeof bad, &junk));
  // (2) GPU
  if (!rhi::vk_api_load(g_api)) { skip("Vulkan loader yok (GPU yolu atlandi)"); stbi_image_free(png); return; }
  rhi::Device dev;
  rhi::DeviceConfig dc;
  if (!dev.init(sys, g_api, dc)) { skip("Vulkan cihazi yok"); stbi_image_free(png); return; }
  const uint32_t W = 64, H = 64;
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
  content::Ktx2UploadInfo ui;
  renderer::TextureHandle th = content::ktx2_upload(ren, dev.caps(), sys, img, &ui);
  CHECK(th.valid());
  renderer::MaterialHandle mat = ren.create_material(th, {1, 1, 1});
  renderer::Vertex v[4];
  uint32_t idx[6];
  uint32_t n = renderer::Renderer::plane(v, idx);
  renderer::MeshHandle plane = ren.create_mesh(v, 4, idx, n);
  ren.set_light({0, 1, 0}, {1, 1, 1}, 0.0f);
  ren.set_shadows_enabled(false);
  // Ustten ortografik, 64 px = 64 texel (1:1): plane 64 birim, kamera y+.
  ren.set_camera(Mat4::look_at({0, 10.0f, 0}, {0, 0, 0}, {0, 0, -1}), Mat4::ortho(-32, 32, -32, 32, 0.1f, 50.0f));
  ren.set_render_size(W, H);
  ren.begin_frame(0);
  ren.draw(plane, mat, Mat4::scale({64, 1, 64}), {1, 1, 1});
  Rec rr{&ren};
  bool drawn = rhi::offscreen_render_custom(off, oc, rec_main, &rr, &ores, rec_shadow);
  CHECK(drawn);
  double psnr_gpu = 0;
  if (drawn) {
    // Yonelim: piksel satiri/sutunu texel'e dogrudan denk gelmeyebilir (uv yonu);
    // dama 8 px periyotlu ve simetrik: 4 yonelimin en iyisi alinir (bilgi basilir).
    static uint8_t reor[64 * 64 * 4];
    double best = 0; int best_k = 0;
    for (int k = 0; k < 4; k++) {
      for (uint32_t y = 0; y < 64; y++) for (uint32_t x = 0; x < 64; x++) {
        uint32_t sx = x, sy = y;
        if (k == 1) sx = 63 - x; else if (k == 2) sy = 63 - y; else if (k == 3) { sx = 63 - x; sy = 63 - y; }
        std::memcpy(reor + (y * 64 + x) * 4, ores.pixels + (sy * 64 + sx) * 4, 4);
      }
      const double p = content::rgba_psnr(png, reor, 64, 64);
      if (p > best) { best = p; best_k = k; }
    }
    psnr_gpu = best;
    std::printf("    [bilgi] GPU yolu: %s (%u seviye), geri okuma PSNR %.1f dB (yonelim %d)\n", ui.hardware ? "donanim ASTC" : "CPU cozumu -> RGBA8",
                ui.levels, psnr_gpu, best_k);
  }
  CHECK(psnr_gpu > 30.0);
  ren.shutdown();
  rhi::offscreen_destroy(off);
  dev.shutdown();
  stbi_image_free(png);
}

// ===========================================================================
// Faz 9 — KUME (cluster) DAG (content/cluster_dag.hpp). Kapilar CPU tarafinda:
// GPU cull + indirect renderer'in isi, burada olculen VERI.
// Test mesh'i PROSEDUREL uretiliyor (yeni varlik dosyasi yok, tamamen
// belirlenimli): 64x64 kareli yukseklik alani = 8192 ucgen — DAG'in birkac
// seviye derinlesmesi icin yeterince yogun.
// ===========================================================================
namespace {
// Belirlenimli yukseklik alani: [-1,1]^2 XZ, yukseklik iki sinusun carpimi.
bool make_grid_mesh(Arena &a, uint32_t n, content::ModelMesh *out, float bump = 1.0f) {
  const uint32_t vn = (n + 1) * (n + 1), tn = n * n * 2;
  renderer::Vertex *v = a.alloc_array_zeroed<renderer::Vertex>(vn);
  uint32_t *idx = a.alloc_array<uint32_t>(tn * 3);
  if (!v || !idx) return false;
  for (uint32_t z = 0; z <= n; z++)
    for (uint32_t x = 0; x <= n; x++) {
      const float fx = (float)x / (float)n * 2.0f - 1.0f, fz = (float)z / (float)n * 2.0f - 1.0f;
      // sinus yerine polinom: libm surumune bagli olmayan bit-esit yukseklik.
      const float h = bump * 0.35f * (1.0f - fx * fx) * (1.0f - fz * fz) * (1.0f + 0.5f * fx * fz);
      v[z * (n + 1) + x].pos = {fx, h, fz};
      v[z * (n + 1) + x].nrm = {0, 1, 0};
      v[z * (n + 1) + x].uv = {(float)x / (float)n, (float)z / (float)n};
    }
  uint32_t k = 0;
  for (uint32_t z = 0; z < n; z++)
    for (uint32_t x = 0; x < n; x++) {
      const uint32_t a0 = z * (n + 1) + x, b0 = a0 + 1, c0 = a0 + (n + 1), d0 = c0 + 1;
      idx[k++] = a0; idx[k++] = c0; idx[k++] = b0;
      idx[k++] = b0; idx[k++] = c0; idx[k++] = d0;
    }
  *out = content::ModelMesh{};
  out->verts = v; out->vertex_count = vn;
  out->indices = idx; out->index_count = k;
  return true;
}
// Kapali UV kure: son sutun ilk sutunun vertex'lerini kullanir ve kutuplar tek
// vertex — ayni pozisyondan iki kopya YOK, mesh su gecirmez (acik kenar 0).
bool make_closed_sphere(Arena &a, uint32_t S, uint32_t R, content::ModelMesh *out) {
  const uint32_t vn = 2 + (R - 1) * S, tn = 2 * S + (R - 2) * S * 2;
  renderer::Vertex *v = a.alloc_array_zeroed<renderer::Vertex>(vn);
  uint32_t *idx = a.alloc_array<uint32_t>(tn * 3);
  if (!v || !idx) return false;
  auto ring = [&](uint32_t r, uint32_t s) { return 2 + (r - 1) * S + (s % S); };
  v[0].pos = {0, 1, 0}; v[0].nrm = {0, 1, 0};
  v[1].pos = {0, -1, 0}; v[1].nrm = {0, -1, 0};
  for (uint32_t r = 1; r < R; r++) {
    const float th = 3.14159265358979f * (float)r / (float)R;
    for (uint32_t s = 0; s < S; s++) {
      const float ph = 6.28318530717959f * (float)s / (float)S;
      const Vec3 p{std::sin(th) * std::cos(ph), std::cos(th), std::sin(th) * std::sin(ph)};
      v[ring(r, s)].pos = p;
      v[ring(r, s)].nrm = p;
    }
  }
  uint32_t k = 0;
  for (uint32_t s = 0; s < S; s++) { idx[k++] = 0; idx[k++] = ring(1, s); idx[k++] = ring(1, s + 1); }
  for (uint32_t r = 1; r + 1 < R; r++)
    for (uint32_t s = 0; s < S; s++) {
      const uint32_t a0 = ring(r, s), b0 = ring(r, s + 1), c0 = ring(r + 1, s), d0 = ring(r + 1, s + 1);
      idx[k++] = a0; idx[k++] = c0; idx[k++] = b0;
      idx[k++] = b0; idx[k++] = c0; idx[k++] = d0;
    }
  for (uint32_t s = 0; s < S; s++) { idx[k++] = 1; idx[k++] = ring(R - 1, s + 1); idx[k++] = ring(R - 1, s); }
  *out = content::ModelMesh{};
  out->verts = v; out->vertex_count = vn;
  out->indices = idx; out->index_count = k;
  return true;
}
void dag_info(const char *tag, const content::ClusterDag &d) {
  std::printf("    [bilgi] %s: %u kume, %u seviye, %u grup, %u kok, ozet %016llx\n", tag, d.node_count, d.levels, d.group_count,
              d.root_count, (unsigned long long)d.hash);
  for (uint32_t l = 0; l < d.levels; l++)
    std::printf("    [bilgi]   seviye %u: %u kume, %u ucgen, en buyuk hata %.6f\n", l, d.level_count[l], d.level_tris[l],
                (double)d.level_error[l]);
}
// Noktadan ucgene en kisa uzaklik (Ericson, Real-Time Collision Detection).
float point_tri_dist(Vec3 p, Vec3 a, Vec3 b, Vec3 c) {
  const Vec3 ab = b - a, ac = c - a, ap = p - a;
  const float d1 = dot(ab, ap), d2 = dot(ac, ap);
  if (d1 <= 0 && d2 <= 0) return length(ap);
  const Vec3 bp = p - b;
  const float d3 = dot(ab, bp), d4 = dot(ac, bp);
  if (d3 >= 0 && d4 <= d3) return length(bp);
  const float vc = d1 * d4 - d3 * d2;
  if (vc <= 0 && d1 >= 0 && d3 <= 0) return length(ap - ab * (d1 / (d1 - d3)));
  const Vec3 cp = p - c;
  const float d5 = dot(ab, cp), d6 = dot(ac, cp);
  if (d6 >= 0 && d5 <= d6) return length(cp);
  const float vb = d5 * d2 - d1 * d6;
  if (vb <= 0 && d2 >= 0 && d6 <= 0) return length(ap - ac * (d2 / (d2 - d6)));
  const float va = d3 * d6 - d5 * d4;
  if (va <= 0 && (d4 - d3) >= 0 && (d5 - d6) >= 0) return length(p - (b + (c - b) * ((d4 - d3) / ((d4 - d3) + (d5 - d6)))));
  const float den = 1.0f / (va + vb + vc);
  return length(p - (a + ab * (vb * den) + ac * (vc * den)));
}
// Seviyedeki ucgenlerden ornek noktalar (kose + agirlik merkezi) alip ORIJINAL
// mesh'e en kisa uzakligi olcer (kaba kuvvet; Hausdorff yerine ornekleme).
float level_deviation(const content::ClusterDag &d, const content::ModelMesh &mm, uint32_t level, uint32_t max_samples, float *mean) {
  float worst = 0, sum = 0;
  uint32_t taken = 0, tri_total = d.level_tris[level] ? d.level_tris[level] : 1;
  const uint32_t stride = tri_total > max_samples ? tri_total / max_samples + 1 : 1;
  uint32_t seen = 0;
  for (uint32_t i = 0; i < d.level_count[level]; i++) {
    const content::ClusterNode &nd = d.nodes[d.level_first[level] + i];
    for (uint32_t t = 0; t < nd.index_count; t += 3, seen++) {
      if (seen % stride) continue;
      const Vec3 p0 = mm.verts[d.indices[nd.index_offset + t]].pos, p1 = mm.verts[d.indices[nd.index_offset + t + 1]].pos,
                 p2 = mm.verts[d.indices[nd.index_offset + t + 2]].pos;
      const Vec3 samples[2] = {(p0 + p1 + p2) * (1.0f / 3.0f), (p0 + p1) * 0.5f};
      for (int s = 0; s < 2; s++) {
        float best = 1e30f;
        for (uint32_t o = 0; o < mm.index_count; o += 3) {
          const float dd = point_tri_dist(samples[s], mm.verts[mm.indices[o]].pos, mm.verts[mm.indices[o + 1]].pos,
                                          mm.verts[mm.indices[o + 2]].pos);
          if (dd < best) best = dd;
        }
        if (best > worst) worst = best;
        sum += best;
        taken++;
      }
    }
  }
  if (mean) *mean = taken ? sum / (float)taken : 0.0f;
  return worst;
}
} // namespace

// KAPI (planin kendi kapisi): "cut gecislerinde crack yok". Olculen sey:
// iki komsu GRUBUN paylastigi kenarlar sadelestirmeden sonra duruyor mu.
// Kenar kilidi calisiyorsa kayip 0 olmali. KONTROL: kilit kapaliyken AYNI
// olcum kayip GORMELI — gormezse kapi bir sey olcmuyordur.
ENGINE_TEST(content_cluster_dag_locks_group_borders) {
  static SystemArena sys;
  if (!sys.reserve(128u << 20, "dag_seam")) { CHECK(false); return; }
  content::ModelMesh mm;
  CHECK(make_grid_mesh(sys, 64, &mm));
  std::printf("    [bilgi] izgara mesh: %u vertex, %u ucgen\n", mm.vertex_count, mm.index_count / 3);

  content::ClusterDagOptions opt = content::cluster_dag_preset(content::DeviceClass::Mid);
  content::ClusterDag dag;
  const bool built = content::cluster_dag_build(sys, mm, opt, &dag);
  CHECK(built);
  if (!built) return;
  dag_info("kilitli DAG", dag);
  CHECK(dag.levels >= 3);
  CHECK(dag.node_count > 32);
  // Kume buyuklugu plan araligi (~64-128 ucgen): seviye 0'da ortalama.
  const uint32_t avg0 = dag.level_tris[0] / (dag.level_count[0] ? dag.level_count[0] : 1);
  std::printf("    [bilgi] seviye 0 kume basina ortalama ucgen: %u (hedef <= %u)\n", avg0, opt.max_triangles);
  CHECK(avg0 >= 32 && avg0 <= opt.max_triangles);

  content::ClusterDagSeamReport rep;
  CHECK(content::cluster_dag_check_seams(dag, mm, &rep));
  std::printf("    [bilgi] KILITLI: %u seviye gecisi, %u sadelestirilmis grup, %u grup sinir kenari, KAYIP %u; sinir vertex %u, kayip %u\n",
              rep.levels_checked, rep.groups_simplified, rep.border_edges, rep.border_edges_lost, rep.border_verts,
              rep.border_verts_lost);
  CHECK(rep.border_edges > 100);     // olculecek sinir var
  CHECK(rep.groups_simplified >= 4); // birden fazla grup gercekten sadelesti
  CHECK(rep.border_edges_lost == 0); // CATLAK YOK
  CHECK(rep.border_verts_lost == 0);

  // KONTROL: kilit kapali -> ayni olcum catlak gormeli.
  content::ClusterDagOptions un = opt;
  un.lock_group_border = false;
  content::ClusterDag dag_un;
  CHECK(content::cluster_dag_build(sys, mm, un, &dag_un));
  content::ClusterDagSeamReport rep_un;
  CHECK(content::cluster_dag_check_seams(dag_un, mm, &rep_un));
  std::printf("    [bilgi] KONTROL (kilit kapali): %u grup sinir kenari, KAYIP %u; sinir vertex %u, kayip %u\n", rep_un.border_edges,
              rep_un.border_edges_lost, rep_un.border_verts, rep_un.border_verts_lost);
  CHECK(rep_un.border_edges > 100);
  CHECK(rep_un.border_edges_lost > 0); // kontrol: kilit olmadan catlak OLUSUR

  // Ayni olcum gercek varlikta (dikis vertex'li UV kure): pozisyon temsilcisi
  // dikisleri birlestirmezse burada yanlis catlak sayilirdi.
  char path[512];
  if (asset_path(path, sizeof path, "lod_sphere.gltf")) {
    static content::Model m;
    if (content::gltf_load(sys, path, &m) && m.mesh_count) {
      content::ClusterDag sd;
      if (content::cluster_dag_build(sys, m.meshes[0], opt, &sd)) {
        dag_info("kure DAG", sd);
        content::ClusterDagSeamReport sr;
        CHECK(content::cluster_dag_check_seams(sd, m.meshes[0], &sr));
        std::printf("    [bilgi] kure: %u sinir kenari, KAYIP %u\n", sr.border_edges, sr.border_edges_lost);
        CHECK(sr.border_edges > 50);
        CHECK(sr.border_edges_lost == 0);
        // Yukleyici yolu (GltfLimits::cluster_dag): model'e bake edilen DAG
        // dogrudan kurulanla AYNI olmali (ayni onayar, ayni baytlar).
        static content::Model m2;
        content::GltfLimits lim;
        lim.cluster_dag = true;
        lim.cluster_class = 1; // orta
        if (content::gltf_load(sys, path, &m2, lim) && m2.mesh_count) {
          const bool baked = m2.meshes[0].dag != nullptr;
          std::printf("    [bilgi] yukleyici bake'i: %s%s\n", baked ? "var" : "YOK",
                      baked ? (m2.meshes[0].dag->hash == sd.hash ? " (ozet ayni)" : " (ozet FARKLI)") : "");
          CHECK(baked);
          CHECK(baked && m2.meshes[0].dag->hash == sd.hash);
          CHECK(baked && m2.meshes[0].lod_index_count[0] > 0); // ayrik LOD'lar hala duruyor
        }
      }
    }
  }
}

// KAPI: hata metrigi ANLAMLI mi — ust seviyeye cikildikca (1) dugumlerdeki
// hata artmali ve (2) gercek geometri sapmasi da artmali ama SINIRLI kalmali.
// Yontem: her seviyenin ucgenlerinden ornek noktalar (agirlik merkezi + kenar
// ortasi) alinip ORIJINAL mesh'e en kisa uzaklik kaba kuvvetle olculuyor
// (Hausdorff degil, ornekleme — alt sinir; yeterli cunku artisi ve sinirliligi
// gosteriyor). KONTROL: mutlak hata tavani kisilinca agac sigleseıyor, yani
// sadelestirmeyi gercekten bu metrik durduruyor.
ENGINE_TEST(content_cluster_dag_error_bounds_deviation) {
  static SystemArena sys;
  if (!sys.reserve(128u << 20, "dag_error")) { CHECK(false); return; }
  content::ModelMesh mm;
  CHECK(make_grid_mesh(sys, 48, &mm));
  content::ClusterDagOptions opt = content::cluster_dag_preset(content::DeviceClass::Mid);
  content::ClusterDag dag;
  CHECK(content::cluster_dag_build(sys, mm, opt, &dag));
  if (dag.levels < 3) { CHECK(false); return; }
  Vec3 lo{1e30f, 1e30f, 1e30f}, hi{-1e30f, -1e30f, -1e30f};
  for (uint32_t v = 0; v < mm.vertex_count; v++) { lo = vmin(lo, mm.verts[v].pos); hi = vmax(hi, mm.verts[v].pos); }
  const float diag = length(hi - lo);
  float dev[content::kClusterDagMaxLevels] = {};
  bool error_grows = true, dev_grows = true;
  for (uint32_t l = 0; l < dag.levels; l++) {
    float mean = 0;
    dev[l] = level_deviation(dag, mm, l, 96, &mean);
    std::printf("    [bilgi] seviye %u: %u ucgen, bildirilen hata %.6f, olculen sapma en buyuk %.6f / ortalama %.6f (oran %.2f)\n", l,
                dag.level_tris[l], (double)dag.level_error[l], (double)dev[l], (double)mean,
                dag.level_error[l] > 0 ? (double)(dev[l] / dag.level_error[l]) : 0.0);
    if (l && dag.level_error[l] < dag.level_error[l - 1]) error_grows = false;
    if (l && dev[l] < dev[l - 1] - 1e-6f) dev_grows = false;
  }
  std::printf("    [bilgi] mesh kosegeni %.4f; en ust seviye sapmasi %.6f (kosegenin %%%.3f'u)\n", (double)diag,
              (double)dev[dag.levels - 1], (double)(100.0f * dev[dag.levels - 1] / diag));
  // Seviye 0 orijinal geometri: sapma sifir (kayan nokta yuvarlamasi kadar).
  std::printf("    [bilgi] seviye 0 sapmasi %.3e (yuvarlama payi)\n", (double)dev[0]);
  CHECK(dev[0] < 1e-5f);
  CHECK(dag.level_error[0] == 0.0f);
  CHECK(error_grows);                    // bildirilen hata monoton
  CHECK(dev_grows);                      // olculen sapma da monoton
  CHECK(dev[dag.levels - 1] > dev[1]);   // sapma gercekten buyuyor (olculebilir)
  CHECK(dev[1] > 0.0f);
  CHECK(dev[dag.levels - 1] < 0.05f * diag); // ama sinirli
  // Bildirilen hata sapmayi orter mu (ornekleme + tahmin payi: 4 kat tolerans).
  bool covered = true;
  for (uint32_t l = 1; l < dag.levels; l++)
    if (dev[l] > dag.level_error[l] * 4.0f + 1e-5f) covered = false;
  CHECK(covered);
  // Her dugumde cut kosulu gecerli mi: error < parent_error (esitlik olursa o
  // kume HICBIR esikte secilmez -> delik).
  bool cut_ok = true;
  for (uint32_t i = 0; i < dag.node_count; i++)
    if (!(dag.nodes[i].error < dag.nodes[i].parent_error)) cut_ok = false;
  CHECK(cut_ok);

  // KONTROL: mutlak hata tavani 0.002 -> sadelestirme erken durmali (daha sig agac).
  content::ClusterDagOptions capped = opt;
  capped.max_error = 0.002f;
  content::ClusterDag dag_capped;
  CHECK(content::cluster_dag_build(sys, mm, capped, &dag_capped));
  std::printf("    [bilgi] KONTROL (hata tavani 0.002): %u seviye / %u kume (tavansiz %u / %u)\n", dag_capped.levels,
              dag_capped.node_count, dag.levels, dag.node_count);
  CHECK(dag_capped.levels < dag.levels);
  CHECK(dag_capped.level_error[dag_capped.levels - 1] <= 0.002f + 1e-6f);
}

// KAPI: determinizm sozlesmesi — ayni girdi ayni DAG baytlarini vermeli.
// KONTROL: tek vertex 1 mm oynatilinca baytlar DEGISMELI (yoksa ozet girdiyi
// olcmuyordur). Ayrica cihaz sinifi onayarlari gercekten farkli DAG uretmeli.
ENGINE_TEST(content_cluster_dag_is_deterministic) {
  static SystemArena sys;
  if (!sys.reserve(96u << 20, "dag_det")) { CHECK(false); return; }
  content::ModelMesh mm;
  CHECK(make_grid_mesh(sys, 32, &mm));
  const content::ClusterDagOptions opt = content::cluster_dag_preset(content::DeviceClass::Mid);
  content::ClusterDag a, b;
  CHECK(content::cluster_dag_build(sys, mm, opt, &a));
  CHECK(content::cluster_dag_build(sys, mm, opt, &b));
  const bool same = a.hash == b.hash && a.node_count == b.node_count && a.index_count == b.index_count &&
                    a.child_count == b.child_count &&
                    std::memcmp(a.nodes, b.nodes, sizeof(content::ClusterNode) * a.node_count) == 0 &&
                    std::memcmp(a.indices, b.indices, sizeof(uint32_t) * a.index_count) == 0;
  std::printf("    [bilgi] iki kurulum: ozet %016llx / %016llx, %u kume, %u indeks — bayt esit %s\n", (unsigned long long)a.hash,
              (unsigned long long)b.hash, a.node_count, a.index_count, same ? "EVET" : "HAYIR");
  CHECK(same);

  // KONTROL: tek vertex oynadi -> ozet degismeli.
  const Vec3 keep = mm.verts[mm.vertex_count / 2].pos;
  mm.verts[mm.vertex_count / 2].pos = keep + Vec3{0, 0.001f, 0};
  content::ClusterDag moved;
  CHECK(content::cluster_dag_build(sys, mm, opt, &moved));
  std::printf("    [bilgi] KONTROL (tek vertex +1 mm): ozet %016llx (%s)\n", (unsigned long long)moved.hash,
              moved.hash != a.hash ? "DEGISTI" : "AYNI KALDI");
  CHECK(moved.hash != a.hash);
  mm.verts[mm.vertex_count / 2].pos = keep;

  // Cihaz sinifi basina bake: ayni mesh, farkli kume buyuklugu / derinlik.
  uint32_t nodes_by_class[3] = {0, 0, 0}, levels_by_class[3] = {0, 0, 0};
  for (uint32_t c = 0; c < 3; c++) {
    content::ClusterDag d;
    const content::DeviceClass dc = c == 0 ? content::DeviceClass::Low : (c == 1 ? content::DeviceClass::Mid : content::DeviceClass::High);
    CHECK(content::cluster_dag_build(sys, mm, content::cluster_dag_preset(dc), &d));
    nodes_by_class[c] = d.node_count;
    levels_by_class[c] = d.levels;
    std::printf("    [bilgi] cihaz sinifi %s: %u kume, %u seviye, seviye 0 ucgen/kume %u\n", content::device_class_name(dc),
                d.node_count, d.levels, d.level_tris[0] / (d.level_count[0] ? d.level_count[0] : 1));
  }
  CHECK(nodes_by_class[2] > nodes_by_class[0]);   // yuksek sinif: daha cok/kucuk kume
  CHECK(levels_by_class[0] <= levels_by_class[1]); // dusuk sinif: daha sig agac
}

// KAPI (planin kapisinin DOGRUDAN hali): "cut gecislerinde crack yok".
// Yukaridaki kapi grup sinirlarini olcer; bu kapi GERCEK CUT'i kurar — esik
// suprulur, her kume `error <= t < parent_error` kuraliyla secilir (seviyeler
// karisir) — ve secilen ucgen kumesinin SU GECIRMEZ oldugu olculur: her ic
// kenar tam iki ucgende gorunmeli. Bir kenar tek ucgende kaliyorsa (ve
// orijinal mesh'in acik kenari degilse) orada CATLAK var demektir.
// KONTROL: kilit kapali kurulan DAG ayni suprumede catlak vermeli.
namespace {
// Kenar listesi: her ucgenin 3 kenari (pozisyon temsilcisi cifti), sirali.
uint32_t cut_edges(const content::ClusterDag &d, const uint32_t *prep, float t, uint64_t *out, uint32_t cap, uint32_t *tris,
                   uint32_t *clusters) {
  uint32_t n = 0;
  *tris = 0;
  *clusters = 0;
  for (uint32_t i = 0; i < d.node_count; i++) {
    const content::ClusterNode &nd = d.nodes[i];
    if (!(nd.error <= t && t < nd.parent_error)) continue;
    (*clusters)++;
    for (uint32_t k = 0; k < nd.index_count; k += 3) {
      (*tris)++;
      const uint32_t a = prep[d.indices[nd.index_offset + k]], b = prep[d.indices[nd.index_offset + k + 1]],
                     c = prep[d.indices[nd.index_offset + k + 2]];
      const uint32_t tri[3] = {a, b, c};
      for (int e = 0; e < 3; e++) {
        const uint32_t u = tri[e], v = tri[(e + 1) % 3];
        if (u == v || n >= cap) continue;
        out[n++] = u < v ? ((uint64_t)u << 32) | v : ((uint64_t)v << 32) | u;
      }
    }
  }
  std::sort(out, out + n);
  return n;
}
// Tek ucgende kalan kenarlar (acik kenar) -> open[], donus sayi.
uint32_t open_edges(const uint64_t *e, uint32_t n, uint64_t *open, uint32_t cap, uint32_t *over) {
  uint32_t no = 0;
  *over = 0;
  for (uint32_t i = 0; i < n;) {
    uint32_t j = i;
    while (j < n && e[j] == e[i]) j++;
    if (j - i == 1 && no < cap) open[no++] = e[i];
    else if (j - i > 2) (*over)++;
    i = j;
  }
  return no;
}
} // namespace

ENGINE_TEST(content_cluster_dag_cut_is_watertight) {
  static SystemArena sys;
  if (!sys.reserve(192u << 20, "dag_cut")) { CHECK(false); return; }
  // KAPALI mesh (UV kure, dikis vertex'i YOK: son sutun ilkini kullanir,
  // kutuplar tek vertex). Kapali olmasi kapiyi ikili yapar: SAGLAM cut'ta
  // acik kenar SIFIR olmali, tek bir acik kenar bile catlaktir.
  content::ModelMesh mm;
  CHECK(make_closed_sphere(sys, 96, 48, &mm));
  uint32_t *prep = sys.alloc_array<uint32_t>(mm.vertex_count);
  CHECK(prep);
  if (!prep) return;
  for (uint32_t v = 0; v < mm.vertex_count; v++) prep[v] = v; // pozisyonlar zaten tekil

  const uint32_t cap = mm.index_count * 2;
  uint64_t *ebuf = sys.alloc_array<uint64_t>(cap);
  uint64_t *obuf = sys.alloc_array<uint64_t>(cap);
  float *thr = sys.alloc_array<float>(256);
  CHECK(ebuf && obuf && thr);
  if (!ebuf || !obuf || !thr) return;

  auto sweep = [&](const content::ClusterDag &d, const char *tag, uint32_t *out_mixed) {
    // Esikler: dugumlerdeki AYRIK hata degerleri — her gecis noktasina basilir.
    uint32_t nt = 0;
    for (uint32_t i = 0; i < d.node_count && nt < 254; i++) {
      const float e = d.nodes[i].error;
      bool have = false;
      for (uint32_t k = 0; k < nt; k++) if (thr[k] == e) { have = true; break; }
      if (!have) thr[nt++] = e;
    }
    std::sort(thr, thr + nt);
    uint32_t worst = 0, mixed = 0, sweeps = 0, min_tri = 0xFFFFFFFFu, max_tri = 0;
    for (uint32_t k = 0; k < nt; k++) {
      const float t = thr[k];
      uint32_t tris = 0, clusters = 0, over = 0;
      const uint32_t n = cut_edges(d, prep, t, ebuf, cap, &tris, &clusters);
      if (!tris) continue;
      sweeps++;
      uint32_t lvl_seen = 0;
      for (uint32_t i = 0; i < d.node_count; i++)
        if (d.nodes[i].error <= t && t < d.nodes[i].parent_error) lvl_seen |= 1u << d.nodes[i].level;
      uint32_t lvl_n = 0;
      for (uint32_t b = 0; b < 32; b++) lvl_n += (lvl_seen >> b) & 1u;
      if (lvl_n > 1) mixed++;
      const uint32_t no = open_edges(ebuf, n, obuf, cap, &over);
      const uint32_t cracks = no + over; // kapali mesh: acik kenar = catlak
      if (cracks > worst) worst = cracks;
      if (tris < min_tri) min_tri = tris;
      if (tris > max_tri) max_tri = tris;
      if (cracks || k % 8 == 0)
        std::printf("    [bilgi] %s t=%.6f: %u kume (%u seviye karisik), %u ucgen, CATLAK %u\n", tag, (double)t, clusters, lvl_n,
                    tris, cracks);
    }
    std::printf("    [bilgi] %s: %u esik, %u tanesinde seviye KARISIK, ucgen %u..%u, EN KOTU CATLAK %u\n", tag, sweeps, mixed,
                min_tri, max_tri, worst);
    if (out_mixed) *out_mixed = mixed;
    return worst;
  };

  content::ClusterDagOptions opt = content::cluster_dag_preset(content::DeviceClass::Mid);
  content::ClusterDag dag;
  CHECK(content::cluster_dag_build(sys, mm, opt, &dag));
  dag_info("kapali kure DAG", dag);
  uint32_t mixed = 0;
  const uint32_t worst = sweep(dag, "KILITLI", &mixed);
  CHECK(worst == 0);  // hicbir esikte catlak yok
  CHECK(mixed >= 2);  // ve cut gercekten seviye karistiriyor (yoksa gecis olcmemis olurduk)

  // Popping kapisi: ayni gruptaki butun kumeler AYNI parent_error'u tasimali
  // (bit bit) — yoksa grup ikiye bolunerek gecer, hem popping hem catlak.
  bool uniform = true;
  for (uint32_t g = 0; g < dag.group_count; g++) {
    float pe = -1;
    for (uint32_t i = 0; i < dag.node_count; i++) {
      if (dag.nodes[i].group != g) continue;
      if (pe < 0) pe = dag.nodes[i].parent_error;
      else if (std::memcmp(&pe, &dag.nodes[i].parent_error, 4) != 0) uniform = false;
    }
  }
  std::printf("    [bilgi] grup esikleri tek degerli (birlikte gecis): %s (%u grup)\n", uniform ? "EVET" : "HAYIR", dag.group_count);
  CHECK(uniform);

  // KONTROL: kilit kapali -> ayni suprume catlak gormeli.
  content::ClusterDagOptions un = opt;
  un.lock_group_border = false;
  content::ClusterDag dag_un;
  CHECK(content::cluster_dag_build(sys, mm, un, &dag_un));
  const uint32_t worst_un = sweep(dag_un, "KONTROL(kilitsiz)", nullptr);
  CHECK(worst_un > 0);
}

// SDF yazitipi atlasi gercekten daha KESKIN mi? `font_measure_sdf_quality`
// ayni glifi ayni hedef cozunurlukte iki yolla kurup yuksek cozunurluklu
// gercek rasterle karsilastiriyor. Bu kapi o olcumu KOSTURUR — fonksiyon
// yazildi ama hicbir yerden cagrilmiyordu, yani "olcen ama olculmeyen" koddu
// (Tuzaklar: kapisi olmayan olcum, olmayan olcumdur).
//
// KONTROL: ayni olcum 1x buyutmede (atlas cozunurlugu = hedef cozunurluk)
// iki yolu da benzer birakmali — SDF'in kazanci BUYUTMEDE ortaya cikar.
// Boylece kapi "SDF her zaman daha iyi" gibi bos bir iddia olmuyor.
ENGINE_TEST(content_sdf_atlas_is_sharper_when_magnified) {
  char ttf[1024];
  const char *adir = std::getenv("TULPAR_ENGINE_ASSETS");
  if (adir && *adir) std::snprintf(ttf, sizeof ttf, "%s/DejaVuSans.ttf", adir);
  else std::snprintf(ttf, sizeof ttf, "%s/assets/fonts/DejaVuSans.ttf", ENGINE_SOURCE_DIR);
  FILE *f = std::fopen(ttf, "rb");
  if (!f) { skip("DejaVuSans.ttf yok (TULPAR_ENGINE_ASSETS ya da engine/assets/fonts)"); return; }
  std::fclose(f);

  // 'B': hem duz kenar hem egri tasiyor; 8x buyutme UI'de tipik (32 px atlas -> 256 px baslik).
  content::SdfQuality big{};
  const bool ok = content::font_measure_sdf_quality(ttf, (uint32_t)'B', 32.0f, 8.0f, 6, &big);
  CHECK(ok);
  if (!ok) return;
  std::printf("    [bilgi] 8x buyutme: kenar SDF %.2f px / bitmap %.2f px, yanlis piksel SDF %.4f / bitmap %.4f (%ux%u, cevre %u)\n",
              big.sdf_edge_px, big.bitmap_edge_px, big.sdf_error, big.bitmap_error, big.width, big.height, big.perimeter);
  // Kenar bandi belirgin daha dar olmali: SDF'in VAAT ETTIGI sey bu.
  CHECK(big.sdf_edge_px * 4.0f < big.bitmap_edge_px);
  // Siluet SADAKATI: keskinlik, dogruluk PAHASINA gelmemeli. Olculen (2026-09-15,
  // DejaVuSans 'B', 32 px atlas -> 8x): SDF 0.157, bitmap 0.147 — SDF hafifce
  // DAHA YUKSEK. Sebep beklenen: bulanik kenarda yanlis siniflandirma yumusak
  // bir gecise yayilir ve ortalama konumu tutturur; keskin kenarda her konum
  // hatasi TAM bir yanlis piksele doner. Yani bu sayi "SDF daha kotu" demiyor,
  // "hata artik yumusatilmiyor" diyor. Kapi bu yuzden esitlik degil YAKINLIK
  // arar: SDF cozme bozulursa (yanlis on_edge, yanlis olcek) bu oran patlar.
  CHECK(big.sdf_error < big.bitmap_error * 1.25f);

  // KONTROL: 1x'te buyutme yok, SDF'in ustunlugu de belirgin olmamali.
  content::SdfQuality same{};
  const bool ok1 = content::font_measure_sdf_quality(ttf, (uint32_t)'B', 32.0f, 1.0f, 6, &same);
  CHECK(ok1);
  if (ok1) {
    const float buyutme_kazanci = big.bitmap_edge_px / (big.sdf_edge_px > 0 ? big.sdf_edge_px : 1.0f);
    const float ayni_kazanc = same.bitmap_edge_px / (same.sdf_edge_px > 0 ? same.sdf_edge_px : 1.0f);
    std::printf("    [bilgi] kenar keskinlik orani: 8x buyutmede %.2fx, 1x'te %.2fx (kontrol)\n", buyutme_kazanci, ayni_kazanc);
    CHECK(buyutme_kazanci > ayni_kazanc);
  }
  // Olmayan dosya sessizce basarili donmemeli.
  content::SdfQuality bad{};
  CHECK(!content::font_measure_sdf_quality("/olmayan/font.ttf", (uint32_t)'B', 32.0f, 4.0f, 6, &bad));
}

// glTF metallic-roughness CARPANLARI ModelMaterial'a akiyor mu — ve dosyada
// pbrMetallicRoughness blogu YOKSA eski Lambert yoluna dusuluyor mu.
//
// NEDEN BU KAPI VAR: PBR golgeleme renderer'a girdi (Cook-Torrance/GGX), ama
// icerik yolu uzun sure yalniz base_color okuyordu — yani yuklenen HER model
// metallic=0/roughness=1 ile ciziliyordu ve PBR pratikte OLU kaliyordu.
// Ekranda hicbir sey kizarmaz: goruntu "makul" gorunur, sadece yanlistir.
// Kapi GPU istemez: okuma CPU tarafinda, tam da kirilan yerde olculuyor.
ENGINE_TEST(content_gltf_metallic_roughness_reaches_material) {
  static SystemArena sys;
  if (!sys.reserve(8u << 20, "gltf_pbr_test")) { CHECK(false); return; }

  // En kucuk gecerli glTF: tek ucgen, tek malzeme. %s -> malzeme blogu.
  const char *kSablon =
      "{\"asset\":{\"version\":\"2.0\"},"
      "\"scene\":0,\"scenes\":[{\"nodes\":[0]}],\"nodes\":[{\"mesh\":0}],"
      "\"meshes\":[{\"primitives\":[{\"attributes\":{\"POSITION\":0},\"material\":0}]}],"
      "\"materials\":[%s],"
      "\"accessors\":[{\"bufferView\":0,\"componentType\":5126,\"count\":3,\"type\":\"VEC3\","
      "\"min\":[0,0,0],\"max\":[1,1,0]}],"
      "\"bufferViews\":[{\"buffer\":0,\"byteOffset\":0,\"byteLength\":36}],"
      "\"buffers\":[{\"byteLength\":36,\"uri\":\"data:application/octet-stream;base64,"
      "AAAAAAAAAAAAAAAAAACAPwAAAAAAAAAAAAAAAAAAgD8AAAAA\"}]}";

  auto yaz = [&](const char *mat, char *out, size_t n) -> bool {
    std::snprintf(out, n, "/tmp/tulpar_gltf_pbr_%p.gltf", (void *)mat);
    char json[2048];
    std::snprintf(json, sizeof json, kSablon, mat);
    FILE *f = std::fopen(out, "wb");
    if (!f) return false;
    std::fwrite(json, 1, std::strlen(json), f);
    std::fclose(f);
    return true;
  };

  // URUN: pbrMetallicRoughness VAR — metal, puruzsuz, isik yayan.
  char p_pbr[256];
  const char *mat_pbr =
      "{\"pbrMetallicRoughness\":{\"baseColorFactor\":[0.9,0.8,0.7,1],"
      "\"metallicFactor\":1.0,\"roughnessFactor\":0.2},\"emissiveFactor\":[0.1,0.2,0.3]}";
  if (!yaz(mat_pbr, p_pbr, sizeof p_pbr)) { skip("/tmp yazilamadi"); return; }
  content::Model mp;
  bool ok_pbr = content::gltf_load(sys, p_pbr, &mp);
  if (!ok_pbr) std::printf("    [bilgi] yukleme hatasi (pbr): %s\n", mp.error);
  CHECK(ok_pbr);
  bool pbr_alanlari = ok_pbr && mp.material_count == 1 && mp.materials[0].has_pbr &&
                      mp.materials[0].metallic > 0.99f && mp.materials[0].roughness > 0.19f &&
                      mp.materials[0].roughness < 0.21f && mp.materials[0].emissive.z > 0.29f;
  CHECK(pbr_alanlari);
  if (ok_pbr)
    std::printf("    [bilgi] pbr blogu VAR: metallic %.2f roughness %.2f emissive (%.2f %.2f %.2f) has_pbr %d\n",
                mp.materials[0].metallic, mp.materials[0].roughness, mp.materials[0].emissive.x,
                mp.materials[0].emissive.y, mp.materials[0].emissive.z, (int)mp.materials[0].has_pbr);

  // KONTROL: pbrMetallicRoughness YOK. has_pbr false kalmali ve degerler
  // Lambert'e en yakin varsayilanda durmali — yoksa "akiyor" olcumu, her
  // malzemeye ayni sayiyi yazan bir koda da yesil verirdi.
  char p_duz[256];
  const char *mat_duz = "{\"name\":\"pbrsiz\"}";
  if (!yaz(mat_duz, p_duz, sizeof p_duz)) { skip("/tmp yazilamadi"); return; }
  content::Model md;
  bool ok_duz = content::gltf_load(sys, p_duz, &md);
  CHECK(ok_duz);
  bool kontrol = ok_duz && md.material_count == 1 && !md.materials[0].has_pbr &&
                 md.materials[0].metallic == 0.0f && md.materials[0].roughness == 1.0f;
  CHECK(kontrol);
  if (ok_duz)
    std::printf("    [bilgi] KONTROL pbr blogu YOK: has_pbr %d, metallic %.2f roughness %.2f (Lambert yolu)\n",
                (int)md.materials[0].has_pbr, md.materials[0].metallic, md.materials[0].roughness);
  std::remove(p_pbr);
  std::remove(p_duz);
}

// --- PBR DOKULARI: glTF -> malzeme ------------------------------------------
// NEDEN BU KAPI VAR: PBR carpanlari (metallic/roughness/emissive) bir onceki
// dilimde akmaya basladi ama DOKULAR akmiyordu; her malzeme doku BASINA degil
// malzeme basina tek skaler cift kullaniyordu. Ekranda yine hicbir sey
// kizarmaz — yuzey "makul" gorunur, yalnizca duzdur. Okuma CPU tarafinda, tam
// da kirildigi yerde olculuyor (GPU gerekmez).
//
// Ayrica burada SATICI AYRISTIRICISININ tuzagi var: cgltf `texture_view.scale`i
// yalniz o JSON nesnesi VARSA 1'e kurar; normalTexture yazilmamissa alan SIFIR
// kalir. Kosulsuz okunsa butun normal haritalari duzlesirdi — KONTROL bloku
// tam olarak bunu olcuyor.
ENGINE_TEST(content_gltf_pbr_textures_reach_material) {
  char path[1024];
  if (!asset_path(path, sizeof path, "pbr_plane.gltf")) { skip("varlik yok (make_test_gltf.py)"); return; }
  static SystemArena sys;
  if (!sys.reserve(32u << 20, "pbr_tex_test")) { CHECK(false); return; }
  content::Model m;
  bool ok = content::gltf_load(sys, path, &m);
  if (!ok) std::printf("    [bilgi] yukleme hatasi: %s\n", m.error);
  CHECK(ok);
  if (!ok) return;
  CHECK(m.material_count == 1 && m.image_count == 4);
  if (m.material_count != 1 || m.image_count != 4) return;
  const content::ModelMaterial &mm = m.materials[0];
  std::printf("    [bilgi] malzeme dokulari: albedo %d, ORM %d, normal %d, isima %d | normal olcegi %.2f, "
              "occlusion gucu %.2f (ayri occlusion dokusu: %d)\n",
              mm.image, mm.orm_image, mm.normal_image, mm.emissive_image, (double)mm.normal_scale,
              (double)mm.occlusion_strength, (int)mm.occlusion_separate);
  bool indices = mm.image == 0 && mm.orm_image == 1 && mm.normal_image == 2 && mm.emissive_image == 3;
  CHECK(indices);
  // normalTexture.scale = 0.75, occlusionTexture.strength = 0.6 (ayni goruntu -> ORM.R bedava)
  bool scales = mm.normal_scale > 0.74f && mm.normal_scale < 0.76f && mm.occlusion_strength > 0.59f &&
                mm.occlusion_strength < 0.61f && !mm.occlusion_separate;
  CHECK(scales);

  // KONTROL: ayni sema, DOKUSUZ malzeme. Indeksler -1 KALMALI ve normal olcegi
  // 1.0 olmali (0.0 DEGIL) — cgltf'in kurmadigi varsayilan burada yakalanir.
  // Bu blok gecmezse yukaridaki "akiyor" olcumu her malzemeye ayni sayiyi
  // yazan bir koda da yesil verirdi.
  const char *kSablon =
      "{\"asset\":{\"version\":\"2.0\"},"
      "\"scene\":0,\"scenes\":[{\"nodes\":[0]}],\"nodes\":[{\"mesh\":0}],"
      "\"meshes\":[{\"primitives\":[{\"attributes\":{\"POSITION\":0},\"material\":0}]}],"
      "\"materials\":[{\"pbrMetallicRoughness\":{\"metallicFactor\":0.5,\"roughnessFactor\":0.5}}],"
      "\"accessors\":[{\"bufferView\":0,\"componentType\":5126,\"count\":3,\"type\":\"VEC3\","
      "\"min\":[0,0,0],\"max\":[1,1,0]}],"
      "\"bufferViews\":[{\"buffer\":0,\"byteOffset\":0,\"byteLength\":36}],"
      "\"buffers\":[{\"byteLength\":36,\"uri\":\"data:application/octet-stream;base64,"
      "AAAAAAAAAAAAAAAAAACAPwAAAAAAAAAAAAAAAAAAgD8AAAAA\"}]}";
  char kpath[256];
  std::snprintf(kpath, sizeof kpath, "/tmp/tulpar_pbr_tex_kontrol.gltf");
  FILE *f = std::fopen(kpath, "wb");
  if (!f) { skip("/tmp yazilamadi"); return; }
  std::fwrite(kSablon, 1, std::strlen(kSablon), f);
  std::fclose(f);
  content::Model k;
  bool ok_k = content::gltf_load(sys, kpath, &k);
  CHECK(ok_k);
  bool kontrol = ok_k && k.material_count == 1 && k.materials[0].orm_image < 0 && k.materials[0].normal_image < 0 &&
                 k.materials[0].emissive_image < 0 && k.materials[0].normal_scale == 1.0f &&
                 k.materials[0].occlusion_strength == 0.0f;
  CHECK(kontrol);
  if (ok_k)
    std::printf("    [bilgi] KONTROL dokusuz malzeme: ORM %d normal %d isima %d, normal olcegi %.2f "
                "(cgltf'in kurmadigi varsayilan: 0.00 olsaydi butun normal haritalari duzlesirdi)\n",
                k.materials[0].orm_image, k.materials[0].normal_image, k.materials[0].emissive_image,
                (double)k.materials[0].normal_scale);
  std::remove(kpath);
}

// --- RENK UZAYI: kullanimdan turetiliyor mu? --------------------------------
// glTF goruntusu renk uzayi TASIMAZ; onu malzemedeki yeri belirler (spec):
// baseColor/emissive = sRGB, metallicRoughness/normal = DOGRUSAL veri. Yanlis
// uzay "goruntu biraz yanlis, hicbir sey kizarmaz" sinifinin ders kitabi
// ornegidir: sRGB yuklenmis bir ORM dokusunda 0.5 puruzluluk GPU'da 0.21 olur.
// Burada CPU siniflandirmasi, GPU tarafi renderer_texture_colorspace_shifts_roughness.
ENGINE_TEST(content_gltf_texture_colorspace) {
  char path[1024];
  if (!asset_path(path, sizeof path, "pbr_plane.gltf")) { skip("varlik yok (make_test_gltf.py)"); return; }
  static SystemArena sys;
  if (!sys.reserve(32u << 20, "colorspace_test")) { CHECK(false); return; }
  content::Model m;
  bool ok = content::gltf_load(sys, path, &m);
  CHECK(ok);
  if (!ok || m.image_count != 4) { CHECK(false); return; }
  std::printf("    [bilgi] goruntu renk uzaylari: base %s, ORM %s, normal %s, isima %s (catisma %u)\n",
              m.images[0].srgb ? "sRGB" : "DOGRUSAL", m.images[1].srgb ? "sRGB" : "DOGRUSAL",
              m.images[2].srgb ? "sRGB" : "DOGRUSAL", m.images[3].srgb ? "sRGB" : "DOGRUSAL", m.colorspace_conflicts);
  bool spaces = m.images[0].srgb && !m.images[1].srgb && !m.images[2].srgb && m.images[3].srgb;
  CHECK(spaces);
  CHECK(m.colorspace_conflicts == 0);
  // NORMAL HARITASI BAYRAGI renk uzayindan AYRI: ORM de dogrusal veridir ama
  // yeniden normallestirilmemelidir (puruzluluk/metaliklik birer skaler).
  // Bayrak yalniz normal goruntusune konmali — yoksa mip yolu ORM'yi bozardi.
  bool normal_flags = !m.images[0].normal_map && !m.images[1].normal_map &&
                      m.images[2].normal_map && !m.images[3].normal_map;
  CHECK(normal_flags);

  // KONTROL: AYNI goruntu hem baseColor hem metallicRoughness. glTF'te gecersiz;
  // renk kazanmali (sRGB) ve catisma SAYILMALI. Bu blok olmadan siniflandirma
  // "her seye dogrusal de" diyen bir koda da yesil verirdi.
  const char *kSablon =
      "{\"asset\":{\"version\":\"2.0\"},"
      "\"scene\":0,\"scenes\":[{\"nodes\":[0]}],\"nodes\":[{\"mesh\":0}],"
      "\"meshes\":[{\"primitives\":[{\"attributes\":{\"POSITION\":0},\"material\":0}]}],"
      "\"materials\":[{\"pbrMetallicRoughness\":{\"baseColorTexture\":{\"index\":0},"
      "\"metallicRoughnessTexture\":{\"index\":0}}}],"
      "\"textures\":[{\"source\":0}],"
      "\"images\":[{\"uri\":\"data:image/png;base64,"
      "iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVR42mP8z8BQDwAEhQGAhKmMIQAAAABJRU5ErkJggg==\"}],"
      "\"accessors\":[{\"bufferView\":0,\"componentType\":5126,\"count\":3,\"type\":\"VEC3\","
      "\"min\":[0,0,0],\"max\":[1,1,0]}],"
      "\"bufferViews\":[{\"buffer\":0,\"byteOffset\":0,\"byteLength\":36}],"
      "\"buffers\":[{\"byteLength\":36,\"uri\":\"data:application/octet-stream;base64,"
      "AAAAAAAAAAAAAAAAAACAPwAAAAAAAAAAAAAAAAAAgD8AAAAA\"}]}";
  char kpath[256];
  std::snprintf(kpath, sizeof kpath, "/tmp/tulpar_colorspace_kontrol.gltf");
  FILE *f = std::fopen(kpath, "wb");
  if (!f) { skip("/tmp yazilamadi"); return; }
  std::fwrite(kSablon, 1, std::strlen(kSablon), f);
  std::fclose(f);
  content::Model k;
  bool ok_k = content::gltf_load(sys, kpath, &k);
  if (!ok_k) std::printf("    [bilgi] kontrol yukleme hatasi: %s\n", k.error);
  CHECK(ok_k);
  bool kontrol = ok_k && k.image_count == 1 && k.images[0].srgb && k.colorspace_conflicts == 1;
  CHECK(kontrol);
  if (ok_k)
    std::printf("    [bilgi] KONTROL ayni goruntu iki rolde: %s, catisma %u (renk kazanir, sessiz degil)\n",
                k.images[0].srgb ? "sRGB" : "DOGRUSAL", k.colorspace_conflicts);
  std::remove(kpath);
}

// --- ASTC + NORMAL HARITASI: hangi kodlama? ---------------------------------
// "Once olc, sonra karar ver." Normal haritasi RENK DEGILDIR ve dogru olcut
// PSNR degil ACISAL hatadir. astcenc'in ASTCENC_FLG_MAP_NORMAL kipi tam bunun
// icin var: iki kanal saklar (girdi duzeni rrrg — X parlaklikta, Y alfada),
// Z'yi cozerken kurar ve hatayi acisal olarak azaltmaya calisir.
//
// Kapi ayni kaynagi UC blok boyunda, IKI kodlamayla olcer ve KARARI sayilarla
// verir. Blok boyu onemli: iki kanalli kip bit butcesi SIKISINCA kazanir
// (8x8'de blok basina ayni 128 bit, dort kat piksel); 4x4'te butce zaten bol.
// KONTROL: ayni kodlama, daha KABA blok (8x8) 4x4'ten ACIKCA kotu olmali —
// olmuyorsa olcut kodlama kalitesini hic gormuyordur ve butun tablo bostur.
ENGINE_TEST(content_astc_normal_map_encoding) {
  static SystemArena sys;
  if (!sys.reserve(160u << 20, "astc_normal")) { CHECK(false); return; }
  const uint32_t W = 128, H = 128;
  uint8_t *src = sys.alloc_array<uint8_t>(W * H * 4);
  float *nx = sys.alloc_array<float>(W * H), *ny = sys.alloc_array<float>(W * H), *nz = sys.alloc_array<float>(W * H);
  if (!src || !nx || !ny || !nz) { CHECK(false); return; }
  // Kaynak: 16 piksellik yarim kure tumsekler — acisal degisim BOL, yon blok
  // icinde hizla doner. Duz bir harita her kodlayiciya "mukemmel" gorunurdu.
  for (uint32_t y = 0; y < H; y++)
    for (uint32_t x = 0; x < W; x++) {
      const float u = ((float)(x % 16) + 0.5f) / 16.0f * 2.0f - 1.0f;
      const float v = ((float)(y % 16) + 0.5f) / 16.0f * 2.0f - 1.0f;
      const float r2 = u * u + v * v;
      float ax = u, ay = v, az = 1.0f;
      if (r2 < 1.0f) az = std::sqrt(1.0f - r2);
      else { const float inv = 1.0f / std::sqrt(r2); ax = u * inv * 0.9f; ay = v * inv * 0.9f; az = 0.436f; }
      const float len = std::sqrt(ax * ax + ay * ay + az * az);
      ax /= len; ay /= len; az /= len;
      const uint32_t i = y * W + x;
      nx[i] = ax; ny[i] = ay; nz[i] = az;
      src[i * 4 + 0] = (uint8_t)(ax * 0.5f * 255.0f + 127.5f + 0.5f);
      src[i * 4 + 1] = (uint8_t)(ay * 0.5f * 255.0f + 127.5f + 0.5f);
      src[i * 4 + 2] = (uint8_t)(az * 0.5f * 255.0f + 127.5f + 0.5f);
      src[i * 4 + 3] = 255;
    }
  const uint32_t blocks_wh[3] = {4, 6, 8};
  const content::AstcMap kinds[2] = {content::AstcMap::Data, content::AstcMap::Normal};
  const char *kind_name[2] = {"dogrusal RGB veri", "MAP_NORMAL (rrrg) "};
  double mean_deg[2][3] = {}, max_deg[2][3] = {};
  uint32_t bytes[2][3] = {};
  bool ok = true;
  for (int k = 0; k < 2 && ok; k++)
    for (int b = 0; b < 3 && ok; b++) {
      uint8_t *blk = nullptr, *dec = nullptr;
      uint32_t n = 0;
      const uint32_t bw = blocks_wh[b];
      ok = content::astc_encode_rgba(sys, src, W, H, bw, bw, false, 90.0f, &blk, &n, kinds[k]);
      if (ok) ok = content::astc_decode_rgba(sys, blk, n, W, H, bw, bw, false, &dec, kinds[k]);
      if (!ok) break;
      bytes[k][b] = n;
      double acc = 0, mx = 0;
      for (uint32_t i = 0; i < W * H; i++) {
        float dx = dec[i * 4 + 0] / 255.0f * 2.0f - 1.0f;
        float dy = dec[i * 4 + 1] / 255.0f * 2.0f - 1.0f;
        float dz = dec[i * 4 + 2] / 255.0f * 2.0f - 1.0f;
        const float len = std::sqrt(dx * dx + dy * dy + dz * dz);
        if (len < 1e-6f) { acc += 90.0; mx = 90.0; continue; }
        dx /= len; dy /= len; dz /= len;
        float d = dx * nx[i] + dy * ny[i] + dz * nz[i];
        d = d > 1.0f ? 1.0f : (d < -1.0f ? -1.0f : d);
        const double deg = std::acos((double)d) * 180.0 / 3.14159265358979;
        acc += deg;
        if (deg > mx) mx = deg;
      }
      mean_deg[k][b] = acc / (double)(W * H);
      max_deg[k][b] = mx;
    }
  CHECK(ok);
  if (!ok) return;
  for (int k = 0; k < 2; k++)
    for (int b = 0; b < 3; b++)
      std::printf("    [bilgi] %s  %ux%u: ortalama %.3f derece, en buyuk %.2f derece, %u bayt\n", kind_name[k],
                  blocks_wh[b], blocks_wh[b], mean_deg[k][b], max_deg[k][b], bytes[k][b]);
  // KONTROL: olcut kodlama kalitesini goruyor mu? 8x8, 4x4'ten (dort kat az bit)
  // ACIKCA kotu olmali. Gormuyorsa yukaridaki butun tablo hicbir sey soylemez.
  bool metric_sees_bit_budget = mean_deg[0][2] > mean_deg[0][0] * 1.5 && mean_deg[1][2] > mean_deg[1][0] * 1.5;
  CHECK(metric_sees_bit_budget);
  std::printf("    [bilgi] KONTROL bit butcesi: 4x4 -> 8x8 acisal hata %.2f kat (dogrusal), %.2f kat (MAP_NORMAL)\n",
              mean_deg[0][0] > 0 ? mean_deg[0][2] / mean_deg[0][0] : 0.0,
              mean_deg[1][0] > 0 ? mean_deg[1][2] / mean_deg[1][0] : 0.0);
  // KARAR, sayilarla: her blok boyunda hangi kodlama kazaniyor?
  for (int b = 0; b < 3; b++)
    std::printf("    [bilgi] KARAR %ux%u: MAP_NORMAL / dogrusal = %.2f  -> %s\n", blocks_wh[b], blocks_wh[b],
                mean_deg[0][b] > 0 ? mean_deg[1][b] / mean_deg[0][b] : 0.0,
                mean_deg[1][b] < mean_deg[0][b] ? "MAP_NORMAL" : "dogrusal RGB");
  // Urun yolunda kullanilan kodlama (engine_texpack --tur normal) MUTLAK olarak
  // yeterli olmali: 4x4'te ortalama acisal hata 2 derecenin altinda.
  bool product_quality_is_enough = mean_deg[content::kAstcNormalUsesMapMode ? 1 : 0][0] < 2.0;
  CHECK(product_quality_is_enough);
}

// --- KTX2: iki kanalli normal haritasinin duzeni kayboluyor mu? -------------
// MAP_NORMAL ile sikistirilan doku RGB'de X'in KOPYASINI, alfada Y'yi tutar
// (G/B kanallarinin agirligi kodlayicida SIFIRLANIR — astcenc_entry.cpp). Bunu
// BILMEYEN bir tuketici `texture(...).xyz` okur, duz bir gri harita gorur ve
// aydinlatma sessizce yanlis cikar. Bu yuzden duzen konteynere yazilir
// (KTX2 key/value "TULPAR_normalXY") ve okunur.
//
// Kapi iki seyi olcer: (1) bayrak dosyadan geri geliyor mu, (2) DONANIMIN
// gordugu duzen gercekten X-tekrarli mi. KONTROL: ayni goruntu --tur orm ile
// yazilirsa bayrak GELMEMELI ve kanallar birbirinin kopyasi OLMAMALI.
ENGINE_TEST(content_ktx2_normal_layout_roundtrip) {
  static SystemArena sys;
  if (!sys.reserve(96u << 20, "ktx2_normal")) { CHECK(false); return; }
  const uint32_t W = 64, H = 64;
  uint8_t *src = sys.alloc_array<uint8_t>(W * H * 4);
  if (!src) { CHECK(false); return; }
  // X ve Y BIRBIRINDEN FARKLI degissin (X yatay, Y dikey): kanallar ayirt
  // edilemezse asagidaki "X tekrarli mi" olcumu anlamsiz olurdu.
  for (uint32_t y = 0; y < H; y++)
    for (uint32_t x = 0; x < W; x++) {
      const uint32_t i = y * W + x;
      src[i * 4 + 0] = (uint8_t)(40 + x * 3);
      src[i * 4 + 1] = (uint8_t)(200 - y * 2);
      src[i * 4 + 2] = 255;
      src[i * 4 + 3] = 255;
    }
  const content::AstcMap kinds[2] = {content::AstcMap::Normal, content::AstcMap::Data};
  bool flag[2] = {false, false};
  uint32_t xy_repeat[2] = {0, 0}; // R == G == B olan texel sayisi
  bool ok = true;
  for (int k = 0; k < 2 && ok; k++) {
    uint8_t *blk = nullptr;
    uint32_t n = 0;
    ok = content::astc_encode_rgba(sys, src, W, H, 4, 4, false, 90.0f, &blk, &n, kinds[k]);
    if (!ok) break;
    char path[512];
    std::snprintf(path, sizeof path, "%s/tulpar_ktx2_normal_%d.ktx2", tulpar::engine::test::tmp_dir(), k);
    const uint8_t *data[1] = {blk};
    const uint32_t sizes[1] = {n};
    ok = content::ktx2_write(path, VK_FORMAT_ASTC_4x4_UNORM_BLOCK, W, H, 1, data, sizes, 4, 4,
                             kinds[k] == content::AstcMap::Normal);
    content::Ktx2Image img;
    if (ok) ok = content::ktx2_load(sys, path, &img);
    if (!ok) { std::printf("    [bilgi] ktx2: %s\n", img.error); std::remove(path); break; }
    flag[k] = img.normal_xy;
    // DONANIMIN gordugu duzen: bloklari duz swizzle ile coz (ktx2_upload'in
    // CPU yedeginin yaptigi sey; GPU ASTC'yi de aynen boyle ornekler).
    uint8_t *dec = nullptr;
    ok = content::astc_decode_rgba(sys, img.level_data[0], img.level_size[0], W, H, 4, 4, false, &dec,
                                   content::AstcMap::Data);
    if (ok)
      for (uint32_t i = 0; i < W * H; i++)
        if (dec[i * 4] == dec[i * 4 + 1] && dec[i * 4 + 1] == dec[i * 4 + 2]) xy_repeat[k]++;
    std::remove(path);
  }
  CHECK(ok);
  if (!ok) return;
  std::printf("    [bilgi] URUN    --tur normal: KVD bayragi %d, R==G==B texel %u/%u (X tekrarli duzen)\n",
              (int)flag[0], xy_repeat[0], W * H);
  std::printf("    [bilgi] KONTROL --tur orm   : KVD bayragi %d, R==G==B texel %u/%u (kanallar bagimsiz)\n",
              (int)flag[1], xy_repeat[1], W * H);
  bool flag_survives_the_container = flag[0] && !flag[1];
  CHECK(flag_survives_the_container);
  bool normal_layout_is_x_replicated = xy_repeat[0] > (W * H) * 9 / 10;
  CHECK(normal_layout_is_x_replicated);
  bool control_keeps_channels_apart = xy_repeat[1] < (W * H) / 10;
  CHECK(control_keeps_channels_apart); // dusmezse "X tekrarli" olcumu bir sey olcmuyor
}

// ---------------------------------------------------------------------------
// NORMAL HARITASI MIP ZINCIRI — kucultmeden SONRA yeniden normallestirme.
//
// Donanim blit'i dogrusal suzuyor, yani dort komsu normalin BILESENLERINI
// ortaliyor. Birbirine ters egimli normallerde ortalamanin BOYU 1 degil ~0
// olur; encode edilince "duz yuzey" cikar ve uzaktaki yuzey sessizce duzlesir.
//
// Kapi iki seyi birden olcuyor, cunku ayri ayri her biri aldatici olurdu:
//   (1) `build_normal_mips` ciktisinda her texel BIRIM uzunlukta mi,
//   (2) KONTROL: ayni veriyi normallestirmeden ortalarsan boy belirgin
//       kuculuyor mu. Bu olmadan "birim uzunluk" testi, girdi zaten duz bir
//       harita oldugunda da gecerdi — yani hicbir sey olcmezdi.
// ---------------------------------------------------------------------------
ENGINE_TEST(content_normal_map_mips_stay_unit_length) {
  static SystemArena sys;
  if (!sys.reserve(16u << 20, "normal_mip_test")) { CHECK(false); return; }

  // Dama deseni: komsu texel'ler ZIT yone egimli (x = +-0.8, z = kalan).
  // Bu kasitli olarak en kotu durum: ortalama neredeyse sifirlanir.
  const uint32_t W = 64, H = 64;
  uint8_t *px = sys.alloc_array<uint8_t>(W * H * 4);
  CHECK(px != nullptr);
  if (!px) return;
  auto enc = [](float v) { return (uint8_t)((v * 0.5f + 0.5f) * 255.0f + 0.5f); };
  const float tilt = 0.8f, nz = std::sqrt(1.0f - tilt * tilt);
  for (uint32_t y = 0; y < H; y++)
    for (uint32_t x = 0; x < W; x++) {
      const float sx = ((x + y) & 1) ? tilt : -tilt;
      uint8_t *p = px + (y * W + x) * 4;
      p[0] = enc(sx); p[1] = enc(0.0f); p[2] = enc(nz); p[3] = 255;
    }
  content::ModelImage img;
  img.width = W; img.height = H; img.rgba = px; img.srgb = false; img.normal_map = true;

  uint32_t levels = 1, mm = W > H ? W : H;
  while (mm > 1) { mm >>= 1; levels++; }
  CHECK(levels == 7); // 64 -> 1
  uint8_t **data = sys.alloc_array_zeroed<uint8_t *>(levels);
  uint32_t *sizes = sys.alloc_array_zeroed<uint32_t>(levels);
  CHECK(data && sizes);
  if (!data || !sizes) return;
  CHECK(content::build_normal_mips(sys, img, levels, data, sizes));

  // (1) URUN: her seviyede her texel birim uzunlukta.
  float worst = 1.0f;
  uint32_t lw = W, lh = H;
  for (uint32_t l = 1; l < levels; l++) {
    lw = lw > 1 ? lw >> 1 : 1;
    lh = lh > 1 ? lh >> 1 : 1;
    CHECK(sizes[l] == lw * lh * 4);
    for (uint32_t i = 0; i < lw * lh; i++) {
      const uint8_t *p = data[l] + i * 4;
      const float nx = (float)p[0] / 255.0f * 2.0f - 1.0f;
      const float ny = (float)p[1] / 255.0f * 2.0f - 1.0f;
      const float nzz = (float)p[2] / 255.0f * 2.0f - 1.0f;
      const float len = std::sqrt(nx * nx + ny * ny + nzz * nzz);
      if (std::fabs(len - 1.0f) > std::fabs(worst - 1.0f)) worst = len;
    }
  }
  // Tolerans 8 bit kuantizasyonundan: 1/255 adim, uc bilesende ~0.01.
  CHECK(std::fabs(worst - 1.0f) < 0.02f);

  // (2) KONTROL: normallestirmeyen ortalama AYNI veride belirgin kisalir.
  // Tek seviye yeter — hata ilk kucultmede ortaya cikiyor.
  float ctrl_worst = 1.0f;
  const uint32_t cw = W / 2, ch = H / 2;
  for (uint32_t y = 0; y < ch; y++)
    for (uint32_t x = 0; x < cw; x++) {
      float ax = 0, ay = 0, az = 0;
      for (int k = 0; k < 4; k++) {
        const uint32_t sx2 = x * 2 + (k & 1), sy2 = y * 2 + (k >> 1);
        const uint8_t *p = px + (sy2 * W + sx2) * 4;
        ax += (float)p[0] / 255.0f * 2.0f - 1.0f;
        ay += (float)p[1] / 255.0f * 2.0f - 1.0f;
        az += (float)p[2] / 255.0f * 2.0f - 1.0f;
      }
      ax *= 0.25f; ay *= 0.25f; az *= 0.25f;
      const float len = std::sqrt(ax * ax + ay * ay + az * az);
      if (len < ctrl_worst) ctrl_worst = len;
    }
  // Kontrol GERCEKTEN kisalmali: yoksa test verisi sorunu uretmiyor demektir
  // ve (1) bos bir iddia olurdu.
  CHECK(ctrl_worst < 0.75f);
  std::printf("    [bilgi] normal mip: yeniden normallestirilmis en kotu boy %.4f (birim 1.0); "
              "KONTROL normallestirmeyen ortalama %.4f (= uzakta yuzey duzlesir)\n",
              (double)worst, (double)ctrl_worst);
}
