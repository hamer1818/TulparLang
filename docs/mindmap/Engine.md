---
tags: [moc, engine, mobile, vulkan]
---

# Tulpar Engine — Yeni Motor Çekirdeği (2026-09-14 →)

[[Tame]]/[[Scene3D]]/[[Editor]] raylib üstünde **dondurulmuş, gönderilmeye devam eden** hat.
Bu not, **ayrı** bir motor çekirdeğinin (mobil öncelikli, Vulkan/Metal, derleme zamanı ağırlıklı)
başlangıcı: `engine/`. Kaynak plan bir oyun geliştiriciden geldi, kod yazılmadan önce yargılandı
ve düzeltildi.

- Plan (revize, ⚠️ REV işaretli 12 düzeltme): [PLAN.md](../engine/PLAN.md)
- Cihaz matrisi + ilk oyun tanımı (⛔ oyun tanımı stüdyo dolduracak): [CIHAZ-MATRISI.md](../engine/CIHAZ-MATRISI.md)
- Faz 0 durumu ve ölçümler: [FAZ0.md](../engine/FAZ0.md)

## Kararlar (bkz. [[Decisions]])
- **Neden yeni çekirdek:** raylib GLES2 yolu fizik/animasyon/UI/platform servisi taşımıyor; "gelişmiş mekanik + çok cihaza ulaşan oyun" hedefi için tavan. Ekleyerek varılmaz (planı raylib'in içine yazmak olurdu).
- **Neden C++ (şimdilik):** Tulpar bugün kutusuz struct, işaretçi, atomik, ayırmasız fonksiyon vermiyor (`struct` → `vm_allocate_object`). L0/L1 C++17; alt küme gelince L1 Tulpar'a taşınır, L2+ dili o zaman. PLAN.md §11.
- **Planın düzeltilen yanlışları:** vis buffer "birinci öncelik" (masaüstü gerekçesi), hacim başına LOD bake, "streaming yok" ↔ VT çelişkisi, Nanite yoğunluk hedefi, faz sırası (oyun Faz 5'e kadar yoktu), Tulpar'ı hazır sayma, little-core pinleme çelişkisi, Jolt determinizm kapsamı.
- **Her AL bir hipotezdir** cihazda ölçülene kadar. Emülatör TBDR değil.

## Faz 0'da öğrenilenler → [[Tuzaklar]] 8a–8c
Fiber havuzu tükenince kilitlenme (satır içi yürütme ile çözüldü); `new/delete` elision'ı ayırma kapısını yanlış geçirir; GCC sabit null dereference'ı siler; 100 karede tek hitch p99'a girmez, max'la okunur; fiber'da TLS adresi bayatlar (noinline getter).

## Faz 1 (başladı 2026-09-14) → [FAZ1.md](../engine/FAZ1.md)
`engine/rhi/` L2: `VkApi` (libvulkan **dlopen**, link bağımlılığı yok; başlıklar `engine/third_party/vulkan` vendored v1.4.351), `Device` (cihaz seçimi + yetenek raporu, blok bellek ayırıcı), offscreen ilk piksel (depth prepass → renk subpass, transient depth, piksel kapısı, GPU zaman damgası, PSO cache dosyası, subpass merge feedback zinciri). Pencere **açılmaz**; masaüstü offscreen, gerçek yüzey Android host'ta. CI: Linux lavapipe, macOS MoltenVK; loader yoksa `atlandi` sayacı görünür. Kotlin host + JNI iskeleti (`android/host/`, `engine/platform/android/`) derlenmedi: SDK yok.
İlk oyun tanımı verildi (`CIHAZ-MATRISI.md` §1). **Tek bloke:** üç gerçek cihaz + NDK — Faz 1 kapısı ("G-buffer DRAM'e inmedi", merge feedback) cihazda ölçülür.
Shader: GLSL → glslc → depoya giren C dizileri (`engine/tools/compile_shaders.py`); Slang Faz 8.
Faz 1 tuzakları: [[Tuzaklar]] 8d (antipodal slerp, ikinci mimari), 8e (NDC kapsama hesabı), 8f (A2 kapısı sürücüyü de sayar), 8g (GCC'nin geçirdiğini Clang reddeder → `engine/tools/clang_syntax_check.sh`).

## Faz 2 (başladı 2026-09-14) → [FAZ2.md](../engine/FAZ2.md)
`engine/sim/` L4: archetype ECS (SoA chunk'lar, nesil etiketli entity, kapasiteler init'te), okuma/yazma maskeli **sistem zamanlayıcı** (aşamalar init'te, aşama içi paralel, kayıt sırası belirlenimli), sabit adım (fazla tick atılır ve sayılır), kayıt/replay. **Faz 2 kapısı:** 1000 tick × 500 entity seri = replay = paralel aynı özet. **Jolt 5.3.0** vendored (`engine/third_party/jolt`, MIT; `JPH_CROSS_PLATFORM_DETERMINISTIC`, x86_64 SSE4.2 / arm64 NEON), `Physics` sarmalayıcısı (Jolt tipleri sızmaz), Jolt job'ları **fiber job sisteminde** (`FiberJoltJobs`), thread sayısından ve job sisteminden bağımsız aynı özet; platformlar arası altın özet (x86_64 `ed7d5c5d0986ca44`) CI arm64'te sınanıyor (REV 8 sınaması). Bulgular: Jolt çarpışma job'ları 64 KB fiber yığınını taşırdı (bekçi sayfa yakaladı → 256 KB), adım içi ayırma `QuadTree::UpdatePrepare` (sınırlı, kabul). Kalan: animasyon, navmesh, GPU particle.
**Faz 2 yazılım tarafı kapandı (2026-09-14):** + Recast/Detour navmesh (bake + 0-ayırmalı sorgu), kendi animasyon formatı (sabit iz eleme, 16-bit niceleme, en-küçük-üç dönüş; 5.3×), entegre headless sahne (16 ajan + 20 kutu + animasyon, 600 tick, seri = paralel, 0 new). Jolt platformlar arası determinizmi CI'da bit eşit doğrulandı → PLAN.md REV-2 ve "Determinizm sözleşmesi" (§11 altı): `-ffp-contract=off` + libm'siz girdi şart.
Faz 2 tuzakları: 8h (fiber yığını, üçüncü parti job), 8i (auto-merge ilk yeşilde birleşir, sonraki push kaybolur), 8j ("cross-platform deterministic" define'ı tek başına yetmez: FMA birleştirmesi derleyicinin).

## Faz 3 ilk dilim (başladı 2026-09-14) → [FAZ3.md](../engine/FAZ3.md)
**İlk uygulama:** `engine_demo` (L5 `engine/app/`) — pencere (`platform/window`, GLFW 3 dlopen) → `Device::init_instance` → yüzey → `init_device(surface)` → `Swapchain` (2 kare uçuşta, FIFO, transient D32, prepass → renk) → `Renderer` (L3, forward Lambert, UBO + push sabiti) → Faz 2 sahnesi (24 ajan + eklem zinciri, 40 Jolt kutusu). `--headless N --out x.ppm` aynı renderer'ı offscreen render pass'e kaydeder (`offscreen_render_custom`): ben bununla doğrularım, pencereyi kullanıcı açar. Yerel: 600 kare, kare içi 0 `new`, 53/53 test. **Çalışma kuralı (kullanıcı, 2026-09-14):** adım başına CI/push yok; yerelde doğrula, uygulama elde olunca push.
Tuzaklar: 8k (y ters çevirme + `CLOCKWISE` = zemin kaybolur), 8l (swapchain/renderer kare yuvası tek kaynaktan; fence gönderimden önce sıfırlanır).

## İlk gerçek cihaz (2026-09-14) → [CIHAZ-MATRISI §2.1](../engine/CIHAZ-MATRISI.md), [FAZ3.md](../engine/FAZ3.md)
Huawei P20 Pro (Kirin 970, **Mali-G72**, Vulkan 1.1, Android 10) adb ile bağlı. Motor **NativeActivity host** olarak koşuyor (`engine/app/android_main.cpp` → `libtulparengine.so`; `engine/tools/android_run.sh` derler, paketler, kurar, logu çeker). Kip `debug.tulpar.mode` ile: `tests` (aynı süreçte `engine_tests_main`), `demo` (ekranda), `headless` (offscreen + PPM).
**Sonuç:** testler 53/53; demo **59.9 fps** (FIFO), MAILBOX'ta 241 fps / 3.48 ms → **CPU-submit bağlı, GPU değil**; kare içi 0 `new`; 600 tick sahne özeti masaüstüyle **bit eşit**; `LAZILY_ALLOCATED` var → TBDR doğrulandı.
**Plan düzeltmesi (⚠️ REV-3):** L2 "zorunlu feature" listesi (descriptorIndexing/timelineSemaphore/bufferDeviceAddress) bu cihazda **yok**; kapı **rapora** çevrildi (`DeviceCaps::missing_mandatory`).
Cihaz tuzakları: [[Tuzaklar]] 8m (SUBOPTIMAL'i recreate saymak = 20 fps), 8n (adb shell'den GPU görünmez), 8o (plan "zorunlu" dedi, cihaz vermedi).

