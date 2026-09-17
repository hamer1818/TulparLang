// content/voxel.hpp: AC GOZLU (greedy) meshlemenin -- hem GORUNMEYEN ic
// yuzeylerin elendigini, HEM DE komsu es-duzlemsel yuzlerin TEK dortgene
// birlestigini -- somut sayilar VE kose konumlariyla kanitlar.
#include "content/voxel.hpp"
#include "core/math/vec.hpp"
#include "tests/test.hpp"

using namespace tulpar::engine;
using namespace tulpar::engine::content;

ENGINE_TEST(voxel_single_cell_emits_six_unit_quads) {
  uint8_t cells[1] = {1};
  VoxelGrid grid{cells, 1, 1, 1, 2.0f}; // tek voksel, kenar uzunlugu 2
  int16_t mask[1];

  VoxelMeshCounts c = count_voxel_mesh(grid, mask);
  CHECK(c.vertices == 24); // 6 yuz * 4 (tek voksel -- birlestirilecek komsu YOK)
  CHECK(c.indices == 36);

  VoxelVertex verts[24];
  uint32_t idx[36];
  build_voxel_mesh(grid, mask, verts, idx);

  // Ilk yuz: X-taramasinin d=0 siniri (-X yuzu). Elle hesap: deger<0 sarma
  // sirasi p(u0,v0),p(u0,v0+h),p(u0+w,v0+h),p(u0+w,v0); u0=v0=0,w=h=1,d=0;
  // p(a,b)=Vec3{d,a,b}*2 -> (0,0,0),(0,0,2),(0,2,2),(0,2,0).
  CHECK(nearly_equal(verts[0].pos, Vec3{0, 0, 0}, 1e-5f));
  CHECK(nearly_equal(verts[1].pos, Vec3{0, 0, 2}, 1e-5f));
  CHECK(nearly_equal(verts[2].pos, Vec3{0, 2, 2}, 1e-5f));
  CHECK(nearly_equal(verts[3].pos, Vec3{0, 2, 0}, 1e-5f));
  CHECK(nearly_equal(verts[0].nrm, Vec3{-1, 0, 0}, 1e-5f));
  CHECK(idx[0] == 0 && idx[1] == 1 && idx[2] == 2 && idx[3] == 0 && idx[4] == 2 && idx[5] == 3);
}

ENGINE_TEST(voxel_adjacent_cells_merge_into_larger_quads) {
  // Iki voksel X ekseninde bitisik: aralarindaki YUZ GORUNMEZ (10 birim
  // yuz kalirdi, ONCEKI naif yontemde 40 vertex/60 indeks olurdu), AMA
  // AC GOZLU birlestirme +Y/-Y/+Z/-Z yuzlerini (her biri iki komsu
  // birim yuzden olusan) TEK dortgene indirger -- sonuc TIPKI TEK bir
  // 2x1x1 kutu gibi: 6 yuz, 24 vertex/36 indeks (iki ayri kutu DEGIL,
  // BIRLESMIS TEK hacim gorunumu -- greedy meshlemenin butun amaci).
  uint8_t cells[2] = {1, 1};
  VoxelGrid grid{cells, 2, 1, 1, 1.0f};
  int16_t mask[2]; // capacity = max(2*1,1*1,1*2) = 2

  VoxelMeshCounts c = count_voxel_mesh(grid, mask);
  CHECK(c.vertices == 24);
  CHECK(c.indices == 36);

  VoxelVertex verts[24];
  uint32_t idx[36];
  build_voxel_mesh(grid, mask, verts, idx);

  // X-taramasi 2 BIRLESTIRILMEMIS yuz uretir (d=0: -X, d=2: +X -- aralarinda
  // d=1 sinirinda IKI voksel de dolu, yuz YOK). Bunlar vertex[0..7].
  CHECK(nearly_equal(verts[0].pos, Vec3{0, 0, 0}, 1e-5f)); // -X yuzu, X=0
  CHECK(nearly_equal(verts[4].pos, Vec3{2, 0, 0}, 1e-5f)); // +X yuzu, X=2 (iki vokselin toplam genisligi)

  // Y-taramasi d=0 (-Y yuzu): IKI vokselin -Y yuzu TEK dortgene birlesir --
  // elle hesap: deger<0 sirasi p(0,0),p(0,2),p(1,2),p(1,0); p(a,b)=Vec3{b,0,a}
  // -> (0,0,0),(2,0,0),(2,0,1),(0,0,1). X ekseninde 2 birim genislik --
  // TEK bir voksel yuzu (1 birim) DEGIL, BIRLESTIRILMIS kanit budur.
  CHECK(nearly_equal(verts[8].pos, Vec3{0, 0, 0}, 1e-5f));
  CHECK(nearly_equal(verts[9].pos, Vec3{2, 0, 0}, 1e-5f));
  CHECK(nearly_equal(verts[10].pos, Vec3{2, 0, 1}, 1e-5f));
  CHECK(nearly_equal(verts[11].pos, Vec3{0, 0, 1}, 1e-5f));
  CHECK(nearly_equal(verts[8].nrm, Vec3{0, -1, 0}, 1e-5f));
  // UV, konumla AYNI dongude: (0,0),(0,h=2),(w=1,h=2),(w=1,0).
  CHECK(nearly_equal(verts[8].uv.x, 0.0f) && nearly_equal(verts[8].uv.y, 0.0f));
  CHECK(nearly_equal(verts[9].uv.x, 0.0f) && nearly_equal(verts[9].uv.y, 2.0f));
}

ENGINE_TEST(voxel_empty_grid_produces_no_geometry) {
  uint8_t cells[8] = {0, 0, 0, 0, 0, 0, 0, 0};
  VoxelGrid grid{cells, 2, 2, 2, 1.0f};
  int16_t mask[4]; // capacity = max(2*2,2*2,2*2) = 4
  VoxelMeshCounts c = count_voxel_mesh(grid, mask);
  CHECK(c.vertices == 0);
  CHECK(c.indices == 0);
}

ENGINE_TEST(voxel_out_of_bounds_neighbor_counts_as_empty) {
  // Tek voksel bir 3x3x3 izgaranin KOSESINDE (0,0,0): -X/-Y/-Z komsulari
  // SINIR DISI (negatif koordinat), +X/+Y/+Z komsulari sinir ICI ama bos.
  // get()'in HER IKI durumu da 0 sayip yuzu GORUNUR isaretledigini
  // dogrular. Tek voksel -- komsu YOK, birlestirme OLMAZ, hala 6 yuz.
  uint8_t cells[27] = {0};
  cells[0] = 1; // kose: index(0,0,0) = (0*3+0)*3+0 = 0
  VoxelGrid grid{cells, 3, 3, 3, 1.0f};
  int16_t mask[9]; // capacity = max(3*3,3*3,3*3) = 9
  VoxelMeshCounts c = count_voxel_mesh(grid, mask);
  CHECK(c.vertices == 24);
  CHECK(c.indices == 36);
}
