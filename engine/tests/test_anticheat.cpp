// speed_check: bilinen mesafe/sure -> bilinen hiz, esik ustu/alti, ve dt<=0
// negatif kontrolu (bolme-sifir yok, yanlis pozitif yok).
#include <cmath>

#include "sim/anticheat.hpp"
#include "tests/test.hpp"

using namespace tulpar::engine;
using namespace tulpar::engine::test;

namespace {
bool approx(float a, float b, float eps = 1e-3f) { return std::fabs(a - b) < eps; }
} // namespace

ENGINE_TEST(anticheat_computes_known_speed) {
  // 10 birim, 2 saniyede -> 5 birim/s.
  sim::SpeedCheckResult r = sim::speed_check(Vec3{0, 0, 0}, Vec3{10, 0, 0}, 2.0f, 100.0f);
  CHECK(approx(r.implied_speed, 5.0f));
  CHECK(!r.suspicious);
}

ENGINE_TEST(anticheat_flags_teleport_above_max_speed) {
  // 1000 birim, 1 tick (1/60 s) -> ~60000 birim/s, herhangi bir mesru max_speed'i asar.
  sim::SpeedCheckResult r = sim::speed_check(Vec3{0, 0, 0}, Vec3{1000, 0, 0}, 1.0f / 60.0f, 50.0f);
  CHECK(r.suspicious);
  CHECK(r.implied_speed > 50.0f);
}

ENGINE_TEST(anticheat_does_not_flag_at_exact_threshold_boundary) {
  // Tam esikte (>= degil, > kontrolu): suspicious OLMAMALI.
  sim::SpeedCheckResult r = sim::speed_check(Vec3{0, 0, 0}, Vec3{10, 0, 0}, 1.0f, 10.0f);
  CHECK(approx(r.implied_speed, 10.0f));
  CHECK(!r.suspicious);
}

ENGINE_TEST(anticheat_zero_or_negative_dt_never_flags) {
  // Negatif kontrol: yinelenen paket/bozuk zaman damgasi hile KANITI sayilmamali.
  sim::SpeedCheckResult r1 = sim::speed_check(Vec3{0, 0, 0}, Vec3{1000, 0, 0}, 0.0f, 1.0f);
  CHECK(!r1.suspicious && r1.implied_speed == 0.0f);
  sim::SpeedCheckResult r2 = sim::speed_check(Vec3{0, 0, 0}, Vec3{1000, 0, 0}, -1.0f, 1.0f);
  CHECK(!r2.suspicious && r2.implied_speed == 0.0f);
}
