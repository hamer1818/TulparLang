// DesyncHistory: World::content_hash() zaten var (sim/ecs.hpp) — burada
// SADECE tarihce+karsilastirma sarmalayicisi test edilir (World'e ihtiyac
// yok, gercek hash uretimi test_scene_faz2.cpp'de zaten kapida).
#include "sim/desync.hpp"
#include "tests/test.hpp"

using namespace tulpar::engine;
using namespace tulpar::engine::test;

ENGINE_TEST(desync_history_get_returns_pushed_hash) {
  sim::DesyncHistory h;
  h.push(10, 0xAAAAull);
  h.push(11, 0xBBBBull);
  uint64_t v = 0;
  CHECK(h.get(10, &v) && v == 0xAAAAull);
  CHECK(h.get(11, &v) && v == 0xBBBBull);
}

ENGINE_TEST(desync_history_get_missing_tick_fails) {
  // Negatif kontrol: hic yazilmamis tick sessizce 0 dondurmemeli, false vermeli.
  sim::DesyncHistory h;
  h.push(10, 0xAAAAull);
  uint64_t v = 123;
  CHECK(!h.get(999, &v));
  CHECK(v == 123); // basarisizlikta cikti degismemeli
}

ENGINE_TEST(desync_compare_detects_match_and_mismatch) {
  sim::DesyncHistory h;
  h.push(5, 0x1234ull);
  CHECK(h.compare(5, 0x1234ull) == sim::DesyncHistory::Result::kMatch);
  CHECK(h.compare(5, 0x9999ull) == sim::DesyncHistory::Result::kMismatch);
  CHECK(h.compare(6, 0x1234ull) == sim::DesyncHistory::Result::kUnknown);
}

ENGINE_TEST(desync_history_ring_evicts_oldest) {
  sim::DesyncHistory h;
  // kMaxHistory+10 push: en eski 10 tanesi halkadan dusmeli.
  for (uint32_t t = 0; t < sim::DesyncHistory::kMaxHistory + 10; t++) h.push(t, (uint64_t)t * 7 + 1);
  uint64_t v = 0;
  CHECK(!h.get(0, &v));  // dustu
  CHECK(!h.get(5, &v));  // dustu
  CHECK(h.get(10, &v) && v == 10ull * 7 + 1); // ilk hayatta kalan
  const uint32_t last = sim::DesyncHistory::kMaxHistory + 9;
  CHECK(h.get(last, &v) && v == (uint64_t)last * 7 + 1); // en yeni
}
