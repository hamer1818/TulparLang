# Faz 1 — RHI, İlk Piksel ve Android Host: Durum

> Plan: `PLAN.md` §7 Faz 1. Başladı 2026-09-14 (Faz 0 kapanır kapanmaz). Kod: `engine/rhi/` (L2),
> `engine/platform/android/`, `android/host/`. Testler: `engine_tests rhi`.

## Teslim edilen

| plan maddesi | durum | kanıt |
|---|---|---|
| Vulkan backend (loader, cihaz, kuyruk, bellek) | ✅ `VkApi` dlopen (link bağımlılığı yok, başlıklar vendored v1.4.351), cihaz seçimi + **yetenek raporu** (zorunlu feature'lar, uzantılar), blok bellek ayırıcı (vkAllocateMemory sayısı sabit: ilk kare 2) | `rhi_loader_and_device_caps` |
| İlk piksel | ✅ **offscreen**, pencere açmadan: depth prepass → renk subpass zinciri, depth transient (`STORE_OP_DONT_CARE`, LAZILY_ALLOCATED varsa), piksel geri okuma; merkez = üçgen rengi, köşe = temizleme, kapsama %17.6 (beklenen %18) | `rhi_first_pixel_offscreen_triangle`; görüntü `/tmp/engine_rhi_*/ilk_piksel.ppm` |
| GPU timing okunuyor | ✅ zaman damgası sorgusu: render pass süresi (RTX 5080: 0.008 ms) | aynı test |
| PSO yükleme stratejisi | ✅ `VkPipelineCache` blob dosyaya yazılır/yüklenir; başlık (vendorID/deviceID) cihazla eşleşmezse **yüklenmez** (yabancı cache sessizce kabul edilmez). `VK_EXT_graphics_pipeline_library` varlığı raporlanır, kullanımı sonraki adım | aynı test: 2. çizimde `pso_cache_loaded` |
| `VK_EXT_subpass_merge_feedback` kapısı | ⚠️ kod hazır (RenderPass2 pNext zinciri, durumlar raporlanır); NVIDIA/lavapipe'ta uzantı **yok** → "uzantı YOK" basılır. Kapı **cihazda** ölçülür (Mali/Adreno) | aynı test `[bilgi]` satırı |
| `VK_EXT_host_image_copy` | ⚠️ varlığı raporlanır; doku yükleme yolu Faz 6 (VT) ile | caps |
| Kotlin host + JNI köprüsü | ⚠️ **iskelet, derlenmedi** (bu makinede SDK/NDK/Gradle yok): `android/host/` (Activity + SurfaceView + ADPF hint oturumu), `engine/platform/android/jni_bridge.cpp` (timestamp'li dokunma halkası, yüzey yaşam döngüsü) | ilk NDK derlemesinde imzalar doğrulanır |
| Pencere + timestamp'li input | ⚠️ Android host'ta tasarlandı (MotionEvent.eventTime → ns halkası); masaüstünde pencere **açılmaz** (kural), offscreen yeter | `jni_bridge.cpp` |
| Gerçek cihaz farm'ı, cihazda perf CI | ⛔ **cihaz yok** — Faz 1 kapısının ("G-buffer DRAM'e inmedi, 3 cihazda") tek engeli | `CIHAZ-MATRISI.md` §2 |
| CI | ✅ Linux: `libvulkan1 + mesa-vulkan-drivers` (lavapipe) ile offscreen test koşar; macOS: `molten-vk`. Loader/cihaz yoksa **ATLANDI** sayılır, özet satırında görünür | build.yml, `engine tests: … atlandi` |

## Ölçümler (bilgi)

```
RTX 5080 (ayrik GPU) api=1.4.351: zorunlu 3/3, lazilyAllocated=0, timestamps 1.0 ns
subpass_merge_feedback=0 graphics_pipeline_library=1 host_image_copy=1 fragment_shading_rate=1
ucgen %17.6 piksel; GPU render pass 0.008 ms; vkAllocateMemory=2; PSO cache 22640 B
2. cizim: cache yuklendi, bizim kodda 0 ayirma (surucu ici malloc sayilmaz)
```

## Notlar
- **A2 kapısı ve sürücü ayırmaları (CI lavapipe, 2026-09-14):** tek adımlı offscreen çizim `operator new` sayacında sıfır vermedi: lavapipe (Mesa + LLVM JIT) pipeline/image kurulumunda C++ `new` kullanıyor ve global override onu da sayıyor; NVIDIA sürücüsü saymadı (kendi ayırıcısı). Kurulum ile kare ayrıldı (`offscreen_create` / `offscreen_render_frame`): kurulumun ayırması ölçülüp **bilgi** basılır, **kare** için 0 iddia edilir. Ders: A2 iddiası "bizim kodumuz kare içinde ayırmaz"dır; sürücü içi ayırma ayrı kalem, cihazda ayrıca ölçülür (Mali/Adreno sürücüleri de sürece yüklenir).
- **macOS CI:** yalnız `molten-vk` kurulunca loader (`libvulkan.1.dylib`) bulunamadı ve 3 test **görünür** atlandı (harness sayacı işini yaptı). `vulkan-loader` eklendi; MoltenVK tam yol yedeği (`/opt/homebrew/lib/libMoltenVK.dylib`) kondu.
- Shader'lar GLSL (`engine/rhi/shaders/*.vert|frag`) → `glslc` → depoya giren C dizileri (`compile_shaders.py`). Plan Faz 8'e kadar Slang diyordu; `slangc` bu makinede ve CI'da yok (Arch'taki `slang` paketi S-Lang kütüphanesi). Faz 8'de Slang'e geçilir; Faz 1 üçgeni için GLSL yeterli.
- Depth `D32_SFLOAT`; LAZILY_ALLOCATED bellek masaüstünde yok (TBDR'da var) — kod tercih eder, yoksa DEVICE_LOCAL'a düşer ve bunu `caps.lazily_allocated_memory` ile raporlar.
- Layer kuralı: `rhi/` L2, yalnız `core/` ve `platform/` içerir; `<vulkan/vulkan.h>` üçüncü parti.

## Kalan (Faz 1 kapısı için)
1. Üç gerçek cihaz (Mali, Adreno, düşük segment) ve NDK/SDK: host derlemesi + `subpass_merge_feedback` + GPU sayaçla "DRAM'e inmedi" kanıtı.
2. Yüzeyli (swapchain) yol: Android `ANativeWindow` → `VK_KHR_android_surface`; masaüstünde açılmaz.
3. `VK_EXT_graphics_pipeline_library` ile parçalı PSO linkleme (cache yükleme süresi ölçümüyle).
4. Komut tamponlarının job sisteminden paralel kaydı (thread başına pool).
