// L6 CONTENT — CPU tarafli, sabit-kapasiteli parcacik (particle) sistemi.
// 500 madde listesinden BAGIMSIZ bir "gorsel" eklenti (kullanicinin acikca
// istedigi yon: render/materyal/parcacik gibi GORSEL katmanlar).
//
// **Rakiplerden farki (devrimsel degil, ama GERCEK ve OLCULEBILIR bir
// avantaj):** dogum-ani rastgelelik (hiz jitter'i, omur) core/math/random.hpp
// icindeki DETERMINISTIK Rng (xorshift32) ile uretilir -- gercek-zaman
// tohumlu (ör. std::random_device, sistem saati) TIPIK parcacik
// sistemlerinin aksine, AYNI tohum + AYNI emit() sirasi HER PLATFORMDA
// (x86_64/ARM) BIT-ES AYNI parcacik dizisini uretir. Bu, sim/replay.hpp
// (deterministik kayit/oynatma) ve sim/rollback.hpp (rollback netcode) ile
// parcacik efektlerinin de (patlama, iz, kivilcim) EK KOD OLMADAN uyumlu
// calismasi anlamina gelir -- coğu motorda VFX rollback/replay disinda
// tutulur (gorsel sacmalamasin diye elle senkronize edilir), Tulpar'da
// BEDAVA gelir.
//
// Bellek: init() SONRASI ayirma YOK (A2 "0 tahsis" kapisiyla ayni cizgi).
// Olu parcacik temizligi TAKAS-ILE-SILME (swap-with-last) O(1): dizi
// HER ZAMAN yogun (dense) tutulur, index bosluklari OLMAZ.
#pragma once
#include <cstdint>

#include "core/math/random.hpp"
#include "core/math/vec.hpp"
#include "core/memory/arena.hpp"

namespace tulpar::engine::content {

struct Particle {
  Vec3 pos{};
  Vec3 vel{};
  float age = 0.0f;
  float lifetime = 1.0f;
  float size_start = 1.0f;
  float size_end = 1.0f;
  float size = 1.0f; // guncel boyut -- update() tarafindan omur-oranindan hesaplanir
};

struct ParticleEmitterConfig {
  Vec3 spawn_pos{};
  Vec3 base_velocity{};
  Vec3 velocity_jitter{}; // her eksen [-jitter, +jitter) araliginda BAGIMSIZ ekleme
  float lifetime_min = 1.0f;
  float lifetime_max = 1.0f; // == lifetime_min ise omur SABIT (rastgelelik yok)
  float size_start = 1.0f;
  float size_end = 1.0f;
};

class ParticleSystem {
 public:
  bool init(Arena &arena, uint32_t max_particles, Vec3 gravity) {
    particles_ = arena.alloc_array<Particle>(max_particles);
    capacity_ = max_particles;
    count_ = 0;
    gravity_ = gravity;
    return particles_ != nullptr;
  }

  // count kadar YENI parcacik dogurmaya CALISIR; havuzda yer kalmazsa
  // SESSIZCE KESILMEZ -- GERCEKTEN dogan sayi doner (cagiran kontrol eder,
  // core/jobs/job_graph.hpp / sim/replay.hpp'deki AYNI "sessiz basarisizlik
  // YOK" disiplini).
  uint32_t emit(const ParticleEmitterConfig &cfg, uint32_t count, Rng &rng) {
    uint32_t spawned = 0;
    for (uint32_t i = 0; i < count; i++) {
      if (count_ >= capacity_) break;
      Particle &p = particles_[count_++];
      p.pos = cfg.spawn_pos;
      const Vec3 jitter{
          (rng.next_float() * 2.0f - 1.0f) * cfg.velocity_jitter.x,
          (rng.next_float() * 2.0f - 1.0f) * cfg.velocity_jitter.y,
          (rng.next_float() * 2.0f - 1.0f) * cfg.velocity_jitter.z,
      };
      p.vel = cfg.base_velocity + jitter;
      p.age = 0.0f;
      p.lifetime = cfg.lifetime_min + rng.next_float() * (cfg.lifetime_max - cfg.lifetime_min);
      p.size_start = cfg.size_start;
      p.size_end = cfg.size_end;
      p.size = cfg.size_start;
      spawned++;
    }
    return spawned;
  }

  // Yari-kapali (semi-implicit/symplectic) Euler: once hiz, SONRA o YENI
  // hizla konum guncellenir -- sim/camera_rig.hpp disindaki tum fizik
  // entegrasyonuyla AYNI sira. Olen parcaciklar TAKAS-ILE-SILME ile
  // cikarilir; YERINE gelen (eski son eleman) AYNI karede islenir (bir kare
  // "donmus" parcacik gorunmesin diye BILINCLI tercih).
  void update(float dt) {
    uint32_t i = 0;
    while (i < count_) {
      Particle &p = particles_[i];
      p.vel += gravity_ * dt;
      p.pos += p.vel * dt;
      p.age += dt;
      if (p.age >= p.lifetime) {
        particles_[i] = particles_[count_ - 1];
        count_--;
        continue; // i SABIT -- yeni takas edilen eleman bu karede TEKRAR islenir
      }
      const float t = p.age / p.lifetime; // (0,1]
      p.size = p.size_start + (p.size_end - p.size_start) * t;
      i++;
    }
  }

  uint32_t alive_count() const { return count_; }
  const Particle &particle(uint32_t i) const { return particles_[i]; }
  // Renk-uzerinden-omur gibi CAGIRANA ozel gradyanlar icin (bu sinif
  // renk/materyal bilmez -- tek sorumluluk).
  float life_fraction(uint32_t i) const { return particles_[i].age / particles_[i].lifetime; }

 private:
  Particle *particles_ = nullptr;
  uint32_t capacity_ = 0;
  uint32_t count_ = 0;
  Vec3 gravity_{0.0f, -9.8f, 0.0f};
};

} // namespace tulpar::engine::content
