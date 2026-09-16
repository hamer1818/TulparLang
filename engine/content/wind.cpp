#include "content/wind.hpp"

#include "core/math/noise.hpp"

namespace tulpar::engine::content {

Vec2 sample_wind(const WindField &w, float world_x, float world_z, float t) {
  // Ruzgarin KENDI yonunde ilerleyen bir gurultu deseni orneklenir (dunya
  // konumu ruzgar yonu boyunca t*strength kadar KAYDIRILARAK) -- desenin
  // KENDISI ruzgarla birlikte "akar" (sabit noktada zamanla titreyen
  // gurultu yerine, esintinin tasindigi izlenimi verir).
  const float sample_x = world_x - w.direction.x * t * w.strength;
  const float sample_z = world_z - w.direction.y * t * w.strength;
  const float n = tulpar::engine::fbm_2d(sample_x * w.gust_frequency, sample_z * w.gust_frequency, w.seed, 3);
  const float gust = 1.0f + w.gustiness * (n * 2.0f - 1.0f); // n:[0,1) -> gust:[1-gustiness, 1+gustiness)
  const float mag = w.strength * gust;
  return {w.direction.x * mag, w.direction.y * mag};
}

} // namespace tulpar::engine::content
