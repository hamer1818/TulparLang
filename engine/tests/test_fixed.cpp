// core/math/fixed.hpp: sabit noktali aritmetigin TAM (bit duzeyinde) beklenen
// degerleri verdigini kanitlar. Buradaki her sayi ELLE hesaplandi -- cunku
// bu tipin butun varlik sebebi "sonuc HER platformda AYNI BIT" olmasidir;
// "yaklasik dogru" bir test bu iddiayi test ETMEZ.
#include "core/math/fixed.hpp"

#include "tests/test.hpp"

using namespace tulpar::engine;

ENGINE_TEST(fixed_construction_and_exact_representation) {
  // kOne = 1<<16 = 65536. from_int(3) -> 3*65536 = 196608.
  CHECK(Fixed::kOne == 65536);
  CHECK(Fixed::from_int(3).raw == 196608);
  CHECK(Fixed::from_int(0).raw == 0);
  CHECK(Fixed::from_int(-2).raw == -131072);

  // Tam sayilar TAM temsil edilir -> gidis-donus KAYIPSIZ.
  for (int32_t i = -100; i <= 100; i++) {
    CHECK(Fixed::from_int(i).floor_int() == i);
  }
}

ENGINE_TEST(fixed_multiply_and_divide_are_bit_exact) {
  const Fixed two = Fixed::from_int(2);
  const Fixed three = Fixed::from_int(3);

  // 2*3: (131072 * 196608) >> 16 = 25769803776 >> 16 = 393216 = 6.0
  CHECK((two * three).raw == 393216);
  CHECK(two * three == Fixed::from_int(6));

  // 6/3: (393216 << 16) / 196608 = 25769803776 / 196608 = 131072 = 2.0
  CHECK((Fixed::from_int(6) / three).raw == 131072);
  CHECK(Fixed::from_int(6) / three == two);

  // Isaret: (-2)*3 = -6
  CHECK(Fixed::from_int(-2) * three == Fixed::from_int(-6));

  // Sifira bolme: TANIMSIZ DAVRANIS YOK, 0 doner (baslikta belgelendi).
  CHECK((two / Fixed::from_int(0)).raw == 0);
}

ENGINE_TEST(fixed_is_deterministic_not_algebraically_exact) {
  // Bu testin amaci tipi OVMEK degil, SINIRINI kayit altina almak.
  // 1/3 Q16.16'da tam temsil EDILEMEZ: (65536<<16)/196608 = 21845.33 ->
  // sifira dogru kesilir -> 21845.
  const Fixed third = Fixed::from_int(1) / Fixed::from_int(3);
  CHECK(third.raw == 21845);

  // Dolayisiyla (1/3)*3 TAM OLARAK 1 DEGILDIR:
  // (21845 * 196608) >> 16 = 4294901760 >> 16 = 65535  (yani 1'den 1 ULP eksik)
  CHECK((third * Fixed::from_int(3)).raw == 65535);
  CHECK(third * Fixed::from_int(3) != Fixed::from_int(1));

  // ONEMLI OLAN: bu sapma HER PLATFORMDA AYNI. Kayan noktada "1e-7 kadar
  // sapar ama ne kadar sapacagi derleyiciye/bayraga gore degisir" derdi
  // vardi; burada sapma SABIT ve TEKRARLANABILIR.
}

ENGINE_TEST(fixed_sqrt_matches_hand_computed_values) {
  // isqrt64 once dogrudan: 16 -> 4, 17179869184 -> 131072
  CHECK(isqrt64(0) == 0);
  CHECK(isqrt64(1) == 1);
  CHECK(isqrt64(16) == 4);
  CHECK(isqrt64(15) == 3);  // ASAGI yuvarlar
  CHECK(isqrt64(17179869184ull) == 131072ull);

  // sqrt(4) = 2 TAM. raw = 262144; 262144<<16 = 17179869184; isqrt = 131072.
  CHECK(fixed_sqrt(Fixed::from_int(4)) == Fixed::from_int(2));
  CHECK(fixed_sqrt(Fixed::from_int(9)) == Fixed::from_int(3));
  CHECK(fixed_sqrt(Fixed::from_int(0)).raw == 0);

  // sqrt(2): raw=131072; 131072<<16 = 8589934592.
  // 92681^2 = 8589767761 <= 8589934592 < 92682^2 = 8589953124  -> isqrt = 92681.
  // 92681/65536 = 1.414199...  (gercek sqrt(2) = 1.4142135, fark ~1 ULP)
  CHECK(fixed_sqrt(Fixed::from_int(2)).raw == 92681);

  // Negatif giris: tanimsiz davranis/NaN YOK.
  CHECK(fixed_sqrt(Fixed::from_int(-5)).raw == 0);
}

ENGINE_TEST(fixed_floor_rounds_toward_negative_infinity) {
  // floor_int aritmetik saga kaydirma -> NEGATIFTE de asagi yuvarlar
  // (sifira dogru DEGIL). -0.5 -> -1, +0.5 -> 0.
  CHECK(Fixed::from_raw(Fixed::kOne / 2).floor_int() == 0);
  CHECK(Fixed::from_raw(-Fixed::kOne / 2).floor_int() == -1);
  CHECK(Fixed::from_raw(-1).floor_int() == -1); // en kucuk negatif kesir
  CHECK(Fixed::from_int(-3).floor_int() == -3); // tam sayi: aynen
}

ENGINE_TEST(fixed_comparisons_and_helpers) {
  const Fixed a = Fixed::from_int(2), b = Fixed::from_int(5);
  CHECK(a < b);
  CHECK(b > a);
  CHECK(a <= a);
  CHECK(a != b);
  CHECK(fixed_min(a, b) == a);
  CHECK(fixed_max(a, b) == b);
  CHECK(fixed_abs(Fixed::from_int(-7)) == Fixed::from_int(7));
  CHECK(fixed_abs(Fixed::from_int(7)) == Fixed::from_int(7));
}

ENGINE_TEST(fixed_vec3_length_is_exact_for_pythagorean_triple) {
  // 3-4-5 ucgeni: uzunluk TAM OLARAK 5 cikmali (karekok kesirsiz).
  const FixedVec3 v{Fixed::from_int(3), Fixed::from_int(4), Fixed::from_int(0)};
  CHECK(length_sq(v) == Fixed::from_int(25));
  CHECK(length(v) == Fixed::from_int(5));

  // dot: (1,2,3).(4,5,6) = 4+10+18 = 32
  const FixedVec3 p{Fixed::from_int(1), Fixed::from_int(2), Fixed::from_int(3)};
  const FixedVec3 q{Fixed::from_int(4), Fixed::from_int(5), Fixed::from_int(6)};
  CHECK(dot(p, q) == Fixed::from_int(32));

  // Vektor aritmetigi
  CHECK(((p + q) == FixedVec3{Fixed::from_int(5), Fixed::from_int(7), Fixed::from_int(9)}));
  CHECK(((q - p) == FixedVec3{Fixed::from_int(3), Fixed::from_int(3), Fixed::from_int(3)}));
  CHECK(((p * Fixed::from_int(2)) == FixedVec3{Fixed::from_int(2), Fixed::from_int(4), Fixed::from_int(6)}));
}

ENGINE_TEST(fixed_multiply_cannot_overflow_intermediate) {
  // En buyuk/kucuk ham degerlerin carpimi int64 ara degerde TASMAZ
  // (32x32 -> en fazla 64 bit). Sonucun kendisi Q16.16'ya sigmayabilir --
  // bu BEKLENEN kirpilmadir, ama HESAP SIRASINDA tanimsiz davranis olmaz.
  const Fixed big = Fixed::from_raw(2147483647);   // INT32_MAX
  const Fixed small = Fixed::from_raw(1);
  // big * small = (2147483647 * 1) >> 16 = 32767
  CHECK((big * small).raw == 32767);

  const Fixed neg = Fixed::from_raw(-2147483647);
  CHECK((neg * small).raw == -32768); // aritmetik kaydirma: asagi yuvarlar
}
