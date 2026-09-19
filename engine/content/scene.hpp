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
// Agac derinligi TAVANI (kok = 0). Tavan OLMAK ZORUNDA: ebeveyn zinciri veri
// dosyasindan gelir, yani dusmanca/bozuk girdi olabilir; ozyineleme yok, her
// yurume bu sayida adimda durur. Asilmasi sessiz kirpma DEGIL, hatadir.
constexpr uint32_t kSceneMaxDepth = 16;

enum SceneComponentBits : uint32_t {
  kSceneModel  = 1u << 0, // glTF model (kaynak indeksi + renk)
  kSceneAnim   = 1u << 1, // model klibi (iskeletli)
  kSceneLight  = 1u << 2, // nokta isik
  kSceneBody   = 1u << 3, // fizik govdesi (kutu / kure)
  kSceneCamera = 1u << 4, // kamera (fov, yakin, uzak)
  kSceneAudio  = 1u << 5, // ses kaynagi (klip, ses, perde, dongu, uzamsal)
  kSceneScript = 1u << 6, // tulpar betik bileseni (.tpr)
  kSceneCharacter = 1u << 7, // karakter kontrolcusu (kapsul)
  kSceneParticle = 1u << 8, // partikul yayici (VFX)
  kSceneTerrain  = 1u << 9, // yukseklik haritasi (arazi)
  kSceneVoxel    = 1u << 10, // voxel grid (greedy mesh)
  kSceneWater    = 1u << 11, // okyanus/su (gerstner)
  kSceneWind     = 1u << 12, // ruzgar alani
  kSceneNavAgent = 1u << 13, // yapay zeka ajani (navmesh)
  kSceneJoint    = 1u << 14, // fizik eklemi (hinge, vb.)
  kSceneSkybox   = 1u << 15, // PBR gokyuzu kutusu
  kSceneRefProbe = 1u << 16, // Yansima sondasi (IBL)
  kSceneReverb   = 1u << 17, // Ses yanki alani
};
enum class SceneShape : uint32_t { Box = 0, Sphere = 1 };
// Nokta: kSceneLight'in eskiden BILDIGI tek tur (yaricapli, konum onemli).
// Yonlu: entity-bazli yon gostergesi (gizmo gunes-oku cizer) -- Dunya panelindeki
// TEK global gunes'ten AYRI, henuz runtime'da gercek ikinci bir yonlu terim
// SHADE ETMEZ (bkz. PLAN takip notu); bugun editoryel/gorsel bir ayrimdir.
enum class SceneLightType : uint32_t { Point = 0, Directional = 1 };
// Varlik bayraklari — EDITOR gorunumu, oyun icerigi DEGIL: `.sahneb` derleyicisi
// bunlara bakmaz (gizli bir varlik yine de blob'a girer), yalniz editor panelleri
// okur. Sifir varsayilan ve dosyaya YAZILMAZ; boylece bayraksiz sahnelerin metni
// Faz E2 oncesiyle bayt bayt aynidir.
enum SceneEntityFlags : uint32_t {
  kSceneHidden = 1u << 0, // editorde gizli (cizilmez, secilemez)
  kSceneLocked = 1u << 1, // kilitli (gizmo/surukleme degistiremez)
};

struct SceneEntity {
  char name[kSceneNameLen];
  Vec3 pos{0, 0, 0}, rot_deg{0, 0, 0}, scale{1, 1, 1}; // YEREL (ebeveyne gore)
  // Sahne agaci: ebeveynin varlik DIZINI, -1 = kok. Dizin ileriyi de
  // gosterebilir (ebeveyn-once siralama SART DEGIL); tutarliligi
  // scene_tree_validate saglar. Varlik silinince/eklenince kaydirilir
  // (SceneDesc::remove_entity / insert_entity).
  int32_t parent = -1;
  uint32_t flags = 0; // SceneEntityFlags
  uint32_t components = 0;
  // model
  int32_t asset = -1;
  int32_t primitive = -1;
  Vec3 tint{1, 1, 1}; // yazar rengi (sRGB)
  // Varsayilanlar renderer::PbrParams ile AYNI olmali: glTF'te varsayilan
  // puruzluluk 1.0'dir ve motor da oyle kabul eder. Sahne 0.5 verirse ayni
  // nesne kaynagina gore (glTF mi ilkel mi) FARKLI parlaklikta gorunur.
  float metallic = 0.0f, roughness = 1.0f, reflectance = 0.5f;
  Vec3 emissive{0, 0, 0};
  float emissive_strength = 1.0f;
  // animasyon
  uint32_t clip = 0;
  float phase = 0, speed = 1;
  // isik
  Vec3 light_color{1, 1, 1};
  float light_intensity = 1, light_radius = 5;
  SceneLightType light_type = SceneLightType::Point;
  // govde
  SceneShape shape = SceneShape::Box;
  Vec3 half{0.5f, 0.5f, 0.5f};
  float radius = 0.5f;
  bool dynamic = false;
  // kamera
  float cam_fov = 60.0f;
  float cam_near = 0.1f, cam_far = 200.0f;
  // ses
  char audio_clip[kSceneNameLen] = {0};
  float audio_volume = 1.0f, audio_pitch = 1.0f;
  bool audio_loop = false, audio_spatial = true;
  // betik
  char script_file[kSceneNameLen] = {0};
  bool script_enabled = true;
  // karakter
  float char_radius = 0.5f, char_height = 1.0f;
  float char_mass = 70.0f, char_max_slope = 45.0f;
  // partikul
  float particle_spawn_rate = 10.0f; // saniyede partikul
  float particle_lifetime_min = 1.0f, particle_lifetime_max = 2.0f;
  float particle_size_start = 0.2f, particle_size_end = 0.0f;
  Vec3 particle_velocity{0, 2.0f, 0};
  Vec3 particle_jitter{1.0f, 0.5f, 1.0f};
  // yapay zeka (navagent)
  Vec3 ai_target{0, 0, 0};
  float ai_speed = 3.0f;
  float ai_turn_speed = 120.0f;
  // fizik eklemi (joint)
  int32_t joint_target = -1; // baglanilan diger varligin indeksi
  Vec3 joint_axis{0, 1, 0}; // donus veya hareket ekseni (yerel)
  float joint_limit_min = -45.0f, joint_limit_max = 45.0f;
  float joint_motor_speed = 0.0f; // >0 ise motor aktif
  // arazi (terrain)
  float terrain_width = 64.0f, terrain_height = 64.0f;
  float terrain_cell = 1.0f, terrain_amp = 20.0f, terrain_freq = 0.02f;
  int32_t terrain_octaves = 5;
  uint32_t terrain_seed = 0;
  // IBL & Yansima (Reflection Probe)
  float ref_probe_radius = 10.0f;
  float ref_probe_intensity = 1.0f;
  // Yanki Alani (Reverb Zone)
  // Gokyuzu: panelde sabit bir dugme vardi, arkasinda ALAN DA yoktu --
  // bileseni eklemek ve ayarlamak hicbir sey kaydetmiyordu.
  char skybox_asset[kScenePathLen] = {0}; // HDRI / kup haritasi dosyasi

  float reverb_decay = 1.5f; // saniye cinsinden yanki sonumlenme suresi
  float reverb_room_size = 0.8f;
  // su (water/gerstner wave)
  float wave_length = 10.0f, wave_amplitude = 0.5f;
  float wave_steepness = 0.3f, wave_speed = 1.0f;
  Vec2 wave_direction{1.0f, 0.0f};
  // ruzgar (wind)
  Vec2 wind_direction{1.0f, 0.0f};
  float wind_strength = 1.0f, wind_gustiness = 0.5f;
  float wind_gust_freq = 0.3f;
  uint32_t wind_seed = 0;
  // voksel (voxel)
  uint32_t voxel_size_x = 16, voxel_size_y = 16, voxel_size_z = 16;
  float voxel_cell = 1.0f;
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
  // Ekleme. MEVCUT varliklarin `parent >= at` olanlari +1 kaydirilir; `e.parent`
  // ise EKLEMEDEN SONRAKI indeks uzayinda yorumlanir (kaydirilmaz) — Remove
  // isleminin geri alinmasi tam da bunu ister.
  // SONA ekleme (at == entity_count) hicbir seyi kaydirmaz: ayristirma sirasinda
  // henuz olusmamis bir varliga bakan ILERI ebeveyn referanslari bozulmasin.
  bool insert_entity(uint32_t at, const SceneEntity &e); // at <= entity_count
  // Silme, AGACI TUTARLI birakir (secim burada; bkz. scene.cpp):
  //   1. silinen dugumun cocuklari BUYUKBABAYA baglanir (silinen kok ise kok
  //      olurlar) — alt agac SESSIZCE yok olmaz, kullanici gordugu varliklari
  //      kaybetmez; alt agaci da silmek isteyen cagiran once onlari siler.
  //   2. `parent > at` olan her ebeveyn -1 kaydirilir (diziler sikisti).
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
// kapisi test_editor'da). Donus Euler derece. Bu YEREL donusumdur — ebeveyn
// zinciri KATILMAZ (dunya icin scene_entity_world_matrix).
Mat4 scene_entity_matrix(const SceneEntity &e);
Quat scene_entity_rotation(const SceneEntity &e);

// --- Sahne agaci (Faz E2) ----------------------------------------------------
// Agac gecerli mi: her ebeveyn indeksi sinir icinde, kendine bakan yok, dongu
// yok, derinlik <= kSceneMaxDepth. Bozuksa false ve *bad_index = ilk bozuk
// varlik. Ayirma yok, ozyineleme yok (her yurume tavanda durur).
bool scene_tree_validate(const SceneDesc &d, uint32_t *bad_index);
// Varligin kok'e uzakligi (kok = 0). Bozuk/derin zincir: kSceneMaxDepth doner
// (tavanda durur, asla donguye girmez).
uint32_t scene_tree_depth(const SceneDesc &d, uint32_t i);
// DUNYA donusumu: kok'ten asagi `M_kok * ... * M_i`. Her varligin kendi sirasi
// T*Rz*Ry*Rx*S olarak kalir. Kok varlikta sonuc scene_entity_matrix ile
// BIT-TAMDIR (erken donus) — duzlestirilmis sahne ile karsilastirma kapisi
// buna dayanir.
Mat4 scene_entity_world_matrix(const SceneDesc &d, uint32_t i);
// Dunya donusu: zincirdeki kuaterniyonlarin carpimi. Kok: scene_entity_rotation
// ile bit-tam.
Quat scene_entity_world_rotation(const SceneDesc &d, uint32_t i);
// Dunya olcegi: zincirdeki olceklerin bilesen carpimi. ⚠ SINIR: ebeveynde hem
// donus hem esit-olmayan olcek varsa gercek dunya donusumu EGIKTIR (shear) ve
// tek bir olcek vektorune sigmaz; bu durumda deger bir YAKLASIMDIR. Cizim
// matrisi (scene_entity_world_matrix) her durumda tamdir; yaklasim yalniz
// rijit govde / GI gibi T-R-S isteyen tuketicileri ilgilendirir.
Vec3 scene_entity_world_scale(const SceneDesc &d, uint32_t i);
// Belirlenimli on-sirali gezinti: kokler indeks sirasinda, her dugumun
// cocuklari indeks sirasinda. Donus: dugum sayisi (cap asilsa da dogru sayar,
// yalniz ilk cap tanesi yazilir). Panelin cizdigi sira budur.
uint32_t scene_tree_order(const SceneDesc &d, int32_t *out, uint32_t cap);
// Yeniden ebeveynleme: `child`'in DUNYA donusumu KORUNUR — yeni yerel
// pos/rot_deg/scale, `inverse(dunya(new_parent)) * dunya(child)` matrisinin
// T*Rz*Ry*Rx*S ayristirmasidir. Reddeder (ve HICBIR SEYI degistirmez):
// sinir disi indeks, kendine ebeveyn, dongu (yeni ebeveyn cocugun altindaysa),
// tavan asimi. ⚠ AYRISTIRMA SINIRI: ayna (negatif determinant) tek eksene — X —
// yuklenir; egik (shear) bir matris T*R*S ile temsil edilemez, o durumda dunya
// donusumu TAM korunmaz (ebeveynde donus + esit olmayan olcek birlikteyse).
bool scene_reparent(SceneDesc &d, uint32_t child, int32_t new_parent);
// Ayni hesap, UYGULAMADAN: sonucu *out'a yazar (gunluge tek islem olarak
// girmek icin; bkz. SceneHistory::reparent).
bool scene_reparent_entity(const SceneDesc &d, uint32_t child, int32_t new_parent, SceneEntity *out);
// Gizmo DUNYA uzayinda calisir (ImGuizmo'ya dunya matrisi verilir); sonucu
// varligin YEREL alanlarina yazmadan once bundan gecirmek ZORUNLU, yoksa
// cocuk varlik ebeveyn donusumunu iki kez yer. Kok varlikta `world` aynen doner.
Mat4 scene_world_to_local_matrix(const SceneDesc &d, uint32_t i, const Mat4 &world);

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
// (govdesizler gecersiz). Donus: eklenen govde sayisi. Govde DUNYA donusumuyle
// kurulur (konum/donus/olcek zincirden) — cocuk govde gorundugu yerde dogar,
// yerel ofsetinde degil.
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
  // Yalniz Remove: silinen dugumun cocuklarinin SILINMEDEN ONCEKI indeksleri.
  // Silme onlari buyukbabaya bagladigi icin sonradan bulunamazlar (gercek
  // buyukbaba cocuklariyla karisirlar) — geri alma bit-tam olsun diye 32 bayt
  // bit kumesi olarak saklanir (dizi kopyasi degil).
  uint32_t child_mask[(kSceneMaxEntities + 31) / 32];
};
class SceneHistory {
public:
  bool init(Arena &arena, uint32_t capacity);
  // Uygular ve kaydeder. Set: before/after esitse kaydetmez (false).
  bool set_entity(SceneDesc &d, uint32_t i, const SceneEntity &after);
  bool add_entity(SceneDesc &d, const SceneEntity &e); // sona
  bool remove_entity(SceneDesc &d, uint32_t i);
  // Yeniden ebeveynleme TEK islemdir: scene_reparent_entity yalniz `child`
  // varliginin alanlarini (parent + yerel donusum) degistirdigi icin mevcut
  // Set islemine oturur — geri alma bayt-tamdir, yeni bir islem turu yok.
  // Donus: gunluge islem girdi mi (ayni ebeveyn / gecersiz istek: false).
  bool reparent(SceneDesc &d, uint32_t child, int32_t new_parent);
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
