#include "content/clod_format.hpp"

#include <cstring>

namespace tulpar::engine::content {

namespace {

// [offset, offset + count*stride) araligi [0, size) icinde mi?
// Carpma ve toplama 64 BIT yapilir: 32 bitte tasma olsaydi kontrol
// SESSIZCE gecer ve sinir disi okuma yapardik -- asil tehlike budur.
bool range_ok(uint64_t offset, uint64_t count, uint64_t stride, uint64_t size) {
  if ((offset & 3u) != 0) return false; // 4 bayt hizalama sozlesmesi
  const uint64_t bytes = count * stride;
  return offset <= size && bytes <= size - offset;
}

Vec3 to_vec3(const float c[3]) { return Vec3{c[0], c[1], c[2]}; }

renderer::ClusterErrorMetric to_metric(const ClodMetricRaw &m) {
  renderer::ClusterErrorMetric r;
  r.center = to_vec3(m.center);
  r.radius = m.radius;
  r.error = m.error;
  return r;
}

} // namespace

bool clod_load(Arena &arena, const void *data, size_t size, ClodMesh &out) {
  out = ClodMesh{};
  if (!data || size < sizeof(ClodHeader)) return false;

  // Basligi KOPYALAYARAK oku: gelen tampon hizali olmayabilir ve dogrudan
  // tip donusumu takma-ad ihlali olurdu.
  ClodHeader h;
  std::memcpy(&h, data, sizeof(h));
  if (h.magic != kClodMagic || h.version != kClodVersion) return false;
  if ((uint64_t)size < (uint64_t)h.total_bytes) return false;

  // Vertex duzeni renderer::Vertex ile AYNI olmak ZORUNDA: dosya dogrudan
  // vertex buffer olarak yuklenecek. Yapi degisirse eski dosyalar SESSIZCE
  // yanlis okunmak yerine burada reddedilir.
  constexpr uint32_t kStrideFloats = sizeof(renderer::Vertex) / sizeof(float);
  if (h.vertex_stride_floats != kStrideFloats) return false;

  const uint64_t cap = h.total_bytes; // dosyanin KENDI bildirdigi sinir
  if (!range_ok(h.clusters_offset, h.cluster_count, sizeof(ClodClusterRaw), cap)) return false;
  if (!range_ok(h.indices_offset, h.index_count, sizeof(uint32_t), cap)) return false;
  if (!range_ok(h.vertices_offset, (uint64_t)h.vertex_count * kStrideFloats, sizeof(float), cap))
    return false;

  // Bos DAG "yuklendi" sayilmamali: cagiran bos ekrana bakip nedenini arardi.
  if (h.cluster_count == 0 || h.index_count == 0 || h.vertex_count == 0) return false;

  const uint8_t *base = static_cast<const uint8_t *>(data);
  renderer::Cluster *clusters = arena.alloc_array<renderer::Cluster>(h.cluster_count);
  if (!clusters) return false;

  for (uint32_t i = 0; i < h.cluster_count; i++) {
    ClodClusterRaw c;
    std::memcpy(&c, base + h.clusters_offset + (size_t)i * sizeof(ClodClusterRaw), sizeof(c));

    // Indeks araligi TOPLAM indeks sayisinin icinde mi? Degilse cizim
    // aninda sinir disi okuma olurdu -- burada reddediyoruz.
    if ((uint64_t)c.index_offset + (uint64_t)c.index_count > (uint64_t)h.index_count) return false;

    clusters[i].center = to_vec3(c.center);
    clusters[i].radius = c.radius;
    clusters[i].current = to_metric(c.current);
    clusters[i].coarser = to_metric(c.coarser);
    clusters[i].index_offset = c.index_offset;
    clusters[i].index_count = c.index_count;
    clusters[i].geometry = c.geometry;
    clusters[i].level = c.level;
  }

  // HER INDEKS vertex tamponunun ICINDE mi? Bu kontrol olmadan bozuk bir
  // dosya, cizim aninda vertex tamponunun disini okuturdu.
  const uint32_t *idx = reinterpret_cast<const uint32_t *>(base + h.indices_offset);
  for (uint32_t i = 0; i < h.index_count; i++)
    if (idx[i] >= h.vertex_count) return false;

  out.clusters = clusters;
  out.cluster_count = h.cluster_count;
  // Indeksler KOPYALANMAZ: en buyuk dizi odur ve salt okunur kullanilir.
  // Hizalama sozlesmesi range_ok'ta dogrulandi.
  out.indices = idx;
  out.index_count = h.index_count;
  out.vertices = reinterpret_cast<const float *>(base + h.vertices_offset);
  out.vertex_count = h.vertex_count;
  return true;
}

renderer::ClusterLodMesh clod_view(const ClodMesh &m) {
  renderer::ClusterLodMesh v;
  v.clusters = m.clusters;
  v.cluster_count = m.cluster_count;
  return v;
}

} // namespace tulpar::engine::content
