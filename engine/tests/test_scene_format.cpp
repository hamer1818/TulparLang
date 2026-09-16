// Sahne kaydet/yukle: EditorEntity[] <-> metin dosyasi roundtrip + pozitif/
// negatif kontrol (DURUM.md §4 kurali: her kapinin acik/kapali karsilastirmasi
// olsun).
#include <cmath>
#include <cstdio>
#include <cstring>

#include "app/scene_format.hpp"
#include "tests/test.hpp"

using namespace tulpar::engine;
using namespace tulpar::engine::test;

namespace {
bool approx(float a, float b) { return std::fabs(a - b) < 1e-4f; }

void make_entity(app::EditorEntity &e, const char *name, float x, float y, float z, int kind, float phase) {
  std::memset(&e, 0, sizeof e);
  std::snprintf(e.name, sizeof e.name, "%s", name);
  e.pos[0] = x; e.pos[1] = y; e.pos[2] = z;
  e.rot_deg[0] = 12.5f; e.rot_deg[1] = -45.0f; e.rot_deg[2] = 180.0f;
  e.scale[0] = 1.0f; e.scale[1] = 2.5f; e.scale[2] = 0.5f;
  e.kind = kind;
  e.phase = phase;
}
} // namespace

ENGINE_TEST(scene_roundtrip_preserves_fields) {
  app::EditorEntity src[3];
  make_entity(src[0], "kure_1", -8.0f, 1.2f, -8.5f, 0, 0.0f);
  make_entity(src[1], "boru_1", 4.0f, 0.0f, -3.5f, 1, 0.35f);
  make_entity(src[2], "boru_2", -4.0f, 0.0f, -3.5f, 1, 0.7f);

  char path[512];
  std::snprintf(path, sizeof path, "%s/tulpar_scene_roundtrip.tsc", tmp_dir());

  app::SceneIoResult sr = app::scene_save(path, src, 3);
  CHECK(sr.ok);
  if (!sr.ok) { std::printf("    [bilgi] kaydet: %s\n", sr.error); return; }

  app::EditorEntity dst[3];
  int loaded = 0;
  app::SceneIoResult lr = app::scene_load(path, dst, 3, &loaded);
  CHECK(lr.ok);
  CHECK(loaded == 3);
  for (int i = 0; i < 3 && i < loaded; i++) {
    CHECK(std::strcmp(dst[i].name, src[i].name) == 0);
    CHECK(approx(dst[i].pos[0], src[i].pos[0]) && approx(dst[i].pos[1], src[i].pos[1]) && approx(dst[i].pos[2], src[i].pos[2]));
    CHECK(approx(dst[i].rot_deg[0], src[i].rot_deg[0]) && approx(dst[i].rot_deg[1], src[i].rot_deg[1]) && approx(dst[i].rot_deg[2], src[i].rot_deg[2]));
    CHECK(approx(dst[i].scale[0], src[i].scale[0]) && approx(dst[i].scale[1], src[i].scale[1]) && approx(dst[i].scale[2], src[i].scale[2]));
    CHECK(dst[i].kind == src[i].kind);
    CHECK(approx(dst[i].phase, src[i].phase));
  }
  std::remove(path);
}

ENGINE_TEST(scene_load_missing_file_reports_error) {
  // Negatif kontrol: olmayan dosya sessizce 0 entity dondurmemeli, ok==false olmali.
  int loaded = 99;
  app::EditorEntity dst[3];
  app::SceneIoResult lr = app::scene_load("/tulpar_bu_dosya_kesinlikle_yok_xyz.tsc", dst, 3, &loaded);
  CHECK(!lr.ok);
  CHECK(loaded == 99); // out_count basarisizlikta DEGISMEMELI (sozlesme)
}

ENGINE_TEST(scene_load_respects_max_count) {
  // Pozitif kontrol (yukarida) + burada max_count'u asan sahne temiz hata vermeli,
  // dizi tasmasi (overflow) OLMAMALI.
  app::EditorEntity src[3];
  make_entity(src[0], "a", 0, 0, 0, 0, 0);
  make_entity(src[1], "b", 1, 1, 1, 0, 0);
  make_entity(src[2], "c", 2, 2, 2, 0, 0);
  char path[512];
  std::snprintf(path, sizeof path, "%s/tulpar_scene_overflow.tsc", tmp_dir());
  CHECK(app::scene_save(path, src, 3).ok);

  app::EditorEntity dst[2]; // kasitli kucuk: 3 entity'lik dosyayi 2'ye sigdirmaya calisir
  int loaded = -1;
  app::SceneIoResult lr = app::scene_load(path, dst, 2, &loaded);
  CHECK(!lr.ok);
  std::printf("    [bilgi] beklenen hata: %s\n", lr.error);
  std::remove(path);
}
