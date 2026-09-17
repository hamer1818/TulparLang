#include "renderer/cluster_cull.hpp"

#include <cmath>
#include <cstring>

namespace tulpar::engine::renderer {

namespace {

// rhi/shaders/cluster_cull.comp'taki screen_error() ile AYNI ifade,
// AYNI SIRADA. Sira onemli: float carpma/bolme birlesmeli degildir, farkli
// gruplanirsa son bit farkli cikabilir ve CPU/GPU sonuclari esikte ayrisir.
float screen_error(Vec3 center, float radius, float error, const ClusterCullPush &p) {
  const Vec3 d = center - p.camera;
  const float dist = std::sqrt(dot(d, d));
  float near_dist = dist - radius;
  if (!(near_dist > p.znear)) near_dist = p.znear;
  return error / near_dist * (p.proj * 0.5f) * p.screen_height;
}

// Shader'daki outside_frustum() ile AYNI test.
bool outside_frustum(Vec3 center, float radius, const ClusterCullPush &p) {
  for (int i = 0; i < 6; i++) {
    if (dot(p.planes[i].normal, center) + p.planes[i].d + radius < 0.0f) return true;
  }
  return false;
}

} // namespace

GpuCluster to_gpu_cluster(const Cluster &c) {
  GpuCluster g;
  g.center_x = c.center.x;
  g.center_y = c.center.y;
  g.center_z = c.center.z;
  g.radius = c.radius;
  g.cur_x = c.current.center.x;
  g.cur_y = c.current.center.y;
  g.cur_z = c.current.center.z;
  g.cur_radius = c.current.radius;
  g.cur_error = c.current.error;
  g.coa_x = c.coarser.center.x;
  g.coa_y = c.coarser.center.y;
  g.coa_z = c.coarser.center.z;
  g.coa_radius = c.coarser.radius;
  g.coa_error = c.coarser.error;
  g.index_offset = c.index_offset;
  g.index_count = c.index_count;
  g.geometry = c.geometry;
  g.level = c.level;
  return g;
}

ClusterCullPush make_cull_push(const LodView &view, const Frustum &frustum, uint32_t cluster_count) {
  ClusterCullPush p{};
  std::memcpy(p.planes, frustum.planes, sizeof(p.planes)); // duzen birebir ayni
  p.camera = view.camera_position;
  p.proj = view.proj;
  p.znear = view.znear;
  p.screen_height = view.screen_height;
  p.threshold_px = view.error_threshold_px;
  p.cluster_count = cluster_count;
  return p;
}

bool cluster_cull_reference(const ClusterLodMesh &mesh, const ClusterCullPush &push,
                            DrawIndexedIndirectCommand *out_draws, uint32_t max_draws) {
  if (!out_draws) return false;
  // Shader her kume icin BIR komut yazar; tampon kucukse sessizce yarim
  // sonuc uretmek yerine basarisiz oluyoruz.
  const uint32_t n = mesh.cluster_count;
  if (n > max_draws) return false;
  if (!mesh.clusters || n == 0) return n == 0;

  for (uint32_t i = 0; i < n; i++) {
    const Cluster &c = mesh.clusters[i];

    // Komut HER ZAMAN yazilir; eleme instance_count = 0 ile ifade edilir.
    // Yazmamak, tamponda onceki karenin verisini birakirdi.
    DrawIndexedIndirectCommand cmd;
    cmd.index_count = c.index_count;
    cmd.instance_count = 0;
    cmd.first_index = c.index_offset;
    cmd.vertex_offset = 0;
    cmd.first_instance = 0;

    if (!outside_frustum(c.center, c.radius, push)) {
      const float cur = screen_error(c.current.center, c.current.radius, c.current.error, push);
      const float coa = screen_error(c.coarser.center, c.coarser.radius, c.coarser.error, push);
      if (cur <= push.threshold_px && coa > push.threshold_px) cmd.instance_count = 1;
    }

    out_draws[i] = cmd;
  }
  return true;
}

} // namespace tulpar::engine::renderer
