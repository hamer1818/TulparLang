// L6 CONTENT — Voksel teknolojisi (500 madde listesi #453 "Voxel
// Teknolojisi"): yogun 3B izgara + AC GOZLU (greedy) yuzey cikarma.
//
// **SIFIRDAN TASARIM DEGIL, GERCEK bir teknigin PORTU:** Mikola Lysenko'nun
// "Meshing in a Minecraft Game" (0fps.net, 2012) makalesinde tanimlanan,
// voksel motorlari camiasinda FIILEN standart olan yontem -- her 3 eksende
// ayri ayri, eksen boyunca KESIT KESIT (2B maske) taranir; ayni malzeme +
// ayni yon'e sahip KOMSU hucreler TEK BUYUK dortgene birlestirilir (naif
// "her voksel 6 birim yuz" yontemine kiyasla, duz/tekduze bolgelerde ucgen
// sayisini ONEMLI OLCUDE azaltir). Isaretli maske hilesi (mask degeri:
// +malzeme = pozitif-eksen-yonlu yuz, -malzeme = negatif-eksen-yonlu yuz,
// 0 = yuz yok) de MAKALENIN AYNI tekniği -- TEK gecişte HER IKI yon islenir.
//
// Iki gecisli API (A2 ilkesi: ayirma yok) -- cagiran once count_voxel_mesh
// ile TAM vertex/indeks sayisini ogrenir (greedy birlesimi BIR KEZ calistirip
// SAYAR, yazMAZ), kendi arenasindan ayirir, sonra build_voxel_mesh (AYNI
// birlesimi bir KEZ DAHA calistirip bu sefer YAZAR) doldurur -- iki gecis de
// ayni mask_scratch tamponunu (cagiranin ayirdigi, voxel_mesh_mask_capacity()
// buyuklugunde) YENIDEN KULLANIR, deterministik ve ayirmasiz.
//
// Cikan VoxelVertex duzeni (pos+normal+uv) renderer::Vertex ile AYNI alan
// sirasina sahiptir (kasitli) -- cagiran gerekirse dogrudan
// Renderer::create_mesh()'e kopyalayabilir; bu dosya renderer/'a BAGIMLI
// DEGIL (content katmani render'i bilmez, PLAN.md katman kurali).
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

// mask_scratch icin GEREKEN EN AZ int16_t eleman sayisi (3 eksenin en genis
// kesitinin alani -- her eksen taramasi kendi kesit boyutunu kullanir,
// ucu de bu tek tamponu SIRAYLA yeniden kullanir).
inline uint32_t voxel_mesh_mask_capacity(const VoxelGrid &g) {
  uint32_t m = g.nx * g.ny;
  if (g.ny * g.nz > m) m = g.ny * g.nz;
  if (g.nz * g.nx > m) m = g.nz * g.nx;
  return m;
}

// mask_scratch: cagiranin ONCEDEN ayirdigi, en az voxel_mesh_mask_capacity(grid)
// int16_t'lik gecici alan (bu fonksiyon SIFIRLAMAYA GEREK DUYMADAN kendi
// yazdigi her hucreyi kullanmadan once BASTAN doldurur).
VoxelMeshCounts count_voxel_mesh(const VoxelGrid &grid, int16_t *mask_scratch);
// verts/indices: count_voxel_mesh() sonucuna gore cagiranin ONCEDEN ayirdigi
// tam boyutlu diziler. mask_scratch: count_voxel_mesh ile AYNI kurallar
// (icerigi bu iki cagri arasinda ANLAMSIZ, temizlemeye GEREK YOK).
void build_voxel_mesh(const VoxelGrid &grid, int16_t *mask_scratch, VoxelVertex *verts, uint32_t *indices);

} // namespace tulpar::engine::content
