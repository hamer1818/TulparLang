#include "audio/clip.hpp"

#include <cmath>
#include <cstdio>

#include <miniaudio.h>

namespace tulpar::engine::audio {

bool clip_load(Arena &arena, const char *path, uint32_t out_rate, uint32_t out_channels, Clip *out, char *err, uint32_t err_n) {
  *out = Clip{};
  ma_decoder_config dc = ma_decoder_config_init(ma_format_f32, out_channels, out_rate);
  ma_decoder dec;
  ma_result r = ma_decoder_init_file(path, &dc, &dec);
  if (r != MA_SUCCESS) { if (err) std::snprintf(err, err_n, "decoder: %s (%s)", ma_result_description(r), path); return false; }
  ma_uint64 len = 0;
  if (ma_decoder_get_length_in_pcm_frames(&dec, &len) != MA_SUCCESS || len == 0) {
    ma_decoder_uninit(&dec);
    if (err) std::snprintf(err, err_n, "uzunluk bilinmiyor (akis kaynaklari sonraki dilim): %s", path);
    return false;
  }
  float *buf = arena.alloc_array<float>((uint32_t)(len * out_channels));
  if (!buf) { ma_decoder_uninit(&dec); if (err) std::snprintf(err, err_n, "arena dolu"); return false; }
  ma_uint64 got = 0;
  r = ma_decoder_read_pcm_frames(&dec, buf, len, &got);
  ma_decoder_uninit(&dec);
  if (r != MA_SUCCESS && r != MA_AT_END) { if (err) std::snprintf(err, err_n, "okuma: %s", ma_result_description(r)); return false; }
  out->samples = buf; out->frames = (uint32_t)got; out->channels = out_channels; out->rate = out_rate;
  return true;
}

bool clip_sine(Arena &arena, float hz, float seconds, uint32_t rate, float amplitude, Clip *out) {
  *out = Clip{};
  const uint32_t n = (uint32_t)(seconds * (float)rate);
  float *buf = arena.alloc_array<float>(n ? n : 1);
  if (!buf) return false;
  const float w = 2.0f * 3.14159265358979f * hz / (float)rate;
  for (uint32_t i = 0; i < n; i++) buf[i] = amplitude * std::sin(w * (float)i);
  out->samples = buf; out->frames = n; out->channels = 1; out->rate = rate;
  return true;
}

} // namespace tulpar::engine::audio
