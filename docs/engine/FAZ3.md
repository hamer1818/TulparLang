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

