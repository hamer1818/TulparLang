// Derlenmis sahne (content/scene_blob + scene_runtime): derleme deterministik
// (ayni desc -> ayni bayt; tek ulp -> farkli ozet, kontrol), tablolar veri
// modeliyle bit-tam (dunya matrisi = scene_entity_matrix, isik konumu = matris
// cevirisi, govde olcekli), bozuk blob'un reddi (magic/surum/kesik/ozet/ofset/
// dizin/hiza; bozulmamis acilir — pozitif kontrol), dosya gidis-donus, dunya
// isleminin gunlukte geri al/yinele ile bayt-esit donusu, runtime'in blob'dan
// cizmesi (cizim/isik sayilari + piksel farki; bos sahne 0 fark — kontrol) ve
// dinamik govdenin sim'den gelmesi.
//
// Faz 6 (sahne derleyicisi): yerlesik kume + tepe bellek blob'da OLCULMUS
// sayilar (runtime tahmin etmez), navmesh derleme aninda bake edilir ve runtime
// yalniz sorgular, eski blob surumu anlamli hatayla reddedilir.
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <unistd.h>

#include "content/gi.hpp"
#include "content/gltf.hpp"
#include "content/importer.hpp"
#include "content/scene.hpp"
#include "content/cluster_dag.hpp"
#include "content/scene_blob.hpp"
#include "content/scene_compile.hpp"
#include "content/scene_runtime.hpp"
#include "core/memory/arena.hpp"
#include "renderer/renderer.hpp"
#include "rhi/device.hpp"
#include "rhi/offscreen.hpp"
#include "rhi/vk_api.hpp"
#include "sim/navmesh.hpp"
#include "sim/physics.hpp"
#include "tests/test.hpp"

using namespace tulpar::engine;
using namespace tulpar::engine::content;
using namespace tulpar::engine::test;

namespace {
SystemArena &arena() {
  static SystemArena sys;
  if (sys.capacity() == 0) sys.reserve(160u << 20, "scene_blob_test");
  return sys;
}
bool feq(float a, float b) { uint32_t x, y; std::memcpy(&x, &a, 4); std::memcpy(&y, &b, 4); return x == y; }
bool v3eq(const float *a, Vec3 b) { return feq(a[0], b.x) && feq(a[1], b.y) && feq(a[2], b.z); }

// Her bilesen turu + zor sayilar; bir varlik iki bilesenli, biri bos.
void fill(SceneDesc &d) {
  d = SceneDesc{};
  d.sun_dir = {0.5f, 1.0f, 0.35f};
  d.ambient = {1.0f / 3.0f, 0.17f, 1e-5f};
  d.shadow_depth = 123456.789f;
  d.cam_yaw = -0.0f;
  d.add_asset("lod_sphere.gltf");
  d.add_asset("dizin/skin_tube.gltf");
  SceneEntity e{};
  std::snprintf(e.name, sizeof e.name, "kure");
  e.pos = {-8.0f, 1.2f, -8.5f}; e.rot_deg = {30, 45, 60}; e.scale = {1, 2, 0.5f};
  e.components = kSceneModel; e.asset = 0; e.tint = {0.85f, 0.9f, 1.0f};
  d.insert_entity(d.entity_count, e);
  e = SceneEntity{};
  std::snprintf(e.name, sizeof e.name, "boru");
  e.pos = {4, 0, -3.5f}; e.scale = {1.2f, 1.2f, 1.2f};
  e.components = kSceneModel | kSceneAnim; e.asset = 1; e.clip = 0; e.phase = 0.35f; e.speed = 1.0f / 7.0f;
  d.insert_entity(d.entity_count, e);
  e = SceneEntity{};
  std::snprintf(e.name, sizeof e.name, "lamba");
  e.pos = {0, 2, -3}; e.rot_deg = {0, 90, 0};
  e.components = kSceneLight; e.light_color = {1, 0.2f, 0.1f}; e.light_intensity = 3; e.light_radius = 8;
  d.insert_entity(d.entity_count, e);
  e = SceneEntity{};
  std::snprintf(e.name, sizeof e.name, "kutu");
  e.pos = {6, 6, 3}; e.rot_deg = {0, 30, 0}; e.scale = {2, 1, 1};
  e.components = kSceneModel | kSceneBody; e.asset = 0; e.shape = SceneShape::Box; e.half = {0.5f, 0.25f, 1e-3f}; e.dynamic = true;
  d.insert_entity(d.entity_count, e);
  e = SceneEntity{};
  std::snprintf(e.name, sizeof e.name, "kure_govde");
  e.pos = {-6, 0.75f, 3}; e.scale = {3, 3, 3};
  e.components = kSceneBody; e.shape = SceneShape::Sphere; e.radius = 0.75f; e.dynamic = false;
  d.insert_entity(d.entity_count, e);
  e = SceneEntity{};
  std::snprintf(e.name, sizeof e.name, "bos");
  d.insert_entity(d.entity_count, e);
}
const char *asset_path(char *buf, size_t n, const char *name) {
  const char *adir = std::getenv("TULPAR_ENGINE_ASSETS");
  if (adir && *adir) std::snprintf(buf, n, "%s/%s", adir, name);
  else std::snprintf(buf, n, "%s/tests/assets/%s", ENGINE_SOURCE_DIR, name);
  return buf;
}
void *compile_to(const SceneDesc &d, size_t *n) {
  const size_t need = scene_blob_compile(d, nullptr, 0);
  void *buf = arena().alloc(need, kSceneBlobAlign);
  if (buf) scene_blob_compile(d, buf, need);
  *n = need;
  return buf;
}
rhi::VkApi g_api;
struct Rec { renderer::Renderer *r; };
void rec_main(VkCommandBuffer cb, void *u) { static_cast<Rec *>(u)->r->record(cb); }
void rec_shadow(VkCommandBuffer cb, void *u) { static_cast<Rec *>(u)->r->record_shadow(cb); }
uint32_t count_diff(const uint8_t *a, const uint8_t *b, uint32_t n_px) {
  uint32_t n = 0;
  for (uint32_t i = 0; i < n_px; i++) {
    const uint8_t *p = a + i * 4, *q = b + i * 4;
    if (std::abs(p[0] - q[0]) > 8 || std::abs(p[1] - q[1]) > 8 || std::abs(p[2] - q[2]) > 8) n++;
  }
  return n;
}
} // namespace

ENGINE_TEST(scene_blob_compile_is_deterministic_and_matches_desc) {
  static SceneDesc d;
  fill(d);
  size_t n1 = 0, n2 = 0;
  void *b1 = compile_to(d, &n1);
  void *b2 = compile_to(d, &n2);
  CHECK(b1 && b2 && n1 == n2 && n1 % kSceneBlobAlign == 0);
  CHECK(std::memcmp(b1, b2, n1) == 0); // ayni desc -> ayni baytlar
  CHECK(scene_blob_compile(d, nullptr, 0) == n1);
  char small[64];
  CHECK(scene_blob_compile(d, small, sizeof small) == n1); // sigmazsa yazmaz, boyutu doner
  SceneBlobView v;
  SceneError err{};
  const bool ok = scene_blob_open(b1, n1, &v, &err);
  if (!ok) std::printf("    [bilgi] acma: %s\n", err.msg);
  CHECK(ok);
  if (!ok) return;
  CHECK(v.h->entity_count == 6 && v.h->asset_count == 2 && v.h->draw_count == 3 && v.h->anim_count == 1 && v.h->light_count == 1 && v.h->body_count == 2);
  CHECK(std::strcmp(v.asset_path(1), "dizin/skin_tube.gltf") == 0 && v.assets[1].path_len == std::strlen("dizin/skin_tube.gltf"));
  CHECK(std::strcmp(v.entity_name(0), "kure") == 0 && std::strcmp(v.entity_name(5), "bos") == 0);
  // Dunya ayarlari bit-tam.
  const SceneWorld w = v.world();
  CHECK(scene_world_equal(w, d.world()));
  CHECK(feq(w.ambient.x, 1.0f / 3.0f) && feq(w.shadow_depth, 123456.789f) && feq(w.cam_yaw, -0.0f));
  // Varlik matrisleri = scene_entity_matrix (bit-tam), kuaterniyon = scene_entity_rotation.
  bool mats = true, quats = true;
  for (uint32_t i = 0; i < d.entity_count; i++) {
    const Mat4 m = scene_entity_matrix(d.entities[i]), bm = v.entity_matrix(i);
    if (std::memcmp(&m.m[0][0], &bm.m[0][0], sizeof m.m) != 0) mats = false;
    const Quat q = scene_entity_rotation(d.entities[i]);
    const float *bq = v.entities[i].quat;
    if (!feq(q.x, bq[0]) || !feq(q.y, bq[1]) || !feq(q.z, bq[2]) || !feq(q.w, bq[3])) quats = false;
    if (v.entities[i].components != d.entities[i].components) mats = false;
    if (!v3eq(v.entities[i].pos, d.entities[i].pos) || !v3eq(v.entities[i].scale, d.entities[i].scale)) mats = false;
  }
  CHECK(mats && quats);
  // Bilesen tablolari.
  const SceneBlobEntity &kure = v.entities[0], &boru = v.entities[1], &lamba = v.entities[2], &kutu = v.entities[3], &kg = v.entities[4], &bos = v.entities[5];
  CHECK(kure.draw == 0 && kure.anim == -1 && kure.light == -1 && kure.body == -1);
  CHECK(boru.draw == 1 && boru.anim == 0 && v.anims[0].entity == 1 && feq(v.anims[0].speed, 1.0f / 7.0f) && feq(v.anims[0].phase, 0.35f));
  CHECK(lamba.light == 0 && lamba.draw == -1);
  CHECK(v.draws[0].asset == 0 && v3eq(v.draws[0].tint, {0.85f, 0.9f, 1.0f}) && v.draws[1].asset == 1);
  // Isik konumu = matris cevirisi (dondurulmus varlikta bile konum).
  const Mat4 lm = scene_entity_matrix(d.entities[2]);
  CHECK(feq(v.lights[0].pos[0], lm.m[3][0]) && feq(v.lights[0].pos[1], lm.m[3][1]) && feq(v.lights[0].pos[2], lm.m[3][2]));
  CHECK(v3eq(v.lights[0].color, {1, 0.2f, 0.1f}) && feq(v.lights[0].intensity, 3) && feq(v.lights[0].radius, 8));
  // Govde: olcekli yarim kenar (scene_spawn_bodies ile ayni), kure yaricapi * olcek.x.
  CHECK(kutu.body == 0 && kutu.draw == 2 && v.bodies[0].shape == 0 && v.bodies[0].dynamic == 1);
  CHECK(v3eq(v.bodies[0].half, d.entities[3].half * d.entities[3].scale) && v3eq(v.bodies[0].scale, {2, 1, 1}));
  CHECK(kg.body == 1 && v.bodies[1].shape == 1 && v.bodies[1].dynamic == 0 && feq(v.bodies[1].radius, 0.75f * 3.0f));
  CHECK(bos.draw == -1 && bos.anim == -1 && bos.light == -1 && bos.body == -1);
  // Sinir: kure_govde (-6,0.75,3) yaricap 2.25 -> lo.x -8.25 en kucuk; kutu y 6+ en buyuk.
  CHECK(v.h->bounds_lo[0] <= -8.25f + 1e-4f && v.h->bounds_hi[1] >= 6.0f);
  // Kontrol: tek ulp degisince baytlar ve ozet degisir.
  static SceneDesc c;
  c = d;
  c.entities[0].pos.x = std::nextafterf(c.entities[0].pos.x, 1000.0f);
  size_t n3 = 0;
  void *b3 = compile_to(c, &n3);
  SceneBlobView v3;
  CHECK(b3 && n3 == n1 && std::memcmp(b1, b3, n1) != 0);
  CHECK(scene_blob_open(b3, n3, &v3, &err) && v3.hash() != v.hash());
  // Bos sahne de derlenir ve acilir.
  static SceneDesc empty;
  empty = SceneDesc{};
  size_t ne = 0;
  void *be = compile_to(empty, &ne);
  SceneBlobView ve;
  CHECK(be && scene_blob_open(be, ne, &ve, &err) && ve.h->entity_count == 0 && ve.h->string_size == 1);
  std::printf("    [bilgi] blob %zu bayt, ozet %016llx; bos sahne %zu bayt\n", n1, (unsigned long long)v.hash(), ne);
}

ENGINE_TEST(scene_roundtrip_equality) {
  static SceneDesc d;
  fill(d);
  
  // 1. Yaz (d -> buf1). scene_write METIN yazar: char*, uint8_t* degil.
  static char buf1[32768];
  const size_t n1 = scene_write(d, buf1, sizeof buf1);
  CHECK(n1 > 0 && n1 < sizeof buf1);
  
  // 2. Oku (buf1 -> d2). scene_read diye bir sey yok; metin ayristirici
  // scene_parse(text, len, out, err).
  static SceneDesc d2;
  SceneError err{};
  const bool ok = scene_parse(buf1, n1, &d2, &err);
  CHECK(ok);
  
  // 3. Yaz (d2 -> buf2)
  static char buf2[32768];
  const size_t n2 = scene_write(d2, buf2, sizeof buf2);
  CHECK(n2 == n1);
  
  // 4. Eşitlik (buf1 == buf2)
  CHECK(std::memcmp(buf1, buf2, n1) == 0);
  std::printf("    [bilgi] roundtrip test: yaz-oku-yaz esitligi OK (%zu bayt, sifir veri kaybi)\n", n1);
}

ENGINE_TEST(scene_blob_open_rejects_corruption) {
  static SceneDesc d;
  fill(d);
  size_t n = 0;
  void *good = compile_to(d, &n);
  CHECK(good);
  if (!good) return;
  uint8_t *bad = static_cast<uint8_t *>(arena().alloc(n + 16, kSceneBlobAlign));
  SceneBlobView v;
  SceneError err{};
  auto reset = [&] { std::memcpy(bad, good, n); };
  auto hdr = [&]() { return reinterpret_cast<SceneBlobHeader *>(bad); };
  // Pozitif kontrol: kopya oldugu gibi acilir.
  reset();
  CHECK(scene_blob_open(bad, n, &v, &err));
  // 1) magic
  reset(); hdr()->magic ^= 1;
  CHECK(!scene_blob_open(bad, n, &v, &err) && std::strstr(err.msg, "magic"));
  // 2) surum
  reset(); hdr()->version = 99;
  CHECK(!scene_blob_open(bad, n, &v, &err) && std::strstr(err.msg, "surum"));
  // 3) kesik dosya (son 16 bayt yok)
  reset();
  CHECK(!scene_blob_open(bad, n - 16, &v, &err) && std::strstr(err.msg, "boyut"));
  // 4) icerik biti (ozet yakalar): metin tablosundaki bir harf
  reset(); bad[hdr()->string_offset] ^= 0x20;
  CHECK(!scene_blob_open(bad, n, &v, &err) && std::strstr(err.msg, "ozet"));
  // 5) ofset sinir disi, ozet yeniden hesaplanmis (ozet tek savunma degil)
  auto rehash = [&] {
    const size_t from = offsetof(SceneBlobHeader, header_size);
    const uint64_t hv = scene_blob_fnv1a(bad + from, n - from);
    hdr()->hash_lo = (uint32_t)(hv & 0xFFFFFFFFu); hdr()->hash_hi = (uint32_t)(hv >> 32);
  };
  reset(); hdr()->entity_offset = (uint32_t)n; rehash();
  CHECK(!scene_blob_open(bad, n, &v, &err) && std::strstr(err.msg, "varlik tablosu"));
  reset(); hdr()->light_count = 1000; rehash();
  CHECK(!scene_blob_open(bad, n, &v, &err) && std::strstr(err.msg, "isik tablosu"));
  reset(); hdr()->string_offset += 4; rehash(); // hizasiz
  CHECK(!scene_blob_open(bad, n, &v, &err) && std::strstr(err.msg, "metin tablosu"));
  // 6) dizin tutarsizligi: cizim tablosunun kaynak dizini tanimsiz
  reset();
  { auto *dr = reinterpret_cast<SceneBlobDraw *>(bad + hdr()->draw_offset); dr[0].asset = 7; }
  rehash();
  CHECK(!scene_blob_open(bad, n, &v, &err) && std::strstr(err.msg, "kaynak dizini"));
  // 7) varligin bilesen dizini tablo disi
  reset();
  { auto *en = reinterpret_cast<SceneBlobEntity *>(bad + hdr()->entity_offset); en[0].light = 5; }
  rehash();
  CHECK(!scene_blob_open(bad, n, &v, &err) && std::strstr(err.msg, "tablo disi"));
  // 8) endian isareti
  reset(); hdr()->endian = 0x04030201u; rehash();
  CHECK(!scene_blob_open(bad, n, &v, &err) && std::strstr(err.msg, "bayt sirasi"));
  // 9) hizasiz isaretci
  reset();
  CHECK(!scene_blob_open(bad + 4, n, &v, &err) && std::strstr(err.msg, "hizali"));
  // 10) bos veri / kisa
  CHECK(!scene_blob_open(nullptr, 0, &v, &err));
  CHECK(!scene_blob_open(bad, 8, &v, &err));
  // Son: bozulmamis kopya hala acilir (testin kendisi blob'u bozmadi).
  reset();
  CHECK(scene_blob_open(bad, n, &v, &err));
  std::printf("    [bilgi] 10 bozulma reddedildi; son hata: %s\n", err.msg);
}

ENGINE_TEST(scene_blob_file_roundtrip_and_path) {
  static SceneDesc d;
  fill(d);
  size_t n = 0;
  void *mem = compile_to(d, &n);
  char tmpl[512];
  tmp_template(tmpl, sizeof tmpl, "sahneb");
  int fd = mkstemp(tmpl);
  CHECK(fd >= 0);
  if (fd < 0) return;
  close(fd);
  SceneError err{};
  CHECK(scene_blob_save(arena(), d, tmpl, &err));
  SceneBlobView v;
  const bool ok = scene_blob_load(arena(), tmpl, &v, &err);
  if (!ok) std::printf("    [bilgi] yukleme: %s\n", err.msg);
  CHECK(ok);
  if (ok) CHECK(v.h->total_size == n && std::memcmp(v.h, mem, n) == 0); // dosya = bellekteki derleme
  // Kesik dosya reddedilir.
  FILE *f = std::fopen(tmpl, "wb");
  if (f) { std::fwrite(mem, 1, n - 32, f); std::fclose(f); }
  CHECK(!scene_blob_load(arena(), tmpl, &v, &err));
  unlink(tmpl);
  CHECK(!scene_blob_load(arena(), "/olmayan/x.sahneb", &v, &err) && err.line == 0);
  char out[64];
  CHECK(scene_blob_path_for("a/b/editor.sahne", out, sizeof out) && std::strcmp(out, "a/b/editor.sahneb") == 0);
  CHECK(scene_blob_path_for("x", out, sizeof out) && std::strcmp(out, "x.sahneb") == 0);
  CHECK(!scene_blob_path_for("cok/uzun/bir/yol/xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx.sahne", out, sizeof out));
  // editor.sahne derlenir ve editor_app'in bekledigi sayilar cikar.
  static SceneDesc ed;
  char path[1024];
  asset_path(path, sizeof path, "editor.sahne");
  if (scene_load(arena(), path, &ed, &err)) {
    size_t ne = 0;
    void *be = compile_to(ed, &ne);
    SceneBlobView ve;
    CHECK(be && scene_blob_open(be, ne, &ve, &err));
    CHECK(ve.h->entity_count == 8 && ve.h->asset_count == 3 && ve.h->draw_count == 6 && ve.h->anim_count == 2 && ve.h->light_count == 1 && ve.h->body_count == 2);
    std::printf("    [bilgi] editor.sahne -> %zu bayt blob, ozet %016llx\n", ne, (unsigned long long)ve.hash());
  } else std::printf("    [bilgi] editor.sahne yok (%s), derleme atlandi\n", err.msg);
}

ENGINE_TEST(scene_history_world_op_undo_redo_restores_bytes) {
  static SceneDesc d;
  static char t0[64 << 10], t1[64 << 10], t2[64 << 10];
  fill(d);
  const size_t n0 = scene_write(d, t0, sizeof t0);
  SceneHistory h;
  CHECK(h.init(arena(), 16));
  SceneWorld w = d.world();
  CHECK(!h.set_world(d, w)); // ayni: islem yok
  w.sun_diffuse = 0.3f;
  w.ambient = {0.05f, 0.05f, 0.1f};
  w.cam_radius = 40.0f;
  CHECK(h.set_world(d, w));
  CHECK(feq(d.sun_diffuse, 0.3f) && feq(d.cam_radius, 40.0f) && h.undo_count() == 1);
  const size_t n1 = scene_write(d, t1, sizeof t1);
  CHECK(!(n1 == n0 && std::memcmp(t0, t1, n0) == 0)); // metin degisti
  // Varlik islemi ile karisik sira: dunya, varlik, dunya.
  SceneEntity e = d.entities[0];
  e.pos.y += 2;
  CHECK(h.set_entity(d, 0, e));
  w.shadow_radius = 5;
  CHECK(h.set_world(d, w) && h.undo_count() == 3);
  CHECK(h.undo(d));
  CHECK(feq(d.shadow_radius, 17.0f)); // son dunya islemi geri: golge yaricapi varsayilana
  CHECK(h.undo(d) && h.undo(d) && h.undo_count() == 0);
  size_t nn = scene_write(d, t2, sizeof t2);
  CHECK(nn == n0 && std::memcmp(t0, t2, n0) == 0); // geri al = baslangic baytlari
  CHECK(h.redo(d) && h.redo(d) && h.redo(d) && h.redo_count() == 0);
  CHECK(feq(d.shadow_radius, 5.0f) && feq(d.entities[0].pos.y, 1.2f + 2.0f));
  CHECK(h.undo(d) && h.undo(d));
  nn = scene_write(d, t2, sizeof t2);
  CHECK(nn == n1 && std::memcmp(t1, t2, n1) == 0); // ilk dunya islemi sonrasi baytlar
  std::printf("    [bilgi] dunya islemi: geri al/yinele bayt-esit; 3 islemli karisik sira OK\n");
}

ENGINE_TEST(scene_runtime_draws_blob_entities_offscreen) {
  char path[1024];
  asset_path(path, sizeof path, "editor.sahne");
  static SceneDesc d;
  SceneError err{};
  if (!scene_load(arena(), path, &d, &err)) { skip("editor.sahne yok (tests/assets ya da TULPAR_ENGINE_ASSETS)"); return; }
  if (!rhi::vk_api_load(g_api)) { skip("Vulkan loader yok"); return; }
  rhi::Device dev;
  rhi::DeviceConfig dc;
  if (!dev.init(arena(), g_api, dc)) { skip("Vulkan cihazi yok"); return; }
  const uint32_t W = 320, H = 200;
  rhi::OffscreenConfig oc;
  oc.srgb = true; oc.width = W; oc.height = H;
  rhi::OffscreenResult ores;
  rhi::OffscreenTarget *off = rhi::offscreen_create(dev, arena(), oc, &ores);
  if (!off) { CHECK(false); dev.shutdown(); return; }
  renderer::Renderer ren;
  renderer::RendererConfig rc;
  rc.frames_in_flight = 1;
  rc.shadow_size = 0;
  if (!ren.init(dev, arena(), rhi::offscreen_render_pass(off), rc)) { CHECK(false); rhi::offscreen_destroy(off); dev.shutdown(); return; }
  ren.set_render_size(W, H);
  // Derle -> ac -> runtime (kaynaklar sahne dizininden).
  size_t n = 0;
  void *blob = compile_to(d, &n);
  SceneBlobView v;
  CHECK(blob && scene_blob_open(blob, n, &v, &err));
  char dir[1024];
  scene_dir_of(path, dir, sizeof dir);
  static SceneRuntime rt;
  const bool ok = rt.init(arena(), ren, v, dir);
  CHECK(ok);
  CHECK(rt.stats().assets_loaded == 3 && rt.stats().assets_failed == 0);
  rt.apply_world(ren);
  const SceneWorld w = v.world();
  const Vec3 eye = {w.cam_target.x + std::cos(w.cam_pitch) * std::sin(w.cam_yaw) * w.cam_radius, w.cam_target.y + std::sin(w.cam_pitch) * w.cam_radius,
                    w.cam_target.z + std::cos(w.cam_pitch) * std::cos(w.cam_yaw) * w.cam_radius};
  ren.set_camera(Mat4::look_at(eye, w.cam_target, {0, 1, 0}), Mat4::perspective(0.9f, (float)W / (float)H, 0.1f, 200.0f));
  Rec rr{&ren};
  static uint8_t px_scene[W * H * 4], px_empty[W * H * 4], px_empty2[W * H * 4];
  // Beklenen cizim: model varliklarinin instance toplami.
  uint32_t expected_draws = 0;
  for (uint32_t i = 0; i < v.h->draw_count; i++) if (const Model *m = rt.model(v.draws[i].asset)) expected_draws += m->instance_count;
  // 1) sahne
  ren.begin_frame(0);
  ren.clear_point_lights();
  rt.draw(ren, eye, 0.5f, nullptr);
  CHECK(rt.stats().draws == v.h->draw_count && rt.stats().lights == v.h->light_count && ren.point_light_count() == 1);
  bool f1 = rhi::offscreen_render_custom(off, oc, rec_main, &rr, &ores, rec_shadow);
  CHECK(f1);
  if (f1) std::memcpy(px_scene, ores.pixels, sizeof px_scene);
  const uint32_t gpu_draws = ren.stats().draws; // kayitta sayilir (record), cizim cagrisinda degil
  CHECK(gpu_draws == expected_draws && expected_draws >= 6);
  // 2) kontrol: bos kare (iki kez -> 0 fark; sahne vs bos -> cok fark)
  for (int k = 0; k < 2; k++) {
    ren.begin_frame(0);
    ren.clear_point_lights();
    bool f = rhi::offscreen_render_custom(off, oc, rec_main, &rr, &ores, rec_shadow);
    CHECK(f);
    if (f) std::memcpy(k == 0 ? px_empty : px_empty2, ores.pixels, sizeof px_empty);
  }
  const uint32_t diff_scene = count_diff(px_scene, px_empty, W * H), diff_ctrl = count_diff(px_empty, px_empty2, W * H);
  std::printf("    [bilgi] cizim %u (beklenen %u), isik %u; piksel farki sahne-bos %u, bos-bos %u\n", gpu_draws, expected_draws,
              rt.stats().lights, diff_scene, diff_ctrl);
  // Sanal GPU'da (Apple Paravirtual, CI macOS) sahne karesi bos cikiyor;
  // ayni sinif test.hpp'de belgelendi. ATLAMA YALNIZ PIKSEL OLCUMUNE —
  // kaparin geri kalani (blob, cizim sayisi, isik, fizik, navmesh) macOS'ta
  // da kosuyor. `diff_ctrl == 0` (iki bos kare ayni) sanal GPU'da da gecerli
  // oldugu icin O KALIYOR: yazicinin sabit ciktigini hala olcuyoruz.
  CHECK(diff_ctrl == 0);
  if (test::gpu_is_virtual(dev.caps().device_name))
    skip("sanal GPU (Apple Paravirtual, CI macOS): sahne-bos piksel farki gercek cihazda olculur");
  else
    CHECK(diff_scene > 500);
  // 3) fizik: govdeler sim'e, dusen kup 2 s sonra yazar konumundan asagida; sabit duvar yerinde.
  sim::Physics ph;
  sim::PhysicsConfig pc;
  pc.threads = 1;
  if (ph.init(arena(), pc)) {
    CHECK(rt.spawn(ph) == v.h->body_count && v.h->body_count == 2);
    for (int i = 0; i < 120; i++) ph.step(1.0f / 60.0f, 1);
    const int32_t kup = d.find_entity("kup_dusen"), duvar = d.find_entity("duvar_sabit");
    CHECK(kup >= 0 && duvar >= 0);
    if (kup >= 0 && duvar >= 0) {
      const Mat4 ms = rt.entity_matrix((uint32_t)kup, &ph), ma = v.entity_matrix((uint32_t)kup);
      CHECK(ms.m[3][1] < ma.m[3][1] - 1.0f); // dustu
      const Mat4 ds = rt.entity_matrix((uint32_t)duvar, &ph), da = v.entity_matrix((uint32_t)duvar);
      CHECK(std::memcmp(&ds.m[0][0], &da.m[0][0], sizeof ds.m) == 0); // sabit: yazar matrisi
      std::printf("    [bilgi] kup_dusen y: yazar %.2f -> sim %.2f (2 s)\n", ma.m[3][1], ms.m[3][1]);
    }
    ren.begin_frame(0);
    ren.clear_point_lights();
    rt.draw(ren, eye, 1.0f, &ph);
    CHECK(rt.stats().draws == v.h->draw_count);
    rt.despawn(ph);
    ph.shutdown();
  } else CHECK(false);
  dev.api().vkDeviceWaitIdle(dev.handle());
  ren.shutdown();
  rhi::offscreen_destroy(off);
  dev.shutdown();
}


// --- Faz 6: sahne derleyicisi (yerlesik kume / tepe bellek / onbellek) ------
namespace {
// Testin KENDI hesabi (derleyicinin formulunden bagimsiz yazildi): modelin GPU
// yerlesik baytlari ve en buyuk tek staging blogu.
struct OwnMeasure {
  uint64_t gpu = 0, cpu = 0;
  uint32_t transient = 0;
};
OwnMeasure measure_own(Arena &a, const char *path) {
  OwnMeasure m;
  const size_t before = a.used();
  Model mdl;
  if (!gltf_load(a, path, &mdl)) return m;
  m.cpu = a.used() - before;
  for (uint32_t i = 0; i < mdl.mesh_count; i++) {
    const ModelMesh &me = mdl.meshes[i];
    const uint64_t vb = (uint64_t)me.vertex_count * (me.skin >= 0 ? 32u : 20u); // paketlenmis GPU vertex
    uint64_t ib = (uint64_t)me.index_count * 4u;
    m.gpu += vb + ib;
    if (vb > m.transient) m.transient = (uint32_t)vb;
    if (ib > m.transient) m.transient = (uint32_t)ib;
    for (uint32_t l = 0; l < kModelMaxLods; l++) {
      const uint64_t lb = (uint64_t)me.lod_index_count[l] * 4u;
      m.gpu += lb;
      if (lb > m.transient) m.transient = (uint32_t)lb;
    }
  }
  for (uint32_t i = 0; i < mdl.image_count; i++) {
    const uint64_t base = (uint64_t)mdl.images[i].width * mdl.images[i].height * 4u;
    m.gpu += base * 4u / 3u; // mip zinciri yaklasik (derleyici tam toplami tutar)
    if (base > m.transient) m.transient = (uint32_t)base;
  }
  return m;
}
bool within(uint64_t got, uint64_t expect, double tol) {
  if (expect == 0) return got == 0;
  const double r = (double)got / (double)expect;
  return r > 1.0 - tol && r < 1.0 + tol;
}
// Kaynakli sahne: her kaynaga bir model varligi.
void fill_assets(SceneDesc &d, uint32_t n) {
  static const char *names[3] = {"checker_cube.gltf", "lod_sphere.gltf", "skin_tube.gltf"};
  d = SceneDesc{};
  for (uint32_t i = 0; i < n && i < 3; i++) {
    d.add_asset(names[i]);
    SceneEntity e{};
    std::snprintf(e.name, sizeof e.name, "varlik_%u", i);
    e.pos = {(float)i * 3.0f, 0, 0};
    e.components = kSceneModel;
    e.asset = (int32_t)i;
    d.insert_entity(d.entity_count, e);
  }
}
// Navmesh sahnesi: 20x20 sabit zemin + gecidi olan duvar (test_navmesh ile ayni
// yerlesim, ama SAHNE VARLIKLARI olarak — corbayi derleyici cikarir).
void fill_nav_scene(SceneDesc &d, bool obstacle) {
  d = SceneDesc{};
  SceneEntity e{};
  std::snprintf(e.name, sizeof e.name, "zemin");
  e.pos = {0, -0.5f, 0};
  e.components = kSceneBody;
  e.shape = SceneShape::Box;
  e.half = {10, 0.5f, 10};
  e.dynamic = false;
  d.insert_entity(d.entity_count, e);
  e = SceneEntity{};
  std::snprintf(e.name, sizeof e.name, "duvar");
  e.pos = {0, 1.5f, -2.0f};
  e.components = kSceneBody;
  e.shape = SceneShape::Box;
  e.half = {0.5f, 1.5f, 8.0f}; // z: -10..6, gecit z=6..10
  e.dynamic = false;
  d.insert_entity(d.entity_count, e);
  if (obstacle) { // KONTROL: gecide engel -> yol degismeli
    e = SceneEntity{};
    std::snprintf(e.name, sizeof e.name, "engel");
    e.pos = {3.0f, 1.5f, 7.5f};
    e.components = kSceneBody;
    e.shape = SceneShape::Box;
    e.half = {3.0f, 1.5f, 2.5f};
    e.dynamic = false;
    d.insert_entity(d.entity_count, e);
  }
  SceneEntity dyn{};
  std::snprintf(dyn.name, sizeof dyn.name, "dinamik_kup"); // dinamik govde corbaya GIRMEZ
  dyn.pos = {0, 5, 0};
  dyn.components = kSceneBody;
  dyn.shape = SceneShape::Box;
  dyn.half = {0.5f, 0.5f, 0.5f};
  dyn.dynamic = true;
  d.insert_entity(d.entity_count, dyn);
}
float path_len(const Vec3 *p, int n) {
  float l = 0;
  for (int i = 1; i < n; i++) l += length(p[i] - p[i - 1]);
  return l;
}
} // namespace

ENGINE_TEST(scene_compile_measures_resident_set_and_peak) {
  char dir[1024], path[1024];
  asset_path(path, sizeof path, "checker_cube.gltf");
  scene_dir_of(path, dir, sizeof dir);
  static SceneDesc d;
  fill_assets(d, 2);
  // Onbellek: testin kendi gecici dizini (repo kirlenmesin).
  char cdir[512];
  std::snprintf(cdir, sizeof cdir, "%s/sahnec_onbellek_XXXXXX", tmp_dir());
  const bool have_cache_dir = mkdtemp(cdir) != nullptr;
  ImportCache cache;
  SceneCompileOptions opt;
  opt.bake_nav = false;
  if (have_cache_dir && import_cache_init(cache, cdir)) opt.cache = &cache;
  SceneBlobExtras x;
  SceneCompileReport rep;
  const bool ok = scene_compile(arena(), d, dir, opt, &x, &rep);
  CHECK(ok);
  if (!ok) return;
  if (rep.assets_measured != 2) { skip("tests/assets glTF dosyalari yok (olcum kapisi kosmadi)"); return; }
  CHECK(rep.cache_misses == 2 && rep.cache_hits == 0); // ilk gecis: is yapildi

  // Blob'a yazilir ve blob'dan OKUNUR (runtime tahmin etmez).
  size_t n = 0;
  const size_t need = scene_blob_compile_ex(d, &x, nullptr, 0);
  void *buf = arena().alloc(need, kSceneBlobAlign);
  CHECK(buf && scene_blob_compile_ex(d, &x, buf, need) == need);
  n = need;
  SceneBlobView v;
  SceneError err{};
  CHECK(scene_blob_open(buf, n, &v, &err));
  CHECK(v.h->version == kSceneBlobVersion && v.h->resident_count == 2 && v.residents != nullptr);
  CHECK(v.h->resident_cpu == rep.resident_cpu && v.h->resident_gpu == rep.resident_gpu && v.h->peak_bytes == rep.peak_bytes);

  // OLCUM: ayni kaynaklari taze olc (testin kendi formulu) ve karsilastir.
  const size_t mark = arena().mark();
  OwnMeasure own[2];
  uint64_t own_cpu = 0, own_gpu = 0;
  uint32_t own_transient = 0;
  for (uint32_t i = 0; i < 2; i++) {
    char ap[1200];
    std::snprintf(ap, sizeof ap, "%s/%s", dir, d.assets[i]);
    own[i] = measure_own(arena(), ap);
    own_cpu += own[i].cpu;
    own_gpu += own[i].gpu;
    if (own[i].transient > own_transient) own_transient = own[i].transient;
  }
  arena().reset_to(mark);
  std::printf("    [bilgi] yerlesik kume: blob CPU %u B / olculen %llu B, blob GPU %u B / olculen %llu B; gecici blob %u B / olculen %u B; tepe %u B\n",
              v.h->resident_cpu, (unsigned long long)own_cpu, v.h->resident_gpu, (unsigned long long)own_gpu, v.h->peak_transient,
              own_transient, v.h->peak_bytes);
  CHECK(within(v.h->resident_cpu, own_cpu, 0.20));
  CHECK(within(v.h->resident_gpu, own_gpu, 0.20));
  CHECK(v.h->peak_transient == own_transient);
  CHECK(v.h->peak_bytes == v.h->resident_cpu + v.h->resident_gpu + v.h->peak_transient);
  CHECK(v.h->budget_flags == 0);

  // KONTROL 1: sahneye ucuncu kaynak eklenince sayilar ARTAR.
  static SceneDesc d3;
  fill_assets(d3, 3);
  SceneBlobExtras x3;
  SceneCompileReport rep3;
  CHECK(scene_compile(arena(), d3, dir, opt, &x3, &rep3));
  std::printf("    [bilgi] kontrol: 2 kaynak -> CPU %u B, GPU %u B; 3 kaynak -> CPU %u B, GPU %u B\n", rep.resident_cpu, rep.resident_gpu,
              rep3.resident_cpu, rep3.resident_gpu);
  CHECK(rep3.resident_cpu > rep.resident_cpu && rep3.resident_gpu > rep.resident_gpu);

  // KONTROL 2: ayni sahne ikinci kez derlenince ONBELLEK ISABET eder (is atlanir)
  // ve sayilar birebir ayni cikar.
  if (opt.cache) {
    SceneBlobExtras x2;
    SceneCompileReport rep2;
    CHECK(scene_compile(arena(), d, dir, opt, &x2, &rep2));
    CHECK(rep2.cache_hits == 2 && rep2.cache_misses == 0);
    CHECK(rep2.resident_cpu == rep.resident_cpu && rep2.resident_gpu == rep.resident_gpu && rep2.peak_bytes == rep.peak_bytes);
    // KONTROL 3: bos (taze) onbellek dizini -> yine is yapilir.
    char cdir2[512];
    std::snprintf(cdir2, sizeof cdir2, "%s/sahnec_onbellek2_XXXXXX", tmp_dir());
    if (mkdtemp(cdir2)) {
      ImportCache fresh;
      SceneCompileOptions opt2 = opt;
      if (import_cache_init(fresh, cdir2)) {
        opt2.cache = &fresh;
        SceneBlobExtras x4;
        SceneCompileReport rep4;
        CHECK(scene_compile(arena(), d, dir, opt2, &x4, &rep4));
        CHECK(rep4.cache_hits == 0 && rep4.cache_misses == 2);
        std::printf("    [bilgi] onbellek: 2. derleme %u isabet / %u iska (kontrol: taze dizin %u isabet / %u iska)\n", rep2.cache_hits,
                    rep2.cache_misses, rep4.cache_hits, rep4.cache_misses);
      }
      rmdir(cdir2); // urunler kalirsa dizin silinmez (gecici dizin)
    }
  }
  if (have_cache_dir) rmdir(cdir);
}

ENGINE_TEST(scene_blob_navmesh_bake_matches_runtime_bake) {
  static SceneDesc d;
  fill_nav_scene(d, false);
  SceneCompileOptions opt;
  opt.measure_resident = false;
  opt.nav.agent_radius = 0.4f;
  SceneBlobExtras x;
  SceneCompileReport rep;
  CHECK(scene_compile(arena(), d, ".", opt, &x, &rep));
  if (!rep.nav_ok) { std::printf("    [bilgi] bake: %s\n", rep.nav_error); CHECK(false); return; }
  CHECK(rep.nav_tris == 24); // 2 sabit kutu x 12 ucgen (dinamik kup HARIC)
  CHECK(rep.nav_polys > 2 && rep.nav_bytes > 0);

  const size_t need = scene_blob_compile_ex(d, &x, nullptr, 0);
  void *buf = arena().alloc(need, kSceneBlobAlign);
  SceneBlobView v;
  SceneError err{};
  CHECK(buf && scene_blob_compile_ex(d, &x, buf, need) == need);
  CHECK(scene_blob_open(buf, need, &v, &err));
  CHECK(v.has_nav() && v.h->nav_size == rep.nav_bytes && v.h->nav_polys == rep.nav_polys);
  CHECK(v.h->nav_agent_radius == 0.4f && v.h->nav_tris == 24);

  // RUNTIME: blob'dan sorgu (bake YOK).
  static SceneNav nav;
  const bool nav_ok = nav.init(arena(), v);
  CHECK(nav_ok);
  if (!nav_ok) return;
  Vec3 blob_pts[64];
  bool blob_partial = true;
  const int blob_n = nav.find_path({-6, 0, 0}, {6, 0, 0}, blob_pts, 64, &blob_partial);
  const float blob_len = path_len(blob_pts, blob_n);

  // KIYAS: ayni corbadan sim::NavMesh runtime bake + sorgu.
  const size_t mark = arena().mark();
  float *verts = arena().alloc_array<float>(kSceneNavMaxVerts * 3);
  int *tris = arena().alloc_array<int>(kSceneNavMaxTris * 3);
  CHECK(verts && tris);
  uint32_t nv = 0;
  const uint32_t nt = scene_nav_soup(d, verts, kSceneNavMaxVerts, tris, kSceneNavMaxTris, &nv);
  sim::NavMesh nm;
  const bool built = nt && nm.build(verts, (int)nv, tris, (int)nt, opt.nav) && nm.init_query(2048);
  CHECK(built);
  Vec3 sim_pts[64];
  bool sim_partial = true;
  const int sim_n = built ? nm.find_path({-6, 0, 0}, {6, 0, 0}, sim_pts, 64, &sim_partial) : 0;
  const float sim_len = path_len(sim_pts, sim_n);
  // Bake edilen veri BIT ESIT olmali: iki yol ayni Recast ardisikligini kosuyor.
  CHECK(built && nav.data_hash() == nm.data_hash());
  CHECK(blob_n == sim_n && blob_n >= 3 && !blob_partial && !sim_partial);
  CHECK(std::fabs(blob_len - sim_len) < 1e-3f);
  CHECK(blob_len > 16.0f); // duz mesafe 12; gecitten dolasma
  bool outside = true;
  for (int i = 0; i < blob_n; i++)
    if (std::fabs(blob_pts[i].x) < 0.5f && blob_pts[i].z < 6.0f) outside = false;
  CHECK(outside); // yol duvarin icinden gecmiyor
  std::printf("    [bilgi] navmesh: %u ucgen -> %u poligon, %u bayt, ozet %016llx; blob yolu %d nokta %.2f birim, sim bake %d nokta %.2f birim\n",
              rep.nav_tris, rep.nav_polys, rep.nav_bytes, (unsigned long long)nav.data_hash(), blob_n, (double)blob_len, sim_n,
              (double)sim_len);

  // DOGRU GORUS (raycast): blob yuzu ile sim bake'i AYNI cevabi vermeli.
  // Duvarin ardina bakan isin ENGELLI, ayni acik zeminde kisa isin TEMIZ.
  // Bu ikisi birlikte kapiyi olculebilir yapiyor: yalniz "engelli" baksaydik
  // her zaman true donen bir gerceklestirme de gecerdi.
  float t_blob = -1.0f, t_sim = -1.0f;
  const bool blocked_blob = nav.raycast({-6, 0, 0}, {6, 0, 0}, &t_blob);
  const bool blocked_sim = built && nm.raycast({-6, 0, 0}, {6, 0, 0}, &t_sim);
  CHECK(blocked_blob && blocked_sim);
  CHECK(std::fabs(t_blob - t_sim) < 1e-3f);
  CHECK(t_blob > 0.0f && t_blob < 1.0f);
  float t_clear = -1.0f;
  const bool blocked_near = nav.raycast({-6, 0, 0}, {-5, 0, 0}, &t_clear);
  CHECK(!blocked_near && std::fabs(t_clear - 1.0f) < 1e-3f); // KONTROL: temiz isin
  // En yakin nokta: mesh'in 3 birim USTUNDEKI nokta zemine insin.
  Vec3 snapped{};
  const bool snap_ok = nav.nearest_point({-6, 3, 0}, &snapped);
  CHECK(snap_ok && std::fabs(snapped.y) < 1.0f);
  // KONTROL: arama kutusunun cok disindaki nokta BULUNMASIN (sessiz 0 degil).
  Vec3 far_out{};
  CHECK(!nav.nearest_point({-600, 300, 400}, &far_out));
  std::printf("    [bilgi] navmesh gorus: duvar ardi ENGEL t=%.3f (sim t=%.3f), kisa isin temiz t=%.3f; (-6,3,0) -> y=%.3f\n",
              (double)t_blob, (double)t_sim, (double)t_clear, (double)snapped.y);

  nm.shutdown();
  arena().reset_to(mark);

  // KONTROL: gecide engel eklenince yol DEGISIR (kapi gercekten yolu olcuyor).
  static SceneDesc d2;
  fill_nav_scene(d2, true);
  SceneBlobExtras x2;
  SceneCompileReport rep2;
  CHECK(scene_compile(arena(), d2, ".", opt, &x2, &rep2));
  CHECK(rep2.nav_tris == 36 && rep2.nav_ok);
  CHECK(rep2.nav_hash != rep.nav_hash); // farkli sahne -> farkli bake
  const size_t need2 = scene_blob_compile_ex(d2, &x2, nullptr, 0);
  void *buf2 = arena().alloc(need2, kSceneBlobAlign);
  SceneBlobView v2;
  CHECK(buf2 && scene_blob_compile_ex(d2, &x2, buf2, need2) == need2 && scene_blob_open(buf2, need2, &v2, &err));
  static SceneNav nav2;
  CHECK(nav2.init(arena(), v2));
  Vec3 pts2[64];
  bool partial2 = false;
  const int n2 = nav2.find_path({-6, 0, 0}, {6, 0, 0}, pts2, 64, &partial2);
  const float len2 = path_len(pts2, n2);
  std::printf("    [bilgi] kontrol: engelli sahne %u ucgen -> yol %d nokta %.2f birim (engelsiz %d nokta %.2f birim)\n", rep2.nav_tris, n2,
              (double)len2, blob_n, (double)blob_len);
  CHECK(n2 != blob_n || std::fabs(len2 - blob_len) > 0.25f || partial2);
  nav2.shutdown();
  nav.shutdown();
}

ENGINE_TEST(scene_blob_rejects_older_version) {
  static SceneDesc d;
  fill(d);
  size_t n = 0;
  void *good = compile_to(d, &n);
  CHECK(good && n);
  if (!good) return;
  uint8_t *bad = static_cast<uint8_t *>(arena().alloc(n, kSceneBlobAlign));
  CHECK(bad);
  if (!bad) return;
  std::memcpy(bad, good, n);
  auto *h = reinterpret_cast<SceneBlobHeader *>(bad);
  CHECK(h->version == kSceneBlobVersion && kSceneBlobVersion == 5);
  // Her ESKI surum ayni anlamli hatayla reddedilmeli: 1 (Faz 6 oncesi),
  // 2 (yerlesik kume + navmesh, kume DAG YOK), 3 (GI sonda bolumu YOK) ve
  // 4 (SceneBlobDraw 24 bayt: ilkel geometri ve malzeme alanlari YOK).
  // Dongu kSceneBlobVersion'a kadar gittigi icin yeni surumler kendiliginden
  // kapsanir; yalniz yukaridaki sabit guncellenir.
  SceneBlobView v;
  SceneError err{};
  for (uint32_t old = 1; old < kSceneBlobVersion; old++) {
    std::memcpy(bad, good, n);
    h->version = old;
    const size_t from = offsetof(SceneBlobHeader, header_size);
    const uint64_t hv = scene_blob_fnv1a(bad + from, n - from);
    h->hash_lo = (uint32_t)(hv & 0xFFFFFFFFu);
    h->hash_hi = (uint32_t)(hv >> 32);
    CHECK(!scene_blob_open(bad, n, &v, &err));
    CHECK(std::strstr(err.msg, "surum") && std::strstr(err.msg, "yeniden derleyin"));
    std::printf("    [bilgi] eski surum %u reddi: %s\n", old, err.msg);
  }
  // Pozitif kontrol: dogru surum hala acilir.
  std::memcpy(bad, good, n);
  CHECK(scene_blob_open(bad, n, &v, &err));
}

// Faz 9 — KUME (cluster) DAG blob'a bake ediliyor mu: derleyici tarafinda
// kurulan DAG blob'a yaziliyor, acilisan dogrulamadan geciyor ve sayilar
// bit-tam geri geliyor mu. Bozuk DAG bolumu REDDEDILMELI (kontrol).
ENGINE_TEST(scene_blob_carries_cluster_dag) {
  static SceneDesc d;
  fill(d);
  const size_t mark = arena().mark();
  // Prosedurel mesh (varlik dosyasi yok): 32x32 izgara, 2048 ucgen.
  const uint32_t N = 32, VN = (N + 1) * (N + 1);
  renderer::Vertex *vb = arena().alloc_array_zeroed<renderer::Vertex>(VN);
  uint32_t *ib = arena().alloc_array<uint32_t>(N * N * 6);
  CHECK(vb && ib);
  if (!vb || !ib) return;
  for (uint32_t z = 0; z <= N; z++)
    for (uint32_t x = 0; x <= N; x++) {
      const float fx = (float)x / (float)N * 2.0f - 1.0f, fz = (float)z / (float)N * 2.0f - 1.0f;
      vb[z * (N + 1) + x].pos = {fx, 0.3f * (1.0f - fx * fx) * (1.0f - fz * fz), fz};
      vb[z * (N + 1) + x].nrm = {0, 1, 0};
    }
  uint32_t k = 0;
  for (uint32_t z = 0; z < N; z++)
    for (uint32_t x = 0; x < N; x++) {
      const uint32_t a0 = z * (N + 1) + x, b0 = a0 + 1, c0 = a0 + (N + 1), d0 = c0 + 1;
      ib[k++] = a0; ib[k++] = c0; ib[k++] = b0;
      ib[k++] = b0; ib[k++] = c0; ib[k++] = d0;
    }
  ModelMesh mm{};
  mm.verts = vb; mm.vertex_count = VN; mm.indices = ib; mm.index_count = k;

  ClusterDag dag;
  CHECK(cluster_dag_build(arena(), mm, cluster_dag_preset(DeviceClass::Mid), &dag));
  if (dag.node_count == 0) { CHECK(false); return; }
  ClusterDagBakeItem item{&dag, 0, 0};
  SceneBlobExtras x{};
  CHECK(cluster_dag_bake(arena(), &item, 1, &x));
  x.dag_device_class = (uint32_t)DeviceClass::Mid;

  const size_t need = scene_blob_compile_ex(d, &x, nullptr, 0);
  const size_t plain = scene_blob_compile_ex(d, nullptr, nullptr, 0);
  uint8_t *buf = static_cast<uint8_t *>(arena().alloc(need, kSceneBlobAlign));
  CHECK(buf && scene_blob_compile_ex(d, &x, buf, need) == need);
  SceneBlobView v;
  SceneError err{};
  const bool opened = scene_blob_open(buf, need, &v, &err);
  if (!opened) std::printf("    [bilgi] acilis hatasi: %s\n", err.msg);
  CHECK(opened);
  if (!opened) return;
  std::printf("    [bilgi] DAG blob: %u kume, %u dugum, %u indeks, %u cocuk, %u seviye; blob %zu B (DAG'siz %zu B, fark %zu B)\n",
              v.h->dag_mesh_count, v.h->dag_node_count, v.h->dag_index_count, v.h->dag_child_count, v.h->dag_levels, need, plain,
              need - plain);
  CHECK(v.has_dag());
  CHECK(v.h->dag_mesh_count == 1 && v.h->dag_node_count == dag.node_count && v.h->dag_index_count == dag.index_count);
  CHECK(v.h->dag_child_count == dag.child_count && v.h->dag_levels == dag.levels);
  CHECK(v.h->dag_device_class == (uint32_t)DeviceClass::Mid);
  CHECK(v.dag_meshes[0].vertex_count == mm.vertex_count && v.dag_meshes[0].levels == dag.levels);
  // Dugumler bit-tam (tek mesh: ofset kaydirmasi 0).
  CHECK(std::memcmp(v.dag_nodes, dag.nodes, sizeof(SceneBlobDagNode) * dag.node_count) == 0);
  CHECK(std::memcmp(v.dag_indices, dag.indices, sizeof(uint32_t) * dag.index_count) == 0);
  // Ayni girdi -> ayni baytlar (determinizm sozlesmesi blob tarafinda da).
  uint8_t *again = static_cast<uint8_t *>(arena().alloc(need, kSceneBlobAlign));
  CHECK(again && scene_blob_compile_ex(d, &x, again, need) == need && std::memcmp(buf, again, need) == 0);

  // KONTROL 1: DAG'siz blob kucuk olmali ve has_dag() false.
  uint8_t *nodag = static_cast<uint8_t *>(arena().alloc(plain, kSceneBlobAlign));
  CHECK(nodag && scene_blob_compile_ex(d, nullptr, nodag, plain) == plain);
  SceneBlobView v2;
  CHECK(scene_blob_open(nodag, plain, &v2, &err) && !v2.has_dag());
  CHECK(need > plain);

  // KONTROL 2: bozuk DAG bolumu reddedilmeli (dogrulama gercekten calisiyor).
  auto corrupt_and_open = [&](uint32_t node, bool bump_index) {
    std::memcpy(again, buf, need);
    auto *h = reinterpret_cast<SceneBlobHeader *>(again);
    auto *nd = reinterpret_cast<SceneBlobDagNode *>(again + h->dag_node_offset) + node;
    if (bump_index) nd->index_offset = h->dag_index_count + 4; // tablo disi
    else nd->parent_error = nd->error - 1.0f;                  // monoton degil
    const size_t from = offsetof(SceneBlobHeader, header_size);
    const uint64_t hv = scene_blob_fnv1a(again + from, need - from);
    h->hash_lo = (uint32_t)(hv & 0xFFFFFFFFu);
    h->hash_hi = (uint32_t)(hv >> 32);
    SceneBlobView bv;
    SceneError be{};
    const bool ok = scene_blob_open(again, need, &bv, &be);
    std::printf("    [bilgi] kontrol (%s): %s\n", bump_index ? "indeks tablo disi" : "hata monoton degil", ok ? "ACILDI (!)" : be.msg);
    return ok;
  };
  CHECK(!corrupt_and_open(0, true));
  CHECK(!corrupt_and_open(dag.node_count / 2, false));
  arena().reset_to(mark);
}

// Sahne derleyicisinin DAG dilimi ucdan uca: gercek glTF kaynaklarindan DAG
// kurulur, blob'a girer, acilir. CIHAZ SINIFI BASINA BAKE: dusuk sinif daha az
// kume / daha sig agac uretmeli (kontrol: sayilar farkli olmali, yoksa sinif
// secimi bir sey yapmiyordur).
ENGINE_TEST(scene_compile_bakes_cluster_dag_per_device_class) {
  char dir[1024], path[1024];
  asset_path(path, sizeof path, "lod_sphere.gltf");
  scene_dir_of(path, dir, sizeof dir);
  static SceneDesc d;
  d = SceneDesc{};
  d.add_asset("lod_sphere.gltf");
  SceneEntity e{};
  std::snprintf(e.name, sizeof e.name, "kure");
  e.scale = {1, 1, 1};
  e.components = kSceneModel;
  e.asset = 0;
  e.tint = {1, 1, 1};
  d.insert_entity(d.entity_count, e);

  uint32_t nodes[3] = {0, 0, 0}, levels[3] = {0, 0, 0};
  uint64_t hashes[3] = {0, 0, 0};
  for (uint32_t c = 0; c < 3; c++) {
    const size_t mark = arena().mark();
    SceneCompileOptions opt;
    opt.measure_resident = false;
    opt.bake_nav = false;
    opt.build_cluster_dag = true;
    opt.dag_class = c == 0 ? DeviceClass::Low : (c == 1 ? DeviceClass::Mid : DeviceClass::High);
    SceneBlobExtras x;
    SceneCompileReport rep;
    CHECK(scene_compile(arena(), d, dir, opt, &x, &rep));
    if (rep.dag_meshes == 0) { arena().reset_to(mark); skip("tests/assets/lod_sphere.gltf yok (DAG kapisi kosmadi)"); return; }
    const size_t need = scene_blob_compile_ex(d, &x, nullptr, 0);
    void *buf = arena().alloc(need, kSceneBlobAlign);
    CHECK(buf && scene_blob_compile_ex(d, &x, buf, need) == need);
    SceneBlobView v;
    SceneError err{};
    const bool opened = scene_blob_open(buf, need, &v, &err);
    if (!opened) std::printf("    [bilgi] acilis hatasi: %s\n", err.msg);
    CHECK(opened);
    CHECK(opened && v.has_dag() && v.h->dag_device_class == (uint32_t)opt.dag_class);
    std::printf("    [bilgi] sinif %s: %u mesh, %u kume, %u seviye, en ust %u ucgen, +%u B blob, ozet %016llx\n",
                device_class_name(opt.dag_class), rep.dag_meshes, rep.dag_nodes, rep.dag_levels, rep.dag_top_tris, rep.dag_bytes,
                (unsigned long long)rep.dag_hash);
    nodes[c] = rep.dag_nodes;
    levels[c] = rep.dag_levels;
    hashes[c] = rep.dag_hash;
    arena().reset_to(mark);
  }
  CHECK(nodes[2] > nodes[0]);                             // yuksek sinif: daha cok kume
  CHECK(levels[0] <= levels[1] && levels[1] <= levels[2]); // dusuk sinif: daha sig
  CHECK(hashes[0] != hashes[1] && hashes[1] != hashes[2]); // her sinif gercekten farkli bake
}

// ===========================================================================
// Faz 6 — GI SONDA BAKE'i (content/gi.hpp). Kapilarin hepsi KONTROLLU: olculen
// farkin gercekten gunesten/geometriden geldigini, esigin kendisinden degil,
// kolu kapatip AYNI olcumu tekrarlayarak gosteriyoruz.
// ===========================================================================
namespace {
// Sahneye SABIT kutu govde koyar (model yok: albedo = opt.surface_albedo).
void gi_add_box(SceneDesc &d, const char *name, Vec3 pos, Vec3 half) {
  SceneEntity e{};
  std::snprintf(e.name, sizeof e.name, "%s", name);
  e.pos = pos;
  e.components = kSceneBody;
  e.shape = SceneShape::Box;
  e.half = half;
  e.dynamic = false;
  d.insert_entity(d.entity_count, e);
}
GiOccluder g_gi_occ[kGiMaxOccluders];
GiLight g_gi_lights[kGiMaxLights];
// Model YOK: yalniz govde tikayicilari (kapilar analitik kalsin).
bool gi_bake_bodies(const SceneDesc &d, const SceneGiOptions &opt, SceneBlobExtras *x, SceneGiReport *rep) {
  GiScene g;
  scene_gi_setup(d, &g);
  g.occluders = g_gi_occ;
  g.occluder_count = scene_gi_occluders(d, opt, g_gi_occ, kGiMaxOccluders);
  g.lights = g_gi_lights;
  g.light_count = scene_gi_lights(d, g_gi_lights, kGiMaxLights);
  return scene_gi_bake(arena(), g, opt, x, rep);
}
float gi_luma(const float *rgb) { return 0.2126f * rgb[0] + 0.7152f * rgb[1] + 0.0722f * rgb[2]; }
// Blob'a derleyip acar; SceneGi'yi baglar. Donus: acildi mi.
bool gi_open(const SceneDesc &d, const SceneBlobExtras &x, SceneBlobView *v, SceneGi *gi) {
  const size_t need = scene_blob_compile_ex(d, &x, nullptr, 0);
  void *buf = arena().alloc(need, kSceneBlobAlign);
  if (!buf) return false;
  scene_blob_compile_ex(d, &x, buf, need);
  SceneError err{};
  if (!scene_blob_open(buf, need, v, &err)) {
    std::printf("    [bilgi] GI blob acilmadi: %s\n", err.msg);
    return false;
  }
  return gi->init(*v);
}
// Zemin + istege bagli cati/duvar; gunes ve ortam cagirandan.
void gi_ground_scene(SceneDesc &d, Vec3 sun_dir, float sun_diffuse) {
  d = SceneDesc{};
  d.sun_dir = sun_dir;
  d.sun_diffuse = sun_diffuse;
  d.ambient = {0.2f, 0.2f, 0.2f};
  gi_add_box(d, "zemin", {0, -0.5f, 0}, {10, 0.5f, 10});
}
SceneGiOptions gi_fast_opts() {
  SceneGiOptions o;
  o.rays = 128;
  o.spacing = 2.0f;
  o.margin = 1.0f;
  o.max_probes = 4096;
  return o;
}
} // namespace

// KAPI 1 (analitik dogruluk): acik gokyuzu altindaki sonda, cati altindaki
// sondadan BELIRGIN parlak olmali. KONTROL: gunes siddeti 0'a cekilince ayni
// oran cokmeli (kalan fark yalniz gokyuzu tikanmasi).
ENGINE_TEST(gi_open_sky_probe_is_brighter_than_covered_and_sun_is_the_cause) {
  const size_t mark = arena().mark();
  const Vec3 sun{0.3f, 1.0f, 0.0f};
  float ratio[2] = {0, 0};
  float open_up[2] = {0, 0}, cov_up[2] = {0, 0};
  float sun_vis[2] = {0, 0};
  for (uint32_t k = 0; k < 2; k++) { // k=0 gunes acik, k=1 KONTROL (gunes 0)
    static SceneDesc d;
    gi_ground_scene(d, sun, k == 0 ? 1.0f : 0.0f);
    gi_add_box(d, "cati", {6, 3.0f, 0}, {4, 0.25f, 4});
    SceneBlobExtras x;
    SceneGiReport rep;
    const bool ok = gi_bake_bodies(d, gi_fast_opts(), &x, &rep);
    CHECK(ok);
    if (!ok) { arena().reset_to(mark); return; }
    SceneBlobView v;
    SceneGi gi;
    CHECK(gi_open(d, x, &v, &gi));
    if (!gi.ok()) { arena().reset_to(mark); return; }
    const int32_t io = gi.nearest_index({-6.0f, 1.2f, 0.0f});
    const int32_t ic = gi.nearest_index({6.0f, 1.2f, 0.0f});
    CHECK(io >= 0 && ic >= 0 && io != ic);
    if (io < 0 || ic < 0) { arena().reset_to(mark); return; }
    const SceneBlobGiProbe &po = gi.probe(io), &pc = gi.probe(ic);
    CHECK((po.flags & 1u) == 0 && (pc.flags & 1u) == 0); // ikisi de kati disinda
    open_up[k] = gi_luma(po.face[2]);
    cov_up[k] = gi_luma(pc.face[2]);
    sun_vis[k] = po.sun_vis - pc.sun_vis;
    ratio[k] = cov_up[k] > 0 ? open_up[k] / cov_up[k] : 0;
    if (k == 0) {
      const Vec3 wo = gi.probe_pos(io), wc = gi.probe_pos(ic);
      std::printf("    [bilgi] sonda %d acik (%.3f %.3f %.3f), sonda %d cati alti (%.3f %.3f %.3f); %u sonda, %u isin, %.3f s\n", io,
                  wo.x, wo.y, wo.z, ic, wc.x, wc.y, wc.z, rep.probes, (uint32_t)rep.rays, rep.seconds);
    }
    arena().reset_to(mark);
  }
  std::printf("    [olcum] +Y isima acik/cati: gunes acik %.4f / %.4f = %.2fx | KONTROL (gunes 0) %.4f / %.4f = %.2fx\n", open_up[0],
              cov_up[0], ratio[0], open_up[1], cov_up[1], ratio[1]);
  CHECK(ratio[0] >= 4.0f);  // gunes acikken acik gokyuzu belirgin parlak
  CHECK(ratio[1] <= 2.0f);  // KONTROL: gunes kapaliyken fark cokuyor
  CHECK(ratio[0] >= 3.0f * ratio[1]);
  CHECK(sun_vis[0] > 0.9f); // acik sonda gunesi goruyor, cati altindaki gormuyor
  CHECK(sun_vis[1] > 0.9f); // gorunurluk siddetten BAGIMSIZ olculur
}

// KAPI 2 (yon dogrulugu): gunese donuk yuz, ters yuzden parlak olmali.
// KONTROL: sahne X'te SIMETRIK (zemin x=0'da merkezli) — gunes kapaninca
// +X ve -X yuzleri ayni sayiyi vermeli (oran ~1).
ENGINE_TEST(gi_sun_facing_face_is_brighter_and_symmetric_without_sun) {
  const size_t mark = arena().mark();
  const Vec3 sun{1.0f, 0.25f, 0.0f}; // agirlikli olarak +X
  float ratio[2] = {0, 0}, px[2] = {0, 0}, nx[2] = {0, 0};
  for (uint32_t k = 0; k < 2; k++) {
    static SceneDesc d;
    gi_ground_scene(d, sun, k == 0 ? 1.0f : 0.0f);
    SceneBlobExtras x;
    SceneGiReport rep;
    const bool ok = gi_bake_bodies(d, gi_fast_opts(), &x, &rep);
    CHECK(ok);
    if (!ok) { arena().reset_to(mark); return; }
    SceneBlobView v;
    SceneGi gi;
    CHECK(gi_open(d, x, &v, &gi));
    if (!gi.ok()) { arena().reset_to(mark); return; }
    const int32_t i = gi.nearest_index({0.0f, 1.2f, 0.0f});
    CHECK(i >= 0);
    if (i < 0) { arena().reset_to(mark); return; }
    const SceneBlobGiProbe &p = gi.probe(i);
    CHECK((p.flags & 1u) == 0);
    px[k] = gi_luma(p.face[0]);
    nx[k] = gi_luma(p.face[1]);
    ratio[k] = nx[k] > 0 ? px[k] / nx[k] : 0;
    arena().reset_to(mark);
  }
  std::printf("    [olcum] +X/-X isima: gunes acik %.4f / %.4f = %.2fx | KONTROL (gunes 0, X-simetrik sahne) %.4f / %.4f = %.3fx\n",
              px[0], nx[0], ratio[0], px[1], nx[1], ratio[1]);
  CHECK(ratio[0] >= 3.0f);                        // gunese donuk yuz belirgin parlak
  CHECK(ratio[1] >= 0.85f && ratio[1] <= 1.2f);   // KONTROL: gunessiz simetrik sahnede fark yok
}

// KAPI 3 (gorunurluk): duvarin arkasindaki sonda DOGRUDAN gunes almamali.
// KONTROL: duvar kaldirilinca ayni sonda gunesi gormeli. Ikinci kontrol:
// duvar varken bile gunes isini duvarin USTUNDEN gecen sonda isik alir.
ENGINE_TEST(gi_wall_blocks_direct_sun_and_removing_it_restores) {
  const size_t mark = arena().mark();
  const Vec3 sun{1.0f, 0.35f, 0.0f};
  float behind[2] = {-1, -1}, far_probe[2] = {-1, -1}, up_behind[2] = {0, 0};
  for (uint32_t k = 0; k < 2; k++) { // k=0 duvar var, k=1 KONTROL (duvar yok)
    static SceneDesc d;
    gi_ground_scene(d, sun, 1.0f);
    if (k == 0) gi_add_box(d, "duvar", {4.0f, 2.0f, 0}, {0.25f, 2.0f, 4.0f});
    SceneBlobExtras x;
    SceneGiReport rep;
    const bool ok = gi_bake_bodies(d, gi_fast_opts(), &x, &rep);
    CHECK(ok);
    if (!ok) { arena().reset_to(mark); return; }
    SceneBlobView v;
    SceneGi gi;
    CHECK(gi_open(d, x, &v, &gi));
    if (!gi.ok()) { arena().reset_to(mark); return; }
    const int32_t ib = gi.nearest_index({0.0f, 1.2f, 0.0f});   // duvarin ARKASI (gunes +X'te)
    const int32_t if_ = gi.nearest_index({-6.0f, 1.2f, 0.0f}); // uzak: isin duvarin ustunden gecer
    CHECK(ib >= 0 && if_ >= 0);
    if (ib < 0 || if_ < 0) { arena().reset_to(mark); return; }
    behind[k] = gi.probe(ib).sun_vis;
    far_probe[k] = gi.probe(if_).sun_vis;
    up_behind[k] = gi_luma(gi.probe(ib).face[0]); // +X yuzu (gunese donuk)
    arena().reset_to(mark);
  }
  std::printf("    [olcum] gunes gorunurlugu: duvar VAR arka=%.2f uzak=%.2f | KONTROL duvar YOK arka=%.2f uzak=%.2f; +X isima %.4f -> %.4f\n",
              behind[0], far_probe[0], behind[1], far_probe[1], up_behind[0], up_behind[1]);
  CHECK(behind[0] == 0.0f);   // duvar dogrudan gunesi kesiyor
  CHECK(behind[1] == 1.0f);   // KONTROL: duvar kalkinca ayni sonda gunesi goruyor
  CHECK(far_probe[0] == 1.0f); // isin duvarin ustunden geciyor: kor golge degil
  CHECK(up_behind[1] > up_behind[0] * 2.0f);
}

// KAPI 4 (belirlenimlilik): ayni sahne -> ayni sonda baytlari ve ayni blob.
// KONTROL 1: tek varlik 1 mm oynayinca BLOB ozeti degismeli. KONTROL 2: tikayici
// 1 m oynayinca SONDA alani da degismeli (bake gercekten geometriyi okuyor).
// Not: 1 mm'lik oynama sonda alanini degistirmek ZORUNDA degil — bu bir hata
// degil, kararlilik: hicbir isinin gorunurluk siniflandirmasi degismezse bake
// ayni sayilari verir. Olculen deger asagida [bilgi] olarak basiliyor.
ENGINE_TEST(gi_bake_is_deterministic_and_geometry_changes_it) {
  const size_t mark = arena().mark();
  SceneGiOptions opt = gi_fast_opts();
  opt.rays = 64;
  opt.spacing = 2.5f;
  uint64_t hash[4] = {0, 0, 0, 0}, blob_hash[4] = {0, 0, 0, 0};
  for (uint32_t k = 0; k < 4; k++) {
    static SceneDesc d;
    gi_ground_scene(d, {0.3f, 1.0f, 0.2f}, 1.0f);
    gi_add_box(d, "kule", {2.0f, 2.0f, 0}, {2.0f, 2.0f, 2.0f});
    if (k == 2) d.entities[1].pos.x += 0.001f; // 1 mm
    if (k == 3) d.entities[1].pos.x += 1.0f;   // 1 m
    SceneBlobExtras x;
    SceneGiReport rep;
    const bool ok = gi_bake_bodies(d, opt, &x, &rep);
    CHECK(ok);
    if (!ok) { arena().reset_to(mark); return; }
    hash[k] = rep.hash;
    SceneBlobView v;
    SceneGi gi;
    CHECK(gi_open(d, x, &v, &gi));
    if (v.h) blob_hash[k] = v.hash();
    arena().reset_to(mark);
  }
  std::printf("    [olcum] sonda ozeti: %016llx == %016llx (ayni sahne); 1 m sonrasi %016llx (DEGISTI mi: %s)\n",
              (unsigned long long)hash[0], (unsigned long long)hash[1], (unsigned long long)hash[3],
              hash[3] != hash[0] ? "evet" : "HAYIR");
  std::printf("    [olcum] blob ozeti: %016llx == %016llx; 1 mm sonrasi %016llx (DEGISTI mi: %s)\n", (unsigned long long)blob_hash[0],
              (unsigned long long)blob_hash[1], (unsigned long long)blob_hash[2], blob_hash[2] != blob_hash[0] ? "evet" : "HAYIR");
  std::printf("    [bilgi] 1 mm oynama sonda alanini degistirdi mi: %s (kararlilik olcumu, kapi degil)\n",
              hash[2] != hash[0] ? "evet" : "hayir");
  CHECK(hash[0] == hash[1] && hash[0] != 0);            // ayni sahne -> ayni sonda baytlari
  CHECK(blob_hash[0] == blob_hash[1] && blob_hash[0] != 0);
  CHECK(blob_hash[2] != blob_hash[0]);                  // KONTROL 1: 1 mm blob'u degistirir
  CHECK(hash[3] != hash[0]);                            // KONTROL 2: 1 m sonda alanini degistirir
}

// KAPI 5 (blob): GI'li blob gidis-donus BIT-TAM, bozuk sonda tablosu ve
// tutarsiz izgara REDDEDILIR; bozulmamis blob acilir (pozitif kontrol).
ENGINE_TEST(gi_blob_roundtrip_is_bit_exact_and_rejects_corruption) {
  const size_t mark = arena().mark();
  SceneGiOptions opt = gi_fast_opts();
  opt.rays = 32;
  opt.spacing = 5.0f;
  static SceneDesc d;
  gi_ground_scene(d, {0.3f, 1.0f, 0.2f}, 1.0f);
  gi_add_box(d, "kule", {2.0f, 1.5f, 0}, {0.5f, 1.5f, 0.5f});
  SceneBlobExtras x;
  SceneGiReport rep;
  const bool ok = gi_bake_bodies(d, opt, &x, &rep);
  CHECK(ok);
  if (!ok) { arena().reset_to(mark); return; }
  const size_t need = scene_blob_compile_ex(d, &x, nullptr, 0);
  uint8_t *buf = static_cast<uint8_t *>(arena().alloc(need, kSceneBlobAlign));
  CHECK(buf);
  if (!buf) { arena().reset_to(mark); return; }
  scene_blob_compile_ex(d, &x, buf, need);
  SceneBlobView v;
  SceneError err{};
  CHECK(scene_blob_open(buf, need, &v, &err));
  if (!v.h) { arena().reset_to(mark); return; }
  CHECK(v.has_gi() && v.h->gi_probe_count == x.gi_probe_count && v.h->gi_valid == x.gi_valid);
  CHECK(v.h->gi_spacing == x.gi_spacing && v.h->gi_rays == opt.rays && v.h->gi_bounces == opt.bounces);
  CHECK((v.h->gi_flags & kGiDirectSun) && (v.h->gi_flags & kGiBounce) && !(v.h->gi_flags & kGiModelTris));
  CHECK(std::memcmp(v.gi_probes, x.gi_probes, sizeof(SceneBlobGiProbe) * x.gi_probe_count) == 0); // bit-tam
  std::printf("    [bilgi] GI'li blob: %zu bayt (%u sonda, %u B sonda tablosu), izgara %ux%ux%u adim %g\n", need, v.h->gi_probe_count,
              (uint32_t)(v.h->gi_probe_count * sizeof(SceneBlobGiProbe)), v.h->gi_dim[0], v.h->gi_dim[1], v.h->gi_dim[2], v.h->gi_spacing);

  uint8_t *bad = static_cast<uint8_t *>(arena().alloc(need, kSceneBlobAlign));
  CHECK(bad);
  if (!bad) { arena().reset_to(mark); return; }
  auto rehash = [&]() {
    auto *h = reinterpret_cast<SceneBlobHeader *>(bad);
    const size_t from = offsetof(SceneBlobHeader, header_size);
    const uint64_t hv = scene_blob_fnv1a(bad + from, need - from);
    h->hash_lo = (uint32_t)(hv & 0xFFFFFFFFu);
    h->hash_hi = (uint32_t)(hv >> 32);
  };
  SceneBlobView bv;
  // 1) sonda isimasi negatif
  std::memcpy(bad, buf, need);
  auto *bh = reinterpret_cast<SceneBlobHeader *>(bad);
  reinterpret_cast<SceneBlobGiProbe *>(bad + bh->gi_probe_offset)[0].face[0][0] = -1.0f;
  rehash();
  CHECK(!scene_blob_open(bad, need, &bv, &err) && std::strstr(err.msg, "isima"));
  std::printf("    [bilgi] bozuk sonda reddi: %s\n", err.msg);
  // 2) gunes gorunurlugu araligin disinda
  std::memcpy(bad, buf, need);
  reinterpret_cast<SceneBlobGiProbe *>(bad + bh->gi_probe_offset)[0].sun_vis = 2.0f;
  rehash();
  CHECK(!scene_blob_open(bad, need, &bv, &err) && std::strstr(err.msg, "gorunurlugu"));
  // 3) izgara boyutu sonda sayisiyla tutarsiz
  std::memcpy(bad, buf, need);
  bh->gi_dim[0] += 1;
  rehash();
  CHECK(!scene_blob_open(bad, need, &bv, &err) && std::strstr(err.msg, "izgara"));
  // 4) gecerli sonda sayaci tabloyla tutarsiz
  std::memcpy(bad, buf, need);
  bh->gi_valid = bh->gi_probe_count > 0 ? bh->gi_probe_count - 1 : 0;
  rehash();
  CHECK(!scene_blob_open(bad, need, &bv, &err) && std::strstr(err.msg, "gecerli sonda"));
  // POZITIF KONTROL: bozulmamis kopya hala aciliyor
  std::memcpy(bad, buf, need);
  CHECK(scene_blob_open(bad, need, &bv, &err));
  arena().reset_to(mark);
}

// KAPI 6 (runtime sorgusu): bilinen konumda okunan deger bake edilenle ayni.
// sample_nearest sonda merkezinde BIT-TAM; trilineer okuma ayni noktada
// 1e-5 bagil tolerans icinde, iki sonda ortasinda ikisinin ortalamasi.
ENGINE_TEST(gi_runtime_query_matches_baked_values) {
  const size_t mark = arena().mark();
  SceneGiOptions opt = gi_fast_opts();
  opt.rays = 64;
  opt.spacing = 3.0f;
  static SceneDesc d;
  gi_ground_scene(d, {0.4f, 1.0f, 0.1f}, 1.0f);
  gi_add_box(d, "kule", {2.0f, 1.5f, 0}, {0.5f, 1.5f, 0.5f});
  SceneBlobExtras x;
  SceneGiReport rep;
  const bool ok = gi_bake_bodies(d, opt, &x, &rep);
  CHECK(ok);
  if (!ok) { arena().reset_to(mark); return; }
  SceneBlobView v;
  SceneGi gi;
  CHECK(gi_open(d, x, &v, &gi));
  if (!gi.ok()) { arena().reset_to(mark); return; }
  // Gecerli iki komsu sonda bul (x yonunde ardisik).
  uint32_t a = 0, b = 0;
  bool found = false;
  for (uint32_t z = 0; z < gi.dim()[2] && !found; z++)
    for (uint32_t y = 0; y < gi.dim()[1] && !found; y++)
      for (uint32_t xx = 0; xx + 1 < gi.dim()[0] && !found; xx++) {
        const uint32_t i0 = gi.index(xx, y, z), i1 = gi.index(xx + 1, y, z);
        if ((gi.probe(i0).flags & 1u) || (gi.probe(i1).flags & 1u)) continue;
        if (gi_luma(gi.probe(i0).face[2]) <= 0) continue;
        a = i0; b = i1; found = true;
      }
  CHECK(found);
  if (!found) { arena().reset_to(mark); return; }
  const Vec3 up{0, 1, 0};
  const Vec3 pa = gi.probe_pos(a);
  const Vec3 baked{gi.probe(a).face[2][0], gi.probe(a).face[2][1], gi.probe(a).face[2][2]};
  const Vec3 nearest = gi.sample_nearest(pa, up);
  CHECK(nearest.x == baked.x && nearest.y == baked.y && nearest.z == baked.z); // BIT-TAM
  const Vec3 tri = gi.sample(pa, up);
  const float rel = baked.y > 0 ? std::fabs(tri.y - baked.y) / baked.y : std::fabs(tri.y - baked.y);
  // Iki sondanin tam ortasinda: agirliklar 0.5/0.5 -> ortalama.
  const Vec3 pb = gi.probe_pos(b);
  const Vec3 mid = gi.sample((pa + pb) * 0.5f, up);
  const float avg = (gi.probe(a).face[2][1] + gi.probe(b).face[2][1]) * 0.5f;
  const float rel_mid = avg > 0 ? std::fabs(mid.y - avg) / avg : 0;
  std::printf("    [olcum] sonda %u (%.2f %.2f %.2f) bake %.5f | en-yakin %.5f (bit-tam) | trilineer %.5f (bagil %.2e) | orta %.5f vs "
              "ortalama %.5f (bagil %.2e)\n",
              a, pa.x, pa.y, pa.z, baked.y, nearest.y, tri.y, rel, mid.y, avg, rel_mid);
  CHECK(rel < 1e-5f);
  CHECK(rel_mid < 1e-5f);
  // Gunes gorunurlugu de ayni noktada bake edilenle esit.
  CHECK(std::fabs(gi.sun_visibility(pa) - gi.probe(a).sun_vis) < 1e-5f);
  // KONTROL: kati icindeki sondalar interpolasyona girmez — zeminin ICINDE
  // bir nokta sorulunca gecerli komsu kalmazsa sahne ortami doner.
  const Vec3 deep = gi.sample({0, -0.5f, 0}, up);
  std::printf("    [bilgi] zemin ICI okuma: (%.4f %.4f %.4f), sahne ortami (%.4f %.4f %.4f), %u/%u sonda kati icinde\n", deep.x, deep.y,
              deep.z, gi.ambient().x, gi.ambient().y, gi.ambient().z, rep.probes_inside, rep.probes);
  CHECK(rep.probes_inside > 0);
  arena().reset_to(mark);
}

// Bake edilmis GI probe'lari SceneRuntime::apply_world'e GERCEKTEN ulasiyor mu
// (content/gi.hpp'nin bake+sorgu motoru kendi icinde dogruydu ama hicbir
// caginin cagirmiyordu -- bu kapi tam da o bagi olcer). KONTROL: AYNI sahne
// GI'siz derlenince ambient eski duz sabitte BIT-TAM kalir (Vulkan gerekmez:
// set_light/set_shadow_volume salt alan atamasi, Renderer init'siz kullanilir).
ENGINE_TEST(scene_runtime_applies_baked_gi_ambient) {
  const size_t mark = arena().mark();
  static SceneDesc d;
  gi_ground_scene(d, Vec3{0.3f, 1.0f, 0.0f}, 1.0f);
  gi_add_box(d, "cati", {6, 3.0f, 0}, {4, 0.25f, 4});
  renderer::Renderer ren;

  // 1) GI'SIZ derleme (POZITIF KONTROL): apply_world eski davranista kalmali.
  {
    SceneBlobExtras x{}; // gi_probe_count 0
    const size_t need = scene_blob_compile_ex(d, &x, nullptr, 0);
    void *buf = arena().alloc(need, kSceneBlobAlign);
    CHECK(buf != nullptr);
    scene_blob_compile_ex(d, &x, buf, need);
    SceneBlobView v;
    SceneError err{};
    CHECK(scene_blob_open(buf, need, &v, &err));
    CHECK(v.h->gi_probe_count == 0);
    static SceneRuntime rt;
    CHECK(rt.init(arena(), ren, v, "."));
    rt.apply_world(ren);
    const Vec3 a = ren.ambient();
    CHECK(feq(a.x, d.ambient.x) && feq(a.y, d.ambient.y) && feq(a.z, d.ambient.z)); // BIT-TAM
  }
  arena().reset_to(mark);

  // 2) GI'LI derleme: ambient artik duz sabit DEGIL, probe orneginden gelir
  // (bkz. scene_runtime.cpp apply_world: sahne AABB ortasi, yukari normal).
  {
    SceneBlobExtras x;
    SceneGiReport rep;
    CHECK(gi_bake_bodies(d, gi_fast_opts(), &x, &rep));
    const size_t need = scene_blob_compile_ex(d, &x, nullptr, 0);
    void *buf = arena().alloc(need, kSceneBlobAlign);
    CHECK(buf != nullptr);
    scene_blob_compile_ex(d, &x, buf, need);
    SceneBlobView v;
    SceneError err{};
    CHECK(scene_blob_open(buf, need, &v, &err));
    CHECK(v.h->gi_probe_count > 0);
    static SceneRuntime rt2;
    CHECK(rt2.init(arena(), ren, v, "."));
    rt2.apply_world(ren);
    const Vec3 gi_amb = ren.ambient();
    SceneGi gi;
    CHECK(gi.init(v));
    const Vec3 lo{v.h->bounds_lo[0], v.h->bounds_lo[1], v.h->bounds_lo[2]};
    const Vec3 hi{v.h->bounds_hi[0], v.h->bounds_hi[1], v.h->bounds_hi[2]};
    const Vec3 expect = gi.sample((lo + hi) * 0.5f, {0, 1, 0});
    std::printf("    [bilgi] GI ambient (%.4f %.4f %.4f) vs duz sabit (%.4f %.4f %.4f)\n", gi_amb.x, gi_amb.y, gi_amb.z, d.ambient.x,
                d.ambient.y, d.ambient.z);
    CHECK(feq(gi_amb.x, expect.x) && feq(gi_amb.y, expect.y) && feq(gi_amb.z, expect.z));
    CHECK(!(feq(gi_amb.x, d.ambient.x) && feq(gi_amb.y, d.ambient.y) && feq(gi_amb.z, d.ambient.z))); // gercekten degisti
  }
  arena().reset_to(mark);
}

// Derleyici yolu: scene_compile GI'yi bake ediyor mu (model ucgenleri dahil).
// Bu, BVH'li yolun tek kapisi — editor.sahne'nin modelleri binlerce ucgen.
ENGINE_TEST(scene_compile_bakes_gi_probes_with_model_triangles) {
  char path[1024];
  asset_path(path, sizeof path, "editor.sahne");
  static SceneDesc d;
  SceneError err{};
  const size_t mark = arena().mark();
  if (!scene_load(arena(), path, &d, &err)) {
    skip("editor.sahne okunamadi (varlik yok)");
    arena().reset_to(mark);
    return;
  }
  char dir[1024];
  scene_dir_of(path, dir, sizeof dir);
  SceneCompileOptions opt;
  opt.measure_resident = false;
  opt.bake_nav = false;
  opt.bake_gi = true;
  opt.gi_include_models = true;
  opt.gi.rays = 24;
  opt.gi.spacing = 6.0f;
  opt.gi.max_probes = 512;
  SceneBlobExtras x;
  SceneCompileReport rep;
  const bool ok = scene_compile(arena(), d, dir, opt, &x, &rep);
  CHECK(ok && rep.gi_ok);
  if (!ok || !rep.gi_ok) { arena().reset_to(mark); return; }
  std::printf("    [olcum] editor.sahne GI: %u sonda (%ux%ux%u adim %g), %u gecerli, %u tikayici + %u ucgen (%u BVH dugumu), %llu isin, "
              "%.3f s, %u B, isima %.4f..%.4f\n",
              rep.gi.probes, rep.gi.dim[0], rep.gi.dim[1], rep.gi.dim[2], rep.gi.spacing, rep.gi.probes - rep.gi.probes_inside,
              rep.gi.occluders, rep.gi.triangles, rep.gi.bvh_nodes, (unsigned long long)rep.gi.rays, rep.gi.seconds, rep.gi_bytes,
              rep.gi.min_luma, rep.gi.max_luma);
  CHECK(rep.gi.probes > 0 && rep.gi.probes <= opt.gi.max_probes);
  CHECK(rep.gi.triangles > 100); // model ucgenleri gercekten tikayici oldu
  CHECK(rep.gi.bvh_nodes > 1);
  CHECK(rep.gi.max_luma > rep.gi.min_luma); // sahne her yerde ayni degil
  SceneBlobView v;
  SceneGi gi;
  CHECK(gi_open(d, x, &v, &gi));
  CHECK(gi.ok() && (gi.flags() & kGiModelTris));
  arena().reset_to(mark);
}

// =============================================================================
// Faz E2 KAPISI — sahne agaci blob'da duzlesir
// =============================================================================
// Hiyerarsik sahne ile ELLE duzlestirilmis ikizi AYNI dunya donusumlerini
// tasir; blob'da ebeveyn alani olmadigi icin ikisinin BAYTLARI da ozeti de ayni
// olmali. Ebeveynler bilerek yalniz OTELEME tasiyor: duzlestirme o zaman
// toplamdir ve karsilastirma TOLERANSSIZ yapilabilir (donuslu ebeveynde elle
// duzlestirme ayristirma sapmasi getirir, kapi da tolerans olcerdi = zayif).
// Kontrol: ebeveyn oynatilinca hiyerarsik blob DEGISIR, duz ikiz AYNI kalir.
namespace {
void fill_hier(SceneDesc &d) {
  d = SceneDesc{};
  d.add_asset("lod_sphere.gltf");
  SceneEntity e{};
  std::snprintf(e.name, sizeof e.name, "kok"); // yalniz oteleme
  e.pos = {3, 1, -2};
  d.insert_entity(d.entity_count, e);
  e = SceneEntity{};
  std::snprintf(e.name, sizeof e.name, "orta"); // yalniz oteleme
  e.pos = {0, 2, 0}; e.parent = 0;
  d.insert_entity(d.entity_count, e);
  e = SceneEntity{};
  std::snprintf(e.name, sizeof e.name, "govde");
  e.pos = {1, 0, 0.5f}; e.rot_deg = {0, 30, 0}; e.scale = {2, 1, 1}; e.parent = 1;
  e.components = kSceneModel | kSceneBody; e.asset = 0; e.tint = {0.85f, 0.9f, 1.0f};
  e.shape = SceneShape::Box; e.half = {0.5f, 0.25f, 0.5f}; e.dynamic = true;
  d.insert_entity(d.entity_count, e);
  e = SceneEntity{};
  std::snprintf(e.name, sizeof e.name, "lamba");
  e.pos = {-1, 0, 0}; e.parent = 1;
  e.components = kSceneLight; e.light_color = {1, 0.2f, 0.1f}; e.light_intensity = 3; e.light_radius = 8;
  d.insert_entity(d.entity_count, e);
  e = SceneEntity{};
  std::snprintf(e.name, sizeof e.name, "bagimsiz");
  e.pos = {-6, 0, 4};
  e.components = kSceneModel; e.asset = 0; e.tint = {1, 1, 1};
  d.insert_entity(d.entity_count, e);
}
// Ayni sahnenin duz ikizi: her varlik kok, konumlar zincirin CARPMA sirasiyla
// toplanmis (kok*orta)*cocuk -> cocuk + (orta + kok).
void flatten(const SceneDesc &h, SceneDesc &f) {
  f = h;
  for (uint32_t i = 0; i < f.entity_count; i++) f.entities[i].parent = -1;
  const Vec3 mid = h.entities[1].pos + h.entities[0].pos;
  f.entities[1].pos = mid;
  f.entities[2].pos = h.entities[2].pos + mid;
  f.entities[3].pos = h.entities[3].pos + mid;
}
} // namespace

ENGINE_TEST(scene_blob_hierarchy_matches_flattened_scene) {
  static SceneDesc h, f;
  fill_hier(h);
  flatten(h, f);
  uint32_t bad = 0;
  CHECK(scene_tree_validate(h, &bad));
  // 1. Veri modeli: dunya matrisleri bit-tam ayni.
  bool mats = true;
  for (uint32_t i = 0; i < h.entity_count; i++) {
    const Mat4 a = scene_entity_world_matrix(h, i), b = scene_entity_world_matrix(f, i);
    if (std::memcmp(&a.m[0][0], &b.m[0][0], sizeof a.m) != 0) mats = false;
  }
  CHECK(mats);
  // 2. Blob: ebeveyn alani YOK, turetilmis her sey dunya uzayinda -> AYNI BAYT.
  size_t nh = 0, nf = 0;
  void *bh = compile_to(h, &nh);
  void *bf = compile_to(f, &nf);
  CHECK(bh && bf && nh == nf);
  CHECK(nh == nf && std::memcmp(bh, bf, nh) == 0);
  SceneBlobView vh, vf;
  SceneError err{};
  CHECK(scene_blob_open(bh, nh, &vh, &err) && scene_blob_open(bf, nf, &vf, &err));
  CHECK(vh.hash() == vf.hash());
  // 3. Blob matrisi = scene_entity_world_matrix (yerel DEGIL).
  bool blob_world = true, blob_local_differs = false;
  for (uint32_t i = 0; i < h.entity_count; i++) {
    const Mat4 w = scene_entity_world_matrix(h, i), bm = vh.entity_matrix(i);
    if (std::memcmp(&w.m[0][0], &bm.m[0][0], sizeof w.m) != 0) blob_world = false;
    const Mat4 l = scene_entity_matrix(h.entities[i]);
    if (h.entities[i].parent >= 0 && std::memcmp(&l.m[0][0], &bm.m[0][0], sizeof l.m) != 0) blob_local_differs = true;
  }
  CHECK(blob_world);
  CHECK(blob_local_differs); // kontrol: kapi gercekten YEREL olmayani olcuyor
  // Govde ve isik de dunyada: govde (1,0,0.5) + (0,2,0) + (3,1,-2) = (4,3,-1.5)
  CHECK(vh.h->body_count == 1 && vh.h->light_count == 1);
  CHECK(v3eq(vh.bodies[0].pos, Vec3{4.0f, 3.0f, -1.5f}));
  CHECK(v3eq(vh.lights[0].pos, Vec3{2.0f, 3.0f, -2.0f})); // (-1,0,0) + (0,2,0) + (3,1,-2)
  CHECK(v3eq(vh.bodies[0].half, h.entities[2].half * h.entities[2].scale));
  // 4. KONTROL: ebeveyni oynat -> hiyerarsik blob DEGISIR, duz ikiz AYNI kalir.
  static SceneDesc h2, f2;
  h2 = h;
  h2.entities[0].pos = h2.entities[0].pos + Vec3{0, 5, 0};
  f2 = f; // duz ikize dokunulmadi
  size_t n2 = 0, n3 = 0;
  void *b2 = compile_to(h2, &n2);
  void *b3 = compile_to(f2, &n3);
  SceneBlobView v2, v3;
  CHECK(b2 && b3 && scene_blob_open(b2, n2, &v2, &err) && scene_blob_open(b3, n3, &v3, &err));
  CHECK(v2.hash() != vh.hash()); // ebeveyn oynadi: cocuklar da oynadi
  CHECK(v3.hash() == vf.hash()); // duz ikiz degismedi
  CHECK(v3eq(v2.bodies[0].pos, Vec3{4.0f, 8.0f, -1.5f}));
  // 5. KONTROL: duzlestirme YANLIS yapilirsa (cocuk konumu guncellenmezse)
  //    baytlar ayrilir — yani 2. adimdaki esitlik bos bir dogru degil.
  static SceneDesc naive;
  naive = h;
  for (uint32_t i = 0; i < naive.entity_count; i++) naive.entities[i].parent = -1;
  size_t nn = 0;
  void *bn = compile_to(naive, &nn);
  SceneBlobView vn;
  CHECK(bn && scene_blob_open(bn, nn, &vn, &err) && vn.hash() != vh.hash());
  std::printf("    [bilgi] hiyerarsik blob %zu bayt ozet %016llx == duz ikiz; ebeveyn oynayinca %016llx, naif duzlestirme %016llx\n", nh,
              (unsigned long long)vh.hash(), (unsigned long long)v2.hash(), (unsigned long long)vn.hash());
}
