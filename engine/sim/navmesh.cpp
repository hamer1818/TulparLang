#include "sim/navmesh.hpp"

#include <DetourAlloc.h>
#include <DetourNavMesh.h>
#include <DetourNavMeshBuilder.h>
#include <DetourNavMeshQuery.h>
#include <Recast.h>
#include <RecastAlloc.h>

#include <atomic>
#include <cmath>
#include <cstdlib>
#include <cstring>

#include "platform/fatal.hpp"

namespace tulpar::engine::sim {

namespace {
std::atomic<uint64_t> g_dt_allocs{0}, g_rc_allocs{0};
void *dt_alloc(size_t n, dtAllocHint) { g_dt_allocs.fetch_add(1, std::memory_order_relaxed); return std::malloc(n); }
void dt_free(void *p) { std::free(p); }
void *rc_alloc(size_t n, rcAllocHint) { g_rc_allocs.fetch_add(1, std::memory_order_relaxed); return std::malloc(n); }
void rc_free(void *p) { std::free(p); }
bool g_hooks = false;
void install_hooks() {
  if (g_hooks) return;
  dtAllocSetCustom(dt_alloc, dt_free);
  rcAllocSetCustom(rc_alloc, rc_free);
  g_hooks = true;
}

uint64_t fnv1a(const void *p, size_t n, uint64_t h) {
  const uint8_t *b = static_cast<const uint8_t *>(p);
  for (size_t i = 0; i < n; i++) { h ^= b[i]; h *= 0x100000001b3ull; }
  return h;
}

// Sessiz Recast baglami: log yok, zamanlama yok.
class QuietCtx : public rcContext {
public:
  QuietCtx() : rcContext(false) {}
};
} // namespace

struct NavMesh::Impl {
  unsigned char *data = nullptr; // dtNavMesh sahiplenir (DT_TILE_FREE_DATA)
  int data_size = 0;
  dtQueryFilter filter;
};

bool NavMesh::build(const float *verts, int nverts, const int *tris, int ntris, const NavMeshBuildConfig &cfg) {
  install_hooks();
  shutdown();
  uint64_t a0 = g_rc_allocs.load() + g_dt_allocs.load();
  impl_ = new Impl();

  float bmin[3], bmax[3];
  rcCalcBounds(verts, nverts, bmin, bmax);

  rcConfig c;
  std::memset(&c, 0, sizeof c);
  c.cs = cfg.cell_size;
  c.ch = cfg.cell_height;
  c.walkableSlopeAngle = cfg.agent_max_slope_deg;
  c.walkableHeight = (int)std::ceil(cfg.agent_height / c.ch);
  c.walkableClimb = (int)std::floor(cfg.agent_max_climb / c.ch);
  c.walkableRadius = (int)std::ceil(cfg.agent_radius / c.cs);
  c.maxEdgeLen = (int)(cfg.edge_max_len / c.cs);
  c.maxSimplificationError = cfg.edge_max_error;
  c.minRegionArea = (int)(cfg.region_min_size * cfg.region_min_size);
  c.mergeRegionArea = (int)(cfg.region_merge_size * cfg.region_merge_size);
  c.maxVertsPerPoly = cfg.verts_per_poly;
  c.detailSampleDist = cfg.detail_sample_dist < 0.9f ? 0 : c.cs * cfg.detail_sample_dist;
  c.detailSampleMaxError = c.ch * cfg.detail_sample_max_error;
  rcVcopy(c.bmin, bmin);
  rcVcopy(c.bmax, bmax);
  rcCalcGridSize(c.bmin, c.bmax, c.cs, &c.width, &c.height);

  QuietCtx ctx;
  bool ok = false;
  rcHeightfield *hf = rcAllocHeightfield();
  rcCompactHeightfield *chf = rcAllocCompactHeightfield();
  rcContourSet *cset = rcAllocContourSet();
  rcPolyMesh *pmesh = rcAllocPolyMesh();
  rcPolyMeshDetail *dmesh = rcAllocPolyMeshDetail();
  unsigned char *areas = static_cast<unsigned char *>(rcAlloc(ntris, RC_ALLOC_TEMP));
  do {
    if (!hf || !chf || !cset || !pmesh || !dmesh || !areas) break;
    if (!rcCreateHeightfield(&ctx, *hf, c.width, c.height, c.bmin, c.bmax, c.cs, c.ch)) break;
    std::memset(areas, 0, ntris);
    rcMarkWalkableTriangles(&ctx, c.walkableSlopeAngle, verts, nverts, tris, ntris, areas);
    if (!rcRasterizeTriangles(&ctx, verts, nverts, tris, areas, ntris, *hf, c.walkableClimb)) break;
    rcFilterLowHangingWalkableObstacles(&ctx, c.walkableClimb, *hf);
    rcFilterLedgeSpans(&ctx, c.walkableHeight, c.walkableClimb, *hf);
    rcFilterWalkableLowHeightSpans(&ctx, c.walkableHeight, *hf);
    if (!rcBuildCompactHeightfield(&ctx, c.walkableHeight, c.walkableClimb, *hf, *chf)) break;
    if (!rcErodeWalkableArea(&ctx, c.walkableRadius, *chf)) break;
    if (!rcBuildDistanceField(&ctx, *chf)) break;
    if (!rcBuildRegions(&ctx, *chf, 0, c.minRegionArea, c.mergeRegionArea)) break;
    if (!rcBuildContours(&ctx, *chf, c.maxSimplificationError, c.maxEdgeLen, *cset)) break;
    if (!rcBuildPolyMesh(&ctx, *cset, c.maxVertsPerPoly, *pmesh)) break;
    if (!rcBuildPolyMeshDetail(&ctx, *pmesh, *chf, c.detailSampleDist, c.detailSampleMaxError, *dmesh)) break;
    for (int i = 0; i < pmesh->npolys; i++) {
      if (pmesh->areas[i] == RC_WALKABLE_AREA) pmesh->areas[i] = 0; // tek alan tipi
      pmesh->flags[i] = 1;                                           // yuruyulebilir
    }
    dtNavMeshCreateParams p;
    std::memset(&p, 0, sizeof p);
    p.verts = pmesh->verts;
    p.vertCount = pmesh->nverts;
    p.polys = pmesh->polys;
    p.polyAreas = pmesh->areas;
    p.polyFlags = pmesh->flags;
    p.polyCount = pmesh->npolys;
    p.nvp = pmesh->nvp;
    p.detailMeshes = dmesh->meshes;
    p.detailVerts = dmesh->verts;
    p.detailVertsCount = dmesh->nverts;
    p.detailTris = dmesh->tris;
    p.detailTriCount = dmesh->ntris;
    p.walkableHeight = cfg.agent_height;
    p.walkableRadius = cfg.agent_radius;
    p.walkableClimb = cfg.agent_max_climb;
    rcVcopy(p.bmin, pmesh->bmin);
    rcVcopy(p.bmax, pmesh->bmax);
    p.cs = c.cs;
    p.ch = c.ch;
    p.buildBvTree = true;
    if (!dtCreateNavMeshData(&p, &impl_->data, &impl_->data_size)) break;
    dtNavMesh *mesh = dtAllocNavMesh();
    if (!mesh) break;
    if (dtStatusFailed(mesh->init(impl_->data, impl_->data_size, DT_TILE_FREE_DATA))) {
      dtFreeNavMesh(mesh);
      break;
    }
    mesh_ = mesh;
    stats_.polys = (uint32_t)pmesh->npolys;
    stats_.verts = (uint32_t)pmesh->nverts;
    stats_.data_bytes = (size_t)impl_->data_size;
    ok = true;
  } while (false);
  rcFree(areas);
  rcFreePolyMeshDetail(dmesh);
  rcFreePolyMesh(pmesh);
  rcFreeContourSet(cset);
  rcFreeCompactHeightfield(chf);
  rcFreeHeightField(hf);
  stats_.build_allocs = g_rc_allocs.load() + g_dt_allocs.load() - a0;
  if (!ok) {
    delete impl_;
    impl_ = nullptr;
  }
  return ok;
}

bool NavMesh::init_query(int max_nodes) {
  if (!mesh_) return false;
  dtNavMeshQuery *q = dtAllocNavMeshQuery();
  if (!q || dtStatusFailed(q->init(static_cast<dtNavMesh *>(mesh_), max_nodes))) {
    if (q) dtFreeNavMeshQuery(q);
    return false;
  }
  query_ = q;
  impl_->filter.setIncludeFlags(0xFFFF);
  impl_->filter.setExcludeFlags(0);
  return true;
}

void NavMesh::shutdown() {
  if (query_) dtFreeNavMeshQuery(static_cast<dtNavMeshQuery *>(query_));
  if (mesh_) dtFreeNavMesh(static_cast<dtNavMesh *>(mesh_)); // DT_TILE_FREE_DATA: veriyi de birakir
  query_ = nullptr;
  mesh_ = nullptr;
  delete impl_;
  impl_ = nullptr;
}

bool NavMesh::nearest_point(Vec3 p, Vec3 *out) {
  if (!query_) return false;
  dtNavMeshQuery *q = static_cast<dtNavMeshQuery *>(query_);
  const float ext[3] = {2.0f, 4.0f, 2.0f};
  const float pos[3] = {p.x, p.y, p.z};
  dtPolyRef ref = 0;
  float nearest[3];
  if (dtStatusFailed(q->findNearestPoly(pos, ext, &impl_->filter, &ref, nearest)) || !ref) return false;
  *out = Vec3{nearest[0], nearest[1], nearest[2]};
  return true;
}

int NavMesh::find_path(Vec3 from, Vec3 to, Vec3 *out, int max_points, bool *partial) {
  if (!query_) return 0;
  uint64_t a0 = g_dt_allocs.load();
  dtNavMeshQuery *q = static_cast<dtNavMeshQuery *>(query_);
  const float ext[3] = {2.0f, 4.0f, 2.0f};
  const float s[3] = {from.x, from.y, from.z};
  const float e[3] = {to.x, to.y, to.z};
  dtPolyRef sref = 0, eref = 0;
  float sp[3], ep[3];
  q->findNearestPoly(s, ext, &impl_->filter, &sref, sp);
  q->findNearestPoly(e, ext, &impl_->filter, &eref, ep);
  if (!sref || !eref) return 0;
  dtPolyRef polys[256];
  int npolys = 0;
  dtStatus st = q->findPath(sref, eref, sp, ep, &impl_->filter, polys, &npolys, 256);
  if (dtStatusFailed(st) || npolys == 0) return 0;
  bool part = (st & DT_PARTIAL_RESULT) != 0;
  // Kismi yolda hedef son poligonun icine cekilir.
  float end[3] = {ep[0], ep[1], ep[2]};
  if (polys[npolys - 1] != eref) {
    q->closestPointOnPoly(polys[npolys - 1], ep, end, nullptr);
    part = true;
  }
  float straight[64 * 3];
  unsigned char flags[64];
  dtPolyRef refs[64];
  int n = 0;
  int cap = max_points < 64 ? max_points : 64;
  q->findStraightPath(sp, end, polys, npolys, straight, flags, refs, &n, cap, 0);
  for (int i = 0; i < n; i++) out[i] = Vec3{straight[i * 3], straight[i * 3 + 1], straight[i * 3 + 2]};
  if (partial) *partial = part;
  stats_.query_allocs = g_dt_allocs.load() - a0;
  return n;
}

bool NavMesh::raycast(Vec3 from, Vec3 to, float *t_hit) {
  if (!query_) return false;
  dtNavMeshQuery *q = static_cast<dtNavMeshQuery *>(query_);
  const float ext[3] = {2.0f, 4.0f, 2.0f};
  const float s[3] = {from.x, from.y, from.z};
  const float e[3] = {to.x, to.y, to.z};
  dtPolyRef sref = 0;
  float sp[3];
  q->findNearestPoly(s, ext, &impl_->filter, &sref, sp);
  if (!sref) return false;
  float t = 0, norm[3];
  dtPolyRef path[64];
  int npath = 0;
  if (dtStatusFailed(q->raycast(sref, sp, e, &impl_->filter, &t, norm, path, &npath, 64))) return false;
  if (t_hit) *t_hit = t > 1.0f ? 1.0f : t;
  return t < 1.0f; // t >= 1 (FLT_MAX): engel yok
}

uint64_t NavMesh::data_hash() const {
  if (!impl_ || !impl_->data) return 0;
  return fnv1a(impl_->data, (size_t)impl_->data_size, 0xcbf29ce484222325ull);
}

} // namespace tulpar::engine::sim
