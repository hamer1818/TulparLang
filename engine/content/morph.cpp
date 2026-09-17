#include "content/morph.hpp"

#include <cstddef> // size_t — libc++ (macOS) gecisli getirmiyor

namespace tulpar::engine::content {

void blend_morph_targets(const Vec3 *base_positions, uint32_t vertex_count, const Vec3 *target_deltas,
                          const float *weights, uint32_t target_count, Vec3 *out_positions) {
  for (uint32_t v = 0; v < vertex_count; v++) out_positions[v] = base_positions[v];
  for (uint32_t t = 0; t < target_count; t++) {
    const float w = weights[t];
    if (w == 0.0f) continue; // sifir agirlikli hedefi atla (hizlandirma, sonucu degistirmez)
    const Vec3 *deltas = target_deltas + (size_t)t * vertex_count;
    for (uint32_t v = 0; v < vertex_count; v++) out_positions[v] = out_positions[v] + deltas[v] * w;
  }
}

} // namespace tulpar::engine::content
