# Faz 3 — Renderer Çekirdeği: ilk dilim (başladı 2026-09-14)

Faz 3'ün asıl işi (clustered forward+, CSM, PBR, VRS, vis buffer A/B) cihaz ister. Bu
dilim ondan önce **elimize çalışan bir uygulama** geçirmek için yapıldı: Faz 0–2'nin
tamamı tek bir ikilide, ekranda. Kullanıcı kuralı (2026-09-14): her adımı CI ile
sınamak yok; yerelde doğrula, uygulama elde olunca push.

## Teslim edilen
| Parça | Katman | Dosya | Not |
|---|---|---|---|
| Pencere | L0 | `platform/window.hpp/.cpp` | GLFW 3 **dlopen** (`libglfw.so.3` / `libglfw.3.dylib`), link bağımlılığı yok; Vulkan yüzeyi + gerekli instance uzantıları; klavye/fare/tekerlek durumu. Android'de derlenmez (`#if !__ANDROID__`) |
| Cihaz iki aşama | L2 | `rhi/device.hpp/.cpp` | `init_instance(api, cfg)` → yüzey → `init_device(surface)`; kuyruk seçimi yüzeye present desteğini ister, `VK_KHR_swapchain` yalnız yüzey varsa |
| Swapchain | L2 | `rhi/swapchain.hpp/.cpp` | 2 kare uçuşta, FIFO (vsync), görüntü başına render semaforu, transient D32 depth, depth prepass → renk (2 subpass), OUT_OF_DATE/SUBOPTIMAL → `needs_recreate` |
| Offscreen özel kayıt | L2 | `rhi/offscreen.cpp` | `offscreen_render_custom`: aynı render pass'e dışarıdan kayıt (renderer'ın headless doğrulaması) |
| Renderer | L3 | `renderer/renderer.hpp/.cpp` | forward Lambert: kare UBO (viewproj, ışık), push sabiti (model, renk), staging ile mesh yükleme, prepass pipeline + renkli pipeline, `cube()`/`plane()` üreteçleri |
| Shader | — | `rhi/shaders/mesh.vert/.frag` → `*_spv.h` | `compile_shaders.py` ile depoya girer |
| Demo | L5 | `app/demo.cpp`, `app/demo_scene.cpp` | `engine_demo`: pencere **ya da** `--headless N --out x.ppm`; Faz 2 sahnesi (24 navmesh ajanı + eklem zinciri animasyonu, 40 Jolt kutusu, duvar), sabit adım, yörünge kamera, profiler özeti, kare içi `new` sayacı, içerik özeti |

Katman denetimine `app` = L5 girdi (`layer_check.py`); `clang_syntax_check.sh` GLFW başlığını görür (41 dosya temiz).

## Ölçüm (yerel, RTX 5080 + lavapipe değil; bilgi)
Headless 600 kare, 640×360:

| Ölçü | Değer |
|---|---|
| kare p50 / p99 / max | 1.78 / 2.27 / 2.35 ms (sim + kayıt + GPU bekleme, tek atış) |
| çizim çağrısı | 162 |
| kare içi `operator new` (en çok, 5. kareden sonra) | **0** |
| engine_tests | 53/53 (1 görünür atlama: doğrulama katmanı yerelde yok) |

Headless kare görüntüsü: zemin, duvar, yerleşmiş kutu yığınları, dağılmış ajanlar ve
üstlerinde sallanan eklem zincirleri — Faz 2'nin "fizikli, animasyonlu, gezinen" sahnesi.

## Bulgular
- **Sarım ve y ters çevirme (Tuzaklar 8k):** `Mat4::perspective` y'yi ters çeviriyor (Vulkan NDC).
  GL alışkanlığıyla `frontFace = CLOCKWISE` konunca tek yüzlü zemin kayboldu, küpler iç
  yüzleriyle (ışıksız, karanlık) çizildi. Doğru: ters çevrilmiş projeksiyon + **COUNTER_CLOCKWISE**.
  Kanıt: tek yüzlü zemin headless karede görünüyor. Sarım testi olarak "zemin var mı" yeter.
- **Kare yuvası tek kaynaktan (Tuzaklar 8l):** swapchain'in fence beklediği yuva ile renderer'ın
  UBO yuvası **aynı sayaçtan** gelmeli (`FrameContext::frame_index`). Ayrı sayaçlar acquire
  başarısız olunca (OUT_OF_DATE) ayrışır ve GPU'nun okuduğu UBO'ya yazılır.
- Fence gönderimden hemen önce sıfırlanır; sıfırlayıp gönderemeyen kod bir sonraki `vkWaitForFences`'i
  sonsuza kadar takar. Gönderim başarısız olursa fence sinyalli yeniden yaratılır.
- Pencereli yol bu makinede **doğrulanmadı** (kural: pencere açma). Headless yol aynı renderer,
  aynı render pass düzeni (2 subpass, D32 transient) ile doğrulandı; swapchain'e özgü kısım
  (acquire/present/yeniden yaratma) kullanıcı çalıştırınca görülecek.

## Açık iş
1. Kullanıcı pencereli demoyu çalıştırır: `./build-linux/engine/engine_demo` (ESC çıkar; `--frames N` ile kendiliğinden kapanır).
   Wayland oturumu: GLFW 3.4+ Wayland'ı seçer; sorun olursa `GLFW_PLATFORM`/X11 ile denenir.
2. Depth görüntüsü `Device::allocate` blok ayırıcıdan geliyor ve **serbest bırakılmıyor**: her
   pencere yeniden boyutlandırması 64 MB bloklardan yer yer. Faz 3'te ayırıcıya serbest bırakma
   (ya da geçici görüntüler için ayrı havuz) gelir.
3. Bu dilim **Faz 3 kapısı değildir**: clustered forward+, CSM, PBR, VRS, vis buffer A/B ve
   "bandwidth < 8 GB/s (3 cihaz)" kapısı PLAN.md §7 Faz 3'te durur.
4. Android: `ANativeWindow` → `VK_KHR_android_surface` ile aynı `Swapchain` (kod yüzey tipinden bağımsız).
