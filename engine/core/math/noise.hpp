// L1 CORE — Deterministik deger gurultusu (value noise) + fraktal Brownian
// motion (fBm). core/math/random.hpp'nin (500 madde listesi #438 "PCG")
// DOGAL UZANTISI: terrain yukseklik haritasi, bulut/varyasyon gibi
// prosedurel icerigin standart ikinci tas'i. Perlin'in DEGRADE gurultusu
// DEGIL (daha basit deger-gurultusu -- kafes noktalarinda hash DEGERLERI,
// gradyan degil -- ilk dilim icin yeterli, daha ucuz).
//
// TAM SAYI hash + smoothstep enterpolasyonu -- kayan nokta/libm'e yalnizca
// floor/interpolasyonda ihtiyac var, HASH'in kendisi tam sayi bit
// operasyonlari (motorun determinizm ilkesiyle ayni ruh, core/math/random.hpp
// gibi -- ayni (nokta,seed) HER platformda AYNI degeri verir).
#pragma once
#include <cmath>
#include <cstdint>

namespace tulpar::engine {

namespace detail {
inline float smoothstep01(float t) { return t * t * (3.0f - 2.0f * t); }
} // namespace detail

// Tam sayi kafes noktasini (ix,iy) [0,1) araliginda deterministik bir
// degere hash'ler -- ayni core/math/random.hpp'deki next_float() gibi ust
// 24 bit kullanilir (float mantissa hassasiyetiyle eslesir).
inline float lattice_hash(int32_t ix, int32_t iy, uint32_t seed) {
  uint32_t h = seed;
  h ^= (uint32_t)ix * 0x27d4eb2fu;
  h = (h ^ (h >> 15)) * 0x85ebca6bu;
  h ^= (uint32_t)iy * 0x165667b1u;
  h = (h ^ (h >> 13)) * 0xc2b2ae35u;
  h ^= h >> 16;
  return (float)(h >> 8) * (1.0f / 16777216.0f);
}

// 2B deger gurultusu: x,y herhangi bir kayan nokta konumu. Cevredeki 4
// kafes noktasinin hash degerleri arasinda smoothstep ile yumusak
// enterpolasyon -- TAM bir kafes noktasinda (x,y tam sayi) SONUC O
// noktanin lattice_hash'ine BIREBIR esittir (enterpolasyon agirligi o
// kosede 1, digerlerinde 0 olur -- bu, testlerde ELLE kanitlanan ozelliktir).
// Donus HER ZAMAN [0,1) araliginda (4 kose degerinin konveks kombinasyonu).
inline float value_noise_2d(float x, float y, uint32_t seed) {
  const int32_t ix0 = (int32_t)std::floor(x);
  const int32_t iy0 = (int32_t)std::floor(y);
  const int32_t ix1 = ix0 + 1, iy1 = iy0 + 1;
  const float fx = x - (float)ix0;
  const float fy = y - (float)iy0;

  const float v00 = lattice_hash(ix0, iy0, seed);
  const float v10 = lattice_hash(ix1, iy0, seed);
  const float v01 = lattice_hash(ix0, iy1, seed);
  const float v11 = lattice_hash(ix1, iy1, seed);

  const float sx = detail::smoothstep01(fx);
  const float sy = detail::smoothstep01(fy);

  const float a = v00 + (v10 - v00) * sx;
  const float b = v01 + (v11 - v01) * sx;
  return a + (b - a) * sy;
}

// Fraktal Brownian Motion: octaves kez value_noise_2d'yi ikiye katlanan
// frekans (lacunarity), yariya inen genlikle (gain) toplar, TOPLAM genlige
// gore normalize eder (sonuc yine [0,1) araliginda kalir -- agirlikli
// ortalama). Her oktav FARKLI bir tohumla ornekler (seed + i*altin-oran-
// sabiti) -- aksi halde oktavlar ayni desenin olceklenmis kopyasi olurdu.
// octaves=1 -> tanim geregi value_noise_2d(x,y,seed) ile BIREBIR AYNI
// (testlerde dogrudan kanitlanir).
inline float fbm_2d(float x, float y, uint32_t seed, int octaves, float lacunarity = 2.0f,
                     float gain = 0.5f) {
  float sum = 0.0f, amplitude = 1.0f, freq = 1.0f, max_amp = 0.0f;
  for (int i = 0; i < octaves; i++) {
    sum += value_noise_2d(x * freq, y * freq, seed + (uint32_t)i * 0x9E3779B9u) * amplitude;
    max_amp += amplitude;
    amplitude *= gain;
    freq *= lacunarity;
  }
  return max_amp > 0.0f ? sum / max_amp : 0.0f;
}

} // namespace tulpar::engine
