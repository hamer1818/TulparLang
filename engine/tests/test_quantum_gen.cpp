// sim/quantum_gen.hpp (WFC): cozulen izgaranin KURALLARI IHLAL ETMEDIGINI
// (kara ile su HICBIR ZAMAN komsu olmaz) ve ayni tohumun AYNI dunyayi
// verdigini kanitlar.
#include "sim/quantum_gen.hpp"

#include "core/math/random.hpp"
#include "tests/test.hpp"

using namespace tulpar::engine;
using namespace tulpar::engine::sim;

namespace {
// 3 dose: 0=kara, 1=kiyi, 2=su. Kural: kara-su KOMSULUGU YASAK, arada
// MUTLAKA kiyi olmali (klasik WFC ornegi).
constexpr uint32_t kLand = 0, kCoast = 1, kWater = 2;
WfcRules make_coast_rules() {
  WfcRules r;
  r.tile_count = 3;
  for (uint32_t d = 0; d < 4; d++) {
    r.allowed[d][kLand] = (1u << kLand) | (1u << kCoast);                    // kara: kara/kiyi
    r.allowed[d][kCoast] = (1u << kLand) | (1u << kCoast) | (1u << kWater);  // kiyi: hepsi
    r.allowed[d][kWater] = (1u << kCoast) | (1u << kWater);                  // su: kiyi/su
  }
  return r;
}
} // namespace

ENGINE_TEST(wfc_rules_symmetry_check_catches_bad_rules) {
  WfcRules good = make_coast_rules();
  CHECK(wfc_rules_symmetric(good));

  // Tek yonlu kural: kara sagina su alabilsin ama su soluna kara ALMASIN.
  WfcRules bad = make_coast_rules();
  bad.allowed[(uint32_t)WfcDir::kRight][kLand] |= (1u << kWater);
  CHECK(!wfc_rules_symmetric(bad));

  WfcRules empty;
  CHECK(!wfc_rules_symmetric(empty)); // tile_count == 0
}

ENGINE_TEST(wfc_solved_grid_never_violates_adjacency_rules) {
  constexpr uint32_t kW = 6, kH = 6;
  uint32_t cells[kW * kH];
  uint32_t stack[kW * kH];
  WfcGrid grid;
  grid.width = kW; grid.height = kH; grid.cells = cells; grid.stack = stack;

  const WfcRules rules = make_coast_rules();
  Rng rng(12345);
  WfcResult r = wfc_solve(grid, rules, rng);
  CHECK(r.solved);
  CHECK(!r.contradiction);

  // ASIL KANIT: her hucre TEK doseye cokmus VE hicbir kara-su komsulugu yok.
  for (uint32_t y = 0; y < kH; y++) {
    for (uint32_t x = 0; x < kW; x++) {
      const uint32_t t = wfc_tile_at(grid, x, y);
      CHECK(t == kLand || t == kCoast || t == kWater); // UINT32_MAX degil -> cokmus
      if (x + 1 < kW) {
        const uint32_t right = wfc_tile_at(grid, x + 1, y);
        CHECK(!((t == kLand && right == kWater) || (t == kWater && right == kLand)));
      }
      if (y + 1 < kH) {
        const uint32_t down = wfc_tile_at(grid, x, y + 1);
        CHECK(!((t == kLand && down == kWater) || (t == kWater && down == kLand)));
      }
    }
  }
}

ENGINE_TEST(wfc_same_seed_produces_identical_world) {
  constexpr uint32_t kW = 5, kH = 5;
  uint32_t cells_a[kW * kH], stack_a[kW * kH];
  uint32_t cells_b[kW * kH], stack_b[kW * kH];
  const WfcRules rules = make_coast_rules();

  WfcGrid a; a.width = kW; a.height = kH; a.cells = cells_a; a.stack = stack_a;
  WfcGrid b; b.width = kW; b.height = kH; b.cells = cells_b; b.stack = stack_b;

  Rng rng_a(777), rng_b(777); // AYNI tohum
  CHECK(wfc_solve(a, rules, rng_a).solved);
  CHECK(wfc_solve(b, rules, rng_b).solved);

  // Prosedurel dunya HER platformda ayni olmali (VIZYON SS1: bit-es).
  for (uint32_t i = 0; i < kW * kH; i++) CHECK(cells_a[i] == cells_b[i]);
}

ENGINE_TEST(wfc_detects_contradiction_on_impossible_rules) {
  // Hicbir dose hicbir doseyle komsu OLAMAZ -> ilk cokus komsuyu bosaltir.
  WfcRules impossible;
  impossible.tile_count = 2;
  for (uint32_t d = 0; d < 4; d++) {
    impossible.allowed[d][0] = 0;
    impossible.allowed[d][1] = 0;
  }
  CHECK(wfc_rules_symmetric(impossible)); // simetrik ama COZULEMEZ

  uint32_t cells[4], stack[4];
  WfcGrid grid;
  grid.width = 2; grid.height = 2; grid.cells = cells; grid.stack = stack;
  Rng rng(1);
  WfcResult r = wfc_solve(grid, impossible, rng);
  CHECK(r.contradiction);
  CHECK(!r.solved);
}

ENGINE_TEST(wfc_single_cell_grid_and_bad_input) {
  uint32_t cells[1], stack[1];
  WfcGrid grid;
  grid.width = 1; grid.height = 1; grid.cells = cells; grid.stack = stack;
  const WfcRules rules = make_coast_rules();
  Rng rng(5);

  WfcResult r = wfc_solve(grid, rules, rng);
  CHECK(r.solved);           // komsu yok -> tek cokus yeter
  CHECK(r.collapsed == 1);
  CHECK(wfc_tile_at(grid, 0, 0) != UINT32_MAX);

  WfcGrid bad; // diziler yok
  CHECK(!wfc_solve(bad, rules, rng).solved);
  CHECK(wfc_tile_at(bad, 0, 0) == UINT32_MAX);
}
