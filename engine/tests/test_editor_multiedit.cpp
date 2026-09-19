// editor_multiedit kapilari: coklu secimde alan duzenlemesi DOGRU yaprak
// duzeyinde yayiliyor mu. Her biri, yanlis bir tasarimin (butun struct'i ya da
// butun kelimeyi kopyalamak) GERCEKTEN bozacagi bir durumu olcer.
#include "tests/test.hpp"

#include <cstring>

#include "app/editor_multiedit.hpp"

using namespace tulpar::engine;
using content::SceneEntity;

namespace {
SceneEntity make(float x, float y, float z) {
  SceneEntity e{};
  e.pos = Vec3{x, y, z};
  return e;
}
} // namespace

// pos.x yazmak digerlerinin pos.y/pos.z'sine DOKUNMAMALI. Butun Vec3'u
// kopyalayan bir tasarim secimi tek noktaya yigardi.
ENGINE_TEST(multiedit_vec_component_is_its_own_leaf) {
  const SceneEntity before = make(1, 2, 3);
  SceneEntity after = before;
  after.pos.x = 9;
  SceneEntity target = make(5, 6, 7);
  CHECK(app::multiedit_apply(before, after, &target));
  CHECK(target.pos.x == 9 && target.pos.y == 6 && target.pos.z == 7);
}

// Bilesen eklemek hedefin KENDI bilesenlerini silmemeli. Butun kelimeyi
// kopyalamak, Model'e Isik eklerken secimdeki Govdeli nesnenin Govdesini
// kaldirirdi.
ENGINE_TEST(multiedit_component_bits_are_merged_not_copied) {
  SceneEntity before{};
  before.components = content::kSceneModel;
  SceneEntity after = before;
  after.components |= content::kSceneLight; // ana secilide Isik eklendi
  SceneEntity target{};
  target.components = content::kSceneBody;  // hedefin kendi bileseni
  CHECK(app::multiedit_apply(before, after, &target));
  CHECK((target.components & content::kSceneLight) != 0); // eklenen geldi
  CHECK((target.components & content::kSceneBody) != 0);  // kendisininki KALDI
  CHECK((target.components & content::kSceneModel) == 0); // ana secilinin eskisi TASINMADI

  // Kaldirma da yalniz kaldirilan biti etkiler.
  SceneEntity b2 = after, a2 = after;
  a2.components &= ~content::kSceneLight;
  CHECK(app::multiedit_apply(b2, a2, &target));
  CHECK((target.components & content::kSceneLight) == 0);
  CHECK((target.components & content::kSceneBody) != 0);
}

// Ad yayilmaz: 20 nesneyi secip birinin adini degistirmek 20 ayni ad uretmemeli.
ENGINE_TEST(multiedit_name_is_never_propagated) {
  SceneEntity before{};
  std::strcpy(before.name, "a");
  SceneEntity after = before;
  std::strcpy(after.name, "yeni_ad");
  SceneEntity target{};
  std::strcpy(target.name, "b");
  CHECK(!app::multiedit_apply(before, after, &target)); // baska hicbir sey degismedi
  CHECK(std::strcmp(target.name, "b") == 0);
}

// Metin alani (dosya yolu) BUTUN olarak gider: yarim dizi kopyalamak iki yolun
// karisimini uretirdi.
ENGINE_TEST(multiedit_string_field_is_copied_whole) {
  SceneEntity before{};
  std::strcpy(before.audio_clip, "kisa.wav");
  SceneEntity after = before;
  std::strcpy(after.audio_clip, "cok_daha_uzun_bir_ad.wav");
  SceneEntity target{};
  std::strcpy(target.audio_clip, "hedefin_kendi_uzun_dosya_adi.wav");
  CHECK(app::multiedit_apply(before, after, &target));
  CHECK(std::strcmp(target.audio_clip, "cok_daha_uzun_bir_ad.wav") == 0);
}

// Hicbir sey degismediyse hedef de degismez ve fonksiyon bunu soyler:
// gunluge bos islem dusmemeli.
ENGINE_TEST(multiedit_no_change_means_no_change) {
  const SceneEntity e = make(1, 2, 3);
  SceneEntity target = make(4, 5, 6);
  const SceneEntity copy = target;
  CHECK(!app::multiedit_apply(e, e, &target));
  CHECK(std::memcmp(&target, &copy, sizeof target) == 0);
}

// Tablo bos degil ve makul buyuklukte (tools/scene_check.py kapsamini ayrica olcer).
ENGINE_TEST(multiedit_leaf_table_is_populated) {
  CHECK(app::multiedit_leaf_count() > 60);
}
