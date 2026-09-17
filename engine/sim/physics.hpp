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
  // Bir karede tutulacak TEMAS OLAYI sayisi. Halka dolarsa olaylar DUSER ve
  // `contact_overflow()` artar — sessizce kirpilmaz; kirpilma gorunur olmazsa
  // oyun "carpma gelmedi" sanip yanlis mantik kurar.
  uint32_t max_contact_events = 256;
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

// Isin testi sonucu. distance: origin'den carpma noktasina (dir normalize
// edilir). Ses okluzyonu bunu kullanir: dinleyici -> kaynak isini bir govdeye
// carparsa arada engel var demektir (bkz. audio/spatial.hpp katman notu).
struct RayHit {
  BodyId body{};
  float distance = 0;
  Vec3 point{};
  Vec3 normal{};
};

// TEMAS OLAYI. Oyun tarafinin en cok istedigi sey "neye carptim": kopru
// bugune kadar bunu yalnizca SORGUYLA (isin/ortusme) verebiliyordu, cunku
// bugunku FFI callback tasimyor. Cozum callback degil KUYRUK: fizik adiminda
// olusan temaslar sabit boy bir halkaya yazilir, oyun kareyi cizerken okur.
//
// `speed`: temas noktasindaki GORELI hizin normal boyu (m/s). "Sert carpma mi"
// sorusunun cevabi bu; Jolt manifoldu itkiyi vermiyor, ama goreli hiz cozumden
// ONCE dogru buyuklugu tasiyor ve belirlenimli.
struct ContactEvent {
  BodyId a{}, b{};
  Vec3 point{};  // dunya uzayinda temas noktasi
  Vec3 normal{}; // a'dan b'ye bakan yuzey normali
  float speed = 0;
};

// --- Karakter denetleyicisi tipleri (PR #322'den) -------------------------
// Jolt'un `CharacterVirtual`i (third_party/jolt, MIT) — kutuphane zaten
// vendored'di ama baglanmamisti. "Sanal": karakter rijit govde DEGIL, carpisma
// cozumu sweep + kaydir. Rijit govdeli karakter rampalarda kayar, basamaklara
// takilir ve kuvvetle itilince kontrolu kaybeder.
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

  // Isin testi (Jolt NarrowPhaseQuery). Govdeler eklendikten SONRA en az bir
  // step() gerekir (genis faz agaci orada guncellenir), yoksa yeni govde
  // bulunmayabilir. Adimlamayi/durumu DEGISTIRMEZ: salt okunur sorgu, altin
  // ozet etkilenmez. dir sifir uzunlukluysa ya da max_distance <= 0 ise false.
  bool raycast(Vec3 origin, Vec3 dir, float max_distance, RayHit *hit = nullptr) const;

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

  // --- Temas olaylari ---------------------------------------------------
  // `step()` sirasinda DOLDURULUR (Jolt geri cagrimlari is parcaciklarindan
  // gelir; halkaya yazma atomik). Kendiliginden TEMIZLENMEZ: cagiran her kare
  // `clear_contacts()` cagirir, boylece bir karede birden fazla adim atilsa da
  // olaylar birikir ve hicbiri kaybolmaz.
  uint32_t contact_count() const;
  ContactEvent contact(uint32_t i) const;
  uint32_t contact_overflow() const; // halkaya sigmayip DUSEN olay sayisi
  void clear_contacts();

  // Belirlenimlilik olcusu: tum govdelerin konum/donus bitleri (FNV-1a).
  uint64_t state_hash() const;
  PhysicsStats stats() const;

  struct Impl; // .cpp'de; Jolt tipleri orada kalir
private:
  Impl *impl_ = nullptr;
};

} // namespace tulpar::engine::sim
