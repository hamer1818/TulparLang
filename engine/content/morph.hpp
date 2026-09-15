// L6 CONTENT — Morph target / blend shape karistirma (500 madde listesi #161
// "Morph Targets"). glTF morph target verisi (cgltf zaten okuyabiliyor)
// POZISYON FARKI olarak saklanir (mutlak pozisyon DEGIL) -- bu yuzden
// karistirma formulu basit toplamdir: out = base + sum(weight_i * delta_i).
// Ayni fonksiyon pozisyon VE normal icin (iki ayri cagriyla) kullanilir.
//
// content/ katmani renderer/'a BAGIMLI DEGIL (PLAN.md katman kurali): CPU'da
// hesaplanan cikti, cagiran (renderer/uygulama) ne isterse yukler (statik
// mesh gibi her kare yeniden yukleme, ya da GPU tarafinda vertex shader'da
// -- o zaman bu fonksiyon hic gerekmez, ilk dilim icin CPU-tarafi yeterli).
#pragma once
#include <cstdint>

#include "core/math/vec.hpp"

namespace tulpar::engine::content {

// target_deltas: DUZ dizi, target_count*vertex_count boyutunda (satir-major:
// t. hedefin delta'lari target_deltas[t*vertex_count .. (t+1)*vertex_count)
// araliginda). weights: target_count adet agirlik -- sinirlanmaz (cagiran
// karar verir, genelde [0,1] ama asiri/negatif deger de gecerli bir "abartma"
// efekti verir). out_positions: cagiranin ONCEDEN ayirdigi vertex_count
// boyutlu dizi -- base_positions/out_positions AYNI POINTER OLABILIR (yerinde
// guncelleme, ic donguler her defasinda okuyup HEMEN yazdigi icin guvenli).
void blend_morph_targets(const Vec3 *base_positions, uint32_t vertex_count, const Vec3 *target_deltas,
                          const float *weights, uint32_t target_count, Vec3 *out_positions);

} // namespace tulpar::engine::content
