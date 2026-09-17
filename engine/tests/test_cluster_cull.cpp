// renderer/cluster_cull.hpp: GPU-driven eleme.
//
// ASIL SINANAN SEY: GPU portunun CPU secimiyle AYNI sonucu vermesi. Iki
// yerde ayri yazilmis matematik sessizce ayrisir; burada her uzaklikta
// kume kume karsilastiriliyor.
//
// (Shader'in kendisi bu ortamda CALISTIRILAMAZ -- cihaz yok. Ama shader ile
// cluster_cull_reference AYNI ifadeleri AYNI SIRADA kullaniyor; test o
// referansi, bagimsiz yazilmis cluster_lod_select_culled'a karsi dogruluyor.
// Yani iki bagimsiz uygulama birbirini kontrol ediyor.)
#include "renderer/cluster_cull.hpp"

#include <cfloat>

#include "core/math/vec.hpp"
#include "renderer/cluster_lod.hpp"
#include "tests/test.hpp"

using namespace tulpar::engine;
using namespace tulpar::engine::renderer;

namespace {

constexpr uint32_t kN = 6;

struct Dag {
  Cluster clusters[kN];
  ClusterLodMesh mesh;
};

// tests/test_cluster_lod.cpp ile AYNI DAG: iki dal, iki seviye.
//   0.125  esigi gecer <=> d < 64        0.03125 esigi gecer <=> d < 16
Dag make_dag() {
  Dag d{};
  for (uint32_t i = 0; i < 4; i++) {
    d.clusters[i].current.error = 0.0f;
    d.clusters[i].index_offset = i * 3;
    d.clusters[i].index_count = 3;
  }
  d.clusters[0].coarser.error = 0.125f;
  d.clusters[1].coarser.error = 0.125f;
  d.clusters[2].coarser.error = 0.03125f;
  d.clusters[3].coarser.error = 0.03125f;

  d.clusters[4].current.error = 0.125f;
  d.clusters[4].coarser.error = FLT_MAX;
  d.clusters[4].index_offset = 12;
  d.clusters[4].index_count = 3;
  d.clusters[5].current.error = 0.03125f;
  d.clusters[5].coarser.error = FLT_MAX;
  d.clusters[5].index_offset = 15;
  d.clusters[5].index_count = 3;

  d.mesh.clusters = d.clusters;
  d.mesh.cluster_count = kN;
  return d;
}

LodView view_at(float distance) {
  LodView v;
  v.camera_position = Vec3{0, 0, distance};
  v.proj = 1.0f;
  v.znear = 0.0078125f;
  v.screen_height = 1024.0f;
  v.error_threshold_px = 1.0f;
  return v;
}

// Her seyi iceren genis frustum: kirpma DEVRE DISI gibi davranir.
Frustum wide_frustum() {
  Frustum f;
  f.planes[0] = Plane{{1, 0, 0}, 1e9f};
  f.planes[1] = Plane{{-1, 0, 0}, 1e9f};
  f.planes[2] = Plane{{0, 1, 0}, 1e9f};
  f.planes[3] = Plane{{0, -1, 0}, 1e9f};
  f.planes[4] = Plane{{0, 0, 1}, 1e9f};
  f.planes[5] = Plane{{0, 0, -1}, 1e9f};
  return f;
}

bool contains(const uint32_t *a, uint32_t n, uint32_t v) {
  for (uint32_t i = 0; i < n; i++)
    if (a[i] == v) return true;
  return false;
}

} // namespace

ENGINE_TEST(cluster_cull_layout_matches_shader_contract) {
  // Bu boyutlar shader'in okudugu tampon duzenini belirler; kayarsa GPU
  // cop okur. static_assert'ler derleme zamaninda tutuyor, burada da
  // ACIKCA belgeleniyor.
  CHECK(sizeof(GpuCluster) == 72);
  CHECK(sizeof(DrawIndexedIndirectCommand) == 20);
  CHECK(sizeof(ClusterCullPush) == 128); // garantili maxPushConstantsSize
  CHECK(sizeof(Plane) == 16);

  // Is grubu yuvarlamasi: shader artigi kendisi eliyor ama dispatch
  // sayisi YETERLI olmali, yoksa son kumeler HIC islenmez.
  CHECK(cluster_cull_group_count(0) == 0);
  CHECK(cluster_cull_group_count(1) == 1);
  CHECK(cluster_cull_group_count(64) == 1);
  CHECK(cluster_cull_group_count(65) == 2);
  CHECK(cluster_cull_group_count(128) == 2);
  CHECK(cluster_cull_group_count(129) == 3);
}

ENGINE_TEST(cluster_cull_to_gpu_cluster_round_trips_every_field) {
  Cluster c{};
  c.center = Vec3{1, 2, 3};
  c.radius = 4.0f;
  c.current.center = Vec3{5, 6, 7};
  c.current.radius = 8.0f;
  c.current.error = 9.0f;
  c.coarser.center = Vec3{10, 11, 12};
  c.coarser.radius = 13.0f;
  c.coarser.error = FLT_MAX;
  c.index_offset = 100;
  c.index_count = 200;
  c.geometry = 3;
  c.level = 2;

  const GpuCluster g = to_gpu_cluster(c);
  CHECK(g.center_x == 1.0f && g.center_y == 2.0f && g.center_z == 3.0f);
  CHECK(g.radius == 4.0f);
  CHECK(g.cur_x == 5.0f && g.cur_y == 6.0f && g.cur_z == 7.0f);
  CHECK(g.cur_radius == 8.0f && g.cur_error == 9.0f);
  CHECK(g.coa_x == 10.0f && g.coa_y == 11.0f && g.coa_z == 12.0f);
  CHECK(g.coa_radius == 13.0f);
  CHECK(g.coa_error == FLT_MAX); // SON seviye isareti bozulmamali
  CHECK(g.index_offset == 100 && g.index_count == 200);
  CHECK(g.geometry == 3 && g.level == 2);
}

ENGINE_TEST(cluster_cull_push_copies_frustum_planes_verbatim) {
  const Frustum f = wide_frustum();
  const LodView v = view_at(32.0f);
  const ClusterCullPush p = make_cull_push(v, f, kN);

  for (int i = 0; i < 6; i++) {
    CHECK(p.planes[i].normal.x == f.planes[i].normal.x);
    CHECK(p.planes[i].normal.y == f.planes[i].normal.y);
    CHECK(p.planes[i].normal.z == f.planes[i].normal.z);
    CHECK(p.planes[i].d == f.planes[i].d);
  }
  CHECK(p.camera.z == 32.0f);
  CHECK(p.proj == 1.0f);
  CHECK(p.znear == v.znear);
  CHECK(p.screen_height == 1024.0f);
  CHECK(p.threshold_px == 1.0f);
  CHECK(p.cluster_count == kN);
}

ENGINE_TEST(cluster_cull_reference_matches_cpu_selection_at_every_distance) {
  // ASIL TEST. GPU yolu ile CPU yolu AYNI kumeleri secmeli. Ayrisirlarsa
  // cihazda gorulen goruntu, testlerin dogruladigi seyden FARKLI olur.
  Dag d = make_dag();
  const Frustum f = wide_frustum();

  DrawIndexedIndirectCommand draws[kN];
  uint32_t selected[kN];

  for (float dist = 1.0f; dist <= 4096.0f; dist *= 1.125f) {
    const LodView v = view_at(dist);
    const ClusterCullPush p = make_cull_push(v, f, kN);

    CHECK(cluster_cull_reference(d.mesh, p, draws, kN));
    const uint32_t n = cluster_lod_select_culled(d.mesh, v, f, selected, kN);

    uint32_t gpu_drawn = 0;
    for (uint32_t i = 0; i < kN; i++) {
      const bool gpu = draws[i].instance_count != 0;
      const bool cpu = contains(selected, n, i);
      CHECK(gpu == cpu); // TAM AYNI kume kumesi
      if (gpu) gpu_drawn++;
      // Elenmis olsa bile komut alanlari DOGRU yazilmali: tamponda onceki
      // karenin verisi kalmamali.
      CHECK(draws[i].index_count == d.clusters[i].index_count);
      CHECK(draws[i].first_index == d.clusters[i].index_offset);
      CHECK(draws[i].vertex_offset == 0);
      CHECK(draws[i].first_instance == 0);
    }
    CHECK(gpu_drawn == n);
    CHECK(gpu_drawn >= 2); // hicbir uzaklikta BOS kalmaz
  }
}

ENGINE_TEST(cluster_cull_reference_applies_frustum_culling) {
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

  const LodView v = view_at(8.0f);
  const ClusterCullPush p = make_cull_push(v, f, kN);
  DrawIndexedIndirectCommand draws[kN];
  CHECK(cluster_cull_reference(d.mesh, p, draws, kN));

  CHECK(draws[0].instance_count == 1); // icerde
  CHECK(draws[1].instance_count == 0); // disarida -> ELENDI
  // Elenen kumenin komutu yine de DOLU olmali (cop veri birakilmaz).
  CHECK(draws[1].index_count == d.clusters[1].index_count);

  // CPU yolu ayni sonucu vermeli.
  uint32_t selected[kN];
  const uint32_t n = cluster_lod_select_culled(d.mesh, v, f, selected, kN);
  CHECK(contains(selected, n, 0));
  CHECK(!contains(selected, n, 1));
}

ENGINE_TEST(cluster_cull_reference_rejects_undersized_buffer) {
  // Shader HER kume icin komut yazar. Tampon kucukse sessizce yarim sonuc
  // uretmek, cizim sirasinda tanimsiz komut okumak demektir.
  Dag d = make_dag();
  const ClusterCullPush p = make_cull_push(view_at(32.0f), wide_frustum(), kN);
  DrawIndexedIndirectCommand small[kN - 1];
  CHECK(!cluster_cull_reference(d.mesh, p, small, kN - 1));
  CHECK(!cluster_cull_reference(d.mesh, p, nullptr, kN));

  // Bos mesh: yazacak sey yok, basarili sayilir.
  ClusterLodMesh empty;
  DrawIndexedIndirectCommand out[1];
  CHECK(cluster_cull_reference(empty, p, out, 1));
}
