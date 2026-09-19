// prefab kapilari: alt agac cikarma + ornekleme. Her biri, indeks yeniden
// eslemesinin atlanmasi durumunda ornegin hedef sahnede YANLIS varliga ya da
// kaynaga isaret edecegi bir durumu olcer.
#include "tests/test.hpp"

#include <cstring>

#include "content/prefab.hpp"

#include "core/memory/arena.hpp"

using namespace tulpar::engine;
using namespace tulpar::engine::content;

namespace {
SystemArena &arena() {
  static SystemArena sys;
  if (sys.capacity() == 0) sys.reserve(8u << 20, "prefab_test");
  return sys;
}
int32_t add(SceneDesc &d, const char *name, int32_t parent, int32_t asset = -1) {
  SceneEntity e{};
  std::strncpy(e.name, name, sizeof e.name - 1);
  e.parent = parent;
  e.asset = asset;
  if (asset >= 0) e.components |= kSceneModel;
  d.entities[d.entity_count] = e;
  return (int32_t)d.entity_count++;
}
} // namespace

// A(kok) -> B -> C, ayrica ilgisiz D. C bir kaynak kullanir, B'nin eklemi C'ye bagli.
ENGINE_TEST(prefab_extract_remaps_parent_asset_and_joint) {
  static SceneDesc src;
  src.entity_count = 0; src.asset_count = 0;
  const int32_t unused = src.add_asset("kullanilmayan.gltf");
  const int32_t xa = src.add_asset("x.gltf");
  CHECK(unused == 0 && xa == 1);
  const int32_t d = add(src, "D", -1);
  const int32_t a = add(src, "A", -1);
  src.entities[a].pos = Vec3{5, 6, 7};
  const int32_t b = add(src, "B", a);
  const int32_t c = add(src, "C", b, xa);
  src.entities[b].joint_target = c;
  src.entities[a].joint_target = d; // alt agac DISINA -> kopmali
  (void)d;

  static SceneDesc pf;
  CHECK(prefab_extract(src, a, &pf) == 3);
  CHECK(pf.entity_count == 3);
  CHECK(std::strcmp(pf.entities[0].name, "A") == 0 && pf.entities[0].parent == -1);
  CHECK(pf.entities[0].pos.x == 0 && pf.entities[0].pos.y == 0 && pf.entities[0].pos.z == 0); // kok orijinde
  CHECK(std::strcmp(pf.entities[1].name, "B") == 0 && pf.entities[1].parent == 0);
  CHECK(std::strcmp(pf.entities[2].name, "C") == 0 && pf.entities[2].parent == 1);
  // Yalniz KULLANILAN kaynak tasindi ve indeksi yeniden eslendi (1 -> 0).
  CHECK(pf.asset_count == 1 && std::strcmp(pf.assets[0], "x.gltf") == 0);
  CHECK(pf.entities[2].asset == 0);
  // Eklem: ic hedef yeniden eslendi, dis hedef koptu.
  CHECK(pf.entities[1].joint_target == 2);
  CHECK(pf.entities[0].joint_target == -1);
}

ENGINE_TEST(prefab_instantiate_appends_with_remapped_indices) {
  static SceneDesc pf;
  pf.entity_count = 0; pf.asset_count = 0;
  const int32_t pa = pf.add_asset("x.gltf");
  const int32_t r = add(pf, "kok", -1);
  const int32_t k = add(pf, "cocuk", r, pa);
  pf.entities[r].joint_target = k;

  static SceneDesc dst;
  dst.entity_count = 0; dst.asset_count = 0;
  dst.add_asset("y.gltf");
  dst.add_asset("x.gltf"); // prefab'in kaynagi hedefte ZATEN var, indeks 1
  add(dst, "mevcut0", -1);
  add(dst, "mevcut1", -1);

  SceneHistory h;
  CHECK(h.init(arena(), 64));
  uint32_t first = 0;
  CHECK(prefab_instantiate(dst, h, pf, Vec3{10, 0, 0}, &first) == 2);
  CHECK(first == 2 && dst.entity_count == 4);
  CHECK(dst.entities[2].parent == -1 && dst.entities[2].pos.x == 10); // kok istenen yerde
  CHECK(dst.entities[3].parent == 2);                                 // 0 -> 2
  CHECK(dst.entities[2].joint_target == 3);                           // 1 -> 3
  CHECK(dst.asset_count == 2 && dst.entities[3].asset == 1);          // yol ile eslendi, YENI eklenmedi
  // Geri alinabilir: iki ekleme gunlukte.
  CHECK(h.undo(dst) && h.undo(dst) && dst.entity_count == 2);
}

// Hepsi ya da hicbiri: yer yoksa HICBIR sey eklenmemeli.
ENGINE_TEST(prefab_instantiate_is_all_or_nothing) {
  static SceneDesc pf;
  pf.entity_count = 0; pf.asset_count = 0;
  add(pf, "a", -1);
  add(pf, "b", 0);

  static SceneDesc dst;
  dst.entity_count = 0; dst.asset_count = 0;
  for (uint32_t i = 0; i < kSceneMaxEntities - 1; i++) add(dst, "dolu", -1); // yalniz 1 yer kaldi
  SceneHistory h;
  CHECK(h.init(arena(), 16));
  const uint32_t before = dst.entity_count;
  CHECK(prefab_instantiate(dst, h, pf, Vec3{0, 0, 0}, nullptr) == 0);
  CHECK(dst.entity_count == before);

  // Elle bozulmus dosya: cocuk ebeveyninden ONCE -> reddedilir.
  static SceneDesc bad;
  bad.entity_count = 0; bad.asset_count = 0;
  add(bad, "cocuk", 1);
  add(bad, "ebeveyn", -1);
  static SceneDesc dst2;
  dst2.entity_count = 0; dst2.asset_count = 0;
  CHECK(prefab_instantiate(dst2, h, bad, Vec3{0, 0, 0}, nullptr) == 0);
  CHECK(dst2.entity_count == 0);
}
