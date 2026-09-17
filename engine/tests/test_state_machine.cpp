#include "tests/test.hpp"
#include "sim/state_machine.hpp"

using namespace tulpar::engine::sim;

namespace {

struct AgentState {
  uint32_t state_id;
  int hp = 100;
  int timer = 0;
  bool is_dead = false;
};

// Durum 1: Idle
void idle_enter(void*, float) {}
void idle_update(void* ctx, float) {
  auto* agent = static_cast<AgentState*>(ctx);
  agent->timer++;
}
void idle_exit(void*, float) {}

// Durum 2: Attack
void attack_enter(void* ctx, float) {
  auto* agent = static_cast<AgentState*>(ctx);
  agent->timer = 0;
}
void attack_update(void*, float) {}
void attack_exit(void*, float) {}

// Durum 3: Dead
void dead_enter(void* ctx, float) {
  auto* agent = static_cast<AgentState*>(ctx);
  agent->is_dead = true;
}
void dead_update(void*, float) {}
void dead_exit(void*, float) {}

// Kosullar
bool hp_zero(void* ctx) {
  return static_cast<AgentState*>(ctx)->hp <= 0;
}

bool timer_expired(void* ctx) {
  return static_cast<AgentState*>(ctx)->timer >= 3;
}

bool always_true(void*) { return true; }

} // namespace

void test_fsm_transitions() {
  StateMachine fsm;
  
  uint32_t st_idle = fsm.add_state(idle_enter, idle_update, idle_exit);
  uint32_t st_attack = fsm.add_state(attack_enter, attack_update, attack_exit);
  uint32_t st_dead = fsm.add_state(dead_enter, dead_update, dead_exit);

  CHECK(st_idle == 0);
  CHECK(st_attack == 1);
  CHECK(st_dead == 2);

  // Gecisler (Transitions)
  // Idle -> Dead (HP <= 0)
  fsm.add_transition(st_idle, st_dead, hp_zero);
  // Idle -> Attack (Timer >= 3)
  fsm.add_transition(st_idle, st_attack, timer_expired);
  
  // Attack -> Dead (HP <= 0)
  fsm.add_transition(st_attack, st_dead, hp_zero);
  // Attack -> Idle (kosulsuz, aninda don)
  fsm.add_transition(st_attack, st_idle, always_true);

  AgentState agent;
  agent.state_id = st_idle;
  
  // 1. Tick: timer 1 olur
  fsm.tick(&agent.state_id, &agent, 1.0f);
  CHECK(agent.state_id == st_idle);
  CHECK(agent.timer == 1);
  
  // 2. Tick: timer 2 olur
  fsm.tick(&agent.state_id, &agent, 1.0f);
  CHECK(agent.state_id == st_idle);
  CHECK(agent.timer == 2);
  
  // 3. Tick: timer 3 olur
  fsm.tick(&agent.state_id, &agent, 1.0f);
  CHECK(agent.state_id == st_idle);
  CHECK(agent.timer == 3);

  // 4. Tick: timer_expired kosulu saglanir, Attack'a gecer. 
  // on_enter(attack) timer'i sifirlar. update(attack) bos.
  fsm.tick(&agent.state_id, &agent, 1.0f);
  CHECK(agent.state_id == st_attack);
  CHECK(agent.timer == 0);
  
  // 5. Tick: Attack'ta kosulsuz her zaman Idle'a don (always_true).
  // on_enter(idle) bos. update(idle) timer'i artirir (0 -> 1).
  fsm.tick(&agent.state_id, &agent, 1.0f);
  CHECK(agent.state_id == st_idle);
  CHECK(agent.timer == 1);
  
  // HP sifirlaninca Dead durumuna gecmeli
  agent.hp = 0;
  fsm.tick(&agent.state_id, &agent, 1.0f);
  CHECK(agent.state_id == st_dead);
  CHECK(agent.is_dead == true);
}

ENGINE_TEST(engine_test_state_machine) {
  test_fsm_transitions();
}
