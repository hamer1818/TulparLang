// renderer/indirect_cull.hpp: Sascha Willems'in cull.comp'undan portlanan
// frustum-testi + mesafe-esikli LOD secimi, KUP seklinde (hand-derivable)
// bir frustum uzerinde ELLE iz surulerek dogrulanir -- 3 gorunur instance
// (her biri farkli LOD), 1 kirpilan instance, artı esik-siniri (tam esitlik)
// davranisi.
#include "renderer/indirect_cull.hpp"

#include "tests/test.hpp"

using namespace tulpar::engine;
using namespace tulpar::engine::renderer;

namespace {
// x,y,z in [-10,10] kup seklinde bir "frustum" -- gercek bir projeksiyondan
// TURETILMEDI (math_frustum_culls_known_aabbs/spheres bunu zaten yapiyor),
// yalniz distance(plane,p)+r<0 mantigini ELLE hesaplanabilir kilmak icin.
Frustum cube_frustum() {
  Frustum f;
  f.planes[0] = Plane{{1, 0, 0}, 10};   // sol:  x > -10
  f.planes[1] = Plane{{-1, 0, 0}, 10};  // sag:  x < 10
  f.planes[2] = Plane{{0, 1, 0}, 10};   // alt:  y > -10
  f.planes[3] = Plane{{0, -1, 0}, 10};  // ust:  y < 10
  f.planes[4] = Plane{{0, 0, 1}, 10};   // yakin: z > -10
  f.planes[5] = Plane{{0, 0, -1}, 10};  // uzak:  z < 10
  return f;
}
} // namespace

ENGINE_TEST(indirect_cull_selects_lod_by_distance_and_culls_outside_frustum) {
  const Frustum f = cube_frustum();
  const Vec3 camera{0, 0, 0};

  const CullInstance instances[4] = {
      {{0, 0, 2}, 0.5f},    // mesafe 2 -- LOD0 (esik 5)
      {{0, 0, 6}, 0.5f},    // mesafe 6 -- LOD1 (esik 8)
      {{0, 0, 9.5f}, 0.3f}, // mesafe 9.5 -- hicbir esigi gecmiyor -> LOD2 (varsayilan); yine de GORUNUR (uzak duzlem: -9.5+10+0.3=0.8>=0)
      {{0, 0, 15}, 0.5f},   // mesafe 15 -- uzak duzlemin COK otesinde -> KIRPILDI
  };
  const LodLevel lods[3] = {
      {0, 300, 5.0f},
      {300, 150, 8.0f},
      {450, 50, 0.0f}, // son seviye: distance alani KULLANILMAZ
  };

  IndexedIndirectCommand cmds[4];
  CullStats stats;
  CHECK(cull_and_select_lod(f, camera, instances, 4, lods, 3, cmds, &stats));

  CHECK(cmds[0].instance_count == 1);
  CHECK(cmds[0].first_index == 0 && cmds[0].index_count == 300);
  CHECK(cmds[0].first_instance == 0);

  CHECK(cmds[1].instance_count == 1);
  CHECK(cmds[1].first_index == 300 && cmds[1].index_count == 150);
  CHECK(cmds[1].first_instance == 1);

  CHECK(cmds[2].instance_count == 1);
  CHECK(cmds[2].first_index == 450 && cmds[2].index_count == 50);
  CHECK(cmds[2].first_instance == 2);

  CHECK(cmds[3].instance_count == 0); // kirpildi
  CHECK(cmds[3].first_instance == 3); // gorunurlukten BAGIMSIZ hala atanir

  CHECK(stats.draw_count == 3); // kirpilan SAYILMAZ
  CHECK(stats.lod_count[0] == 1);
  CHECK(stats.lod_count[1] == 1);
  CHECK(stats.lod_count[2] == 1);
}

ENGINE_TEST(indirect_cull_lod_threshold_is_strict_less_than) {
  const Frustum f = cube_frustum();
  const Vec3 camera{0, 0, 0};
  // mesafe TAM OLARAK esige esit -- "<" kati oldugu icin bu seviyeyi
  // GECEMEMELI, bir SONRAKINE dusmeli.
  const CullInstance instances[1] = {{{0, 0, 5}, 0.1f}};
  const LodLevel lods[2] = {{0, 300, 5.0f}, {300, 150, 0.0f}};

  IndexedIndirectCommand cmds[1];
  CullStats stats;
  CHECK(cull_and_select_lod(f, camera, instances, 1, lods, 2, cmds, &stats));
  CHECK(cmds[0].instance_count == 1);
  CHECK(cmds[0].first_index == 300 && cmds[0].index_count == 150); // LOD1'e dustu, LOD0 DEGIL
}

ENGINE_TEST(indirect_cull_rejects_zero_lod_levels) {
  const Frustum f = cube_frustum();
  const CullInstance instances[1] = {{{0, 0, 0}, 1.0f}};
  IndexedIndirectCommand cmds[1];
  CullStats stats;
  CHECK(!cull_and_select_lod(f, Vec3{0, 0, 0}, instances, 1, nullptr, 0, cmds, &stats));
}
