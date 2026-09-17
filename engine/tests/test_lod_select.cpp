// content/lod_select.hpp: esik disiplinini ve gecis bandinin DOGRUSAL
// karisimini elle hesaplanmis degerlerle dogrular.
#include "content/lod_select.hpp"
#include "core/math/vec.hpp"
#include "tests/test.hpp"

using namespace tulpar::engine;
using namespace tulpar::engine::content;

ENGINE_TEST(traditional_lod_picks_correct_band) {
  const float th[3] = {10, 20, 30}; // 4 LOD (0..3)
  CHECK(select_lod_traditional(5, th, 4) == 0);
  CHECK(select_lod_traditional(10, th, 4) == 1);  // esikte -> BIR SONRAKI
  CHECK(select_lod_traditional(15, th, 4) == 1);
  CHECK(select_lod_traditional(25, th, 4) == 2);
  CHECK(select_lod_traditional(99, th, 4) == 3); // tum esikleri asti -> en dusuk detay
}

ENGINE_TEST(dithered_lod_matches_traditional_outside_blend_band) {
  const float th[3] = {10, 20, 30};
  DitheredLod d = select_lod_dithered(5.0f, th, 4, 4.0f); // bant genisligi 4 (yari=2)
  CHECK(d.lod_a == 0 && d.lod_b == 0 && d.blend == 0.0f);
  d = select_lod_dithered(15.0f, th, 4, 4.0f); // iki bant (8-12, 18-22) arasinda
  CHECK(d.lod_a == 1 && d.lod_b == 1 && d.blend == 0.0f);
  d = select_lod_dithered(99.0f, th, 4, 4.0f);
  CHECK(d.lod_a == 3 && d.lod_b == 3 && d.blend == 0.0f);
}

ENGINE_TEST(dithered_lod_blends_linearly_inside_band) {
  const float th[3] = {10, 20, 30};
  // Bant [8,12] (esik 10, yari-genislik 2): 9 -> t=(9-8)/4=0.25, 10 -> t=0.5, 12'ye YAKIN -> t~1.
  DitheredLod d = select_lod_dithered(9.0f, th, 4, 4.0f);
  CHECK(d.lod_a == 0 && d.lod_b == 1);
  CHECK(nearly_equal(d.blend, 0.25f, 1e-4f));

  d = select_lod_dithered(10.0f, th, 4, 4.0f);
  CHECK(d.lod_a == 0 && d.lod_b == 1);
  CHECK(nearly_equal(d.blend, 0.5f, 1e-4f));

  d = select_lod_dithered(11.9f, th, 4, 4.0f);
  CHECK(d.lod_a == 0 && d.lod_b == 1);
  CHECK(d.blend > 0.9f && d.blend <= 1.0f); // bandin sonuna COK yakin -> neredeyse tam lod_b
}
