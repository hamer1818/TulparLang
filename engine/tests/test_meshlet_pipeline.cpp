// renderer/meshlet_pipeline.hpp: kume insasi + NORMAL KONISI.
//
// Bu dosyanin daha once HIC testi yoktu (motorun "her kapinin kontrolu var"
// kurali ihlal ediliyordu). Asil sinanan sey, koni verisinin gercekten
// hesaplandigi ve arka-yuz elemesinin DOGRU YONDE calistigi -- cunku yanlis
// yonde calisan bir eleme, gorunmesi gereken geometriyi yok eder.
#include "renderer/meshlet_pipeline.hpp"

#include "core/math/vec.hpp"
#include "tests/test.hpp"

using namespace tulpar::engine;
using namespace tulpar::engine::renderer;

namespace {

// XY duzleminde, normali +Z olan bir kare (CCW, +Z'den bakildiginda).
// meshoptimizer ucgen normalini cross(p1-p0, p2-p0) ile hesaplar:
// cross((1,0,0), (1,1,0)) = (0,0,1) -> +Z. Dogrulanabilir olsun diye
// koordinatlar tam sayi secildi.
constexpr float kQuadVerts[] = {
    0.0f, 0.0f, 0.0f, //
    1.0f, 0.0f, 0.0f, //
    1.0f, 1.0f, 0.0f, //
    0.0f, 1.0f, 0.0f,
};
constexpr uint32_t kQuadIndices[] = {0, 1, 2, 0, 2, 3};

// Birim kup: TUM yonlerde normal var -> normal konisi yarim kureden genis
// -> meshoptimizer bunu DEJENERE sayar (meshletutils.cpp:220).
constexpr float kCubeVerts[] = {
    0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f,
    0.0f, 0.0f, 1.0f, 1.0f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f, 0.0f, 1.0f, 1.0f,
};
constexpr uint32_t kCubeIndices[] = {
    0, 2, 1, 0, 3, 2, // -Z
    4, 5, 6, 4, 6, 7, // +Z
    0, 1, 5, 0, 5, 4, // -Y
    3, 7, 6, 3, 6, 2, // +Y
    0, 4, 7, 0, 7, 3, // -X
    1, 2, 6, 1, 6, 5, // +X
};

constexpr uint32_t kStride = 3 * sizeof(float);

} // namespace

ENGINE_TEST(meshlet_capacity_is_sane_and_rejects_empty) {
  const MeshletCapacity c = meshlet_capacity(3000);
  CHECK(c.meshlets > 0);
  CHECK(c.vertex_indices == c.meshlets * kMeshletMaxVertices);
  // meshoptimizer sozlesmesi: 3 bayt/ucgen, kume basina 4'e HIZALANMIS.
  CHECK(c.triangle_bytes == c.meshlets * ((kMeshletMaxTriangles * 3 + 3) & ~3u));

  // Gecersiz girdi: sifir kapasite, cokme YOK.
  CHECK(meshlet_capacity(0).meshlets == 0);
  CHECK(meshlet_capacity(3000, 0, 124).meshlets == 0);
  CHECK(meshlet_capacity(3000, 64, 0).meshlets == 0);
}

ENGINE_TEST(meshlet_build_rejects_invalid_input) {
  const MeshletCapacity cap = meshlet_capacity(6);
  Meshlet ml[8];
  uint32_t vi[8 * kMeshletMaxVertices];
  uint8_t tri[8 * ((kMeshletMaxTriangles * 3 + 3) & ~3u)];
  CHECK(cap.meshlets <= 8);

  CHECK(!build_meshlets(nullptr, 6, kQuadVerts, 4, kStride, ml, vi, tri).ok);
  CHECK(!build_meshlets(kQuadIndices, 6, nullptr, 4, kStride, ml, vi, tri).ok);
  // index_count 3'un kati DEGIL
  CHECK(!build_meshlets(kQuadIndices, 5, kQuadVerts, 4, kStride, ml, vi, tri).ok);
  CHECK(!build_meshlets(kQuadIndices, 0, kQuadVerts, 4, kStride, ml, vi, tri).ok);
  CHECK(!build_meshlets(kQuadIndices, 6, kQuadVerts, 0, kStride, ml, vi, tri).ok);
  // stride 0: konum okumasi anlamsiz olurdu
  CHECK(!build_meshlets(kQuadIndices, 6, kQuadVerts, 4, 0, ml, vi, tri).ok);
}

ENGINE_TEST(meshlet_build_produces_valid_clusters_and_bounds) {
  Meshlet ml[8];
  uint32_t vi[8 * kMeshletMaxVertices];
  uint8_t tri[8 * ((kMeshletMaxTriangles * 3 + 3) & ~3u)];

  const MeshletBuildResult r = build_meshlets(kQuadIndices, 6, kQuadVerts, 4, kStride, ml, vi, tri);
  CHECK(r.ok);
  CHECK(r.meshlet_count == 1); // 2 ucgen tek kumeye sigar
  const Meshlet &m = ml[0];
  CHECK(m.triangle_count == 2);
  CHECK(m.vertex_count == 4);

  // Kume vertex indeksleri ASIL mesh'in sinirlari icinde olmali.
  for (uint32_t v = 0; v < m.vertex_count; v++) CHECK(vi[m.vertex_offset + v] < 4);

  // AABB karenin TAMAMINI kapsamali (z duzlemsel: 0..0).
  CHECK(m.bounds.min.x <= 0.0f && m.bounds.max.x >= 1.0f);
  CHECK(m.bounds.min.y <= 0.0f && m.bounds.max.y >= 1.0f);

  // Kure siniri: merkez karenin uzerinde, yaricap kosegenin yarisini
  // (sqrt(2)/2 ~= 0.7071) kapsayacak kadar buyuk olmali.
  CHECK(m.sphere.radius >= 0.7f);
  // Tum noktalar z=0 duzleminde; merkez de orada olmali (kutuphanenin
  // ic algoritmasina tam esitlikle baglanmamak icin tolerans).
  CHECK(m.sphere.center.z > -1e-5f && m.sphere.center.z < 1e-5f);
}

ENGINE_TEST(meshlet_cone_culls_only_from_behind) {
  // ASIL TEST: koni verisi gercekten hesaplaniyor mu ve eleme DOGRU YONDE mi?
  // Kare normali +Z. Yani +Z tarafindaki kamera ON yuzu gorur (ELEMEZ),
  // -Z tarafindaki kamera ARKA yuzu gorur (ELER).
  Meshlet ml[8];
  uint32_t vi[8 * kMeshletMaxVertices];
  uint8_t tri[8 * ((kMeshletMaxTriangles * 3 + 3) & ~3u)];
  const MeshletBuildResult r = build_meshlets(kQuadIndices, 6, kQuadVerts, 4, kStride, ml, vi, tri);
  CHECK(r.ok);
  const Meshlet &m = ml[0];

  // Duz bir kumede tum normaller ayni -> mindp = 1 -> cutoff = sqrt(1-1) = 0.
  // (Dejenere durumun sentineli olan cutoff == 1 DEGIL.)
  CHECK(m.cone.cutoff < 0.001f);
  // Koni ekseni yuzey normali (+Z) olmali.
  CHECK(m.cone.axis.z > 0.99f);

  CHECK(!meshlet_backfacing(m, Vec3{0.5f, 0.5f, 5.0f}));  // ONDEN: cizilir
  CHECK(meshlet_backfacing(m, Vec3{0.5f, 0.5f, -5.0f}));  // ARKADAN: elenir
  // Kenardan bakis: yaricap payi yuzunden elenmemeli (yanlis eleme,
  // gorunmesi gereken geometriyi yok ederdi).
  CHECK(!meshlet_backfacing(m, Vec3{20.0f, 0.5f, 0.0f}));
}

ENGINE_TEST(meshlet_degenerate_cone_never_culls) {
  // Kup: normaller her yone bakar -> koni yarim kureden genis ->
  // meshoptimizer DEJENERE isaretler (cutoff = 1, eksen SIFIR birakilir).
  // Boyle bir kume HICBIR yonden elenmemeli, yoksa kapali geometri kaybolur.
  Meshlet ml[16];
  uint32_t vi[16 * kMeshletMaxVertices];
  uint8_t tri[16 * ((kMeshletMaxTriangles * 3 + 3) & ~3u)];
  const MeshletBuildResult r = build_meshlets(kCubeIndices, 36, kCubeVerts, 8, kStride, ml, vi, tri);
  CHECK(r.ok);
  CHECK(r.meshlet_count == 1); // 12 ucgen tek kumeye sigar

  const Meshlet &m = ml[0];
  CHECK(m.cone.cutoff == 1.0f); // DEJENERE sentineli
  CHECK(length(m.cone.axis) == 0.0f);

  // Alti ana yonden de: ASLA elenmez.
  CHECK(!meshlet_backfacing(m, Vec3{10, 0.5f, 0.5f}));
  CHECK(!meshlet_backfacing(m, Vec3{-10, 0.5f, 0.5f}));
  CHECK(!meshlet_backfacing(m, Vec3{0.5f, 10, 0.5f}));
  CHECK(!meshlet_backfacing(m, Vec3{0.5f, -10, 0.5f}));
  CHECK(!meshlet_backfacing(m, Vec3{0.5f, 0.5f, 10}));
  CHECK(!meshlet_backfacing(m, Vec3{0.5f, 0.5f, -10}));
}

ENGINE_TEST(meshlet_default_constructed_is_never_culled) {
  // Insa edilmemis bir Meshlet yanlislikla elenmemeli. Varsayilan eksen
  // {0,0,1} olsaydi -Z'den bakan kamera onu arka yuz sanardi.
  Meshlet m{};
  CHECK(!meshlet_backfacing(m, Vec3{0, 0, -10}));
  CHECK(!meshlet_backfacing(m, Vec3{0, 0, 10}));
  CHECK(!meshlet_backfacing(m, Vec3{5, -3, 2}));
}
