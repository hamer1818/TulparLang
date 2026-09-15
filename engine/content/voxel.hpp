// L6 CONTENT — Voksel teknolojisi (500 madde listesi #453 "Voxel
// Teknolojisi"): yogun 3B izgara + "gorunen yuz" yuzey cikarma (surface
// extraction). Minecraft-tarzi ilk dilim, kasitli basit: greedy meshleme
// (komsu es-duzlemsel yuzleri birlestirme) SONRAKI optimizasyon, burada YOK.
//
// Iki gecisli API (A2 ilkesi: ayirma yok) -- cagiran once count_voxel_mesh
// ile TAM vertex/indeks sayisini ogrenir, kendi arenasindan ayirir, sonra
// build_voxel_mesh doldurur. Cikan VoxelVertex duzeni (pos+normal+uv)
// renderer::Vertex ile AYNI alan sirasina sahiptir (kasitli) -- cagiran
// gerekirse dogrudan Renderer::create_mesh()'e kopyalayabilir, ama bu dosya
// renderer/'a BAGIMLI DEGIL (content katmani render'i bilmez, PLAN.md katman
// kurali).
//
// Yuz tablosu (normal+teget eksenler) renderer::Renderer::cube()'daki AYNI,
// KANITLANMIS sarma (winding) sirasini kullanir -- voksel meshleme icin
// yeniden turetmeye GEREK yok, ayni pipeline'da AYNI on-yuz kurali gecerli olur.
#pragma once
#include <cstdint>

#include "core/math/vec.hpp"

namespace tulpar::engine::content {

struct VoxelGrid {
  const uint8_t *cells = nullptr; // uzunluk nx*ny*nz, 0=bos, !=0=malzeme id (sahiplik CAGIRANDA)
  uint32_t nx = 0, ny = 0, nz = 0;
  float voxel_size = 1.0f; // dunya biriminde bir voksel kenari

  uint32_t index(uint32_t x, uint32_t y, uint32_t z) const { return (z * ny + y) * nx + x; }
  // Sinir DISI -> BOS (0) sayilir: grid kenarindaki dolu voksellerin disariya
  // bakan yuzleri de mesh'e girsin diye (aksi halde dis yuzeyler "komsu var"
  // sanilip yanlislikla gizlenirdi).
  uint8_t get(int32_t x, int32_t y, int32_t z) const {
    if (x < 0 || y < 0 || z < 0 || (uint32_t)x >= nx || (uint32_t)y >= ny || (uint32_t)z >= nz) return 0;
    return cells[index((uint32_t)x, (uint32_t)y, (uint32_t)z)];
  }
};

struct VoxelVertex {
  Vec3 pos, nrm;
  Vec2 uv;
};

struct VoxelMeshCounts {
  uint32_t vertices = 0; // build_voxel_mesh icin cagiranin ayirmasi gereken TAM VoxelVertex sayisi
  uint32_t indices = 0;  // ayni sekilde TAM uint32_t indeks sayisi
};

VoxelMeshCounts count_voxel_mesh(const VoxelGrid &grid);
// verts/indices: count_voxel_mesh() sonucuna gore cagiranin ONCEDEN ayirdigi
// tam boyutlu diziler (bu fonksiyon ayirma yapmaz).
void build_voxel_mesh(const VoxelGrid &grid, VoxelVertex *verts, uint32_t *indices);

} // namespace tulpar::engine::content
