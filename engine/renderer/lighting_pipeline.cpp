#include "renderer/lighting_pipeline.hpp"

#include <cmath>

namespace tulpar::engine::renderer {

bool compute_cascade_splits(float near_plane, float far_plane, uint32_t count, float lambda, float *out_splits) {
  if (!out_splits) return false;
  if (count == 0 || count > kMaxShadowCascades) return false;
  if (near_plane <= 0.0f || far_plane <= near_plane) return false;
  if (lambda < 0.0f) lambda = 0.0f;
  if (lambda > 1.0f) lambda = 1.0f;

  const float ratio = far_plane / near_plane;
  for (uint32_t i = 1; i <= count; i++) {
    const float si = (float)i / (float)count;
    const float c_log = near_plane * std::pow(ratio, si);
    const float c_uni = near_plane + (far_plane - near_plane) * si;
    out_splits[i - 1] = lambda * c_log + (1.0f - lambda) * c_uni;
  }
  // Son kademe TAM OLARAK far olmali: pow/carpim yuvarlamasi birkac ULP
  // kaydirabilir, golge hacmi sahnenin son metresini KACIRMAMALI.
  out_splits[count - 1] = far_plane;
  return true;
}

CascadeSphere cascade_bounding_sphere(float fov_y, float aspect, float split_near, float split_far) {
  CascadeSphere s;
  if (split_far <= split_near || fov_y <= 0.0f || aspect <= 0.0f) return s;

  // k: dilim EKSENINDEN kosesine olan mesafenin, eksen boyunca uzakliga
  // ORANI. k^2 = tan^2(fov/2) * (1 + aspect^2)  (yatay+dikey birlikte).
  const float t = std::tan(fov_y * 0.5f);
  const float k2 = t * t * (1.0f + aspect * aspect);
  const float a = split_near, b = split_far;

  // Merkez, iki kosenin de ESIT uzaklikta oldugu nokta: c = (a+b)(1+k^2)/2.
  // Bu deger b'yi ASARSA (genis fov / kisa dilim) kure YALNIZ uzak duzlem
  // koseleriyle belirlenir -- merkez uzak duzlemde, yaricap b*k.
  if (k2 * (a + b) > (b - a)) {
    s.center_view = Vec3{0.0f, 0.0f, -b};
    s.radius = b * std::sqrt(k2);
    return s;
  }

  const float c = (a + b) * (1.0f + k2) * 0.5f;
  const float r2 = ((b - a) * (b - a) + 2.0f * (a * a + b * b) * k2 + (a + b) * (a + b) * k2 * k2) * 0.25f;
  s.center_view = Vec3{0.0f, 0.0f, -c};
  s.radius = std::sqrt(r2);
  return s;
}

} // namespace tulpar::engine::renderer
