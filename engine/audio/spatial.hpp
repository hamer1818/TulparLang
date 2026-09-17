// L3 AUDIO — uzamsal ses: dinleyici bazi, mesafe sonumu, sabit-guc panlama ve
// KAFA GOLGESI. Saf fonksiyon: durum yok, ayirma yok, kilit yok — cihaz
// callback'inden guvenle cagrilir.
//
// "HRTF DEGIL, basit kafa golgesi": gercek HRTF olculmus bir HRIR veri kumesi
// (kisi basina yuzlerce yon x ~256 tap FIR) ister; depoda boyle bir kume yok ve
// lisansi uygun bir kume vendor edilmedi. Onun yerine karsi kulakta (kafanin
// golgesinde kalan kulak) yuksek frekanslarin zayiflamasi tek kutuplu alcak
// gecirgenle taklit edilir. Bu yon duygusu verir ama yukseklik (elevation) ve
// on/arka ayrimi TASIMAZ — genlik panlamasi on/arka icin ayni degeri uretir.
//
// Doppler YAPILMADI (bilincli): oynatma hizini (pitch) degistirmek gerekir,
// bugunku karistirici kareyi tam sayi adimla okuyor (pos++ ; yeniden ornekleme
// yok). Dogru Doppler once kesirli okuma + interpolasyon dilimi ister; o dilim
// acilinca ust katman bu baslikta hesaplanan bagil hizi kullanabilir.
#pragma once
#include <cmath>
#include <cstdint>

#include "core/math/vec.hpp"

namespace tulpar::engine::audio {

// Dinleyici bazi: konum + bakis yonu + yukari. Sag vektor cross(forward, up).
struct Listener {
  Vec3 pos{0, 0, 0};
  Vec3 forward{0, 0, -1};
  Vec3 up{0, 1, 0};
};

// Ses kaynagi basina uzamsal ayarlar. Bayraklar POZITIF KONTROL icin de var:
// tek degiskeni kapatip ayni yolu kosmak (sonum kapali -> duzey dusmemeli).
struct SpatialParams {
  Vec3 pos{0, 0, 0};
  float min_distance = 1.0f;   // bu mesafeye kadar sonum yok (referans mesafe)
  float max_distance = 100.0f; // pencere: burada ve otesinde sessiz
  float occlusion = 0.0f;      // 0..1 — DISARIDAN verilir (bkz. asagidaki not)
  bool distance_attenuation = true;
  bool panning = true;
  bool head_shadow = true;
};

// OKLUZYON KATMAN NOTU: audio L3, sim L4 — audio sim'i INCLUDE EDEMEZ (katman
// denetimi yakalar). Bu yuzden isin testi BURADA yapilmaz: dinleyici ile kaynak
// arasindaki isini uygulama/oyun katmani (L5/L6) fizik job'inda atar ve sonucu
// 0..1 bir sayi olarak Mixer::set_occlusion ile ses kaynagina PARAMETRE verir.
// Fizik tarafi icin sim::Physics::raycast() eklendi.

struct SpatialGains {
  float gain_l = 1.0f, gain_r = 1.0f; // kanal kazanci (sonum x panlama x okluzyon)
  float shadow_l = 0.0f, shadow_r = 0.0f; // kulak basina golge miktari 0..1 (0 = filtre yok)
  float distance = 0.0f;
  float azimuth = 0.0f; // -1 tam sol, 0 on/arka, +1 tam sag
};

// Okluzyon tam 1 iken kalan genlik ve uygulanan golge miktari.
constexpr float kOccludedGain = 0.20f;    // -14 dB
constexpr float kOcclusionShadow = 0.90f; // duvar HF'yi yutar
// Pencerenin yumusak kapanma payi (menzilin son %10'u) — klik olmasin.
constexpr float kDistanceWindow = 0.10f;

inline float clampf(float x, float lo, float hi) { return x < lo ? lo : (x > hi ? hi : x); }

inline SpatialGains spatial_compute(const Listener &l, const SpatialParams &s) {
  SpatialGains g;
  const Vec3 d = s.pos - l.pos;
  const float dist = length(d);
  g.distance = dist;

  // --- Mesafe sonumu: genlik 1/d (enerji 1/d^2 = "ters-kare") ---------------
  float att = 1.0f;
  if (s.distance_attenuation) {
    const float mn = s.min_distance > 1e-4f ? s.min_distance : 1e-4f;
    const float mx = s.max_distance > mn ? s.max_distance : mn * 1.0001f;
    const float dc = dist < mn ? mn : (dist > mx ? mx : dist);
    att = mn / dc;
    const float w = (mx - mn) * kDistanceWindow;
    if (dist >= mx) {
      att = 0.0f;
    } else if (w > 0.0f && dist > mx - w) {
      const float t = (mx - dist) / w; // 1 -> 0
      att *= t * t * (3.0f - 2.0f * t);
    }
  }

  // --- Panlama: dinleyici bazinda azimut, sabit guc (gl^2 + gr^2 = 1) -------
  float x = 0.0f;
  if (s.panning && dist > 1e-5f) {
    const Vec3 fwd = normalize(l.forward);
    const Vec3 right = normalize(cross(fwd, l.up));
    x = clampf(dot(d * (1.0f / dist), right), -1.0f, 1.0f);
  }
  g.azimuth = x;
  const float theta = (x * 0.5f + 0.5f) * (kPi * 0.5f);
  float gl = std::cos(theta), gr = std::sin(theta);

  // --- Okluzyon: genel zayiflama + iki kulakta alcak gecirgen ---------------
  const float occ = clampf(s.occlusion, 0.0f, 1.0f);
  att *= 1.0f - occ * (1.0f - kOccludedGain);

  // --- Kafa golgesi: karsi kulakta HF zayiflamasi ---------------------------
  float sl = 0.0f, sr = 0.0f;
  if (s.head_shadow) {
    sl = x > 0.0f ? x : 0.0f;  // kaynak sagda -> SOL kulak golgede
    sr = x < 0.0f ? -x : 0.0f; // kaynak solda -> SAG kulak golgede
  }
  const float occ_shadow = occ * kOcclusionShadow;
  if (occ_shadow > sl) sl = occ_shadow;
  if (occ_shadow > sr) sr = occ_shadow;

  g.gain_l = gl * att;
  g.gain_r = gr * att;
  g.shadow_l = sl;
  g.shadow_r = sr;
  return g;
}

// Tek kutuplu alcak gecirgen katsayisi: y += a*(x - y).
// a = 1 - exp(-2*pi*fc/sr); fc >= Nyquist ise 1 (gecirgen degil, birebir).
inline float onepole_coeff(float cutoff_hz, uint32_t rate) {
  if (rate == 0 || cutoff_hz <= 0.0f) return 1.0f;
  const float nyq = 0.5f * (float)rate;
  if (cutoff_hz >= nyq) return 1.0f;
  const float a = 1.0f - std::exp(-2.0f * kPi * cutoff_hz / (float)rate);
  return clampf(a, 0.0f, 1.0f);
}

// Golge miktari (0..1] -> kesim frekansi: 18 kHz .. 700 Hz, logaritmik.
inline float shadow_cutoff_hz(float amount) {
  const float a = clampf(amount, 0.0f, 1.0f);
  return 18000.0f * std::exp(a * std::log(700.0f / 18000.0f));
}

// Golge miktari -> dogrudan katsayi. 0 = filtre YOK (katsayi 1, birebir gecis);
// kontrol kapisi bunun tam 1 olmasina dayaniyor (kapaliyken iki kulak esit).
inline float shadow_coeff(float amount, uint32_t rate) {
  if (amount <= 0.0f) return 1.0f;
  return onepole_coeff(shadow_cutoff_hz(amount), rate);
}

} // namespace tulpar::engine::audio
