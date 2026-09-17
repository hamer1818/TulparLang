#include "tests/test.hpp"
#include "core/event.hpp"

using namespace tulpar::engine::core;

namespace {

struct PlayerDamageEvent {
  uint32_t player_id;
  float damage;
};

struct TestContext {
  int received_damage = 0;
  int heal_events = 0;
};

void on_damage(const Event &ev, void *user_data) {
  auto *ctx = static_cast<TestContext *>(user_data);
  if (ev.payload_size == sizeof(PlayerDamageEvent)) {
    const auto *dmg = reinterpret_cast<const PlayerDamageEvent *>(ev.payload);
    if (dmg->player_id == 1) {
      ctx->received_damage += (int)dmg->damage;
    }
  }
}

void on_heal(const Event&, void *user_data) {
  auto *ctx = static_cast<TestContext *>(user_data);
  ctx->heal_events++;
}

} // namespace

ENGINE_TEST(engine_test_event_bus) {
  EventBus bus;
  CHECK(bus.init() == true);

  TestContext ctx;

  // Type ID'leri enum gibi dusunebiliriz
  constexpr uint32_t kEvDamage = 100;
  constexpr uint32_t kEvHeal = 101;

  CHECK(bus.subscribe(kEvDamage, on_damage, &ctx) == true);
  CHECK(bus.subscribe(kEvHeal, on_heal, &ctx) == true);

  // Olay uretimi
  PlayerDamageEvent d1{1, 25.0f}; // player 1, 25 hasar
  PlayerDamageEvent d2{2, 10.0f}; // player 2 (ignore), 10 hasar
  PlayerDamageEvent d3{1, 15.0f}; // player 1, 15 hasar

  CHECK(bus.emit(kEvDamage, d1) == true);
  CHECK(bus.emit(kEvDamage, d2) == true);
  CHECK(bus.emit(kEvHeal) == true); // yuksuz event
  CHECK(bus.emit(kEvDamage, d3) == true);
  CHECK(bus.emit(kEvHeal) == true);

  CHECK(bus.pending_event_count() == 5);

  // Dispatch sonrasi test context guncellenmeli
  bus.dispatch();
  
  CHECK(bus.pending_event_count() == 0);
  CHECK(ctx.received_damage == 40); // 25 + 15
  CHECK(ctx.heal_events == 2);
}
