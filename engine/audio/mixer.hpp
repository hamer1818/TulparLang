// L3 AUDIO — karistirici: sabit ses (voice) havuzu, oyun thread'inden kilitsiz
// SPSC komut halkasi, cihaz callback'inde render (0 ayirma, kilit yok).
// Klipler yukleme aninda PCM float'a cozulur (Arena). Uzamsal ses/HRTF (Steam
// Audio) ve akis (stream) sonraki dilim; plan Faz 4.
#pragma once
#include <atomic>
#include <cstdint>

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
  // Cihaz thread'i (tek tuketici): out[frames * channels], toplama (out once sifirlanir).
  void render(float *out, uint32_t frames);
  MixerStats stats() const;
  uint32_t rate() const { return rate_; }
  uint32_t channels() const { return channels_; }

private:
  enum class Cmd : uint8_t { Play, Stop, Gain, StopAll };
  struct Command {
    Cmd cmd;
    uint8_t slot;
    uint8_t loop;
    uint8_t pad;
    uint32_t gen;
    float gain;
    const Clip *clip;
  };
  struct Voice {
    const Clip *clip = nullptr;
    uint64_t pos = 0;    // kare (sabit nokta: 32.32 gerekmez, pitch 1)
    float gain = 1.0f;
    bool loop = false;
    bool active = false;
    uint32_t gen = 0;
  };
  bool push(const Command &c);
  Voice voices_[kVoices];
  Command queue_[kQueue];
  std::atomic<uint32_t> head_{0}, tail_{0};
  uint32_t next_gen_[kVoices] = {};
  uint32_t alloc_cursor_ = 0;
  uint32_t rate_ = 48000, channels_ = 2;
  std::atomic<uint64_t> frames_rendered_{0}, callbacks_{0};
  std::atomic<uint32_t> voices_active_{0}, dropped_{0};
  std::atomic<float> peak_{0};
};

} // namespace tulpar::engine::audio
