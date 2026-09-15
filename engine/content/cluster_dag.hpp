// L6 CONTENT — Kume (cluster) DAG: mesh'i ~64-128 ucgenlik KUMELERE boler,
// komsu kumeleri gruplar ve her grubu GRUP SINIRI KILITLI sadelestirerek bir
// ust seviye kume uretir; seviye seviye yukari cikinca yonlu cevrimsiz bir
// cizge (DAG) cikar. PLAN Faz 9'un veri tarafi. Iki kural tasiyici:
//
//  1. KENAR KILIDI — bir grup sadelestirilirken grup SINIRINDAKI pozisyonlar
//     kilitlenir (meshopt_SimplifyVertex_Lock). Iki komsu grup ayni siniri
//     birbirinden bagimsiz sadelestirirse yuzeyler ayrilir: CATLAK. Kilitle
//     sinir kenarlari bit bit ayni kalir — meshopt_simplify vertex TASIMAZ,
//     yalniz cokertir; kilitli vertex cokmedigi icin sinir kenari iki tarafta
//     da ayni kalir, komsu gruplar farkli seviyelerde bile otursur.
//     Kapinin kontrolu: lock_group_border=false -> ayni olcum catlak GORMELI.
//  2. MONOTON HATA — bir grubun urettigi ust kumelerin hatasi, o gruptaki
//     butun cocuklarin hatasindan kucuk olamaz. "Cut" (hangi seviyeden
//     cizilecegi) kosulu boylece tek esitsizlige iner:  error <= t < parent_error.
//     Ayni grubun kumeleri AYNI parent_error'u tasir, yani hep birlikte gecer:
//     grup ici bolunmus gecis hem catlagin hem popping'in kaynagidir.
//
// LOD cut secimi RUNTIME'in isidir (PLAN ⚠️ REV: cull pass'inde, bake yok);
// burasi yalniz veriyi ve hatayi uretir. Hata MUTLAK, dunya birimindedir
// (meshopt_SimplifyErrorAbsolute) — ekran uzayi esigi runtime'da hesaplanir.
//
// Uygulama content/meshopt.cpp icinde (ayni meshoptimizer sarmalayicisi;
// engine/CMakeLists.txt'teki engine_content kaynak listesi elle tutuluyor).
#pragma once
#include <cstdint>

#include "content/model.hpp"
#include "content/scene_blob.hpp"

namespace tulpar::engine::content {

// Kume dugumu = blob kaydi (SceneBlobDagNode): bake sirasinda alan alan
// donusum yok, yalniz ofset kaydirma. Bkz. content/scene_blob.hpp.
using ClusterNode = SceneBlobDagNode;

constexpr uint32_t kClusterDagMaxLevels = 12;
constexpr float kClusterErrorInf = 3.4e38f;          // kok kume: ustu yok, her esikte cizilir
constexpr uint32_t kClusterNoGroup = 0xFFFFFFFFu;    // kesim grubu yok (kok kume)

// Cihaz sinifi (docs/engine/CIHAZ-MATRISI.md §2). Bake edilen DAG sinifa gore
// degisir: dusukte daha buyuk kume + daha sig derinlik (daha az cizim, daha az
// dugum), yuksekte daha kucuk kume + tam derinlik (daha iyi cull cozunurlugu).
enum class DeviceClass : uint32_t { Low = 0, Mid = 1, High = 2 };

struct ClusterDagOptions {
  uint32_t max_triangles = 124; // kume basina ucgen (plan: ~64-128)
  uint32_t max_vertices = 128;  // meshopt siniri 256
  uint32_t group_size = 4;      // birlikte sadelestirilecek komsu kume sayisi
  float cone_weight = 0.5f;     // kumeleme: normal konisi sikiligi (backface cull)
  float simplify_ratio = 0.5f;  // grup hedefi: ucgenlerin bu orani
  float max_error = 1.0e30f;    // mutlak hata tavani (dunya birimi)
  uint32_t max_levels = kClusterDagMaxLevels;
  bool lock_group_border = true; // KAPI KONTROLU: false -> catlak olusmali
};
ClusterDagOptions cluster_dag_preset(DeviceClass c);
const char *device_class_name(DeviceClass c);

struct ClusterDag {
  ClusterNode *nodes = nullptr;  uint32_t node_count = 0;
  uint32_t *indices = nullptr;   uint32_t index_count = 0; // mesh vertex indeksleri
  uint32_t *children = nullptr;  uint32_t child_count = 0; // dugum dizinleri (DAG icinde)
  uint32_t levels = 0;
  uint32_t vertex_count = 0;  // kaynak mesh (indeks siniri denetimi)
  uint32_t group_count = 0;   // toplam kesim grubu (ayni grup birlikte gecer)
  uint32_t root_count = 0;    // parent_error sonsuz olan kumeler
  uint32_t level_first[kClusterDagMaxLevels] = {};
  uint32_t level_count[kClusterDagMaxLevels] = {};
  uint32_t level_tris[kClusterDagMaxLevels] = {};
  float level_error[kClusterDagMaxLevels] = {}; // seviyedeki en buyuk hata
  uint64_t hash = 0; // dugum + indeks + cocuk baytlarinin FNV-1a'si (determinizm)
};

// Mesh -> DAG. Sonuc dizileri `arena`dan (yukleme aninda); gecici bellek
// malloc/free (meshoptimizer'in kendisi gibi) ve cikista birakilir.
// false: mesh cok kucuk, kapasite asildi ya da arena dolu.
bool cluster_dag_build(Arena &arena, const ModelMesh &mm, const ClusterDagOptions &opt, ClusterDag *out);

// CATLAK OLCUMU (planin kapisi). DAG'in KENDISINDEN, seviye seviye:
// bir seviyedeki kumelerin grup dagilimi dugumlerde durur; iki FARKLI gruba
// ait ucgenlerin paylastigi kenar bir "grup sinir kenari"dir. Grubun ust
// kumelerinde (cocuk baglantilariyla bulunur) ayni kenar hala varsa sinir
// yerinde demektir. Pozisyon karsilastirmasi BIT BIT: ayni pozisyona sahip
// farkli vertex indeksleri tek temsilciye indirgenir (meshopt remap), yani
// dikis vertex'leri catlak sayilmaz.
struct ClusterDagSeamReport {
  uint32_t levels_checked = 0;
  uint32_t border_edges = 0;      // incelenen grup sinir kenari (grup basina sayilir)
  uint32_t border_edges_lost = 0; // sadelestirmeden sonra KAYBOLAN = catlak
  uint32_t border_verts = 0, border_verts_lost = 0;
  uint32_t groups_simplified = 0;
};
bool cluster_dag_check_seams(const ClusterDag &dag, const ModelMesh &mm, ClusterDagSeamReport *out);

// --- Bake: DAG(lar) -> blob bolumleri (SceneBlobExtras) --------------------
// Her kayit bir (kaynak, mesh) ciftini gosterir. Diziler `arena`dan; ofsetler
// blob tablolarina gore kaydirilir (cocuk degerleri global dugum dizinidir).
struct ClusterDagBakeItem {
  const ClusterDag *dag = nullptr;
  uint32_t asset = 0, mesh = 0;
};
bool cluster_dag_bake(Arena &arena, const ClusterDagBakeItem *items, uint32_t n, SceneBlobExtras *x);

} // namespace tulpar::engine::content
