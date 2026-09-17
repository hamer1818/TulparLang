// AUDIO — Ses DSP Zinciri (Filtreler ve Efektler).
// DEVAM_PLANI.md Faz 6 (Ileri Düzey Mimari) madde A1.
// 
// Miniaudio mikserinden once veya sonra, ornek bazli (sample-based) 
// Digital Signal Processing islemlerini (Low Pass, Delay, vs.) 
// sifir-tahsisli (zero-alloc) ve performansli bir sekilde uygulamayi saglar.
#pragma once
#include <cstdint>

namespace tulpar::engine::audio {

// DSP (Digital Signal Processing) temel dugumu. 
// Ses isleme zincirinin bir parcasi.
class DspNode {
 public:
  virtual ~DspNode() = default;

  // In-place isleme yapar.
  // samples: ses verisi (interleaved, yani L R L R L R)
  // frames: islenecek ornek (frame) sayisi
  // channels: kanal sayisi (or: 2 = stereo)
  // sample_rate: or: 44100 veya 48000
  virtual void process(float* samples, uint32_t frames, uint32_t channels, uint32_t sample_rate) = 0;
};

// Low-Pass Filter (Alcak Geciren)
// Basit 1-kutup (1-pole) IIR (Infinite Impulse Response) filtre
class LowPassFilter : public DspNode {
 public:
  void set_cutoff(float cutoff_hz) { cutoff_hz_ = cutoff_hz; }
  void process(float* samples, uint32_t frames, uint32_t channels, uint32_t sample_rate) override;

 private:
  float cutoff_hz_ = 20000.0f; 
  float prev_out_[8] = {}; // En fazla 8 kanal icin durumu tutar
};

// Delay (Gecikme)
// Basit geri-beslemeli (feedback) gecikme. Sifir-tahsisli (zero-alloc) olmasi
// icin init() sirasinda disaridan verilen tamponu (ring buffer) kullanir.
class DelayEffect : public DspNode {
 public:
  // buffer: disaridan saglanmis, ornegin Arena'dan alinmis kalici bir tampon
  void init(float* buffer, uint32_t buffer_frames);
  
  void set_params(float delay_ms, float feedback, float mix);
  void process(float* samples, uint32_t frames, uint32_t channels, uint32_t sample_rate) override;

 private:
  float* buffer_ = nullptr;
  uint32_t buffer_frames_ = 0;
  uint32_t write_idx_ = 0;

  float delay_ms_ = 250.0f;
  float feedback_ = 0.5f;
  float mix_ = 0.5f;
};

} // namespace tulpar::engine::audio
