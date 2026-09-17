// L0 PLATFORM — Kare hizi ayarlama (500 madde listesi #200/#464 "Frame
// Pacing"). Hedef kare suresine gore, bu kareyi bitirip sonrakini
// baslatmadan once ne kadar UYUNMASI (sleep) gerektigini hesaplar +
// "gecikmis kare" (missed frame -- bir onceki kare hedeften UZUN surdu)
// sayacini tutar. Basit, durum-siz bir hesap -- gercek uyku CAGIRANIN
// isi (platform/time.cpp zaten motorda var), bu sinif YALNIZ ARITMETIGI yapar.
#pragma once
#include <cstdint>

namespace tulpar::engine::platform {

class FramePacer {
 public:
  // target_fps<=0 ise pacing KAPALI sayilir (begin_frame HER ZAMAN 0 doner,
  // hicbir kare "gecikmis" sayilmaz -- "sinirsiz kare hizi" modu).
  explicit FramePacer(double target_fps)
      : target_frame_time_s_(target_fps > 0.0 ? 1.0 / target_fps : 0.0) {}

  // frame_start_s: bu karenin baslangic zaman damgasi (MONOTON ARTAN,
  // saniye). Donus: hedefe ULASMAK icin beklenmesi gereken sure -- NEGATIF/0
  // ISE zaten GECIKILMIS demektir (hic bekleme yapilmamali).
  double begin_frame(double frame_start_s) {
    if (target_frame_time_s_ <= 0.0) {
      last_start_s_ = frame_start_s;
      has_last_ = true;
      return 0.0;
    }
    double result = 0.0;
    if (has_last_) {
      const double elapsed = frame_start_s - last_start_s_;
      result = target_frame_time_s_ - elapsed;
      if (elapsed > target_frame_time_s_) missed_frames_++;
    }
    last_start_s_ = frame_start_s;
    has_last_ = true;
    return result;
  }

  uint64_t missed_frames() const { return missed_frames_; }
  double target_frame_time_s() const { return target_frame_time_s_; }

 private:
  double target_frame_time_s_;
  double last_start_s_ = 0.0;
  bool has_last_ = false;
  uint64_t missed_frames_ = 0;
};

} // namespace tulpar::engine::platform
