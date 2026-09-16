// L6 APP — Sahne dosya formati: EditorEntity[] <-> insan-okunur metin dosyasi.
// DEVAM_PLANI.md Faz A madde 1-2 karsiligi ("sahne formati" + "kaydet/yukle").
// Format bilincli olarak basit tutuldu (satir = anahtar deger), git diff'i
// tek satirlik degisiklik gosterir (ornek: bir entity'nin konumu degisince
// yalniz o entity'nin "pos" satiri degisir).
//
// Ornek dosya:
//   # tulpar sahne v1
//   entity kure_1
//     pos -8.000000 1.200000 -8.500000
//     rot 0.000000 0.000000 0.000000
//     scale 1.000000 1.000000 1.000000
//     kind 0
//     phase 0.000000
#pragma once
#include "app/editor_app.hpp"

namespace tulpar::engine::app {

struct SceneIoResult {
  bool ok = false;
  char error[128] = {};
};

// ents[0..count) dosyaya yazilir (UTF-8 metin, LF). Basarisizlikta
// result.ok == false ve result.error dolu; dosyanin durumu tanimsizdir
// (kismi yazilmis olabilir).
SceneIoResult scene_save(const char *path, const EditorEntity *ents, int count);

// Dosyadan en fazla max_count entity okunur, *out_count gercek okunan sayiyi
// alir. Basarisizlikta (dosya yok, max_count asildi) *out_count DEGISMEZ ve
// ents[] icerigi tanimsiz olabilir — cagiran onceki sahneyi geri yuklemeli.
// Bilinmeyen/bozuk satirlar sessizce atlanir (ileri-uyumluluk: gelecekte
// eklenecek alanlar eski yukleyiciyi kirmaz).
SceneIoResult scene_load(const char *path, EditorEntity *ents, int max_count, int *out_count);

} // namespace tulpar::engine::app
