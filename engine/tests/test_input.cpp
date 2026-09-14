// Sanal joystick: sol yarim surukleme -> hareket vektoru; sag yarim tap ->
// eylem; sag surukleme -> bakis; parmak kalkinca sifirlanir.
#include <cmath>
#include <cstdio>

#include "app/virtual_stick.hpp"
#include "tests/test.hpp"

using namespace tulpar::engine;
using namespace tulpar::engine::test;

ENGINE_TEST(input_virtual_stick_move_look_and_tap) {
  platform::TouchState t;
  t.width = 1000; t.height = 500;
  app::VirtualStick vs;
  vs.stick_radius_px = 100;
  uint64_t now = 1000000000ull;
  // Sol yarimda bas: kokte, hareket 0
  t.begin(7, 200, 300);
  vs.update(t, now);
  bool origin_zero = vs.move_active && std::fabs(vs.move.x) < 1e-6f && std::fabs(vs.move.y) < 1e-6f;
  CHECK(origin_zero);
  // 50 px saga, 50 px yukari (ekran y asagi) -> (0.5, +0.5)
  t.move(7, 250, 250);
  vs.update(t, now += 16000000);
  bool half = std::fabs(vs.move.x - 0.5f) < 1e-5f && std::fabs(vs.move.y - 0.5f) < 1e-5f;
  CHECK(half);
  // 300 px: doygun, uzunluk 1
  t.move(7, 500, 300);
  vs.update(t, now += 16000000);
  bool sat = std::fabs(std::sqrt(vs.move.x * vs.move.x + vs.move.y * vs.move.y) - 1.0f) < 1e-5f && vs.move.x > 0.99f;
  CHECK(sat);
  // Sag yarimda ikinci parmak: bakis suruklemesi
  t.begin(9, 800, 250);
  vs.update(t, now += 16000000);
  t.move(9, 830, 260);
  vs.update(t, now += 16000000);
  bool look = vs.look_active && std::fabs(vs.look_delta.x - 30) < 1e-5f && std::fabs(vs.look_delta.y - 10) < 1e-5f;
  CHECK(look);
  // Sol parmak kalkar: hareket sifir
  t.end(7);
  vs.update(t, now += 16000000);
  bool released = !vs.move_active && vs.move.x == 0 && vs.move.y == 0;
  CHECK(released);
  // Sag parmak cok hareket ettiyse kalkinca EYLEM DEGIL
  t.end(9);
  vs.update(t, now += 16000000);
  CHECK(!vs.action);
  // Kisa tap: eylem
  t.begin(11, 900, 400);
  vs.update(t, now += 16000000);
  t.end(11);
  vs.update(t, now += 100000000);
  CHECK(vs.action);
  // Sonraki karede eylem tek kare surer
  vs.update(t, now += 16000000);
  CHECK(!vs.action);
  std::printf("    [bilgi] joystick: kok 0, yarim %.2f, doygun 1, tap tek kare\n", 0.5f);
}
