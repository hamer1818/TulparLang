# Faz 4 — UI ve Ses (durum)

> UI çekirdeği FAZ3.md "2B arayüz çekirdeği" bölümünde (Faz 4'ün ilk parçası, editörün önkoşulu olarak öne alındı).
> Bu dosya ses dilimini izler.

## Ses — ilk dilim: karıştırıcı + cihaz + klip (2026-09-14, tarama belgesi §9/§11)

**Karar.** Cihaz katmanı **miniaudio** (vendored 0.11.25, MIT-0, tek başlık; gövde `audio/miniaudio_impl.c`,
yalnız gereken: cihaz + kod çözme; engine/node graph/resource manager kapalı). Android'de **AAudio** doğrudan
(API 26+, Oboe'nin sardığı düşük gecikmeli API; OpenSL yedek), Linux PulseAudio→ALSA, macOS CoreAudio, **null**
arka ucu testler için. Belgenin önerdiği Oboe, gerçek cihazda AAudio tuzağı görülürse eklenir (ek cihaz-atlatma
katmanı; şimdilik gereksiz bağımlılık). Steam Audio (HRTF/oklüzyon) ve akış (stream) sonraki dilim.

**Teslim edilen (`engine/audio/`, katman 3 — renderer ile aynı seviye)**
- `Mixer`: 32 sabit ses (voice), oyun thread'inden **kilitsiz SPSC komut halkası** (play/stop/gain/stop_all;
  nesil damgalı yuva), cihaz callback'inde `render()` — **0 ayırma, kilit yok**; mono→stereo kopya, döngü, sert
  sınır [-1,1]; istatistik (kare, callback, aktif ses, tepe, düşen komut — sessiz değil).
- `AudioDevice`: miniaudio bağlam + cihaz (f32, 48 kHz, 2 kanal, periyot arka ucun varsayılanı), callback → Mixer.
- `clip_load` (WAV/FLAC/MP3 → PCM float Arena'da, hedef hız/kanal), `clip_sine` (sentetik; test/demo).
- Demo: `TULPAR_ENGINE_AUDIO=1` (masaüstü) / `TULPAR_AUDIO=1 android_run.sh demo` → 440 Hz ton döngüde, raporda
  cihaz/arka uç + callback/kare sayısı.

**Kapılar (`tests/test_audio.cpp`)**
- `audio_mixer_levels_are_deterministic`: 0.5 + 0.25 genlikli iki sinüs → RMS **0.3953** (analitik √(0.125+0.03125)),
  tepe 0.65; biten ses düşer (aktif 2→1), stop → tepe 0; mono klip iki kanala eşit; 8 tam genlikli ses toplamı sınırda.
- `audio_null_device_runs_callbacks`: null arka uçta callback thread'i gerçekten koşar (150 ms'de 17 callback);
  **pozitif kontrol** sessizken tepe 0, ses verilince tepe 0.50.
- `audio_default_device_opens`: gerçek cihaz varsa açılır (masaüstü: PulseAudio, 48 kHz, periyot 900 kare, 12
  callback/200 ms); yoksa **ATLANDI** görünür.

**Ölçüm.** Masaüstü 71/71. Demo (masaüstü, ses açık) 120 kare: 82 callback, 73800 kare = 1.5 s, tepe 0.20, 0
düşen komut. **Emülatör (x86_64, API 37):** 70/70; **AAudio** açıldı (periyot 4360 kare), demo 300 kare: 57 callback,
248520 kare = 5.2 s ses, tepe 0.20, 0 düşen komut. **Telefon (Mali/Kirin): USB düştü, bekliyor** — AAudio periyodu ve
callback düzeni orada ölçülecek (asıl hedef).

**Sonraki.** Uzamsal ses (mesafe sönümü + pan; Steam Audio HRTF sonra), akış (uzun müzik), pitch, kaynak
öncelik/kısıtlama, `content` pack'ten klip yükleme, Opus.
