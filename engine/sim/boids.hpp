// L4 SIMULATION — Suru Davranisi (Boids).
// DEVAM_PLANI.md Faz 4 (Devrimsel) madde 10'un karsiligi.
//
// O(n^2) probleminden kurtulmak icin dogrudan core/math/spatial_hash.hpp'yi
// (Teschner ve ark. 2003) kullanir. Bellek (Arena) ayrintilarini disari
// birakir; sadece hesaplamaya odaklanir (Veri Yonelimli Tasarim).
#pragma once
#include <cstdint>
#include "core/math/vec.hpp"
#include "core/math/spatial_hash.hpp"

namespace tulpar::engine::sim {

struct BoidsConfig {
  float perception_radius = 2.5f;
  float separation_radius = 1.0f;
  float separation_weight = 1.5f;
  float alignment_weight = 1.0f;
  float cohesion_weight = 1.0f;
  float max_speed = 4.0f;
  float max_force = 10.0f;
};

// Sürü adimi hesaplar ve yeni konumlari/hizlari uretir.
// Girdiler:
//  - positions, velocities: n elemanli (count)
//  - temp_hash: build() cagrilmak uzere hazirlanmis (bellekleri ayrilmis)
//  - neighbor_buffer: gecici sorgu tamponu (ornegin 256 elemanli dizi)
void boids_step(
    Vec3* positions, 
    Vec3* velocities, 
    uint32_t count,
    const BoidsConfig& cfg,
    float dt,
    SpatialHash& temp_hash,
    uint32_t* neighbor_buffer,
    uint32_t max_neighbors
);

} // namespace tulpar::engine::sim
