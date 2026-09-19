#include "content/prefab.hpp"

#include <cstring>

namespace tulpar::engine::content {

namespace {
// i, root'un kendisi ya da torunu mu. Ebeveyn zinciri kMax adimdan uzun
// olamaz; sinir, bozuk (dongulu) bir zincirde sonsuz donguyu engeller.
bool in_subtree(const SceneDesc &d, int32_t i, int32_t root) {
  for (uint32_t guard = 0; i >= 0 && (uint32_t)i < d.entity_count && guard < kSceneMaxEntities; guard++) {
    if (i == root) return true;
    i = d.entities[i].parent;
  }
  return false;
}
} // namespace

uint32_t prefab_extract(const SceneDesc &src, int32_t root, SceneDesc *out) {
  if (!out || root < 0 || (uint32_t)root >= src.entity_count) return 0;
  // Buyuk bir SceneDesc gecicisi YIGINA kurulmaz (~yuzlerce KB): alanlar
  // yerinde sifirlanir, varliklar asagida uzerine yazilir.
  out->set_world(SceneWorld{});
  out->asset_count = 0;
  out->entity_count = 0;

  int32_t order[kSceneMaxEntities];
  const uint32_t n = scene_tree_order(src, order, kSceneMaxEntities);
  int32_t remap[kSceneMaxEntities];
  for (uint32_t i = 0; i < kSceneMaxEntities; i++) remap[i] = -1;
  int32_t amap[kSceneMaxAssets];
  for (uint32_t a = 0; a < kSceneMaxAssets; a++) amap[a] = -1;

  // On-sira: ebeveyn her zaman cocugundan once gelir, yani cocugun ebeveyni
  // islendiginde remap'i zaten doludur.
  for (uint32_t k = 0; k < n; k++) {
    const int32_t i = order[k];
    if (i < 0 || (uint32_t)i >= src.entity_count || !in_subtree(src, i, root)) continue;
    SceneEntity e = src.entities[i];
    if (i == root) {
      e.parent = -1;
      e.pos = Vec3{0, 0, 0}; // yerel orijin: ornekleme konumu disaridan verilir
    } else {
      e.parent = remap[e.parent];
    }
    if (e.asset >= 0 && (uint32_t)e.asset < src.asset_count) {
      if (amap[e.asset] < 0) amap[e.asset] = out->add_asset(src.assets[e.asset]);
      e.asset = amap[e.asset];
    } else {
      e.asset = -1;
    }
    remap[i] = (int32_t)out->entity_count;
    out->entities[out->entity_count++] = e;
  }

  // joint_target ikinci geciste: hedef, eklemden SONRA listelenmis olabilir.
  // Alt agacin DISINA isaret eden eklem kopar (-1) -- dis dunyaya bagli bir
  // prefab baska bir sahnede anlamsiz olurdu.
  for (uint32_t k = 0; k < out->entity_count; k++) {
    SceneEntity &e = out->entities[k];
    if (e.joint_target >= 0)
      e.joint_target = ((uint32_t)e.joint_target < src.entity_count) ? remap[e.joint_target] : -1;
  }
  return out->entity_count;
}

uint32_t prefab_instantiate(SceneDesc &dst, SceneHistory &h, const SceneDesc &prefab, Vec3 at, uint32_t *first_index) {
  const uint32_t n = prefab.entity_count;
  if (n == 0 || dst.entity_count + n > kSceneMaxEntities) return 0;

  // Hepsi ya da hicbiri: ONCE her seyi dogrula, sonra degistir.
  uint32_t missing = 0;
  for (uint32_t a = 0; a < prefab.asset_count; a++) {
    bool found = false;
    for (uint32_t b = 0; b < dst.asset_count && !found; b++) found = std::strcmp(dst.assets[b], prefab.assets[a]) == 0;
    if (!found) missing++;
  }
  if (dst.asset_count + missing > kSceneMaxAssets) return 0;
  for (uint32_t k = 0; k < n; k++) {
    const int32_t p = prefab.entities[k].parent;
    if (p >= (int32_t)k) return 0; // cocuk ebeveyninden ONCE: elle bozulmus dosya
    const int32_t a = prefab.entities[k].asset;
    if (a >= (int32_t)prefab.asset_count) return 0;
  }

  int32_t amap[kSceneMaxAssets];
  for (uint32_t a = 0; a < prefab.asset_count; a++) amap[a] = dst.add_asset(prefab.assets[a]);

  const uint32_t base = dst.entity_count;
  uint32_t added = 0;
  for (uint32_t k = 0; k < n; k++) {
    SceneEntity e = prefab.entities[k];
    if (e.parent < 0) e.pos = at + e.pos; // kok(ler) istenen yere
    else e.parent = (int32_t)(base + (uint32_t)e.parent);
    if (e.joint_target >= 0) e.joint_target = ((uint32_t)e.joint_target < n) ? (int32_t)(base + (uint32_t)e.joint_target) : -1;
    if (e.asset >= 0) e.asset = amap[e.asset];
    // add_entity yalniz sahne doluysa (yukarida denetlendi) ya da gunluk hic
    // kurulmamissa basarisiz olur; gunluk doluyken EN ESKIYI atar, reddetmez.
    if (!h.add_entity(dst, e)) break;
    added++;
  }
  if (first_index) *first_index = base;
  return added;
}

} // namespace tulpar::engine::content
