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

// Eski 6-tokenli "isik" satiri (tur yok) -> varsayilan Nokta; yeni 7-tokenli
// satir ("nokta"/"yonlu") tur alanini ayarlar; bilinmeyen anahtar sozcuk
// reddedilir. Gecmis dosyalarin sessizce Nokta okunmasi (C1'in geriye donuklugu).
ENGINE_TEST(scene_light_type_roundtrips_and_defaults_to_point) {
  static SceneDesc d;
  SceneError err{};
  const char *old6 = "tulpar-sahne 1\nnesne \"lamba\"\n  isik 1 1 1 2 5\nson\n";
  CHECK(scene_parse(old6, std::strlen(old6), &d, &err));
  CHECK(d.entity_count == 1 && (d.entities[0].components & kSceneLight));
  CHECK(d.entities[0].light_type == SceneLightType::Point);

  const char *new_dir = "tulpar-sahne 1\nnesne \"gunes\"\n  isik 1 1 1 2 5 yonlu\nson\n";
  CHECK(scene_parse(new_dir, std::strlen(new_dir), &d, &err));
  CHECK(d.entities[0].light_type == SceneLightType::Directional);

  const char *new_point = "tulpar-sahne 1\nnesne \"lamba2\"\n  isik 1 1 1 2 5 nokta\nson\n";
  CHECK(scene_parse(new_point, std::strlen(new_point), &d, &err));
  CHECK(d.entities[0].light_type == SceneLightType::Point);

  const char *bad_kw = "tulpar-sahne 1\nnesne \"x\"\n  isik 1 1 1 2 5 gokkusagi\nson\n";
  CHECK(!scene_parse(bad_kw, std::strlen(bad_kw), &d, &err));

  // Yaz-oku: tur her zaman aciktan yazilir (govde'nin dinamik|sabit'i gibi).
  static char t[256];
  d = SceneDesc{};
  SceneEntity e{};
  std::snprintf(e.name, sizeof e.name, "gunes");
  e.components = kSceneLight;
  e.light_type = SceneLightType::Directional;
  e.light_color = {1, 1, 1}; e.light_intensity = 2; e.light_radius = 5;
  CHECK(d.insert_entity(0, e));
  const size_t n = scene_write(d, t, sizeof t);
  CHECK(std::strstr(t, "yonlu") != nullptr);
  static SceneDesc d2;
  CHECK(scene_parse(t, n, &d2, &err));
  CHECK(scene_entity_equal(d.entities[0], d2.entities[0]));
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

// =============================================================================
// Faz E2 — sahne agaci (ebeveyn/cocuk + donusum kalitimi)
// =============================================================================

namespace {
// Karisik agac: kok, cocuk, torun, ILERI referans (ebeveyni kendinden SONRA
// gelen varlik) ve ikinci bir kok. Ileri referans bilerek var: bicim
// "ebeveyn-once" siralama SART KOSMUYOR, tutarliligi dogrulama sagliyor.
void fill_tree(SceneDesc &d) {
  d = SceneDesc{};
  SceneEntity e{};
  std::snprintf(e.name, sizeof e.name, "kok");
  e.pos = {2, -1, 0.5f};
  d.insert_entity(d.entity_count, e);
  e = SceneEntity{};
  std::snprintf(e.name, sizeof e.name, "cocuk");
  e.pos = {1, 0, 0}; e.parent = 0;
  d.insert_entity(d.entity_count, e);
  e = SceneEntity{};
  std::snprintf(e.name, sizeof e.name, "torun");
  e.pos = {0, 3, 0}; e.rot_deg = {30, 45, 60}; e.scale = {1, 2, 0.5f}; e.parent = 1;
  e.components = kSceneBody; e.shape = SceneShape::Box; e.half = {0.5f, 0.25f, 0.5f};
  d.insert_entity(d.entity_count, e);
  e = SceneEntity{};
  std::snprintf(e.name, sizeof e.name, "ileri"); // ebeveyni 4 -> henuz yok
  e.pos = {0, 0, 1}; e.parent = 4; e.flags = kSceneHidden | kSceneLocked;
  d.insert_entity(d.entity_count, e);
  e = SceneEntity{};
  std::snprintf(e.name, sizeof e.name, "kok2");
  e.pos = {-4, 0, 0};
  d.insert_entity(d.entity_count, e);
}
// n varlikli zincir metni: 0 kok, k'nin ebeveyni k-1 (derinlik n-1).
size_t chain_text(char *buf, size_t cap, uint32_t n) {
  size_t k = (size_t)std::snprintf(buf, cap, "tulpar-sahne 1\n");
  for (uint32_t i = 0; i < n; i++) {
    k += (size_t)std::snprintf(buf + k, cap - k, "nesne \"v%u\"\n", i);
    if (i) k += (size_t)std::snprintf(buf + k, cap - k, "  ebeveyn %u\n", i - 1);
    k += (size_t)std::snprintf(buf + k, cap - k, "son\n");
  }
  return k;
}
} // namespace

ENGINE_TEST(scene_tree_text_keeps_parent_and_rejects_bad_chains) {
  static SceneDesc a, b, flat;
  static char t1[64 << 10], t2[64 << 10];
  fill_tree(a);
  const size_t n1 = scene_write(a, t1, sizeof t1);
  SceneError err{};
  const bool ok = scene_parse(t1, n1, &b, &err);
  if (!ok) std::printf("    [bilgi] ayristirma: %s\n", err.msg);
  CHECK(ok);
  const size_t n2 = scene_write(b, t2, sizeof t2);
  CHECK(n1 == n2 && std::memcmp(t1, t2, n1) == 0); // gidis-donus ayni bayt
  CHECK(b.entity_count == 5);
  for (uint32_t i = 0; i < 5 && i < b.entity_count; i++) CHECK(scene_entity_equal(a.entities[i], b.entities[i]));
  CHECK(b.entities[1].parent == 0 && b.entities[2].parent == 1 && b.entities[3].parent == 4 && b.entities[4].parent == -1);
  CHECK(b.entities[3].flags == (kSceneHidden | kSceneLocked));
  CHECK(std::strstr(t1, "  ebeveyn 4\n") != nullptr && std::strstr(t1, "  bayrak 3\n") != nullptr);
  // KONTROL: hiyerarsisiz sahne ebeveyn/bayrak satiri YAZMAZ (editor.sahne
  // kanonik kapisi bunun uzerinde durur; alan eklendi diye baytlar kaymamali).
  fill(flat);
  static char tf[64 << 10];
  const size_t nf = scene_write(flat, tf, sizeof tf);
  CHECK(std::strstr(tf, "ebeveyn") == nullptr && std::strstr(tf, "bayrak") == nullptr);
  // Bozuk zincirler: hepsi SATIR NUMARALI hata, sessiz kabul yok.
  const char *oob = "tulpar-sahne 1\nnesne \"a\"\nson\nnesne \"b\"\n  ebeveyn 7\nson\n";
  CHECK(!scene_parse(oob, std::strlen(oob), &b, &err) && err.line == 5);
  CHECK(std::strstr(err.msg, "ebeveyn") != nullptr);
  const char *self = "tulpar-sahne 1\nnesne \"a\"\n  ebeveyn 0\nson\n";
  CHECK(!scene_parse(self, std::strlen(self), &b, &err) && err.line == 3);
  const char *cyc = "tulpar-sahne 1\nnesne \"a\"\n  ebeveyn 1\nson\nnesne \"b\"\n  ebeveyn 0\nson\n";
  CHECK(!scene_parse(cyc, std::strlen(cyc), &b, &err) && err.line == 3);
  const char *neg = "tulpar-sahne 1\nnesne \"a\"\n  ebeveyn -1\nson\n";
  CHECK(!scene_parse(neg, std::strlen(neg), &b, &err)); // -1 dosyada YAZILMAZ
  const char *dup = "tulpar-sahne 1\nnesne \"a\"\nson\nnesne \"b\"\n  ebeveyn 0\n  ebeveyn 0\nson\n";
  CHECK(!scene_parse(dup, std::strlen(dup), &b, &err) && err.line == 6);
  const char *badflag = "tulpar-sahne 1\nnesne \"a\"\n  bayrak 9\nson\n";
  CHECK(!scene_parse(badflag, std::strlen(badflag), &b, &err) && err.line == 3);
  // Derinlik tavani: kSceneMaxDepth derinlik (tavan+1 varlik) GECER — pozitif
  // kontrol; bir fazlasi reddedilir ve donguye GIRMEZ.
  static char big[1 << 16];
  size_t bn = chain_text(big, sizeof big, kSceneMaxDepth + 1);
  CHECK(scene_parse(big, bn, &b, &err));
  CHECK(b.entity_count == kSceneMaxDepth + 1 && scene_tree_depth(b, kSceneMaxDepth) == kSceneMaxDepth);
  bn = chain_text(big, sizeof big, kSceneMaxDepth + 2);
  CHECK(!scene_parse(big, bn, &b, &err));
  std::printf("    [bilgi] agac metni %zu bayt; derinlik tavani %u gecti, %u reddedildi: %s\n", n1, kSceneMaxDepth,
              kSceneMaxDepth + 1, err.msg);
}

ENGINE_TEST(scene_tree_world_matrix_composes_chain) {
  // Hiyerarsik sahne ve ELLE duzlestirilmis ikizi. Ebeveynler yalniz OTELEME
  // tasir: duzlestirme toplamdir ve karsilastirma BIT-TAM olabilir (donuslu
  // ebeveynde ayristirma sapmasi girerdi, kapi da tolerans olcerdi).
  static SceneDesc h, f;
  fill_tree(h);
  f = h;
  for (uint32_t i = 0; i < f.entity_count; i++) f.entities[i].parent = -1;
  // Parantezler zincirin CARPMA sirasiyla ayni: dunya matrisi kok'ten asagi
  // kurulur ((kok*cocuk)*torun), yani oteleme toplami torun + (cocuk + kok)
  // olarak birikir. Farkli parantezleme bit-tam esitligi bozardi.
  f.entities[1].pos = h.entities[1].pos + h.entities[0].pos;                       // cocuk
  f.entities[2].pos = h.entities[2].pos + (h.entities[1].pos + h.entities[0].pos); // torun
  f.entities[3].pos = h.entities[3].pos + h.entities[4].pos;                       // ileri referans
  bool bitexact = true;
  for (uint32_t i = 0; i < h.entity_count; i++) {
    const Mat4 wh = scene_entity_world_matrix(h, i), wf = scene_entity_world_matrix(f, i);
    if (std::memcmp(&wh.m[0][0], &wf.m[0][0], sizeof wh.m) != 0) bitexact = false;
  }
  CHECK(bitexact);
  // Derinlik + belirlenimli on-sirali gezinti.
  CHECK(scene_tree_depth(h, 0) == 0 && scene_tree_depth(h, 1) == 1 && scene_tree_depth(h, 2) == 2);
  CHECK(scene_tree_depth(h, 4) == 0 && scene_tree_depth(h, 3) == 1);
  int32_t order[kSceneMaxEntities];
  const uint32_t n = scene_tree_order(h, order, kSceneMaxEntities);
  CHECK(n == h.entity_count);
  CHECK(order[0] == 0 && order[1] == 1 && order[2] == 2 && order[3] == 4 && order[4] == 3);
  CHECK(scene_tree_order(h, nullptr, 0) == n); // sayim gecisi
  int32_t small_out[2] = {-9, -9};
  CHECK(scene_tree_order(h, small_out, 2) == n && small_out[0] == 0 && small_out[1] == 1); // kapasite tasmasi sessiz degil
  // KONTROL 1: ebeveyni DONDURUNCE cocuk da doner (duzlestirilmis olan DONMEZ).
  static SceneDesc r;
  r = h;
  r.entities[0].rot_deg = {0, 90, 0};
  const Mat4 wr = scene_entity_world_matrix(r, 1), wh1 = scene_entity_world_matrix(h, 1);
  const Vec3 pr{wr.m[3][0], wr.m[3][1], wr.m[3][2]}, ph1{wh1.m[3][0], wh1.m[3][1], wh1.m[3][2]};
  // kok (2,-1,0.5) + Ry(90)*(1,0,0) = (2,-1,0.5) + (0,0,-1)
  CHECK(nearly_equal(pr, Vec3{2.0f, -1.0f, -0.5f}, 1e-4f));
  CHECK(!nearly_equal(pr, ph1, 1e-3f));
  const Mat4 wfr = scene_entity_world_matrix(f, 1);
  CHECK(std::memcmp(&wfr.m[0][0], &wh1.m[0][0], sizeof wh1.m) == 0); // duz ikiz etkilenmedi
  // KONTROL 2: donus kalitimi olcegi de tasir (ebeveyn olcegi cocugu buyutur).
  static SceneDesc s;
  s = h;
  s.entities[0].scale = {2, 2, 2};
  const Mat4 ws = scene_entity_world_matrix(s, 1);
  CHECK(nearly_equal(Vec3{ws.m[3][0], ws.m[3][1], ws.m[3][2]}, Vec3{4.0f, -1.0f, 0.5f}, 1e-4f));
  CHECK(nearly_equal(scene_entity_world_scale(s, 2), Vec3{2, 4, 1}, 1e-5f));
  // Dogrulama + bozuk agac.
  uint32_t bad = 0xFFFFFFFFu;
  CHECK(scene_tree_validate(h, &bad) && bad == 0);
  static SceneDesc c;
  c = h;
  c.entities[0].parent = 2; // 0 -> 2 -> 1 -> 0 dongusu
  CHECK(!scene_tree_validate(c, &bad));
  CHECK(scene_tree_depth(c, 0) == kSceneMaxDepth); // tavanda durur, donguye girmez
  c = h;
  c.entities[1].parent = 99;
  CHECK(!scene_tree_validate(c, &bad) && bad == 1);
  std::printf("    [bilgi] agac: %u dugum, on-sira 0,1,2,4,3; dunya matrisi duzlestirilmisle BIT-TAM; dondurulmus ebeveyn cocugu (%.3f,%.3f,%.3f)'e tasidi\n",
              n, (double)pr.x, (double)pr.y, (double)pr.z);
}

ENGINE_TEST(scene_tree_reparent_keeps_world_and_undo_is_byte_exact) {
  static SceneDesc d, snapshot;
  static char t_orig[64 << 10], t_now[64 << 10];
  d = SceneDesc{};
  SceneEntity e{};
  std::snprintf(e.name, sizeof e.name, "ebeveyn");
  e.pos = {2, 1, -3}; e.rot_deg = {0, 40, 0}; e.scale = {1.5f, 1.5f, 1.5f};
  d.insert_entity(d.entity_count, e);
  e = SceneEntity{};
  std::snprintf(e.name, sizeof e.name, "serbest");
  e.pos = {5, 0, 2}; e.rot_deg = {0, 0, 25};
  d.insert_entity(d.entity_count, e);
  e = SceneEntity{};
  std::snprintf(e.name, sizeof e.name, "torun");
  e.pos = {0, 1, 0}; e.parent = 1;
  d.insert_entity(d.entity_count, e);
  const size_t n_orig = scene_write(d, t_orig, sizeof t_orig);
  snapshot = d;

  const Mat4 before = scene_entity_world_matrix(d, 1), tbefore = scene_entity_world_matrix(d, 2);
  SceneHistory h;
  CHECK(h.init(arena(), 16));
  CHECK(h.reparent(d, 1, 0)); // serbest -> ebeveynin cocugu
  CHECK(d.entities[1].parent == 0);
  const Mat4 after = scene_entity_world_matrix(d, 1), tafter = scene_entity_world_matrix(d, 2);
  float worst = 0;
  for (int c = 0; c < 4; c++)
    for (int r = 0; r < 4; r++) {
      const float dv = std::fabs(after.m[c][r] - before.m[c][r]);
      if (dv > worst) worst = dv;
      const float dt = std::fabs(tafter.m[c][r] - tbefore.m[c][r]);
      if (dt > worst) worst = dt;
    }
  CHECK(worst < 1e-4f); // dunya donusumu (torun dahil) korundu
  // Reddedilen istekler HICBIR SEYI degistirmez.
  static char t_before_reject[64 << 10];
  const size_t nb = scene_write(d, t_before_reject, sizeof t_before_reject);
  CHECK(!scene_reparent(d, 0, 1)); // dongu: 0'in cocugu 1
  CHECK(!scene_reparent(d, 0, 2)); // dongu: 2, 1'in altinda
  CHECK(!scene_reparent(d, 1, 1)); // kendine
  CHECK(!scene_reparent(d, 1, 99));
  CHECK(!scene_reparent(d, 99, 0));
  size_t nn = scene_write(d, t_now, sizeof t_now);
  CHECK(nn == nb && std::memcmp(t_before_reject, t_now, nb) == 0);
  // Ayni ebeveyne "yeniden" baglamak islem URETMEZ (ayristir/kur sapmasi yok).
  CHECK(!h.reparent(d, 1, 0));
  CHECK(h.undo_count() == 1);
  // Geri al: BASLANGIC BAYTLARI.
  CHECK(h.undo(d));
  nn = scene_write(d, t_now, sizeof t_now);
  CHECK(nn == n_orig && std::memcmp(t_orig, t_now, n_orig) == 0);
  CHECK(scene_entity_equal(d.entities[1], snapshot.entities[1]));
  CHECK(h.redo(d) && d.entities[1].parent == 0);
  // Ayirma (kok'e alma) da dunyayi korur.
  const Mat4 w1 = scene_entity_world_matrix(d, 1);
  CHECK(scene_reparent(d, 1, -1) && d.entities[1].parent == -1);
  const Mat4 w2 = scene_entity_world_matrix(d, 1);
  float worst2 = 0;
  for (int c = 0; c < 4; c++)
    for (int r = 0; r < 4; r++) {
      const float dv = std::fabs(w2.m[c][r] - w1.m[c][r]);
      if (dv > worst2) worst2 = dv;
    }
  CHECK(worst2 < 1e-4f);
  // Derinlik tavani: dolu bir zincirin dibine baska zincir eklenemez.
  static SceneDesc deep;
  deep = SceneDesc{};
  for (uint32_t i = 0; i <= kSceneMaxDepth; i++) {
    SceneEntity v{};
    std::snprintf(v.name, sizeof v.name, "v%u", i);
    v.parent = i ? (int32_t)i - 1 : -1;
    deep.insert_entity(deep.entity_count, v);
  }
  SceneEntity extra{};
  std::snprintf(extra.name, sizeof extra.name, "fazla");
  deep.insert_entity(deep.entity_count, extra);
  const uint32_t last = deep.entity_count - 1;
  CHECK(!scene_reparent(deep, last, (int32_t)kSceneMaxDepth)); // tavan asilir
  CHECK(scene_reparent(deep, last, (int32_t)kSceneMaxDepth - 1)); // pozitif kontrol
  // Gizmo yolu: dunya matrisini YEREL'e cevirme (editor gizmo'yu dunyada surer).
  static SceneDesc g;
  fill_tree(g);
  float rt = 0;
  for (uint32_t i = 0; i < g.entity_count; i++) {
    const Mat4 loc = scene_world_to_local_matrix(g, i, scene_entity_world_matrix(g, i));
    const Mat4 ref = scene_entity_matrix(g.entities[i]);
    for (int c = 0; c < 4; c++)
      for (int r2 = 0; r2 < 4; r2++) {
        const float dv = std::fabs(loc.m[c][r2] - ref.m[c][r2]);
        if (dv > rt) rt = dv;
      }
  }
  CHECK(rt < 1e-5f); // dunya -> yerel -> ayni yerel matris
  // KONTROL: cevirmeden yazmak (dunya matrisini YEREL sanmak) cocukta SAPAR.
  const Mat4 wrong = scene_entity_world_matrix(g, 2), right = scene_entity_matrix(g.entities[2]);
  bool differs = false;
  for (int c = 0; c < 4 && !differs; c++)
    for (int r2 = 0; r2 < 4; r2++)
      if (std::fabs(wrong.m[c][r2] - right.m[c][r2]) > 1e-3f) { differs = true; break; }
  CHECK(differs);
  // Kok varlikta cevrim kimlik (bit-tam).
  const Mat4 rootw = scene_entity_world_matrix(g, 0);
  const Mat4 rootl = scene_world_to_local_matrix(g, 0, rootw);
  CHECK(std::memcmp(&rootw.m[0][0], &rootl.m[0][0], sizeof rootw.m) == 0);
  std::printf("    [bilgi] yeniden ebeveynleme: dunya sapmasi %.3e (baglama) / %.3e (ayirma); dunya->yerel gidis-donus %.3e; geri alma bayt-tam\n",
              (double)worst, (double)worst2, (double)rt);
}

ENGINE_TEST(scene_tree_remove_keeps_indices_and_undo_restores_children) {
  // 0 A(kok) 1 B(A) 2 C(B) 3 D(A) 4 E(kok) 5 F(E)
  static SceneDesc d;
  static char t_orig[64 << 10], t_now[64 << 10];
  d = SceneDesc{};
  const int32_t parents[6] = {-1, 0, 1, 0, -1, 4};
  const char *names[6] = {"A", "B", "C", "D", "E", "F"};
  for (uint32_t i = 0; i < 6; i++) {
    SceneEntity e{};
    std::snprintf(e.name, sizeof e.name, "%s", names[i]);
    e.pos = {(float)i, 0, 0};
    e.parent = parents[i];
    d.insert_entity(d.entity_count, e);
  }
  const size_t n_orig = scene_write(d, t_orig, sizeof t_orig);
  SceneHistory h;
  CHECK(h.init(arena(), 8));
  CHECK(h.remove_entity(d, 1)); // ortadaki B: cocugu C var
  CHECK(d.entity_count == 5);
  // Kaydirma: C 2->1, D 3->2, E 4->3, F 5->4. C buyukbabaya (A=0) bagli.
  CHECK(std::strcmp(d.entities[1].name, "C") == 0 && d.entities[1].parent == 0);
  CHECK(std::strcmp(d.entities[2].name, "D") == 0 && d.entities[2].parent == 0);
  CHECK(std::strcmp(d.entities[3].name, "E") == 0 && d.entities[3].parent == -1);
  CHECK(std::strcmp(d.entities[4].name, "F") == 0 && d.entities[4].parent == 3); // 4 -> 3 kaydi
  uint32_t bad = 0;
  CHECK(scene_tree_validate(d, &bad));
  // Geri al: cocuk yeniden B'ye baglanir (child_mask) — bayt-tam.
  CHECK(h.undo(d));
  size_t nn = scene_write(d, t_now, sizeof t_now);
  CHECK(nn == n_orig && std::memcmp(t_orig, t_now, n_orig) == 0);
  CHECK(h.redo(d) && d.entity_count == 5 && d.entities[1].parent == 0);
  CHECK(h.undo(d));
  // Kok silinince cocuklari KOK olur (buyukbaba yok).
  CHECK(h.remove_entity(d, 0)); // A
  CHECK(d.entity_count == 5);
  CHECK(std::strcmp(d.entities[0].name, "B") == 0 && d.entities[0].parent == -1);
  CHECK(std::strcmp(d.entities[2].name, "D") == 0 && d.entities[2].parent == -1);
  CHECK(d.entities[1].parent == 0); // C hala B'nin cocugu
  CHECK(scene_tree_validate(d, &bad));
  CHECK(h.undo(d));
  nn = scene_write(d, t_now, sizeof t_now);
  CHECK(nn == n_orig && std::memcmp(t_orig, t_now, n_orig) == 0);
  // KONTROL: ebeveyn alani OLMAYAN (hepsi kok) bir sahnede silme eskisi gibi.
  static SceneDesc flat;
  fill(flat);
  static char f_orig[64 << 10], f_now[64 << 10];
  const size_t fn = scene_write(flat, f_orig, sizeof f_orig);
  SceneHistory h2;
  CHECK(h2.init(arena(), 4));
  CHECK(h2.remove_entity(flat, 2) && flat.entity_count == 4);
  CHECK(h2.undo(flat));
  CHECK(scene_write(flat, f_now, sizeof f_now) == fn && std::memcmp(f_orig, f_now, fn) == 0);
  std::printf("    [bilgi] silme: 6->5 varlik, ebeveyn indeksleri kaydi, cocuk buyukbabaya; geri alma bayt-tam\n");
}

ENGINE_TEST(scene_tree_bodies_spawn_at_world_transform) {
  static SceneDesc d;
  static sim::BodyId ids[kSceneMaxEntities];
  sim::Physics ph;
  sim::PhysicsConfig cfg;
  cfg.threads = 1;
  if (!ph.init(arena(), cfg)) { CHECK(false); return; }
  auto build = [&](bool linked) {
    d = SceneDesc{};
    SceneEntity e{};
    std::snprintf(e.name, sizeof e.name, "platform"); // govdesiz tasiyici
    e.pos = {10, 4, -2};
    e.scale = {2, 2, 2};
    d.insert_entity(d.entity_count, e);
    e = SceneEntity{};
    std::snprintf(e.name, sizeof e.name, "kutu");
    e.pos = {0, 1, 0};
    e.parent = linked ? 0 : -1;
    e.components = kSceneBody; e.shape = SceneShape::Box; e.half = {0.5f, 0.5f, 0.5f}; e.dynamic = false;
    d.insert_entity(d.entity_count, e);
  };
  build(true);
  uint32_t n = scene_spawn_bodies(d, ph, ids);
  CHECK(n == 1 && ids[1].valid());
  const Vec3 linked_pos = ph.position(ids[1]);
  CHECK(nearly_equal(linked_pos, Vec3{10, 6, -2}, 1e-3f)); // 4 + 2*1
  scene_remove_bodies(ph, ids, d.entity_count);
  // KONTROL: ayni sahne, ebeveyn bagi YOK -> govde yerel ofsette dogar.
  build(false);
  n = scene_spawn_bodies(d, ph, ids);
  CHECK(n == 1);
  const Vec3 free_pos = ph.position(ids[1]);
  CHECK(nearly_equal(free_pos, Vec3{0, 1, 0}, 1e-3f));
  CHECK(!nearly_equal(free_pos, linked_pos, 1e-2f));
  scene_remove_bodies(ph, ids, d.entity_count);
  ph.shutdown();
  std::printf("    [bilgi] govde konumu: ebeveynli (%.2f,%.2f,%.2f), ebeveynsiz (%.2f,%.2f,%.2f)\n", (double)linked_pos.x,
              (double)linked_pos.y, (double)linked_pos.z, (double)free_pos.x, (double)free_pos.y, (double)free_pos.z);
}
