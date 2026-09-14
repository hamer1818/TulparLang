#include "audio/mixer.hpp"

#include <cmath>
#include <cstring>

namespace tulpar::engine::audio {

bool Mixer::push(const Command &c) {
  const uint32_t h = head_.load(std::memory_order_relaxed);
  const uint32_t t = tail_.load(std::memory_order_acquire);
  if (h - t >= kQueue) { dropped_.fetch_add(1, std::memory_order_relaxed); return false; }
  queue_[h % kQueue] = c;
  head_.store(h + 1, std::memory_order_release);
  return true;
}

VoiceHandle Mixer::play(const Clip *clip, float gain, bool loop) {
  if (!clip || !clip->samples || clip->frames == 0) return VoiceHandle{};
  // Yuva secimi uretici tarafinda (yuva "gen" ile dogrulanir; tuketici eski
  // nesil komutu yok sayar). Dolu havuzda en eski yuva geri kazanilir.
  const uint32_t slot = alloc_cursor_++ % kVoices;
  const uint32_t gen = ++next_gen_[slot];
  Command c{Cmd::Play, (uint8_t)slot, (uint8_t)(loop ? 1 : 0), 0, gen, gain, clip};
  if (!push(c)) return VoiceHandle{};
  return VoiceHandle{(gen << 8) | slot};
}

void Mixer::stop(VoiceHandle v) {
  if (!v.valid()) return;
  Command c{Cmd::Stop, (uint8_t)(v.id & 0xFF), 0, 0, v.id >> 8, 0, nullptr};
  push(c);
}

void Mixer::set_gain(VoiceHandle v, float gain) {
  if (!v.valid()) return;
  Command c{Cmd::Gain, (uint8_t)(v.id & 0xFF), 0, 0, v.id >> 8, gain, nullptr};
  push(c);
}

void Mixer::stop_all() {
  Command c{Cmd::StopAll, 0, 0, 0, 0, 0, nullptr};
  push(c);
}

void Mixer::render(float *out, uint32_t frames) {
  // Komutlari tuket.
  uint32_t t = tail_.load(std::memory_order_relaxed);
  const uint32_t h = head_.load(std::memory_order_acquire);
  while (t != h) {
    const Command &c = queue_[t % kQueue];
    switch (c.cmd) {
    case Cmd::Play: {
      Voice &v = voices_[c.slot];
      v.clip = c.clip; v.pos = 0; v.gain = c.gain; v.loop = c.loop != 0; v.active = true; v.gen = c.gen;
      break;
    }
    case Cmd::Stop: if (voices_[c.slot].gen == c.gen) voices_[c.slot].active = false; break;
    case Cmd::Gain: if (voices_[c.slot].gen == c.gen) voices_[c.slot].gain = c.gain; break;
    case Cmd::StopAll: for (uint32_t i = 0; i < kVoices; i++) voices_[i].active = false; break;
    }
    t++;
  }
  tail_.store(t, std::memory_order_release);

  const uint32_t ch = channels_;
  std::memset(out, 0, sizeof(float) * frames * ch);
  uint32_t active = 0;
  float peak = 0;
  for (uint32_t vi = 0; vi < kVoices; vi++) {
    Voice &v = voices_[vi];
    if (!v.active || !v.clip) continue;
    active++;
    const Clip &cl = *v.clip;
    for (uint32_t f = 0; f < frames; f++) {
      if (v.pos >= cl.frames) {
        if (!v.loop) { v.active = false; break; }
        v.pos = 0;
      }
      const float *s = cl.samples + v.pos * cl.channels;
      if (cl.channels == 1) {
        const float m = s[0] * v.gain;
        for (uint32_t c = 0; c < ch; c++) out[f * ch + c] += m;
      } else {
        for (uint32_t c = 0; c < ch; c++) out[f * ch + c] += s[c < cl.channels ? c : 0] * v.gain;
      }
      v.pos++;
    }
  }
  // Bloktan sonra hala aktif olanlar (tam blok sinirinda biten ses sayilmaz).
  active = 0;
  for (uint32_t vi = 0; vi < kVoices; vi++) {
    Voice &v = voices_[vi];
    if (v.active && v.clip && !v.loop && v.pos >= v.clip->frames) v.active = false;
    if (v.active) active++;
  }
  for (uint32_t i = 0; i < frames * ch; i++) {
    float x = out[i];
    if (x > 1.0f) x = 1.0f; else if (x < -1.0f) x = -1.0f; // sert sinir (limiter sonraki dilim)
    out[i] = x;
    const float a = x < 0 ? -x : x;
    if (a > peak) peak = a;
  }
  frames_rendered_.fetch_add(frames, std::memory_order_relaxed);
  callbacks_.fetch_add(1, std::memory_order_relaxed);
  voices_active_.store(active, std::memory_order_relaxed);
  peak_.store(peak, std::memory_order_relaxed);
}

MixerStats Mixer::stats() const {
  MixerStats s;
  s.frames_rendered = frames_rendered_.load(std::memory_order_relaxed);
  s.callbacks = callbacks_.load(std::memory_order_relaxed);
  s.voices_active = voices_active_.load(std::memory_order_relaxed);
  s.commands_dropped = dropped_.load(std::memory_order_relaxed);
  s.peak = peak_.load(std::memory_order_relaxed);
  return s;
}

} // namespace tulpar::engine::audio
