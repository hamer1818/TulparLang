#include "content/voxel.hpp"

namespace tulpar::engine::content {

namespace {

// 2B maske uzerinde AC GOZLU (greedy) dikdortgen birlestirme -- Mikola
// Lysenko'nun "Meshing in a Minecraft Game" (0fps.net) makalesindeki
// TEKNIGIN kendisi: her (u,v) icin, HENUZ islenmemis (deger!=0) bir hucre
// bulununca, ONCE genislik (w) AYNI degere sahip komsu hucrelerle
// genisletilir, SONRA yukseklik (h) -- w genislikteki TAM SATIRIN her
// hucresi AYNI degere sahip oldugu surece -- genisletilir. Birlestirilen
// dikdortgen emit(u0,v0,w,h,value) ile bildirilir, sonra mask'ta 0'lanir
// (bir sonraki tarama o bolgeyi ATLAR). mask[v*du+u] duzeninde, du*dv
// boyutunda cagiranin ayirdigi bir tampon.
template <class Emit>
void greedy_merge_mask(int16_t *mask, uint32_t du, uint32_t dv, Emit &&emit) {
  for (uint32_t v = 0; v < dv; v++) {
    for (uint32_t u = 0; u < du;) {
      const int16_t val = mask[v * du + u];
      if (val == 0) { u++; continue; }
      uint32_t w = 1;
      while (u + w < du && mask[v * du + (u + w)] == val) w++;
      uint32_t h = 1;
      bool extend = true;
      while (extend && v + h < dv) {
        for (uint32_t k = 0; k < w; k++) {
          if (mask[(v + h) * du + (u + k)] != val) { extend = false; break; }
        }
        if (extend) h++;
      }
      emit(u, v, w, h, val);
      for (uint32_t hh = 0; hh < h; hh++)
        for (uint32_t ww = 0; ww < w; ww++)
          mask[(v + hh) * du + (u + ww)] = 0;
      u += w;
    }
  }
}

// Bir eksen taramasinin ORTAK 4-kose yazma/sayma adimi: value'nun isaretine
// gore SARMA (winding) YONU ters cevrilir (pozitif eksen yonu icin
// u0,v0->u0+w,v0->u0+w,v0+h->u0,v0+h; negatif icin bu dongunun TERSI --
// U x W = +eksen olacak sekilde SECILEN her eksenin (u,v) tabani icin bu
// TEK kural HER UCUNDE de dogru sarma verir, bkz. asagidaki 3 cagiran).
template <class PointFn>
void emit_quad(PointFn &&p, Vec3 n, uint32_t u0, uint32_t v0, uint32_t w, uint32_t h, int16_t value,
               VoxelVertex *verts, uint32_t *indices, uint32_t &vi, uint32_t &ii) {
  if (verts) {
    if (value > 0) {
      verts[vi + 0] = {p(u0, v0), n, {0, 0}};
      verts[vi + 1] = {p(u0 + w, v0), n, {(float)w, 0}};
      verts[vi + 2] = {p(u0 + w, v0 + h), n, {(float)w, (float)h}};
      verts[vi + 3] = {p(u0, v0 + h), n, {0, (float)h}};
    } else {
      verts[vi + 0] = {p(u0, v0), n, {0, 0}};
      verts[vi + 1] = {p(u0, v0 + h), n, {0, (float)h}};
      verts[vi + 2] = {p(u0 + w, v0 + h), n, {(float)w, (float)h}};
      verts[vi + 3] = {p(u0 + w, v0), n, {(float)w, 0}};
    }
    indices[ii + 0] = vi; indices[ii + 1] = vi + 1; indices[ii + 2] = vi + 2;
    indices[ii + 3] = vi; indices[ii + 4] = vi + 2; indices[ii + 5] = vi + 3;
  }
  vi += 4;
  ii += 6;
}

// d ekseni X boyunca supurulur (d=0..nx dahil sinir konumlari); (u,v)=(y,z)
// -- U=+Y,W=+Z secildi (UxW=+X, pozitif yon icin dogru disa-donuk sarma).
void sweep_axis_x(const VoxelGrid &g, int16_t *mask, VoxelVertex *verts, uint32_t *indices,
                   uint32_t &vi, uint32_t &ii) {
  const float s = g.voxel_size;
  for (uint32_t d = 0; d <= g.nx; d++) {
    for (uint32_t v = 0; v < g.nz; v++) {
      for (uint32_t u = 0; u < g.ny; u++) {
        const uint8_t before = g.get((int32_t)d - 1, (int32_t)u, (int32_t)v);
        const uint8_t after = g.get((int32_t)d, (int32_t)u, (int32_t)v);
        int16_t m = 0;
        if (before != 0 && after == 0) m = (int16_t)before;
        else if (before == 0 && after != 0) m = (int16_t)(-(int)after);
        mask[v * g.ny + u] = m;
      }
    }
    greedy_merge_mask(mask, g.ny, g.nz, [&](uint32_t u0, uint32_t v0, uint32_t w, uint32_t h, int16_t value) {
      const Vec3 n{value > 0 ? 1.0f : -1.0f, 0, 0};
      auto p = [&](uint32_t a, uint32_t b) { return Vec3{(float)d, (float)a, (float)b} * s; };
      emit_quad(p, n, u0, v0, w, h, value, verts, indices, vi, ii);
    });
  }
}

// (u,v)=(z,x) -- U=+Z,W=+X secildi (UxW=+Y).
void sweep_axis_y(const VoxelGrid &g, int16_t *mask, VoxelVertex *verts, uint32_t *indices,
                   uint32_t &vi, uint32_t &ii) {
  const float s = g.voxel_size;
  for (uint32_t d = 0; d <= g.ny; d++) {
    for (uint32_t v = 0; v < g.nx; v++) {
      for (uint32_t u = 0; u < g.nz; u++) {
        const uint8_t before = g.get((int32_t)v, (int32_t)d - 1, (int32_t)u);
        const uint8_t after = g.get((int32_t)v, (int32_t)d, (int32_t)u);
        int16_t m = 0;
        if (before != 0 && after == 0) m = (int16_t)before;
        else if (before == 0 && after != 0) m = (int16_t)(-(int)after);
        mask[v * g.nz + u] = m;
      }
    }
    greedy_merge_mask(mask, g.nz, g.nx, [&](uint32_t u0, uint32_t v0, uint32_t w, uint32_t h, int16_t value) {
      const Vec3 n{0, value > 0 ? 1.0f : -1.0f, 0};
      auto p = [&](uint32_t a, uint32_t b) { return Vec3{(float)b, (float)d, (float)a} * s; };
      emit_quad(p, n, u0, v0, w, h, value, verts, indices, vi, ii);
    });
  }
}

// (u,v)=(x,y) -- U=+X,W=+Y secildi (UxW=+Z) -- eski per-voksel kodun +Z
// yuzuyle (kFaceU[0]=+X,kFaceW[0]=+Y) AYNI taban.
void sweep_axis_z(const VoxelGrid &g, int16_t *mask, VoxelVertex *verts, uint32_t *indices,
                   uint32_t &vi, uint32_t &ii) {
  const float s = g.voxel_size;
  for (uint32_t d = 0; d <= g.nz; d++) {
    for (uint32_t v = 0; v < g.ny; v++) {
      for (uint32_t u = 0; u < g.nx; u++) {
        const uint8_t before = g.get((int32_t)u, (int32_t)v, (int32_t)d - 1);
        const uint8_t after = g.get((int32_t)u, (int32_t)v, (int32_t)d);
        int16_t m = 0;
        if (before != 0 && after == 0) m = (int16_t)before;
        else if (before == 0 && after != 0) m = (int16_t)(-(int)after);
        mask[v * g.nx + u] = m;
      }
    }
    greedy_merge_mask(mask, g.nx, g.ny, [&](uint32_t u0, uint32_t v0, uint32_t w, uint32_t h, int16_t value) {
      const Vec3 n{0, 0, value > 0 ? 1.0f : -1.0f};
      auto p = [&](uint32_t a, uint32_t b) { return Vec3{(float)a, (float)b, (float)d} * s; };
      emit_quad(p, n, u0, v0, w, h, value, verts, indices, vi, ii);
    });
  }
}

} // namespace

VoxelMeshCounts count_voxel_mesh(const VoxelGrid &grid, int16_t *mask_scratch) {
  uint32_t vi = 0, ii = 0;
  sweep_axis_x(grid, mask_scratch, nullptr, nullptr, vi, ii);
  sweep_axis_y(grid, mask_scratch, nullptr, nullptr, vi, ii);
  sweep_axis_z(grid, mask_scratch, nullptr, nullptr, vi, ii);
  return VoxelMeshCounts{vi, ii};
}

void build_voxel_mesh(const VoxelGrid &grid, int16_t *mask_scratch, VoxelVertex *verts, uint32_t *indices) {
  uint32_t vi = 0, ii = 0;
  sweep_axis_x(grid, mask_scratch, verts, indices, vi, ii);
  sweep_axis_y(grid, mask_scratch, verts, indices, vi, ii);
  sweep_axis_z(grid, mask_scratch, verts, indices, vi, ii);
}

} // namespace tulpar::engine::content
