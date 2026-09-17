#include "renderer/meshlet_pipeline.hpp"

#include <cstring>

#include "meshoptimizer.h"

namespace tulpar::engine::renderer {

MeshletCapacity meshlet_capacity(uint32_t index_count, uint32_t max_vertices, uint32_t max_triangles) {
  MeshletCapacity c;
  if (index_count == 0 || max_vertices == 0 || max_triangles == 0) return c;
  c.meshlets = (uint32_t)meshopt_buildMeshletsBound(index_count, max_vertices, max_triangles);
  c.vertex_indices = c.meshlets * max_vertices;
  // meshoptimizer sozlesmesi: ucgen dizisi 3 bayt/ucgen, 4'e HIZALANMIS
  // (kume basina yuvarlanir) -- kutuphanenin kendi belgesindeki formul.
  c.triangle_bytes = c.meshlets * ((max_triangles * 3 + 3) & ~3u);
  return c;
}

MeshletBuildResult build_meshlets(const uint32_t *indices, uint32_t index_count, const float *positions,
                                  uint32_t vertex_count, uint32_t position_stride, Meshlet *out_meshlets,
                                  uint32_t *out_vertex_indices, uint8_t *out_triangles, uint32_t max_vertices,
                                  uint32_t max_triangles, float cone_weight) {
  MeshletBuildResult res;
  if (!indices || !positions || !out_meshlets || !out_vertex_indices || !out_triangles) return res;
  if (index_count == 0 || (index_count % 3) != 0 || vertex_count == 0) return res;
  if (max_vertices == 0 || max_triangles == 0 || position_stride == 0) return res;

  // meshoptimizer kendi Meshlet yapisini yazar; biz onu kendi (sinirlayici
  // kutu eklenmis) yapimiza cevirecegiz. Gecici olarak cikis dizimizin
  // ustune yazmak yerine, kutuphaneye AYRI bir dizi vermemiz gerekir --
  // ama ayirma YAPAMAYIZ (A2). Cozum: kutuphanenin yapisi bizimkinden
  // KUCUK oldugu icin (4 uint32 vs 4 uint32 + Aabb), AYNI tamponun BASINA
  // yazdirip GERIYE DOGRU (son elemandan basa) kendi yapimiza cevirmek
  // guvenlidir -- kaynak ve hedef ayni indekste ust uste binmez.
  static_assert(sizeof(meshopt_Meshlet) <= sizeof(Meshlet),
                "Meshlet, meshopt_Meshlet'ten kucuk olamaz -- yerinde donusum varsayimi bozulur");

  meshopt_Meshlet *raw = reinterpret_cast<meshopt_Meshlet *>(out_meshlets);
  const size_t count = meshopt_buildMeshlets(raw, out_vertex_indices, out_triangles, indices, index_count,
                                             positions, vertex_count, position_stride, max_vertices,
                                             max_triangles, cone_weight);
  if (count == 0) return res;

  // Yerinde donusum: SONDAN basa (i azalarak) -- boylece raw[i] okunurken
  // out_meshlets[i] yazilir ve i'den KUCUK indeksler hic bozulmaz.
  for (size_t i = count; i-- > 0;) {
    // memcpy ile oku: ayni depolamayi iki farkli tiple kullandigimiz icin
    // (kutuphane meshopt_Meshlet yazdi, biz Meshlet yazacagiz) dogrudan
    // dereference yerine bayt kopyasi -- katı takma-ad (strict aliasing)
    // kurallarina karsi kesin guvenli.
    meshopt_Meshlet m;
    std::memcpy(&m, reinterpret_cast<const uint8_t *>(raw) + i * sizeof(meshopt_Meshlet), sizeof(m));
    Meshlet out;
    out.vertex_offset = m.vertex_offset;
    out.triangle_offset = m.triangle_offset;
    out.vertex_count = m.vertex_count;
    out.triangle_count = m.triangle_count;

    Aabb box{};
    for (uint32_t v = 0; v < m.vertex_count; v++) {
      const uint32_t vi = out_vertex_indices[m.vertex_offset + v];
      if (vi >= vertex_count) continue; // bozuk indeks: kutuya katma (savunmaci)
      const float *p = reinterpret_cast<const float *>(reinterpret_cast<const uint8_t *>(positions) +
                                                       (size_t)vi * position_stride);
      box = merge(box, Vec3{p[0], p[1], p[2]});
    }
    out.bounds = box;

    // Kume siniri + NORMAL KONISI. Bu veriyi meshoptimizer bedava uretiyor
    // (kumeyi olustururken normalleri zaten tariyor); cagirmamak, hesaplanmis
    // bir kazanci CÖPE ATMAK olurdu. Koni sayesinde tamamen arka yuze bakan
    // kumeler vertex donusumune bile girmeden atlanir.
    // NOT: out_vertex_indices / out_triangles bu dongude DEGISTIRILMIYOR,
    // bu yuzden meshlet i'nin verisini okumak guvenlidir.
    const meshopt_Bounds mb = meshopt_computeMeshletBounds(
        out_vertex_indices + m.vertex_offset, out_triangles + m.triangle_offset, m.triangle_count,
        positions, vertex_count, position_stride);
    out.sphere.center = Vec3{mb.center[0], mb.center[1], mb.center[2]};
    out.sphere.radius = mb.radius;
    out.cone.apex = Vec3{mb.cone_apex[0], mb.cone_apex[1], mb.cone_apex[2]};
    out.cone.axis = Vec3{mb.cone_axis[0], mb.cone_axis[1], mb.cone_axis[2]};
    out.cone.cutoff = mb.cone_cutoff;

    out_meshlets[i] = out;
  }

  res.meshlet_count = (uint32_t)count;
  res.ok = true;
  return res;
}

} // namespace tulpar::engine::renderer
