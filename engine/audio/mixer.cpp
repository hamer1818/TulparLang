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
  Command c{Cmd::Play, (uint8_t)slot, (uint8_t)(loop ? 1 : 0), 0, gen, gain, clip, SpatialParams{}};
  if (!push(c)) return VoiceHandle{};
  return VoiceHandle{(gen << 8) | slot};
}

VoiceHandle Mixer::play_3d(const Clip *clip, const SpatialParams &sp, float gain, bool loop) {
  if (!clip || !clip->samples || clip->frames == 0) return VoiceHandle{};
  const uint32_t slot = alloc_cursor_++ % kVoices;
  const uint32_t gen = ++next_gen_[slot];
  Command c{Cmd::Play3D, (uint8_t)slot, (uint8_t)(loop ? 1 : 0), 0, gen, gain, clip, sp};
  if (!push(c)) return VoiceHandle{};
  return VoiceHandle{(gen << 8) | slot};
}

void Mixer::stop(VoiceHandle v) {
  if (!v.valid()) return;
  Command c{Cmd::Stop, (uint8_t)(v.id & 0xFF), 0, 0, v.id >> 8, 0, nullptr, SpatialParams{}};
  push(c);
}

void Mixer::set_gain(VoiceHandle v, float gain) {
  if (!v.valid()) return;
  Command c{Cmd::Gain, (uint8_t)(v.id & 0xFF), 0, 0, v.id >> 8, gain, nullptr, SpatialParams{}};
  push(c);
}

void Mixer::set_spatial(VoiceHandle v, const SpatialParams &sp) {
  if (!v.valid()) return;
  Command c{Cmd::Spatial, (uint8_t)(v.id & 0xFF), 0, 0, v.id >> 8, 0, nullptr, sp};
  push(c);
}

void Mixer::set_voice_position(VoiceHandle v, Vec3 pos) {
  if (!v.valid()) return;
  SpatialParams sp;
  sp.pos = pos;
  Command c{Cmd::Position, (uint8_t)(v.id & 0xFF), 0, 0, v.id >> 8, 0, nullptr, sp};
  push(c);
}

void Mixer::set_occlusion(VoiceHandle v, float occlusion) {
  if (!v.valid()) return;
  SpatialParams sp;
  sp.occlusion = clampf(occlusion, 0.0f, 1.0f);
  Command c{Cmd::Occlusion, (uint8_t)(v.id & 0xFF), 0, 0, v.id >> 8, 0, nullptr, sp};
  push(c);
}

void Mixer::stop_all() {
  Command c{Cmd::StopAll, 0, 0, 0, 0, 0, nullptr, SpatialParams{}};
  push(c);
}

// Dinleyici yayini: 4 yuvali halka + tek atomik sayac. Uretici bir sonraki
// yuvaya yazip sayaci artirir; tuketici sayaci okuyup o yuvayi okur. Yazicinin
// okuyucunun yuvasini ezmesi icin 4 yayin gerekir (kare basi 1 yayin, callback
// basi 1 okuma) — kilit yok, bekleme yok.
void Mixer::set_listener(const Listener &l) {
  const uint32_t c = listener_ctr_.load(std::memory_order_relaxed);
  listeners_[(c + 1) & 3] = l;
  listener_ctr_.store(c + 1, std::memory_order_release);
}

Listener Mixer::listener() const {
  const uint32_t c = listener_ctr_.load(std::memory_order_acquire);
  return listeners_[c & 3];
}

void Mixer::apply_spatial(Voice &v) {
  const uint32_t c = listener_ctr_.load(std::memory_order_acquire);
  const SpatialGains g = spatial_compute(listeners_[c & 3], v.sp);
  v.cur_l = g.gain_l * v.gain;
  v.cur_r = g.gain_r * v.gain;
}

void Mixer::render(float *out, uint32_t frames) {
  // Komutlari tuket.
  uint32_t t = tail_.load(std::memory_order_relaxed);
  const uint32_t h = head_.load(std::memory_order_acquire);
  while (t != h) {
    const Command &c = queue_[t % kQueue];
    switch (c.cmd) {
    case Cmd::Play:
    case Cmd::Play3D: {
      Voice &v = voices_[c.slot];
      v.clip = c.clip; v.pos = 0; v.gain = c.gain; v.loop = c.loop != 0; v.active = true; v.gen = c.gen;
      v.spatial = (c.cmd == Cmd::Play3D);
      v.sp = c.sp;
      v.lp_l = 0; v.lp_r = 0;
      // Baslangicta rampa yok: hedef kazanc dogrudan (aksi halde ilk blok
      // sifirdan rampalanir ve analitik kapi yaniltir).
      if (v.spatial) apply_spatial(v); else { v.cur_l = v.gain; v.cur_r = v.gain; }
      break;
    }
    case Cmd::Stop: if (voices_[c.slot].gen == c.gen) voices_[c.slot].active = false; break;
    case Cmd::Gain:
      if (voices_[c.slot].gen == c.gen) voices_[c.slot].gain = c.gain;
      break;
    case Cmd::Spatial:
      if (voices_[c.slot].gen == c.gen) { voices_[c.slot].sp = c.sp; voices_[c.slot].spatial = true; }
      break;
    case Cmd::Position:
      if (voices_[c.slot].gen == c.gen) { voices_[c.slot].sp.pos = c.sp.pos; voices_[c.slot].spatial = true; }
      break;
    case Cmd::Occlusion:
      if (voices_[c.slot].gen == c.gen) { voices_[c.slot].sp.occlusion = c.sp.occlusion; voices_[c.slot].spatial = true; }
      break;
    case Cmd::StopAll: for (uint32_t i = 0; i < kVoices; i++) voices_[i].active = false; break;
    }
    t++;
  }
  tail_.store(t, std::memory_order_release);

  const uint32_t ch = channels_;
  std::memset(out, 0, sizeof(float) * frames * ch);
  // Dinleyiciyi blok basinda BIR kez oku (blok icinde sabit).
  const uint32_t lc = listener_ctr_.load(std::memory_order_acquire);
  const Listener lst = listeners_[lc & 3];
  uint32_t active = 0, spatial_n = 0;
  float peak = 0;
  for (uint32_t vi = 0; vi < kVoices; vi++) {
    Voice &v = voices_[vi];
    if (!v.active || !v.clip) continue;
    active++;
    const Clip &cl = *v.clip;
    if (v.spatial && ch >= 2) {
      spatial_n++;
      const SpatialGains g = spatial_compute(lst, v.sp);
      const float tl = g.gain_l * v.gain, tr = g.gain_r * v.gain;
      // Kazanc blok boyunca dogrusal rampalanir (fermuar gurultusu olmasin);
      // filtre katsayisi blok basinda yerine oturur.
      const float dl = frames ? (tl - v.cur_l) / (float)frames : 0.0f;
      const float dr = frames ? (tr - v.cur_r) / (float)frames : 0.0f;
      const float cl_coef = shadow_coeff(g.shadow_l, rate_);
      const float cr_coef = shadow_coeff(g.shadow_r, rate_);
      float gl = v.cur_l, gr = v.cur_r;
      for (uint32_t f = 0; f < frames; f++) {
        if (v.pos >= cl.frames) {
          if (!v.loop) { v.active = false; break; }
          v.pos = 0;
        }
        const float *s = cl.samples + v.pos * cl.channels;
        const float mono = cl.channels == 1 ? s[0] : 0.5f * (s[0] + s[1]);
        float L = mono * gl, R = mono * gr;
        if (cl_coef < 1.0f) { v.lp_l += cl_coef * (L - v.lp_l); L = v.lp_l; } else { v.lp_l = L; }
        if (cr_coef < 1.0f) { v.lp_r += cr_coef * (R - v.lp_r); R = v.lp_r; } else { v.lp_r = R; }
        out[f * ch + 0] += L;
        out[f * ch + 1] += R;
        for (uint32_t c = 2; c < ch; c++) out[f * ch + c] += 0.5f * (L + R);
        gl += dl; gr += dr;
        v.pos++;
      }
      v.cur_l = tl; v.cur_r = tr;
      continue;
    }
    if (v.spatial) {
      // Mono cikis: panlama anlamsiz, sabit-guc toplam kazanc = sonum.
      spatial_n++;
      const SpatialGains g = spatial_compute(lst, v.sp);
      const float gm = std::sqrt(g.gain_l * g.gain_l + g.gain_r * g.gain_r) * v.gain;
      const float sh = g.shadow_l > g.shadow_r ? g.shadow_l : g.shadow_r;
      const float coef = shadow_coeff(sh, rate_);
      for (uint32_t f = 0; f < frames; f++) {
        if (v.pos >= cl.frames) {
          if (!v.loop) { v.active = false; break; }
          v.pos = 0;
        }
        const float *s = cl.samples + v.pos * cl.channels;
        const float mono = cl.channels == 1 ? s[0] : 0.5f * (s[0] + s[1]);
        float m = mono * gm;
        if (coef < 1.0f) { v.lp_l += coef * (m - v.lp_l); m = v.lp_l; } else { v.lp_l = m; }
        for (uint32_t c = 0; c < ch; c++) out[f * ch + c] += m;
        v.pos++;
      }
      v.cur_l = gm; v.cur_r = gm;
      continue;
    }
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
  spatial_n = 0;
  for (uint32_t vi = 0; vi < kVoices; vi++) {
    Voice &v = voices_[vi];
    if (v.active && v.clip && !v.loop && v.pos >= v.clip->frames) v.active = false;
    if (v.active) { active++; if (v.spatial) spatial_n++; }
  }
  // Ana yol DSP zinciri (bos ise dokunmaz) — sert sinirdan ONCE.
  dsp_.process(out, frames, ch, rate_);
  for (uint32_t i = 0; i < frames * ch; i++) {
    float x = out[i];
    if (x > 1.0f) x = 1.0f; else if (x < -1.0f) x = -1.0f; // son emniyet siniri (limiter DSP dugumu)
    out[i] = x;
    const float a = x < 0 ? -x : x;
    if (a > peak) peak = a;
  }
  frames_rendered_.fetch_add(frames, std::memory_order_relaxed);
  callbacks_.fetch_add(1, std::memory_order_relaxed);
  voices_active_.store(active, std::memory_order_relaxed);
  voices_spatial_.store(spatial_n, std::memory_order_relaxed);
  peak_.store(peak, std::memory_order_relaxed);
}

MixerStats Mixer::stats() const {
  MixerStats s;
  s.frames_rendered = frames_rendered_.load(std::memory_order_relaxed);
  s.callbacks = callbacks_.load(std::memory_order_relaxed);
  s.voices_active = voices_active_.load(std::memory_order_relaxed);
  s.commands_dropped = dropped_.load(std::memory_order_relaxed);
  s.peak = peak_.load(std::memory_order_relaxed);
  s.voices_spatial = voices_spatial_.load(std::memory_order_relaxed);
  return s;
}

} // namespace tulpar::engine::audio
