// L3 RENDERER — SUREKLI LOD: gorunume bagli kume secimi (cut).
//
// **SIFIRDAN YAZILMADI.** Veri modeli ve kesme kurali
// Georgy-Khachatryan/MeshDecimationTools'tan (MIT, third_party/mdt/,
// birebir upstream) alinmistir. O kutuphane DAG'i CEVRIMDISI kurar
// (kume bolme -> gruplama -> QEM ile sadelestirme -> tekrar bolme);
// burasi onun urettigi veriyi okuyan calisma zamani yuzudur.
//
// **Neden MeshDecimationTools:** olculerek secildi --
//   * `throw`/`catch`: 0, `dynamic_cast`/`typeid`: 0
//   * STL konteyner: 0 (yalniz <assert.h> <float.h> <math.h> <stdint.h>
//     <stdio.h> <stdlib.h> <string.h>) -> motorun L1 kuralini CIGNEMEZ
//   * Ayirici TAKILABILIR (MdtSystemCallbacks::realloc) -> Arena'ya baglanir
//   * Materyal basina ayirma HAZIR (her meshlet tek geometriye ait)
//   * Seviye indeksi HAZIR -> grup bazli akis (streaming) icin gereken sey
//   * Nitelik sureksizliklerini ve coklu materyal dikislerini DOGRU isler
//
// **Klasik LOD'dan farki:** geleneksel LOD (content/lod_select.hpp) mesh'in
// TAMAMI icin tek seviye secer; yakin duran binanin arka duvari da yuksek
// detayda cizilir ve seviye atlayinca tum mesh birden "pop" eder. Surekli
// LOD her KUME icin ayri seviye secer -- ayni mesh'in bir yarisi ince, obur
// yarisi kaba olabilir.
//
// **Neden mobilde bu yol:** Nanite'in mobile TASINAMAYAN parcasi software
// rasterizer'dir (64-bit image atomic: Android ~%32, iOS %0 kapsama; ve tam
// ekran bir visibility buffer'a atomicMin yazmak TBDR'nin tile belleginde
// OLAMAZ -- her piksel bir DRAM islemi olur). Mesh shader da yol degil
// (Android ~%5). TASINABILIR parca budur: saf CPU matematigi, hicbir Vulkan
// uzantisi gerektirmez, tahsis yapmaz, belirlenimlidir.
#pragma once
#include <cstdint>

#include "core/math/vec.hpp"

namespace tulpar::engine::renderer {

// MdtErrorMetric'in birebir karsiligi: hatanin BAGLI OLDUGU kure + mesh
// uzayindaki sapma. Kure gerekli cunku hata ekran uzayina yansitilirken
// kumenin kameraya olan gercek uzakligi kullanilir.
struct ClusterErrorMetric {
  Vec3 center{0, 0, 0};
  float radius = 0.0f;
  float error = 0.0f;
};

// MdtMeshlet'in calisma zamaninda ihtiyac duyulan alanlari.
struct Cluster {
  // Kirpma icin (MdtMeshlet::geometric_sphere_bounds).
  Vec3 center{0, 0, 0};
  float radius = 0.0f;

  // BU seviyenin hatasi. Ilk (en ince) seviyede 0.
  ClusterErrorMetric current{};
  // BIR UST (daha kaba) seviyenin hatasi. Son seviyede FLT_MAX --
  // boylece en kaba temsil HER ZAMAN cizilebilir kalir ve nesne uzakta
  // BIRDEN KAYBOLMAZ.
  ClusterErrorMetric coarser{};

  uint32_t index_offset = 0;
  uint32_t index_count = 0;
  uint32_t geometry = 0; // materyal ayirma: her kume TEK geometriye aittir
  uint32_t level = 0;    // 0 = en yuksek kalite; akis (streaming) birimi
};

struct ClusterLodMesh {
  const Cluster *clusters = nullptr;
  uint32_t cluster_count = 0;
};

// Kamera, MESH (model) uzayinda. Kamerayi mesh uzayina almak, kume basina
// donusum yapmaktan cok daha ucuzdur.
struct LodView {
  Vec3 camera_position{0, 0, 0};
  float proj = 1.0f;   // projection[1][1] == cot(fovy/2)
  float znear = 0.05f; // POZITIF yakin duzlem
  float screen_height = 1080.0f;
  float error_threshold_px = 1.0f; // kac piksel sapmaya izin var
};

// MeshDecimationTools'un `EvaluateErrorMetric`i: mesh uzayindaki hatanin
// ekranda kac piksele karsilik geldigi. Kutuphane bu fonksiyonu KENDISI
// saglamaz (projeksiyon motorun isidir), sozlesmeyi tanimlar.
//
//   error / max(dist(center, kamera) - radius, znear) * (proj * 0.5) * yukseklik
//
// `- radius` SART: kurenin kameraya EN YAKIN noktasi esas alinir. Yoksa
// kamerayi iceren buyuk bir kume "uzak" sayilip kucuk hata uretir ve
// yanlislikla elenir.
float cluster_screen_error(const ClusterErrorMetric &m, const LodView &v);

// Cizilecek kume indekslerini sec.
//
// Kesme kurali (MeshDecimationTools.h'nin sozlesmesi, birebir):
//   ciz  <=>  eval(current) <= esik   VE   eval(coarser) > esik
// Birinci kosul "bu seviye yeterince iyi", ikincisi "bir ust seviye YETERSIZ"
// demektir. Ikisi birlikte DAG'daki her yol boyunca TAM BIR seviye secer;
// tek kosul yeterli DEGILDIR, kumeler arasinda catlak ya da ust uste binme
// olusurdu.
//
// Donus: SECILEN kume sayisi. `max_out` asilsa bile sayim SURER, boylece
// cagiran tasmayi gorur (motorun "sessiz kirpma yok" ilkesi).
uint32_t cluster_lod_select(const ClusterLodMesh &mesh, const LodView &view,
                            uint32_t *out_clusters, uint32_t max_out);

// Ayni secim + frustum kirpma. `frustum` MESH uzayinda olmalidir.
uint32_t cluster_lod_select_culled(const ClusterLodMesh &mesh, const LodView &view,
                                   const Frustum &frustum, uint32_t *out_clusters,
                                   uint32_t max_out);

} // namespace tulpar::engine::renderer
