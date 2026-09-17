// L4 SIMULATION — Fizik: Jolt (vendored) uzerine ince sarmalayici. Jolt
// tipleri disari SIZMAZ (plan A5: soyutlama degil, sinir). Sabit adim,
// belirlenimli (JPH_CROSS_PLATFORM_DETERMINISTIC, FMA kapali). Butun
// kapasiteler init'te (govde, cift, temas, gecici bellek); Jolt'un ayirmalari
// sayilir (custom allocator kancasi) — adim icinde 0 iddiasi test edilir.
#pragma once
#include <cstdint>

#include "core/jobs/job_system.hpp"
#include "core/math/vec.hpp"
#include "core/memory/arena.hpp"

namespace tulpar::engine::sim {

struct PhysicsConfig {
  uint32_t max_bodies = 1024;
  uint32_t max_body_pairs = 1024;
  uint32_t max_contacts = 1024;
  uint32_t temp_bytes = 8u << 20; // Jolt gecici ayirici (adim ici yigin)
  uint32_t threads = 0;           // Jolt thread havuzu icin (jobs == nullptr ise): 0 = donanim-1
  JobSystem *jobs = nullptr;      // verilirse Jolt job'lari BIZIM fiber job sisteminde kosar
  uint32_t max_jolt_jobs = 1024;  // Jolt Job havuzu (jobs != nullptr)
  Vec3 gravity = {0, -9.81f, 0};
  uint32_t max_characters = 8; // kinematik karakter slotu (bkz. CharacterConfig)
};

struct BodyId {
  uint32_t v = 0xFFFFFFFFu;
  bool valid() const { return v != 0xFFFFFFFFu; }
};

// --- Kinematik karakter denetleyicisi ---------------------------------
// **SIFIRDAN YAZILMADI.** Jolt'un `CharacterVirtual`i (third_party/jolt,
// MIT) kullaniliyor -- kutuphane ZATEN vendored'di ama BAGLANMAMISTI.
// "Sanal" olmasinin anlami: karakter bir rijit govde DEGIL; carpisma
// cozumu sweep + kaydir (collide-and-slide) ile yapilir. Oyun karakteri
// icin dogru olan budur -- rijit govdeli bir karakter rampalarda kayar,
// basamaklara takilir ve kuvvetle itildiginde kontrolu kaybeder.
//
// Jolt tipleri BU BASLIGA SIZMAZ (physics.hpp'nin bastaki kurali).
// Karakter API'si ayri bir dosyaya degil `Physics`e eklendi: carpisma
// katmani filtreleri (BPLayerInterface/ObjectPairFilter) physics.cpp'nin
// anonim ad alaninda yasiyor; ayri bir cevirim birimi onlari GOREMEZ.

struct CharacterId {
  uint32_t v = 0xFFFFFFFFu;
  bool valid() const { return v != 0xFFFFFFFFu; }
};

enum class GroundState : uint8_t {
  OnGround,      // zeminde, serbest hareket
  OnSteepGround, // cok dik yamac: tirmanamaz, kaymasi beklenir
  NotSupported,  // bir seye degiyor ama tasinmiyor -> dusmeli
  InAir,         // havada
};

struct CharacterConfig {
  float radius = 0.3f;
  // TOPLAM boy: ayak tabanindan tepeye. Kapsul yarim-silindiri buradan
  // cikarilir, bu yuzden `height` > 2*radius OLMALIDIR; degilse
  // add_character gecersiz kimlik doner (Jolt'un assert'ine dusmek yerine).
  float height = 1.8f;
  float max_slope_deg = 50.0f;
  float mass = 70.0f;
  // Cikilabilecek basamak yuksekligi (ExtendedUpdate'in merdiven yurumesi).
  float step_up = 0.4f;
  float jump_speed = 4.0f;
  Vec3 position = {0, 0, 0};
};

struct PhysicsStats {
  uint32_t bodies = 0;
  uint64_t allocs_total = 0;    // Jolt allocator kancasindan
  uint64_t allocs_last_step = 0;
  uint64_t frees_total = 0;
};

class Physics {
public:
  bool init(Arena &arena, const PhysicsConfig &cfg);
  void shutdown();
  bool ok() const { return impl_ != nullptr; }

  BodyId add_box(Vec3 half_extent, Vec3 pos, Quat rot, bool dynamic);
  BodyId add_sphere(float radius, Vec3 pos, bool dynamic);
  void remove(BodyId id);

  void step(float dt, int collision_steps = 1);

  // --- Geometri Sorgulari (S7) ---------------------------------------
  // Belirtilen yonde (normalize edilmis olmali) isin gonderir.
  // Geriye en yakin carpisma doner (hit == true ise gecerlidir).
  struct RaycastResult {
    bool hit = false;
    BodyId body_id;
    Vec3 point;
    Vec3 normal;
    float fraction = 1.0f; // 0..1 arasinda isin uzerindeki mesafe carpanı
  };
  RaycastResult raycast(Vec3 origin, Vec3 direction, float max_distance) const;

  struct OverlapResult {
    bool hit = false;
    BodyId body_id;
  };
  
  // Belirtilen sekille kesisen (overlap) butun govdeleri bulur (Trigger Volumes S1).
  // max_results kadar OverlapResult dizisine yazar, gercek carpisan sayisini doner.
  uint32_t overlap_box(Vec3 center, Vec3 half_extent, Quat rot, OverlapResult* results, uint32_t max_results) const;
  uint32_t overlap_sphere(Vec3 center, float radius, OverlapResult* results, uint32_t max_results) const;

  // --- Karakter ------------------------------------------------------
  // Gecersiz yapilandirmada (height <= 2*radius, radius <= 0, havuz dolu)
  // GECERSIZ kimlik doner -- sessizce bozuk bir karakter YARATMAZ.
  CharacterId add_character(const CharacterConfig &cfg);
  void remove_character(CharacterId id);

  // Kare basina girdi; step() icinde uygulanir.
  // `desired_horizontal_velocity`in YUKARI bileseni YOK SAYILIR: dikey hiz
  // yercekimi ve ziplamaya aittir, girdiye degil (aksi halde oyuncu
  // havada surekli yukari "yuruyebilirdi").
  // `jump` KENAR-TETIKLI: uygulandigi kare tuketilir, basili tutmak
  // zincirleme ziplama yapmaz.
  void set_character_input(CharacterId id, Vec3 desired_horizontal_velocity, bool jump);

  Vec3 character_position(CharacterId id) const;
  Vec3 character_velocity(CharacterId id) const;
  GroundState character_ground_state(CharacterId id) const;
  bool character_grounded(CharacterId id) const;
  // Karakterin bastigi zeminin normali (havadayken yukari yonu).
  Vec3 character_ground_normal(CharacterId id) const;

  Vec3 position(BodyId id) const;
  Quat rotation(BodyId id) const;
  Vec3 linear_velocity(BodyId id) const;
  void set_linear_velocity(BodyId id, Vec3 v);
  bool is_active(BodyId id) const;

  // Belirlenimlilik olcusu: tum govdelerin konum/donus bitleri (FNV-1a).
  uint64_t state_hash() const;
  PhysicsStats stats() const;

  struct Impl; // .cpp'de; Jolt tipleri orada kalir
private:
  Impl *impl_ = nullptr;
};

} // namespace tulpar::engine::sim
