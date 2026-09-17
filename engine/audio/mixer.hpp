// L3 AUDIO — karistirici: sabit ses (voice) havuzu, oyun thread'inden kilitsiz
// SPSC komut halkasi, cihaz callback'inde render (0 ayirma, kilit yok).
// Klipler yukleme aninda PCM float'a cozulur (Arena).
//
// Faz 4 (uzamsal dilim): her ses 3B olabilir — dinleyici bazina gore mesafe
// sonumu + sabit-guc panlama + kafa golgesi (HRTF DEGIL, bkz. spatial.hpp) +
// okluzyon (DISARIDAN parametre; isin testi sim katmaninda). Ana yolda sabit
// kapasiteli DSP zinciri (dsp.hpp). Akis (stream) sonraki dilim.
#pragma once
#include <atomic>
#include <cstdint>

#include "audio/dsp.hpp"
#include "audio/spatial.hpp"

namespace tulpar::engine::audio {

struct Clip {
  const float *samples = nullptr; // interleaved, channels x frames
  uint32_t frames = 0;
  uint32_t channels = 1;          // 1 ya da 2
  uint32_t rate = 48000;
};

struct VoiceHandle {
  uint32_t id = 0; // 0 = gecersiz; (nesil << 8) | yuva
  bool valid() const { return id != 0; }
};

struct MixerStats {
  uint64_t frames_rendered = 0;   // callback'te uretilen kare (stereo cift)
  uint64_t callbacks = 0;
  uint32_t voices_active = 0;
  uint32_t commands_dropped = 0;  // halka doluydu (sayilir, sessiz degil)
  float peak = 0;                 // son render'daki en buyuk |ornek|
  uint32_t voices_spatial = 0;    // bunlarin kaci 3B
};

class Mixer {
public:
  static constexpr uint32_t kVoices = 32;
  static constexpr uint32_t kQueue = 256;
  void init(uint32_t out_rate, uint32_t out_channels) { rate_ = out_rate; channels_ = out_channels; }
  // Oyun thread'i (tek uretici):
  VoiceHandle play(const Clip *clip, float gain = 1.0f, bool loop = false);
  void stop(VoiceHandle v);
  void set_gain(VoiceHandle v, float gain);
  void stop_all();

  // --- 3B (uzamsal) yol ----------------------------------------------------
  // Dinleyici: 4 yuvali yayin (seqlock yerine sayac) — kilitsiz, beklemesiz.
  void set_listener(const Listener &l);
  Listener listener() const;
  VoiceHandle play_3d(const Clip *clip, const SpatialParams &sp, float gain = 1.0f, bool loop = false);
  void set_spatial(VoiceHandle v, const SpatialParams &sp);
  void set_voice_position(VoiceHandle v, Vec3 pos);
  // Okluzyon 0..1 — isini UST KATMAN atar (audio L3, sim L4: include yasak).
  void set_occlusion(VoiceHandle v, float occlusion);

  // --- Ana yol DSP zinciri (bos baslar; bkz. dsp.hpp) ----------------------
  DspChain &dsp() { return dsp_; }
  const DspChain &dsp() const { return dsp_; }

  // Cihaz thread'i (tek tuketici): out[frames * channels], toplama (out once sifirlanir).
  void render(float *out, uint32_t frames);
  MixerStats stats() const;
  uint32_t rate() const { return rate_; }
  uint32_t channels() const { return channels_; }

private:
  enum class Cmd : uint8_t { Play, Stop, Gain, StopAll, Play3D, Spatial, Position, Occlusion };
  struct Command {
    Cmd cmd;
    uint8_t slot;
    uint8_t loop;
    uint8_t pad;
    uint32_t gen;
    float gain;
    const Clip *clip;
    SpatialParams sp; // Play3D/Spatial: tamami; Position: sp.pos; Occlusion: sp.occlusion
  };
  struct Voice {
    const Clip *clip = nullptr;
    uint64_t pos = 0;    // kare (sabit nokta: 32.32 gerekmez, pitch 1)
    float gain = 1.0f;
    bool loop = false;
    bool active = false;
    bool spatial = false;
    uint32_t gen = 0;
    SpatialParams sp{};
    // Cihaz thread'ine ait durum: blok icinde rampalanan kazanc + golge filtresi.
    float cur_l = 0, cur_r = 0;
    float lp_l = 0, lp_r = 0;
  };
  bool push(const Command &c);
  void apply_spatial(Voice &v); // Play3D/Spatial sonrasi kazanclari tazele
  Voice voices_[kVoices];
  Command queue_[kQueue];
  std::atomic<uint32_t> head_{0}, tail_{0};
  uint32_t next_gen_[kVoices] = {};
  uint32_t alloc_cursor_ = 0;
  uint32_t rate_ = 48000, channels_ = 2;
  Listener listeners_[4];
  std::atomic<uint32_t> listener_ctr_{0};
  DspChain dsp_;
  std::atomic<uint64_t> frames_rendered_{0}, callbacks_{0};
  std::atomic<uint32_t> voices_active_{0}, dropped_{0}, voices_spatial_{0};
  std::atomic<float> peak_{0};
};

} // namespace tulpar::engine::audio
