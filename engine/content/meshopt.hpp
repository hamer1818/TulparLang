// L6 CONTENT — mesh optimizasyonu (meshoptimizer, vendored, MIT). Yukleme
// aninda: tekillestirme, vertex cache (ACMR), overdraw, vertex fetch sirasi;
// sonra ayrik LOD'lar (meshopt_simplify, bagil hata sinirli). Mobilde vertex
// yeniden donusumu ve bellek bant genisligi kare butcesinin parcasi (Faz 6/9);
// cluster DAG'in temeli de bu kutuphane.
#pragma once
#include "content/model.hpp"

namespace tulpar::engine::content {

// mm yerinde guncellenir (yeni diziler Arena'da; eski yer bosa cikar — yukleme
// aninda kabul). stats: oncesi/sonrasi olcum (bilgi; test kapisi kotulesmemesini bekler).
bool mesh_optimize(Arena &arena, ModelMesh &mm, ModelOptStats *stats);

// ratios[i]: hedef indeks orani (0.5, 0.25); target_error: bagil (0.05 = %5).
// Hedefin %90'inin uzerinde kalan seviye "yok" sayilir (lod_index_count 0).
bool mesh_build_lods(Arena &arena, ModelMesh &mm, const float *ratios, uint32_t n, float target_error);

} // namespace tulpar::engine::content
