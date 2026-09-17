// L6 CONTENT — Sahne veri modeli + deterministik metin format (.sahne) +
// islem gunlugu (geri al / yinele). PLAN L7 "The Truth": editorun tamami
// tek bir veri modelinin ustune oturur; her degisiklik bir islemdir.
//
// Sahne dosyasi = yazar formati (L6 icerik boru hatti, offline). Runtime bunu
// yorumlamaz (PLAN §6 "sahne bir blob + kod"); derlenmis blob sonraki dilim.
// Ayirma yok: sabit kapasiteli diziler, dosya okuma icin cagiranin arenasi.
// Format ASCII, satir tabanli, deterministik: ayni SceneDesc -> ayni bayt.
#pragma once
#include <cstddef>
#include <cstdint>

#include "core/math/vec.hpp"
#include "core/memory/arena.hpp"
#include "sim/physics.hpp"

namespace tulpar::engine::content {

constexpr uint32_t kSceneVersion = 1;
constexpr uint32_t kSceneMaxEntities = 256;
constexpr uint32_t kSceneMaxAssets = 16;
constexpr uint32_t kSceneNameLen = 32;   // NUL dahil
constexpr uint32_t kScenePathLen = 128;  // NUL dahil

enum SceneComponentBits : uint32_t {
  kSceneModel = 1u << 0, // glTF model (kaynak indeksi + renk)
  kSceneAnim = 1u << 1,  // model klibi (iskeletli)
  kSceneLight = 1u << 2, // nokta isik
  kSceneBody = 1u << 3,  // fizik govdesi (kutu / kure)
};
enum class SceneShape : uint32_t { Box = 0, Sphere = 1 };

struct SceneEntity {
  char name[kSceneNameLen];
  Vec3 pos{0, 0, 0}, rot_deg{0, 0, 0}, scale{1, 1, 1};
  uint32_t components = 0;
  // model
  int32_t asset = -1;
  Vec3 tint{1, 1, 1}; // yazar rengi (sRGB)
  // animasyon
  uint32_t clip = 0;
  float phase = 0, speed = 1;
  // isik
  Vec3 light_color{1, 1, 1};
  float light_intensity = 1, light_radius = 5;
  // govde
  SceneShape shape = SceneShape::Box;
  Vec3 half{0.5f, 0.5f, 0.5f};
  float radius = 0.5f;
  bool dynamic = false;
};
// Veri modeli esitligi: yalniz mevcut bilesenlerin alanlari (dosyaya yazilanlar).
bool scene_entity_equal(const SceneEntity &a, const SceneEntity &b);

// Dunya ayarlari (varlik disi): gunes/ortam isigi, golge hacmi, yazar kamerasi.
// Ayri struct: editorde tek islem olarak gunluge girer (SceneOp::World).
struct SceneWorld {
  Vec3 sun_dir{0.5f, 1.0f, 0.35f};
  Vec3 ambient{0.16f, 0.17f, 0.2f};
  float sun_diffuse = 0.85f;
  Vec3 shadow_center{0, 1.0f, -1.0f};
  float shadow_radius = 17.0f, shadow_depth = 70.0f;
  Vec3 cam_target{0, 1.0f, -3.0f};
  float cam_yaw = 0.7f, cam_pitch = 0.45f, cam_radius = 26.0f;
};
bool scene_world_equal(const SceneWorld &a, const SceneWorld &b); // bit-tam

struct SceneDesc : SceneWorld {
  const SceneWorld &world() const { return *this; }
  void set_world(const SceneWorld &w) { static_cast<SceneWorld &>(*this) = w; }
  char assets[kSceneMaxAssets][kScenePathLen];
  uint32_t asset_count = 0;
  SceneEntity entities[kSceneMaxEntities];
  uint32_t entity_count = 0;

  int32_t add_asset(const char *path); // varsa mevcut indeks; sigmazsa -1
  int32_t find_entity(const char *name) const;
  bool insert_entity(uint32_t at, const SceneEntity &e); // at <= entity_count
  bool remove_entity(uint32_t at);
};

struct SceneError {
  char msg[160];
  uint32_t line = 0; // 0 = satirsiz (dosya acilamadi vb.)
};

// Metin -> SceneDesc. out tam yeniden kurulur. Hata: err (satir numarali), false.
bool scene_parse(const char *text, size_t len, SceneDesc *out, SceneError *err);
// SceneDesc -> metin (NUL sonlu). Donus: gereken uzunluk (NUL haric; snprintf
// gibi, cap asilsa da). Ayni desc her zaman ayni baytlari verir.
size_t scene_write(const SceneDesc &d, char *buf, size_t cap);
bool scene_load(Arena &scratch, const char *path, SceneDesc *out, SceneError *err);
bool scene_save(Arena &scratch, const SceneDesc &d, const char *path, SceneError *err);
// Sahne dosyasinin dizini ("a/b/c.sahne" -> "a/b"); dizin yoksa ".".
void scene_dir_of(const char *path, char *out, size_t cap);

// Varlik donusumu: T * Rz * Ry * Rx * S (ImGuizmo ayristirmasiyla ayni sira;
// kapisi test_editor'da). Donus Euler derece.
Mat4 scene_entity_matrix(const SceneEntity &e);
Quat scene_entity_rotation(const SceneEntity &e);

// Secim: isin–AABB. Yerel sinir = model sinirlari (varsa) ∪ govde ∪ isaret
// kutusu (bos/isik varligi 0.3). Dunya AABB yerel kutunun 8 kosesinden.
struct SceneBounds {
  Vec3 lo, hi;
};
SceneBounds scene_entity_local_bounds(const SceneEntity &e, const SceneBounds *model /* null = model yok */);
SceneBounds scene_world_bounds(const SceneBounds &local, const Mat4 &m);
// Slab testi; t >= 0 en yakin giris (isin icindeyse 0). dir normalize olmali.
bool scene_ray_aabb(Vec3 origin, Vec3 dir, const SceneBounds &b, float *t);
// En yakin vurusun indeksi, yoksa -1. bounds[n] dunya uzayinda.
int32_t scene_pick(const SceneBounds *bounds, uint32_t n, Vec3 origin, Vec3 dir, float *t_out);

// Fizik: govde bilesenli varliklari dunyaya koyar; ids[entity_count] doldurur
// (govdesizler gecersiz). Donus: eklenen govde sayisi.
uint32_t scene_spawn_bodies(const SceneDesc &d, sim::Physics &ph, sim::BodyId *ids);
void scene_remove_bodies(sim::Physics &ph, sim::BodyId *ids, uint32_t n);
// Dinamik govdenin sim'deki yeri: T(sim) * R(sim) * S(yazar).
Mat4 scene_body_matrix(const SceneEntity &e, const sim::Physics &ph, sim::BodyId id);

// Islem gunlugu: her degisiklik once/sonra kopyasiyla kaydedilir. Yeni islem
// yinele kuyrugunu siler; kapasite dolunca en eski dusuruIur.
struct SceneOp {
  enum Kind : uint32_t { Set = 0, Add = 1, Remove = 2, World = 3 };
  Kind kind;
  uint32_t index;
  SceneEntity before, after;
  SceneWorld world_before, world_after; // yalniz World
};
class SceneHistory {
public:
  bool init(Arena &arena, uint32_t capacity);
  // Uygular ve kaydeder. Set: before/after esitse kaydetmez (false).
  bool set_entity(SceneDesc &d, uint32_t i, const SceneEntity &after);
  bool add_entity(SceneDesc &d, const SceneEntity &e); // sona
  bool remove_entity(SceneDesc &d, uint32_t i);
  bool set_world(SceneDesc &d, const SceneWorld &after); // esitse kaydetmez (false)
  bool undo(SceneDesc &d);
  bool redo(SceneDesc &d);
  uint32_t undo_count() const { return cursor_; }
  uint32_t redo_count() const { return count_ - cursor_; }
  void clear() { count_ = cursor_ = 0; }

private:
  bool push(const SceneOp &op);
  SceneOp *ops_ = nullptr;
  uint32_t cap_ = 0, count_ = 0, cursor_ = 0;
};

} // namespace tulpar::engine::content
