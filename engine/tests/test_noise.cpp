// core/math/noise.hpp: en guclu kontrol TAM KAFES NOKTASINDA sonucun
// lattice_hash ile BIREBIR AYNI olmasidir (enterpolasyon agirligi o
// kosede TAM 1) -- bu, hash'in kendi bit aritmetigini elle dogrulamaya
// GEREK KALMADAN (core/math/random.hpp'deki Rng testleriyle ayni ilke)
// enterpolasyon mantigini kesin olarak kanitlar. Geri kalani sinir/ozellik
// testleridir (deterministik, [0,1) araliginda, fbm(oktav=1)=value_noise).
#include "core/math/noise.hpp"
#include "core/math/random.hpp"
#include "tests/test.hpp"

using namespace tulpar::engine;

ENGINE_TEST(value_noise_matches_lattice_hash_exactly_at_integer_points) {
  CHECK(value_noise_2d(3.0f, 5.0f, 42u) == lattice_hash(3, 5, 42u));
  CHECK(value_noise_2d(0.0f, 0.0f, 7u) == lattice_hash(0, 0, 7u));
  // Negatif koordinatlar: floor() dogru yuvarlamali ((int)(-3.0f) TESADUFEN
  // dogru cikar ama floor()'un KENDISI negatifte de doguru calismali --
  // asil risk kesirli negatiflerde, asagidaki testte ayrica kontrol edilir).
  CHECK(value_noise_2d(-3.0f, -5.0f, 42u) == lattice_hash(-3, -5, 42u));
}

ENGINE_TEST(value_noise_negative_fractional_uses_correct_floor) {
  // x=-0.5 -> floor(-0.5)=-1 (ix0=-1, fx=0.5) -- NAIF (int) donusumu (kesme,
  // floor DEGIL) burada -0.5f'i 0'a keserdi, YANLIS kafes hucresini secerdi.
  // Elle kontrol: fx=fy=0.5 (tam ORTA), sonuc [0,1) icinde olmali VE
  // (0,0)/(−1,0)/(0,−1)/(−1,−1) koselerinin bir konveks kombinasyonu olmali
  // -- asil dogrulanan floor'un doğru hucreyi sectigidir, tam deger degil.
  float v = value_noise_2d(-0.5f, -0.5f, 1u);
  CHECK(v >= 0.0f && v < 1.0f);
  float v00 = lattice_hash(-1, -1, 1u);
  float v10 = lattice_hash(0, -1, 1u);
  float v01 = lattice_hash(-1, 0, 1u);
  float v11 = lattice_hash(0, 0, 1u);
  float lo = v00, hi = v00;
  if (v10 < lo) lo = v10; if (v10 > hi) hi = v10;
  if (v01 < lo) lo = v01; if (v01 > hi) hi = v01;
  if (v11 < lo) lo = v11; if (v11 > hi) hi = v11;
  CHECK(v >= lo - 1e-5f && v <= hi + 1e-5f); // konveks kombinasyon sinirlar disina CIKAMAZ
}

ENGINE_TEST(value_noise_stays_in_unit_range_across_many_samples) {
  Rng r(123);
  for (int i = 0; i < 500; i++) {
    float x = (r.next_float() - 0.5f) * 200.0f; // [-100, 100)
    float y = (r.next_float() - 0.5f) * 200.0f;
    float v = value_noise_2d(x, y, 99u);
    CHECK(v >= 0.0f && v < 1.0f);
  }
}

ENGINE_TEST(value_noise_is_deterministic) {
  CHECK(value_noise_2d(12.34f, -5.67f, 8u) == value_noise_2d(12.34f, -5.67f, 8u));
}

ENGINE_TEST(fbm_single_octave_matches_value_noise_exactly) {
  // octaves=1 -> tanim geregi tek terim, seed+0*sabit=seed, max_amp=1.0 ->
  // bolme etkisiz -- BIREBIR ayni sonuc olmali.
  float a = fbm_2d(7.5f, -2.25f, 55u, 1);
  float b = value_noise_2d(7.5f, -2.25f, 55u);
  CHECK(a == b);
}

ENGINE_TEST(fbm_multi_octave_stays_in_unit_range) {
  Rng r(321);
  for (int i = 0; i < 200; i++) {
    float x = (r.next_float() - 0.5f) * 50.0f;
    float y = (r.next_float() - 0.5f) * 50.0f;
    float v = fbm_2d(x, y, 4u, 5); // 5 oktav
    CHECK(v >= 0.0f && v < 1.0f);
  }
}
