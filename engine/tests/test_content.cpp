// Faz 3/6: glTF yukleme + dokulu cizim. Varlik: tests/assets/checker_cube.gltf
// (make_test_gltf.py, belirlenimli). Cihazda: TULPAR_ENGINE_ASSETS dizini
// (android_run.sh push eder); yoksa GORUNUR atlanir.
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "content/gltf.hpp"
#include "content/ktx2.hpp"

#include <stb_image.h>
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
