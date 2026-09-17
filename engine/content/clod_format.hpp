// L6 CONTENT — .clod dosya formati: CEVRIMDISI firinlanmis surekli LOD verisi.
//
// Zincir:  engine_clodbake (tools/)  ->  .clod  ->  clod_load()  ->
//          renderer::cluster_lod_select()  ->  cizim
//
// Veri modeli Georgy-Khachatryan/MeshDecimationTools'un (MIT,
// third_party/mdt/) `MdtMeshlet`inden alinmistir: her kume KENDI iki hata
// metrigini tasir (bu seviye + bir ust seviye), bu yuzden calisma zamaninda
// grup tablosuna BAKMAYA GEREK YOKTUR -- tek dizi uzerinde duz tarama.
//
// **Dosya duzeni** (little-endian, 4 bayta hizali):
//   [ClodHeader][ClodClusterRaw x N][uint32 indeks x K][float vertex x V*8]
//
// **Vertex tamponu neden dosyada:** sadelestirme YENI vertex konumlari
// uretir (kenar cokertme sonrasi en iyi konum aranir), yani .clod'un
// indeksleri KAYNAK mesh'in vertex tamponuna uymaz. Dosya kendi kendine
// yeterli olmali. Duzen `renderer::Vertex` ile AYNI (pos, nrm, uv = 8
// float), bu yuzden dogrudan vertex buffer olarak yuklenebilir.
// Ofsetler baslikta ACIKCA yazilir; okuyucu yapiyi "hesaplamaz", DOGRULAR.
#pragma once
#include <cstddef>
#include <cstdint>

#include "core/memory/arena.hpp"
#include "renderer/cluster_lod.hpp"
#include "renderer/renderer.hpp" // Vertex duzeni: stride dogrulamasi icin

namespace tulpar::engine::content {

constexpr uint32_t kClodMagic = 0x444F4C43u; // 'CLOD'
constexpr uint32_t kClodVersion = 2;         // v1: grup tabloluydu (MDT oncesi)

// Diskteki POD kopyalar. Calisma zamani tipleriyle ayni alan sirasina sahip
// olsalar da BILINCLI olarak ayri tutuluyorlar: dosya duzeni bir SOZLESMEDIR,
// C++ struct yerlesimi ise derleyiciye baglidir.
struct ClodMetricRaw {
  float center[3];
  float radius;
  float error; // son seviyede FLT_MAX
};
static_assert(sizeof(ClodMetricRaw) == 20, ".clod sozlesmesi: metrik 20 bayt");

struct ClodClusterRaw {
  float center[3]; // kirpma kuresi
  float radius;
  ClodMetricRaw current; // bu seviyenin hatasi
  ClodMetricRaw coarser; // bir ust (kaba) seviyenin hatasi
  uint32_t index_offset;
  uint32_t index_count;
  uint32_t geometry; // materyal ayirma
  uint32_t level;    // 0 = en yuksek kalite
};
static_assert(sizeof(ClodClusterRaw) == 72, ".clod sozlesmesi: kume 72 bayt");

struct ClodHeader {
  uint32_t magic;
  uint32_t version;
  uint32_t cluster_count;
  uint32_t index_count;
  uint32_t vertex_count;
  uint32_t vertex_stride_floats; // renderer::Vertex = 8; ACIKCA yazilir ki
                                 // yapi degisirse eski dosya SESSIZCE degil
                                 // GORUNUR bicimde reddedilsin
  uint32_t clusters_offset;      // dosya BASINDAN itibaren bayt
  uint32_t indices_offset;
  uint32_t vertices_offset;
  uint32_t total_bytes; // dosyanin beklenen TAM boyutu
};
static_assert(sizeof(ClodHeader) == 40, ".clod sozlesmesi: baslik 40 bayt");

struct ClodMesh {
  renderer::Cluster *clusters = nullptr;
  uint32_t cluster_count = 0;
  const uint32_t *indices = nullptr;  // dogrudan tampona bakar (kopyalanmaz)
  uint32_t index_count = 0;
  const float *vertices = nullptr;    // renderer::Vertex duzeninde
  uint32_t vertex_count = 0;
};

// `data` cagirana aittir ve ClodMesh yasadigi surece GECERLI kalmalidir
// (indices ona bakar). Kume dizisi Arena'ya ACILIR: ClodClusterRaw ile
// renderer::Cluster arasinda tip donusumu yapmak KATI TAKMA-AD ihlali olur
// ve dolgu (padding) varsayimina dayanirdi. Donusum YUKLEME ANINDA bir kez;
// kare icinde maliyeti sifir. Buyuk olan indeks dizisi kopyalanmaz.
//
// DOGRULAMA -- bozuk/kotu niyetli dosya cizim aninda sinir disi okuma
// demektir. Hepsi kontrol edilir, biri bile tutmazsa false:
//   * sihirli sayi ve surum
//   * size >= total_bytes
//   * her dizinin araligi dosyanin ICINDE (64 bit aritmetikle: 32 bitte
//     tasma kontrolu SESSIZCE gecerdi)
//   * ofsetlerin 4 bayta hizali olmasi
//   * her kumenin index_offset + index_count <= toplam index_count
//   * HER INDEKSIN vertex_count'tan kucuk olmasi (cizim aninda vertex
//     tamponunun disina cikilmasini onler)
//   * vertex_stride_floats'in beklenen degerde olmasi
bool clod_load(Arena &arena, const void *data, size_t size, ClodMesh &out);

renderer::ClusterLodMesh clod_view(const ClodMesh &m);

} // namespace tulpar::engine::content
