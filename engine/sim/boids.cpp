#include "sim/boids.hpp"

#include <cmath>

namespace tulpar::engine::sim {

namespace {
Vec3 limit(Vec3 v, float max) {
  float sq = length_sq(v);
  if (sq > max * max && sq > 0.00001f) {
    return v * (max / std::sqrt(sq));
  }
  return v;
}
} // namespace

void boids_step(
    Vec3* positions, 
    Vec3* velocities, 
    uint32_t count,
    const BoidsConfig& cfg,
    float dt,
    SpatialHash& temp_hash,
    uint32_t* neighbor_buffer,
    uint32_t max_neighbors
) {
  if (count == 0 || !positions || !velocities || !neighbor_buffer) return;

  // 1. Hash gridini insa et
  temp_hash.cell_size = cfg.perception_radius;
  if (!spatial_hash_build(temp_hash, positions, count)) return;

  const float sep_sq = cfg.separation_radius * cfg.separation_radius;

  // Yeni hizlari hesapla
  for (uint32_t i = 0; i < count; i++) {
    const Vec3 pos = positions[i];
    const Vec3 vel = velocities[i];
    
    Vec3 sep_force{0, 0, 0};
    Vec3 ali_force{0, 0, 0};
    Vec3 coh_force{0, 0, 0};
    int sep_count = 0;
    int ali_coh_count = 0;

    // Uzamsal hash sorgusu
    uint32_t n_count = spatial_hash_query(temp_hash, positions, pos, cfg.perception_radius, neighbor_buffer, max_neighbors);
    if (n_count > max_neighbors) n_count = max_neighbors;

    for (uint32_t j = 0; j < n_count; j++) {
      uint32_t other_idx = neighbor_buffer[j];
      if (other_idx == i) continue; // Kendisi haric

      const Vec3 other_pos = positions[other_idx];
      const Vec3 other_vel = velocities[other_idx];
      Vec3 diff = pos - other_pos;
      float d_sq = length_sq(diff);

      if (d_sq > 0.00001f) {
        if (d_sq < sep_sq) {
          float d = std::sqrt(d_sq);
          sep_force += (diff * (1.0f / d)) * (1.0f / d); 
          sep_count++;
        }
        ali_force += other_vel;
        coh_force += other_pos;
        ali_coh_count++;
      }
    }

    Vec3 total_force{0, 0, 0};
    
    if (sep_count > 0) {
      sep_force = sep_force * (1.0f / (float)sep_count);
      sep_force = normalize(sep_force) * cfg.max_speed - vel;
      total_force += limit(sep_force, cfg.max_force) * cfg.separation_weight;
    }
    
    if (ali_coh_count > 0) {
      ali_force = ali_force * (1.0f / (float)ali_coh_count);
      ali_force = normalize(ali_force) * cfg.max_speed - vel;
      total_force += limit(ali_force, cfg.max_force) * cfg.alignment_weight;
      
      coh_force = coh_force * (1.0f / (float)ali_coh_count);
      Vec3 desired = coh_force - pos;
      desired = normalize(desired) * cfg.max_speed;
      Vec3 steer = desired - vel;
      total_force += limit(steer, cfg.max_force) * cfg.cohesion_weight;
    }

    // Yeni hizi kaydet
    velocities[i] = limit(vel + total_force * dt, cfg.max_speed);
  }
  
  // Konumlari guncelle (Sonradan tek seferde yapilir ki eski konumlar sorguyu bozmasin)
  for (uint32_t i = 0; i < count; i++) {
    positions[i] += velocities[i] * dt;
  }
}

} // namespace tulpar::engine::sim
