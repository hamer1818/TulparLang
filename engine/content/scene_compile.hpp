// L6 CONTENT — Sahne DERLEYICISI (PLAN Faz 6: "scene compiler: resident set,
// peak memory, PSO uretimi, GI probe / navmesh bake").
//
// scene_blob_compile saf bir donusumdur (SceneDesc -> baytlar, dosya okumaz).
// Derleyici onun ustunde durur ve DISK'e dokunur:
//   1. Yerlesik kume (resident set): her kaynagi bir kez yukler ve arenadan
//      aldigi baytlari OLCER; GPU tarafini (paketlenmis vertex, indeks, mip
//      zincirli doku, sikistirilmis klip) model verisinden hesaplar. Sonuc
//      blob basligina girer — runtime tahmin etmez, okur.
//   2. Tepe bellek (peak): yerlesik + en buyuk tek gecici blok (staging).
//   3. Navmesh bake: sahnenin SABIT kutu govdelerinden ucgen corbasi cikarir,
//      Recast ile bake eder, Detour verisini blob'a gomer. Runtime BAKE
//      YAPMAZ (SceneNav yalniz sorgular).
//   4. GI sonda bake'i (content/gi.hpp): sahne geometrisinde CPU isin izlemesi,
//      sonda basina 6 yonlu ambient cube + gunes gorunurlugu. Varsayilan
//      KAPALI (yavas; engine_sahnec --gi ile acilir).
// Olcum adimi ice aktarma onbelleginden (content/importer.hpp) gecer: ayni
// glTF + ayni ayar ikinci kez YUKLENMEZ, kayit diskten okunur.
#pragma once
#include <cstdint>

#include "content/cluster_dag.hpp"
#include "content/gi.hpp"
#include "content/importer.hpp"
#include "content/scene_blob.hpp"
#include "sim/navmesh.hpp"

namespace tulpar::engine::content {

// Ucgen corba siniri: her sabit kutu 8 vertex / 12 ucgen.
constexpr uint32_t kSceneNavMaxVerts = kSceneMaxEntities * 8;
constexpr uint32_t kSceneNavMaxTris = kSceneMaxEntities * 12;
// Kume DAG bake'i icin ust sinir: (kaynak, mesh) cifti sayisi.
constexpr uint32_t kSceneDagMaxMeshes = 64;

struct SceneCompileOptions {
  bool measure_resident = true; // kaynaklari yukleyip yerlesik kumeyi olc
  bool bake_nav = true;         // sabit kutulardan navmesh bake et
  ImportCache *cache = nullptr; // null = onbellek yok (her calistirmada yukle)
  sim::NavMeshBuildConfig nav{};
  // Kume (cluster) DAG bake'i (Faz 9). Varsayilan KAPALI: blob'u buyutur ve
  // her kaynagi bir kez daha yukler. UYARI: bu adim modelleri arenada TUTAR
  // (olcum adimi gibi geri sarmaz) — cagiran bol arena vermeli.
  bool build_cluster_dag = false;
  DeviceClass dag_class = DeviceClass::Mid; // bake cihaz sinifina gore degisir
  // GI sonda bake'i (Faz 6). Varsayilan KAPALI: sonda basina yuzlerce isin,
  // buyuk sahnede saniyeler surer. include_models acikken kaynaklar bir kez
  // daha yuklenir ve ucgenleri DUNYA uzayinda arenada TUTULUR.
  bool bake_gi = false;
  bool gi_include_models = true; // model ucgenleri de tikayici olsun mu
  SceneGiOptions gi{};
};

struct SceneCompileReport {
  uint32_t assets_measured = 0, assets_missing = 0;
  uint32_t cache_hits = 0, cache_misses = 0;
  uint32_t resident_cpu = 0, resident_gpu = 0, peak_transient = 0, peak_bytes = 0;
  uint32_t nav_tris = 0, nav_polys = 0, nav_verts = 0, nav_bytes = 0;
  uint64_t nav_hash = 0; // bake ciktisinin FNV-1a'si (sim::NavMesh ile karsilastirilabilir)
  uint32_t dag_meshes = 0, dag_nodes = 0, dag_indices = 0, dag_children = 0, dag_levels = 0;
  uint32_t dag_top_tris = 0;  // en ust seviyedeki toplam ucgen (yogunluk bilgisi)
  uint32_t dag_bytes = 0;     // blob'a eklenen bayt (dugum + indeks + cocuk + dilim)
  uint64_t dag_hash = 0;      // butun DAG'larin ozeti (belirlenimlilik)
  bool nav_ok = false;
  char nav_error[112] = {0};
  SceneGiReport gi{};  // GI bake olcumu (probes = 0: bake yapilmadi)
  uint32_t gi_bytes = 0; // blob'a eklenen sonda baytlari
  bool gi_ok = false;
};

// Sahnenin SABIT (dynamic=false) kutu govdelerinden ucgen corbasi: her kutu 12
// ucgen, dunya uzayinda (yazar donusumu uygulanmis). Kureler atlanir (Recast
// girdisi ucgendir; kure govdeler yurunebilir zemin tanimlamaz).
// Donus: ucgen sayisi. verts[3*nv], tris[3*nt].
uint32_t scene_nav_soup(const SceneDesc &d, float *verts, uint32_t max_verts, int *tris, uint32_t max_tris, uint32_t *out_verts);

// Recast bake: ucgen corbasi -> Detour navmesh verisi (arenaya kopyalanir).
// sim::NavMesh::build ile AYNI ardisiklik/ayarlar — kapi ikisinin bit-esit
// veri urettigini olcer. false: err dolu.
bool scene_navmesh_bake(Arena &arena, const float *verts, int nverts, const int *tris, int ntris, const sim::NavMeshBuildConfig &cfg,
                        void **out_data, uint32_t *out_size, uint32_t *out_polys, uint32_t *out_verts, char *err, size_t err_cap);

// Derleyicinin tam adimi: olcum + bake -> SceneBlobExtras (bellegi `arena`da).
// Kaynak yollari `dir`e gore. Kaynak acilamazsa kayit bayrakli kalir, derleme
// SURER (budget_flags bit0). Donus: extras kuruldu mu.
bool scene_compile(Arena &arena, const SceneDesc &d, const char *dir, const SceneCompileOptions &opt, SceneBlobExtras *out,
                   SceneCompileReport *rep);

} // namespace tulpar::engine::content
