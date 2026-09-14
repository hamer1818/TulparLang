// Faz 2: navmesh — zemin + duvar: yol duvarin etrafindan dolasmali; ulasilamayan
// ada icin yol yok; raycast duvari gormeli; sorgu icinde dt ayirmasi 0.
#include <cmath>
#include <cstdio>

#include "core/memory/alloc_gate.hpp"
#include "sim/navmesh.hpp"
#include "tests/test.hpp"

using namespace tulpar::engine;
using namespace tulpar::engine::sim;

namespace {
// Ucgen corbasi: 20x20 zemin (y=0), ortada x=0 duzleminde z=-10..6 arasi
// (guneyde 4 birimlik gecit) 3 birim yuksek kutu duvar; uzakta ayri ada.
struct Soup {
  float v[3 * 64];
  int t[3 * 64];
  int nv = 0, nt = 0;
  int add_v(float x, float y, float z) { v[nv * 3] = x; v[nv * 3 + 1] = y; v[nv * 3 + 2] = z; return nv++; }
  void quad(int a, int b, int c, int d) { t[nt * 3] = a; t[nt * 3 + 1] = b; t[nt * 3 + 2] = c; nt++;
                                          t[nt * 3] = a; t[nt * 3 + 1] = c; t[nt * 3 + 2] = d; nt++; }
  void box(float x0, float x1, float y0, float y1, float z0, float z1) {
    int a = add_v(x0, y0, z0), b = add_v(x1, y0, z0), c = add_v(x1, y0, z1), d = add_v(x0, y0, z1);
    int e = add_v(x0, y1, z0), f = add_v(x1, y1, z0), g = add_v(x1, y1, z1), h = add_v(x0, y1, z1);
    quad(e, h, g, f); // ust (y yukari, saat yonu tersi disaridan)
    quad(a, b, c, d); // alt
    quad(a, e, f, b); quad(b, f, g, c); quad(c, g, h, d); quad(d, h, e, a); // yanlar
  }
};

bool build_scene(NavMesh &nm) {
  Soup s;
  int a = s.add_v(-10, 0, -10), b = s.add_v(10, 0, -10), c = s.add_v(10, 0, 10), d = s.add_v(-10, 0, 10);
  s.quad(a, d, c, b); // zemin (y yukari)
  s.box(-0.5f, 0.5f, 0.0f, 3.0f, -10.0f, 6.0f); // duvar; z=6..10 gecit
  int e = s.add_v(30, 0, -5), f = s.add_v(40, 0, -5), g = s.add_v(40, 0, 5), h = s.add_v(30, 0, 5);
  s.quad(e, h, g, f); // ada (ulasilamaz)
  NavMeshBuildConfig cfg;
  cfg.agent_radius = 0.4f;
  if (!nm.build(s.v, s.nv, s.t, s.nt, cfg)) return false;
  return nm.init_query(1024);
}

float path_len(const Vec3 *p, int n) {
  float l = 0;
  for (int i = 1; i < n; i++) l += length(p[i] - p[i - 1]);
  return l;
}
} // namespace

ENGINE_TEST(navmesh_builds_and_routes_around_wall) {
  NavMesh nm;
  bool built = build_scene(nm);
  CHECK(built);
  if (!built) { // bake duserse sorgular anlamsiz: sebebi bas, devam etme
    NavMeshStats s0 = nm.stats();
    std::printf("    [bilgi] bake DUSTU: poly=%u vert=%u bayt=%zu ayirma=%llu\n", s0.polys, s0.verts, s0.data_bytes,
                (unsigned long long)s0.build_allocs);
    return;
  }
  NavMeshStats st = nm.stats();
  CHECK(st.polys > 2);
  Vec3 pts[64];
  bool partial = true;
  int n = nm.find_path({-6, 0, 0}, {6, 0, 0}, pts, 64, &partial);
  CHECK(n >= 3);      // dolanma: en az bir ara kose
  CHECK(!partial);
  float len = path_len(pts, n);
  CHECK(len > 16.0f); // duz mesafe 12; gecit z=6..10'dan dolanma > 16
  // Yolun butun noktalari duvarin disinda kalmali (|x| > 0.5 ya da z > 6 gecit)
  bool outside = true;
  for (int i = 0; i < n; i++) if (std::fabs(pts[i].x) < 0.5f && pts[i].z < 6.0f) outside = false;
  CHECK(outside);
  std::printf("    [bilgi] navmesh: %u poligon, %u vertex, %zu B, bake ayirmasi %llu; yol %d nokta, %.1f birim (duz 12)\n",
              st.polys, st.verts, st.data_bytes, (unsigned long long)st.build_allocs, n, len);
  nm.shutdown();
}

ENGINE_TEST(navmesh_unreachable_island_has_no_full_path) {
  NavMesh nm;
  CHECK(build_scene(nm));
  Vec3 pts[64];
  bool partial = false;
  int n = nm.find_path({-6, 0, 0}, {35, 0, 0}, pts, 64, &partial);
  CHECK(n == 0 || partial); // ya yol yok ya kismi (hedefe ulasilmadi)
  nm.shutdown();
}

ENGINE_TEST(navmesh_raycast_sees_wall) {
  NavMesh nm;
  CHECK(build_scene(nm));
  float t = 1.0f;
  CHECK(nm.raycast({-6, 0, 0}, {6, 0, 0}, &t)); // duvar arada
  CHECK(t > 0.3f && t < 0.6f);                  // yaklasik ortada (duvar x=-0.5..0.5, yaricap payi)
  CHECK(!nm.raycast({-6, 0, 8}, {6, 0, 8}, &t)); // gecitten gecer, engel yok
  nm.shutdown();
}

ENGINE_TEST(navmesh_query_allocates_nothing) {
  NavMesh nm;
  CHECK(build_scene(nm));
  Vec3 pts[64];
  nm.find_path({-6, 0, 0}, {6, 0, 0}, pts, 64); // isinma (yok ama olsun)
  AllocGate::begin_frame();
  int n = 0;
  for (int i = 0; i < 100; i++)
    n = nm.find_path({-6.0f + (float)(i % 3), 0, -3.0f + (float)(i % 5)}, {6, 0, 2}, pts, 64);
  float t;
  nm.raycast({-6, 0, 0}, {6, 0, 0}, &t);
  uint64_t allocs = AllocGate::end_frame();
  CHECK(n >= 2);
  CHECK(nm.stats().query_allocs == 0); // Detour kancasi
  CHECK(allocs == 0);                   // global new
  nm.shutdown();
}

ENGINE_TEST(navmesh_build_is_deterministic_in_process) {
  NavMesh a, b;
  CHECK(build_scene(a) && build_scene(b));
  CHECK(a.data_hash() != 0 && a.data_hash() == b.data_hash());
  std::printf("    [bilgi] navmesh bake ozeti %016llx (bake makinesine ozgu, platformlar arasi iddia yok)\n",
              (unsigned long long)a.data_hash());
  a.shutdown();
  b.shutdown();
}
