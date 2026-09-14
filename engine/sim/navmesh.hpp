// L4 SIMULATION — Navmesh: Recast (bake) + Detour (sorgu) uzerine ince sarmalayici.
// Plan §1.6: navmesh build'de bake edilir, runtime yalniz sorgular. Bake
// (build()) ayirma yapar — sahne derleyicisinin isi; sorgu (find_path,
// raycast, nearest) dugum havuzunu init'te alir ve icinde AYIRMA YAPMAZ
// (Detour ayirici kancasiyla olculur, test 0 ister). Recast/Detour tipleri
// disari sizmaz.
#pragma once
#include <cstddef>
#include <cstdint>

#include "core/math/vec.hpp"
#include "core/memory/arena.hpp"

namespace tulpar::engine::sim {

struct NavMeshBuildConfig {
  float cell_size = 0.3f;
  float cell_height = 0.2f;
  float agent_height = 2.0f;
  float agent_radius = 0.6f;
  float agent_max_climb = 0.9f;
  float agent_max_slope_deg = 45.0f;
  float region_min_size = 8.0f;
  float region_merge_size = 20.0f;
  float edge_max_len = 12.0f;
  float edge_max_error = 1.3f;
  int verts_per_poly = 6;
  float detail_sample_dist = 6.0f;
  float detail_sample_max_error = 1.0f;
};

struct NavMeshStats {
  uint32_t polys = 0;
  uint32_t verts = 0;
  size_t data_bytes = 0;      // Detour navmesh verisi (bake ciktisi)
  uint64_t build_allocs = 0;  // bake sirasinda rc/dt ayirmasi (bilgi)
  uint64_t query_allocs = 0;  // son sorguda dt ayirmasi (0 olmali)
};

class NavMesh {
public:
  // BAKE: ucgen corbasi (y yukari) -> Detour navmesh. Sahne derleyicisi / test.
  bool build(const float *verts_xyz, int nverts, const int *tris, int ntris, const NavMeshBuildConfig &cfg);
  // RUNTIME: sorgu nesnesi + dugum havuzu (bir kez).
  bool init_query(int max_nodes = 2048);
  void shutdown();
  bool ok() const { return mesh_ != nullptr; }

  // Duz yol (kose noktalari). Donus: nokta sayisi; 0 = yol yok. Kismi yol
  // (hedefe ulasilamadi) `partial` ile bildirilir.
  int find_path(Vec3 from, Vec3 to, Vec3 *out, int max_points, bool *partial = nullptr);
  // Navmesh uzerinde dogru gorus: engel varsa t (0..1) ve true.
  bool raycast(Vec3 from, Vec3 to, float *t_hit);
  // Navmesh'e en yakin nokta.
  bool nearest_point(Vec3 p, Vec3 *out);

  NavMeshStats stats() const { return stats_; }
  uint64_t data_hash() const; // bake ciktisinin FNV-1a'si (belirlenimlilik bilgisi)

private:
  struct Impl;
  Impl *impl_ = nullptr;
  void *mesh_ = nullptr;   // dtNavMesh*
  void *query_ = nullptr;  // dtNavMeshQuery*
  NavMeshStats stats_{};
};

} // namespace tulpar::engine::sim
