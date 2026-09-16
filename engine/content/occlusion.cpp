#include "content/occlusion.hpp"

namespace tulpar::engine::content {

namespace {
void clamp_rect(const DepthBuffer &db, float min_x, float min_y, float max_x, float max_y, uint32_t *x0,
                 uint32_t *y0, uint32_t *x1, uint32_t *y1) {
  float cx0 = min_x < 0.0f ? 0.0f : min_x;
  float cy0 = min_y < 0.0f ? 0.0f : min_y;
  float cx1 = max_x > (float)db.width ? (float)db.width : max_x;
  float cy1 = max_y > (float)db.height ? (float)db.height : max_y;
  *x0 = (uint32_t)cx0;
  *y0 = (uint32_t)cy0;
  *x1 = cx1 > cx0 ? (uint32_t)cx1 : *x0;
  *y1 = cy1 > cy0 ? (uint32_t)cy1 : *y0;
}
} // namespace

void clear_depth_buffer(DepthBuffer &db, float far_value) {
  for (uint32_t i = 0; i < db.width * db.height; i++) db.cells[i] = far_value;
}

void rasterize_occluder_rect(DepthBuffer &db, float min_x, float min_y, float max_x, float max_y, float depth) {
  uint32_t x0, y0, x1, y1;
  clamp_rect(db, min_x, min_y, max_x, max_y, &x0, &y0, &x1, &y1);
  for (uint32_t y = y0; y < y1; y++)
    for (uint32_t x = x0; x < x1; x++) {
      float &c = db.cells[y * db.width + x];
      if (depth < c) c = depth;
    }
}

bool is_occluded(const DepthBuffer &db, float min_x, float min_y, float max_x, float max_y, float depth) {
  uint32_t x0, y0, x1, y1;
  clamp_rect(db, min_x, min_y, max_x, max_y, &x0, &y0, &x1, &y1);
  if (x0 >= x1 || y0 >= y1) return false; // tampon disi/bos: guvenli varsayim, GORUNUR
  const float eps = 1e-5f;
  for (uint32_t y = y0; y < y1; y++)
    for (uint32_t x = x0; x < x1; x++)
      if (db.cells[y * db.width + x] >= depth - eps) return false; // bu hucrede occluder YOK/daha uzak
  return true;
}

} // namespace tulpar::engine::content
