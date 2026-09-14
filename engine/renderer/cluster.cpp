#include "renderer/cluster.hpp"

#include <cmath>
#include <cstring>

namespace tulpar::engine::renderer {

void cluster_slice_params(const ClusterGrid &g, float *scale, float *bias) {
  const float lr = std::log(g.zfar / g.znear);
  *scale = (float)g.z / lr;
  *bias = -(float)g.z * std::log(g.znear) / lr;
}

uint32_t cluster_slice_of(const ClusterGrid &g, float d) {
  float scale, bias;
  cluster_slice_params(g, &scale, &bias);
  float s = std::floor(std::log(d > 1e-6f ? d : 1e-6f) * scale + bias);
  if (s < 0) s = 0;
  if (s > (float)(g.z - 1)) s = (float)(g.z - 1);
  return (uint32_t)s;
}

void cluster_assign(const Mat4 &view, const Mat4 &proj, const PointLight *lights, uint32_t n, const ClusterGrid &g,
                    uint32_t *masks, ClusterStats *stats) {
  std::memset(masks, 0, sizeof(uint32_t) * g.count());
  ClusterStats st{};
  const uint32_t nl = n > 32 ? 32 : n;
  for (uint32_t li = 0; li < nl; li++) {
    const PointLight &L = lights[li];
    if (L.radius <= 0.0f || L.intensity <= 0.0f) continue;
    Vec4 c4 = view * Vec4{L.pos.x, L.pos.y, L.pos.z, 1.0f};
    const Vec3 c{c4.x, c4.y, c4.z};
    const float r = L.radius;
    // Derinlik araligi (view -z ileri)
    const float dmin = -c.z - r, dmax = -c.z + r;
    if (dmax <= g.znear || dmin >= g.zfar) continue; // tumuyle disarida
    uint32_t s0 = cluster_slice_of(g, dmin < g.znear ? g.znear : dmin);
    uint32_t s1 = cluster_slice_of(g, dmax > g.zfar ? g.zfar : dmax);
    // Ekran araligi: AABB koselerini projekte et; near gerisine tasarsa tum ekran.
    float x0 = 1e9f, x1 = -1e9f, y0 = 1e9f, y1 = -1e9f;
    bool full = false;
    for (int k = 0; k < 8 && !full; k++) {
      Vec3 p{c.x + ((k & 1) ? r : -r), c.y + ((k & 2) ? r : -r), c.z + ((k & 4) ? r : -r)};
      Vec4 q = proj * Vec4{p.x, p.y, p.z, 1.0f};
      if (q.w <= 1e-5f) { full = true; break; }
      float nx = q.x / q.w, ny = q.y / q.w;
      if (nx < x0) x0 = nx; if (nx > x1) x1 = nx;
      if (ny < y0) y0 = ny; if (ny > y1) y1 = ny;
    }
    int tx0 = 0, tx1 = (int)g.x - 1, ty0 = 0, ty1 = (int)g.y - 1;
    if (!full) {
      if (x1 < -1.0f || x0 > 1.0f || y1 < -1.0f || y0 > 1.0f) continue; // ekran disi
      auto tile = [](float ndc, uint32_t tiles) {
        float t = (ndc * 0.5f + 0.5f) * (float)tiles;
        if (t < 0) t = 0;
        if (t > (float)tiles - 1) t = (float)tiles - 1;
        return (int)t;
      };
      tx0 = tile(x0, g.x); tx1 = tile(x1, g.x);
      ty0 = tile(y0, g.y); ty1 = tile(y1, g.y); // NDC y asagi = framebuffer y asagi (proj y'yi cevirdi)
    }
    const uint32_t bit = 1u << li;
    bool any = false;
    for (uint32_t s = s0; s <= s1; s++)
      for (int ty = ty0; ty <= ty1; ty++)
        for (int tx = tx0; tx <= tx1; tx++) {
          masks[(s * g.y + (uint32_t)ty) * g.x + (uint32_t)tx] |= bit;
          any = true;
        }
    if (any) st.lights_visible++;
  }
  if (stats) {
    for (uint32_t i = 0; i < g.count(); i++) {
      if (!masks[i]) continue;
      st.clusters_touched++;
      uint32_t m = masks[i], cnt = 0;
      while (m) { cnt++; m &= m - 1; }
      if (cnt > st.max_lights_in_cluster) st.max_lights_in_cluster = cnt;
    }
    *stats = st;
  }
}

} // namespace tulpar::engine::renderer
