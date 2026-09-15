// content/water_wave.hpp: GPU Gems'in Gerstner formulunu ozel sayilarla
// (dalga boyu=2*PI -> dalga sayisi k=1) elle hesaplanmis TAM degerlerle
// dogrular -- tepe (crest) ve baslangic noktasinda kapali-form sonuc verir.
#include "content/water_wave.hpp"
#include "core/math/vec.hpp"
#include "tests/test.hpp"

using namespace tulpar::engine;
using namespace tulpar::engine::content;

ENGINE_TEST(gerstner_wave_at_origin_and_time_zero) {
  // k=2*PI/wavelength=2*PI/(2*PI)=1 (temiz sayi). qa=steepness*amplitude=0.5*2=1.
  // theta=k*(Dx*0+Dy*0)+speed*k*0=0 -> cos=1,sin=0.
  // displacement=(qa*Dx*1, A*0, qa*Dy*1) = (1*1, 0, 1*0) = (1,0,0) (D=(1,0)).
  GerstnerWave w{{1, 0}, 2.0f * kPi, 2.0f, 0.5f, 1.0f};
  Vec3 d = gerstner_displacement(w, 0.0f, 0.0f, 0.0f);
  CHECK(nearly_equal(d, Vec3{1, 0, 0}, 1e-4f));
}

ENGINE_TEST(gerstner_wave_crest_reaches_exact_amplitude) {
  // Ayni dalga, t=PI/2'de: theta=speed*k*t=1*1*(PI/2)=PI/2 -> cos~0,sin=1.
  // displacement=(qa*1*0, A*1, qa*0*0)=(0, amplitude, 0) -- TEPE NOKTASI,
  // yukseklik TAM amplitude kadar (Gerstner'in tanimindan beklenen).
  GerstnerWave w{{1, 0}, 2.0f * kPi, 2.0f, 0.5f, 1.0f};
  Vec3 d = gerstner_displacement(w, 0.0f, 0.0f, kPi * 0.5f);
  CHECK(nearly_equal(d, Vec3{0, 2.0f, 0}, 1e-3f));
}

ENGINE_TEST(gerstner_wave_direction_maps_to_correct_axis) {
  // Yon (0,1) (Z ekseni boyunca yayilma): x0=z0=0,t=0'da ayni theta=0
  // mantigiyla displacement=(qa*0*1, 0, qa*1*1)=(0,0,qa) -- yatay bilesen
  // SADECE Z'de, X'te DEGIL (yon esleme testi).
  GerstnerWave w{{0, 1}, 2.0f * kPi, 2.0f, 0.5f, 1.0f};
  Vec3 d = gerstner_displacement(w, 0.0f, 0.0f, 0.0f);
  CHECK(nearly_equal(d, Vec3{0, 0, 1.0f}, 1e-4f));
}

ENGINE_TEST(sum_of_identical_waves_is_linear_superposition) {
  GerstnerWave w{{1, 0}, 2.0f * kPi, 2.0f, 0.5f, 1.0f};
  GerstnerWave two[2] = {w, w};
  Vec3 single = gerstner_displacement(w, 3.0f, -1.5f, 0.7f);
  Vec3 summed = sum_gerstner_displacement(two, 2, 3.0f, -1.5f, 0.7f);
  CHECK(nearly_equal(summed, single * 2.0f, 1e-4f));
}
