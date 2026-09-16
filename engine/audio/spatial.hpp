// L3 AUDIO — Uzamsal ses matematigi (420 madde listesi #11: "3D Ses (HRTF +
// Okluzyon)"). Bilincli olarak `mixer.hpp`'nin kilitsiz, gercek-zamanli
// render() yoluna DOKUNMAZ (o dosyanin kendi notu: "Uzamsal ses/HRTF sonraki
// dilim, Faz 4" -- gercek zamanli ses koduna derleyicisiz dokunmak riskli).
// Bunun yerine SAF matematik saglar: dinleyici+kaynak -> kazanc/filtre
// katsayisi. "HRTF-lite": tam kulak-kanali darbe tepkisi konvolusyonu
// (Steam Audio) DEGIL -- sabit-guc (equal-power) stereo pan + ters-kare
// mesafe azalimi (renderer'daki nokta isik dususuyle AYNI mantik, mesh.frag
// D_GGX/point_lights() Is 3'te kurulan fizik) + okluzyon icin bir alcak-gecis
// katsayisi ONERISI (filtrenin kendisini cagiran taraf uygular).
#pragma once
#include "core/math/vec.hpp"

namespace tulpar::engine::audio {

struct SpatialGains {
  float left = 1.0f, right = 1.0f;   // esit-guc pan: left^2 + right^2 == 1
  float distance_atten = 1.0f;        // ters-kare, [0,1]
  float lowpass_alpha = 1.0f;         // 1 = filtresiz; dusuk deger = daha karanlik/tikanik ses
};

struct SpatialListener {
  Vec3 pos{0, 0, 0};
  Vec3 right{1, 0, 0}; // dinleyicinin sag eli, normalize KABUL EDILIR (cagiran normalize eder)
};

// occlusion: 0 = engelsiz, 1 = tam engelli (cagiran bir raycast ile hesaplar
// -- bu fonksiyon raycast YAPMAZ, yalniz sonucu kazanca cevirir).
// min_distance altinda azalim 1.0'da kilitlenir (bolme-sifir/asiri kazanc
// onlenir); max_distance ve otesinde azalim 0'dir (sert kesim).
SpatialGains spatial_compute(const SpatialListener &listener, Vec3 source_pos, float occlusion,
                             float min_distance = 1.0f, float max_distance = 100.0f);

} // namespace tulpar::engine::audio
