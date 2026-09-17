#include "content/terrain.hpp"

#include "core/math/noise.hpp"

namespace tulpar::engine::content {

void generate_heightmap(const HeightmapConfig &cfg, float *out_heights) {
  for (uint32_t gz = 0; gz < cfg.height; gz++) {
    for (uint32_t gx = 0; gx < cfg.width; gx++) {
      const float wx = (float)gx * cfg.cell_size;
      const float wz = (float)gz * cfg.cell_size;
      const float n = tulpar::engine::fbm_2d(wx * cfg.frequency, wz * cfg.frequency, cfg.seed, cfg.octaves,
                                              cfg.lacunarity, cfg.gain);
      out_heights[gz * cfg.width + gx] = n * cfg.amplitude;
    }
  }
}

float sample_height(const float *heights, uint32_t width, uint32_t height, float cell_size, float world_x,
                     float world_z) {
  float gx = world_x / cell_size;
  float gz = world_z / cell_size;
  const float max_gx = (float)(width - 1);
  const float max_gz = (float)(height - 1);
  gx = gx < 0.0f ? 0.0f : (gx > max_gx ? max_gx : gx);
  gz = gz < 0.0f ? 0.0f : (gz > max_gz ? max_gz : gz);

  const uint32_t ix0 = (uint32_t)gx;
  const uint32_t iz0 = (uint32_t)gz;
  const uint32_t ix1 = ix0 + 1 < width ? ix0 + 1 : ix0;
  const uint32_t iz1 = iz0 + 1 < height ? iz0 + 1 : iz0;
  const float fx = gx - (float)ix0;
  const float fz = gz - (float)iz0;

  const float h00 = heights[iz0 * width + ix0];
  const float h10 = heights[iz0 * width + ix1];
  const float h01 = heights[iz1 * width + ix0];
  const float h11 = heights[iz1 * width + ix1];

  const float a = h00 + (h10 - h00) * fx;
  const float b = h01 + (h11 - h01) * fx;
  return a + (b - a) * fz;
}

namespace {
// [lo-blend,lo]'da 0->1, [lo,hi]'de 1, [hi,hi+blend]'de 1->0. blend<=0 ->
// sert esik (elle kontrol: h=lo/h=hi'de TAM 1, bandin disinda TAM 0).
float trapezoid(float h, float lo, float hi, float blend) {
  if (blend <= 0.0f) return (h >= lo && h <= hi) ? 1.0f : 0.0f;
  const float rise = (h - (lo - blend)) / blend;
  const float fall = ((hi + blend) - h) / blend;
  float w = rise < fall ? rise : fall;
  return w < 0.0f ? 0.0f : (w > 1.0f ? 1.0f : w);
}
} // namespace

void compute_splat_weights(float height, const SplatLayer *layers, uint32_t layer_count, float *out_weights) {
  float sum = 0.0f;
  for (uint32_t i = 0; i < layer_count; i++) {
    out_weights[i] = trapezoid(height, layers[i].height_min, layers[i].height_max, layers[i].blend);
    sum += out_weights[i];
  }
  if (sum > 1e-6f) {
    for (uint32_t i = 0; i < layer_count; i++) out_weights[i] /= sum;
  }
}

} // namespace tulpar::engine::content
