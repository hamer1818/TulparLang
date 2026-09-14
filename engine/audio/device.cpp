#include "audio/device.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <miniaudio.h>

namespace tulpar::engine::audio {

namespace {
struct Impl {
  ma_context context;
  ma_device device;
  Mixer *mixer;
};
void data_callback(ma_device *dev, void *out, const void *, ma_uint32 frames) {
  Impl *im = static_cast<Impl *>(dev->pUserData);
  im->mixer->render(static_cast<float *>(out), frames);
}
} // namespace

bool AudioDevice::init(Mixer &mixer, const DeviceConfig &cfg) {
  mixer_ = &mixer;
  Impl *im = static_cast<Impl *>(std::calloc(1, sizeof(Impl)));
  if (!im) { std::snprintf(err_, sizeof err_, "bellek"); return false; }
  im->mixer = &mixer;
  ma_backend backends[8];
  ma_uint32 nb = 0;
  if (cfg.null_backend) backends[nb++] = ma_backend_null;
  else {
#if defined(__ANDROID__)
    backends[nb++] = ma_backend_aaudio; backends[nb++] = ma_backend_opensl;
#elif defined(__APPLE__)
    backends[nb++] = ma_backend_coreaudio;
#else
    backends[nb++] = ma_backend_pulseaudio; backends[nb++] = ma_backend_alsa;
#endif
  }
  ma_context_config cc = ma_context_config_init();
  ma_result r = ma_context_init(backends, nb, &cc, &im->context);
  if (r != MA_SUCCESS) { std::snprintf(err_, sizeof err_, "ma_context_init: %s", ma_result_description(r)); std::free(im); return false; }
  ma_device_config dc = ma_device_config_init(ma_device_type_playback);
  dc.playback.format = ma_format_f32;
  dc.playback.channels = cfg.channels;
  dc.sampleRate = cfg.sample_rate;
  dc.periodSizeInFrames = cfg.period_frames;
  dc.dataCallback = data_callback;
  dc.pUserData = im;
  r = ma_device_init(&im->context, &dc, &im->device);
  if (r != MA_SUCCESS) {
    std::snprintf(err_, sizeof err_, "ma_device_init: %s", ma_result_description(r));
    ma_context_uninit(&im->context);
    std::free(im);
    return false;
  }
  mixer.init(im->device.sampleRate, im->device.playback.channels);
  std::snprintf(info_.backend, sizeof info_.backend, "%s", ma_get_backend_name(im->context.backend));
  size_t nlen = 0;
  ma_device_get_name(&im->device, ma_device_type_playback, info_.name, sizeof info_.name, &nlen);
  info_.sample_rate = im->device.sampleRate;
  info_.channels = im->device.playback.channels;
  info_.period_frames = im->device.playback.internalPeriodSizeInFrames;
  r = ma_device_start(&im->device);
  if (r != MA_SUCCESS) {
    std::snprintf(err_, sizeof err_, "ma_device_start: %s", ma_result_description(r));
    ma_device_uninit(&im->device);
    ma_context_uninit(&im->context);
    std::free(im);
    return false;
  }
  impl_ = im;
  started_ = true;
  return true;
}

void AudioDevice::shutdown() {
  if (!impl_) return;
  Impl *im = static_cast<Impl *>(impl_);
  ma_device_uninit(&im->device);
  ma_context_uninit(&im->context);
  std::free(im);
  impl_ = nullptr;
  started_ = false;
}

} // namespace tulpar::engine::audio
