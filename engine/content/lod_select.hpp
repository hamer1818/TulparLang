// L6 CONTENT — Mesafeye gore LOD secimi (500 madde listesi #95 "Traditional
// LOD", #97 "Dithered LOD"). content/meshoptimizer entegrasyonunun (zaten
// motorda, DURUM.md L6) URETTIGI ayrik LOD seviyeleri arasinda HANGISININ
// secilecegine karar veren, motordan/renderer'dan BAGIMSIZ saf mantik.
#pragma once
#include <cstdint>

namespace tulpar::engine::content {

// thresholds: lod_count-1 boyutunda, ARTAN sirali mesafe esikleri
// (thresholds[i] = LOD i -> LOD i+1 gecis mesafesi). distance<thresholds[0]
// -> LOD 0, ... distance>=thresholds[lod_count-2] -> son LOD (en dusuk detay).
uint32_t select_lod_traditional(float distance, const float *thresholds, uint32_t lod_count);

struct DitheredLod {
  uint32_t lod_a = 0; // yakin (daha detayli) LOD
  uint32_t lod_b = 0; // uzak (daha az detayli) LOD -- gecis DISINDA lod_a ile AYNI
  float blend = 0.0f; // 0 = tam lod_a, 1 = tam lod_b (cagiran crossfade/dither ile karistirir)
};

// Her esigin etrafinda `blend_range` genisliginde bir gecis bandi tanimlar
// (esik MERKEZDE): bandin DISINDA sert secim (Traditional ile AYNI sonuc,
// blend=0), bandin ICINDE lod_a/lod_b arasinda DOGRUSAL blend. Boylece LOD
// degisimi ani "pop" yerine yumusak gecer (dithered/crossfade LOD'un amaci).
DitheredLod select_lod_dithered(float distance, const float *thresholds, uint32_t lod_count, float blend_range);

} // namespace tulpar::engine::content
