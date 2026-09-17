// sim/fluid_system.hpp: Muller 2003 cekirdeklerinin TAM degerleri elle
// hesaplanip dogrulanir; sonra iki FIZIKSEL degismez kanitlanir --
// (1) tek parcacik = serbest dusus, (2) simetrik cift = TOPLAM MOMENTUM
// KORUNUR (Newton'un 3. yasasi; basinc kuvveti simetriklestirilmeseydi
// akiskan kendi kendine ivmelenirdi).
#include "sim/fluid_system.hpp"

#include "core/math/vec.hpp"
#include "tests/test.hpp"

using namespace tulpar::engine;
using namespace tulpar::engine::sim;

ENGINE_TEST(sph_kernels_match_hand_computed_values) {
  const float h = 1.0f; // h=1 secildi: h^9 = 1, sabitler SADE kalir
  // Poly6(0,1) = 315/(64*pi) = 1.566681...
  CHECK(nearly_equal(sph_poly6(0.0f, h), 315.0f / (64.0f * kPi), 1e-5f));
  // Poly6(0.5,1) = 315/(64*pi) * (1-0.25)^3 = 1.566681 * 0.421875
  CHECK(nearly_equal(sph_poly6(0.5f, h), (315.0f / (64.0f * kPi)) * 0.421875f, 1e-5f));
  // Cekirdek YARICAPTA ve OTESINDE TAM SIFIR olmali (aksi halde uzaktaki
  // parcaciklar sessizce katki verir, komsu araması anlamsizlasir).
  CHECK(sph_poly6(1.0f, h) == 0.0f);
  CHECK(sph_poly6(1.5f, h) == 0.0f);

  // Spiky gradyan buyuklugu: 45/(pi) * (1-r)^2
  CHECK(nearly_equal(sph_spiky_grad_magnitude(0.0f, h), 45.0f / kPi, 1e-4f));
  CHECK(nearly_equal(sph_spiky_grad_magnitude(0.5f, h), (45.0f / kPi) * 0.25f, 1e-4f));
  CHECK(sph_spiky_grad_magnitude(1.0f, h) == 0.0f);

  // Viskozite Laplacian'i: 45/pi * (1-r)
  CHECK(nearly_equal(sph_viscosity_laplacian(0.0f, h), 45.0f / kPi, 1e-4f));
  CHECK(nearly_equal(sph_viscosity_laplacian(0.5f, h), (45.0f / kPi) * 0.5f, 1e-4f));
  CHECK(sph_viscosity_laplacian(1.0f, h) == 0.0f);

  // Gecersiz yaricap -> sifir (sifira bolme YOK).
  CHECK(sph_poly6(0.5f, 0.0f) == 0.0f);
  CHECK(sph_spiky_grad_magnitude(0.5f, 0.0f) == 0.0f);
}

ENGINE_TEST(sph_single_particle_is_free_fall) {
  SphParticle p[1];
  p[0].pos = Vec3{0, 0, 0};
  p[0].vel = Vec3{0, 0, 0};
  Vec3 accel[1];
  SphParams params; // varsayilan yercekimi (0,-9.81,0)

  sph_compute_density(p, 1, params);
  // Tek parcacik KENDI yogunluguna katki verir -> density > 0 olmali
  // (aksi halde sifira bolme/NaN olurdu).
  CHECK(p[0].density > 0.0f);

  sph_step(p, 1, params, 0.1f, accel);
  // Komsu YOK -> yalniz yercekimi. v = g*dt, x = v*dt (yari-kapali Euler).
  CHECK(nearly_equal(p[0].vel.y, -0.981f, 1e-4f));
  CHECK(nearly_equal(p[0].pos.y, -0.0981f, 1e-4f));
  CHECK(nearly_equal(p[0].pos.x, 0.0f, 1e-6f));
  CHECK(nearly_equal(accel[0].y, -9.81f, 1e-4f));
}

ENGINE_TEST(sph_symmetric_pair_conserves_total_momentum) {
  // Yercekimi KAPALI: geriye yalniz PARCACIKLAR ARASI kuvvetler kalir.
  // Newton'un 3. yasasi geregi bunlarin TOPLAMI sifir olmali.
  SphParams params;
  params.gravity = Vec3{0, 0, 0};
  params.smoothing_radius = 0.2f;

  SphParticle p[2];
  p[0].pos = Vec3{-0.05f, 0, 0};
  p[1].pos = Vec3{0.05f, 0, 0};
  Vec3 accel[2];

  sph_compute_density(p, 2, params);
  CHECK(nearly_equal(p[0].density, p[1].density, 1e-4f)); // simetri: yogunluklar ESIT

  for (int step = 0; step < 20; step++) {
    sph_compute_density(p, 2, params);
    sph_step(p, 2, params, 0.001f, accel);
  }

  // Toplam momentum (esit kutle -> hizlarin toplami) SIFIR kalmali.
  const Vec3 total_v = p[0].vel + p[1].vel;
  CHECK(nearly_equal(total_v, Vec3{0, 0, 0}, 1e-4f));
  // Simetri korunmali: merkez hala orijinde.
  const Vec3 center = (p[0].pos + p[1].pos) * 0.5f;
  CHECK(nearly_equal(center, Vec3{0, 0, 0}, 1e-4f));
  // Ve gercekten bir sey OLMUS olmali (basinc onlari ITMIS): birbirlerinden
  // uzaklasmis olmalilar (baslangicta 0.1 birim aralikti).
  CHECK(length(p[1].pos - p[0].pos) > 0.1f);
}

ENGINE_TEST(sph_step_rejects_missing_scratch_and_bad_dt) {
  SphParticle p[1];
  Vec3 accel[1];
  SphParams params;
  sph_compute_density(p, 1, params);

  const Vec3 before = p[0].pos;
  sph_step(p, 1, params, 0.1f, nullptr); // scratch YOK -> hicbir sey yapma
  CHECK(nearly_equal(p[0].pos, before, 1e-6f));
  sph_step(p, 1, params, 0.0f, accel); // dt=0 -> hicbir sey yapma
  CHECK(nearly_equal(p[0].pos, before, 1e-6f));
}
