// sim/chaos_physics.hpp: FABRIK'in DEGISMEZ ozelligini (kemik uzunluklari
// KORUNUR, kok SABIT kalir) ve akis alaninin duvarlari DOLANDIGINI elle
// hesaplanmis izgaralarla kanitlar.
#include "sim/chaos_physics.hpp"

#include "core/math/vec.hpp"
#include "tests/test.hpp"

using namespace tulpar::engine;
using namespace tulpar::engine::sim;

namespace {
// FABRIK'in TEK kritik degismezi: cozum sonrasi her kemik TAM olarak
// baslangictaki uzunlugunda olmali (aksi halde karakterin kollari uzar).
bool lengths_preserved(const Vec3 *p, const float *len, uint32_t count, float eps = 1e-3f) {
  for (uint32_t i = 0; i + 1 < count; i++) {
    if (!nearly_equal(length(p[i + 1] - p[i]), len[i], eps)) return false;
  }
  return true;
}
} // namespace

ENGINE_TEST(fabrik_reaches_reachable_target_and_preserves_bone_lengths) {
  // 3 eklem / 2 kemik, her biri 1 birim. Kok (0,0,0), baslangicta +X'e duz.
  // Hedef (0,2,0): kokten uzaklik 2 == toplam erisim 2 -> TAM ulasilabilir.
  Vec3 p[3] = {{0, 0, 0}, {1, 0, 0}, {2, 0, 0}};
  const float len[2] = {1.0f, 1.0f};

  FabrikResult r = solve_fabrik(p, len, 3, Vec3{0, 2, 0}, 0.01f, 32);
  CHECK(r.reached);
  CHECK(nearly_equal(p[0], Vec3{0, 0, 0}, 1e-5f)); // KOK oynamadi
  CHECK(length(p[2] - Vec3{0, 2, 0}) < 0.01f);     // uc eklem hedefte
  CHECK(lengths_preserved(p, len, 3));
}

ENGINE_TEST(fabrik_unreachable_target_straightens_chain_toward_target) {
  // Toplam erisim 2, hedef 10 birim uzakta -> ULASILAMAZ. FABRIK'in tanimli
  // davranisi: zincir hedefe DOGRU tamamen duzlesir, kemikler UZAMAZ.
  Vec3 p[3] = {{0, 0, 0}, {1, 0, 0}, {2, 0, 0}};
  const float len[2] = {1.0f, 1.0f};

  FabrikResult r = solve_fabrik(p, len, 3, Vec3{0, 10, 0}, 0.01f, 32);
  CHECK(!r.reached);
  CHECK(r.iterations == 0); // ulasilamaz durum YINELEME GEREKTIRMEZ
  CHECK(nearly_equal(p[0], Vec3{0, 0, 0}, 1e-5f));
  // Hedef +Y'de: zincir +Y boyunca duzlesmeli -> (0,1,0) ve (0,2,0).
  CHECK(nearly_equal(p[1], Vec3{0, 1, 0}, 1e-4f));
  CHECK(nearly_equal(p[2], Vec3{0, 2, 0}, 1e-4f));
  CHECK(lengths_preserved(p, len, 3));
}

ENGINE_TEST(fabrik_rejects_degenerate_input) {
  Vec3 p[1] = {{0, 0, 0}};
  const float len[1] = {1.0f};
  FabrikResult r = solve_fabrik(p, len, 1, Vec3{1, 0, 0}); // count<2 -> gecersiz
  CHECK(!r.reached);
  CHECK(r.iterations == 0);
}

ENGINE_TEST(flow_field_points_every_cell_toward_goal_on_open_grid) {
  // 3x3 acik izgara, hedef MERKEZ (1,1). Elle hesaplanan maliyetler:
  //   z=0: 2 1 2
  //   z=1: 1 0 1
  //   z=2: 2 1 2
  const uint8_t blocked[9] = {0, 0, 0, 0, 0, 0, 0, 0, 0};
  uint16_t cost[9];
  Vec3 flow[9];
  uint32_t queue[9];
  FlowField f;
  f.width = 3; f.height = 3; f.cell_size = 1.0f;
  f.blocked = blocked; f.cost = cost; f.flow = flow; f.queue = queue;

  CHECK(build_flow_field(f, 1, 1));
  CHECK(cost[f.index(1, 1)] == 0);
  CHECK(cost[f.index(1, 0)] == 1);
  CHECK(cost[f.index(0, 1)] == 1);
  CHECK(cost[f.index(0, 0)] == 2); // kose: iki adim
  CHECK(cost[f.index(2, 2)] == 2);

  // (1,0) hucresi: tek adimda hedefe -> +Z yonu.
  CHECK(nearly_equal(flow[f.index(1, 0)], Vec3{0, 0, 1}, 1e-4f));
  // (0,0) kosesi: CAPRAZ olarak hedefe (kose kesme serbest, komsular acik).
  const float d = 0.70710678f;
  CHECK(nearly_equal(flow[f.index(0, 0)], Vec3{d, 0, d}, 1e-4f));
  // Hedefin kendisi: yon YOK.
  CHECK(nearly_equal(flow[f.index(1, 1)], Vec3{0, 0, 0}, 1e-5f));
}

ENGINE_TEST(flow_field_routes_around_wall) {
  // 3x3, MERKEZ (1,1) DUVAR, hedef (0,0). Elle hesaplanan maliyetler:
  //   z=0: 0 1 2
  //   z=1: 1 X 3
  //   z=2: 2 3 4
  uint8_t blocked[9] = {0, 0, 0, 0, 0, 0, 0, 0, 0};
  uint16_t cost[9];
  Vec3 flow[9];
  uint32_t queue[9];
  FlowField f;
  f.width = 3; f.height = 3; f.cell_size = 1.0f;
  f.blocked = blocked; f.cost = cost; f.flow = flow; f.queue = queue;
  blocked[f.index(1, 1)] = 1;

  CHECK(build_flow_field(f, 0, 0));
  CHECK(cost[f.index(0, 0)] == 0);
  CHECK(cost[f.index(2, 1)] == 3); // duvari DOLANARAK: (0,0)->(1,0)->(2,0)->(2,1)
  CHECK(cost[f.index(2, 2)] == 4);
  CHECK(cost[f.index(1, 1)] == kFlowFieldBlocked); // duvar maliyet almaz

  // (2,2): capraz (1,1) DUVAR oldugu icin kullanilamaz -> -X'e (yani (1,2)) gider.
  CHECK(nearly_equal(flow[f.index(2, 2)], Vec3{-1, 0, 0}, 1e-4f));
  CHECK(nearly_equal(flow[f.index(1, 1)], Vec3{0, 0, 0}, 1e-5f)); // duvarda akis yok
}

ENGINE_TEST(flow_field_rejects_bad_input_and_samples_outside_as_zero) {
  const uint8_t blocked[4] = {0, 0, 0, 0};
  uint16_t cost[4];
  Vec3 flow[4];
  uint32_t queue[4];
  FlowField f;
  f.width = 2; f.height = 2; f.cell_size = 1.0f;
  f.blocked = blocked; f.cost = cost; f.flow = flow; f.queue = queue;

  CHECK(!build_flow_field(f, 5, 0)); // hedef izgara DISINDA
  CHECK(build_flow_field(f, 0, 0));
  // Izgara disi dunya konumu -> yon yok (ekstrapolasyon YAPILMAZ).
  CHECK(nearly_equal(sample_flow(f, Vec3{-5, 0, -5}), Vec3{0, 0, 0}, 1e-5f));
  CHECK(nearly_equal(sample_flow(f, Vec3{100, 0, 100}), Vec3{0, 0, 0}, 1e-5f));
  // (1,0) hucresi icindeki bir dunya noktasi -> hedefe (-X) dogru.
  CHECK(nearly_equal(sample_flow(f, Vec3{1.5f, 0, 0.5f}), Vec3{-1, 0, 0}, 1e-4f));
}
