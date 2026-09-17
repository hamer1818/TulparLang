#include "tests/test.hpp"
#include "audio/dsp.hpp"

#include <cmath>
#include <cstring>

using namespace tulpar::engine::audio;

// L1 kurali (tools/layer_check.py): STL konteyner YOK -- testlerde DE yok,
// yoksa "sifir tahsis" iddiasi test tarafindan gizlenir. Tamponlar sabit
// boyutlu ve dosya kapsaminda: 1 sn'lik gecikme tamponu (384 KB) yigina
// sigmaz, bu yuzden BSS'te durur. Her test kendi tamponunu SIFIRLAR.
namespace {
constexpr uint32_t kSR = 48000;
constexpr uint32_t kCh = 2;
float g_samples[1000 * kCh];
float g_samples2[1000 * kCh];
float g_delay_buf[48000 * kCh];
} // namespace

ENGINE_TEST(engine_test_dsp_lowpass) {
  LowPassFilter lpf;
  lpf.set_cutoff(1000.0f); // 1 kHz cutoff

  const uint32_t sample_rate = kSR;
  const uint32_t frames = 480; // 10 ms
  const uint32_t channels = kCh; // Stereo

  float *samples = g_samples;
  std::memset(samples, 0, sizeof(float) * frames * channels);
  
  // Ornekleme (1 frame = 1, Digerleri = 0 -> Impulse Response)
  samples[0] = 1.0f; // L
  samples[1] = 1.0f; // R
  
  lpf.process(samples, frames, channels, sample_rate);
  
  // Eger low pass calisiyorsa, ilk andaki ani vurus (impulse) yayilmalidir
  // Yani samples[0] tam 1.0f degil daha kucuk bir deger olmalidir
  // ve ondan sonraki framelerde de ses sönerek devam etmelidir.
  CHECK(samples[0] < 1.0f); 
  CHECK(samples[0] > 0.0f);
  CHECK(samples[2] > 0.0f); // Bir sonraki frame L kanali
  CHECK(samples[2] < samples[0]); // Azalarak bitmeli
}

ENGINE_TEST(engine_test_dsp_delay) {
  DelayEffect delay;
  
  const uint32_t sample_rate = kSR;
  const uint32_t channels = kCh;

  // 1 saniyelik buffer
  const uint32_t buffer_frames = kSR * channels;
  float *buffer = g_delay_buf;
  std::memset(buffer, 0, sizeof(g_delay_buf));

  delay.init(buffer, buffer_frames);
  delay.set_params(10.0f, 0.5f, 0.5f); // 10ms gecikme, %50 feedback, %50 mix
  
  // 10ms karsiligi ornek sayisi = 480 (her kanal icin ayri oldugu icin 480*2 = 960 interlaced)
  const uint32_t frames = 1000; // 10ms'den (480 frame) buyuk bir sure verelim
  float *samples = g_samples;
  std::memset(samples, 0, sizeof(g_samples));
  
  samples[0] = 1.0f; // L kanali baslangic vurusu (Impulse)
  
  delay.process(samples, frames, channels, sample_rate);
  
  // %50 mix yapildigi icin orjinal ses ilk ciktida %50 zayiflar
  CHECK(samples[0] == 0.5f); // Dry %50 = 0.5
  
  // 10ms sonra (480 frame -> index: 480*2 = 960) yanki gelmeli
  // Ilk yanki: Wet = 1.0 * mix(0.5) = 0.5.
  CHECK(samples[960] == 0.5f); 
  
  // 2. yanki testi (Feedback) icin bir tur daha islem yapalim
  // Yeni bos sample'lar
  float *samples2 = g_samples2;
  std::memset(samples2, 0, sizeof(g_samples2));
  delay.process(samples2, frames, channels, sample_rate);
  
  // Ilk turun 960. indeksinden itibaren gecikme 480 frame sonra (20ms) 
  // Feedback %50 oldugundan 0.5 * 0.5 = 0.25 beklenir
  CHECK(samples2[960] == 0.25f); 
}
