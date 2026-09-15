// content/voxel.hpp: "gorunen yuz" meshlemenin dogrulugunu -- ozellikle komsu
// vokseller arasindaki PAYLASILAN (ic) yuzeylerin GERCEKTEN elendigini --
// somut sayilarla kanitlar (aksi halde her voksel bagimsiz 6 yuzle cizilir,
// ucgen sayisi gereksiz yere katlanir).
#include "content/voxel.hpp"
#include "core/math/vec.hpp"
#include "tests/test.hpp"

using namespace tulpar::engine;
using namespace tulpar::engine::content;

ENGINE_TEST(voxel_single_cell_emits_all_six_faces) {
  uint8_t cells[1] = {1};
  VoxelGrid grid{cells, 1, 1, 1, 2.0f}; // tek voksel, kenar uzunlugu 2

  VoxelMeshCounts c = count_voxel_mesh(grid);
  CHECK(c.vertices == 24); // 6 yuz * 4 vertex
  CHECK(c.indices == 36);  // 6 yuz * 6 indeks

  VoxelVertex verts[24];
  uint32_t idx[36];
  build_voxel_mesh(grid, verts, idx);

  // Elle hesap: voksel merkezi (0.5,0.5,0.5)*2=(1,1,1). Ilk yuz (+Z, f=0):
  // yuz merkezi (1,1,1)+(0,0,1)*1=(1,1,2); U=(1,0,0),W=(0,1,0), yari-boy=1.
  CHECK(nearly_equal(verts[0].pos, Vec3{0, 0, 2}, 1e-5f));
  CHECK(nearly_equal(verts[1].pos, Vec3{2, 0, 2}, 1e-5f));
  CHECK(nearly_equal(verts[2].pos, Vec3{2, 2, 2}, 1e-5f));
  CHECK(nearly_equal(verts[3].pos, Vec3{0, 2, 2}, 1e-5f));
  CHECK(nearly_equal(verts[0].nrm, Vec3{0, 0, 1}, 1e-5f));
  CHECK(idx[0] == 0 && idx[1] == 1 && idx[2] == 2 && idx[3] == 0 && idx[4] == 2 && idx[5] == 3);
}

ENGINE_TEST(voxel_adjacent_cells_cull_shared_internal_face) {
  // Iki voksel X ekseninde bitisik: aralarindaki YUZ (voksel0'in +X'i,
  // voksel1'in -X'i) GORUNMEZ, geri kalan 5+5=10 yuz gorunur -- naif
  // (kulleme YOK) yontem 6+6=12 yuz uretirdi, fark TAM 2.
  uint8_t cells[2] = {1, 1};
  VoxelGrid grid{cells, 2, 1, 1, 1.0f};

  VoxelMeshCounts c = count_voxel_mesh(grid);
  CHECK(c.vertices == 40); // 10 yuz * 4
  CHECK(c.indices == 60);  // 10 yuz * 6

  VoxelVertex verts[40];
  uint32_t idx[60];
  build_voxel_mesh(grid, verts, idx);
  (void)verts;
  (void)idx; // asil kanit yukaridaki SAYI (40/60) - iceriklerine burada girilmiyor
}

ENGINE_TEST(voxel_empty_grid_produces_no_geometry) {
  uint8_t cells[8] = {0, 0, 0, 0, 0, 0, 0, 0};
  VoxelGrid grid{cells, 2, 2, 2, 1.0f};
  VoxelMeshCounts c = count_voxel_mesh(grid);
  CHECK(c.vertices == 0);
  CHECK(c.indices == 0);
}

ENGINE_TEST(voxel_out_of_bounds_neighbor_counts_as_empty) {
  // Tek voksel bir 3x3x3 izgaranin KOSESINDE (0,0,0): -X/-Y/-Z komsulari
  // SINIR DISI (negatif koordinat), +X/+Y/+Z komsulari sinir ICI ama bos.
  // get()'in HER IKI durumu da (sinir disi VE bos hucre) 0 sayip yuzu
  // GORUNUR isaretledigini dogrular -- yalniz "merkezde" bir voksel test
  // edilseydi negatif-koordinat dalı hic calismamis olurdu.
  uint8_t cells[27] = {0};
  cells[0] = 1; // kose: index(0,0,0) = (0*3+0)*3+0 = 0
  VoxelGrid grid{cells, 3, 3, 3, 1.0f};
  VoxelMeshCounts c = count_voxel_mesh(grid);
  CHECK(c.vertices == 24); // tek voksel, TUM komsulari (sinir disi + bos) -> 6 yuz
  CHECK(c.indices == 36);
}
