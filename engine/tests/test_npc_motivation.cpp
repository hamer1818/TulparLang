#include "tests/test.hpp"
#include "sim/npc_motivation.hpp"

using namespace tulpar::engine::sim;

ENGINE_TEST(engine_test_npc_motivation) {
  NpcMotivationSystem sys;
  MotivationConfig cfg;
  cfg.learning_rate = 0.5f;
  cfg.discount_factor = 0.9f;
  cfg.exploration_rate = 0.0f; // Epsilon 0, surekli somurur (en iyi Q)
  
  NpcMotivationState state;
  // Baslangicta hepsi 0. Eylem 1 icin odul verelim
  state.current_state = 0;
  state.last_action = 1;

  // Odul 10, sonraki durum 1 olsun
  sys.update_q_value(&state, cfg, 1, 10.0f);
  
  // Q tablosu: q(0,1) = 0 + 0.5 * (10 + 0.9 * 0 - 0) = 5.0f
  CHECK(state.q_table[0][1] > 4.9f && state.q_table[0][1] < 5.1f);
  CHECK(state.current_state == 1);
  
  // Simdi Durum 0'da en iyi eylemi secmeye calisalim
  state.current_state = 0;
  uint32_t action = sys.select_action(state, cfg, 0.5f);
  
  // Epsilon 0 oldugu icin 0.5 ile rastgelelik islemez, her zaman en yuksek Q (eylem 1) secilir.
  CHECK(action == 1);
}
