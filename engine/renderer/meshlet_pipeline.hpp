// L3 RENDERER — Meshlet (kume) insasi: GPU-driven cizimin CPU tarafi.
//
// Mesh, ~64 vertex / ~124 ucgenlik KUCUK kumelere bolunur; her kume kendi
// sinirlayici kutusuyla AYRI AYRI kirpilabilir (frustum/okluzyon) ve mesh
// shader'a ya da indirect draw'a TEK BIRIM olarak verilir. Nanite/meshlet
// tabanli tum boru hatlarinin ilk adimi budur.
//
// **Kendi algoritmamiz DEGIL:** kumeleme meshoptimizer'in (MIT, zeux/
// meshoptimizer -- motorda ZATEN vendored ve LOD/vertex-cache icin
// kullaniliyor) `meshopt_buildMeshlets` fonksiyonuyla yapilir. Burasi
// yalnizca Tulpar'in "cagiran ayirir, fonksiyon ayirmaz" (A2) sozlesmesine
// uyan ince bir kabuk + kume sinirlayici kutusu hesabidir.
//
// Vulkan boru hatti (VK_EXT_mesh_shader ya da compute+indirect) BU DOSYADA
// DEGIL: mobilde mesh shader destegi seyrek, ve derleyicisiz/cihazsiz
// ortamda dogrulanamaz. renderer/indirect_cull.hpp (Sascha Willems portu)
// bu kumelerin GORUNURLUK testini ZATEN yapabiliyor -- kablolama, gercek
// cihazda dogrulanacak sonraki adim.
#pragma once
#include <cstdint>

#include "core/math/vec.hpp"

namespace tulpar::engine::renderer {

// meshoptimizer'in onerdigi varsayilanlar (donanim dalga boyutuyla uyumlu).
constexpr uint32_t kMeshletMaxVertices = 64;
constexpr uint32_t kMeshletMaxTriangles = 124;

// Normal konisi: kumedeki TUM ucgen normallerini kapsayan en dar koni.
// Kamera bu koninin "arka" tarafindaysa kumenin HER ucgeni arka yuzdur ve
// kume HIC cizilmeden atlanabilir -- vertex donusumu bile yapilmaz.
// meshoptimizer'in gercek davranisi (meshletutils.cpp:220 dogrulandi):
//   * koni yarim kureden genisse (normaller cok dagilmis) DEJENERE sayilir:
//     cutoff = 1 ve eksen SIFIR birakilir -> test hicbir zaman elemez.
//   * tamamen DUZ bir kume icin cutoff = sqrt(1 - 1) = 0 -> tam arkadan
//     bakildiginda elenir.
// Bu yuzden varsayilan eksen {0,0,0}: insa edilmemis bir Meshlet ASLA
// yanlislikla elenmez. (Eksen {0,0,1} olsaydi -Z'den bakan kamera onu
// arka yuz sanardi.)
struct MeshletCone {
  Vec3 apex{0, 0, 0};
  Vec3 axis{0, 0, 0};
  float cutoff = 1.0f;
};

struct Meshlet {
  uint32_t vertex_offset = 0;   // meshlet_vertices[] icindeki baslangic
  uint32_t triangle_offset = 0; // meshlet_triangles[] icindeki baslangic (bayt)
  uint32_t vertex_count = 0;
  uint32_t triangle_count = 0;
  Aabb bounds{};   // model uzayi kutusu -- frustum kirpmasi icin
  Sphere sphere{}; // meshoptimizer'in kure siniri -- koni testi BUNU ister
  MeshletCone cone{};
};

// Arka-yuz kume elemesi. meshoptimizer'in kendi belgesindeki, TEPE NOKTASI
// GEREKTIRMEYEN formul (Real-Time Rendering 4th ed., 19.3):
//   dot(center - kamera, axis) >= cutoff * |center - kamera| + radius
// Tepe noktali surum biraz daha kesin, ama zaten kure siniriyla frustum
// kirpmasi yaptigimiz icin bu surum tercih edilir (tek veri kumesi).
// true => kume TAMAMEN arka yuz, cizilmesine gerek YOK.
inline bool meshlet_backfacing(const Meshlet &m, Vec3 camera_position) {
  const Vec3 d = m.sphere.center - camera_position;
  const float len = length(d);
  return dot(d, m.cone.axis) >= m.cone.cutoff * len + m.sphere.radius;
}

// Cagiranin ayirmasi gereken EN KOTU DURUM boyutlari.
struct MeshletCapacity {
  uint32_t meshlets = 0;         // Meshlet dizisi
  uint32_t vertex_indices = 0;   // uint32_t dizisi
  uint32_t triangle_bytes = 0;   // uint8_t dizisi
};
MeshletCapacity meshlet_capacity(uint32_t index_count, uint32_t max_vertices = kMeshletMaxVertices,
                                 uint32_t max_triangles = kMeshletMaxTriangles);

struct MeshletBuildResult {
  uint32_t meshlet_count = 0;
  bool ok = false;
};

// positions: vertex_count adet konum, position_stride BAYT aralikli (ör.
// renderer::Vertex icin sizeof(Vertex), pos alani BASTA oldugu icin dogrudan
// &verts[0].pos verilebilir).
// cone_weight: 0 = yalnizca yerel yogunluk, 1 = normal konisine agirlik ver
// (arka-yuz kume elemesi icin); meshoptimizer'in onerisi 0.0-0.5 arasi.
// VARSAYILAN 0.25: 0 verilirse kumeler normal yonune hic bakilmadan
// olusturulur, konileri genis cikar ve arka-yuz elemesi neredeyse HIC
// calismaz -- yani hesaplanan koni verisi bosa gider.
// out_* diziler meshlet_capacity() ile ONCEDEN ayrilmis olmalidir.
MeshletBuildResult build_meshlets(const uint32_t *indices, uint32_t index_count, const float *positions,
                                  uint32_t vertex_count, uint32_t position_stride, Meshlet *out_meshlets,
                                  uint32_t *out_vertex_indices, uint8_t *out_triangles,
                                  uint32_t max_vertices = kMeshletMaxVertices,
                                  uint32_t max_triangles = kMeshletMaxTriangles, float cone_weight = 0.25f);

} // namespace tulpar::engine::renderer
