// content/terrain.hpp: heightmap uretimi/orneklemesi ve splat agirliklarini
// elle hesaplanmis degerlerle dogrular.
#include "content/terrain.hpp"
#include "core/math/vec.hpp"
#include "tests/test.hpp"

using namespace tulpar::engine;
using namespace tulpar::engine::content;

ENGINE_TEST(heightmap_generation_is_deterministic_and_bounded) {
  HeightmapConfig cfg;
  cfg.width = 16;
  cfg.height = 16;
  cfg.seed = 7;
  cfg.amplitude = 20.0f;
  float a[16 * 16], b[16 * 16];
  generate_heightmap(cfg, a);
  generate_heightmap(cfg, b);
  for (int i = 0; i < 16 * 16; i++) {
    CHECK(a[i] == b[i]);                 // ayni tohum -> AYNI harita
    CHECK(a[i] >= 0.0f && a[i] < 20.0f);  // fbm_2d [0,1) * amplitude
  }
}

ENGINE_TEST(sample_height_matches_grid_point_and_interpolates_linearly) {
  // 3x3, satir-major (z*3+x): 0 1 2 / 3 4 5 / 6 7 8
  const float h[9] = {0, 1, 2, 3, 4, 5, 6, 7, 8};
  // Tam kafes noktasi (1,1) -> h[4]=4 BIREBIR.
  CHECK(sample_height(h, 3, 3, 1.0f, 1.0f, 1.0f) == 4.0f);
  // Yarim yol (0.5,0.0): satir 0'da h[0]=0 ile h[1]=1 arasi -> TAM 0.5.
  CHECK(sample_height(h, 3, 3, 1.0f, 0.5f, 0.0f) == 0.5f);
  // Sinir DISI (negatif) -> en yakin kenara (0,0)=h[0]=0 KILITLENIR.
  CHECK(sample_height(h, 3, 3, 1.0f, -5.0f, -5.0f) == 0.0f);
  // Sinir DISI (asiri buyuk) -> en yakin kenara (2,2)=h[8]=8 KILITLENIR.
  CHECK(sample_height(h, 3, 3, 1.0f, 99.0f, 99.0f) == 8.0f);
}

ENGINE_TEST(splat_weights_normalize_across_two_overlapping_layers) {
  // layer0 [0,10] blend2 (dusme rampasi [10,12]), layer1 [8,20] blend2
  // (yukselme rampasi [6,8]). h=11: rise0=(11+2)/2=6.5, fall0=((12)-11)/2=0.5
  // -> w0=min=0.5(clamp gerekmiyor). layer1: rise1=(11-6)/2=2.5(>1->1
  // esdegeri min ile), fall1=((22)-11)/2=5.5 -> w1=min(2.5,5.5)=2.5->clamp 1.
  // Toplam=1.5 -> normalize: w0=0.5/1.5=1/3, w1=1/1.5=2/3.
  SplatLayer layers[2] = {{0, 10, 2}, {8, 20, 2}};
  float w[2];
  compute_splat_weights(11.0f, layers, 2, w);
  CHECK(nearly_equal(w[0], 1.0f / 3.0f, 1e-4f));
  CHECK(nearly_equal(w[1], 2.0f / 3.0f, 1e-4f));
  CHECK(nearly_equal(w[0] + w[1], 1.0f, 1e-5f)); // TOPLAM tam 1
}

ENGINE_TEST(splat_weights_single_layer_fully_inside_band_is_one) {
  SplatLayer layers[2] = {{0, 10, 2}, {50, 60, 2}}; // ikinci katman COK uzak
  float w[2];
  compute_splat_weights(5.0f, layers, 2, w);
  CHECK(nearly_equal(w[0], 1.0f, 1e-5f));
  CHECK(nearly_equal(w[1], 0.0f, 1e-5f));
}

ENGINE_TEST(splat_weights_all_zero_when_outside_every_band) {
  SplatLayer layers[2] = {{0, 10, 1}, {50, 60, 1}};
  float w[2];
  compute_splat_weights(25.0f, layers, 2, w); // [11,49] araligi, HICBIR katmana girmiyor
  CHECK(w[0] == 0.0f && w[1] == 0.0f);
}
