#include "sim/state_machine.hpp"

namespace tulpar::engine::sim {

uint32_t StateMachine::add_state(FsmStateFn on_enter, FsmStateFn on_update, FsmStateFn on_exit) {
  if (state_count_ >= kMaxStates) return kFsmInvalidState;
  const uint32_t idx = state_count_++;
  states_[idx].on_enter = on_enter;
  states_[idx].on_update = on_update;
  states_[idx].on_exit = on_exit;
  states_[idx].transition_count = 0;
  return idx;
}

bool StateMachine::add_transition(uint32_t from_state, uint32_t to_state, FsmTransitionCondition condition) {
  if (from_state >= state_count_ || to_state >= state_count_) return false;
  FsmState &s = states_[from_state];
  if (s.transition_count >= kFsmMaxTransitionsPerState) return false;
  s.transitions[s.transition_count].condition = condition;
  s.transitions[s.transition_count].target_state = to_state;
  s.transition_count++;
  return true;
}

void StateMachine::tick(uint32_t *current_state_id, void *context, float dt) const {
  if (!current_state_id || *current_state_id >= state_count_) return;

  uint32_t state_id = *current_state_id;
  const FsmState *state = &states_[state_id];

  // 1. Durum degisikligi (transition) kontrolu
  for (uint32_t i = 0; i < state->transition_count; ++i) {
    if (state->transitions[i].condition && state->transitions[i].condition(context)) {
      uint32_t next_state_id = state->transitions[i].target_state;
      
      // on_exit cagir
      if (state->on_exit) {
        state->on_exit(context, dt);
      }
      
      // Durumu degistir
      state_id = next_state_id;
      *current_state_id = state_id;
      state = &states_[state_id];
      
      // on_enter cagir
      if (state->on_enter) {
        state->on_enter(context, dt);
      }
      
      // Ilk saglanan gecis alinir, donguden cikilir (oncelikli sirasi gecerli)
      break;
    }
  }

  // 2. Guncel durumu calistir
  if (state->on_update) {
    state->on_update(context, dt);
  }
}

} // namespace tulpar::engine::sim
