// L3 AUDIO — klip yukleme: WAV/FLAC/MP3 (miniaudio decoder) -> PCM float Arena'da,
// hedef hiza/kanala donusturulmus. Sentetik sinus: test ve demo (dosya gerekmez).
#pragma once
#include "audio/mixer.hpp"
#include "core/memory/arena.hpp"

namespace tulpar::engine::audio {

bool clip_load(Arena &arena, const char *path, uint32_t out_rate, uint32_t out_channels, Clip *out, char *err, uint32_t err_n);
bool clip_sine(Arena &arena, float hz, float seconds, uint32_t rate, float amplitude, Clip *out);

} // namespace tulpar::engine::audio
