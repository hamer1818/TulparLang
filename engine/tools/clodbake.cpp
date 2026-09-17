// engine_clodbake — sahne derleyicisinin GEOMETRI dilimi:
//   glTF -> surekli LOD (cluster DAG) -> .clod
//
//   engine_clodbake in.gltf out_prefix [--mesh N] [--tri 124] [--vert 64]
//   (her mesh icin "<out_prefix>.<mesh_index>.clod" yazar)
//
// **SIFIRDAN YAZILMADI.** Butun agir isi Georgy-Khachatryan/
// MeshDecimationTools (MIT, third_party/mdt/, birebir upstream) yapar:
// kume bolme -> gruplama -> QEM kenar cokertme ile sadelestirme -> tekrar
// bolme, ve seviyeler arasi MONOTON hata metrigi. Bu dosya yalnizca glTF'i
// onun girdi yapisina cevirir ve sonucu .clod'a yazar.
//
// **Neden bu kutuphane (clusterlod.h yerine):** ikisi de ayni isi yapiyor
// ama MDT'nin STL bagimliligi YOK (yalniz C basliklari), ayiricisi
// TAKILABILIR, nitelik sureksizliklerini ve coklu materyal dikislerini
// dogru isler, ve her meshlet'te HEM bu seviyenin HEM bir ust seviyenin
// hata metrigini tasir -- boylece calisma zamani grup tablosuna hic
// bakmadan duz tarama yapabilir.
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "content/clod_format.hpp"
#include "content/gltf.hpp"
#include "content/model.hpp"
#include "core/memory/arena.hpp"

#include "mdt/MeshDecimationTools.h"

using namespace tulpar::engine;
using namespace tulpar::engine::content;

namespace {

// MDT'nin ayirici sozlesmesi C realloc()'un aynisi:
//   (NULL, n)  -> ayir      (p, 0) -> serbest birak      (p, n) -> buyut
// Bu bir CEVRIMDISI arac oldugu icin dogrudan libc yeterli; motorun Arena
// kurali calisma zamani icindir ve MDT calisma zamanina girmiyor.
void *mdt_realloc(void *old_block, uint64_t size_bytes, void *user_data) {
  (void)user_data;
  if (size_bytes == 0) {
    std::free(old_block);
    return nullptr;
  }
  return std::realloc(old_block, (size_t)size_bytes);
}

// Sadelestirme sonrasi normaller artik birim uzunlukta DEGILDIR (kenar
// cokertme vertex niteliklerini enterpole eder). MDT bu kanca ile yeniden
// normallestirmeye izin veriyor; kullanmazsak isik hesabi bozulur.
// Duzen: [0..2] normal, [3..4] uv (pozisyon niteliklere DAHIL DEGIL).
void normalize_attributes(float *attributes) {
  const float x = attributes[0], y = attributes[1], z = attributes[2];
  const float len2 = x * x + y * y + z * z;
  if (len2 > 1e-12f) {
    const float inv = 1.0f / std::sqrt(len2);
    attributes[0] = x * inv;
    attributes[1] = y * inv;
    attributes[2] = z * inv;
  } else {
    attributes[0] = 0.0f;
    attributes[1] = 1.0f;
    attributes[2] = 0.0f;
  }
  // uv'ye DOKUNULMAZ: enterpolasyon dogru sonucu zaten verir.
}

ClodMetricRaw to_raw(const MdtErrorMetric &m) {
  ClodMetricRaw r;
  r.center[0] = m.bounds.center.x;
  r.center[1] = m.bounds.center.y;
  r.center[2] = m.bounds.center.z;
  r.radius = m.bounds.radius;
  r.error = m.error;
  return r;
}

bool write_clod(const char *path, const MdtContinuousLodBuildResult &r, uint32_t stride_floats) {
  // MDT meshlet'leri YEREL (8 bit) ucgen indeksleri tutar; biz duz bir
  // 32 bit indeks tamponu yaziyoruz, kume yalniz [offset, offset+count)
  // araligini bilir. Toplam indeks sayisi = ucgen sayisi * 3.
  const uint32_t total_indices = r.meshlet_triangle_count * 3u;

  ClodHeader h{};
  h.magic = kClodMagic;
  h.version = kClodVersion;
  h.cluster_count = r.meshlet_count;
  h.index_count = total_indices;
  h.vertex_count = r.vertex_count;
  h.vertex_stride_floats = stride_floats;
  h.clusters_offset = sizeof(ClodHeader);
  h.indices_offset = h.clusters_offset + h.cluster_count * (uint32_t)sizeof(ClodClusterRaw);
  h.vertices_offset = h.indices_offset + h.index_count * (uint32_t)sizeof(uint32_t);
  h.total_bytes = h.vertices_offset + h.vertex_count * stride_floats * (uint32_t)sizeof(float);
  // Tum yapilar 4'un kati boyutta (static_assert'lerle sabit) -> ofsetler
  // kendiliginden 4 bayta hizali; okuyucu bunu ayrica dogruluyor.

  std::FILE *f = std::fopen(path, "wb");
  if (!f) return false;
  bool ok = std::fwrite(&h, sizeof(h), 1, f) == 1;

  // 1) Kumeler
  uint32_t index_cursor = 0;
  for (uint32_t i = 0; ok && i < r.meshlet_count; i++) {
    const MdtMeshlet &m = r.meshlets[i];
    ClodClusterRaw c{};
    c.center[0] = m.geometric_sphere_bounds.center.x;
    c.center[1] = m.geometric_sphere_bounds.center.y;
    c.center[2] = m.geometric_sphere_bounds.center.z;
    c.radius = m.geometric_sphere_bounds.radius;
    c.current = to_raw(m.current_level_error_metric);
    c.coarser = to_raw(m.coarser_level_error_metric);
    c.index_offset = index_cursor;
    c.index_count = (m.end_meshlet_triangles_index - m.begin_meshlet_triangles_index) * 3u;
    c.geometry = m.geometry_index;
    // MDT seviyeyi grupta tutuyor; kumenin ait oldugu (kaba) grup her zaman
    // gecerlidir (son seviyede bile), oradan okunur.
    c.level = r.meshlet_groups[m.coarser_level_meshlet_group_index].level_of_detail_index;
    index_cursor += c.index_count;
    ok = std::fwrite(&c, sizeof(c), 1, f) == 1;
  }

  // 2) Indeksler: yerel ucgen indekslerini GLOBAL vertex indeksine cevir.
  for (uint32_t i = 0; ok && i < r.meshlet_count; i++) {
    const MdtMeshlet &m = r.meshlets[i];
    for (uint32_t t = m.begin_meshlet_triangles_index; ok && t < m.end_meshlet_triangles_index; t++) {
      const MdtMeshletTriangle tri = r.meshlet_triangles[t];
      const uint32_t base = m.begin_vertex_indices_index;
      const uint32_t v[3] = {r.meshlet_vertex_indices[tri.i0 + base],
                             r.meshlet_vertex_indices[tri.i1 + base],
                             r.meshlet_vertex_indices[tri.i2 + base]};
      ok = std::fwrite(v, sizeof(uint32_t), 3, f) == 3;
    }
  }

  // 3) Vertex tamponu (sadelestirme YENI vertexler uretmis olabilir).
  if (ok && r.vertex_count) {
    const size_t n = (size_t)r.vertex_count * stride_floats;
    ok = std::fwrite(r.vertices, sizeof(float), n, f) == n;
  }

  std::fclose(f);
  return ok;
}

} // namespace

int main(int argc, char **argv) {
  if (argc < 3) {
    std::fprintf(stderr, "kullanim: engine_clodbake in.gltf out_prefix [--mesh N] [--tri 124] [--vert 64]\n");
    return 2;
  }
  const char *in_path = argv[1];
  const char *out_prefix = argv[2];
  int only_mesh = -1;
  uint32_t target_tri = 124, target_vert = 64;
  for (int i = 3; i < argc; i++) {
    if (!std::strcmp(argv[i], "--mesh") && i + 1 < argc) only_mesh = std::atoi(argv[++i]);
    else if (!std::strcmp(argv[i], "--tri") && i + 1 < argc) target_tri = (uint32_t)std::atoi(argv[++i]);
    else if (!std::strcmp(argv[i], "--vert") && i + 1 < argc) target_vert = (uint32_t)std::atoi(argv[++i]);
    else { std::fprintf(stderr, "bilinmeyen secenek: %s\n", argv[i]); return 2; }
  }

  static SystemArena sys;
  if (!sys.reserve(512u << 20, "clodbake")) { std::fprintf(stderr, "arena ayrilamadi\n"); return 1; }

  Model model;
  if (!gltf_load(sys, in_path, &model)) {
    std::fprintf(stderr, "glTF okunamadi: %s (%s)\n", in_path, model.error);
    return 1;
  }

  MdtSystemCallbacks callbacks = {};
  callbacks.temp_allocator.reallocate = &mdt_realloc;
  callbacks.heap_allocator.reallocate = &mdt_realloc;

  constexpr uint32_t kStrideFloats = sizeof(renderer::Vertex) / sizeof(float); // pos+nrm+uv = 8
  // Nitelik agirliklari: [0..2] normal, [3..4] uv. Normal gorsel olarak daha
  // belirleyici oldugu icin daha agir; uv esneyince doku kaymasi olusur ama
  // silueti bozmaz.
  float attribute_weights[kStrideFloats - 3] = {1.0f, 1.0f, 1.0f, 0.5f, 0.5f};

  uint32_t written = 0;
  for (uint32_t mi = 0; mi < model.mesh_count; mi++) {
    if (only_mesh >= 0 && (uint32_t)only_mesh != mi) continue;
    const ModelMesh &m = model.meshes[mi];
    if (!m.indices || m.index_count < 3 || (m.index_count % 3) != 0 || !m.verts || m.vertex_count == 0) {
      std::fprintf(stderr, "mesh %u: bos ya da gecersiz, atlandi\n", mi);
      continue;
    }

    MdtTriangleGeometryDesc geom = {};
    geom.indices = m.indices;
    geom.vertices = reinterpret_cast<const float *>(m.verts); // ILK 12 bayt konum
    geom.index_count = m.index_count;
    geom.vertex_count = m.vertex_count;

    MdtContinuousLodBuildInputs in = {};
    in.mesh.geometry_descs = &geom;
    in.mesh.geometry_desc_count = 1;
    in.mesh.vertex_stride_bytes = sizeof(renderer::Vertex);
    in.mesh.geometric_weight = 1.0f;
    in.mesh.attribute_weights = attribute_weights;
    in.mesh.normalize_vertex_attributes = &normalize_attributes;
    in.meshlet_target_triangle_count = target_tri;
    in.meshlet_target_vertex_count = target_vert;

    MdtContinuousLodBuildResult result = {};
    MdtBuildContinuousLod(&in, &result, &callbacks);
    if (result.meshlet_count == 0) {
      std::fprintf(stderr, "mesh %u: DAG uretilemedi\n", mi);
      MdtFreeContinuousLodBuildResult(&result, &callbacks);
      continue;
    }

    char path[512];
    std::snprintf(path, sizeof(path), "%s.%u.clod", out_prefix, mi);
    const bool ok = write_clod(path, result, kStrideFloats);

    // OLCUM (iddia degil).
    std::printf("mesh %u -> %s | kume %u, grup %u, seviye %u, ucgen %u, vertex %u (girdi: ucgen %u, vertex %u)\n",
                mi, path, result.meshlet_count, result.meshlet_group_count, result.level_count,
                result.meshlet_triangle_count, result.vertex_count, m.index_count / 3, m.vertex_count);

    MdtFreeContinuousLodBuildResult(&result, &callbacks);
    if (!ok) { std::fprintf(stderr, "yazilamadi: %s\n", path); return 1; }
    written++;
  }

  if (written == 0) { std::fprintf(stderr, "hicbir mesh firinlanmadi\n"); return 1; }
  return 0;
}
