// content/wind.hpp: gustiness=0 iken gurultu KATKISI sifirlanir (1.0*(n*2-1)
// carpani devre disi kalir) -- sonuc TANIM GEREGI direction*strength'e
// BIREBIR esit olmali, gurultunun kendi degerinden BAGIMSIZ.
#include "content/wind.hpp"
#include "core/math/vec.hpp"
#include "core/math/random.hpp" // Rng (PR #322: eksikti)
#include "tests/test.hpp"

using namespace tulpar::engine;
using namespace tulpar::engine::content;

ENGINE_TEST(wind_zero_gustiness_is_exactly_direction_times_strength) {
  WindField w;
  w.direction = {1, 0};
  w.strength = 3.0f;
  w.gustiness = 0.0f;
  Vec2 v = sample_wind(w, 12.3f, -4.5f, 7.0f); // konum/zaman ONEMSIZ, gustiness=0
  CHECK(nearly_equal(v.x, 3.0f, 1e-5f));
  CHECK(nearly_equal(v.y, 0.0f, 1e-5f));
}

ENGINE_TEST(wind_magnitude_stays_within_gust_bounds) {
  WindField w;
  w.direction = {0, 1};
  w.strength = 2.0f;
  w.gustiness = 0.4f; // gust carpani [0.6, 1.4) araliginda -> siddet [1.2, 2.8)
  Rng r(11);
  for (int i = 0; i < 200; i++) {
    float x = (r.next_float() - 0.5f) * 100.0f;
    float z = (r.next_float() - 0.5f) * 100.0f;
    float t = r.next_float() * 50.0f;
    Vec2 v = sample_wind(w, x, z, t);
    float mag = length(v);
    CHECK(mag >= 2.0f * 0.6f - 1e-4f && mag <= 2.0f * 1.4f + 1e-4f);
  }
}

ENGINE_TEST(wind_direction_is_preserved_regardless_of_gust) {
  // Yon (0,1) sabit: HER zaman v.x=0 olmali (gust sadece BUYUKLUGU degistirir).
  WindField w;
  w.direction = {0, 1};
  w.strength = 1.0f;
  w.gustiness = 0.8f;
  Vec2 v = sample_wind(w, 5.0f, 5.0f, 2.0f);
  CHECK(nearly_equal(v.x, 0.0f, 1e-5f));
}
