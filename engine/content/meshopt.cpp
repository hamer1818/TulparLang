#include "content/meshopt.hpp"

#include <cstdlib>
#include <cstring>

#include <meshoptimizer.h>

namespace tulpar::engine::content {

bool mesh_optimize(Arena &arena, ModelMesh &mm, ModelOptStats *stats) {
  if (mm.index_count < 3 || mm.vertex_count == 0) return true;
  const size_t vsz = sizeof(renderer::Vertex);
  const float *pos = &mm.verts[0].pos.x;
  meshopt_VertexCacheStatistics vc0 = meshopt_analyzeVertexCache(mm.indices, mm.index_count, mm.vertex_count, 16, 0, 0);
  meshopt_OverdrawStatistics od0 = meshopt_analyzeOverdraw(mm.indices, mm.index_count, pos, mm.vertex_count, vsz);

  // 1. Tekillestirme (ayni pos/nrm/uv olan vertex'ler birlesir).
  unsigned *remap = static_cast<unsigned *>(std::malloc(sizeof(unsigned) * mm.vertex_count));
  if (!remap) return false;
  const size_t nv = meshopt_generateVertexRemap(remap, mm.indices, mm.index_count, mm.verts, mm.vertex_count, vsz);
  uint32_t *idx = arena.alloc_array<uint32_t>(mm.index_count);
  renderer::Vertex *vb = arena.alloc_array<renderer::Vertex>((uint32_t)nv);
  if (!idx || !vb) { std::free(remap); return false; }
  meshopt_remapIndexBuffer(idx, mm.indices, mm.index_count, remap);
  meshopt_remapVertexBuffer(vb, mm.verts, mm.vertex_count, vsz, remap);
  std::free(remap);
  // 2. Vertex cache, 3. overdraw (kucuk ACMR odunu), 4. fetch sirasi (yerinde).
  uint32_t *tmp = static_cast<uint32_t *>(std::malloc(sizeof(uint32_t) * mm.index_count));
  if (!tmp) return false;
  meshopt_optimizeVertexCache(tmp, idx, mm.index_count, nv);
  meshopt_optimizeOverdraw(idx, tmp, mm.index_count, &vb[0].pos.x, nv, vsz, 1.05f);
  std::free(tmp);
  renderer::Vertex *vb2 = arena.alloc_array<renderer::Vertex>((uint32_t)nv);
  if (!vb2) return false;
  const size_t nv2 = meshopt_optimizeVertexFetch(vb2, idx, mm.index_count, vb, nv, vsz);

  meshopt_VertexCacheStatistics vc1 = meshopt_analyzeVertexCache(idx, mm.index_count, nv2, 16, 0, 0);
  meshopt_OverdrawStatistics od1 = meshopt_analyzeOverdraw(idx, mm.index_count, &vb2[0].pos.x, nv2, vsz);
  if (stats) {
    const float tri = (float)(mm.index_count / 3);
    const float wsum = (float)stats->triangles + tri;
    auto blend = [&](float acc, float v) { return (acc * (float)stats->triangles + v * tri) / wsum; };
    stats->acmr_before = blend(stats->acmr_before, vc0.acmr);
    stats->acmr_after = blend(stats->acmr_after, vc1.acmr);
    stats->overdraw_before = blend(stats->overdraw_before, od0.overdraw);
    stats->overdraw_after = blend(stats->overdraw_after, od1.overdraw);
    stats->vertices_before += mm.vertex_count;
    stats->vertices_after += (uint32_t)nv2;
    stats->triangles += (uint32_t)tri;
  }
  mm.indices = idx;
  mm.verts = vb2;
  mm.vertex_count = (uint32_t)nv2;
  return true;
}

bool mesh_build_lods(Arena &arena, ModelMesh &mm, const float *ratios, uint32_t n, float target_error) {
  if (n > kModelMaxLods) n = kModelMaxLods;
  const size_t vsz = sizeof(renderer::Vertex);
  for (uint32_t i = 0; i < n; i++) {
    mm.lod_indices[i] = nullptr;
    mm.lod_index_count[i] = 0;
    mm.lod_error[i] = 0;
    if (mm.index_count < 12) continue;
    uint32_t *dst = arena.alloc_array<uint32_t>(mm.index_count);
    if (!dst) return false;
    size_t target = (size_t)((float)mm.index_count * ratios[i]);
    target -= target % 3;
    float err = 0;
    const size_t got = meshopt_simplify(dst, mm.indices, mm.index_count, &mm.verts[0].pos.x, mm.vertex_count, vsz, target,
                                        target_error, 0, &err);
    if (got == 0 || got > (size_t)((float)mm.index_count * 0.9f)) continue; // hedefe yaklasamadi: seviye yok
    mm.lod_indices[i] = dst;
    mm.lod_index_count[i] = (uint32_t)got;
    mm.lod_error[i] = err;
  }
  return true;
}

} // namespace tulpar::engine::content
