// Ses (Faz 4 ilk dilim): karistirici belirlenimli ve olculebilir (RMS, tepe,
// bitis), null arka ucta cihaz callback'i gercekten kosuyor (kare sayisi
// artar; pozitif kontrol: sessiz karistiricinin tepesi 0), gercek cihaz varsa
// acilir (yoksa ATLANDI, gorunur).
#include <cmath>
#include <cstdio>

#include "audio/clip.hpp"
#include "audio/device.hpp"
#include "audio/mixer.hpp"
#include "core/memory/arena.hpp"
#include "platform/thread.hpp"
#include "tests/test.hpp"

using namespace tulpar::engine;
using namespace tulpar::engine::test;

ENGINE_TEST(audio_mixer_levels_are_deterministic) {
  static SystemArena sys;
  if (!sys.reserve(8u << 20, "audio")) { CHECK(false); return; }
  audio::Clip a, b;
  CHECK(audio::clip_sine(sys, 440.0f, 0.5f, 48000, 0.5f, &a));
  CHECK(audio::clip_sine(sys, 880.0f, 0.1f, 48000, 0.25f, &b));
  audio::Mixer mx;
  mx.init(48000, 2);
  static float out[4800 * 2];
  audio::VoiceHandle va = mx.play(&a, 1.0f, false);
  audio::VoiceHandle vb = mx.play(&b, 1.0f, false);
  CHECK(va.valid() && vb.valid());
  mx.render(out, 4800); // 100 ms: a + b (b tam bu blokta biter: 4800 kare = 0.1 s)
  double sum = 0;
  for (uint32_t i = 0; i < 4800; i++) sum += (double)out[i * 2] * out[i * 2];
  const double rms = std::sqrt(sum / 4800.0);
  audio::MixerStats st = mx.stats();
  std::printf("    [bilgi] 2 ses (0.5 + 0.25 genlik): RMS %.4f (beklenen ~%.4f), tepe %.3f, aktif %u, kare %llu\n", rms,
              std::sqrt(0.125 + 0.03125), st.peak, st.voices_active, (unsigned long long)st.frames_rendered);
  CHECK(std::fabs(rms - std::sqrt(0.125 + 0.03125)) < 0.01);
  CHECK(st.peak > 0.5f && st.peak <= 0.75f);
  CHECK(st.frames_rendered == 4800);
  mx.render(out, 4800); // b bitti: yalniz a
  st = mx.stats();
  CHECK(st.voices_active == 1);
  mx.stop(va);
  mx.render(out, 480);
  st = mx.stats();
  float peak_after = st.peak;
  CHECK(st.voices_active == 0 && peak_after == 0.0f);
  // Kanal kopyasi: mono klip her iki kanala.
  mx.play(&a, 0.5f, true);
  mx.render(out, 480);
  bool stereo_same = true;
  for (uint32_t i = 0; i < 480; i++) if (out[i * 2] != out[i * 2 + 1]) stereo_same = false;
  CHECK(stereo_same);
  // Sert sinir: 8 tam genlikli ses toplami [-1,1] disina cikmaz.
  audio::Clip loud;
  CHECK(audio::clip_sine(sys, 100.0f, 0.05f, 48000, 1.0f, &loud));
  mx.stop_all();
  for (int i = 0; i < 8; i++) mx.play(&loud, 1.0f, true);
  mx.render(out, 480);
  st = mx.stats();
  CHECK(st.peak <= 1.0f && st.voices_active == 8);
}

ENGINE_TEST(audio_null_device_runs_callbacks) {
  static SystemArena sys;
  if (!sys.reserve(8u << 20, "audio_dev")) { CHECK(false); return; }
  audio::Mixer mx;
  audio::AudioDevice dev;
  audio::DeviceConfig cfg;
  cfg.null_backend = true;
  bool ok = dev.init(mx, cfg);
  if (!ok) std::printf("    [bilgi] null cihaz: %s\n", dev.last_error());
  CHECK(ok);
  if (!ok) return;
  audio::Clip a;
  CHECK(audio::clip_sine(sys, 440.0f, 2.0f, mx.rate(), 0.5f, &a));
  // POZITIF KONTROL: ses yokken callback kosar ama tepe 0.
  platform::thread_sleep_us(150000);
  audio::MixerStats st0 = mx.stats();
  mx.play(&a, 1.0f, true);
  platform::thread_sleep_us(250000);
  audio::MixerStats st1 = mx.stats();
  std::printf("    [bilgi] null cihaz %s %u Hz %u kanal, periyot %u kare: sessiz %llu callback tepe %.2f -> sesli +%llu callback tepe %.2f\n",
              dev.info().backend, dev.info().sample_rate, dev.info().channels, dev.info().period_frames,
              (unsigned long long)st0.callbacks, st0.peak, (unsigned long long)(st1.callbacks - st0.callbacks), st1.peak);
  CHECK(st0.callbacks > 0 && st0.peak == 0.0f);
  CHECK(st1.callbacks > st0.callbacks && st1.peak > 0.4f);
  dev.shutdown();
}

ENGINE_TEST(audio_default_device_opens) {
  static SystemArena sys;
  if (!sys.reserve(8u << 20, "audio_real")) { CHECK(false); return; }
  audio::Mixer mx;
  audio::AudioDevice dev;
  audio::DeviceConfig cfg;
  if (!dev.init(mx, cfg)) {
    std::printf("    [bilgi] ses cihazi: %s\n", dev.last_error());
    skip("ses cihazi yok (arka uc acilamadi) — gercek cihaz yolu kosmadi");
    return;
  }
  audio::Clip a;
  CHECK(audio::clip_sine(sys, 440.0f, 0.3f, mx.rate(), 0.2f, &a));
  mx.play(&a, 1.0f, false);
  platform::thread_sleep_us(200000);
  audio::MixerStats st = mx.stats();
  std::printf("    [bilgi] cihaz %s '%s' %u Hz %u kanal periyot %u: %llu callback, %llu kare, tepe %.2f\n", dev.info().backend,
              dev.info().name, dev.info().sample_rate, dev.info().channels, dev.info().period_frames, (unsigned long long)st.callbacks,
              (unsigned long long)st.frames_rendered, st.peak);
  CHECK(st.callbacks > 0);
  dev.shutdown();
}
