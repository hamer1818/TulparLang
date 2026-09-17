#include "content/water_wave.hpp"

#include <cmath>

namespace tulpar::engine::content {

Vec3 gerstner_displacement(const GerstnerWave &w, float x0, float z0, float t) {
  const float k = 2.0f * tulpar::engine::kPi / w.wavelength; // dalga sayisi
  const float phase = w.speed * k;
  const float theta = k * (w.direction.x * x0 + w.direction.y * z0) + phase * t;
  const float c = std::cos(theta);
  const float s = std::sin(theta);
  const float qa = w.steepness * w.amplitude;
  return {qa * w.direction.x * c, w.amplitude * s, qa * w.direction.y * c};
}

Vec3 sum_gerstner_displacement(const GerstnerWave *waves, uint32_t count, float x0, float z0, float t) {
  Vec3 sum{0, 0, 0};
  for (uint32_t i = 0; i < count; i++) sum = sum + gerstner_displacement(waves[i], x0, z0, t);
  return sum;
}

} // namespace tulpar::engine::content
