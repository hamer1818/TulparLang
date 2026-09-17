#include "sim/npc_motivation.hpp"

namespace tulpar::engine::sim {

uint32_t NpcMotivationSystem::select_action(const NpcMotivationState& state, const MotivationConfig& cfg, float random_value) const {
  if (state.current_state >= kMaxStates) return 0;
  
  // Epsilon-Greedy secimi:
  if (random_value < cfg.exploration_rate) {
    // Rastgele kesif (exploration). Determinizm icin random_value carpan olarak kullanilir.
    // kMaxActions sabit oldugu icin basit bir mod veya carpim yeterlidir.
    // random_value [0, exploration_rate) araliginda.
    float normalized_rand = random_value / cfg.exploration_rate; // 0..1 arasi
    return (uint32_t)(normalized_rand * kMaxActions) % kMaxActions;
  }
  
  // Sömürme (Exploitation): En yuksek Q-degerine sahip eylemi sec
  uint32_t best_action = 0;
  float best_q = state.q_table[state.current_state][0];
  
  for (uint32_t a = 1; a < kMaxActions; ++a) {
    float q = state.q_table[state.current_state][a];
    if (q > best_q) {
      best_q = q;
      best_action = a;
    }
  }
  
  return best_action;
}

void NpcMotivationSystem::update_q_value(NpcMotivationState* state, const MotivationConfig& cfg, uint32_t next_state, float reward) {
  if (!state || state->current_state >= kMaxStates || next_state >= kMaxStates || state->last_action >= kMaxActions) {
    return;
  }
  
  // Bir sonraki durumda (next_state) elde edilebilecek maksimum Q degeri (max_next_Q)
  float max_next_q = state->q_table[next_state][0];
  for (uint32_t a = 1; a < kMaxActions; ++a) {
    float q = state->q_table[next_state][a];
    if (q > max_next_q) {
      max_next_q = q;
    }
  }
  
  // Guncel Q degeri
  float current_q = state->q_table[state->current_state][state->last_action];
  
  // TD Hatasi (Temporal Difference Error)
  // td_error = Odul + Indirim_Faktoru * Max_Gelecek_Q - Mevcut_Q
  float td_error = reward + cfg.discount_factor * max_next_q - current_q;
  
  // Q tablosunu guncelle (Dopamin sinyali)
  state->q_table[state->current_state][state->last_action] += cfg.learning_rate * td_error;
  
  // Durumu ilerlet
  state->current_state = next_state;
}

} // namespace tulpar::engine::sim
