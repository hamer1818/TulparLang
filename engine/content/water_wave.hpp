// L6 CONTENT — Gerstner dalgalari (500 madde listesi #137 "Gerstner
// Dalgalari"). GPU Gems 1, Bolum 1'deki (Finch, "Effective Water Simulation
// from Physical Models") standart, yaygin bilinen formul -- kendi turetim
// degil, ama asagida NOKTA NOKTA elle dogrulandi (tests/test_water_wave.cpp).
//
// Su yuzeyindeki HER nokta, dalga yayilma yonu boyunca dairesel bir yorunge
// izler (Gerstner/trokoidal dalga -- gercek okyanus dalgalarina Sinus
// dalgasindan DAHA yakin: sivri tepe, yassi cukur). Bircok dalga UST USTE
// eklenerek gercekci "karisik deniz" elde edilir.
//
// content/ katmani renderer/'a BAGIMLI DEGIL (PLAN.md katman kurali): cikti
// bir yer-degistirme vektoru, cagiran (CPU mesh guncelleme ya da vertex
// shader'a push constant/UBO ile) kendi entegre eder.
#pragma once
#include "core/math/vec.hpp"

namespace tulpar::engine::content {

struct GerstnerWave {
  Vec2 direction{1, 0};  // yayilma yonu, NORMALIZE EDILMIS OLMALI (cagiran saglar)
  float wavelength = 10.0f; // dalga boyu (dunya birimi), > 0
  float amplitude = 0.5f;   // dalga yuksekligi (tepe-orta mesafesi)
  // Sivrilik [0,1]: 0=duz sinus, 1'e yaklastikca tepeler sivrilesir/cukurlar
  // yassilasir. UYARI (standart Gerstner tuzagi): COK sayida dalganin
  // steepness*amplitude*k TOPLAMI 1'i asarsa yorungeler KESISIR (dalga
  // "kivrilir/loop atar") -- cagiran toplam sivriligi N dalgaya gore
  // olceklemeli (ör. steepness/dalga_sayisi).
  float steepness = 0.3f;
  float speed = 1.0f; // faz hizi (radyan/saniye cinsinden ACISAL hiz CARPANI)
};

// Tek bir dalganin (x0,z0) TABAN (dinlenme) konumunda, t anindaki yer-
// degistirmesi -- TABAN konumuna EKLENECEK (dx,dy,dz) ofseti (mutlak
// pozisyon DEGIL).
Vec3 gerstner_displacement(const GerstnerWave &w, float x0, float z0, float t);

// N dalganin yer-degistirmelerinin TOPLAMI (standart "birden fazla dalga
// UST USTE" teknigi -- farkli yon/dalga boyu/faz ile gercekci gorunum).
Vec3 sum_gerstner_displacement(const GerstnerWave *waves, uint32_t count, float x0, float z0, float t);

} // namespace tulpar::engine::content
