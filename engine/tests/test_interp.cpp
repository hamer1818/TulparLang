// InterpBuffer: bilinen ornekler arasinda bilinen ara degerler + sinir
// kilitleme (ekstrapolasyon YOK) + en kisa yol aci enterpolasyonu.
#include <cmath>

#include "sim/interp.hpp"
#include "tests/test.hpp"

using namespace tulpar::engine;
using namespace tulpar::engine::test;

namespace {
bool approx(float a, float b, float eps = 1e-3f) { return std::fabs(a - b) < eps; }
} // namespace

ENGINE_TEST(interp_needs_at_least_two_samples) {
  sim::InterpBuffer buf;
  sim::InterpSample out;
  CHECK(!buf.sample(0.0, &out)); // 0 ornek
  buf.push({0.0, Vec3{0, 0, 0}, 0.0f});
  CHECK(!buf.sample(0.0, &out)); // 1 ornek: enterpolasyon icin yetersiz
}

ENGINE_TEST(interp_linear_between_two_samples) {
  sim::InterpBuffer buf;
  buf.push({0.0, Vec3{0, 0, 0}, 0.0f});
  buf.push({2.0, Vec3{10, 0, 0}, 0.0f});
  sim::InterpSample out;
  CHECK(buf.sample(1.0, &out));
  CHECK(approx(out.pos.x, 5.0f)); // tam ortada
  CHECK(buf.sample(0.5, &out));
  CHECK(approx(out.pos.x, 2.5f)); // %25 noktasi
}

ENGINE_TEST(interp_clamps_outside_range_no_extrapolation) {
  sim::InterpBuffer buf;
  buf.push({5.0, Vec3{1, 0, 0}, 0.0f});
  buf.push({10.0, Vec3{2, 0, 0}, 0.0f});
  sim::InterpSample out;
  CHECK(buf.sample(0.0, &out) && approx(out.pos.x, 1.0f));   // erken: ilk ornege kilitli
  CHECK(buf.sample(100.0, &out) && approx(out.pos.x, 2.0f)); // gec: son ornege kilitli
}

ENGINE_TEST(interp_yaw_takes_shortest_path_across_wrap) {
  sim::InterpBuffer buf;
  buf.push({0.0, Vec3{0, 0, 0}, 350.0f});
  buf.push({2.0, Vec3{0, 0, 0}, 10.0f}); // 350 -> 10: en kisa yol +20 (360/0 uzerinden), -340 DEGIL
  sim::InterpSample out;
  CHECK(buf.sample(1.0, &out));
  // Orta nokta 0 (=360) civarinda olmali, 180 DEGIL.
  const float d = std::fmod(out.yaw_deg + 360.0f, 360.0f);
  CHECK(d < 5.0f || d > 355.0f);
}

ENGINE_TEST(interp_ring_evicts_oldest_and_reclamps) {
  sim::InterpBuffer buf;
  for (uint32_t i = 0; i < sim::InterpBuffer::kCapacity + 4; i++)
    buf.push({(double)i, Vec3{(float)i, 0, 0}, 0.0f});
  sim::InterpSample out;
  // t=0..3 halkadan dustu; artik en eski ornek t=4 olmali (kCapacity+4 push,
  // en yeni t=kCapacity+3, en eski hayatta kalan t=4).
  CHECK(buf.sample(0.0, &out) && approx(out.pos.x, 4.0f)); // dusen bir zamana sorulunca YENI en eskiye kilitlenir
}
