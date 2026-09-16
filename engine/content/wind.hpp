// L6 CONTENT — Prosedurel ruzgar alani (500 madde listesi #472 "Wind
// Animation" — rhi/shaders/foliage.vert'in ("bu oturumda yazildi, kablolama
// BEKLIYOR" notuyla) ihtiyac duydugu CPU-tarafi ruzgar orneklemesi: shader
// bir "zaman" alani + vertex basina ruzgar-agirligi bekliyordu, bu fonksiyon
// gelecekte o alanlari BESLEYECEK ruzgar VEKTORUNU dunya konumu+zamana gore
// hesaplar). core/math/noise.hpp'nin (fbm_2d) DOGRUDAN tuketicisi.
#pragma once
#include "core/math/vec.hpp"

namespace tulpar::engine::content {

struct WindField {
  Vec2 direction{1, 0};    // ANA ruzgar yonu, NORMALIZE EDILMIS OLMALI (cagiran saglar)
  float strength = 1.0f;   // temel ruzgar siddeti (dunya birimi/saniye cinsinden bukulme olcegi)
  float gustiness = 0.5f;  // [0,1]: ani ruzgar (gust) varyasyon MIKTARI
  float gust_frequency = 0.3f; // gurultunun dunya-uzayi orneklemesi icin frekans
  uint32_t seed = 0;
};

// world_x,world_z: orneklenen dunya konumu. t: zaman (saniye, MONOTON
// ARTAN). Donus: o nokta/zamandaki ruzgar VEKTORU (yon*siddet, dunya XZ
// duzleminde). gustiness=0 -> HER ZAMAN TAM olarak direction*strength
// (gurultudan bagimsiz, testlerde dogrudan kanitlanir).
Vec2 sample_wind(const WindField &wind, float world_x, float world_z, float t);

} // namespace tulpar::engine::content
