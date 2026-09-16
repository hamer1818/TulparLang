// Ses (Faz 4 ilk dilim): karistirici belirlenimli ve olculebilir (RMS, tepe,
// bitis), null arka ucta cihaz callback'i gercekten kosuyor (kare sayisi
// artar; pozitif kontrol: sessiz karistiricinin tepesi 0), gercek cihaz varsa
// acilir (yoksa ATLANDI, gorunur).
#include <cmath>
#include <cstdio>
#include <cstdlib>

#include "audio/clip.hpp"
#include "audio/device.hpp"
#include "audio/dsp.hpp"
#include "audio/mixer.hpp"
#include "audio/spatial.hpp"
#include "core/memory/alloc_gate.hpp"
#include "core/memory/arena.hpp"
#include "platform/thread.hpp"
#include "sim/physics.hpp" // okluzyon isini: testler her katmani gorebilir
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
  // GENLIK BILEREK DUYULMAZ.
  //
  // Bu kapi GERCEK ses cihazini aciyor (Linux'ta PulseAudio) — null arka ucla
  // test etmek "ses calisiyor" demez, o yuzden gercek yol kosmali. Ama genlik
  // uzun sure 0.2 idi ve 440 Hz (la notasi) 0.3 saniye CALINIYORDU: takimi her
  // kosturan kisi kulakliginda bir bip duyuyordu (kullanici 2026-09-16'da
  // bildirdi; ajanlar da paralel kosturunca rastgele araliklarla geliyordu).
  //
  // Genlik bu kapinin OLCTUGU sey degil: iddialar "cihaz acildi + geri cagrimlar
  // akti + karistirici gercekten sinyal uretti". 0.0005 (~-66 dBFS) bunlarin
  // ucunu de aynen saglar, duyulmaz. SIFIR YAPILMADI: sifir genlikte tepe de 0
  // olurdu ve "karistirici sayisal sessizlik basiyor" durumu ile "karistirici
  // dogru calisiyor" durumu AYIRT EDILEMEZDI — kapi kor kalirdi.
  //
  // Duymak isteyen: TULPAR_ENGINE_TEST_AUDIBLE=1
  const char *audible = std::getenv("TULPAR_ENGINE_TEST_AUDIBLE");
  const float genlik = (audible && *audible && *audible != '0') ? 0.2f : 0.0005f;
  audio::Clip a;
  CHECK(audio::clip_sine(sys, 440.0f, 0.3f, mx.rate(), genlik, &a));
  // DONGULU CALIYOR — ve bu bir zevk meselesi degil, YARISI kapatiyor.
  //
  // `MixerStats::peak` SON render cagrisinin tepesidir (kumulatif degil,
  // mixer.hpp). Klip dongusuz calinca "olcum aninda klip hala caliyor mu"
  // sorusu cihazin ne kadar ONDEN tampon doldurduguna baglanir. CI macOS'un
  // sanal ses cihazi GERCEK ZAMANDAN HIZLI cekiyor: 200 ms uykuda 33 callback
  // x 480 = 15 840 kare (= 330 ms ses) render etti, yani 0,3 s'lik klip bitti
  // ve son render SESSIZDI -> tepe 0,00000 ve kapi kirmizi (2026-09-16).
  // Ayni kapi bir onceki kosumda 22 callback (220 ms) ile tepe 0,00050 verip
  // GECMISTI — yani "bazen dusen" bir kapiydi ve sebebi gurultu degil yaristi.
  //
  // Donguluyken son render her zaman sinyal tasir; cihazin hizi olcumu
  // etkilemez. Olcumden sonra susturuluyor.
  mx.play(&a, 1.0f, true);
  platform::thread_sleep_us(200000);
  audio::MixerStats st = mx.stats();
  mx.stop_all();
  std::printf("    [bilgi] cihaz %s '%s' %u Hz %u kanal periyot %u: %llu callback, %llu kare, tepe %.5f (genlik %.4f%s)\n",
              dev.info().backend, dev.info().name, dev.info().sample_rate, dev.info().channels, dev.info().period_frames,
              (unsigned long long)st.callbacks, (unsigned long long)st.frames_rendered, st.peak, genlik,
              (audible && *audible && *audible != '0') ? ", DUYULUR" : ", duyulmaz");
  CHECK(st.callbacks > 0);
  // Tepe > 0: karistirici sayisal sessizlik degil GERCEK sinyal uretti.
  // Genlik dusurulunce bu iddia ONEM KAZANDI — yoksa "sessiz cal, yesil gec"
  // yolu acik kalirdi.
  CHECK(st.peak > 0.0f);
  dev.shutdown();
}

// ============================================================================
// Faz 4 — uzamsal ses / DSP / okluzyon kapilari. Hepsi ANALITIK (beklenen deger
// hesaplanabilir) ve hepsinin POZITIF KONTROLU var: ayni yol, tek degisken
// kapali -> etki KAYBOLMALI. Tek degiskenli olcum icin ilgisiz ozellikler
// (sonum/panlama/golge) kapatiliyor.
// ============================================================================
namespace {
constexpr uint32_t kRate = 48000;
constexpr uint32_t kBlock = 4800; // 100 ms
float g_out[kBlock * 2];

double chan_rms(const float *buf, uint32_t frames, uint32_t ch, uint32_t c) {
  double s = 0;
  for (uint32_t f = 0; f < frames; f++) { const double v = buf[f * ch + c]; s += v * v; }
  return std::sqrt(s / (double)frames);
}
double buf_peak(const float *buf, uint32_t n) {
  double p = 0;
  for (uint32_t i = 0; i < n; i++) { const double a = buf[i] < 0 ? -buf[i] : buf[i]; if (a > p) p = a; }
  return p;
}
double db(double ratio) { return 20.0 * std::log10(ratio > 1e-12 ? ratio : 1e-12); }

// Bir 3B sesi baslatir, bir blok isinma (tek kutup gecici rejimi + rampa),
// sonraki bloktan kanal RMS'lerini olcer.
void render_spatial(audio::Mixer &mx, const audio::Clip &clip, const audio::SpatialParams &sp, double *l, double *r) {
  mx.stop_all();
  mx.render(g_out, kBlock);
  mx.play_3d(&clip, sp, 1.0f, true);
  mx.render(g_out, kBlock); // isinma
  mx.render(g_out, kBlock); // olcum
  if (l) *l = chan_rms(g_out, kBlock, 2, 0);
  if (r) *r = chan_rms(g_out, kBlock, 2, 1);
}
} // namespace

// KAPI: mesafe iki katina cikinca genlik yariya (-6 dB) — ters-kare enerji.
// KONTROL: distance_attenuation kapaliyken ayni iki mesafe ayni duzeyi verir.
ENGINE_TEST(audio_spatial_distance_attenuation_is_inverse_square) {
  static SystemArena sys;
  if (!sys.reserve(8u << 20, "audio_dist")) { CHECK(false); return; }
  audio::Clip sine;
  CHECK(audio::clip_sine(sys, 440.0f, 1.0f, kRate, 0.5f, &sine));
  audio::Mixer mx;
  mx.init(kRate, 2);
  audio::Listener lis; // konum 0, bakis -Z, yukari +Y
  mx.set_listener(lis);

  audio::SpatialParams sp;
  sp.min_distance = 1.0f;
  sp.max_distance = 1000.0f;
  sp.head_shadow = false; // tek degisken: sonum
  double l4 = 0, r4 = 0, l8 = 0, r8 = 0;
  sp.pos = Vec3{0, 0, -4};
  render_spatial(mx, sine, sp, &l4, &r4);
  sp.pos = Vec3{0, 0, -8};
  render_spatial(mx, sine, sp, &l8, &r8);
  const double ratio = l8 / (l4 > 0 ? l4 : 1e-9);
  std::printf("    [bilgi] sonum: d=4 RMS %.5f -> d=8 RMS %.5f = %.2f dB (beklenen -6.02 dB)\n", l4, l8, db(ratio));
  CHECK(l4 > 0.01);
  CHECK(std::fabs(db(ratio) + 6.02) < 0.3); // +-0.3 dB

  // POZITIF KONTROL: sonum kapali -> mesafe duzeyi DEGISTIRMEZ.
  sp.distance_attenuation = false;
  double c4 = 0, c8 = 0;
  sp.pos = Vec3{0, 0, -4};
  render_spatial(mx, sine, sp, &c4, nullptr);
  sp.pos = Vec3{0, 0, -8};
  render_spatial(mx, sine, sp, &c8, nullptr);
  std::printf("    [bilgi] kontrol (sonum kapali): d=4 %.5f, d=8 %.5f = %.3f dB (beklenen 0)\n", c4, c8,
              db(c8 / (c4 > 0 ? c4 : 1e-9)));
  CHECK(c4 > 0.01 && std::fabs(db(c8 / (c4 > 0 ? c4 : 1e-9))) < 0.05);

  // Pencere: max_distance'ta ve otesinde sessiz (klik olmadan kapanir).
  sp.distance_attenuation = true;
  sp.max_distance = 20.0f;
  sp.pos = Vec3{0, 0, -25};
  double lout = 0;
  render_spatial(mx, sine, sp, &lout, nullptr);
  CHECK(lout < 1e-6);
}

// KAPI: kaynak sagdayken sag kanal enerjisi soldan belirgin yuksek.
// KONTROL: kaynak tam ondeyken iki kanal esit (%5 icinde).
ENGINE_TEST(audio_spatial_panning_favors_the_near_ear) {
  static SystemArena sys;
  if (!sys.reserve(8u << 20, "audio_pan")) { CHECK(false); return; }
  audio::Clip sine;
  CHECK(audio::clip_sine(sys, 440.0f, 1.0f, kRate, 0.5f, &sine));
  audio::Mixer mx;
  mx.init(kRate, 2);
  mx.set_listener(audio::Listener{});

  audio::SpatialParams sp;
  sp.distance_attenuation = false; // tek degisken: panlama
  sp.head_shadow = false;
  double l = 0, r = 0;
  sp.pos = Vec3{10, 0, 0}; // tam sag (+X = cross(forward,up))
  render_spatial(mx, sine, sp, &l, &r);
  std::printf("    [bilgi] panlama sag: sol RMS %.5f, sag RMS %.5f (sabit guc: 0 / 0.3536)\n", l, r);
  CHECK(r > 0.3 && l < 0.02 * r);

  sp.pos = Vec3{-10, 0, 0}; // tam sol
  render_spatial(mx, sine, sp, &l, &r);
  CHECK(l > 0.3 && r < 0.02 * l);

  // POZITIF KONTROL: tam onde -> iki kanal esit (+-%5).
  sp.pos = Vec3{0, 0, -10};
  render_spatial(mx, sine, sp, &l, &r);
  const double diff = std::fabs(l - r) / (l > r ? l : r);
  std::printf("    [bilgi] kontrol (tam onde): sol %.5f, sag %.5f, fark %%%.3f\n", l, r, diff * 100.0);
  CHECK(l > 0.2 && diff < 0.05);
}

// KAPI: karsi kulakta YUKSEK frekans enerjisi dusuk (kafa golgesi — HRTF DEGIL).
// KONTROL: golge kapaliyken kulaklar arasi oran SAF panlama oranina esit.
ENGINE_TEST(audio_head_shadow_dims_the_far_ear_highs) {
  static SystemArena sys;
  if (!sys.reserve(16u << 20, "audio_shadow")) { CHECK(false); return; }
  audio::Clip hi, lo;
  CHECK(audio::clip_sine(sys, 6000.0f, 1.0f, kRate, 0.5f, &hi));
  CHECK(audio::clip_sine(sys, 200.0f, 1.0f, kRate, 0.5f, &lo));
  audio::Mixer mx;
  mx.init(kRate, 2);
  mx.set_listener(audio::Listener{});

  audio::SpatialParams sp;
  sp.distance_attenuation = false;
  sp.pos = Vec3{6, 0, -8}; // azimut x = +0.6: sag tarafta ama sol kanal hala sesli
  // Saf panlama orani (analitik): cos(theta)/sin(theta), theta=(0.6*0.5+0.5)*pi/2
  const double th = (0.6 * 0.5 + 0.5) * (3.14159265358979 * 0.5);
  const double pan_ratio = std::cos(th) / std::sin(th);

  double l_off = 0, r_off = 0, l_on = 0, r_on = 0;
  sp.head_shadow = false;
  render_spatial(mx, hi, sp, &l_off, &r_off);
  sp.head_shadow = true;
  render_spatial(mx, hi, sp, &l_on, &r_on);
  const double ratio_off = l_off / (r_off > 0 ? r_off : 1e-9);
  const double ratio_on = l_on / (r_on > 0 ? r_on : 1e-9);
  std::printf("    [bilgi] 6 kHz, azimut 0.6: golge KAPALI sol/sag %.4f (analitik panlama %.4f) -> golge ACIK %.4f (%.2f dB)\n",
              ratio_off, pan_ratio, ratio_on, db(ratio_on / ratio_off));
  CHECK(std::fabs(ratio_off - pan_ratio) < 0.03);      // KONTROL: kapaliyken saf panlama
  CHECK(ratio_on < 0.6 * ratio_off);                    // KAPI: karsi kulakta HF dustu
  CHECK(r_on > 0.9 * r_off);                            // yakin kulak bozulmadi

  // Ayni golge DUSUK frekansi neredeyse hic etkilemez (filtre HF'ye ozgu).
  double l_lo_off = 0, l_lo_on = 0;
  sp.head_shadow = false;
  render_spatial(mx, lo, sp, &l_lo_off, nullptr);
  sp.head_shadow = true;
  render_spatial(mx, lo, sp, &l_lo_on, nullptr);
  std::printf("    [bilgi] 200 Hz ayni golgede: sol %.5f -> %.5f (%.2f dB)\n", l_lo_off, l_lo_on,
              db(l_lo_on / (l_lo_off > 0 ? l_lo_off : 1e-9)));
  CHECK(l_lo_on > 0.9 * l_lo_off);
}

// KAPI: alcak gecirgen acikken YUKSEK frekansli sinusun RMS'i duser, DUSUK
// frekanslininki neredeyse ayni kalir; limiter tepeyi sinirda tutar.
// KONTROL: her dugum icin "kapali" hali -> etki yok / tepe sinirin ustunde.
ENGINE_TEST(audio_dsp_chain_gain_lowpass_limiter) {
  static float buf[kBlock * 2];
  auto fill = [](float *b, float hz, float amp) {
    const float w = 2.0f * 3.14159265358979f * hz / (float)kRate;
    for (uint32_t f = 0; f < kBlock; f++) { const float s = amp * std::sin(w * (float)f); b[f * 2] = s; b[f * 2 + 1] = s; }
  };

  audio::DspChain ch;
  const int gain = ch.add_gain(0.5f);
  const int lp = ch.add_lowpass(2000.0f);
  const int lim = ch.add_limiter(0.5f);
  CHECK(gain == 0 && lp == 1 && lim == 2 && ch.count() == 3);
  uint8_t ord[8] = {0};
  CHECK(ch.order(ord, 8) == 3 && ord[0] == 0 && ord[1] == 1 && ord[2] == 2);

  // --- Kazanc: tek basina, tam olcum -----------------------------------------
  ch.set_enabled(lp, false);
  ch.set_enabled(lim, false);
  fill(buf, 200.0f, 0.4f);
  const double raw = chan_rms(buf, kBlock, 2, 0);
  ch.process(buf, kBlock, 2, kRate);
  const double after_gain = chan_rms(buf, kBlock, 2, 0);
  std::printf("    [bilgi] DSP kazanc 0.5: RMS %.5f -> %.5f (oran %.4f)\n", raw, after_gain, after_gain / raw);
  CHECK(std::fabs(after_gain / raw - 0.5) < 0.005);

  // --- Alcak gecirgen (2 kHz): 8 kHz duser, 100 Hz kalir ---------------------
  ch.set_enabled(gain, false);
  ch.set_enabled(lp, true);
  fill(buf, 8000.0f, 0.4f);
  ch.process(buf, kBlock, 2, kRate); // isinma
  fill(buf, 8000.0f, 0.4f);
  ch.process(buf, kBlock, 2, kRate);
  const double hi_on = chan_rms(buf, kBlock, 2, 0);
  fill(buf, 100.0f, 0.4f);
  ch.process(buf, kBlock, 2, kRate);
  fill(buf, 100.0f, 0.4f);
  ch.process(buf, kBlock, 2, kRate);
  const double lo_on = chan_rms(buf, kBlock, 2, 0);
  // KONTROL: ayni tampon, filtre kapali.
  ch.set_enabled(lp, false);
  fill(buf, 8000.0f, 0.4f);
  ch.process(buf, kBlock, 2, kRate);
  const double hi_off = chan_rms(buf, kBlock, 2, 0);
  fill(buf, 100.0f, 0.4f);
  ch.process(buf, kBlock, 2, kRate);
  const double lo_off = chan_rms(buf, kBlock, 2, 0);
  std::printf("    [bilgi] DSP alcak gecirgen 2 kHz: 8 kHz %.5f->%.5f (%.2f dB), 100 Hz %.5f->%.5f (%.2f dB)\n",
              hi_off, hi_on, db(hi_on / hi_off), lo_off, lo_on, db(lo_on / lo_off));
  CHECK(hi_on < 0.35 * hi_off); // KAPI: HF belirgin dustu
  CHECK(lo_on > 0.97 * lo_off); // KAPI: LF neredeyse ayni
  CHECK(hi_off > 0.2 && lo_off > 0.2);

  // --- Limiter: tepe sinirda ------------------------------------------------
  ch.set_enabled(lim, true);
  fill(buf, 300.0f, 0.9f);
  ch.process(buf, kBlock, 2, kRate);
  const double peak_on = buf_peak(buf, kBlock * 2);
  fill(buf, 300.0f, 0.9f);
  const double peak_raw = buf_peak(buf, kBlock * 2);
  ch.set_enabled(lim, false);
  ch.process(buf, kBlock, 2, kRate);
  const double peak_off = buf_peak(buf, kBlock * 2);
  std::printf("    [bilgi] DSP limiter (sinir 0.5): giris tepe %.3f -> acik %.4f, KONTROL kapali %.4f\n", peak_raw,
              peak_on, peak_off);
  CHECK(peak_on <= 0.5);       // KAPI
  CHECK(peak_off > 0.5);       // KONTROL: kapaliyken sinir asilir

  // --- Sira VERI: zinciri tersine cevir -------------------------------------
  const uint8_t rev[3] = {2, 1, 0};
  ch.set_order(rev, 3);
  CHECK(ch.order(ord, 8) == 3 && ord[0] == 2 && ord[1] == 1 && ord[2] == 0);
  ch.bypass_all();
  fill(buf, 8000.0f, 0.4f);
  ch.process(buf, kBlock, 2, kRate);
  CHECK(std::fabs(chan_rms(buf, kBlock, 2, 0) - hi_off) < 1e-6); // bos zincir dokunmaz

  // --- Karistiricinin ANA YOLUNA bagli mi? ----------------------------------
  static SystemArena sys;
  if (!sys.reserve(8u << 20, "audio_dsp")) { CHECK(false); return; }
  audio::Clip sine;
  CHECK(audio::clip_sine(sys, 440.0f, 1.0f, kRate, 0.5f, &sine));
  audio::Mixer mx;
  mx.init(kRate, 2);
  mx.play(&sine, 1.0f, true);
  mx.render(g_out, kBlock);
  const double bus_raw = chan_rms(g_out, kBlock, 2, 0);
  const int mg = mx.dsp().add_gain(0.25f);
  CHECK(mg >= 0);
  mx.render(g_out, kBlock);
  const double bus_dsp = chan_rms(g_out, kBlock, 2, 0);
  std::printf("    [bilgi] ana yol DSP (kazanc 0.25): RMS %.5f -> %.5f\n", bus_raw, bus_dsp);
  CHECK(std::fabs(bus_dsp / bus_raw - 0.25) < 0.005);
}

// KAPI: test KENDI fizik dunyasini kurar, dinleyici->kaynak isini atar; duvar
// varken okluzyon 1 ve ses enerjisi duser. KONTROL: duvar kaldirilinca isin
// iskalar, okluzyon 0 ve enerji DUSMEZ.
// Katman notu: audio (L3) sim'i (L4) include etmez — isin BURADA (test/uygulama
// tarafinda) atilir, sonuc set_occlusion ile ses kaynagina PARAMETRE verilir.
ENGINE_TEST(audio_occlusion_follows_a_physics_ray) {
  static SystemArena sys;
  if (!sys.reserve(32u << 20, "audio_occ")) { CHECK(false); return; }
  audio::Clip sine;
  CHECK(audio::clip_sine(sys, 440.0f, 1.0f, kRate, 0.5f, &sine));

  sim::Physics ph;
  sim::PhysicsConfig pcfg;
  pcfg.threads = 1;
  if (!ph.init(sys, pcfg)) { CHECK(false); return; }
  const Vec3 ear{0, 0, 0}, src{0, 0, -10};
  sim::BodyId wall = ph.add_box({4, 4, 0.5f}, {0, 0, -5}, Quat::identity(), false);
  CHECK(wall.valid());
  ph.step(1.0f / 60.0f); // genis faz agaci: yeni govde sorgulanabilsin

  sim::RayHit hit;
  const Vec3 to_src = src - ear;
  const float dist = length(to_src);
  const bool blocked = ph.raycast(ear, to_src, dist, &hit);
  std::printf("    [bilgi] okluzyon isini: engel %s, mesafe %.3f (beklenen 4.5), normal (%.2f %.2f %.2f)\n",
              blocked ? "VAR" : "yok", hit.distance, hit.normal.x, hit.normal.y, hit.normal.z);
  CHECK(blocked && std::fabs(hit.distance - 4.5f) < 0.02f);

  audio::Mixer mx;
  mx.init(kRate, 2);
  mx.set_listener(audio::Listener{});
  audio::SpatialParams sp;
  sp.pos = src;
  sp.distance_attenuation = false; // tek degisken: okluzyon
  sp.head_shadow = false;
  double open_l = 0;
  mx.stop_all();
  mx.render(g_out, kBlock);
  audio::VoiceHandle v = mx.play_3d(&sine, sp, 1.0f, true);
  CHECK(v.valid());
  mx.render(g_out, kBlock);
  mx.render(g_out, kBlock);
  open_l = chan_rms(g_out, kBlock, 2, 0);

  mx.set_occlusion(v, blocked ? 1.0f : 0.0f);
  mx.render(g_out, kBlock); // rampa + filtre isinmasi
  mx.render(g_out, kBlock);
  const double occ_l = chan_rms(g_out, kBlock, 2, 0);
  std::printf("    [bilgi] duvar VAR: RMS %.5f -> %.5f (%.2f dB)\n", open_l, occ_l, db(occ_l / open_l));
  CHECK(open_l > 0.05);
  CHECK(occ_l < 0.35 * open_l); // KAPI: zayifladi (kazanc 0.2 x alcak gecirgen)

  // --- POZITIF KONTROL: duvari kaldir -> isin iskalar, ses DUSMEZ -----------
  ph.remove(wall);
  ph.step(1.0f / 60.0f);
  sim::RayHit miss;
  const bool blocked2 = ph.raycast(ear, to_src, dist, &miss);
  std::printf("    [bilgi] duvar kaldirildi: engel %s\n", blocked2 ? "VAR (BEKLENMEDIK)" : "yok");
  CHECK(!blocked2);
  mx.set_occlusion(v, blocked2 ? 1.0f : 0.0f);
  mx.render(g_out, kBlock);
  mx.render(g_out, kBlock);
  const double clear_l = chan_rms(g_out, kBlock, 2, 0);
  std::printf("    [bilgi] kontrol (duvarsiz): RMS %.5f (acik hal %.5f, %.2f dB)\n", clear_l, open_l,
              db(clear_l / open_l));
  CHECK(clear_l > 0.98 * open_l);
  ph.shutdown();
}

// KAPI: cihaz callback'i uzamsal ses + DSP zinciri koserken 0 ayirma.
// KONTROL: ayni pencerede bilincli bir `new` sayaci artirmali (sayac calisiyor).
ENGINE_TEST(audio_device_callback_allocates_nothing) {
  static SystemArena sys;
  if (!sys.reserve(8u << 20, "audio_alloc")) { CHECK(false); return; }
  audio::Mixer mx;
  audio::AudioDevice dev;
  audio::DeviceConfig cfg;
  cfg.null_backend = true;
  if (!dev.init(mx, cfg)) { std::printf("    [bilgi] null cihaz: %s\n", dev.last_error()); CHECK(false); return; }
  audio::Clip sine;
  CHECK(audio::clip_sine(sys, 440.0f, 1.0f, mx.rate(), 0.5f, &sine));
  mx.set_listener(audio::Listener{});
  audio::SpatialParams sp;
  sp.pos = Vec3{3, 0, -4};
  sp.occlusion = 0.4f;
  for (int i = 0; i < 8; i++) {
    sp.pos = Vec3{(float)i - 4.0f, 0, -4};
    mx.play_3d(&sine, sp, 0.1f, true);
  }
  mx.dsp().add_gain(0.8f);
  mx.dsp().add_lowpass(4000.0f);
  mx.dsp().add_limiter(0.7f);
  platform::thread_sleep_us(200000); // cihazin kendi kurulum ayirmalari bitsin
  const audio::MixerStats before = mx.stats();

  AllocGate::begin_frame();
  platform::thread_sleep_us(250000);
  const uint64_t allocs = AllocGate::end_frame();
  const audio::MixerStats after = mx.stats();

  // POZITIF KONTROL: sayac gercekten oluyor mu?
  AllocGate::begin_frame();
  int *p = new int(7);
  test::escape(p);
  delete p;
  platform::thread_sleep_us(50000);
  const uint64_t control = AllocGate::end_frame();

  std::printf("    [bilgi] callback %llu -> %llu (%llu blok), %u ses (%u 3B), tepe %.3f: pencerede %llu ayirma; kontrol %llu (>=1)\n",
              (unsigned long long)before.callbacks, (unsigned long long)after.callbacks,
              (unsigned long long)(after.callbacks - before.callbacks), after.voices_active, after.voices_spatial,
              after.peak, (unsigned long long)allocs, (unsigned long long)control);
  CHECK(after.callbacks > before.callbacks + 2); // gercekten render oldu (bos olcum degil)
  CHECK(after.voices_spatial == 8 && after.peak > 0.01f);
  CHECK(allocs == 0);
  CHECK(control >= 1);
  dev.shutdown();
}

// Ses thread'i onceligi: TAHMIN degil OLCUM (pthread_getschedparam). Ayricalik
// yoksa SCHED_FIFO alinamaz — o zaman bu gorunur sekilde raporlanir.
ENGINE_TEST(audio_device_thread_priority_is_measured) {
  audio::Mixer mx;
  audio::AudioDevice dev;
  audio::DeviceConfig cfg;
  cfg.null_backend = true;
  cfg.realtime_thread = true; // SCHED_FIFO iste
  if (!dev.init(mx, cfg)) { std::printf("    [bilgi] null cihaz: %s\n", dev.last_error()); CHECK(false); return; }
  const audio::DeviceInfo &di = dev.info();
  std::printf("    [bilgi] ses thread'i: politika %s, oncelik %d, gercek zamanli %s\n", di.thread_policy,
              di.thread_priority, di.thread_realtime ? "EVET" : "hayir");
  if (!di.thread_realtime) {
    skip("SCHED_FIFO alinamadi (RLIMIT_RTPRIO/CAP_SYS_NICE yok) — miniaudio NORMAL oncelige dustu; "
         "politika okundu, gercek zamanli sinif dogrulanmadi");
  }
  CHECK(di.thread_policy[0] != 0); // olcum YAPILDI (bos degil)
  dev.shutdown();
}
