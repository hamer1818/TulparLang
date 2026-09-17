// renderer/cluster_lod.hpp: surekli LOD kume secimi (cut).
//
// Kesme kurali MeshDecimationTools.h'nin sozlesmesidir:
//   ciz  <=>  eval(current) <= esik   VE   eval(coarser) > esik
//
// Buradaki sayilarin hepsi IKININ KUVVETI (0.125, 0.03125, 512, 1024) --
// ekran hatasi hesabi float'ta TAM yapilsin, testin sonucu yuvarlama
// sansina kalmasin. Esikteki (tam 1.0 piksel) davranis da bilerek sinanir.
#include "renderer/cluster_lod.hpp"

#include <cfloat>

#include "core/math/vec.hpp"
#include "tests/test.hpp"

using namespace tulpar::engine;
using namespace tulpar::engine::renderer;

namespace {

// --- Elle kurulmus, IKI SEVIYELI DAG ---------------------------------
//
//   seviye 0 (orijinal):  C0 C1          C2 C3
//                           \  /           \  /
//   seviye 1 (sadelesmis):   C4             C5
//
// Sol dal 0.125, sag dal 0.03125 sadelestirme hatasiyla kabalasir.
// C4/C5 son seviyedir -> coarser.error = FLT_MAX (daha fazla kabalasma
// yok), yani her zaman cizilebilir kalirlar.
//
// Gorunum: proj=1, screen_height=1024, esik = 1 piksel, yaricap 0
//   ekran_hatasi = hata / uzaklik * 0.5 * 1024 = hata * 512 / uzaklik
// Yani:
//   0.125  esigi gecer  <=>  0.125*512/d  > 1  <=>  d < 64
//   0.03125 esigi gecer <=>  0.03125*512/d > 1 <=>  d < 16
constexpr uint32_t kN = 6;

struct Dag {
  Cluster clusters[kN];
  ClusterLodMesh mesh;
};

Dag make_dag() {
  Dag d{};
  // Seviye 0: bu seviyenin hatasi 0 (orijinal geometri, sapma yok).
  for (uint32_t i = 0; i < 4; i++) {
    d.clusters[i].radius = 0.0f;
    d.clusters[i].current.error = 0.0f;
    d.clusters[i].index_offset = i * 3;
    d.clusters[i].index_count = 3;
    d.clusters[i].level = 0;
  }
  d.clusters[0].coarser.error = 0.125f; // sol dal kabalasinca bu hatayi alir
  d.clusters[1].coarser.error = 0.125f;
  d.clusters[2].coarser.error = 0.03125f; // sag dal
  d.clusters[3].coarser.error = 0.03125f;

  // Seviye 1: kabalasmis temsiller. current = kendilerini ureten
  // sadelestirmenin hatasi; coarser = FLT_MAX (SON seviye).
  d.clusters[4].current.error = 0.125f;
  d.clusters[4].coarser.error = FLT_MAX;
  d.clusters[4].index_offset = 12;
  d.clusters[4].index_count = 3;
  d.clusters[4].level = 1;
  d.clusters[5].current.error = 0.03125f;
  d.clusters[5].coarser.error = FLT_MAX;
  d.clusters[5].index_offset = 15;
  d.clusters[5].index_count = 3;
  d.clusters[5].level = 1;

  d.mesh.clusters = d.clusters;
  d.mesh.cluster_count = kN;
  return d;
}

LodView view_at(float distance) {
  LodView v;
  v.camera_position = Vec3{0, 0, distance};
  v.proj = 1.0f;
  v.znear = 0.0078125f; // 1/128
  v.screen_height = 1024.0f;
  v.error_threshold_px = 1.0f;
  return v;
}

bool contains(const uint32_t *a, uint32_t n, uint32_t v) {
  for (uint32_t i = 0; i < n; i++)
    if (a[i] == v) return true;
  return false;
}

} // namespace

ENGINE_TEST(cluster_screen_error_uses_nearest_point_of_sphere) {
  LodView v = view_at(0.0f);

  ClusterErrorMetric m;
  m.center = Vec3{0, 0, 16.0f};
  m.radius = 0.0f;
  m.error = 0.125f;
  // 0.125 / 16 * 0.5 * 1024 = 4
  CHECK(cluster_screen_error(m, v) == 4.0f);

  // Yaricap uzakligi AZALTIR (kurenin kameraya EN YAKIN noktasi esas alinir):
  // 0.125 / (16 - 8) * 0.5 * 1024 = 8
  m.radius = 8.0f;
  CHECK(cluster_screen_error(m, v) == 8.0f);

  // Kamera kurenin ICINDE: uzaklik znear'a kirpilir -- negatif/sonsuz olmaz.
  m.radius = 100.0f;
  CHECK(cluster_screen_error(m, v) == 0.125f / v.znear * 0.5f * 1024.0f);

  // Hatasi sifir olan (orijinal) seviye HER ZAMAN esigin altindadir.
  m.error = 0.0f;
  CHECK(cluster_screen_error(m, v) == 0.0f);
}

ENGINE_TEST(cluster_lod_picks_coarsest_level_when_far) {
  Dag d = make_dag();
  uint32_t out[16];
  // d = 512: sol dal 0.125 px, sag dal 0.03125 px -- ikisi de esigin
  // ALTINDA, yani en kaba temsile inmeye iznimiz var.
  const uint32_t n = cluster_lod_select(d.mesh, view_at(512.0f), out, 16);
  CHECK(n == 2);
  CHECK(contains(out, n, 4));
  CHECK(contains(out, n, 5));
  CHECK(!contains(out, n, 0)); // ince seviye CIZILMEZ
}

ENGINE_TEST(cluster_lod_picks_finest_level_when_close) {
  Dag d = make_dag();
  uint32_t out[16];
  // d = 8: sol dal 8 px, sag dal 2 px -- ikisi de esigin USTUNDE, yani
  // kabalasmaya izin YOK.
  const uint32_t n = cluster_lod_select(d.mesh, view_at(8.0f), out, 16);
  CHECK(n == 4);
  for (uint32_t i = 0; i < 4; i++) CHECK(contains(out, n, i));
  CHECK(!contains(out, n, 4));
  CHECK(!contains(out, n, 5));
}

ENGINE_TEST(cluster_lod_mixes_levels_within_one_mesh) {
  // ASIL IDDIA: klasik LOD'un YAPAMADIGI sey. Ayni mesh'in bir parcasi
  // yuksek, obur parcasi dusuk detayda cizilir.
  Dag d = make_dag();
  uint32_t out[16];
  // d = 32: sol dal 0.125*512/32 = 2 px    -> esigi GECER -> C0,C1 ince
  //         sag dal 0.03125*512/32 = 0.5px -> esigin ALTI -> C5 kaba
  const uint32_t n = cluster_lod_select(d.mesh, view_at(32.0f), out, 16);
  CHECK(n == 3);
  CHECK(contains(out, n, 0));
  CHECK(contains(out, n, 1));
  CHECK(contains(out, n, 5)); // OBUR yari KABA seviyede
  CHECK(!contains(out, n, 2));
  CHECK(!contains(out, n, 3));
  CHECK(!contains(out, n, 4));
}

ENGINE_TEST(cluster_lod_is_crack_free_at_every_distance) {
  // KESME KURALININ ASIL GARANTISI: DAG'daki her yol boyunca TAM BIR
  // seviye secilir. Sol dalda ya {C0,C1} cizilir ya {C4} -- ikisi birden
  // ya da hicbiri ASLA. Aksi halde kumeler arasinda catlak ya da ust uste
  // binme gorulurdu. Genis bir uzaklik araliginda taranir.
  Dag d = make_dag();
  uint32_t out[16];
  for (float dist = 1.0f; dist <= 4096.0f; dist *= 1.125f) {
    const uint32_t n = cluster_lod_select(d.mesh, view_at(dist), out, 16);
    const bool c0 = contains(out, n, 0), c1 = contains(out, n, 1), c4 = contains(out, n, 4);
    const bool c2 = contains(out, n, 2), c3 = contains(out, n, 3), c5 = contains(out, n, 5);
    CHECK(c0 == c1); // ayni gruptaki kumeler BIRLIKTE secilir
    CHECK(c0 != c4); // ya ince ya kaba -- TAM BIRI
    CHECK(c2 == c3);
    CHECK(c2 != c5);
    CHECK(n >= 2); // hicbir uzaklikta BOS kalmaz
  }
}

ENGINE_TEST(cluster_lod_last_level_is_always_renderable) {
  // Son seviyenin coarser.error'u FLT_MAX: ne kadar uzaklasilirsa
  // uzaklasilsin cizilebilir kalmali, yoksa nesne BIRDEN KAYBOLURDU.
  Dag d = make_dag();
  uint32_t out[16];
  const uint32_t n = cluster_lod_select(d.mesh, view_at(1000000.0f), out, 16);
  CHECK(n == 2);
  CHECK(contains(out, n, 4));
  CHECK(contains(out, n, 5));
}

ENGINE_TEST(cluster_lod_threshold_boundary_is_exclusive) {
  // d = 64 -> sol dalin kabalasma hatasi TAM 1.0 piksel. Kural "esigi
  // GECERSE" oldugu icin 1.0 esigi GECMEZ -> kaba seviye secilir.
  Dag d = make_dag();
  uint32_t out[16];
  const LodView v = view_at(64.0f);
  CHECK(cluster_screen_error(d.clusters[0].coarser, v) == 1.0f); // TAM esik
  const uint32_t n = cluster_lod_select(d.mesh, v, out, 16);
  CHECK(contains(out, n, 4));  // kaba
  CHECK(!contains(out, n, 0)); // ince DEGIL
}

ENGINE_TEST(cluster_lod_reports_overflow_instead_of_truncating) {
  Dag d = make_dag();
  uint32_t out[2];
  // Yakinda 4 kume secilir ama tampon 2 kisilik: donus GERCEK sayiyi
  // vermeli ki cagiran tasmayi FARK ETSIN.
  const uint32_t n = cluster_lod_select(d.mesh, view_at(8.0f), out, 2);
  CHECK(n == 4);
  CHECK(n > 2);
}

ENGINE_TEST(cluster_lod_frustum_culling_removes_offscreen_clusters) {
  Dag d = make_dag();
  d.clusters[0].center = Vec3{0, 0, 0};
  d.clusters[0].radius = 1.0f;
  d.clusters[1].center = Vec3{1000, 0, 0}; // ekran disi
  d.clusters[1].radius = 1.0f;

  Frustum f;
  f.planes[0] = Plane{{1, 0, 0}, 10};
  f.planes[1] = Plane{{-1, 0, 0}, 10};
  f.planes[2] = Plane{{0, 1, 0}, 10};
  f.planes[3] = Plane{{0, -1, 0}, 10};
  f.planes[4] = Plane{{0, 0, 1}, 10};
  f.planes[5] = Plane{{0, 0, -1}, 10};

  uint32_t out[16];
  const uint32_t n = cluster_lod_select_culled(d.mesh, view_at(8.0f), f, out, 16);
  CHECK(contains(out, n, 0));  // icerde
  CHECK(!contains(out, n, 1)); // disarida -> ELENDI

  // Kirpma SEVIYE SECIMINI degistirmez, yalnizca sonucu daraltir.
  uint32_t out2[16];
  const uint32_t n2 = cluster_lod_select(d.mesh, view_at(8.0f), out2, 16);
  CHECK(n == n2 - 1);
}

ENGINE_TEST(cluster_lod_handles_empty_input) {
  ClusterLodMesh empty;
  uint32_t out[4];
  CHECK(cluster_lod_select(empty, view_at(10.0f), out, 4) == 0);
  Frustum f{};
  CHECK(cluster_lod_select_culled(empty, view_at(10.0f), f, out, 4) == 0);
}
