#include "audio/dsp.hpp"

#include <cmath>
#include <algorithm>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace tulpar::engine::audio {

void LowPassFilter::process(float* samples, uint32_t frames, uint32_t channels, uint32_t sample_rate) {
  if (channels > 8) return; // Guvenlik limiti
  
  // Cutoff hesaplama (RC filter)
  float dt = 1.0f / static_cast<float>(sample_rate);
  float rc = 1.0f / (cutoff_hz_ * 2.0f * static_cast<float>(M_PI));
  float alpha = dt / (rc + dt);

  // Eger cutoff cok yuksekse (Nyquist'e yakin), alpha yaklasik 1 olur, filtre devre disi kalir.
  if (alpha >= 1.0f) {
    alpha = 1.0f;
  }

  for (uint32_t f = 0; f < frames; ++f) {
    for (uint32_t c = 0; c < channels; ++c) {
      uint32_t idx = f * channels + c;
      float in = samples[idx];
      // 1-pole IIR (Infinite Impulse Response)
      float out = prev_out_[c] + alpha * (in - prev_out_[c]);
      samples[idx] = out;
      prev_out_[c] = out;
    }
  }
}

void DelayEffect::init(float* buffer, uint32_t buffer_frames) {
  buffer_ = buffer;
  buffer_frames_ = buffer_frames;
  write_idx_ = 0;
  
  // Buffer'i temizle (eski ses kalintilarini onlemek icin)
  if (buffer_) {
    for (uint32_t i = 0; i < buffer_frames_; i++) {
      buffer_[i] = 0.0f;
    }
  }
}

void DelayEffect::set_params(float delay_ms, float feedback, float mix) {
  delay_ms_ = std::max(0.0f, delay_ms);
  feedback_ = std::clamp(feedback, 0.0f, 0.99f); // < 1.0 yankinin sonsuza ulasip patlamasini onler
  mix_ = std::clamp(mix, 0.0f, 1.0f);
}

void DelayEffect::process(float* samples, uint32_t frames, uint32_t channels, uint32_t sample_rate) {
  if (!buffer_ || buffer_frames_ == 0) return;

  uint32_t delay_samples = static_cast<uint32_t>((delay_ms_ / 1000.0f) * sample_rate);
  
  for (uint32_t f = 0; f < frames; ++f) {
    for (uint32_t c = 0; c < channels; ++c) {
      uint32_t idx = f * channels + c;
      float in = samples[idx];
      
      // Kanal bazli buffer indeksi
      uint32_t b_idx = (write_idx_ + c) % buffer_frames_;
      
      // Okuma indeksi (gecmisteki nokta). Kac frame geride = delay_samples, 
      // bunu kanal sayisiyla carparak (cunku buffer da interleaved) dogru indeksi buluruz.
      uint32_t r_idx = (b_idx + buffer_frames_ - (delay_samples * channels) % buffer_frames_) % buffer_frames_;
      
      float delayed_sample = buffer_[r_idx];
      
      // Wet + Dry karisimi
      float out = (in * (1.0f - mix_)) + (delayed_sample * mix_);
      
      // Feedback ile birlikte buffera yaz (yeni ses + eskisinin ufalmis hali)
      buffer_[b_idx] = in + (delayed_sample * feedback_);
      
      samples[idx] = out;
    }
    
    // Her frame ilerlediginde kanal sayisi kadar buffer index'ini artir
    write_idx_ = (write_idx_ + channels) % buffer_frames_;
  }
}

} // namespace tulpar::engine::audio
