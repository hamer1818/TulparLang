#include "sim/interp.hpp"

namespace tulpar::engine::sim {

namespace {
float lerp_deg(float a, float b, float t) {
  // En kisa yol: 350 -> 10 derece 20 derecelik bir donus olmali, 340 degil.
  float d = b - a;
  while (d > 180.0f) d -= 360.0f;
  while (d < -180.0f) d += 360.0f;
  return a + d * t;
}
} // namespace

void InterpBuffer::push(const InterpSample &s) {
  buf_[next_ % kCapacity] = s;
  next_++;
  if (count_ < kCapacity) count_++;
}

bool InterpBuffer::sample(double render_time_s, InterpSample *out) const {
  if (count_ < 2) return false;
  const uint32_t oldest = (next_ - count_) % kCapacity;
  const InterpSample &first = buf_[oldest];
  const InterpSample &last = buf_[(next_ - 1) % kCapacity];
  if (render_time_s <= first.time_s) { *out = first; return true; }
  if (render_time_s >= last.time_s) { *out = last; return true; }
  for (uint32_t i = 0; i + 1 < count_; i++) {
    const InterpSample &a = buf_[(oldest + i) % kCapacity];
    const InterpSample &b = buf_[(oldest + i + 1) % kCapacity];
    if (render_time_s >= a.time_s && render_time_s <= b.time_s) {
      const double span = b.time_s - a.time_s;
      const float t = span > 1e-9 ? float((render_time_s - a.time_s) / span) : 0.0f;
      out->time_s = render_time_s;
      out->pos = a.pos + (b.pos - a.pos) * t;
      out->yaw_deg = lerp_deg(a.yaw_deg, b.yaw_deg, t);
      return true;
    }
  }
  return false; // sinir kontrolleri kapsamali; buraya ulasmak bir ic tutarsizliktir
}

} // namespace tulpar::engine::sim
