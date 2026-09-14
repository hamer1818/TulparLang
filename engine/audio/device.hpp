// L3 AUDIO — cihaz: miniaudio (vendored, MIT-0) uzerinden platform ses cikisi.
// Android: AAudio (API 26+, dusuk gecikme; Oboe'nin sardigi API) -> OpenSL yedek;
// Linux: PulseAudio -> ALSA; macOS: CoreAudio. "null" arka ucu: cihazsiz makine /
// testler (callback thread'i calisir, ses yok). Callback -> Mixer::render.
// Karar (tarama belgesi §9/§11): Oboe'nin ek cihaz-tuzak katmani, gercek cihazda
// sorun gorulurse eklenir; AAudio dogrudan kullaniliyor.
#pragma once
#include <cstdint>

#include "audio/mixer.hpp"

namespace tulpar::engine::audio {

struct DeviceConfig {
  uint32_t sample_rate = 48000;
  uint32_t channels = 2;
  uint32_t period_frames = 0;   // 0 = arka ucun varsayilani (Android: dusuk gecikme)
  bool null_backend = false;    // test: cihaz yok, sadece callback thread'i
};

struct DeviceInfo {
  char backend[32] = {0};
  char name[96] = {0};
  uint32_t sample_rate = 0, channels = 0, period_frames = 0;
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
