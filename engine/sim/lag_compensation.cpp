#include "sim/lag_compensation.hpp"

namespace tulpar::engine::sim {

void LagCompensator::record(uint32_t track, double time_s, Vec3 center, float radius) {
  if (track >= kMaxTracks) return;
  Track &tr = tracks_[track];
  tr.active = true;
  tr.buf[tr.next % kHistoryPerTrack] = HitboxSample{time_s, center, radius};
  tr.next++;
  if (tr.count < kHistoryPerTrack) tr.count++;
}

bool LagCompensator::rewind(uint32_t track, double now_s, double render_time_s, HitboxSample *out) const {
  if (track >= kMaxTracks) return false;
  const Track &tr = tracks_[track];
  if (!tr.active || tr.count < 2) return false;
  if (now_s - render_time_s > max_rewind_s_) return false; // istismar siniri ("peeker's advantage")
  const uint32_t oldest = (tr.next - tr.count) % kHistoryPerTrack;
  const HitboxSample &first = tr.buf[oldest];
  const HitboxSample &last = tr.buf[(tr.next - 1) % kHistoryPerTrack];
  if (render_time_s <= first.time_s) { *out = first; return true; }
  if (render_time_s >= last.time_s) { *out = last; return true; }
  for (uint32_t i = 0; i + 1 < tr.count; i++) {
    const HitboxSample &a = tr.buf[(oldest + i) % kHistoryPerTrack];
    const HitboxSample &b = tr.buf[(oldest + i + 1) % kHistoryPerTrack];
    if (render_time_s >= a.time_s && render_time_s <= b.time_s) {
      const double span = b.time_s - a.time_s;
      const float t = span > 1e-9 ? float((render_time_s - a.time_s) / span) : 0.0f;
      out->time_s = render_time_s;
      out->center = a.center + (b.center - a.center) * t;
      out->radius = a.radius + (b.radius - a.radius) * t;
      return true;
    }
  }
  return false; // sinir kontrolleri kapsamali; buraya ulasmak bir ic tutarsizliktir
}

bool LagCompensator::raycast(const Ray &ray, double now_s, double render_time_s, uint32_t *hit_track,
                              float *hit_t) const {
  bool found = false;
  float best_t = 0.0f;
  uint32_t best_track = 0;
  for (uint32_t i = 0; i < kMaxTracks; i++) {
    if (!tracks_[i].active) continue;
    HitboxSample hb;
    if (!rewind(i, now_s, render_time_s, &hb)) continue;
    float t;
    if (!intersect(ray, Sphere{hb.center, hb.radius}, &t)) continue;
    if (!found || t < best_t) {
      found = true;
      best_t = t;
      best_track = i;
    }
  }
  if (found) {
    if (hit_track) *hit_track = best_track;
    if (hit_t) *hit_t = best_t;
  }
  return found;
}

void LagCompensator::clear_track(uint32_t track) {
  if (track >= kMaxTracks) return;
  tracks_[track] = Track{};
}

} // namespace tulpar::engine::sim
