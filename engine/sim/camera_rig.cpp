#include "sim/camera_rig.hpp"

#include <cmath>

namespace tulpar::engine::sim {

float exponential_smooth(float current, float target, float half_life_s, float dt_s) {
  if (half_life_s <= 0.0f) return target; // sifir/negatif yari-omur -> aninda hedefe
  const float lambda = 0.6931471805599453f / half_life_s; // ln(2)/half_life
  const float decay = std::exp(-lambda * dt_s);
  return target + (current - target) * decay;
}

Vec3 exponential_smooth(Vec3 current, Vec3 target, float half_life_s, float dt_s) {
  return {exponential_smooth(current.x, target.x, half_life_s, dt_s),
          exponential_smooth(current.y, target.y, half_life_s, dt_s),
          exponential_smooth(current.z, target.z, half_life_s, dt_s)};
}

void CameraShake::add_trauma(float amount) {
  trauma += amount;
  if (trauma > 1.0f) trauma = 1.0f;
  if (trauma < 0.0f) trauma = 0.0f;
}

Vec3 CameraShake::update(float dt_s, double time_s, float max_offset) {
  trauma -= decay_per_s * dt_s;
  if (trauma < 0.0f) trauma = 0.0f;
  if (trauma > 1.0f) trauma = 1.0f;
  const float shake = trauma * trauma;
  // Eksen basina FARKLI frekans/faz -- Perlin yerine basit ama deterministik
  // sinus toplami (|sin|<=1 oldugu icin |sonuc|<=max_offset HER ZAMAN saglanir).
  const float t = (float)time_s;
  const float x = std::sin(t * 13.0f) * shake * max_offset;
  const float y = std::sin(t * 17.0f + 1.7f) * shake * max_offset;
  const float z = std::sin(t * 11.0f + 3.1f) * shake * max_offset;
  return {x, y, z};
}

Mat4 FollowCamera::update(Vec3 target, Vec3 offset, Vec3 up, float dt_s) {
  const Vec3 desired = target + offset;
  position = exponential_smooth(position, desired, position_half_life_s, dt_s);
  return Mat4::look_at(position, target, up);
}

} // namespace tulpar::engine::sim
