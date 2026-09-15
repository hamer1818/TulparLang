#include "content/meshopt.hpp"

#include <cstdlib>
#include <cstring>

#include <meshoptimizer.h>

#include "content/cluster_dag.hpp"
#include "content/hash.hpp"

namespace tulpar::engine::content {

bool mesh_optimize(Arena &arena, ModelMesh &mm, ModelOptStats *stats) {
  if (mm.index_count < 3 || mm.vertex_count == 0) return true;
  const size_t vsz = sizeof(renderer::Vertex);
  const float *pos = &mm.verts[0].pos.x;
  meshopt_VertexCacheStatistics vc0 = meshopt_analyzeVertexCache(mm.indices, mm.index_count, mm.vertex_count, 16, 0, 0);
  meshopt_OverdrawStatistics od0 = meshopt_analyzeOverdraw(mm.indices, mm.index_count, pos, mm.vertex_count, vsz);

  // 1. Tekillestirme (ayni pos/nrm/uv olan vertex'ler birlesir).
  unsigned *remap = static_cast<unsigned *>(std::malloc(sizeof(unsigned) * mm.vertex_count));
  if (!remap) return false;
  const size_t nv = meshopt_generateVertexRemap(remap, mm.indices, mm.index_count, mm.verts, mm.vertex_count, vsz);
  uint32_t *idx = arena.alloc_array<uint32_t>(mm.index_count);
  renderer::Vertex *vb = arena.alloc_array<renderer::Vertex>((uint32_t)nv);
  if (!idx || !vb) { std::free(remap); return false; }
  meshopt_remapIndexBuffer(idx, mm.indices, mm.index_count, remap);
  meshopt_remapVertexBuffer(vb, mm.verts, mm.vertex_count, vsz, remap);
  std::free(remap);
  // 2. Vertex cache, 3. overdraw (kucuk ACMR odunu), 4. fetch sirasi (yerinde).
  uint32_t *tmp = static_cast<uint32_t *>(std::malloc(sizeof(uint32_t) * mm.index_count));
  if (!tmp) return false;
  meshopt_optimizeVertexCache(tmp, idx, mm.index_count, nv);
  meshopt_optimizeOverdraw(idx, tmp, mm.index_count, &vb[0].pos.x, nv, vsz, 1.05f);
  std::free(tmp);
  renderer::Vertex *vb2 = arena.alloc_array<renderer::Vertex>((uint32_t)nv);
  if (!vb2) return false;
  const size_t nv2 = meshopt_optimizeVertexFetch(vb2, idx, mm.index_count, vb, nv, vsz);

  meshopt_VertexCacheStatistics vc1 = meshopt_analyzeVertexCache(idx, mm.index_count, nv2, 16, 0, 0);
  meshopt_OverdrawStatistics od1 = meshopt_analyzeOverdraw(idx, mm.index_count, &vb2[0].pos.x, nv2, vsz);
  if (stats) {
    const float tri = (float)(mm.index_count / 3);
    const float wsum = (float)stats->triangles + tri;
    auto blend = [&](float acc, float v) { return (acc * (float)stats->triangles + v * tri) / wsum; };
    stats->acmr_before = blend(stats->acmr_before, vc0.acmr);
    stats->acmr_after = blend(stats->acmr_after, vc1.acmr);
    stats->overdraw_before = blend(stats->overdraw_before, od0.overdraw);
    stats->overdraw_after = blend(stats->overdraw_after, od1.overdraw);
    stats->vertices_before += mm.vertex_count;
    stats->vertices_after += (uint32_t)nv2;
    stats->triangles += (uint32_t)tri;
  }
  mm.indices = idx;
  mm.verts = vb2;
  mm.vertex_count = (uint32_t)nv2;
  return true;
}

bool mesh_build_lods(Arena &arena, ModelMesh &mm, const float *ratios, uint32_t n, float target_error) {
  if (n > kModelMaxLods) n = kModelMaxLods;
  const size_t vsz = sizeof(renderer::Vertex);
  for (uint32_t i = 0; i < n; i++) {
    mm.lod_indices[i] = nullptr;
    mm.lod_index_count[i] = 0;
    mm.lod_error[i] = 0;
    if (mm.index_count < 12) continue;
    uint32_t *dst = arena.alloc_array<uint32_t>(mm.index_count);
    if (!dst) return false;
    size_t target = (size_t)((float)mm.index_count * ratios[i]);
    target -= target % 3;
    float err = 0;
    const size_t got = meshopt_simplify(dst, mm.indices, mm.index_count, &mm.verts[0].pos.x, mm.vertex_count, vsz, target,
                                        target_error, 0, &err);
    if (got == 0 || got > (size_t)((float)mm.index_count * 0.9f)) continue; // hedefe yaklasamadi: seviye yok
    mm.lod_indices[i] = dst;
    mm.lod_index_count[i] = (uint32_t)got;
    mm.lod_error[i] = err;
  }
  return true;
}

// ===========================================================================
// KUME (cluster) DAG — sozlesme ve gerekce: content/cluster_dag.hpp.
// Burada: kumeleme (meshopt_buildMeshlets) -> gruplama (partitionClusters) ->
// GRUP SINIRI KILITLI sadelestirme (simplifyWithAttributes + vertex_lock) ->
// bir ust seviye kume. Ayni dosyada duruyor cunku engine_content kaynak
// listesi elle tutuluyor (engine/CMakeLists.txt); mantik olarak ayri katman.
// ===========================================================================
namespace {

// --- acik adresleme u64 kumesi / haritasi (STL yok, malloc; kapasite sabit) ---
inline uint64_t mix64(uint64_t x) {
  x += 0x9e3779b97f4a7c15ull;
  x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ull;
  x = (x ^ (x >> 27)) * 0x94d049bb133111ebull;
  return x ^ (x >> 31);
}
struct U64Map {
  uint64_t *keys = nullptr; // 0 = bos (anahtar +1 saklanir)
  uint32_t *vals = nullptr;
  uint32_t mask = 0, count = 0;
  bool init(uint32_t want, bool with_vals) {
    uint32_t cap = 16;
    while (cap < (want ? want * 3 : 16)) cap <<= 1;
    keys = static_cast<uint64_t *>(std::calloc(cap, sizeof(uint64_t)));
    vals = with_vals ? static_cast<uint32_t *>(std::calloc(cap, sizeof(uint32_t))) : nullptr;
    mask = cap - 1;
    count = 0;
    return keys && (!with_vals || vals);
  }
  void clear() {
    if (keys) std::memset(keys, 0, ((size_t)mask + 1) * sizeof(uint64_t));
    count = 0;
  }
  void release() { std::free(keys); std::free(vals); keys = nullptr; vals = nullptr; }
  uint32_t slot(uint64_t k) const {
    uint32_t i = (uint32_t)(mix64(k + 1) & mask);
    while (keys[i] && keys[i] != k + 1) i = (i + 1) & mask;
    return i;
  }
  bool insert(uint64_t k, uint32_t v = 0) { // true: yeni
    const uint32_t i = slot(k);
    if (keys[i]) return false;
    keys[i] = k + 1;
    if (vals) vals[i] = v;
    count++;
    return true;
  }
  bool has(uint64_t k) const { return keys[slot(k)] != 0; }
  const uint32_t *find(uint64_t k) const {
    const uint32_t i = slot(k);
    return keys[i] ? &vals[i] : nullptr;
  }
};
inline uint64_t edge_key(uint32_t a, uint32_t b) {
  return a < b ? ((uint64_t)a << 32) | b : ((uint64_t)b << 32) | a;
}

// Pozisyon temsilcisi: BIT BIT ayni 12 bayta sahip vertex'ler tek numaraya
// iner (dikis vertex'leri catlak sayilmasin). Ayrica kompakt float3 dizisi
// (meshoptimizer'a stride 12 ile verilir).
bool build_positions(const ModelMesh &mm, float **out_pos, uint32_t **out_prep) {
  float *pos = static_cast<float *>(std::malloc(sizeof(float) * 3 * mm.vertex_count));
  uint32_t *prep = static_cast<uint32_t *>(std::malloc(sizeof(uint32_t) * mm.vertex_count));
  if (!pos || !prep) { std::free(pos); std::free(prep); return false; }
  for (uint32_t v = 0; v < mm.vertex_count; v++) {
    pos[v * 3 + 0] = mm.verts[v].pos.x;
    pos[v * 3 + 1] = mm.verts[v].pos.y;
    pos[v * 3 + 2] = mm.verts[v].pos.z;
  }
  meshopt_generateVertexRemap(prep, nullptr, mm.vertex_count, pos, mm.vertex_count, sizeof(float) * 3);
  *out_pos = pos;
  *out_prep = prep;
  return true;
}

struct DagBuilder {
  const ModelMesh *mm = nullptr;
  ClusterDagOptions opt;
  float *pos = nullptr;
  uint32_t *prep = nullptr;
  ClusterNode *nodes = nullptr; uint32_t node_cap = 0, node_n = 0;
  uint32_t *idx = nullptr;      uint32_t idx_cap = 0, idx_n = 0;
  uint32_t *kids = nullptr;     uint32_t kid_cap = 0, kid_n = 0;
  meshopt_Meshlet *ml = nullptr; uint32_t *mlv = nullptr; uint8_t *mlt = nullptr;
  uint32_t *grp = nullptr, *simp = nullptr;   // grup indeksleri / sadelestirme ciktisi
  uint8_t *lock = nullptr;
  uint32_t *part = nullptr, *cl_counts = nullptr;
  uint32_t *owner = nullptr; uint8_t *shared = nullptr;
  uint32_t group_next = 0;
  bool ok = true;

  bool reserve() {
    const size_t bound = meshopt_buildMeshletsBound(mm->index_count, opt.max_vertices, opt.max_triangles);
    node_cap = (uint32_t)bound * 3 + 16;
    idx_cap = mm->index_count * 3 + 64;
    kid_cap = node_cap * 8;
    nodes = static_cast<ClusterNode *>(std::calloc(node_cap, sizeof(ClusterNode)));
    idx = static_cast<uint32_t *>(std::malloc(sizeof(uint32_t) * idx_cap));
    kids = static_cast<uint32_t *>(std::malloc(sizeof(uint32_t) * kid_cap));
    ml = static_cast<meshopt_Meshlet *>(std::malloc(sizeof(meshopt_Meshlet) * bound));
    mlv = static_cast<uint32_t *>(std::malloc(sizeof(uint32_t) * mm->index_count));
    mlt = static_cast<uint8_t *>(std::malloc(mm->index_count));
    grp = static_cast<uint32_t *>(std::malloc(sizeof(uint32_t) * mm->index_count));
    simp = static_cast<uint32_t *>(std::malloc(sizeof(uint32_t) * mm->index_count));
    lock = static_cast<uint8_t *>(std::malloc(mm->vertex_count));
    part = static_cast<uint32_t *>(std::malloc(sizeof(uint32_t) * node_cap));
    cl_counts = static_cast<uint32_t *>(std::malloc(sizeof(uint32_t) * node_cap));
    owner = static_cast<uint32_t *>(std::malloc(sizeof(uint32_t) * mm->vertex_count));
    shared = static_cast<uint8_t *>(std::malloc(mm->vertex_count));
    return nodes && idx && kids && ml && mlv && mlt && grp && simp && lock && part && cl_counts && owner && shared;
  }
  void release() {
    std::free(pos); std::free(prep); std::free(nodes); std::free(idx); std::free(kids);
    std::free(ml); std::free(mlv); std::free(mlt); std::free(grp); std::free(simp);
    std::free(lock); std::free(part); std::free(cl_counts); std::free(owner); std::free(shared);
  }
  // src ucgenlerini kumelere boler, dugumleri ekler. Donus: eklenen kume sayisi.
  uint32_t emit(const uint32_t *src, uint32_t src_count, uint32_t level, float error, uint32_t child_first, uint32_t child_count) {
    if (!ok) return 0;
    const size_t n = meshopt_buildMeshlets(ml, mlv, mlt, src, src_count, pos, mm->vertex_count, sizeof(float) * 3, opt.max_vertices,
                                           opt.max_triangles, opt.cone_weight);
    uint32_t made = 0;
    for (size_t i = 0; i < n; i++) {
      const meshopt_Meshlet &m = ml[i];
      if (m.triangle_count == 0) continue;
      const uint32_t first = idx_n, ic = m.triangle_count * 3;
      if (idx_n + ic > idx_cap || node_n >= node_cap) { ok = false; return made; }
      for (uint32_t t = 0; t < ic; t++) idx[idx_n++] = mlv[m.vertex_offset + mlt[m.triangle_offset + t]];
      const meshopt_Bounds b = meshopt_computeClusterBounds(idx + first, ic, pos, mm->vertex_count, sizeof(float) * 3);
      ClusterNode &nd = nodes[node_n];
      std::memset(&nd, 0, sizeof nd);
      for (int k = 0; k < 3; k++) { nd.center[k] = b.center[k]; nd.cone_apex[k] = b.cone_apex[k]; nd.cone_axis[k] = b.cone_axis[k]; }
      nd.radius = b.radius;
      nd.cone_cutoff = b.cone_cutoff;
      nd.error = error;
      nd.parent_error = kClusterErrorInf; // ustu yok: kok (bir grup onu yukari tasirsa duzelir)
      nd.index_offset = first;
      nd.index_count = ic;
      nd.level = level;
      nd.child_offset = child_first;
      nd.child_count = child_count;
      nd.group = kClusterNoGroup;
      node_n++;
      made++;
    }
    return made;
  }
};
} // namespace

ClusterDagOptions cluster_dag_preset(DeviceClass c) {
  ClusterDagOptions o;
  switch (c) {
    case DeviceClass::Low: // az cizim / az dugum: buyuk kume, genis grup, sig agac
      o.max_triangles = 128; o.max_vertices = 128; o.group_size = 6; o.max_levels = 4;
      break;
    case DeviceClass::High: // ince cull cozunurlugu: kucuk kume, tam derinlik
      o.max_triangles = 64; o.max_vertices = 96; o.group_size = 4; o.max_levels = kClusterDagMaxLevels;
      break;
    case DeviceClass::Mid:
    default:
      o.max_triangles = 124; o.max_vertices = 128; o.group_size = 4; o.max_levels = 8;
      break;
  }
  return o;
}
const char *device_class_name(DeviceClass c) {
  switch (c) {
    case DeviceClass::Low: return "dusuk";
    case DeviceClass::High: return "yuksek";
    default: return "orta";
  }
}

bool cluster_dag_build(Arena &arena, const ModelMesh &mm, const ClusterDagOptions &opt_in, ClusterDag *out) {
  *out = ClusterDag{};
  if (mm.index_count < 3 || mm.vertex_count == 0 || !mm.verts || !mm.indices) return false;
  DagBuilder B;
  B.mm = &mm;
  B.opt = opt_in;
  if (B.opt.max_triangles < 16) B.opt.max_triangles = 16;
  if (B.opt.max_triangles > 512) B.opt.max_triangles = 512;
  if (B.opt.max_vertices < 32) B.opt.max_vertices = 32;
  if (B.opt.max_vertices > 256) B.opt.max_vertices = 256;
  if (B.opt.group_size < 2) B.opt.group_size = 2;
  if (B.opt.max_levels < 1) B.opt.max_levels = 1;
  if (B.opt.max_levels > kClusterDagMaxLevels) B.opt.max_levels = kClusterDagMaxLevels;
  if (B.opt.cone_weight < 0) B.opt.cone_weight = 0;
  if (B.opt.cone_weight > 1) B.opt.cone_weight = 1;
  if (!(B.opt.simplify_ratio > 0.05f && B.opt.simplify_ratio < 0.95f)) B.opt.simplify_ratio = 0.5f;
  if (!build_positions(mm, &B.pos, &B.prep) || !B.reserve()) { B.release(); return false; }

  ClusterDag dag;
  dag.vertex_count = mm.vertex_count;
  // --- Seviye 0: ham ucgenler -> kumeler (hata 0: orijinal geometri).
  uint32_t lvl_first = 0;
  uint32_t lvl_count = B.emit(mm.indices, mm.index_count, 0, 0.0f, 0, 0);
  if (!B.ok || lvl_count == 0) { B.release(); return false; }
  dag.levels = 1;
  dag.level_first[0] = 0;
  dag.level_count[0] = lvl_count;

  // --- Seviye dongusu: grupla, grup siniri kilitli sadelestir, yeniden kumele.
  for (uint32_t level = 0; level + 1 < B.opt.max_levels && lvl_count > 1; level++) {
    const uint32_t base_idx = B.nodes[lvl_first].index_offset;
    uint32_t total = 0;
    for (uint32_t i = 0; i < lvl_count; i++) {
      B.cl_counts[i] = B.nodes[lvl_first + i].index_count;
      total += B.cl_counts[i];
    }
    const size_t nparts = meshopt_partitionClusters(B.part, B.idx + base_idx, total, B.cl_counts, lvl_count, B.pos, mm.vertex_count,
                                                    sizeof(float) * 3, B.opt.group_size);
    if (nparts == 0 || nparts >= lvl_count) break; // gruplasma yok: agac burada biter

    // Grup sahipligi: bir pozisyonu birden fazla grup kullaniyorsa o pozisyon
    // GRUP SINIRINDA demektir -> sadelestirmede kilitlenir.
    for (uint32_t v = 0; v < mm.vertex_count; v++) { B.owner[v] = 0xFFFFFFFFu; B.shared[v] = 0; }
    for (uint32_t i = 0; i < lvl_count; i++) {
      const ClusterNode &nd = B.nodes[lvl_first + i];
      const uint32_t g = B.part[i];
      for (uint32_t k = 0; k < nd.index_count; k++) {
        const uint32_t p = B.prep[B.idx[nd.index_offset + k]];
        if (B.owner[p] == 0xFFFFFFFFu) B.owner[p] = g;
        else if (B.owner[p] != g) B.shared[p] = 1;
      }
    }
    for (uint32_t v = 0; v < mm.vertex_count; v++)
      B.lock[v] = (B.opt.lock_group_border && B.shared[B.prep[v]]) ? (uint8_t)meshopt_SimplifyVertex_Lock : (uint8_t)0;

    const uint32_t next_first = B.node_n;
    for (uint32_t g = 0; g < (uint32_t)nparts; g++) {
      uint32_t gc = 0, members = 0;
      for (uint32_t i = 0; i < lvl_count; i++) {
        if (B.part[i] != g) continue;
        const ClusterNode &nd = B.nodes[lvl_first + i];
        std::memcpy(B.grp + gc, B.idx + nd.index_offset, sizeof(uint32_t) * nd.index_count);
        gc += nd.index_count;
        members++;
      }
      if (members == 0 || gc < 12) continue; // cok kucuk: bu kumeler kok kalir
      size_t target = (size_t)((float)gc * B.opt.simplify_ratio);
      target -= target % 3;
      if (target < 3) target = 3;
      float err = 0;
      const size_t got = meshopt_simplifyWithAttributes(B.simp, B.grp, gc, B.pos, mm.vertex_count, sizeof(float) * 3, nullptr, 0,
                                                        nullptr, 0, B.lock, target, B.opt.max_error,
                                                        meshopt_SimplifyErrorAbsolute, &err);
      // Hedefe yaklasamadiysa (kilitli sinir yuzunden olabilir) grup kok kalir.
      if (got < 3 || got > (size_t)((float)gc * 0.9f)) continue;
      // MONOTON HATA: ust, butun cocuklarindan buyuk olmali (esitlik cut'ta
      // cocugu SECILEMEZ yapardi: error <= t < parent_error bos aralik).
      float child_max = 0;
      for (uint32_t i = 0; i < lvl_count; i++)
        if (B.part[i] == g && B.nodes[lvl_first + i].error > child_max) child_max = B.nodes[lvl_first + i].error;
      float perr = err > child_max ? err : child_max;
      if (!(perr > child_max)) perr = child_max > 0 ? child_max * 1.0001f + 1e-7f : 1e-6f;
      if (perr >= kClusterErrorInf) perr = kClusterErrorInf * 0.5f;

      const uint32_t kid_first = B.kid_n;
      const uint32_t group_id = B.group_next++;
      for (uint32_t i = 0; i < lvl_count; i++) {
        if (B.part[i] != g) continue;
        if (B.kid_n >= B.kid_cap) { B.ok = false; break; }
        B.kids[B.kid_n++] = lvl_first + i;
        B.nodes[lvl_first + i].group = group_id;
        B.nodes[lvl_first + i].parent_error = perr; // ayni grup = ayni esik = birlikte gecis
      }
      if (!B.ok) break;
      const uint32_t made = B.emit(B.simp, (uint32_t)got, level + 1, perr, kid_first, B.kid_n - kid_first);
      if (!B.ok) break;
      if (made == 0) { // kume uretilemedi: geri al, grup kok kalsin
        B.kid_n = kid_first;
        B.group_next--;
        for (uint32_t i = 0; i < lvl_count; i++)
          if (B.part[i] == g) { B.nodes[lvl_first + i].group = kClusterNoGroup; B.nodes[lvl_first + i].parent_error = kClusterErrorInf; }
      }
    }
    if (!B.ok) break;
    const uint32_t made = B.node_n - next_first;
    if (made == 0) break; // hicbir grup sadelesmedi
    lvl_first = next_first;
    lvl_count = made;
    dag.level_first[level + 1] = lvl_first;
    dag.level_count[level + 1] = made;
    dag.levels = level + 2;
  }
  if (!B.ok) { B.release(); return false; }

  // --- Arenaya kopyala + olcum satirlari + ozet.
  ClusterNode *nodes = arena.alloc_array_zeroed<ClusterNode>(B.node_n);
  uint32_t *indices = arena.alloc_array<uint32_t>(B.idx_n);
  uint32_t *children = B.kid_n ? arena.alloc_array<uint32_t>(B.kid_n) : nullptr;
  if (!nodes || !indices || (B.kid_n && !children)) { B.release(); return false; }
  std::memcpy(nodes, B.nodes, sizeof(ClusterNode) * B.node_n);
  std::memcpy(indices, B.idx, sizeof(uint32_t) * B.idx_n);
  if (B.kid_n) std::memcpy(children, B.kids, sizeof(uint32_t) * B.kid_n);
  dag.nodes = nodes; dag.node_count = B.node_n;
  dag.indices = indices; dag.index_count = B.idx_n;
  dag.children = children; dag.child_count = B.kid_n;
  dag.group_count = B.group_next;
  for (uint32_t l = 0; l < dag.levels; l++) {
    uint32_t tris = 0;
    float emax = 0;
    for (uint32_t i = 0; i < dag.level_count[l]; i++) {
      const ClusterNode &nd = nodes[dag.level_first[l] + i];
      tris += nd.index_count / 3;
      if (nd.error > emax) emax = nd.error;
    }
    dag.level_tris[l] = tris;
    dag.level_error[l] = emax;
  }
  for (uint32_t i = 0; i < dag.node_count; i++)
    if (nodes[i].parent_error >= kClusterErrorInf) dag.root_count++;
  uint64_t h = content_fnv1a(nodes, sizeof(ClusterNode) * dag.node_count);
  h = content_fnv1a(indices, sizeof(uint32_t) * dag.index_count, h);
  if (dag.child_count) h = content_fnv1a(children, sizeof(uint32_t) * dag.child_count, h);
  dag.hash = h;
  *out = dag;
  B.release();
  return true;
}

bool cluster_dag_check_seams(const ClusterDag &dag, const ModelMesh &mm, ClusterDagSeamReport *out) {
  *out = ClusterDagSeamReport{};
  if (!dag.nodes || dag.node_count == 0 || dag.vertex_count != mm.vertex_count) return false;
  float *pos = nullptr;
  uint32_t *prep = nullptr;
  if (!build_positions(mm, &pos, &prep)) return false;
  std::free(pos);
  // Grup basina ust kume listesi: bir ust kumenin butun cocuklari ayni gruptadir.
  U64Map edges, border, seen, parent_edges, parent_verts, border_verts;
  const uint32_t emax = dag.index_count + 16;
  bool alloc_ok = edges.init(emax, true) && border.init(emax, false) && seen.init(emax, false) && parent_edges.init(emax, false) &&
                  parent_verts.init(emax, false) && border_verts.init(emax, false);
  if (!alloc_ok) { edges.release(); border.release(); seen.release(); parent_edges.release(); parent_verts.release(); border_verts.release(); std::free(prep); return false; }

  auto eff_group = [&](uint32_t n) { return dag.nodes[n].group == kClusterNoGroup ? (0x80000000u | n) : dag.nodes[n].group; };

  for (uint32_t l = 0; l + 1 < dag.levels; l++) {
    const uint32_t first = dag.level_first[l], count = dag.level_count[l];
    edges.clear();
    border.clear();
    border_verts.clear();
    // 1) Bu seviyedeki butun kenarlar: iki FARKLI gruba dokunan kenar = grup siniri.
    for (uint32_t i = 0; i < count; i++) {
      const ClusterNode &nd = dag.nodes[first + i];
      const uint32_t g = eff_group(first + i);
      for (uint32_t t = 0; t < nd.index_count; t += 3) {
        const uint32_t a = prep[dag.indices[nd.index_offset + t]], b = prep[dag.indices[nd.index_offset + t + 1]],
                       c = prep[dag.indices[nd.index_offset + t + 2]];
        const uint32_t tri[3] = {a, b, c};
        for (int e = 0; e < 3; e++) {
          const uint32_t u = tri[e], v = tri[(e + 1) % 3];
          if (u == v) continue;
          const uint64_t k = edge_key(u, v);
          if (!edges.insert(k, g)) {
            const uint32_t *have = edges.find(k);
            if (have && *have != g) { border.insert(k); border_verts.insert(u); border_verts.insert(v); }
          }
        }
      }
    }
    // 2) Her grup icin: ust kumelerin kenar kumesi kuruluyor, grup sinir
    //    kenarlari orada hala var mi diye bakiliyor. Yoksa = CATLAK.
    for (uint32_t gi = 0; gi < dag.group_count; gi++) {
      uint32_t parents = 0;
      parent_edges.clear();
      parent_verts.clear();
      for (uint32_t n = 0; n < dag.node_count; n++) {
        const ClusterNode &pn = dag.nodes[n];
        if (pn.level != l + 1 || pn.child_count == 0) continue;
        if (dag.nodes[dag.children[pn.child_offset]].group != gi) continue;
        parents++;
        for (uint32_t t = 0; t < pn.index_count; t += 3) {
          const uint32_t tri[3] = {prep[dag.indices[pn.index_offset + t]], prep[dag.indices[pn.index_offset + t + 1]],
                                   prep[dag.indices[pn.index_offset + t + 2]]};
          for (int e = 0; e < 3; e++) {
            if (tri[e] == tri[(e + 1) % 3]) continue;
            parent_edges.insert(edge_key(tri[e], tri[(e + 1) % 3]));
            parent_verts.insert(tri[e]);
          }
        }
      }
      if (parents == 0) continue; // bu seviyede bu grup yok (ya da kok kaldi)
      out->groups_simplified++;
      seen.clear();
      for (uint32_t i = 0; i < count; i++) {
        if (dag.nodes[first + i].group != gi) continue;
        const ClusterNode &nd = dag.nodes[first + i];
        for (uint32_t t = 0; t < nd.index_count; t += 3) {
          const uint32_t tri[3] = {prep[dag.indices[nd.index_offset + t]], prep[dag.indices[nd.index_offset + t + 1]],
                                   prep[dag.indices[nd.index_offset + t + 2]]};
          for (int e = 0; e < 3; e++) {
            const uint32_t u = tri[e], v = tri[(e + 1) % 3];
            if (u == v) continue;
            const uint64_t k = edge_key(u, v);
            if (!border.has(k) || !seen.insert(k)) continue;
            out->border_edges++;
            if (!parent_edges.has(k)) out->border_edges_lost++;
          }
          for (int e = 0; e < 3; e++) {
            if (!border_verts.has(tri[e]) || !seen.insert(0x4000000000000000ull | tri[e])) continue;
            out->border_verts++;
            if (!parent_verts.has(tri[e])) out->border_verts_lost++;
          }
        }
      }
    }
    out->levels_checked++;
  }
  edges.release(); border.release(); seen.release(); parent_edges.release(); parent_verts.release(); border_verts.release();
  std::free(prep);
  return true;
}

bool cluster_dag_bake(Arena &arena, const ClusterDagBakeItem *items, uint32_t n, SceneBlobExtras *x) {
  if (!items || n == 0 || !x) return false;
  uint32_t nodes = 0, indices = 0, children = 0, levels = 0;
  for (uint32_t i = 0; i < n; i++) {
    if (!items[i].dag || items[i].dag->node_count == 0) return false;
    nodes += items[i].dag->node_count;
    indices += items[i].dag->index_count;
    children += items[i].dag->child_count;
    if (items[i].dag->levels > levels) levels = items[i].dag->levels;
  }
  SceneBlobDagMesh *dm = arena.alloc_array_zeroed<SceneBlobDagMesh>(n);
  SceneBlobDagNode *dn = arena.alloc_array_zeroed<SceneBlobDagNode>(nodes);
  uint32_t *di = arena.alloc_array<uint32_t>(indices);
  uint32_t *dc = children ? arena.alloc_array<uint32_t>(children) : nullptr;
  if (!dm || !dn || !di || (children && !dc)) return false;
  uint32_t no = 0, io = 0, co = 0;
  for (uint32_t i = 0; i < n; i++) {
    const ClusterDag &d = *items[i].dag;
    dm[i].asset = items[i].asset;
    dm[i].mesh = items[i].mesh;
    dm[i].levels = d.levels;
    dm[i].flags = 0;
    dm[i].node_first = no; dm[i].node_count = d.node_count;
    dm[i].index_first = io; dm[i].index_count = d.index_count;
    dm[i].child_first = co; dm[i].child_count = d.child_count;
    dm[i].vertex_count = d.vertex_count;
    dm[i].reserved = 0;
    for (uint32_t k = 0; k < d.node_count; k++) {
      SceneBlobDagNode nd = d.nodes[k];
      nd.index_offset += io;   // blob tablosuna gore
      nd.child_offset += co;
      dn[no + k] = nd;
    }
    std::memcpy(di + io, d.indices, sizeof(uint32_t) * d.index_count);
    for (uint32_t k = 0; k < d.child_count; k++) dc[co + k] = d.children[k] + no; // global dugum dizini
    no += d.node_count; io += d.index_count; co += d.child_count;
  }
  x->dag_meshes = dm; x->dag_mesh_count = n;
  x->dag_nodes = dn; x->dag_node_count = nodes;
  x->dag_indices = di; x->dag_index_count = indices;
  x->dag_children = dc; x->dag_child_count = children;
  x->dag_levels = levels;
  return true;
}

} // namespace tulpar::engine::content
