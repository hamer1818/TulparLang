// Sahne veri modeli (content/scene): deterministik metin gidis-donus (ayni
// bayt, bit-tam sayilar; tek bit degisince metin degisir — kontrol), hatali
// satirin numarasiyla reddi, kapasite tasmasi, islem gunlugu (geri al/yinele
// baytlari geri getirir; bos gunlukte false — kontrol), fizik govde kurulumu
// (zemin varken oturur, zemin yokken duser — kontrol), donus kuaterniyonu ile
// matrisin uyusmasi.
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <unistd.h>

#include "content/scene.hpp"
#include "core/memory/arena.hpp"
#include "sim/physics.hpp"
#include "tests/test.hpp"

using namespace tulpar::engine;
using namespace tulpar::engine::content;
using namespace tulpar::engine::test;

namespace {
SystemArena &arena() {
  static SystemArena sys;
  if (sys.capacity() == 0) sys.reserve(32u << 20, "scene_test");
  return sys;
}
bool feq(float a, float b) { uint32_t x, y; std::memcpy(&x, &a, 4); std::memcpy(&y, &b, 4); return x == y; }

// Her bileseni ve "zor" sayilari (1e-5, -0, 1/3, buyuk) iceren sahne. `e`
// bilerek sifirlanmaz: olmayan bilesenin alanlari (ornegin isik varliginda
// kalan `phase`) veri degildir, gidis-donus esitligi bunlari saymamali.
void fill(SceneDesc &d) {
  d = SceneDesc{};
  d.sun_dir = {0.5f, 1.0f, 0.35f};
  d.ambient = {1.0f / 3.0f, 0.17f, 1e-5f};
  d.cam_yaw = -0.0f;
  d.shadow_depth = 123456.789f;
  d.add_asset("lod_sphere.gltf");
  d.add_asset("skin_tube.gltf");
  SceneEntity e{};
  std::snprintf(e.name, sizeof e.name, "ad bosluklu");
  e.pos = {-8.0f, 1.2f, -8.5f}; e.rot_deg = {30, 45, 60}; e.scale = {1, 2, 0.5f};
  e.components = kSceneModel; e.asset = 0; e.tint = {0.85f, 0.9f, 1.0f};
  d.insert_entity(d.entity_count, e);
  std::snprintf(e.name, sizeof e.name, "boru");
  e.components = kSceneModel | kSceneAnim; e.asset = 1; e.clip = 0; e.phase = 0.35f; e.speed = 1.0f / 7.0f;
  d.insert_entity(d.entity_count, e);
  std::snprintf(e.name, sizeof e.name, "lamba");
  e.components = kSceneLight; e.light_color = {1, 0.2f, 0.1f}; e.light_intensity = 3; e.light_radius = 8;
  d.insert_entity(d.entity_count, e);
  std::snprintf(e.name, sizeof e.name, "kutu");
  e.components = kSceneModel | kSceneBody; e.shape = SceneShape::Box; e.half = {0.5f, 0.25f, 1e-3f}; e.dynamic = true;
  d.insert_entity(d.entity_count, e);
  std::snprintf(e.name, sizeof e.name, "kure_govde");
  e.components = kSceneBody; e.shape = SceneShape::Sphere; e.radius = 0.75f; e.dynamic = false;
  d.insert_entity(d.entity_count, e);
}
const char *asset_path(char *buf, size_t n, const char *name) {
  const char *adir = std::getenv("TULPAR_ENGINE_ASSETS");
  if (adir && *adir) std::snprintf(buf, n, "%s/%s", adir, name);
  else std::snprintf(buf, n, "%s/tests/assets/%s", ENGINE_SOURCE_DIR, name);
  return buf;
}
} // namespace

ENGINE_TEST(scene_text_roundtrip_is_deterministic) {
  static SceneDesc a, b, c;
  static char t1[64 << 10], t2[64 << 10], t3[64 << 10];
  fill(a);
  const size_t n1 = scene_write(a, t1, sizeof t1);
  CHECK(n1 > 0 && n1 < sizeof t1);
  SceneError err{};
  const bool ok = scene_parse(t1, n1, &b, &err);
  if (!ok) std::printf("    [bilgi] ayristirma: %s\n", err.msg);
  CHECK(ok);
  const size_t n2 = scene_write(b, t2, sizeof t2);
  CHECK(n1 == n2 && std::memcmp(t1, t2, n1) == 0); // ayni baytlar
  CHECK(b.entity_count == a.entity_count && b.asset_count == a.asset_count);
  for (uint32_t i = 0; i < a.entity_count && i < b.entity_count; i++) CHECK(scene_entity_equal(a.entities[i], b.entities[i]));
  CHECK(feq(b.ambient.x, 1.0f / 3.0f) && feq(b.ambient.z, 1e-5f) && feq(b.shadow_depth, 123456.789f) && feq(b.cam_yaw, -0.0f));
  CHECK(std::strcmp(b.entities[0].name, "ad bosluklu") == 0);
  // Kontrol: tek bir ulp degisince metin degismeli (yazici sabit cikti vermiyor).
  c = b;
  c.entities[0].pos.x = std::nextafterf(c.entities[0].pos.x, 1000.0f);
  const size_t n3 = scene_write(c, t3, sizeof t3);
  CHECK(!(n3 == n1 && std::memcmp(t1, t3, n1) == 0));
  // Uzunluk sayimi (cap 0) ve kisa tampon NUL sonu.
  CHECK(scene_write(a, nullptr, 0) == n1);
  char small[16];
  scene_write(a, small, sizeof small);
  CHECK(small[15] == 0);
  std::printf("    [bilgi] sahne metni %zu bayt, %u varlik, %u kaynak; gidis-donus ayni\n", n1, a.entity_count, a.asset_count);
}

ENGINE_TEST(scene_parse_reports_bad_line_and_rejects_overflow) {
  static SceneDesc d;
  SceneError err{};
  const char *bad_key = "tulpar-sahne 1\nisik-gunes 0.5\nnesne \"a\"\n  konum 1 2 3\n  olcekk 1 1 1\nson\n";
  CHECK(!scene_parse(bad_key, std::strlen(bad_key), &d, &err));
  CHECK(err.line == 5);
  CHECK(std::strstr(err.msg, "satir 5") != nullptr);
  const char *no_son = "tulpar-sahne 1\nnesne \"a\"\n  konum 0 0 0\n";
  CHECK(!scene_parse(no_son, std::strlen(no_son), &d, &err));
  const char *bad_asset = "tulpar-sahne 1\nnesne \"a\"\n  model 0 1 1 1\nson\n";
  CHECK(!scene_parse(bad_asset, std::strlen(bad_asset), &d, &err) && err.line == 3);
  const char *bad_ver = "tulpar-sahne 2\n";
  CHECK(!scene_parse(bad_ver, std::strlen(bad_ver), &d, &err) && err.line == 1);
  const char *bad_num = "tulpar-sahne 1\nnesne \"a\"\n  konum x 0 0\nson\n";
  CHECK(!scene_parse(bad_num, std::strlen(bad_num), &d, &err) && err.line == 3);
  const char *nan_num = "tulpar-sahne 1\nnesne \"a\"\n  konum nan 0 0\nson\n";
  CHECK(!scene_parse(nan_num, std::strlen(nan_num), &d, &err) && err.line == 3);
  const char *bad_quote = "tulpar-sahne 1\nnesne \"a\n";
  CHECK(!scene_parse(bad_quote, std::strlen(bad_quote), &d, &err) && err.line == 2);
  const char *dup = "tulpar-sahne 1\nnesne \"a\"\n  konum 0 0 0\n  konum 1 1 1\nson\n";
  CHECK(!scene_parse(dup, std::strlen(dup), &d, &err) && err.line == 4);
  const char *empty = "";
  CHECK(!scene_parse(empty, 0, &d, &err));
  // Yorum + bos satir + CRLF kabul.
  const char *ok_text = "tulpar-sahne 1\r\n# yorum\r\n\r\nnesne \"a\" # ad\r\n  konum 1 2 3\r\nson\r\n";
  CHECK(scene_parse(ok_text, std::strlen(ok_text), &d, &err));
  CHECK(d.entity_count == 1 && feq(d.entities[0].pos.z, 3.0f));
  // Kapasite: kSceneMaxEntities kabul (pozitif kontrol), +1 red.
  static char big[1 << 20];
  for (int extra = 0; extra < 2; extra++) {
    size_t n = (size_t)std::snprintf(big, sizeof big, "tulpar-sahne 1\n");
    for (uint32_t i = 0; i < kSceneMaxEntities + (uint32_t)extra; i++)
      n += (size_t)std::snprintf(big + n, sizeof big - n, "nesne \"v%u\"\nson\n", i);
    const bool ok = scene_parse(big, n, &d, &err);
    CHECK(ok == (extra == 0));
    if (extra == 0) CHECK(d.entity_count == kSceneMaxEntities);
  }
  std::printf("    [bilgi] son hata: %s\n", err.msg);
}

ENGINE_TEST(scene_history_undo_redo_restores_bytes) {
  static SceneDesc d;
  static char t_orig[64 << 10], t_edit[64 << 10], t_now[64 << 10];
  fill(d);
  const size_t n_orig = scene_write(d, t_orig, sizeof t_orig);
  SceneHistory h;
  CHECK(h.init(arena(), 64));
  SceneEntity e = d.entities[0];
  e.pos.x += 5;
  CHECK(h.set_entity(d, 0, e));
  CHECK(!h.set_entity(d, 0, e)); // aynisi: islem yok
  CHECK(h.undo_count() == 1);
  SceneEntity n = d.entities[1];
  std::snprintf(n.name, sizeof n.name, "yeni");
  CHECK(h.add_entity(d, n));
  CHECK(h.remove_entity(d, 2));
  e = d.entities[1];
  std::snprintf(e.name, sizeof e.name, "ad2");
  CHECK(h.set_entity(d, 1, e));
  CHECK(h.undo_count() == 4 && h.redo_count() == 0);
  const size_t n_edit = scene_write(d, t_edit, sizeof t_edit);
  CHECK(!(n_edit == n_orig && std::memcmp(t_orig, t_edit, n_orig) == 0));
  int undone = 0;
  while (h.undo(d)) undone++;
  CHECK(undone == 4 && h.undo_count() == 0 && h.redo_count() == 4);
  size_t n_now = scene_write(d, t_now, sizeof t_now);
  CHECK(n_now == n_orig && std::memcmp(t_orig, t_now, n_orig) == 0); // geri al = baslangic baytlari
  int redone = 0;
  while (h.redo(d)) redone++;
  CHECK(redone == 4);
  n_now = scene_write(d, t_now, sizeof t_now);
  CHECK(n_now == n_edit && std::memcmp(t_edit, t_now, n_edit) == 0); // yinele = duzenlenmis baytlar
  // Iki geri al + yeni islem: yinele kuyrugu silinir.
  CHECK(h.undo(d) && h.undo(d) && h.redo_count() == 2);
  e = d.entities[0];
  e.scale.y = 3;
  CHECK(h.set_entity(d, 0, e));
  CHECK(h.redo_count() == 0 && !h.redo(d));
  // Kontrol: bos gunlukte geri al / yinele false.
  SceneHistory h2;
  CHECK(h2.init(arena(), 4));
  CHECK(!h2.undo(d) && !h2.redo(d));
  // Halka: 4 kapasite, 6 islem -> 4 geri alinir, en eski ikisi dusmustur.
  for (int i = 0; i < 6; i++) { e = d.entities[0]; e.pos.z += 1; h2.set_entity(d, 0, e); }
  CHECK(h2.undo_count() == 4);
  undone = 0;
  while (h2.undo(d)) undone++;
  CHECK(undone == 4);
  std::printf("    [bilgi] gunluk: 4 islem geri/ileri bayt-esit; halka 6->4\n");
}

ENGINE_TEST(scene_file_editor_sahne_is_canonical) {
  static SceneDesc d, d2;
  static char path[1024], raw[64 << 10], out[64 << 10];
  asset_path(path, sizeof path, "editor.sahne");
  SceneError err{};
  const bool ok = scene_load(arena(), path, &d, &err);
  if (!ok) std::printf("    [bilgi] %s: %s\n", path, err.msg);
  CHECK(ok);
  if (!ok) return;
  CHECK(d.entity_count == 8 && d.asset_count == 3);
  const int32_t kup = d.find_entity("kup_dusen");
  CHECK(kup >= 0);
  if (kup >= 0) CHECK((d.entities[kup].components & (kSceneModel | kSceneBody)) == (kSceneModel | kSceneBody) && d.entities[kup].dynamic);
  CHECK(d.find_entity("yok") == -1);
  // Dosya kanonik: yaz(oku(dosya)) == dosya baytlari.
  FILE *f = std::fopen(path, "rb");
  size_t raw_n = f ? std::fread(raw, 1, sizeof raw, f) : 0;
  if (f) std::fclose(f);
  const size_t out_n = scene_write(d, out, sizeof out);
  CHECK(raw_n > 0 && raw_n == out_n && std::memcmp(raw, out, raw_n) == 0);
  if (raw_n != out_n) std::printf("    [bilgi] dosya %zu bayt, yazici %zu bayt\n", raw_n, out_n);
  // Kaydet -> yukle -> ayni.
  char tmpl[512];
  tmp_template(tmpl, sizeof tmpl, "sahne");
  int fd = mkstemp(tmpl);
  CHECK(fd >= 0);
  if (fd >= 0) {
    close(fd);
    CHECK(scene_save(arena(), d, tmpl, &err));
    CHECK(scene_load(arena(), tmpl, &d2, &err));
    static char out2[64 << 10];
    const size_t n2 = scene_write(d2, out2, sizeof out2);
    CHECK(n2 == out_n && std::memcmp(out, out2, out_n) == 0);
    unlink(tmpl);
  }
  // Kontrol: olmayan dosya false, satir 0.
  CHECK(!scene_load(arena(), "/olmayan/dizin/x.sahne", &d2, &err) && err.line == 0);
  char dir[64];
  scene_dir_of("a/b/c.sahne", dir, sizeof dir); CHECK(std::strcmp(dir, "a/b") == 0);
  scene_dir_of("c.sahne", dir, sizeof dir); CHECK(std::strcmp(dir, ".") == 0);
  scene_dir_of("/c.sahne", dir, sizeof dir); CHECK(std::strcmp(dir, "/") == 0);
  std::printf("    [bilgi] editor.sahne: %u varlik, %u kaynak, %zu bayt kanonik\n", d.entity_count, d.asset_count, raw_n);
}

ENGINE_TEST(scene_bodies_spawn_and_settle_in_physics) {
  static SceneDesc d;
  static sim::BodyId ids[kSceneMaxEntities];
  sim::Physics ph;
  sim::PhysicsConfig cfg;
  cfg.threads = 1;
  if (!ph.init(arena(), cfg)) { CHECK(false); return; }
  auto build = [&](bool with_floor) {
    d = SceneDesc{};
    SceneEntity e{};
    if (with_floor) {
      std::snprintf(e.name, sizeof e.name, "zemin");
      e.pos = {0, -1, 0}; e.components = kSceneBody; e.half = {10, 1, 10}; e.dynamic = false;
      d.insert_entity(d.entity_count, e);
    }
    e = SceneEntity{};
    std::snprintf(e.name, sizeof e.name, "kutu");
    e.pos = {0, 5, 0}; e.rot_deg = {0, 30, 0}; e.components = kSceneBody; e.half = {0.5f, 0.5f, 0.5f}; e.dynamic = true;
    d.insert_entity(d.entity_count, e);
    e = SceneEntity{};
    std::snprintf(e.name, sizeof e.name, "govdesiz");
    e.pos = {3, 0, 0}; e.components = kSceneLight;
    d.insert_entity(d.entity_count, e);
  };
  build(true);
  uint32_t n = scene_spawn_bodies(d, ph, ids);
  CHECK(n == 2);
  CHECK(ids[0].valid() && ids[1].valid() && !ids[2].valid());
  for (int i = 0; i < 180; i++) ph.step(1.0f / 60.0f, 1);
  const Mat4 m = scene_body_matrix(d.entities[1], ph, ids[1]);
  const float y_floor = ph.position(ids[1]).y;
  CHECK(y_floor > 0.3f && y_floor < 0.8f); // zemine oturdu (yarim kenar 0.5)
  CHECK(std::fabs(m.m[3][1] - y_floor) < 1e-6f && std::fabs(m.m[3][3] - 1.0f) < 1e-6f);
  scene_remove_bodies(ph, ids, d.entity_count);
  CHECK(!ids[1].valid());
  // Kontrol: zemin yokken duser.
  build(false);
  n = scene_spawn_bodies(d, ph, ids);
  CHECK(n == 1);
  for (int i = 0; i < 180; i++) ph.step(1.0f / 60.0f, 1);
  const float y_free = ph.position(ids[0]).y;
  CHECK(y_free < -5.0f);
  scene_remove_bodies(ph, ids, d.entity_count);
  ph.shutdown();
  std::printf("    [bilgi] 3 s sonra kutu y: zeminli %.3f, zeminsiz %.3f\n", y_floor, y_free);
}

ENGINE_TEST(scene_rotation_quat_matches_matrix) {
  SceneEntity e{};
  e.pos = {1, 2, 3}; e.rot_deg = {30, 45, 60}; e.scale = {1, 2, 0.5f};
  const Mat4 m = scene_entity_matrix(e);
  const Mat4 r = to_mat4(scene_entity_rotation(e));
  // Donus kismi: m sutunlari = r sutunlari * olcek.
  const float s[3] = {e.scale.x, e.scale.y, e.scale.z};
  bool ok = true;
  for (int c = 0; c < 3; c++)
    for (int rr = 0; rr < 3; rr++)
      if (std::fabs(m.m[c][rr] - r.m[c][rr] * s[c]) > 1e-5f) ok = false;
  CHECK(ok);
  CHECK(std::fabs(m.m[3][0] - 1) < 1e-6f && std::fabs(m.m[3][1] - 2) < 1e-6f && std::fabs(m.m[3][2] - 3) < 1e-6f);
  // Kontrol: farkli sira (Rx*Ry*Rz) ayni matrisi VERMEZ — sira gercekten olculuyor.
  const float k = 3.14159265f / 180.0f;
  const Mat4 other = Mat4::rotate({1, 0, 0}, 30 * k) * Mat4::rotate({0, 1, 0}, 45 * k) * Mat4::rotate({0, 0, 1}, 60 * k);
  bool differs = false;
  for (int c = 0; c < 3; c++)
    for (int rr = 0; rr < 3; rr++)
      if (std::fabs(other.m[c][rr] - r.m[c][rr]) > 1e-3f) differs = true;
  CHECK(differs);
}

ENGINE_TEST(scene_pick_returns_nearest_hit_and_misses) {
  // Uc kutu +z boyunca: 5, 10, 15 uzaklikta; isin +z'ye bakar -> en yakin (0).
  SceneEntity e{};
  e.components = kSceneBody; e.half = {0.5f, 0.5f, 0.5f};
  SceneBounds w[4];
  for (int i = 0; i < 3; i++) { e.pos = {0, 0, 5.0f + 5.0f * i}; w[i] = scene_world_bounds(scene_entity_local_bounds(e, nullptr), scene_entity_matrix(e)); }
  // 4.: 45 derece donmus, 2 birim yana kaymis kutu — dunya AABB kose kapsar (yarim kenar 0.707).
  e.pos = {3.0f, 0, 5.0f}; e.rot_deg = {0, 45, 0};
  w[3] = scene_world_bounds(scene_entity_local_bounds(e, nullptr), scene_entity_matrix(e));
  CHECK(std::fabs(w[3].hi.x - (3.0f + 0.70710678f)) < 1e-4f && std::fabs(w[3].lo.z - (5.0f - 0.70710678f)) < 1e-4f);
  float t = 0;
  CHECK(scene_pick(w, 4, {0, 0, 0}, {0, 0, 1}, &t) == 0);
  CHECK(std::fabs(t - 4.5f) < 1e-4f);
  CHECK(scene_pick(w, 4, {0, 0, 12}, {0, 0, 1}, &t) == 2);   // ortadakinin arkasindan: 3.
  CHECK(scene_pick(w, 4, {0, 0, 7.5f}, {0, 0, -1}, &t) == 0); // geri: 1.
  CHECK(scene_pick(w, 4, {0, 0, 5}, {1, 0, 0}, &t) == 0 && t == 0.0f); // isin kutunun icinde: t 0
  CHECK(scene_pick(w, 4, {3.0f, 0, 0}, {0, 0, 1}, &t) == 3);  // donmus kutu (AABB kosesi)
  // Kontrol: ters yon ve bosluktan gecen isin -1.
  CHECK(scene_pick(w, 4, {0, 0, 0}, {0, 0, -1}, &t) == -1);
  CHECK(scene_pick(w, 4, {0, 2, 0}, {0, 0, 1}, &t) == -1);
  CHECK(scene_pick(w, 4, {0, 0, 0}, normalize(Vec3{1, 0, 1}), &t) == -1);
  // Model sinirlari + isaret: modelli varlik model kutusunu, bos varlik 0.3 isaret kutusunu alir.
  SceneBounds mdl{{-2, -1, -2}, {2, 1, 2}};
  SceneEntity m{}; m.components = kSceneModel; m.asset = 0;
  const SceneBounds lb = scene_entity_local_bounds(m, &mdl);
  CHECK(std::fabs(lb.lo.x + 2) < 1e-6f && std::fabs(lb.hi.y - 1) < 1e-6f);
  SceneEntity empty{};
  const SceneBounds eb = scene_entity_local_bounds(empty, nullptr);
  CHECK(std::fabs(eb.hi.x - 0.15f) < 1e-6f);
  // Olcekli varlik: dunya AABB olcekle buyur.
  m.scale = {2, 2, 2};
  const SceneBounds sb = scene_world_bounds(lb, scene_entity_matrix(m));
  CHECK(std::fabs(sb.hi.x - 4) < 1e-5f && std::fabs(sb.lo.y + 2) < 1e-5f);
  std::printf("    [bilgi] secim: en yakin kutu t=%.3f; ters/bosluk isinlari -1\n", 4.5f);
}
