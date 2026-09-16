// audio::spatial_compute: saf matematik, cihaz/ses kartii gerekmez. Bilinen
// acilar/mesafeler icin bilinen sonuclar + esit-guc (left^2+right^2==1)
// degismezi.
#include <cmath>

#include "audio/spatial.hpp"
#include "tests/test.hpp"

using namespace tulpar::engine;
using namespace tulpar::engine::test;

namespace {
bool approx(float a, float b, float eps = 1e-3f) { return std::fabs(a - b) < eps; }
} // namespace

ENGINE_TEST(spatial_pan_centers_when_source_ahead) {
  audio::SpatialListener l;
  l.pos = {0, 0, 0};
  l.right = {1, 0, 0};
  audio::SpatialGains g = audio::spatial_compute(l, Vec3{0, 0, -5}, 0.0f); // -Z: sagdan bagimsiz, tam onde
  CHECK(approx(g.left, g.right));
  CHECK(approx(g.left * g.left + g.right * g.right, 1.0f)); // esit-guc degismezi
}

ENGINE_TEST(spatial_pan_full_right_and_left) {
  audio::SpatialListener l;
  l.pos = {0, 0, 0};
  l.right = {1, 0, 0};
  audio::SpatialGains r = audio::spatial_compute(l, Vec3{5, 0, 0}, 0.0f); // tam sag
  CHECK(approx(r.right, 1.0f) && approx(r.left, 0.0f));
  audio::SpatialGains lft = audio::spatial_compute(l, Vec3{-5, 0, 0}, 0.0f); // tam sol
  CHECK(approx(lft.left, 1.0f) && approx(lft.right, 0.0f));
}

ENGINE_TEST(spatial_distance_attenuation_inverse_square) {
  audio::SpatialListener l;
  l.pos = {0, 0, 0};
  l.right = {1, 0, 0};
  audio::SpatialGains at_min = audio::spatial_compute(l, Vec3{1, 0, 0}, 0.0f, 1.0f, 100.0f);
  CHECK(approx(at_min.distance_atten, 1.0f)); // min_distance'da tam kazanc
  audio::SpatialGains at_2x = audio::spatial_compute(l, Vec3{2, 0, 0}, 0.0f, 1.0f, 100.0f);
  CHECK(approx(at_2x.distance_atten, 0.25f)); // ters-kare: (1/2)^2
  audio::SpatialGains beyond = audio::spatial_compute(l, Vec3{200, 0, 0}, 0.0f, 1.0f, 100.0f);
  CHECK(beyond.distance_atten == 0.0f); // max_distance otesi: sert kesim
  audio::SpatialGains closer_than_min = audio::spatial_compute(l, Vec3{0.1f, 0, 0}, 0.0f, 1.0f, 100.0f);
  CHECK(approx(closer_than_min.distance_atten, 1.0f)); // min_distance alti kilitli, >1 OLMAZ
}

ENGINE_TEST(spatial_occlusion_maps_to_lowpass_alpha) {
  audio::SpatialListener l;
  l.pos = {0, 0, 0};
  l.right = {1, 0, 0};
  CHECK(approx(audio::spatial_compute(l, Vec3{0, 0, -5}, 0.0f).lowpass_alpha, 1.0f));  // engelsiz
  CHECK(approx(audio::spatial_compute(l, Vec3{0, 0, -5}, 0.5f).lowpass_alpha, 0.5f));  // yari engelli
  CHECK(approx(audio::spatial_compute(l, Vec3{0, 0, -5}, 1.0f).lowpass_alpha, 0.1f));  // tam engelli: 0 DEGIL, taban 0.1
}

ENGINE_TEST(spatial_source_at_listener_does_not_crash_or_nan) {
  // Negatif kontrol: dist==0 -> normalize() ic yordami sifir vektor donmeli,
  // NaN/Inf olmamali.
  audio::SpatialListener l;
  l.pos = {3, 3, 3};
  l.right = {1, 0, 0};
  audio::SpatialGains g = audio::spatial_compute(l, Vec3{3, 3, 3}, 0.0f);
  CHECK(approx(g.distance_atten, 1.0f));
  CHECK(g.left == g.left && g.right == g.right); // NaN != NaN kontrolu
}
