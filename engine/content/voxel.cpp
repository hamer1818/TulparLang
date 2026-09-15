#include "content/voxel.hpp"

namespace tulpar::engine::content {

namespace {
// renderer::Renderer::cube() (renderer/renderer.cpp) ile BIREBIR AYNI 6 yuz
// tablosu -- N=normal, U/W=teget eksenler, sira (+Z,-Z,+X,-X,+Y,-Y).
constexpr float kFaceN[6][3] = {{0, 0, 1}, {0, 0, -1}, {1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}};
constexpr float kFaceU[6][3] = {{1, 0, 0}, {-1, 0, 0}, {0, 0, -1}, {0, 0, 1}, {1, 0, 0}, {1, 0, 0}};
constexpr float kFaceW[6][3] = {{0, 1, 0}, {0, 1, 0}, {0, 1, 0}, {0, 1, 0}, {0, 0, -1}, {0, 0, 1}};
constexpr int32_t kFaceDx[6] = {0, 0, 1, -1, 0, 0};
constexpr int32_t kFaceDy[6] = {0, 0, 0, 0, 1, -1};
constexpr int32_t kFaceDz[6] = {1, -1, 0, 0, 0, 0};

// out[6]: her yuzun GORUNUR olup olmadigi (komsusu bos mu). Donus: gorunen yuz sayisi.
uint32_t visible_faces_of(const VoxelGrid &grid, uint32_t x, uint32_t y, uint32_t z, bool *out) {
  uint32_t n = 0;
  for (int f = 0; f < 6; f++) {
    const bool visible = grid.get((int32_t)x + kFaceDx[f], (int32_t)y + kFaceDy[f], (int32_t)z + kFaceDz[f]) == 0;
    out[f] = visible;
    if (visible) n++;
  }
  return n;
}
} // namespace

VoxelMeshCounts count_voxel_mesh(const VoxelGrid &grid) {
  VoxelMeshCounts c;
  bool faces[6];
  for (uint32_t z = 0; z < grid.nz; z++)
    for (uint32_t y = 0; y < grid.ny; y++)
      for (uint32_t x = 0; x < grid.nx; x++) {
        if (grid.get((int32_t)x, (int32_t)y, (int32_t)z) == 0) continue;
        const uint32_t nf = visible_faces_of(grid, x, y, z, faces);
        c.vertices += nf * 4;
        c.indices += nf * 6;
      }
  return c;
}

void build_voxel_mesh(const VoxelGrid &grid, VoxelVertex *verts, uint32_t *indices) {
  uint32_t vi = 0, ii = 0;
  bool faces[6];
  const float h = grid.voxel_size * 0.5f; // yari-voksel-boyutu (yuz merkezine/kenarina kaydirma)
  for (uint32_t z = 0; z < grid.nz; z++)
    for (uint32_t y = 0; y < grid.ny; y++)
      for (uint32_t x = 0; x < grid.nx; x++) {
        if (grid.get((int32_t)x, (int32_t)y, (int32_t)z) == 0) continue;
        visible_faces_of(grid, x, y, z, faces);
        const Vec3 center = Vec3{(float)x + 0.5f, (float)y + 0.5f, (float)z + 0.5f} * grid.voxel_size;
        for (int f = 0; f < 6; f++) {
          if (!faces[f]) continue;
          const Vec3 N{kFaceN[f][0], kFaceN[f][1], kFaceN[f][2]};
          const Vec3 U{kFaceU[f][0], kFaceU[f][1], kFaceU[f][2]};
          const Vec3 W{kFaceW[f][0], kFaceW[f][1], kFaceW[f][2]};
          const Vec3 c = center + N * h;
          const Vec3 p0 = c - U * h - W * h;
          const Vec3 p1 = c + U * h - W * h;
          const Vec3 p2 = c + U * h + W * h;
          const Vec3 p3 = c - U * h + W * h;
          verts[vi + 0] = {p0, N, {0, 0}};
          verts[vi + 1] = {p1, N, {1, 0}};
          verts[vi + 2] = {p2, N, {1, 1}};
          verts[vi + 3] = {p3, N, {0, 1}};
          indices[ii++] = vi; indices[ii++] = vi + 1; indices[ii++] = vi + 2;
          indices[ii++] = vi; indices[ii++] = vi + 2; indices[ii++] = vi + 3;
          vi += 4;
        }
      }
}

} // namespace tulpar::engine::content
