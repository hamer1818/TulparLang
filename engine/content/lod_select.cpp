#include "content/lod_select.hpp"

namespace tulpar::engine::content {

uint32_t select_lod_traditional(float distance, const float *thresholds, uint32_t lod_count) {
  if (lod_count == 0) return 0;
  uint32_t lod = 0;
  for (uint32_t i = 0; i + 1 < lod_count; i++) {
    if (distance < thresholds[i]) break;
    lod = i + 1;
  }
  return lod;
}

DitheredLod select_lod_dithered(float distance, const float *thresholds, uint32_t lod_count, float blend_range) {
  DitheredLod r{};
  if (lod_count == 0) return r;
  const float half = blend_range * 0.5f;
  uint32_t base = 0;
  for (uint32_t i = 0; i + 1 < lod_count; i++) {
    if (distance < thresholds[i] - half) {
      base = i;
      break;
    }
    if (distance < thresholds[i] + half) {
      r.lod_a = i;
      r.lod_b = i + 1;
      float t = (distance - (thresholds[i] - half)) / blend_range;
      r.blend = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
      return r;
    }
    base = i + 1;
  }
  r.lod_a = base;
  r.lod_b = base;
  r.blend = 0.0f;
  return r;
}

} // namespace tulpar::engine::content
