// L0 PLATFORM — Jest tanima: platform/touch.hpp'nin HAM cok-parmakli durumu
// (yalniz konum/down-up) uzerine TAP ve PINCH (2-parmak buyutme+donus)
// cikarir (500 madde listesi #459 "Coklu Dokunmatik + Jest" — dokunmatik
// KISMI zaten vardi/touch.hpp, JEST kismi buradaydi eksik).
//
// Bilincli olarak dar kapsam: TAP + PINCH. Drag/swipe icin ayri bir
// "recognizer" GEREKMEZ -- cagiran tek parmakli hareketi TouchState'ten
// dogrudan (onceki-simdiki fark) okuyabilir, katma deger dusuk. Uzun-basma/
// rotate ayri bahisler, ilk dilim kasitli dar (interp.hpp/lag_compensation.hpp
// ile ayni ilke).
//
// Kullanim: cagiran HER KAREDE, o karenin TouchState'ini (platform/touch.hpp,
// host'un doldurdugu) update()'e verir; donen GestureEvents o karede olusan
// (sifir/bir/birden fazla) jesti tasir. Ayirma yok (A2): sabit kapasite.
#pragma once
#include <cstdint>

#include "platform/touch.hpp"

namespace tulpar::engine::platform {

enum class GestureType : uint8_t { kNone, kTap, kPinchBegin, kPinchUpdate, kPinchEnd };

struct Gesture {
  GestureType type = GestureType::kNone;
  float x = 0, y = 0;         // Tap: dokunma konumu. Pinch: iki parmagin ORTA noktasi.
  float scale = 1.0f;         // PinchUpdate: bu KAREDEKI olcek carpani (ONCEKI kareye GORE, MUTLAK degil -- cagiran genelde `zoom *= scale` yapar)
  float rotation_rad = 0.0f;  // PinchUpdate: bu karedeki donus (radyan, ONCEKI kareye GORE, en kisa yol sarilmis)
};

struct GestureEvents {
  static constexpr uint32_t kMax = 4; // bir karede en fazla bu kadar olay (ör. tap + pinch-end ayni anda)
  Gesture events[kMax];
  uint32_t count = 0;
  void push(Gesture g) {
    if (count < kMax) events[count++] = g;
  }
};

class GestureRecognizer {
 public:
  // tap_max_duration_s: bu sureden UZUN basili tutma TAP SAYILMAZ.
  // tap_max_move_px: bu mesafeden FAZLA hareket TAP'i KALICI OLARAK bozar
  // (parmak geri gelse bile artik tap adayi degildir -- standart "slop" davranisi).
  explicit GestureRecognizer(float tap_max_duration_s = 0.3f, float tap_max_move_px = 16.0f);

  GestureEvents update(const TouchState &state, double now_s);

 private:
  struct TrackedTouch {
    int32_t id = -1;
    float start_x = 0, start_y = 0;
    double start_time_s = 0;
    bool moved_too_far = false;
  };
  static constexpr uint32_t kMaxTracked = TouchState::kMax;
  TrackedTouch tracked_[kMaxTracked];

  // Pinch: aktif ciftin id'leri (-1 = yok) + ONCEKI karedeki mesafe/aci
  // (delta-scale/delta-rotation hesabi icin -- mutlak degil, kareler ARASI).
  int32_t pinch_id_a_ = -1, pinch_id_b_ = -1;
  float pinch_prev_dist_ = 0.0f, pinch_prev_angle_ = 0.0f;
  bool pinch_active_ = false;

  float tap_max_duration_s_;
  float tap_max_move_px_;

  const TouchPoint *find_down(const TouchState &state, int32_t id) const;
};

} // namespace tulpar::engine::platform
