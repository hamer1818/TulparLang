// L3 AUDIO — cihaz: miniaudio (vendored, MIT-0) uzerinden platform ses cikisi.
// Android: AAudio (API 26+, dusuk gecikme; Oboe'nin sardigi API) -> OpenSL yedek;
// Linux: PulseAudio -> ALSA; macOS: CoreAudio. "null" arka ucu: cihazsiz makine /
// testler (callback thread'i calisir, ses yok). Callback -> Mixer::render.
// Karar (tarama belgesi §9/§11): Oboe'nin ek cihaz-tuzak katmani, gercek cihazda
// sorun gorulurse eklenir; AAudio dogrudan kullaniliyor.
//
// SES THREAD ONCELIGI: thread'i miniaudio acar; oncelik ma_context uzerinden
// istenir. Varsayilan `highest` (miniaudio'nun kendi varsayilani, SCHED_OTHER
// icinde en yuksek nice). `realtime_thread = true` SCHED_FIFO ister — Linux'ta
// RLIMIT_RTPRIO/CAP_SYS_NICE yoksa pthread_create EPERM doner ve miniaudio
// NORMAL oncelige DUSER (highest'tan da asagi), bu yuzden istege bagli.
// Elde edilen oncelik TAHMIN EDILMEZ, OLCULUR: pthread_getschedparam ile
// cihazin worker thread'inden okunup DeviceInfo'ya yazilir.
// UYARI (olculdu 2026-09-15, CachyOS): ayricalik VARSA miniaudio SCHED_FIFO'yu
// sched_get_priority_max ile, yani oncelik 99 ile acar — cogu cekirdek
// thread'inin ustunde. Donen bir ses thread'i makineyi kilitleyebilir; bu
// yuzden varsayilan `highest`tir ve realtime acikca istenir.
#pragma once
#include <cstdint>

#include "audio/mixer.hpp"

namespace tulpar::engine::audio {

struct DeviceConfig {
  uint32_t sample_rate = 48000;
  uint32_t channels = 2;
  uint32_t period_frames = 0;   // 0 = arka ucun varsayilani (Android: dusuk gecikme)
  bool null_backend = false;    // test: cihaz yok, sadece callback thread'i
  bool realtime_thread = false; // SCHED_FIFO iste (ayricalik yoksa NORMAL'e duser)
};

struct DeviceInfo {
  char backend[32] = {0};
  char name[96] = {0};
  uint32_t sample_rate = 0, channels = 0, period_frames = 0;
  // OLCULDU (pthread_getschedparam), iddia degil. "?" = arka uc kendi
  // thread'ini yonetiyor ya da platform sorgulanamadi.
  char thread_policy[16] = {0}; // "OTHER" / "FIFO" / "RR" / "?"
  int thread_priority = 0;
  bool thread_realtime = false; // FIFO/RR elde edildi mi
};

class AudioDevice {
public:
  bool init(Mixer &mixer, const DeviceConfig &cfg);
  void shutdown();
  bool ok() const { return started_; }
  const DeviceInfo &info() const { return info_; }
  const char *last_error() const { return err_; }

private:
  void *impl_ = nullptr; // ma_context + ma_device (heap, yukleme aninda)
  Mixer *mixer_ = nullptr;
  DeviceInfo info_{};
  bool started_ = false;
  char err_[128] = {0};
};

} // namespace tulpar::engine::audio
