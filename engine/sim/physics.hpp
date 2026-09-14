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
};

struct BodyId {
  uint32_t v = 0xFFFFFFFFu;
  bool valid() const { return v != 0xFFFFFFFFu; }
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
