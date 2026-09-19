// L6 CONTENT — Derlenmis sahnenin (SceneBlobView) runtime tuketicisi:
// kaynaklari yukler (glTF -> GPU), govdeleri fizige koyar, her kare cizim +
// isik tablolarini oldugu gibi renderer'a verir. Metin ayristirma yok, varlik
// taramasi yok (tablolar derlemede hazir), kare icinde ayirma yok.
// Editor bunu KULLANMAZ: editor canli veri modelinden (SceneDesc) cizer; bu
// sinif "sahne = blob + kod" tarafidir (engine_demo --scene).
#pragma once
#include <cstdint>

#include "content/gi.hpp"
#include "content/model.hpp"
#include "content/particles.hpp"
#include "content/primitives.hpp"
#include "content/scene_blob.hpp"
#include "content/terrain.hpp"
#include "content/voxel.hpp"
#include "content/water_wave.hpp"
#include "core/memory/arena.hpp"
#include "renderer/renderer.hpp"
#include "sim/physics.hpp"

namespace tulpar::engine::content {

// Blob'daki BAKE EDILMIS navmesh'in runtime yuzu: bake YOK, yalniz sorgu.
// Detour tile verisine baglanti kurarken YAZAR, bu yuzden blob (ya da mmap'li
// pack) baytlari once arenaya kopyalanir — tek seferlik, yukleme aninda.
// Sorgu yolunda ayirma yok (dugum havuzu init'te alinir; kapi 0 olcer).
class SceneNav {
public:
  bool init(Arena &arena, const SceneBlobView &view, int max_nodes = 2048);
  void shutdown();
  bool ok() const { return mesh_ != nullptr; }
  // Duz yol (kose noktalari); 0 = yol yok. sim::NavMesh::find_path ile ayni
  // ayarlar (arama kutusu, filtre, kismi yol islemi) — kapi ikisini karsilastirir.
  int find_path(Vec3 from, Vec3 to, Vec3 *out, int max_points, bool *partial = nullptr) const;
  bool nearest_point(Vec3 p, Vec3 *out) const;
  // Navmesh uzerinde dogru gorus (sim::NavMesh::raycast ile ayni sozlesme):
  // donus true = ENGEL VAR, t_hit carpma parametresi (0..1). Engel yoksa
  // false ve t_hit = 1. Yol bulmadan "su noktaya dumduz gidebilir miyim"
  // sorusu bu; yol arama maliyeti odenmez.
  bool raycast(Vec3 from, Vec3 to, float *t_hit) const;
  uint32_t polys() const { return polys_; }
  uint64_t data_hash() const { return data_hash_; }

private:
  struct Impl;
  Impl *impl_ = nullptr;
  void *mesh_ = nullptr;  // dtNavMesh*
  void *query_ = nullptr; // dtNavMeshQuery*
  uint32_t polys_ = 0;
  uint64_t data_hash_ = 0;
};

struct SceneRuntimeStats {
  uint32_t assets_loaded = 0, assets_failed = 0;
  uint32_t draws = 0, lights = 0, bodies = 0; // son kare / kurulum
  uint32_t lod[kModelMaxLods + 1] = {};       // son kare: LOD0/1/2 secim sayilari
};

class SceneRuntime {
public:
  // Kaynak yollari `dir`e gore (sahne dosyasinin dizini). Yuklenemeyen kaynak
  // atlanir (rapor), sahne yine calisir. view'in bellegi yasamaya devam etmeli.
  bool init(Arena &arena, renderer::Renderer &r, const SceneBlobView &view, const char *dir);
  // Dunya isigi + golge hacmi renderer'a. GI bake edilmisse (SceneGi::ok())
  // duz SceneWorld.ambient yerine sahne sinirlarinin ORTASINDA orneklenen
  // probe degeri kullanilir (kaba -- kare basina TEK ornek, per-pixel DEGIL;
  // bkz. gi.hpp'nin runtime sorgu sozlesmesi). Bake yoksa davranis eskisiyle
  // BIT-TAM ayni (pozitif kontrol: gi_.ok() false doner).
  void apply_world(renderer::Renderer &r) const;
  // Govdeler fizige (bir kez). Donus: eklenen govde.
  uint32_t spawn(sim::Physics &ph);
  void despawn(sim::Physics &ph);
  // Kare guncellemesi (zaman bagimli sistemler: parcacik vb.)
  void update(float dt, const sim::Physics *ph = nullptr);
  // Kare: modeller (LOD / animasyon), isiklar. ph null = yazar donusumu;
  // degilse dinamik govdeli varliklar sim'den.
  void draw(renderer::Renderer &r, Vec3 cam_pos, float time_s, const sim::Physics *ph);
  const SceneBlobView &view() const { return view_; }
  SceneRuntimeStats stats() const { return stats_; }
  const Model *model(uint32_t asset) const { return asset < view_.h->asset_count && have_[asset] ? &models_[asset] : nullptr; }
  const UploadedModel *uploaded(uint32_t asset) const { return asset < view_.h->asset_count && have_[asset] ? &ups_[asset] : nullptr; }
  // Varligin bu karedeki dunya matrisi (sim'de ise oradan).
  Mat4 entity_matrix(uint32_t i, const sim::Physics *ph) const;
  // Varliga bagli fizik govdesi (yoksa ya da govdeler fizige girmediyse
  // gecersiz). Kopru bunu KUVVET uygulamak icin kullanir: sahne varliklari
  // yalniz okunabilir degil, itilebilir de olsun.
  sim::BodyId entity_body(uint32_t i) const;
  // Varligin govdesi dinamik mi (sabit govdeye kuvvet uygulanamaz).
  bool entity_dynamic(uint32_t i) const;
  // Govdeler su an fizikte mi (spawn edildi, despawn edilmedi).
  bool bodies_live() const { return bodies_live_; }

private:
  SceneBlobView view_;
  Model models_[kSceneMaxAssets];
  UploadedModel ups_[kSceneMaxAssets];
  bool have_[kSceneMaxAssets] = {};
  sim::BodyId *body_ids_ = nullptr; // [body_count]
  bool bodies_live_ = false;
  PoseScratch *pose_scratch_ = nullptr;
  SceneRuntimeStats stats_;
  SceneGi gi_; // ok()==false (bake yok) ise apply_world eski davranista kalir
  ParticleSystem particles_;
  renderer::MeshHandle prims_[kPrimitiveSlotCount] = {};
  renderer::MeshHandle terrain_meshes_[kSceneMaxEntities] = {};
  renderer::MeshHandle voxel_meshes_[kSceneMaxEntities] = {};
  renderer::MeshHandle water_meshes_[kSceneMaxEntities] = {};
  renderer::MaterialHandle entity_mats_[kSceneMaxEntities] = {};
};

} // namespace tulpar::engine::content
