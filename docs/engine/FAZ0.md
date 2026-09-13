# Faz 0 — Ölçüm ve Temel: Durum

> Plan: `PLAN.md` §7 Faz 0. Kapatıldı 2026-09-14. Kod: `engine/` (L0 `platform/`, L1 `core/`), testler `engine/tests/`.
> Çalıştırma: `cmake --build build-linux --target engine_tests && ./build-linux/engine/engine_tests` — `./build.sh suites` de koşturur (CI: Linux x86_64 + macOS arm64).

## Plan maddeleri ↔ teslim

| plan maddesi | durum | kanıt (test) |
|---|---|---|
| Arena ailesi + tasma/sizinti tespiti | ✅ `SystemArena`/`SceneArena`/`FrameArena`, `carve`, işaret/geri sar, kanarya (`ENGINE_MEM_CANARY`), taşma sayacı, `ReturnNull`/`Fatal` politikası | `arena_*` (4), pozitif kontrol: `arena_canary_catches_overrun` |
| Pool slotmap (nesil etiketli handle) | ✅ `Pool<T>`, `Handle{index,gen}`, bayat handle → `nullptr` | `pool_handles_are_generation_tagged` |
| Fiber job system + ARM64 switch | ✅ x86_64 SysV + AArch64 AAPCS64 asm, park/resume, sayaç, bekçi sayfalı yığınlar; havuz tükenince satır içi yürütme | `fiber_switch_roundtrip_preserves_state`, `jobs_*` (4) |
| Profiler (kare çizelgesi, iş grafiği, arena görünümü) | ✅ bölgeler fiber'la göçer, `JobDecl.name` → iş başına bölge, p50/p99/max, Chrome trace JSON | `profiler_*` (2) |
| Crash reporter + sembol sunucusu | ✅ sinyal işleyici (alternatif yığın, async-signal-safe yazım), build id, modül+ofset; `tools/symbolize.py` (llvm-symbolizer/addr2line); fiber içinden çökme de raporlanıyor | `crash_reporter_writes_report_from_{plain,fiber}_crash`, `crash_capture_frames_*` |
| Math + container | ✅ `Vec2/3/4`, `Mat4` (sütun-major, Vulkan NDC), `Quat`; `Array`, `Span`, `HashMap` (açık adresleme, geri kaydırma), `StaticString` | `math_*` (4), `array_*`, `hash_map_*` (2), `static_string_*` |
| Katman kuralı = build hatası | ✅ `tools/layer_check.py` engine_core'un ön koşulu; STL konteyneri de yasak (testler dahil) | build çıktısı: "engine katman denetimi: 39 dosya, 0 ihlal" |
| A2 kapısı: karede 0 ayırma | ✅ `AllocGate` (global new/delete sayacı, yürütülebilire açıkça eklenir) | `faz0_gate_zero_allocations_per_frame` (240 kare), pozitif kontrol `faz0_gate_catches_injected_allocation` |
| Performans CI | ⚠️ **masaüstü**: `engine_tests` her build'de iki mimaride; zaman değerleri `[profiler]`/`[bilgi]` olarak bilgi, karar vermez. **Gerçek cihaz farm'ı Faz 1** | build.sh `suites` bloğu |
| Boş pencere, timestamp'li input | ➡️ **Faz 1** (Vulkan yüzeyi + Android host; plan REV 11) | — |
| İlk oyun tanımı, cihaz matrisi | ✅ belge yazıldı; oyun tanımı 2026-09-14 (stilize üçüncü şahıs arena aksiyon) | `CIHAZ-MATRISI.md` §1 |

Toplam: 27 test, 27 geçiyor (yerel, 2026-09-14). ASan/UBSan: bellek, profiler, math ve container testleri temiz (fiber/job/crash ASan altında koşturulmuyor: yığın değişimi annotasyonsuz yanlış pozitif verir — Faz 1'de `__sanitizer_start_switch_fiber` ile).

## Ölçümler (bilgi — yerel Ryzen 9800X3D, 15 worker; karar verilmez)

```
[profiler] kare=240  ort=0.058 ms  p50=0.052 ms  p99=0.143 ms  max=0.797 ms  bolge=760 dusen=0
[arena] system  tepe=9.09 MB / 32 MB  ayirma=11   (job sistemi + profiler + alt arenalar, acilista)
[arena] frame   tepe=10.8 KB / 4 MB   ayirma=720  reset=240
jobs_run_all: 10240 is, 9811 fiber gocu (fiber baska thread'de devam etti — TLS getter'i noinline)
nested wait: 64 ebeveyn 64 kez park etti, 2112 is, aclik 0
starvation:  8 fiber + 32 ebeveyn → aclik 491, kilitlenme yok
```

Kare döngüsü harness'i: 64 iş/kare (LCG), profiler bölgeleri, frame arena reset — 240 karede ayırma sayısı 0.

## Faz 0'da bulunanlar (defter: Tuzaklar 8a–8c)

1. **Fiber havuzu tükenince kilitlenme.** Park etmiş ebeveynler tüm fiber'ları tutunca çocuklara fiber kalmadı; 8 fiber + 32 ebeveyn sonsuz bekledi (1548% CPU, 33 dk). Çözüm: fiber yoksa iş worker yığınında satır içi koşar, beklemesi kuyruğa yardım eder. Havuz boyutu yine init'te (A2); açlık sayılır.
2. **`new`/`delete` çifti elenebiliyor** (C++14 allocation elision, GCC -O2+). Ayırma sayan testte pointer kaçmazsa sayaç artmaz ve kapı **yanlış geçer**. `test::escape()` engeli.
3. **GCC sabit null dereference'ı siliyor.** `volatile int *p = nullptr; *p = 42;` çökmeden döndü. Çökme kobayı adresi derleyicinin göremediği global'den okur.
4. **100 karede tek hitch p99'a girmez** (indeks 98). p99 "yüzde birlik kuyruk"tur; `max` ile birlikte okunur, ikisi de basılıyor.
5. **Fiber TLS önbelleği.** Fiber başka thread'de devam edince `thread_local` adresi bayatlar; getter `noinline` (`JobSystem::tls()`), her geçişten sonra yeniden okunur. Göç sayısı istatistikte görünür (9811/10240).

## Faz 1'e devreden

- Pencere + Vulkan yüzeyi, timestamp'li input (ALooper), Android host (Kotlin + JNI)
- Gerçek cihaz farm'ı ve cihazda perf CI (`CIHAZ-MATRISI.md` §2–3)
- ASan fiber annotasyonu
- Lock-free iş kuyruğu (şimdi spinlock'lu ring; ölçülmeden değiştirilmez)
- Kurulum boyutu ölçüm hattı (EK G.3): ilk ikili çıkınca
