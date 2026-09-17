// L4 SIMULATION — Basit Sonlu Durum Makinesi (Finite State Machine).
// DEVAM_PLANI.md Faz B madde 6'nin karsiligi (BehaviorTree ile birlikte).
//
// Veri-yonelimli tasarim: BehaviorTree ile ayni felsefe. StateMachine
// TANIMI sabit/paylasilabilir bir struct'tir. Ajanlarin guncel durumlari
// (hangi state'te olduklari) ajan bilesenlerinde (ECS icinde) tutulur, 
// boylece ayni makineyi yuzlerce ajan bellek cogaltmadan kullanabilir.
#pragma once
#include <cstdint>

namespace tulpar::engine::sim {

using FsmStateFn = void (*)(void *context, float dt);
using FsmTransitionCondition = bool (*)(void *context);

constexpr uint32_t kFsmMaxStates = 32;
constexpr uint32_t kFsmMaxTransitionsPerState = 8;
constexpr uint32_t kFsmInvalidState = 0xFFFFFFFFu;

struct FsmTransition {
  FsmTransitionCondition condition = nullptr;
  uint32_t target_state = kFsmInvalidState;
};

struct FsmState {
  FsmStateFn on_enter = nullptr;
  FsmStateFn on_update = nullptr;
  FsmStateFn on_exit = nullptr;
  FsmTransition transitions[kFsmMaxTransitionsPerState] = {};
  uint32_t transition_count = 0;
};

class StateMachine {
 public:
  static constexpr uint32_t kMaxStates = kFsmMaxStates;

  // Yeni durum ekler ve indeksini dondurur.
  uint32_t add_state(FsmStateFn on_enter, FsmStateFn on_update, FsmStateFn on_exit);
  
  // Iki durum arasina kosullu gecis (transition) ekler.
  bool add_transition(uint32_t from_state, uint32_t to_state, FsmTransitionCondition condition);

  // Ajan bazli guncelleme. current_state_id baslangicta gecerli bir state'i isaret etmelidir.
  // Eger kosullardan biri saglanirsa on_exit cagrilir, current_state_id hedefe degisir ve on_enter cagrilir.
  // Son olarak (guncel durum uzerinden) on_update cagrilir.
  void tick(uint32_t *current_state_id, void *context, float dt) const;

 private:
  FsmState states_[kFsmMaxStates];
  uint32_t state_count_ = 0;
};

} // namespace tulpar::engine::sim
