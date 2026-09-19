#include "content/primitives.hpp"

namespace tulpar::engine::content {

uint32_t build_primitive_meshes(renderer::Renderer &r, renderer::MeshHandle *out) {
  if (!out) return 0;

  // Uretecler INDEKS sayisini dondurur, vertex sayisini DONDURMEZ (cube()/
  // plane()'in eski sozlesmesi). Vertex sayisini elle yazmak (33*17 gibi)
  // segment varsayilanina GIZLI bir bagimlilik kurar: seg_h 32'den baska bir
  // sey olursa create_mesh YANLIS boyutta tampon ayirir ve GPU cop okur.
  // Bu yuzden vertex sayisi indekslerden TURETILIYOR (en buyuk indeks + 1) --
  // hangi ureteci cagirdigimizdan bagimsiz dogru sonuc verir.
  //
  // Tavanlar varsayilan segment sayilarina gore degil PAYLI secildi; tasma
  // sessiz bellek bozulmasi olurdu, asagida ACIKCA kontrol ediliyor.
  constexpr uint32_t kMaxVerts = 2048, kMaxIdx = 12288;
  static renderer::Vertex v[kMaxVerts]; // ~64 KB + 48 KB: yigina KOYULMAZ
  static uint32_t idx[kMaxIdx];

  uint32_t built = 0;
  // Uretec tavani asmissa mesh KURULMAZ: bozuk mesh cizmektense eksik ciz.
  auto make = [&](uint32_t slot, uint32_t index_count) {
    if (slot >= kPrimitiveSlotCount) return;
    if (index_count == 0 || index_count > kMaxIdx) return;
    uint32_t max_idx = 0;
    for (uint32_t j = 0; j < index_count; j++)
      if (idx[j] > max_idx) max_idx = idx[j];
    const uint32_t vc = max_idx + 1;
    if (vc > kMaxVerts) return;
    out[slot] = r.create_mesh(v, vc, idx, index_count);
    if (out[slot].valid()) built++;
  };

  make(kPrimitivePlane,  renderer::Renderer::plane(v, idx));    // Duzlem
  make(kPrimitiveCube, renderer::Renderer::cube(v, idx));     // Kup
  make(kPrimitiveSphere, renderer::Renderer::sphere(v, idx));   // Kure
  make(kPrimitiveCapsule, renderer::Renderer::capsule(v, idx));  // Kapsul
  make(kPrimitiveCylinder, renderer::Renderer::cylinder(v, idx)); // Silindir
  make(kPrimitiveCone, renderer::Renderer::cone(v, idx));     // Koni
  make(kPrimitiveQuad, renderer::Renderer::quad(v, idx));     // Dortgen
  make(kPrimitiveTorus, renderer::Renderer::torus(v, idx));    // Simit
  return built;
}

} // namespace tulpar::engine::content
