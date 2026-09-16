// renderer/lighting_pipeline.hpp: PSSM bolme formulunun ve kademe
// sinirlayici kuresinin ELLE hesaplanmis degerlerle dogrulanmasi.
#include "renderer/lighting_pipeline.hpp"

#include <cmath>

#include "core/math/vec.hpp"
#include "tests/test.hpp"

using namespace tulpar::engine;
using namespace tulpar::engine::renderer;

ENGINE_TEST(cascade_splits_uniform_and_logarithmic_extremes) {
  float s[4];

  // lambda=0 -> SAF DUZGUN: near + (far-near)*i/N. near=1, far=100, N=4:
  // 1+99*0.25=25.75, 1+99*0.5=50.5, 1+99*0.75=75.25, son = far = 100.
  CHECK(compute_cascade_splits(1.0f, 100.0f, 4, 0.0f, s));
  CHECK(nearly_equal(s[0], 25.75f, 1e-3f));
  CHECK(nearly_equal(s[1], 50.5f, 1e-3f));
  CHECK(nearly_equal(s[2], 75.25f, 1e-3f));
  CHECK(nearly_equal(s[3], 100.0f, 1e-4f));

  // lambda=1 -> SAF LOGARITMIK: near*(far/near)^(i/N). i=2, N=4 ->
  // 1*100^0.5 = 10 TAM OLARAK.
  CHECK(compute_cascade_splits(1.0f, 100.0f, 4, 1.0f, s));
  CHECK(nearly_equal(s[1], 10.0f, 1e-3f));
  CHECK(nearly_equal(s[3], 100.0f, 1e-4f));

  // Kademeler HER ZAMAN artan olmali (golge hacimleri ust uste binmez).
  CHECK(compute_cascade_splits(0.5f, 250.0f, 3, 0.7f, s));
  CHECK(s[0] < s[1] && s[1] < s[2]);
  CHECK(nearly_equal(s[2], 250.0f, 1e-3f));
}

ENGINE_TEST(cascade_splits_reject_invalid_parameters) {
  float s[4];
  CHECK(!compute_cascade_splits(1.0f, 100.0f, 0, 0.5f, s));  // sifir kademe
  CHECK(!compute_cascade_splits(1.0f, 100.0f, 9, 0.5f, s));  // kMaxShadowCascades asildi
  CHECK(!compute_cascade_splits(0.0f, 100.0f, 2, 0.5f, s));  // near <= 0
  CHECK(!compute_cascade_splits(50.0f, 10.0f, 2, 0.5f, s));  // far <= near
  CHECK(!compute_cascade_splits(1.0f, 100.0f, 2, 0.5f, nullptr));
}

ENGINE_TEST(cascade_bounding_sphere_touches_both_slice_planes) {
  // Dar fov: tan(fov/2)=0.1, aspect=1 -> k^2 = 0.01*2 = 0.02. Dilim [1,2].
  // (b-a)/(a+b) = 1/3 > k^2 -> GENEL durum.
  // c = (1+2)(1.02)/2 = 1.53 ; r^2 = [1 + 2*0.02*5 + 9*0.0004]/4 = 0.3009
  const float t = 0.1f;
  const float fov_y = 2.0f * std::atan(t);
  CascadeSphere s = cascade_bounding_sphere(fov_y, 1.0f, 1.0f, 2.0f);
  CHECK(nearly_equal(s.center_view.z, -1.53f, 1e-3f));
  CHECK(nearly_equal(s.radius, 0.5486f, 1e-3f));

  // Dogrulama (formule GUVENMEDEN, saf geometriyle): uzak VE yakin koseler
  // merkeze AYNI uzaklikta olmali -- kurenin ikisine de DEGDIGININ kaniti.
  const Vec3 far_corner{2.0f * t, 2.0f * t, -2.0f};
  const Vec3 near_corner{1.0f * t, 1.0f * t, -1.0f};
  CHECK(nearly_equal(length(far_corner - s.center_view), s.radius, 1e-3f));
  CHECK(nearly_equal(length(near_corner - s.center_view), s.radius, 1e-3f));
}

ENGINE_TEST(cascade_bounding_sphere_wide_fov_uses_far_plane_only) {
  // fov_y=90 (tan=1), aspect=1 -> k^2 = 2. Dilim [0.1, 1]:
  // k^2*(a+b) = 2*1.1 = 2.2 > (b-a)=0.9 -> UZAK DUZLEM durumu.
  // merkez = -b = -1, yaricap = b*sqrt(k^2) = sqrt(2).
  CascadeSphere s = cascade_bounding_sphere(kPi * 0.5f, 1.0f, 0.1f, 1.0f);
  CHECK(nearly_equal(s.center_view.z, -1.0f, 1e-4f));
  CHECK(nearly_equal(s.radius, std::sqrt(2.0f), 1e-4f));

  // Uzak kose TAM sinirda, yakin kose ICERIDE olmali.
  const Vec3 far_corner{1.0f, 1.0f, -1.0f};
  const Vec3 near_corner{0.1f, 0.1f, -0.1f};
  CHECK(nearly_equal(length(far_corner - s.center_view), s.radius, 1e-4f));
  CHECK(length(near_corner - s.center_view) < s.radius);
}

ENGINE_TEST(cascade_bounding_sphere_rejects_degenerate_slice) {
  CascadeSphere s = cascade_bounding_sphere(kPi * 0.5f, 1.0f, 5.0f, 5.0f); // far == near
  CHECK(s.radius == 0.0f);
}
