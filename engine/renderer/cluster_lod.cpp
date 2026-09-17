#include "renderer/cluster_lod.hpp"

#include <cmath>

namespace tulpar::engine::renderer {

namespace {

// Kesme kuralinin tek satirlik hali (bkz. baslikteki sozlesme).
inline bool draw_cluster(const Cluster &c, const LodView &v) {
  // (1) BU seviye yeterince iyi mi?
  if (cluster_screen_error(c.current, v) > v.error_threshold_px) return false;
  // (2) Bir UST (kaba) seviye YETERSIZ mi? Yeterliyse onu cizecegiz, bunu degil.
  return cluster_screen_error(c.coarser, v) > v.error_threshold_px;
}

} // namespace

float cluster_screen_error(const ClusterErrorMetric &m, const LodView &v) {
  const Vec3 d = m.center - v.camera_position;
  const float dist = std::sqrt(dot(d, d));
  // Kurenin kameraya en yakin noktasi, en az znear.
  float near_dist = dist - m.radius;
  if (!(near_dist > v.znear)) near_dist = v.znear;
  // error == FLT_MAX (son seviye) ise sonuc tasip +inf olur: ISTENEN budur,
  // cunku esigi her zaman gecer ve en kaba temsil cizilebilir kalir.
  return m.error / near_dist * (v.proj * 0.5f) * v.screen_height;
}

uint32_t cluster_lod_select(const ClusterLodMesh &mesh, const LodView &view,
                            uint32_t *out_clusters, uint32_t max_out) {
  if (!mesh.clusters || mesh.cluster_count == 0) return 0;

  uint32_t found = 0;
  // Tarama sirasi kume indeksi sirasi: SONUC SIRASI her platformda AYNI.
  for (uint32_t i = 0; i < mesh.cluster_count; i++) {
    if (!draw_cluster(mesh.clusters[i], view)) continue;
    if (found < max_out && out_clusters) out_clusters[found] = i;
    found++; // tampon dolsa da SAYMAYA devam -> cagiran tasmayi gorur
  }
  return found;
}

uint32_t cluster_lod_select_culled(const ClusterLodMesh &mesh, const LodView &view,
                                   const Frustum &frustum, uint32_t *out_clusters,
                                   uint32_t max_out) {
  if (!mesh.clusters || mesh.cluster_count == 0) return 0;

  uint32_t found = 0;
  for (uint32_t i = 0; i < mesh.cluster_count; i++) {
    const Cluster &c = mesh.clusters[i];
    // Frustum testi ONCE: seviye secimi iki karekok + iki bolme yapar,
    // duzlem testi yalnizca nokta carpimi -- ucuz olan once elemeli.
    Sphere s;
    s.center = c.center;
    s.radius = c.radius;
    if (!intersects(frustum, s)) continue;
    if (!draw_cluster(c, view)) continue;

    if (found < max_out && out_clusters) out_clusters[found] = i;
    found++;
  }
  return found;
}

} // namespace tulpar::engine::renderer
