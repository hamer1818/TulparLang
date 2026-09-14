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

**Ölçüm:** emülatörde (API 37) init başarılı (yenileme 16.67 ms) ama **ilk sunumda asılı kalıyor** (300 s):
Swappy'nin bellekten yüklediği Java simi (`SwappyDisplayManager`, API ≥ 30 yolu) `System.loadLibrary("tulparengine")`
ile kendi doğal metotlarını bağlamak istiyor; `InMemoryDexClassLoader`'ın `nativeLibraryDirectories`'i yalnız sistem
dizinleri → "couldn't find libtulparengine.so" → vsync callback'i gelmez → `SwappyVk_queuePresent` bekler.
Denenen: `hasCode=true` + boş `classes.dex` (javac+d8) — değişmedi; geri alındı. **Huawei P20 Pro Android 10
(SDK 29)** Swappy'de Java sim yolunu **kullanmaz** (NDK Choreographer) → telefonda çalışması beklenir; ölçüm
(p99/max, geç kare histogramı, FIFO ile A/B) telefon bağlanınca. Tuzaklar 8v.

