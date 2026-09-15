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

**Pencereli koşu (kullanıcı, 2026-09-14, Wayland + RTX 5080):** açıldı, ESC ile kapandı, 18 s / 3228 kare.

| Ölçü | Değer |
|---|---|
| kare p50 / p99 / max | 5.55 / ~5.7 / 7.6 ms (FIFO vsync, 180 Hz ekran — GPU/CPU sınırı değil, ekran) |
| kare içi `operator new` | 0 |
| içerik özeti, 600. tick | `1513845f8ca5afd9` — headless 600 karelik koşuyla **bit eşit** (sabit adım: pencere hızından bağımsız aynı simülasyon) |

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
1. ~~Pencereli demo~~ çalıştı (Wayland, GLFW 3.5). Yeniden boyutlandırma/küçültme yolu (`needs_recreate`) henüz denenmedi.
2. ~~Depth görüntüsü blok ayırıcıdan geliyor ve serbest bırakılmıyor~~ **kapandı**:
   `Device::allocate_dedicated` / `free_dedicated` eklendi, swapchain derinliği onu kullanıyor.
   Test `rhi_dedicated_allocation_is_released` 8 "yeniden boyutlandırma" döngüsünde blok sayısının
   değişmediğini gösteriyor; **pozitif kontrol** aynı döngünün blok ayırıcıyla 0 → 2 blok (128 MB)
   yediğini gösteriyor — yani test gerçekten bir şey ölçüyor.
3. Bu dilim **Faz 3 kapısı değildir**: clustered forward+, CSM, PBR, VRS, vis buffer A/B ve
   "bandwidth < 8 GB/s (3 cihaz)" kapısı PLAN.md §7 Faz 3'te durur.
4. Android: `ANativeWindow` → `VK_KHR_android_surface` ile aynı `Swapchain` (kod yüzey tipinden bağımsız).

## Telefonda (Huawei P20 Pro, Mali-G72) — 2026-09-14

Kullanıcı adb ile bağlı bir telefon verdi; **ilk gerçek cihaz**. Tüm sayılar
`docs/engine/CIHAZ-MATRISI.md` §2.1'de. Buradaki iş:

| parça | dosya | not |
|---|---|---|
| NativeActivity host | `app/android_main.cpp` | `libtulparengine.so`; `debug.tulpar.mode` = `tests` / `demo` / `headless`; stdout → boru → logcat + `files/engine_log.txt` |
| Manifest | `platform/android/AndroidManifest.xml` | `hasCode=false`, NativeActivity, yatay |
| Koşum betiği | `tools/android_run.sh` | NDK derleme → APK paketleme → kurulum → başlat → log çek |
| Test girişi | `tests/test_main.cpp` | `engine_tests_main()` ayrıldı (`ENGINE_TESTS_NO_MAIN`); geçici dizin `$TMPDIR` (Android'de `/tmp` yok); alt süreç yoksa çökme testi **görünür atlanır** |
| Tek başına CMake | `engine/CMakeLists.txt` | `cmake -S engine -B build-android -DCMAKE_TOOLCHAIN_FILE=...` ile çapraz derlenir |
| Ön-döndürme + sunum kipi | `rhi/swapchain.*` | `SwapchainConfig{prerotate, preferred_present_mode}` |
| "Zorunlu" feature kapısı | `rhi/device.*` | kapı → rapor (`missing_mandatory`), Tuzaklar 8o |

**Üç bulgu, üçü de plan/kod düzeltmesi:**
1. **`adb shell`den GPU görünmüyor** (0 fiziksel cihaz) — ölçüm APK sürecinde yapılır (Tuzaklar 8n).
2. **Plan L2 "zorunlu" listesi cihazda yok** — kapı rapora çevrildi, PLAN.md ⚠️ REV-3 (Tuzaklar 8o).
3. **SUBOPTIMAL → her karede swapchain yeniden** = 20 fps; düzeltildi + ön-döndürme → **59.9 fps** (Tuzaklar 8m).

**Doğrulananlar:** `engine_tests` cihazda 53/53; Jolt altın özeti eşit; 600 tick sahne özeti
`1513845f8ca5afd9` masaüstüyle **bit eşit**; kare içi 0 `new`; aynı karenin pikselleri masaüstüne
göre yalnız %0.237 (üçgen kenarları). `LAZILY_ALLOCATED` bellek **var** — TBDR doğrulandı, transient
depth gerçekten tile'da kalıyor (masaüstünde bu yol yok).

**Bu bir Faz 1/3 kapısı değil:** tek koşu, soğuk başlangıç, termal pencere yok; GPU zaman damgası bu
sürücüde yok, `subpass_merge_feedback` yok → "G-buffer DRAM'e inmedi" kanıtı **hâlâ** Adreno + modern
Mali bekliyor.

## Açık: macOS CI'da `engine_tests` çöküyor (yerelden ulaşılamıyor)

macOS arm64'te özet satırı basılmadan düşüyor; son `[bilgi]` fizik testinin, yani **navmesh /
animasyon / sahne** üçlüsünün birinde. Yerel imkânlarla kovalandı ve **bulunamadı**:

| yerel araç | sonuç |
|---|---|
| Linux x86_64 tam takım | 54/54 geçiyor |
| Android arm64 (telefon, aynı Recast/Detour) | 54/54 geçiyor → "arm64 sorunu" **değil** |
| ASan + UBSan (Linux, `build-engine-asan`) | tek bulgu kasıtlı null yazması (çökme testi); sim/navmesh/animasyon **temiz** |
| `clang_syntax_check.sh` | 43 dosya temiz |

Elimizde Mac yok; bu yüzden **teşhis altyapısı** eklendi ve sıradaki macOS koşusunda yanıt kendiliğinden
gelecek: harness çökme sinyalinde **kosan testin adını** basıyor (`COKME testi: <ad> (SIGSEGV)`,
`--cokme-kontrol` ile yerel pozitif kontrolü yapıldı), `build.sh` o satırı gösteriyor ve navmesh testi
bake düşerse sorgulara devam etmeden sebebini basıyor. Kural gereği (kullanıcı, 2026-09-14) CI'ya
gidilmiyor; bu madde **açık** kalır.

## Gölge haritası — Faz 3 çıktısının ilk yarısı (2026-09-14)

Plan Faz 3'ün çıktısı "ışıklı, **gölgeli** sahne". Tek kademeli yönlü ışık gölge haritası geldi:

| parça | yer | not |
|---|---|---|
| Gölge render pass'i | `renderer/renderer.cpp` `make_shadow()` | ayrı pass, tek derinlik eki, `storeOp = STORE` (örneklenecek, tile'da kalamaz) |
| Format seçimi | aynı | önce **D16_UNORM** (mobil bant genişliği), yoksa D32; `SAMPLED` + `DEPTH_STENCIL_ATTACHMENT` şartı sorgulanır. Hem NVIDIA hem Mali D16 verdi |
| Örnekleme | `mesh.frag` | `sampler2DShadow` + `compareOp LESS_OR_EQUAL` (donanım PCF) + 3×3 |
| Eğilim | `mesh.vert` | **dünya uzayında normal kaydırması** (`shadow_params.w`, metre). `depthBias` KULLANILMIYOR — Tuzaklar 8q |
| Işık matrisi | `Renderer::directional_light_matrix` | `Mat4::ortho` (Vulkan z∈[0,1], y aşağı) + `look_at`; dik ışıkta `up` değiştirilir (NaN) |
| Pass sırası | `Swapchain::acquire` / `begin_render_pass` ayrıldı, `offscreen_render_custom(..., before)` | bir render pass içinde başka pass açılamaz |

**Cihazda bulunan hata:** ilk sürüm NVIDIA'da doğru, **Mali-G72'de gölgesizdi**. Sebep boru hattının
`depthBias`'ı: birimi sürücüye bağlı. Dünya uzayı normal kaydırmasına geçince iki cihazda da doğru.

**Kapı:** `renderer_shadow_map_actually_darkens` — gölge açık/kapalı iki kareyi karşılaştırır,
koyulaşan pikseli sayar, **açılan piksel 0 olmalı**, ve kendi negatif kontrolünü koşar (6 m kaydırma →
koyulaşan 0). Masaüstü ve telefon **aynı sayıyı** verdi: 2437 farklı piksel, 2433 koyulaşan.

**Maliyet (Huawei P20 Pro, Mali-G72, 2159×1080, 2048² D16 gölge, 162 çizim):**

| ölçü | gölgesiz | gölgeli |
|---|---|---|
| FIFO (ürün yolu) | 59.9 fps | **59.9 fps** (vsync kilitli; iş ~5.5 → ~6.2 ms) |
| MAILBOX (kilit açık) | ~242 fps | ~234 fps (tek koşu, yüksek varyans) |
| CPU kayıt | 1.29 ms | 2.0 ms (ikinci geçişin çizimleri) |

Yani 2018 orta segment telefonda gölge **%3 civarı** bir bedelle geldi ve 60 fps korundu.
Kalan (PLAN.md Faz 3): kümelenmiş forward+ ve çok ışık, CSM kademeleri, PBR, VRS, vis buffer A/B.

## Doku, malzeme ve glTF — Faz 6'nın içe aktarma dilimi Faz 3'e çekildi (2026-09-14)

**Neden şimdi:** Faz 3'ün kapısı bant genişliği ölçümü; dokusuz küplerle o ölçüm anlamsız. Bir oyun
motoru gerçek içerik yüklemeden motor değildir. Plan sırası (içerik Faz 6) bu dilim için öne alındı;
pipeline'ın kalanı (pack, ASTC, scene compiler) Faz 6'da duruyor.

| parça | yer | not |
|---|---|---|
| Vertex UV | `renderer::Vertex{pos, nrm, uv}` | 32 B; `cube()`/`plane(uv_repeat)` üreteçleri UV yazar |
| Doku | `Renderer::create_texture` | RGBA8, staging → optimal tiling, **mip zinciri blit ile** (format `BLIT_SRC/DST` + doğrusal süzme sorgulanır) |
| Malzeme | `Renderer::create_material` | **klasik descriptor set (set 1)** — bindless YOK: düşük sınıf `descriptorIndexing` vermiyor (REV-3). Malzeme değişiminde set bağlanır; `stats.material_binds` sayılır |
| Varsayılan | 1×1 beyaz doku + malzeme | dokusuz çizim aynı shader yolundan gider (tek pipeline) |
| glTF 2.0 | `engine/content/gltf.*` (L6) | **cgltf 1.14** + **stb_image 2.30** vendored (`third_party/cgltf`, `third_party/stb`; gövdeler `content/vendored_impl.c`, `-w`). Üçgen primitifleri, POSITION/NORMAL/TEXCOORD_0, indeks, baseColor faktör+doku, düğüm hiyerarşisi → dünya matrisli instance'lar, sınırlar. Data URI, dış dosya ve GLB görüntüleri |
| Yükleme/çizim | `content::upload_model` / `draw_model` | dokular → malzemeler → mesh'ler; instance'lar `model * world` |
| Test varlığı | `tests/assets/checker_cube.gltf` | `tools/make_test_gltf.py` üretir (belirlenimli, tek dosya, gömülü PNG) |
| Katman | `layer_check.py`: `app` = L6 | app birleştirme kökü: content'i görür, tools'u görmez |

**Kapılar:** `content_gltf_loads_checker_cube` (sayılar, dama pikselleri, UV aralığı, sınırlar) ve
`content_textured_cube_renders_checker` — dokulu küp ile aynı küpün düz malzemesini karşılaştırır:
keskin geçiş sayısı dokuluda ≥ 4× (ölçüldü 2142 / 384) ve hem turuncu hem lacivert piksel var.
Düz küp **pozitif kontrol**: doku yolu kırılsa ikisi aynı çıkar ve test düşer.

**Demo:** zemin yordamsal gri dama (dosyasız, cihazda da var), kutular glTF dama küpü ve malzemesiyle
(`TULPAR_ENGINE_ASSETS` ya da kaynak ağacı). Masaüstü headless 600 kare: sahne özeti değişmedi
(`1513845f8ca5afd9`) — doku sim'e dokunmaz.

**Telefonda (Mali-G72, USB geri gelince):** içerik kapıları geçti — keskin geçiş 2152 / 384 (masaüstü 2142:
rasterizer farkı), turuncu 8226 / lacivert 7731; `engine_tests` 58/58; dokulu+gölgeli demo FIFO 59.6 fps,
MAILBOX 233.9 fps (gölge-yalnız 233.8) → doku bedeli bu ölçekte ölçülemez. Ekran görüntüsü dama zemin ve
glTF kutuları gösteriyor.

Bilinen boşluk: renk uzayı yok (UNORM doku × ışık → UNORM hedef, "ekran uzayında aydınlatma"); sRGB/linear
ayrımı PBR ile gelir. Mobil asıl doku yolu ASTC/KTX2 (Faz 6).

## Sanal makine (Android Emülatörü) — işlevsel test yatağı (2026-09-14)

Telefonun adb bağlantısı kopunca kullanıcı "sanal makine ile test et" dedi. `Pixel_10_Pro_XL` AVD
(Android 17 / API 37, x86_64, 16 KB sayfa, gfxstream → ana makinenin RTX 5080'i, Vulkan 1.3) headless
(`-no-window -gpu host`) koşuyor; `TULPAR_ANDROID_ABI=x86_64 engine/tools/android_run.sh ...` aynı
akışı sürüyor (`build-android-x86_64/`). **Yalnız işlevsel:** TBDR değil, `lazily_allocated=0`,
performans kapısı değil (CIHAZ-MATRISI §2 kuralı).

| ölçü | emülatör |
|---|---|
| `engine_tests` | 58/58, 4 görünür atlama (alt süreç ×2, GPL, doğrulama katmanı) |
| içerik kapıları | masaüstüyle **aynı sayılar** (keskin geçiş 2142/384; turuncu 8163, lacivert 7724) |
| gölge kapısı | aynı (2437 / 2433) |
| demo | 60.5 fps (vsync), glTF APK'dan yüklendi |

**Bulgu:** `adb push` ile `/sdcard/Android/data/<pkg>/files/` altına konan varlığı uygulama **okuyamadı**
(kapsamlı depolama; Android 10 telefonda okunuyordu). Doğru yol: varlıklar APK'nın `assets/` dizinine
girer (`android_run.sh` stage eder), host açılışta `AAssetManager` ile **dahili** dizine çıkarır ve
`TULPAR_ENGINE_ASSETS` oraya işaret eder. cgltf `fopen` istediği için çıkarma şart.
Emülatörü sandbox içinden arka planda başlatmak olmuyor (süreç 144 ile ölüyor); sandbox dışı arka plan
görevle başlatılır.

## Çok ışık — kümelenmiş (clustered) nokta ışıklar (2026-09-14)

Faz 3'ün özü: PLAN §1 "clustered forward+ **AL**", ilk oyun 8–16 dinamik ışık ister (CIHAZ-MATRISI §1).

| parça | yer | not |
|---|---|---|
| Küme atama | `renderer/cluster.*` | 16×9×24 grid (ekran tile × log derinlik dilimi), küme başına **32-bit ışık maskesi**. **CPU'da** atanır: bu ölçekte mikrosaniye, belirlenimli, compute + SSBO senkronu yok, Vulkan 1.1 cihazda ek özellik istemez. Işık sayısı büyürse aynı maskeler compute'ta üretilir (render pass **öncesi**, zinciri bölmez, §8/10) |
| Konservatiflik | aynı | küre görünüm-uzayı AABB'siyle projekte edilir; yakın düzlem gerisine taşan köşe **tüm ekranı** işaretler (ışık zaten kamerada); ekran dışı / derinlik dışı ışık hiç işaretlemez |
| GPU tarafı | `mesh.frag` | `gl_FragCoord` + görünüm derinliği (`v_viewz`) → küme; maskede `findLSB` döngüsü; Lambert + pencereli ters-kare sönüm (yarıçapta sıfır). Işıklar UBO (binding 2, 32×32 B), maskeler SSBO (binding 3, 13.8 KB), uçuşlu kare başına |
| Grid uzayı | `set_render_size` | küme framebuffer uzayında: Android ön-döndürmede `proj` döndürülmüş olduğundan atama da otomatik döner |
| API | `add_point_light` / `clear_point_lights` | kare başına en çok 32; `stats.clusters` (görünen ışık, dokunulan küme, küme başına en çok ışık) |

**Kapılar:** `renderer_cluster_assignment_is_conservative_and_local` (ortadaki ışık orta tile'ı işaretler,
köşeyi/yakın dilimi işaretlemez; arkadaki ışık hiçbir şeyi; dev ışık her şeyi; dilim formülü tekdüze) ve
`renderer_point_light_lights_only_near_pixels` — karanlık sahnede kırmızı nokta ışık: ışıklı 14 821 kırmızı
piksel, ışıksız **0** (pozitif kontrol), görüş dışına konan ışık **0** (küme ataması ekran uzayında doğru).
Telefonda aynı: 14 824 / 0 / 0.

**Maliyet (Mali-G72, 8 ışık, 2159×1080, doku + gölge açık):**

| koşul | önce (doku+gölge) | + 8 küme ışığı |
|---|---|---|
| FIFO | 59.6 fps | **59.9 fps** |
| MAILBOX | ~234 fps / 3.02 ms | ~223 fps / 3.80 ms → **~%5** |
| kayıt CPU | 1.05 ms | 1.24 ms |

Kalan (Faz 3): stochastic tile (düşük segment), CSM kademeleri, VRS, renk uzayı, vis buffer A/B.

## Girdi ve oynanabilir karakter (2026-09-14)

Plan Faz 1 "timestamp'li callback input" ve ilk oyunun "dokunmatik joystick + eylem düğmeleri" gereği.

| parça | yer | not |
|---|---|---|
| Dokunmatik durum | `platform/touch.hpp` (L0) | sabit 10 nokta, id'li begin/move/end; host doldurur |
| Android | `app/android_main.cpp` | `onInputEvent` → AMotionEvent (DOWN/POINTER_DOWN/MOVE/UP/CANCEL) |
| Masaüstü | `app/demo.cpp` | fare sol tuş = parmak 0; WASD + boşluk klavye yolu |
| Sanal joystick | `app/virtual_stick.*` | sol yarım: dokunulan noktadan sürükleme = hareket (-1..1, 120 px tam sapma); sağ yarım: sürükleme = bakış, kısa dokunuş = eylem. Saf matematik, testli |
| Oyuncu | `app/demo_scene.*` | dinamik Jolt kutusu; komut karede latch, **her tick** uygulanır (yatay hız doğrudan, düşey korunur, zıplama yerdeyken darbe) → deterministik |
| Kamera | `app/demo_app.cpp` | oyuncuyu izleyen yörünge; hareket kameraya göre (sağ/ileri) |
| Arena | `demo_scene.cpp` | fizik zemini görsel arena kadar + görünmez kenar duvarları (oyuncu -25 m'ye yürüyüp görüntüden çıkmıştı) |

**Kapı:** `input_virtual_stick_move_look_and_tap` (kök 0, yarım sapma 0.5, doygunluk 1, bakış deltası,
bırakınca sıfır, uzun sürüklemede eylem yok, kısa dokunuşta tek karelik eylem).

**Cihazda uçtan uca (Huawei P20 Pro):** `adb shell input swipe` ile sol yarıma sürükleme → log
`cubuk (0.81, 0.58) dokunus 1` → oyuncu (-4, 0.5, 4)'ten duvara kadar yürüdü ve **-9.62'de durdu**
(arena kenarı -10, yarı genişlik 0.4). Sağ yarıma sürükleme kamerayı döndürdü. 59.8 fps korundu.
Replay: komutlar tick başına latch'lendiği için `InputRecorder` ile kaydedilebilir; oyun katmanına kaldı.

## 2B arayüz çekirdeği + font (Faz 4'ün ilk parçası, editörün önkoşulu) — 2026-09-14

Kullanıcı yönü: "arayüz işlerini komple editör içinden yapmamız lazım." Editör (PLAN L7, Faz 9) öne
çekildi; sıralama **UI çekirdeği → veri modeli/sahne formatı → editör**. Bu dilim UI çekirdeği.

| parça | yer | not |
|---|---|---|
| Immediate-mode 2B | `Renderer::ui_*` | piksel uzayı dörtgen kuyruğu (`UiVertex` 20 B), kare başına sabit kapasite (32 768 köşe), alfa karışımlı, derinliksiz, **aynı subpass'te 3B'den sonra** (`ui_record`). Ana pipeline layout'u paylaşır (set 1 = atlas) |
| Ön-döndürme | `ui.vert` | UI **mantıksal** (görünen) piksel uzayında çizilir, push sabitiyle 3B ile aynı açıda döndürülür; dokunmatik koordinatlarla aynı uzay. İlk sürüm framebuffer uzayındaydı: telefonda HUD 90° yatık çıktı (ölçüldü, düzeltildi) |
| Font | `content/font.*` | **stb_truetype** vendored; ASCII + Latin-1 + Türkçe (ğ ı ş İ Ğ Ş), 2× oversample, RGBA8 atlas (beyaz + alfa; (0,0) texeli beyaz opak → `ui_rect`). UTF-8 çözücü. Atlas sığmazsa kenar ikiye katlanır (2048'e kadar): 28 px × 213 glif 512'ye sığmayıp telefonda "font yok" vermişti |
| Varlık | `engine/assets/fonts/DejaVuSans.ttf` | Bitstream Vera lisansı (`DejaVu-LICENSE.txt`); APK'ya `.ttf` de giriyor |
| HUD | `demo_app.cpp` `draw_hud` | fps / ms / ışık; etkileşimliyse oyuncu konumu + joystick halkası ve topuzu |

**Kapı:** `renderer_ui_text_draws_pixels` — "Tulpar Engine ğüşİ" 807 parlak piksel, boş metin **0**
(pozitif kontrol), 50×20 kutu 1000 piksel; genişlikler tekdüze. Telefonda aynı sayılar.
Telefonda HUD ekranda üstte, doğru yönde, 60 fps korundu (`phone_hud2`).

**Editöre giden yol (sıra):** (1) sahne veri modeli — entity/bileşen/transform/mesh/malzeme/ışık/fizik
şekli, deterministik metin format, yükle/kaydet; (2) editör = masaüstünde motor uygulaması: kamera
uçuşu, entity listesi, tıklamayla seçim (ışın–AABB), eksen gizmosu, özellik paneli, kaydet, oynat/durdur;
(3) Tulpar oyun betiği bağlaması. UI parçacıkları (düğme, kaydırıcı, metin girişi) bu çekirdeğin üstüne.

## Mali linter (PerfDoc'un ardılı) + tile bütçesi — tarama belgesine karşı kapatılan boşluklar (2026-09-14)

Kaynak: kullanıcının "GitHub Derinlik Taraması — Mobil" belgesi; madde madde karşılaştırma
`docs/engine/BOSLUK-TARAMASI.md`, boyut tablosu `docs/engine/KARSILASTIRMA.md`.

**Teslim edilen**
- `DeviceConfig::best_practices`: Khronos doğrulama katmanının BestPractices + **Arm satıcı kuralları**
  (`validate_best_practices_arm`, `VK_EXT_layer_settings` pNext ile). PerfDoc arşivlendi; ardılı bu.
  Uyarılar kimlik başına sayılır (`Device::best_practice_id`, `best_practice_count`).
- `rhi/tile_budget.hpp`: Arm birleştirme bütçesi (≤8 renk+girdi attachment, ≤128 bit/px renk) swapchain ve
  offscreen geçişi yaratılırken **zorlanır**; aşım init hatası, bilinmeyen biçim hata.
- Katman telefona: `engine/tools/fetch_vvl_android.sh` (Khronos Android ikilileri 1.4.357.0, gitignore'lu)
  → `android_run.sh tests` ve `TULPAR_VALIDATION=1 … demo` APK'ya koyar. Masaüstünde katman kullanıcı
  düzeyinde kuruldu (`~/.local/share/vulkan/explicit_layer.d`): masaüstü **64/64, 0 atlandı**.
- Demo, doğrulama açıkken linter raporunu sonda basar (`debug.tulpar.validation=1` / `TULPAR_ENGINE_VK_VALIDATION`).

**Kapılar:** `rhi_mali_tile_budget_rule` (ana geçiş 32+32 bit; plan zinciri 120 bit sığar; 256 bit ve 9
attachment reddedilir; ASTC biçimi hata), `renderer_mali_best_practices_gate` (tam kare: gölge + doku +
nokta ışık + UI → Arm uyarısı 0; pozitif kontrol: LOD kırpan sampler Arm uyarısı vermeli, 0→1).

**Linterin ilk koşumunda bulduğu gerçek hatalar (üçü de düzeltildi)**
1. İki sampler `maxLod` kırpıyordu (`BestPractices-Arm-vkCreateSampler-lod-clamping`) → `VK_LOD_CLAMP_NONE`.
2. Telefonda katman "ETKİN" ama mesaj kanalı yoktu: `VK_EXT_debug_utils`'i ICD listesi vermiyor, katman
   verir → katman adıyla enumerasyon; `caps.debug_messenger` yoksa doğrulama testleri geçmez (Tuzaklar 8s).
3. `compositeAlpha=OPAQUE` Huawei yüzeyinde desteklenmiyor (yalnız INHERIT) — doğrulama hatası; artık
   yüzeyin desteklediği ilk kip seçiliyor. Tanımsız davranışla çalışıyordu.

**Katmanın kendi hatası:** `BestPractices-Arm-vkCmdDrawIndexed-sparse-index-buffer` taraması alt-ayırmalı
tamponda bellek bloğunun **başını** indeks sanıyor (offset'i atlıyor, VVL issue 45); telefonda "%0.00" ×10
sahte pozitif. Aynı kural CPU'da doğru offsetle ölçülüyor (`Renderer::sparse_mesh_count`, create_mesh'te);
o 0 ise bu kimlik **açıkça** düşülür, başka hiçbir kimlik düşülmez.

**Ölçüm (telefon, 2026-09-14):** testler 64/64 (3 atlandı: GPL yok, alt süreç yok ×2); demo katmanla 0
doğrulama hatası, 0 gerçek Arm uyarısı; katmansız 600 kare **59.8 fps (FIFO)**, p50 16.8 ms (bekle 10.0 ms
vsync), kayıt 2.4 ms, 0 kare içi `new`. Strip'li `libtulparengine.so` arm64 **2.7 MB** (KARSILASTIRMA.md).

## Renk uzayı — doğrusal aydınlatma + sRGB hedef (Filament tarifi, tarama belgesi İP-A) — 2026-09-14

**Teslim edilen**
- Swapchain `*_SRGB` yüzey biçimini tercih eder (`SwapchainConfig::srgb`, `srgb_output()`); offscreen
  `OffscreenConfig::srgb` (RHI üçgen testleri UNORM'da kalır, renderer testleri ve demo sRGB).
- Dokular: albedo `R8G8B8A8_SRGB` (örnekleme doğrusal döner), font atlası/veri `UNORM`
  (`create_texture(..., srgb)`); mip blit biçim özelliklerine göre.
- Yazarın verdiği renkler (malzeme, çizim, UI `rgba`) sRGB algısal → malzeme/çizim CPU'da, UI shader'da
  doğrusala çevrilir; ışıklar doğrusal. Hedef SRGB biçimliyse donanım kodlar; UNORM yedeğinde shader
  (`light_dir.w` / UI `encode` düz varyant).
- Kapı `renderer_srgb_roundtrip_is_identity`: gri {32,128,200,255} ışıksız düz yüzeyde **aynen** geri okunur
  (ürün yolu ve yedek yol); **pozitif kontrol** kodlamasız UNORM yolunda doğrusal çıkar (4 55 147 255).

Mevcut kapılar (gölge, doku keskinliği, nokta ışık, UI metni) değişmeden yeşil; masaüstü **65/65** (katmanla),
emülatör (x86_64, gfxstream, katmanla) **65/65** — gidiş-dönüş 32 128 200 255 aynen, kontrol 4 55 147 255.
**Telefon (Mali-G72, tekrar bağlanınca):** **65/65**, gidiş-dönüş 32 128 200 255 aynen, kontrol 4 55 147 255;
demo sRGB yüzey biçimi seçti, 600 kare **59.8 fps** (FIFO), kayıt 1.9 ms, 0 kare içi `new`; ekran görüntüsü
`build-android/demo_srgb_phone.png` (görsel değerlendirme kullanıcıda). Emülatör demosu da sRGB, 60 fps.
Görsel fark: orta tonlar açılır (0.5 albedo → 188), nokta ışık sönümü fiziksel; ekran testi kullanıcıda
(`build-android/demo_srgb.png`).

## meshoptimizer + ayrık LOD (Faz 6 dilimi, tarama belgesi §13) — 2026-09-14

Vendored `engine/third_party/meshoptimizer` (v1.2, MIT). glTF yüklemesinde (`GltfLimits::optimize/lods`):
tekilleştirme → vertex cache → overdraw → vertex fetch (`content/meshopt.hpp`), ardından `meshopt_simplify`
ile %50 ve %25 LOD (bağıl hata ≤ 0.05; hedefin %90'ının üstünde kalan seviye "yok"). `UploadedModel::lod_meshes`,
`draw_model(..., ModelLod)` kameraya uzaklığa göre LOD0/1/2 seçer (istenen seviye yoksa alta düşer).

**Kapı `content_meshopt_lods_keep_silhouette`** (kure 2208 üçgen, `make_test_gltf.py`): ACMR **1.064 → 0.707**,
overdraw kötüleşmez, LOD1 3312 / LOD2 1656 indeks (hata 0.006 / 0.009), LOD2 silueti LOD0'ın **%1.5** içinde
(7480 → 7365 px); **pozitif kontrol** hata sınırı 0.0001 ile hedefe inilemez (seviye yok). Masaüstü 66/66,
telefon 66/66 (LOD2 hatası 0.0081 — sadeleştirici platforma göre son ondalıkta farklı; bilgi). Demo: üç LOD
küresi, rapor satırında `lod a/b/c` seçim sayısı; telefon 59.9 fps, 166 çizim.
The Forge SRT ilkesi (CPU-GPU tek kaynak tablosu) için ilk mekanik adım: `FrameUbo` std140 ofsetleri
`static_assert` ile derlemede sabitlendi (kayma = derleme hatası).

## Tracy (İP-R, tarama belgesi §10) — 2026-09-14

Vendored `engine/third_party/tracy` (v0.14.1 istemci, BSD-3). CMake `ENGINE_TRACY=ON` (varsayılan OFF, kapalıyken
sıfır maliyet): kendi profiler'ımızın bölgeleri (`ENGINE_ZONE`) ve kare işaretleri Tracy'ye de basılır
(`core/profiler/profiler.cpp`), fiber'lar `___tracy_fiber_enter/leave` ile bağlanır (göç olsa da bölge doğru),
worker thread adları verilir. Ayarlar: talep üzerine (`TRACY_ON_DEMAND`), örnekleme/callstack/sistem izleme/
yayın kapalı (mobilde izin yok, ağ gürültüsü yok). Araçlar `tracy-capture` + `tracy-csvexport` kaynaktan derlendi
(`~/.local/opt/tracy-tools`; sistem capstone 5 eski, CPM ile capstone 6). **Uçtan uca kapı**
`engine/tools/tracy_check.sh desktop|phone`: istemci → yakalama → CSV; `render` ve `sim` bölgeleri sayılır.

**Ölçüm:** masaüstü 475 kare / 5543 bölge (anim, nav, phys, jolt, render, sim, frame); **telefon** (Mali,
`adb forward tcp:8086`) 366 kare / 2187 bölge, demo 59.8 fps, 0 kare içi `new` (Tracy açıkken de).

**Yol boyunca dört tuzak (Tuzaklar 8t):** (1) `adb forward 8086` masaüstünde kalınca yakalama sessizce telefona
gider, iz boş; betik portu denetler ve sonda forward'ı kaldırır. (2) Dinamik srcloc (`___tracy_alloc_srcloc_name`)
csvexport istatistiğinde görünmez → ad başına statik srcloc tablosu. (3) `TracyCZoneCtx` yalnız id+aktif olarak
saklanınca `ON_DEMAND` bağlantı kimliği kaybolur, `zone_end` sessizce atlanır (5994 bölge yakalanıp hiçbiri
kapanmadı) → bağlam ham kopyasıyla saklanır. (4) `TRACY_NO_EXIT` sunucu yoksa süreci çıkışta sonsuza dek
bekletir (engine_tests asılı kaldı) → kullanılmaz; yakalama penceresi demonun içinde tutulur.

## glTF iskelet + animasyon içe aktarma ve GPU skinning (tarama belgesi §11) — 2026-09-14

**Karar: ozz-animation vendored DEĞİL.** Belge "ozz + ACL hazır" diyordu; ama `sim/animation.hpp` zaten ACL
sınıfı sıkıştırılmış klip + örnekleme + `to_model` içeriyor (PLAN §1.6 kararı: veri formatı bizim, sahne derleyicisi
üretecek). Boşluk gerçekte **içe aktarma + GPU skinning**'di; o kapatıldı. ozz yalnız karıştırma/IK/SoA gerekince
yeniden değerlendirilir (1.3 MB kaynak, STL, ayrı veri formatı).

**Teslim edilen**
- `content/gltf.cpp`: `skins` → `ModelSkin` (eklemler ebeveyn-önce yeniden sıralanır, `sim::to_model` kuralı; ters
  bind matrisleri; dönüş/ölçek `matrix` verilmişse ayrıştırılır), `animations` → `ModelClip` (kanallar 30 Hz sabit
  hızda örneklenir: LINEAR/STEP, CUBICSPLINE'da anahtar değeri; `ClipBuilder` sıkıştırır), `JOINTS_0/WEIGHTS_0` →
  `renderer::SkinnedVertex` (u8 eklem yeniden eşlenmiş, unorm16 ağırlık, toplam 1). İskeletli mesh'te meshopt/LOD
  atlanır (paralel dizi yeniden sıralanmıyor; sonraki iş).
- `renderer`: set 0 binding 4 eklem SSBO'su (kare başına, `max_skin_matrices`), push sabiti 96 B (`skin.x` ofset),
  `mesh_skin.vert` + `shadow_skin.vert` (gölge de iskeletli), statik/iskeletli boru hattı çiftleri tek
  `make_pipeline_set`'ten; `draw_skinned(mesh, mat, model, color, joints, n)`.
- `content::model_pose_evaluate(model, clip, t, PoseScratch, ModelPose)`: örnekle → model uzayı → skin matrisleri,
  ayırma yok; `draw_model(..., pose)` iskeletli instance'ları `draw_skinned` ile çizer.
- Test varlığı `tests/assets/skin_tube.gltf` (`make_test_gltf.py`): 2 eklemli boru, "bend" klibi (uç eklem 1 s'de
  Z etrafında 90°).

**Kapı `content_skinned_gltf_bends`:** içe aktarma (2 eklem, ebeveyn -1/0, klip 1.00 s, 2480 → 426 bayt, dönüş hatası
0.04°); **CPU skinning analitik** — tepe vertex t=0 (0.15, 2, 0) → t=1 (−1.00, 1.15, 0) (beklenen (−1, 1+x, z));
**GPU siluet** dik 10×64 px → bükük 37×37 px (genişlik >2×, tepe aşağıda, sol kenar sola). Masaüstü 67/67; doğrulama
katmanıyla headless demo 0 hata, 0 BestPractices. Demo: 4 boru farklı fazda (kare indeksinden, belirlenimli).
Emülatör (x86_64, gfxstream) 67/67, aynı analitik/siluet sayıları, demo 60.6 fps; ekran görüntüsü
`build-android-x86_64/demo_skin_emu.png` (borular bükülüyor). **Telefon: USB düştü, skinning Mali'de bekliyor**
(son telefon koşumu meshopt adımı 66/66).

**Tuzak (8u):** `alloc_array_zeroed<ModelMesh>` varsayılan kurucuyu çalıştırmaz — `skin = -1` varsayılanı 0 oldu,
dama küpü "skin 0" sanılıp `cgltf_accessor_read_uint(nullptr)` çöktü. Kural: zeroed dizide `-1` anlamlı alanları
açıkça yaz (ya da 0'ı "yok" yap).

## Editör iskeleti — Dear ImGui + ImGuizmo (tarama belgesi §14, PLAN L7) — 2026-09-14

**Karar:** masaüstü editör arayüzü Dear ImGui (vendored v1.92.9, MIT) + ImGuizmo; oyun içi HUD kendi 2B
çekirdeğimizde kalır (0 ayırma, telefon). ImGui `malloc` kullanır ve editör karesi 0-ayırma kapısının dışındadır;
Android'e derlenmez (`if(NOT ANDROID)`).

**Teslim edilen**
- `app/editor_ui.hpp/.cpp`: ImGui bağlamı + Vulkan arka ucu **bizim dlopen'li yükleyiciden** (`ImGui_ImplVulkan_LoadFunctions`,
  prototip yok); yüzeysiz instance'ta WSI fonksiyonları sesli çökme stub'una bağlanır (yardımcıları kullanmıyoruz);
  girdi `platform::InputState`'ten (fare, tekerlek, GLFW tuş → ImGuiKey, karakter kuyruğu — `Window`'a
  `glfwSetCharCallback` eklendi); renk subpass'inde 3B + HUD'dan sonra çizer.
- `engine_editor` (`app/editor_app.cpp`, `app/editor.cpp`): motorun düzenleme kipi — demo sahnesi + düzenlenebilir
  varlıklar (3 LOD küresi, 2 iskeletli boru); paneller: menü çubuğu (Oynat/Durdur, kare/tick/seçim), **Sahne**
  (varlık listesi), **Özellikler** (ad, konum/dönüş/ölçek, faz), **ImGuizmo** gizmo (T/R/S; Vulkan y-ters projeksiyon
  gizmo için GL gelenegine çevrilir), yörünge kamerası (sağ fare, tekerlek; ImGui üzerindeyken sahne girdi almaz).
  Headless: `--headless N --out x.ppm`, betikli durum (seçili küre, oynatma).
- Kapı `editor_imgui_draws_into_offscreen_pass`: pencere+metin+düğme karesi 114 vertex ve 24000 farklı piksel,
  boş kare 0 vertex / 0 fark (kontrol). Headless editör doğrulama katmanıyla 0 hata (tek uyarı ImGui'nin kendi
  font sampler'ı, Arm LOD kırpma — masaüstü, bilgi). Masaüstü 68/68.

**Sırada (editörün geri kalanı):** sahne veri modeli + deterministik dosya (kaydet/yükle), tıklamayla seçim
(ışın–AABB), geri al/yinele (PLAN "The Truth" işlem günlüğü), ışık/malzeme düzenleme, oynat/durdur sim geri
sarımı, implot ile kare zamanı grafiği.

## Android kare temposu — AGDK Swappy (tarama belgesi §9, İP-E) — 2026-09-14

**Teslim edilen (derlendi, varsayılan KAPALI):** `rhi::SwapchainConfig::Hooks` (on_create / on_destroy / present):
kare temposu katmanı sunumu sarar; `Swapchain` bunları yaratma/yok etme/sunumda çağırır. Android host
(`app/android_main.cpp`, `ENGINE_SWAPPY=1`): swapchain yaratılınca JNI env bu thread'e bağlanır,
`SwappyVk_initAndGetRefreshCycleDuration(env, activity->clazz, ...)`, `setWindow`, hedef 60 fps
(`SwappyVk_setSwapIntervalNS`), istatistik açık; sunum `SwappyVk_queuePresent`; sonda `SwappyVk_getStats`
(geç kare / kayma / bekleme histogramları) basılır. `engine/tools/fetch_swappy.sh` AGDK games-frame-pacing
AAR'ını (2.3.0-alpha01, Apache-2.0) Google Maven'dan indirir, prefab statik kütüphaneler gitignore'lu;
`TULPAR_SWAPPY=ON android_run.sh demo`.

**Ölçüm (2026-09-15, Huawei P20 Pro, Android 10):** ilk koşum emülatördeki gibi ilk sunumda asıldı (siyah ekran,
300 s). Kök neden **sınıf yükleyici değil**: `SwappyVk_setQueueFamilyIndex(dev, q, aile)` init'ten önce çağrılmamıştı.
A/B ile ayrıldı: (A) Java simi `classes.dex` APK'da + aile bildirilmemiş → 3 sunum "tamamlanır", sonra ana thread
binder ioctl'de (`wchan binder_ioctl_write_read`) sonsuza dek bekler (sunumlar ekrana ulaşmaz, acquire döner gelmez);
(B) dex yok + aile bildirilmiş → 300 kare, 59.8 fps; logcat'teki `couldn't find libtulparengine.so` hatası **zararsız**
(looper thread yine başlar). Düzeltme: `Hooks::on_create` kuyruk ailesini de verir; host `SwappyVk_setQueueFamilyIndex`
çağırır. `fetch_swappy.sh` gömülü dex'i `classes.dex` olarak oyar ve `android_run.sh` `TULPAR_SWAPPY=ON` ile APK'ya
koyar (hasCode=true) — gerekmediği ölçüldü ama sınıf yükleme hatasını susturur (`TULPAR_SWAPPY_DEX=0` ile kapatılır).

| yol | ort. fps | p50 | p99 | max | bekle+acquire | submit+present | kare içi `new` |
|---|---|---|---|---|---|---|---|
| FIFO (Swappy yok) | 59.8 | 16.67 | 21.3–22.1 | 23.5–26.4 | 10.5–10.9 | 2.5–3.1 | 0 |
| Swappy 60 fps | 59.2–59.6 | 16.7 | 18.2–19.7 | 18.9–21.9 | 1.9–3.0 | 9.9–11.4 | **7** (Swappy'nin kendi ayırmaları) |

p99/max ~2–4 ms iyileşir (bekleme acquire'dan sunuma taşınır); bedel: sunum yolunda kare başına 7 `operator new`
(0-ayırma kapısı Swappy açıkken tutmaz — rapor satırında görünür). **`VK_GOOGLE_display_timing` bu cihazda var ve
açılıyor** (`DeviceConfig::optional_device_extensions`, `DeviceCaps::optional_extension_enabled`); Swappy
`SwappyVk_getStats` yine 0 kare döndürüyor — sürücü tarafı değil: kendi sondamız (`debug.tulpar.dtprobe=1`,
`TULPAR_DTPROBE=1`, presentID + `vkGetPastPresentationTimingGOOGLE`) 600 sunumda 596 kayıt aldı; FIFO'da sunum
aralığı histogramı **595/595 tek periyot (0 geç kare)**, marj çoğunlukla 8–12+ ms. Swappy istatistiği açık soru
(Swappy içi; kaynak koduna bakılmadı).

**Bekçi:** `debug.tulpar.swappy=1` iken 15 s sunum ilerlemesi yoksa host takılan thread'in `/proc/self/task/<tid>/{stat,wchan,syscall}`
satırlarını basar, SIGUSR1 ile yığın ister (binder beklemesinde yanıt vermedi), `_exit(3)`. Huawei'de `abort()` →
debuggerd tombstone'u **logcat crash tamponuna düşmüyor** (0 satır); `android_run.sh` artık süreç ölünce beklemeyi keser
("SUREC OLDU"). Emülatör bu düzeltmeyle yeniden denenmedi. Varsayılan hâlâ KAPALI (`ENGINE_SWAPPY`); açma kararı
ayırma bedeli ile p99 kazancı tartılarak verilecek. Tuzaklar 8v (düzeltildi), 8y, 8z.


## Sahne veri modeli + `.sahne` dosyası + işlem günlüğü (PLAN L7 "The Truth", editörün 2. dilimi) — 2026-09-15

**Karar:** editörün tamamı tek bir veri modelinin (`content::SceneDesc`) üstünde; sim ve GPU kaynakları ondan
**türetilir**, tersi değil. Durdur = yazar dönüşümüne dönüş (sim durumu atılır), oynat = gövdeler fiziğe girer.
Dosya yazar formatıdır (L6 içerik boru hattı, offline); runtime'a derlenmiş blob sonraki dilim (PLAN §6).

**Teslim edilen**
- `content/scene.hpp/.cpp`: `SceneEntity` (ad, konum/dönüş(Euler °)/ölçek + bileşen bitleri: **model** (kaynak
  indeksi + renk), **animasyon** (klip, faz, hız), **ışık** (nokta: renk, şiddet, yarıçap), **gövde** (kutu/küre,
  dinamik/sabit)); `SceneDesc` (güneş/ortam/gölge hacmi/kamera + ≤16 kaynak + ≤256 varlık, sabit dizi — 0 ayırma).
- Metin format `.sahne`: satır tabanlı ASCII (`tulpar-sahne 1`, `kaynak "yol"`, `nesne "ad" … son`, `# yorum`,
  CRLF kabul). **Deterministik:** aynı `SceneDesc` → aynı bayt; sayılar `%.6g…%.9g` arasından **bit-tam geri
  okunan en kısa** gösterim (strtof/snprintf doğru yuvarlar → glibc/musl/bionic/Apple aynı). Hata satır numaralı
  (`SceneError{msg, line}`); bilinmeyen anahtar, yinelenen bileşen, tanımsız kaynak, NaN, kapanmamış tırnak,
  sürüm, kapasite aşımı reddedilir.
- `SceneHistory`: her değişiklik önce/sonra kopyasıyla (`Set/Add/Remove`), `undo/redo`, yeni işlem yinele kuyruğunu
  siler, halka dolunca en eski düşer. Editörde sürükleme/metin girişi **tek işlem** (ImGui `IsItemActivated` →
  kopya, `IsItemDeactivatedAfterEdit` → günlük), gizmo sürüklemesi de (`ImGuizmo::IsUsing` geçişleri); ayrık
  widget'lar kopya üstünde değişip hemen işler.
- Fizik: `scene_spawn_bodies` / `scene_remove_bodies` / `scene_body_matrix`; `DemoScene::physics()` ile aynı Jolt
  dünyası. Ekle/sil/geri al/yinele oynarken gövde indekslerini kaydırır → önce çıkar, sonra yeniden girer.
- Dönüşüm sırası `T·Rz·Ry·Rx·S` — ImGuizmo Recompose/Decompose geleneğiyle **ölçülerek** aynı (kapı; yanlış sıra
  gizmoyu her karede "düzeltir", varlık titrer).
- Editör: `--scene x.sahne` (varsayılan `tests/assets/editor.sahne`), menü **Kaydet (Ctrl+S) / Geri al (Ctrl+Z) /
  Yinele (Ctrl+Y)**, Sahne paneli **Ekle / Sil (Delete)**, Özellikler panelinde bileşen onay kutuları + alanlar;
  kirli (`*`) durumu ve son işlem çubukta. Modelsiz gövde çarpışma kutusu olarak, boş/ışık varlığı küçük işaretle
  çizilir. Kaynak yolları sahne dosyasının dizinine göre.

**Kapılar (masaüstü 79/79, doğrulama katmanıyla headless editör 0 hata):**
`scene_text_roundtrip_is_deterministic` (yaz→oku→yaz aynı bayt, bit-tam sayılar; **kontrol:** tek ulp değişince metin
değişir), `scene_parse_reports_bad_line_and_rejects_overflow` (satır 5 hatası "satir 5", 256 kabul / 257 red),
`scene_history_undo_redo_restores_bytes` (4 işlem geri = başlangıç baytları, ileri = düzenlenmiş baytlar; **kontrol:**
boş günlükte false; halka 6→4), `scene_file_editor_sahne_is_canonical` (depodaki dosya = yazıcı çıktısı; kaydet→yükle
aynı; olmayan dosya false), `scene_bodies_spawn_and_settle_in_physics` (zeminli y=0.48, **kontrol:** zeminsiz −37),
`scene_rotation_quat_matches_matrix`, `editor_scene_matrix_matches_gizmo_convention` (fark 3e-8; **kontrol:** ters
sıra 1.2). Headless editör: 8 varlık, betikli işlem + geri al (`gunluk 0/1`), 171 çizim.

**Öğrenilen:** olmayan bileşenin alanları veri değildir — eşitlik (`scene_entity_equal`) yalnız mevcut bileşenleri
karşılaştırır; ilk sürüm hepsini karşılaştırınca gidiş-dönüş kapısı düştü (Tuzaklar 8x).

**Tıklamayla seçim (aynı gün):** `SceneBounds` — yerel sınır = model sınırları (`Model::bounds_*`) ∪ gövde ∪ 0.3
işaret kutusu; `scene_world_bounds` (8 köşe), `scene_ray_aabb` (slab), `scene_pick` (en yakın t). Editörde sol tık
(geçiş; ImGui/gizmo üzerinde değilken) kamera tabanından üretilen ışınla (matris tersi yok) seçer. Kapılar:
`scene_pick_returns_nearest_hit_and_misses` (3 kutu + 45° dönmüş kutu; **kontrol:** ters yön/boşluk −1) ve headless
editörün uçtan uca **seçim kapısı**: ilk varlığın merkezi ekrana izdüşürülür, o pikselden atılan ışın aynı varlığı
seçmeli (`kure_1 piksel (584, 267) -> kure_1 OK`; yanlışsa çıkış 1).

**Sırada:** sahne → runtime blob derleyici (PLAN §6), ışık/gölge/kamera düzenleme paneli, Tulpar betik bağlaması.

## Sahne → runtime blob (`.sahneb`) + Dünya paneli (PLAN §6 "sahne bir blob + kod", editörün 3. dilimi) — 2026-09-15

**Karar:** yazar formatı (`.sahne` metni, `SceneDesc`) runtime'a **gitmez**. `engine_sahnec` (ya da editörde **Derle**,
Ctrl+B) onu `.sahneb` blob'una derler; runtime blob'u olduğu gibi belleğe alır (16 hizalı) ve başlık/tablo işaretçileriyle
okur: ayrıştırma yok, ayırma yok, kare içinde 0 `new`. Türetilmiş veri **derlemede** hesaplanır: dünya matrisi
(`T·Rz·Ry·Rx·S`, `scene_entity_matrix` ile bit-tam), kuaterniyon, ölçekli gövde boyutları (`scene_spawn_bodies` ile aynı),
ışığın dünya konumu, tüm sahnenin dünya AABB'si; bileşen tabloları (çizim / animasyon / ışık / gövde) varlık taraması
gerektirmez. `*.sahneb` gitignore'lu: türetilmiş ve deterministik, kaynağı `.sahne`.

**Teslim edilen**
- `content/scene_blob.hpp/.cpp`: format — magic `TSHN`, sürüm 1, endian işareti, toplam boyut, **FNV-1a 64 özeti**
  (özet alanından sonraki tüm baytlar), yalnız 4 baytlık alanlar (8 bayt hizalama şartı yok; özet iki u32), 16 hizalı
  bölümler, dolgu/rezerve 0 → aynı `SceneDesc` **aynı bayt**. `scene_blob_compile` (snprintf gibi gereken boyutu döner),
  `scene_blob_open` (bütünlük: magic / sürüm / endian / boyut / özet / tablo sınırı + hiza / dizin tutarlılığı — özet tek
  savunma değil, özet yeniden hesaplanmış bozuk ofset de reddedilir), `scene_blob_save` / `scene_blob_load`,
  `scene_blob_path_for`. `editor.sahne` → **1792 bayt** (8 varlık, 3 kaynak, 6 çizim, 2 animasyon, 1 ışık, 2 gövde).
- `content/scene_runtime.hpp/.cpp`: `SceneRuntime` — `init` (kaynaklar sahne dizininden glTF → GPU; yüklenemeyen atlanır,
  raporlanır), `apply_world` (güneş/ortam/gölge hacmi), `spawn` / `despawn` (gövdeler Jolt'a), `draw` (modeller LOD ya da
  animasyonla, ışıklar; dinamik gövdeli varlık sim'den), `entity_matrix`, istatistik (çizim/ışık/gövde/LOD).
- `engine_sahnec` (L7 aracı): `in.sahne [out.sahneb]`, `--check in.sahne` (derle+aç, dosya yazmaz), `--dump x.sahneb`.
- `engine_demo --scene x.sahneb` (ya da `TULPAR_ENGINE_SCENE`): küreler / borular / ışıklar / gövdeler **blob'dan**, kod
  içindeki sabit yerleşim yerine — "sahne = blob + kod": ajanlar, Jolt kutuları, oyuncu **kod**; yazar içeriği **blob**.
  Ölçüm (headless 120 kare, RTX 5080): 169 çizim (163 kod + 6 blob), 9 ışık (8 dönen + 1 blob), LOD 1/2/1, kare içi
  **0 `new`**. Sim özeti blob'la değişir (2 ek gövde aynı dünyada) — altın 600-tick özeti blob'suz yol içindir.
- Veri modeli: `SceneWorld` (güneş / ortam / gölge hacmi / yazar kamerası) ayrı struct, `SceneDesc : SceneWorld`;
  `SceneOp::World` — günlükte dünya işlemi (`SceneHistory::set_world`, bit-eşitse no-op).
- Editör: **Dünya** paneli — güneş yönü/şiddeti, ortam, gölge merkez/yarıçap/derinlik (sürükleme/metin **tek işlem**,
  geri al/yinele); kamera canlı, "sahneye yaz" bir işlem, "sahnedekine git"; ışık ve gölge hacmi **her kare veri modelinden**
  (panel canlı). Menüde **Derle** (durum satırında yol, bayt, sayılar, özet), Ctrl+B. Headless kip **derleme kapısı**:
  derle → aç → sayılar veri modeliyle tutarlı, değilse çıkış 1.

**Kapılar (masaüstü 85/85; headless editör seçim + derleme kapısı OK; clang sözdizimi 68 dosya temiz):**
`scene_blob_compile_is_deterministic_and_matches_desc` (iki derleme aynı bayt; matris / kuaterniyon / konum / ölçek /
tablolar bit-tam; ışık konumu = matris çevirisi; gövde ölçekli; **kontrol:** tek ulp → farklı bayt **ve** özet; boş sahne
224 bayt açılır), `scene_blob_open_rejects_corruption` (10 bozulma: magic, sürüm, kesik, içerik biti → özet, ofset sınır
dışı, sayı taşması, hizasız tablo, tanımsız kaynak dizini, tablo dışı bileşen dizini, endian, hizasız işaretçi, boş/kısa;
**pozitif kontrol:** bozulmamış kopya öncesinde ve sonrasında açılır), `scene_blob_file_roundtrip_and_path` (dosya =
bellek baytları; kesik dosya ve olmayan dosya red; `editor.sahne` sayıları), `scene_history_world_op_undo_redo_restores_bytes`
(dünya + varlık + dünya karışık sıra; geri al = başlangıç baytları, yinele = düzenlenmiş; **kontrol:** eşit dünya no-op),
`scene_runtime_draws_blob_entities_offscreen` (`editor.sahne` → blob → runtime: 6 çizim = instance toplamı, 1 ışık, piksel
farkı 836; **kontrol:** boş-boş 0; fizik: `kup_dusen` 2 s'de y 6.0 → −13.1, sabit duvar yazar matrisiyle bit-eşit).

**Öğrenilen:** `Renderer::stats().draws` **kayıtta** (`record`) sayılır, `draw()` çağrısında değil — kayıttan önce okunan
sayı önceki karenindir; ilk test 0 gördü ve "runtime çizmiyor" sandı. Tuzaklar 8aa.

**Sırada:** blob'a bake çıktıları (navmesh, ışık haritası; PLAN §6), telefon demosunda blob (`android_run.sh` push +
`TULPAR_ENGINE_SCENE`), Tulpar betik bağlaması.

## Tulpar köprüsü — motor C++, oyun betiği Tulpar (PLAN L5; ayrıntı: [KOPRU.md](KOPRU.md)) — 2026-09-15

**Karar (kullanıcı):** motor C++ kalır, oyun mantığı Tulpar'da yazılır; ikisi aynı ikilide, script sınırı yok.
PLAN §11 bunu zaten söylüyordu — eksik olan taşıyıcı arayüzdü.

**Teslim edilen**
- `engine/bridge/`: `engine_api.h` (79 fonksiyonluk **düz skaler** C ABI — struct/callback yok, Tulpar'ın bugünkü
  FFI'si bunu taşır), `engine_api.cpp` (tek global bağlam, nesil etiketli varlık tablosu, kare döngüsü, HUD kuyruğu,
  **log**), `bridge_host.hpp` + `desktop_host.cpp` (GLFW dlopen) + `android_host.cpp` (NativeActivity → oyunun `main`i).
- **Üretilmiş bağlama:** `engine/tools/gen_engine_bindings.py` içindeki `SPEC` tek kaynak; dört dosyayı birden yazar
  (`runtime/engine_bindings.cpp`, `src/aot/engine_builtins_table.inc`, `src/typeinfer/engine_builtins_sigs.inc`,
  `src/lsp/engine_builtins.inc`). "5 noktada bağlama" böylece **mekanik** — noktalar birbirinden kayamaz.
- AOT: `import "engine"` ya da bir `eng_*` çağrısı `backend->uses_engine`i kurar; `engine_link_flags()` motor
  arşivlerini (`--start-group`, çapraz bağımlı) link satırına ekler. Android kolunda tame yerine motor arşivleri.
- `lib/engine.tpr` (gömülü): TR + EN sarmalayıcılar, renk sabitleri, `yon_x/yon_y` (klavye **veya** joystick),
  `durum_seridi()`, `yerde()`, `yuru()`.
- `examples/engine_ilk_oyun.tpr`: zemin + 9 kutu piramidi + oyuncu topu, WASD/dokunmatik, zıplama, skor, 30 s.
- Araç: `engine/tools/build_bridge_android.sh` (iki ABI arşivi → `android/dist/<abi>/`).

**Ölçüm:** masaüstü pencersiz 120 kare, p50 13.1 ms, 11 çizim, 0 hata. **Emülatörde (x86_64) 60 fps**, p50 16.66 ms;
dokunmatik joystick yürüttü, tap zıplattı, kutu devrilince **puan 1** (skor döngüsü Tulpar'da).

**Kapılar:** `engine_tests` **86/86** — yeni `bridge_runs_a_scripted_game_headless` (kurulum, varlık, fizik
zeminli 0.480 / **zeminsiz −15.13 kontrolü**, ölü id hata sayacı **+1** / canlı id **+0 kontrolü**, kare dışı HUD reddi,
tuş adı doğrulaması, piksel farkı 37 402 / **boş-boş 0 kontrolü**). Tulpar tarafında
`tests/engine_bridge.test.tpr` 8/8 (builtin dispatch + typeinfer + sarmalayıcılar uçtan uca; GPU yoksa görünür ATLANDI).

**Öğrenilen (Tuzaklar 8ab–8ae):** ön-döndürme köprüde uygulanmayınca **3B yan yatar ama HUD düzgün görünür**
(UI yolu dönüşü zaten alıyor — yanıltıcı); Tulpar'da bit kaydırma ve onaltılık literal **yok**, `log()` doğal
logaritma (sarmalayıcı `logla`); girdi cihazı yokken erken dönen `key_down` **ad doğrulamasını atlıyordu**
(sessiz false); `install_run.sh` paket adını sabit tutuyordu.

**Döngü kapandı (aynı gün):** `examples/engine_arena.tpr` + `examples/assets/arena.sahne` — sahne editörde
hazırlanır, `engine_sahnec` ile derlenir, oyun mantığı Tulpar'da koşar. Emülatörde APK içindeki blob'dan
59–60 fps, kasa hedefe itildi (skor 1/6), 2880 karede temiz kapanış. `engine_sahnec --kanonik` elle yazılan
sahneyi yazıcının biçimine sokar. Kapıya `eng_scene_load` bölümü eklendi (sahne yüklenir, dinamik gövde
sim'den düşer / sabit yerinde kalır, ikinci yükleme ve olmayan dosya reddedilir — kontroller).

**İki gerçek hata kapıda yakalandı:** (1) profiler çalışma tamponu kare kapasitesinin iki katı olmalıydı,
köprü bir katını veriyordu — 300+ kare koşan her oturum **kapanışta** abort ediyordu (emülatörde 1984 karelik
koşum sessizce çökmüştü); aynı hata `editor_app.cpp`'de de vardı, düzeltildi (Tuzaklar 8af). (2) Android
varlıkları alt dizine montaj ediliyor ama native API alt dizin adı vermiyor; host artık JNI ile özyinelemeli
çıkarıyor (Tuzaklar 8ag).

**Sırada:** ses/animasyon/navmesh'i `SPEC`'e eklemek, çarpışma olayı (callback FFI gelene kadar sorgu),
sahne varlıklarına kuvvet uygulama, bölüm geçişi (sahneyi boşalt + yeniden yükle), telefonda köprü doğrulaması.

## Kademeli gölge (CSM) — Faz 3 kaleminin ilki, cihaz gerektirmeyen yol — 2026-09-15

**Karar:** kademeler **tek dokuda atlas** (yan yana tile), seçim **kapsamaya** göre. Kamera frustum'u derinlikten
bölünmüyor; bunun yerine iç içe kutular kullanılıyor: en yakın kademe **odak** noktasında (kameranın baktığı yer)
ve küçük yarıçaplı, en dıştaki kademe tüm gölge hacmini kapsıyor (bugünkü tek-kademe kutusunun aynısı).
Fragment shader en yakın kademeden başlayıp ışık uzayında `[0,1]` içinde kalan **ilk** kademeyi kullanıyor.

Neden frustum bölme değil: projeksiyon matrisinin tersi gerekmez. Android ön-döndürmede projeksiyon clip
uzayında döndürülüyor (Tuzaklar 8ab), frustum köşelerini oradan geri çözmek kırılgan olurdu. Oyun tanımı da
yörünge kameralı arena ölçeğinde (CIHAZ-MATRISI §1), iç içe kutular o şekle birebir oturuyor.

**Teslim edilen**
- `RendererConfig::shadow_cascades` (varsayılan **3**; 1 = eski tek kademe yolu, bayt bayt aynı yerleşim),
  `shadow_size` artık **kademe başına** tile. Atlas görüntü `(size * cascades) x size`, tek framebuffer,
  tek render pass, tek örnekleyici bağlama — dizi katmanı yok.
- Varsayılan bellek **3 x 1024 = 6 MB** (D16). Bugünkü tek 2048 harita 8 MB'tı: **daha az bellek**, yakın alanda
  ~3 kat daha ince texel (yakın kademe hacmin 1/9'u).
- `Renderer::set_shadow_focus(p)`: yakın kademelerin merkezi. Köprü, demo ve editör bunu **kamera hedefine**
  bağlıyor, yani oyuncunun etrafı her zaman en yüksek çözünürlüklü kademede.
- Kademe kutuları ışık uzayında **texel katına kenetleniyor** (`cascade_matrix`): odak kımıldadıkça gölge
  kenarlarının her karede yarım texel kayması (shadow crawl) böyle kesiliyor.
- UBO: `mat4 light_viewproj[3]` + `cascade_params` (kademe sayısı, 1/kademe, atlas texel x/y). Gölge geçişi
  kademe indeksini **push sabitinden** (`skin.y`, zaten boştu) alıyor; kayıt döngüsü kademe başına viewport/scissor
  kuruyor. std140 ofsetleri derleme zamanı `static_assert`'lerle sabit (ilk denemede boyut assert'i yakaladı).
- `mesh.vert`/`mesh_skin.vert`den `v_light_pos` varyanı kalktı: kademe seçimi piksel başına olduğu için ışık
  uzayı konumu fragment'ta hesaplanıyor.

**Kapı (yeni):** `renderer_cascades_sharpen_near_shadows` — aynı sahne üç kurulumla çizilir: referans
(tek kademe, 2048), kaba (tek kademe, 256), kademeli (3 x 256). Kademeli kurulum referanstan **8.34 kat** daha az
sapıyor (kaba 5 271 896, kademeli 632 479). **Kontrol:** kaba kurulumun gerçekten bozulduğu ayrıca ölçülüyor,
yoksa karşılaştırma boş olurdu. Mevcut `renderer_shadow_map_actually_darkens` (negatif kontrolüyle) ve Mali
linter kapısı (0 Arm uyarısı, pozitif kontrol çalışıyor) kademelerle de yeşil. Masaüstü **87/87**.

**Ölçüm:** arena örneği masaüstünde p50 13.4 ms, emülatörde **60 fps** (p50 16.66 ms) kademeli gölgeyle.

**Sırada (Faz 3, cihaz gerektirmeyen):** derlenmiş render graph, bloom mip zinciri + paketlenmiş format bütçesi,
düşük segment için stokastik tile ışıklandırma. Cihaza bağlı olanlar (bant genişliği kapısı, visibility buffer
A/B, VRS/FDM) üç gerçek telefon gelene kadar bekliyor.

## Paketlenmiş vertex formatı (Faz 3 "packed format bit bütçesi") — 2026-09-15

**Karar:** yazar tarafı (`renderer::Vertex`) düz float kalır, **yükleme anında** GPU biçimine paketlenir.
API değişmiyor: `create_mesh` hâlâ float alıyor, paketleme staging belleğine doğrudan yazılıyor (ara dizi yok).

| alan | önce | sonra |
|---|---|---|
| konum | float3, 12 bayt | float3, 12 bayt (değişmedi) |
| normal | float3, 12 bayt | **oktahedral SNORM16x2**, 4 bayt |
| UV | float2, 8 bayt | **yarım hassasiyet**, 4 bayt |
| toplam | 32 bayt | **20 bayt** (%37 daha az) |
| iskeletli | 44 bayt | **32 bayt** (%27 daha az) |

Vertex okuma bant genişliği mobilde kare bütçesinin görünür kalemi; bu, geometri başına sabit kazanç.
`R16G16_SNORM` ve `R16G16_SFLOAT` Vulkan'da **zorunlu** vertex biçimleridir, yedek yola gerek yok.
Oktahedral kodlama birim küreyi oktahedron üzerinden kareye açar; CPU tarafı `Renderer::encode_normal`,
GPU tarafı `oct_decode` (mesh.vert / mesh_skin.vert) aynı sözleşmeyi paylaşır.

**Kapı (yeni):** `renderer_packed_vertex_keeps_normals` üç şeyi ölçer. (1) Kodek hassasiyeti: kürede 2000 yönde
en büyük açı hatası **0.034 derece**; **kontrol** olarak aynı kodlamanın 8 bitlik sürümü **1.743 derece** verir
(51 kat kötü), yani eşik gerçekten hassasiyeti ölçüyor. (2) Bellek: GPU'daki vertex baytı 24 x **20**.
(3) Piksel: bilinen ışıkla aydınlatılan küpün yüzleri analitik Lambert'e oturuyor — 2112 tam parlak, 3256 karanlık,
**0 ara ton** piksel; kodlama ile çözme kayarsa düz yüzler ara tonlara dağılırdı.

Mevcut kapılar bu değişikliği aynen geçti (doku UV'si, skinning, LOD silüeti, gölge): masaüstü **88/88**.
Arena örneği masaüstünde ve emülatörde (59-60 fps) paketleme öncesiyle **görsel olarak aynı**.

## Derlenmiş render graph (ilk sürüm) + bloom — 2026-09-15

**Karar:** geçişler **veri**, kod değil. `engine/renderer/graph.hpp` içinde tablo (ad, tür, girdi/çıktı hedefi,
mip); tablo kurulumda bir kez üretilir, üretici–okuyucu eşleşmesi ve geçiş sonu layout'u oradan **türetilir**.
Kayıt sırasında çözücü, arama ya da ayırma yok — plan §6'daki "build'de derlenmiş graph" fikrinin ilk adımı.
`graph_validate` tutarsız tabloyu reddeder: üreticisi olmayan girdi, okuyucusu olmayan (ölü) geçiş, ileri bağımlılık.

Geçiş dizisi (post + gölge açık, 4 mip → **10 geçiş**):
`golge → sahne_hdr → parlak → indirge1..3 → yukari2..0 → birlestir`. Hepsi **grafik** boru hattı, compute yok
(plan §8/10). İç hedef `B10G11R11_UFLOAT_PACK32` (mobilde RGBA16F'in yarı bant genişliği), yoksa RGBA16F,
o da yoksa post kapanır ve sebep `PostInfo::disabled_reason` ile **dışarı verilir** — sessiz kapanma yok.

**Çağıran sözleşmesi korundu:** `record_shadow()` gölgeden sonra sahneyi iç HDR hedefine çizip bloom zincirini
koşturur, `record()` çağıranın geçişinde yalnız tam ekran birleştirmeyi çizer. Demo, editör ve köprü kodu
değişmeden çalışır. `RendererConfig::post` **varsayılan kapalı**: kapalıyken bugünkü yol bit bit aynı.

**Kapılar** (`engine/tests/test_render_graph.cpp`, hepsi kontrollü): post kapalıyken iki kare farkı **0 bayt**
(kontrol: post açıkken 105 632 bayt); bloom halkası **14 119 piksel** (kontrol: yoğunluk 0 → 0, eşik 100 → 0);
seçici bloom eşik 1.0'da 0 piksel (kontrol: eşik 0.05 → 10 451); tablo doğrulaması bozuk tabloyu reddediyor;
biçim/bellek raporu (256x256'da 0.67 MB). Post açık tam karede **0 Arm uyarısı, 0 doğrulama hatası**.

**Tulpar'a bağlandı:** `parlama(ac, esik, yogunluk)` / `eng_bloom` — `motor_ac`'tan önce açılır, eşik ve yoğunluk
kare içinde değişebilir. Ölçüm: aynı sahne bloom açık/kapalı **26 857 piksel** fark, halenin görünür kısmı üstte.

**Öğrenilen (Tuzaklar 8ai):** post yolu kendi temizleme rengini kullandığı için bloom açılınca gökyüzü siyaha
döndü; ilk A/B ölçümündeki 112 bin piksel farkın çoğu bloom değil arka plandı. İç hedefin temizleme rengi,
post kapalıyken kullanılan hedefin rengiyle eşitlendi, fark 26 bine indi.

## Editör: çoklu seçim, kaynak tarayıcı, gizmolar — 2026-09-15

- **Çoklu seçim:** Ctrl+tık (3B'de ve sahne listesinde), Ctrl+A tümü, Esc temizler. Gizmo ana seçiliye bağlı,
  sürüklemede ana seçilinin **konum deltası** gruba uygulanır. Bir kullanıcı eylemi günlükte **tek grup**
  (`OpGroups`): grup taşıma sürükleme bitince yazılır, grup silme büyükten küçüğe; geri al/yinele grubu birlikte işler.
- **Kaynak tarayıcı:** sahne dizinindeki `.gltf`/`.glb` dosyaları (POSIX `dirent`, **ada göre sıralı** — `readdir`
  sırası dosya sistemine bağlı, deterministik olsun diye), "Ekle" kaynağı tabloya koyar + `kSceneModel` varlık kurar
  + modeli anında yükler. Sahne kaynakları yüklendi/yüklenemedi durumuyla listelenir.
- **Gizmolar:** ışık yarıçapı ve gölge hacmi tel kutuları, güneş yönü oku — motorun kendi `draw`'u ile ince
  kutulardan, ayrı çizgi boru hattı yok. **G** ile hepsi açılıp kapanır.
- **Kapılar:** grup taşıma/silme bayt karşılaştırmasıyla (**kontrol:** tek geri al başlangıca dönmüyor, iki geri al
  dönüyor), kaynak tarayıcı (**kontrol:** olmayan dizin 0, aynı dizindeki `.png` listeye girmiyor — dosyanın varlığı
  ayrıca doğrulanıyor), gizmo çizimi 1/1/27 ve piksel farkı **0 (kapalı) / 3515 (açık)**. Headless editörde de iki
  yeni kapı; yanlışsa çıkış 1. Çizim sayısı 171 → **197**, fark tam gizmoların.

## Köprü: ses, animasyon, sahne kuvveti, bölüm geçişi — 2026-09-15

`SPEC` 79 → **108** builtin. `docs/engine/KOPRU.md` §8'deki dört boşluk kapandı:
- **Ses (13):** cihaz aç/kapat/durum, WAV-FLAC-MP3 klip yükleme, sentetik ton, çal/durdur/hepsini durdur, ana
  seviye, çalan ses sayısı, tepe genlik. Cihaz açılamazsa **oyun sessiz sürer** (hata loglanır); log seli hata
  sayacını gizlemesin diye cihaz kapalıyken gelen çağrıların ilk 3'ü HATA, sonrası ayrıntı.
- **Animasyon (6):** klip sayısı/süresi/adı, varlığa klip atama (hız, döngü), klip zamanı ve bitti mi. Poz her kare
  değerlendirilip `draw_model`'e verilir; klip zamanı çizimden bağımsız ilerler (arka planda kare atlanırsa durmaz).
- **Sahne kuvveti (6):** sahne varlıkları artık yalnız okunmuyor, itilebiliyor; dinamik olmayan gövdede hata loglanıp
  çağrı yok sayılıyor.
- **Bölüm geçişi (2):** `eng_scene_unload` + `eng_scene_loaded`; `lib/engine.tpr`'de `bolum_gec(yol)` boşalt+yükle'yi
  tek çağrı yapıyor.

**Ölçüldü:** sahne kuvveti kutu x 6.00 → **7.97** (kontrol: sabit duvar kımıldamadı, hata +1); bölüm geçişi çizim
8 → 2 → 8, gövde 4 → 2 → 4; animasyon pozlu-statik **2514 piksel** fark (kontrol: aynı poz 0 fark); ses çalan 1 /
tepe 0.25, durdurunca 0 / 0.00. Emülatörde **AAudio** açıldı, WAV klip APK'dan yüklendi, oyun 60 fps sürdü.

**Toplam kapı durumu:** masaüstü `engine_tests` **95/95**, Tulpar `tests/engine_bridge.test.tpr` **11/11**,
katman denetimi 141 dosya 0 ihlal, clang sözdizimi temiz.

## Faz 5 (Temporal) yazılım dilimi — cihaz kapısı hariç — 2026-09-15

Faz 5'in kapısı (10 dk sustained, 99p < 18 ms, üç cihaz) fiziksel cihaz ister; **yazılım tarafı** bitti ve
yerelde ölçüldü. Hepsi **varsayılan kapalı**, bugünkü yol bit bit aynı.

- **Jitter**: Halton(2,3), projeksiyonun **solundan** clip uzayı ötelemesiyle — Android ön-döndürme içeride
  olsa bile kaydırma gerçek framebuffer ekseninde kalıyor (Tuzaklar 8ab'nin tersten sınanması). Ölçüldü:
  NDC kayması dört farklı derinlikte sabit (hata 7.5e-08), 8 faz 4x4 gridde 8 ayrı hücreye düşüyor
  (**kontrol:** sabit dizi tek hücreye yığılır).
- **Hareket vektörü**: ayrı geçiş, kendi transient derinliği, RGBA16F = piksel kayması + reactive maske;
  temizleme `(0,0,0,0)` = "hareketsiz + güvenilir" (Tuzaklar 8ai'nin uygulanışı). `gl_Position` jitter'li,
  MV jitter'siz iki matristen. Ölçüldü: MV **3.254 piksel**, analitik **3.254** (tolerans 0.35);
  **kontrol:** duran nesne 0.0000, boş piksel (0,0,0).
- **Çağıran sözleşmesi korundu**: `draw(...)`/`draw_skinned(...)` sonuna `prev_model` ve `reactive`
  **varsayılan argüman** olarak eklendi. Renderer kalıcı nesne kimliği tutmuyor: çizim listesi kare başına
  sıfırlanıyor, önceki matrisi zaten tutan çağıran.
- **Dinamik çözünürlük**: hedefler en büyük ölçüde bir kez kurulur, ölçek yalnız viewport + birleştirmenin
  uv çarpanı — ölçüldü: ölçek değişiminde **0 yeni ayırma**. %50'de 96x96 (tam/4), referanstan %4.68 bayt
  sapma; **kontrol:** %100'de sapma 0.
- **Yükseltici arayüzü** `None/Bilinear/Sharpen` (Arm ASR entegrasyon noktası işaretli, SDK yok). Ölçüldü:
  %50'de üç kip birbirinden farklı; **kontrol:** %100'de None ile Bilinear 0 bayt fark.
- Graph'a `PassKind::Motion` + `GraphPass::out_external` (tüketicisi graph dışında olan çıktı "ölü geçiş"
  sayılmaz) eklendi; tablo artık post kapalıyken de kuruluyor.

**Bilinen boşluklar (uydurulmadı, sayılıyor):** iskeletli mesh'lerde MV yok (önceki eklem matrisleri
saklanmıyor — atlanan çizim `TemporalInfo::motion_skipped_skinned` ile dışarı veriliyor, kapı 0 doğruluyor);
MV hedefi dinamik çözünürlükle ölçeklenmiyor; dinamik çözünürlük `post = true` istiyor; MV hedefinin
`redundant-store x2`'si tüketici (yükseltici/TAA) yazılana kadar bilinen ve attribute edilmiş bedel.

## Faz 4 ses dilimi — uzamsal ses, DSP zinciri, oklüzyon — 2026-09-15

- **3B ses**: `Mixer::play_3d / set_listener / set_spatial / set_voice_position`. Mesafe sönümü genlikte
  1/d (enerjide ters kare), min/max mesafe ve max'ta yumuşak pencere; sabit güç panlama. Ölçüldü: mesafe
  iki katına çıkınca **−6.02 dB** (analitik −6.02); **kontrol:** sönüm kapalıyken 0.00 dB.
- **HRTF DEĞİL, basit kafa gölgesi**: karşı kulakta tek kutuplu alçak geçiren (18 kHz → 700 Hz). Yükseklik
  ve ön/arka ayrımı **yok**. Ölçüldü: 6 kHz'de kulaklar arası oran 0.3249 → **0.1311**; 200 Hz'de −0.03 dB
  (etkinin yüksek frekansa özgü olduğunun kontrolü). Gerçek HRTF yapılmadı: lisansı uygun HRIR veri kümesi
  vendor edilmedi. Doppler yapılmadı: karıştırıcıda kesirli okuma/interpolasyon yok.
- **DSP zinciri** (`audio/dsp.hpp`): 8 düğümlük sabit dizi (kazanç / tek kutuplu LP / tanh limiter), çalışma
  sırası **veri** (tek 64-bit atomik kelimede 8 yuva). Ölçüldü: LP 2 kHz'de 8 kHz **−11.91 dB** iken 100 Hz
  **−0.01 dB**; limiter sınır 0.5'te tepe 0.4734, **kontrol:** kapalıyken 0.9000.
- **Oklüzyon katman sınırında**: `audio` L3, `sim` L4 — audio, sim'i include **edemez**. Işını üst katman
  atar (`sim::Physics::raycast`, Jolt NarrowPhaseQuery — bu dilimde eklendi) ve sonucu
  `Mixer::set_occlusion(voice, 0..1)` ile parametre verir. Ölçüldü: duvar arkasında **−14.79 dB**,
  duvar kalkınca 0.00 dB. Raycast analitik: −Z küre 9.0000, +X kutu 4.0000; **kontrol:** ıskalayan ışın false.
- **Cihaz geri çağrısında 0 ayırma** (8 uzamsal ses + 3 DSP düğümü, 25 blok; pozitif kontrol 1 ayırma).
- Ses thread'i önceliği **ölçülüyor** (`pthread_getschedparam` → `DeviceInfo::thread_policy`), tahmin
  edilmiyor; SCHED_FIFO opt-in, ayrıcalık yoksa görünür atlama. Bu makinede FIFO/99 alındı.

## Faz 6 içerik boru hattı — pack, delta yama, önbellek, blob v2 — 2026-09-15

- **`.tpak` blok adreslenebilir arşiv**: 16 hizalı bloklar, dizin **ad özetine göre sıralı** (ekleme sırası
  baytları değiştirmez), **mmap** ile açılır ve okuma yolunda **ayırma yapmaz** (ölçüldü: 0; kontrol olarak
  bilerek yapılan ayırma 1 sayıldı). Bütünlük iki katmanlı: açılışta başlık + dizin + ad tablosu özeti (tüm
  dosyayı okumak mmap'i boşa çıkarırdı), blok başına özet talep üzerine. 9 bozulma türü reddediliyor.
- **Delta yama**: yama da bir pack; 20 256 baytlık arşiv için **8 528 bayt** yama (değişen 1/3 blok), uygulanan
  sonuç yeni arşivle **bayt eşit**. Kontroller: fark yokken 0 blok, her şey farklıyken 3 blok; yanlış taban red.
- **İçe aktarma önbelleği** (`.tulpar_onbellek/`): anahtar = girdi baytları + ayar bloğu + içe aktarıcı sürümü.
  İkinci çalıştırmada glTF ölçümü ve ASTC sıkıştırma **atlanıyor** (`engine_texpack` 9 ms → 1 ms, ürün bayt
  eşit); **kontrol:** girdi ya da ayar değişince atlamıyor, ürün bozulursa isabet sayılmıyor.
- **Sahne blob v2**: yerleşik küme (kaynak başına ölçülmüş CPU/GPU baytı), tepe bellek ve **derleme anında
  bake edilmiş navmesh**. Runtime artık bake etmiyor: `content::SceneNav` blob'dan sorguluyor ve sonuç
  `sim::NavMesh`'in runtime bake'iyle **bit eşit** (24 üçgen → 6 poligon, yol 5 nokta / 18.75 birim;
  **kontrol:** geçide engel konunca 2 nokta / 4.88 birim). Ölçülen yerleşik küme, testin bağımsız formülüyle
  birebir tutuyor (CPU 314 080 bayt). v1 blob açılışta anlamlı hatayla reddediliyor.
- **KTX2 kopyasız**: `ktx2_parse_inplace` ASTC bloklarını pack eşlemesinden doğrudan GPU'ya taşıyor
  (PSNR 999 dB = bit eşit; **kontrol:** kaydırılmış görüntü 5.4 dB).
- `engine_sahnec` artık ölçüm + navmesh bake yapıyor (`--hizli` kapatır), `--onbellek`, `--pack`, `--yama`,
  `--yama-uygula` alt komutları var.

**Yapılmayanlar (bu dilimde):** PSO üretimi; mağaza/servis kalemleri (Play Asset Delivery, Firebase, IAP)
hesap ister. Navmesh çorbası şimdilik yalnız sabit kutu gövdelerden çıkıyor (küreler ve model üçgenleri dahil
değil) — bilinçli sınır, kodda yazılı. **GI probe bake aynı gün kapandı** (blob sürüm 4; aşağıda kendi
bölümü) — bu satırdaki "yapılmadı" artık yalnız PSO üretimi için geçerli.

## Faz 7 — ilk oyunun dikey dilimi: "Gölge Salonları" — 2026-09-15

`examples/engine_aksiyon.tpr` (**saf Tulpar**; oyun kabuğu dilimiyle birlikte 1070 satır) + `examples/assets/salon1.sahne`, `salon2.sahne`.
Oyun tanımının (CIHAZ-MATRISI §1) oynanabilir dilimi: yörünge kamera, kameraya göre hareket, zıplama,
bekleme süreli atılma, **fizik tabanlı vuruş** (önde küre sorgusu → dürtü + hasar + savurma), oyuncu canı ve
geri tepme, **iki davranışlı düşman durum makinesi** (devriye ↔ kovalama; geçiş mesafe + görüş açısı + ışın
görüş hattı, kayıp süresiyle geri dönüş), iki bölüm ve `bolum_gec()` geçişi, iki hedef tipi (düşmanları
temizle, sonra açılan kapıya bas), HUD ve oyun sonu akışı.

**Yol bulma:** sahne blob'unda `engine_sahnec`'in Recast bake'i varsa **navmesh** (Detour düz yolu), yoksa
(kodda kurulan yedek düzen) **düz yol + ışınla engelden kaçınma**. Sebep: bake bir derleme işidir, çalışma
anında kurulmaz. İkisi de ölçüldü.

**Köprüye eklenen sorgular (SPEC 108 → 128):** `eng_raycast` (+ nokta/normal/id/sahne erişimcileri, `skip_id`
ile kendini vurmama), `eng_overlap`/`eng_nearest` (küre sorgusu, yakından uzağa sıralı; ölçüt varlığın **sınır
küresi**, zemin ve ışık sorgu dışı), `eng_nav_*` (bake edilmiş navmesh sorgusu). Çarpışma **olayı** hâlâ yok
(callback FFI yok) — savaş ve yapay zekâ bu iki sorguyla yazıldı.

**Kapılar:** Tulpar suite 11 → **14/14** (ışın 5.5 m analitik, ıska −1, `atla` kendi gövdesini geçiyor, duvar
görüşü kapatıp silinince açıyor; küre sorgusu 1.0/3.5 m sıralı, r=0.2'de hiçbiri; navmesh'siz sahnede hata
sayılıyor, salon1'de 28 poligon / 5 noktalı yol). Oyunun kendi **otopilot kapısı** pencersiz koşuyor:
`kare=3200 bolum=2 gecis=1 oldurulen=9 kalan_dusman=0 can=26 skor=900 navmesh=true hata=0 → [kapi] TAMAM`
(p50 12.9 ms). Yedek düzende: 2400 kare, `navmesh=false`, öldürülen 6, geçiş 1, hata 0.

**Öğrenilen (kodda yazılı):** Jolt'ta ışın filtresi yok; `skip_id` çarpma noktasının ötesinden yeniden atmayla
yapılıyor ve **ışın atlanacak gövdenin içinden başlarsa Jolt mesafe 0 verir** — 0.01 ilerlemek sonsuz döngü,
gövdenin sınır küresi kadar ilerlemek gerekir. Ayrıca depodaki `arena.sahneb` türetilmiş dosyası v1 kalmıştı ve
bir kapı sessizce atlanıyordu (Tuzaklar 8am).

## Faz 9 (içerik) — küme DAG builder — 2026-09-15

Mesh ~64–128 üçgenlik kümelere bölünüyor (`meshopt_buildMeshlets`; her kümede sınır küresi + normal konisi),
komşu kümeler gruplanıyor ve her grup **grup sınırı kilitli** sadeleştiriliyor (`simplifyWithAttributes` +
`vertex_lock`) → üst seviye kümeler. Hata **mutlak** (dünya birimi) ve **monoton**; aynı grubun kümeleri aynı
üst hatayı taşıdığı için cut kuralı tek eşitsizliğe iniyor: `error <= t < parent_error`. LOD cut **seçimi**
runtime'ın (cull geçişi) işi, bake yalnız veriyi üretir — planın ⚠️ REV kararı bu.

**Planın kapısı ölçüldü:** kapalı küre (9024 üçgen → 157 küme, 8 seviye, 38 grup) üzerinde 39 eşik süpürüldü,
**32'sinde cut seviye karıştırdı** ve hiçbirinde açık kenar (çatlak) çıkmadı. **Kontrol:** kenar kilidi
kapatılınca aynı süpürme **249 açık kenara** kadar çıktı — yani kapı gerçekten kilidi ölçüyor. Popping için:
grup eşiklerinin 38/38'i bit-tek değerli, yani bir grubun kümeleri hep birlikte geçiyor.

Hata metriği gerçekten sınırlıyor: bildirilen hata 0 → 0.0158 iken **örneklenen sapma** 0 → 0.0173 (oran
1.01–2.07, kapı ≤4×), en üst seviyede sapma köşegenin **%0.61'i**. **Kontrol:** hata tavanı 0.002'ye çekilince
7 seviye yerine 5 seviye üretiliyor. Determinizm: iki kurulum bayt eşit, tek vertex 1 mm oynayınca özet değişiyor.

**Blob sürüm 3**: mesh başına DAG dilimi + düğüm/indeks/çocuk tabloları; açılışta 7 yeni doğrulama (dilim
sınırları, düğüm indeks aralığı, çocuk bağlantısı, indeks < vertex sayısı, hata monotonluğu). Sürüm 1 ve 2
anlamlı hatayla reddediliyor. **Cihaz sınıfı başına bake**: `engine_sahnec --kume=dusuk|orta|yuksek`
(lod_sphere: 37/40/74 küme, 4/6/7 seviye) — CIHAZ-MATRISI §2 sınıflarına göre.

**Yapılmayanlar:** üçgen/piksel oranı ölçümü (TBDR binning bütçesi GPU geçişi ister, renderer tarafı);
iskeletli mesh'lerde DAG yok (poz başına değişir).

## Faz 9 (renderer) — GPU görünürlük elemesi + dolaylı çizim — 2026-09-15

`RendererConfig::gpu_cull` (**varsayılan kapalı**; kapalıyken bugünkü yol aynı SPIR-V ve aynı komut akışıyla
koşuyor). Çizim verisi (model, renk, yerel sınır küresi, malzeme) SSBO'da; compute geçişi frustum testini yapıp
`VkDrawIndexedIndirectCommand` dizisini ve örnek sayısını yazıyor. Graph'a `cull` geçişi eklendi (tablonun ilk
geçişi; çıktısı görüntü değil tampon olduğu için `out_external`).

**Cihaz kısıtı tasarımı belirledi:** bu yapılandırmada `multiDrawIndirect`, `drawIndirectFirstInstance`,
`drawIndirectCount` ve `shaderDrawParameters` açık değil. Bu yüzden ardışık aynı (mesh, malzeme) çizimleri bir
**küme** sayılıyor, küme başına tek dolaylı komut veriliyor ve hayatta kalanlar compute'ta **sıra koruyarak**
sıkıştırılıyor (paylaşımlı bellekte ön ek toplam — `atomicAdd` belirlenimsiz sıra verip piksel kapısını
sahteleştirirdi). Tek dispatch dört frustum'u birden çözüyor: kamera + 3 gölge kademesi, yani **gölge geçişi de
dolaylı**.

**Ölçüm:** 1024 çizimde CPU tarafı 1024 `vkCmdDrawIndexed` yerine **1** dolaylı komut veriyor; 512 eleniyor,
cull compute **0.031 ms** (25 çizimde 0.006 ms). Gölge tarafında 75 aday → 41 hayatta kalan.
**Kapılar:** cull açık/kapalı **piksel farkı 0 bayt** (post açıkken de 0), elenen sayısı `cull.hpp`'deki CPU
referansıyla birebir (17/17 — çapraz doğrulama), **kontrol:** hepsi içerideyken elenen 0, kamera çevrilince
hayatta kalan 0 ve ekran boş kareyle 0 bayt fark. Mali linteri 0 Arm uyarısı (pozitif kontrol çalışıyor).

**Sınırlar (kodda yazılı):** örnek başına LOD cut seçimi `multiDrawIndirect` açılana kadar yapılamıyor (bir küme
tek komut); iskeletli çizimler cull dışında ve CPU yolunda **sayılıyor** (`CullInfo::cpu_draws`).

## Oyun kabuğu: arayüz, kalıcı kayıt, sahne sıcak yükleme — 2026-09-15

Köprü 128 → **156 builtin**. Ayrıntı `KOPRU.md` §8; ölçümler: arayüzlü kare ile boş kare arası **44 800
piksel** fark (**kontrol:** iki boş kare arası 0), düğme dışına tıklama tetiklemiyor ve sayaç artmıyor,
kaydırıcı devre dışıyken değer değişmiyor. Kalıcı kayıt iki koşum arasında taşındı (ses 0.25, rekor 900).
Sıcak yükleme: dosya değişmezken 40 karede yanlış pozitif yok, değişince **bir kez** olay ve varlık sayısı
13 → 3; oyun koşarken `.sahneb` dokunulduğunda k84'te değişim görüldü, k90'da yüklendi ve oynanış kesilmedi.

"Gölge Salonları" artık ana menü, duraklat ve ayarlar taşıyor (ses seviyesi, parlama, motor bilgileri);
ayarlar ve rekor kalıcı. Pencersiz koşumda menü enjekte tıklamayla geçiliyor, oyun sayıları menüsüz koşumla
birebir aynı kalıyor. Tulpar suite 14 → **17/17**.

## Faz 4 UI dilimi — SDF atlası, tek batch, retained yerleşim, overdraw ölçümü — 2026-09-15

PLAN Faz 4'ün UI maddesi tek tek yazılmıştı: "SDF font atlası, tek batch çizim, retained layout (yalnız
dirty'de hesap), opak önce / blend sonra, tam ekran şeffaf katman yasak". Bu dilim o maddeyi kapatıyor;
**kuyruk hâlâ immediate-mode**, değişen şey kayıt anında kurulan SIRA ve ölçüm yüzeyi.

| parça | yer | not |
|---|---|---|
| Sıralama kipi | `UiSortMode{Source, OpaqueFirst, BlendFirst}` | `OpaqueFirst` **güvenli** opak yükseltme: bir opak dörtgen yalnız kendinden önceki ve akışta kalan hiçbir dörtgenle **örtüşmüyorsa** öne alınır ve harmanlamasız çizilir (tile belleğinden okuma yok). `BlendFirst` **kontrol kipi**: bilerek yanlış sıra |
| Atlas gruplama | `ui_group_by_atlas` | atlasa göre **kararlı** sıralama, yalnız güvenliyse (ayrık bölgeler); en çok 8 atlas grubu |
| Retained blok | `ui_block_begin(id, hash)` / `ui_block_end()` | `true` = blok kirli (içeriği üret), `false` = önceki karenin dörtgenleri önbellekten kuyruğa kopyalandı ve **içerideki kod koşmaz**. `ui_begin`de ekran ölçüsü/döndürmesi değişirse bütün bloklar geçersizlenir |
| Overdraw | `ui_record_overdraw` + `ui_overdraw_measure` | tahmini alan toplamı DEĞİL: aynı dörtgen akışı "her fragment +1" boru hattıyla UNORM hedefe çizilir, geri okunan R baytı o pikseldeki **fragment sayısıdır**. `saturated > 0` ise sayım eksik — sessiz değil |
| Tam ekran harmanlı katman | `ui_warning()`, `UiStats::fullscreen_blended` | TBDR yasağı: ekranı kaplayan harmanlı katman sayılır ve sebebiyle dışarı verilir |
| Süre | `ui_timing_reset` / `ui_fetch_stats` | `cpu_gen_ms` (dörtgen üretimi), `cpu_build_ms` (sırala + vertex yaz + komut), `gpu_ms` (zaman damgası). Damga okunamazsa `gpu_timing_reason` yazılır |
| SDF örnekleme | `ui_set_sdf(bool)`, `ui_sdf.frag` | boru hattı **tembel** yaratılır; kurulamazsa harmanlı yola **düşer** ve sebep `UiStats::sdf_reason`'a yazılır (sessiz kapanma yok) |
| SDF atlas üretimi | `content/font.*` | `font_build_sdf_atlas` (raf paketleyici, sığmazsa kenar 2048'e kadar ikiye katlanır), `Font::load_sdf`; shader'ın ihtiyaç duyduğu iki sayı dışarı verilir: `on_edge` ve `pixel_dist_scale` → `alpha = clamp(0.5 + (d − on_edge) · 255 / (pixel_dist_scale · fwidth_px))`. Bitmap ve SDF atlasları **aynı kod noktası listesini** kullanır (Türkçe dahil), iki yol karşılaştırılabilir kalsın |
| SDF kalite ölçümü | `font_measure_sdf_quality` | **GPU gerekmez**: aynı glifi aynı hedef çözünürlükte iki yolla yeniden kurar (bitmap kapsama bilineer büyütme vs SDF uzaklık + türev genişliği) ve yüksek çözünürlüklü **gerçek** rasterle karşılaştırır. `edge_px` = geçiş bandındaki piksel / gerçek siluetin çevresi — küçük = keskin |

**Kapılar (`engine/tests/test_render_graph.cpp` + `test_content.cpp`, hepsi kontrollü):**
- `renderer_ui_opaque_first_is_pixel_identical` — opak-önce sıralama kaynak sırasıyla **piksel aynı**
  (en büyük kanal farkı ≤ 1), iki panel güvenle öne alınır ve **batch sayısı düşer**; **kontrol:**
  `BlendFirst` (bilerek yanlış sıra) 500 bayttan fazla fark üretmeli, yoksa "sıralama önemli" iddiası boş
  olurdu. Ayrıca **güvenlik denemesi:** panel metnin üstüne konunca yükseltme **engellenir** (taşınan 0,
  engellenen 1 — sessizce değil, sayılarak) ve piksel farkı 0 kalır.
- `renderer_ui_batches_group_by_atlas` — tek atlastan N dörtgen **tek** batch (1 atlas grubu); iki atlas
  **ayrık** bölgelerdeyse atlasa göre kararlı sıralanır → atlas başına 1, toplam 2 batch; iki atlas
  **üst üsteyse** sıralama yapılmaz ve batch sayısı N'e çıkar — **doğruluk batch'ten önce gelir**.
- `renderer_ui_overdraw_is_measured` — üst üste binen dörtgenlerin ölçülen `shaded/covered` oranı;
  **kontrol:** aynı sayıda **ayrık** dörtgen.
- `renderer_ui_frame_budget_under_1_5_ms` — 1280×720, 2000 opak HUD kutusu + 5000 glif ölçüsünde harmanlı
  dörtgen = **7000 dörtgen**, **2 batch** (opak koşu + harmanlı koşu), **0 düşen**; GPU + CPU toplamı
  **< 1,5 ms** (PLAN §4 kare bütçesindeki UI kalemi). **Kontroller:** 1/10 yük belirgin daha kısa sürmeli,
  ve zaman damgası gerçekten okunmuş olmalı (`timed`) — damgasız "süre" iddiası boştur.
- `renderer_ui_retained_block_skips_layout` — 2. karede blok önbellekten gelir, yerleşim hesabı **koşmaz**;
  önbellekten çizilen kare ile hesaplanan kare arasında **0 bayt** fark; **kontrol:** içerik değişince blok
  yeniden üretilir.
- `renderer_ui_warns_on_fullscreen_blended_layer` — tam ekran harmanlı katman uyarı verir; **kontroller:**
  yarım ekran harmanlı ve tam ekran **opak** uyarı vermez.
- `renderer_ui_sdf_sampling_keeps_edge_sharp` — SDF boru hattının örnekleme yolu (atlas üretimi ayrı kapı).
- `content_sdf_atlas_is_sharper_when_magnified` — DejaVuSans `'B'`, 32 px atlas, **8× büyütme**: SDF kenar
  bandı bitmap'in **dörtte birinden dar** olmalı. Siluet sadakati ayrıca sınanır: ölçülen (2026-09-15)
  yanlış piksel oranı **SDF 0.157 / bitmap 0.147** — yani SDF hafifçe **daha yüksek**, ve bu beklenen:
  bulanık kenarda yanlış sınıflandırma yumuşak geçişe yayılır, keskin kenarda her konum hatası tam bir
  yanlış piksele döner. Kapı bu yüzden eşitlik değil **yakınlık** arar (`sdf_error < bitmap_error · 1.25`);
  SDF çözme bozulursa (yanlış `on_edge`, yanlış ölçek) bu oran patlar. **Kontroller:** 1× büyütmede
  keskinlik kazancı 8×'tekinden küçük olmalı; olmayan dosya sessizce başarılı dönmemeli.

**PLAN Faz 4 kapısı** ("UI overdraw ölçülmüş; UI kalemi kare bütçesinde < 1,5 ms") bu dilimle **kapandı**.
Açık kalan Faz 4 maddesi: çok dilli metin **shaping** (bugün UTF-8 çözme + glif başına ilerleme var,
Harfbuzz sınıfı shaping yok).

## Faz 6 GI — sonda bake'i (ambient cube) + sahne blob sürüm 4 — 2026-09-15

PLAN Faz 6 "scene compiler: resident set, peak memory, PSO üretimi, **GI probe / navmesh bake**" diyor;
navmesh bake'i blob v2'de gelmişti, GI sondası bu dilimde geldi. Oyun tanımı (CIHAZ-MATRISI §1) "PBR yok,
basit BRDF + **bake GI**" dediği için statik ışık **derleme anında** CPU'da ışın izlemeyle çözülüyor;
runtime hesaplamıyor, yalnız dünya konumundan **okuyor** (`content::SceneGi`).

**Gösterim kararı — SH-L1 değil, 6 yönlü ambient cube** (sonda başına 6 × RGB = 72 bayt; SH-L1 48 bayt
olurdu). Gerekçe kodda yazılı: (1) **halka (ringing) yok** — güçlü tek yönlü bir güneş SH-L1'e sığmaz, lob
ters tarafta negatife düşer, kırpınca enerji kaybolur ve "güneşe dönük yüz vs ters yüz" ölçümü göstergenin
**kendi hatasını** ölçmeye başlar; ambient cube'da her yüz bağımsız, negatif olamayan bir integraldir.
(2) Çözüm 3 çarpma-topla (n² ağırlıklı), Mali/Adreno'da SH-L1'in 4 nokta çarpımından ucuz, kırpma/koruma
kodu gerekmez. (3) İnterpolasyon yüz yüz doğrusal kalır. Bedeli: sonda başına 24 bayt ve yumuşak alanlarda
hafif kare etkisi — mobil oyun sahnesi hedeflendiği için kabul edildi.

**Birim sözleşmesi:** yüz değeri `E(n)/π`, yani "albedo 1 olan Lambert yüzeyin o yönde vereceği renk" —
`mesh.frag`'in ışık terimiyle **aynı** sözleşme. Bake, hangi terimlerin sonda alanına dahil edildiğini
`gi_flags` ile söylüyor (`kGiDirectSun`, `kGiPointLights`, `kGiModelTris`, `kGiBounce`); runtime çift sayım
yapmamak için bayraklara bakmak zorunda. Uygulama `content/gi.hpp` + `content/scene_blob.cpp` (scene_compile
ile aynı TU: motorun CMake kaynak listesi elle tutuluyor).

**Blob sürüm 4:** GI sonda bölümü eklendi (tablo üst sınırı 32 768 sonda, en çok 64 ışık). Sürüm 1/2/3
anlamlı hatayla reddediliyor — "yeniden derleyin: engine_sahnec" diyerek.
`engine_sahnec --gi` (`--gi-isin N` ışın sayısı, `--gi-adim S` sonda aralığı, `--gi-sicrama N` sıçrama,
`--gi-modelsiz` model üçgenlerini tıkayıcı saymaz) bake'i açıyor.

**Kapılar (`engine/tests/test_scene_blob.cpp`, 7 kapı; hepsi kolu kapatıp aynı ölçümü tekrarlayan
kontrollerle):**
- `gi_open_sky_probe_is_brighter_than_covered_and_sun_is_the_cause` — açık gökyüzü altındaki sonda çatı
  altındakinden belirgin parlak; **kontrol:** güneş şiddeti 0'a çekilince aynı oran çöker (kalan fark
  yalnız gökyüzü tıkanması).
- `gi_sun_facing_face_is_brighter_and_symmetric_without_sun` — güneşe dönük yüz parlak; **kontrol:** güneş
  yokken yüzler simetrik.
- `gi_wall_blocks_direct_sun_and_removing_it_restores` — duvar doğrudan güneşi keser, duvar kalkınca geri gelir.
- `gi_bake_is_deterministic_and_geometry_changes_it` — iki bake bit eşit; **kontrol:** geometri değişince özet değişir.
- `gi_blob_roundtrip_is_bit_exact_and_rejects_corruption` — blob gidiş-dönüşü bit tam, bozulma reddediliyor.
- `gi_runtime_query_matches_baked_values` — runtime sorgusu bake edilen değerleri veriyor.
- `scene_compile_bakes_gi_probes_with_model_triangles` — model üçgenleri tıkayıcı olarak devrede
  (`kGiModelTris`).

## Web ve Android hedefleri onarıldı + paket/tazelik denetimleri — 2026-09-15

Motor köprüsü masaüstünde ve Android'de çalışırken **web hedefi tamamen kırıktı ve bunu hiçbir kapı
söylemiyordu**. Üç ayrı sessiz hata üst üste binmişti (Tuzaklar 8aj, 8ak, 8ao):

1. **Denetimin kör noktası (8aj):** `tests/dist_archive_audit.py` yalnız `aot_tm_*` (tame) tablosuna
   bakıyordu. Ölçüldü: denetim **"dist arsiv denetimi temiz"** derken `tulpar build --target=web`
   **her** oyunda `undefined symbol: aot_intern_string` ile düşüyordu — çekirdek runtime sembolü tablonun
   dışındaydı. Aynı kör nokta Android'de `aot_http_request`'te vardı (skor tablosu kullanan her Android
   derlemesi link'te ölecekti).
2. **wasm32'de işaretçi 4 bayt (8ak):** codegen dizi erişiminin hızlı yolunu satır içi GEP ile yapıyor ve
   `ObjArray` düzenini kendi kuruyordu — başlık dolgusu **sabit 28 bayt**, yani 64-bit varsayımı. wasm32'de
   `Obj` 20 bayt. Üstelik `backend->target_web` **`llvm_init_types`'tan SONRA** atanıyordu: tip gövdesi
   kurulurken bayrak hep 0 görünüyordu.
3. **`runtime_net.cpp` web runtime'ında yoktu** — `aot_http_request` (arcade skor tablosu) tanımsızdı.

**Düzeltilenler**
- `src/aot/llvm_types.cpp`: `ObjArray` başlık dolgusu hedefe göre (`target_web ? 16 : 28`); düzen kilidi
  artık iki işaretçi boyutunu ayrı ayrı sabitliyor.
- `src/aot/llvm_backend.cpp`: `backend->target_web` **`llvm_init_types`'tan önce** kuruluyor (yorumu da
  kodda: "MUTLAKA llvm_init_types'tan ÖNCE").
- `wasm/build_tame_web.sh`: `src/vm/runtime_net.cpp` runtime kaynak listesine eklendi.
- `tests/dist_archive_audit.py` genişletildi: artık **codegen'in adıyla bildirdiği** sembol kümesini
  (`LLVMAddFunction` literalleri + tame/motor tabloları + `engine_builtins_table.inc`) hedefin arşivine
  karşı denetliyor. `nm` çıktısında **tür harfi `U` olan satır TANIMSIZ demektir**; onu "var" saymak
  denetimi sahte yeşile çevirirdi — sembol taraması artık yalnız tanımlı sembolleri alıyor. Hedefte
  bilerek olmayan aileler (async → ucontext yok; TLS → OpenSSL yok) **sebebiyle** listeleniyor, sessizce
  yok sayılmıyor. `aot_tm_*` / `aot_eng_*` aileleri kendi arşivlerinde arandığı için çekirdek denetiminin
  dışında tutuluyor.

**Yeni: `tests/paket_boyut_audit.py`** — üçü de "sessizce bozulan" sınıfından olduğu için tek denetimde:

| kalem | eşik | gerekçe (ölçülen) |
|---|---|---|
| `libtulpar_runtime.a` | 8 MB | ölçülen 3.28 MB |
| `wasm/dist/libtulpar_runtime_web.a` | 5 MB | ölçülen 2.06 MB |
| `wasm/dist/libtulpar_tame_web.a` | 5 MB | ölçülen 1.84 MB |
| `android/dist/arm64-v8a/libtulpar_runtime_android.a` | 7 MB | ölçülen 2.72 MB |
| `android/dist/arm64-v8a/libtulpar_engine_android.a` | 5 MB | ölçülen 1.76 MB |
| motor arşivleri toplamı (arm64 / x86_64) | 160 MB | ölçülen ~73 MB / ~66 MB |
| üretilmiş `.wasm` | 8 MB | arcade oyunu ~1.7 MB |
| üretilmiş `.apk` | 96 MB | arena APK ~62 MB (iki ABI, striplenmemiş) |
| açılış (pencersiz, tam tur) | 6.0 s | AOT derleme + motor kurulumu + 1 kare |

Eşikler ölçülen değerin **yaklaşık iki katı**: amaç "bir gün büyüdü" değil, "bir anda **zıpladı**" demek.
Boyut kalemi PLAN EK G.3'ün bütçesi (temel modül < 200 MB); açılış kalemi PLAN Faz 7'nin eşikli perf
listesinden. **SPIR-V tazelik:** her GLSL kaynağı yeniden derlenip depoya giren `*_spv.h` ile karşılaştırılır
(21 shader kaynağı) — kaynak değişip başlık üretilmezse derleme yeşil kalır ve **GPU eski shader'ı koşturur**;
bu aynı zamanda PLAN Faz 6 kapısının ("runtime'da shader derlemesi yok") kanıtıdır. `glslc` yoksa en azından
karşılığın **varlığı** denetlenir.

**Denetimin kendi tuzağı (8ao):** SPIR-V tazelik denetiminin ilk sürümü üretilmiş başlıkları **bayt** dizisi
sanıp taradı; başlıklar 32-bit **kelime** tutuyor (`0x%08x`). Regex hiç eşleşme bulmadı ve denetim 21
shader'ın 21'ini birden "BAYAT" ilan etti — çıktı tamamen ikna ediciydi. Kural: yeni bir kapının **ilk
sonucu** makullük sınavından geçmeli; "hepsi bozuk" ile "hiçbiri bozuk değil" aynı şüpheyi hak eder.

**Otomasyona bağlandı (2026-09-15).** Yeni bir denetimin otomasyona bağlanmaması tam olarak bu projenin
`assert`-hiç-düşmez sınıfı olurdu (CLAUDE.md, Testing.md), o yüzden ikisi de `build.sh suites` içinde
koşuyor ve **kırmızı denetim suite'i düşürüyor**: `build.sh:276` (`dist_archive_audit.py`) ve
`build.sh:292` (`paket_boyut_audit.py`), her ikisi de `if ! python3 ...; then ... exit 1; fi` biçiminde.
Kontrol edildi: bir `*_spv.h` bozulduğunda bölüm çıkış kodu 1 veriyor ve sonraki adıma geçmiyor.
Elle koşum hâlâ mümkün: `python3 tests/paket_boyut_audit.py [paket_dizini...]`.

## Oyun emülatörde doğrulandı — ve yolda bir derleyici hatası çıktı — 2026-09-15

"Gölge Salonları" (`examples/engine_aksiyon.tpr`) ilk kez bir Android yüzeyinde koştu: Android 16 x86_64
emülatörü, Goldfish GFXStream. Ekranda ana menü (Başla / Ayarlar / Çıkış), arkasında `salon1.sahneb`'den
yüklenen sahne — 13 varlık, 11 çizim, 2 ışık, 10 gövde, 28 poligonluk navmesh — kademeli gölgeler ve
bloom. Ses AAudio ile açıldı (48 kHz, 2 kanal). APK 71 MB (iki ABI, striplenmemiş), varlıklar
`TULPAR_ANDROID_ASSETS=examples/assets` ile paketlendi.

### Yolda çıkan iki hata

**1. Depo kökündeki `./tulpar` bayattı.** `lib/engine.tpr` yeni sarmalayıcılar (`ui_basla`, `ui_bitir`,
`rekor_guncelle`) kazandı, `src/embedded_libs.h` yeniden gömüldü, ama kökteki ikili kopya eski kaldı.
Stdlib ikilinin *içine* gömülü olduğu için derleme "`rekor_guncelle` adında bir fonksiyon bulunamadı"
diyordu — örnek doğruydu, ikili bayattı. Belirti insanı örneğe baktırır; bakılacak yer ikilinin tarihidir.

**2. Aynı `LLVMModule`'den iki ABI üretmek ikincisini bozuyordu** (asıl bulgu; tuzak kaydı
[Tuzaklar](../mindmap/Tuzaklar.md) 8ap). Uygulama ilk karede SIGSEGV veriyordu. Motorun kendi çökme
raporu faili tam yerinden söyledi — `t_menu_ciz.f+576` — ve disassembly geri kalanı verdi: 16 bayt hizalı
`movapd`, 8 mod 16 olan `0x48(%rsp)` yuvasına yazıyor. Masaüstü ikilisinde aynı fonksiyon `0x40(%rsp)`
kullanıyor. Kanıt tek komutla kapandı: aynı optimize IR `llc -mtriple=x86_64-linux-android34
-relocation-model=pic` ile **tek başına** derlendiğinde doğru yuvayı üretti. Fark yalnızca şuydu: sürücü
o modülü daha önce **arm64 için bir kez emit etmişti**. `LLVMTargetMachineEmitToFile` saf bir okuma
değildir — CodeGen boru hattı modülü yerinde değiştiren IR geçişleri içerir — yani ikinci emit,
birincinin alçaltılmış IR'i üzerine biniyordu.

Düzeltme: `emit_object_with_triple` artık her hedef için `LLVMCloneModule` üstünde çalışıyor
(`src/aot/llvm_backend.cpp`). Doğrulama: yeniden üretilen x86_64 nesnesi `0x40(%rsp)` kullanıyor —
tek başına `llc` referansıyla **birebir aynı** — ve uygulama emülatörde açılıyor.

**Kapı:** `src/aot/aot_pipeline.cpp` Android kolunda, ABI döngüsünün öncesinde ve sonrasında modülün
IR'inin FNV-1a özeti alınıp karşılaştırılıyor; değişmişse sürücü `AOT_ERROR_EMIT` ile duruyor. Kontroller:
temiz ağaçta çıkış 0; klonlama kaldırılıp yeniden derlendiğinde çıkış **1** ve mesaj basılıyor.

**Kapsam uyarısı:** bu yalnızca motor örneğini değil, **Android x86_64 için üretilen her şeyi**
ilgilendirir — tame/arcade oyunları dahil. İlk ABI (arm64, gerçek telefon) her zaman doğru üretiliyordu,
o yüzden telefonda yapılan doğrulamalar geçerliliğini koruyor; şüpheli olan emülatör (x86_64) tarafıdır
ve o taraf bu düzeltmeden sonra yeniden üretilmelidir.

## Faz 3 kapanış durumu — ne kapandı, ne açık (2026-09-15)

**Kapı sayımı (kaynaktan):** `engine/tests/*.cpp` içinde **141 `ENGINE_TEST` bloğu** tanımlı (masaüstü);
Android'de editör (5) ve köprü (1) kapıları derlenmediği için **135**. Tulpar tarafında
`tests/engine_bridge.test.tpr` **17** test. Katman denetimi (`engine/tools/layer_check.py`): **159 dosya,
0 ihlal**. Köprü `SPEC`'i **156 builtin** (= `engine/bridge/engine_api.h`'deki `teng_*` sayısı; ikisi
birbirine karşı denetlenebilir).

**Tam koşu (2026-09-15 akşamı, bu belgedeki bütün dilimler dahil):** masaüstü `engine_tests`
**141/141, 0 atlandı**; Tulpar suite'leri **81/81** (1295 iddia, `Tests:` satırı eksik suite yok),
`tests/engine_bridge.test.tpr` **17/17**; örnekler **102/102**. Pencersiz motor koşumlarının çıktıları
tek renk değil — `engine_ilk_oyun` 1178, `engine_arena` 3883, `engine_aksiyon` 4823 ayrı renk
(1280x720; en sık renk payı sırasıyla %31, %41, %18).

**Faz 3'te kapananlar (cihaz gerektirmeyen yol):** kademeli gölge (CSM, tek atlas), paketlenmiş vertex
formatı (32 → 20 bayt), derlenmiş render graph + bloom/post, kümelenmiş nokta ışıklar, doku/malzeme/glTF,
sRGB doğrusal aydınlatma, GPU skinning, Mali linteri + tile bütçesi.

**Faz 3'te AÇIK kalanlar:**
- **Faz 3 kapısı: "bandwidth < 8 GB/s ölçülmüş (3 cihaz)"** — elde **bir** cihaz var (Huawei P20 Pro,
  Mali-G72). Üç fiziksel cihaz (Mali + Adreno + düşük segment) gerekiyor; **kullanıcı kararı: şimdilik pas**.
  Aynı kapıya bağlı: visibility buffer A/B ölçümü, VRS/FDM baseline, stochastic tile ışıklandırma
  (düşük segment), Adreno tile memory heap yolu.
- **PBR** — oyun tanımı "PBR yok, basit BRDF + bake GI" dediği için bilinçli ertelendi; GI sondası geldi.
- **PSO üretimi** (Faz 6 scene compiler maddesi).
- **Faz 5 cihaz kapısı** ("10 dk sustained, 99p < 18 ms, 3 cihaz") — yazılım tarafı bitti, kapı cihaz bekliyor.
- **Faz 8 (Tulpar shader stage)** — önkoşul PLAN §11'in dil alt kümesi (kutusuz struct, işaretçi, atomik);
  o gelene kadar shader'lar GLSL'de yazılıyor. Başlanmadı.
- **Faz 10 (Metal / iOS)** — Mac ve geliştirici hesabı gerekiyor. Başlanmadı.
- **Mağaza ve servis kalemleri** (Play Asset Delivery, Firebase crash/analytics, uygulama içi güncelleme,
  bulut kayıt, UMP/rıza, IAP) — hepsi **hesap** gerektiriyor.
- **macOS CI'da `engine_tests` çökmesi** — yerelden ulaşılamıyor; teşhis altyapısı hazır, yanıt bir
  sonraki macOS koşusunda gelecek (bu belgede kendi bölümü var).
