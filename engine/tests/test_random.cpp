// core/math/random.hpp: Rng'nin TEK var olma nedeni determinizmdir (motorun
// "3 platform bit-es" garantisini prosedurel icerige tasimak) -- bu yuzden
// asil test EXACT deger degil, DETERMINIZM ozelligidir (ayni tohum -> ayni
// dizi). Ayrica bilinen xorshift zafiyeti (tohum=0) ve aralik/karistirma
// kisitlarini dogrular.
#include "core/math/random.hpp"
#include "tests/test.hpp"

using namespace tulpar::engine;

ENGINE_TEST(rng_same_seed_produces_identical_sequence) {
  Rng a(42), b(42);
  for (int i = 0; i < 100; i++) CHECK(a.next_u32() == b.next_u32());
}

ENGINE_TEST(rng_different_seeds_diverge) {
  Rng a(1), b(2);
  bool any_diff = false;
  for (int i = 0; i < 8; i++)
    if (a.next_u32() != b.next_u32()) any_diff = true;
  CHECK(any_diff); // pratikte ILK cekiliste bile farkli cikar
}

ENGINE_TEST(rng_zero_seed_does_not_get_stuck) {
  // xorshift'in bilinen zayifligi: HAM tohum 0 ise state HEP 0 kalir ve
  // next_u32() SONSUZA KADAR 0 doner -- constructor bunu onlemeli.
  Rng r(0);
  CHECK(r.state() != 0);
  for (int i = 0; i < 20; i++) CHECK(r.next_u32() != 0);
}

ENGINE_TEST(rng_next_float_stays_in_unit_range) {
  Rng r(123);
  for (int i = 0; i < 500; i++) {
    float f = r.next_float();
    CHECK(f >= 0.0f && f < 1.0f);
  }
}

ENGINE_TEST(rng_next_range_respects_bounds) {
  Rng r(7);
  bool saw_low = false, saw_high_minus_one = false;
  for (int i = 0; i < 2000; i++) {
    int32_t v = r.next_range(-5, 5); // [-5, 5)
    CHECK(v >= -5 && v < 5);
    if (v == -5) saw_low = true;
    if (v == 4) saw_high_minus_one = true;
  }
  CHECK(saw_low);            // 2000 cekiliste alt sinira HIC rastlanmamasi neredeyse imkansiz
  CHECK(saw_high_minus_one); // ayni sekilde ust sinirin bir altina
}

ENGINE_TEST(rng_shuffle_is_deterministic_and_preserves_elements) {
  int arr1[8] = {0, 1, 2, 3, 4, 5, 6, 7};
  int arr2[8] = {0, 1, 2, 3, 4, 5, 6, 7};
  Rng r1(99), r2(99);
  shuffle(arr1, 8, r1);
  shuffle(arr2, 8, r2);
  for (int i = 0; i < 8; i++) CHECK(arr1[i] == arr2[i]); // ayni tohum -> AYNI permutasyon

  // Eleman kumesi KORUNDU (0..7 hepsi TAM BIR KEZ var, sirasi degisti).
  bool seen[8] = {false};
  for (int i = 0; i < 8; i++) {
    CHECK(arr1[i] >= 0 && arr1[i] < 8);
    CHECK(!seen[arr1[i]]);
    seen[arr1[i]] = true;
  }
}
