# Tulpar Engine — Durum Özeti (2026-09-15 akşamı, sonraki aşama için)

> Tek sayfada "ne var, ne ölçüldü, ne yok". Ayrıntı: FAZ0–FAZ3.md, KOPRU.md, CIHAZ-MATRISI.md, PLAN.md.
> Kural: her sayı ya masaüstünde (RTX 5080, Linux) ya telefonda (Huawei P20 Pro, Mali-G72) ölçüldü.

## 1. Ne var (katman katman)

| katman | içerik | durum |
|---|---|---|
| L0 platform | zaman, bellek, thread, çökme raporu, **GLFW dlopen pencere**, **dokunmatik durum** | ✅ |
| L1 core | arena ailesi + slotmap, **fiber job sistemi** (x86_64 + AArch64 asm), profiler (+ **Tracy** istemcisi, seçenek), math, konteynerler, `AllocGate` | ✅ |
| L2 rhi | Vulkan **dlopen**, `Device` (iki aşama, yüzey), **Swapchain** (ön-döndürme, sunum kipi, **sunum kancaları**), offscreen, PSO cache, `allocate_dedicated`, **Mali linter** (BestPractices+Arm), **tile bütçesi** (kodla zorlanır) | ✅ |
| L3 renderer (+ **derlenmiş render graph**, **bloom/post**, **temporal** ve **GPU cull + dolaylı çizim**, hepsi varsayılan kapalı) | depth prepass → renk; **kademeli gölge** (CSM: 3 kademe tek atlas, kapsamaya göre seçim, texel kenetleme, odak = kamera hedefi; D16, PCF, dünya-uzayı normal eğilimi); **doku + malzeme** (klasik descriptor set, mip blit); **8–32 kümelenmiş nokta ışık** (CPU atama); **2B arayüz** (tek batch, opak-önce, retained blok, **SDF örnekleme**, ölçülmüş overdraw); **doğrusal aydınlatma + sRGB hedef**; **GPU skinning** (gölge dahil), **paketlenmiş vertex** (oktahedral normal + yarım UV: 32 -> 20 bayt); **jitter + hareket vektörü + dinamik çözünürlük** | ✅ ilk dilim |
| L3 audio | **miniaudio** cihaz (AAudio/Pulse/ALSA/CoreAudio/null), kilitsiz **karıştırıcı** (32 ses, 0 ayırma), klip yükleme, **3B uzamsal** (mesafe sönümü + panlama + kafa gölgesi), **DSP zinciri** (kazanç/LP/limiter), **oklüzyon** (fizik ışını üst katmandan) | ✅ ilk dilim |
| L4 sim | archetype ECS, sistem zamanlayıcı, sabit adım + replay, **Jolt** fiber job'larda, **Recast/Detour** navmesh, sıkıştırılmış animasyon | ✅ yazılım tarafı |
| L6 content | **glTF 2.0** (cgltf + stb_image; mesh, malzeme, doku, **iskelet + animasyon**), **meshoptimizer + ayrık LOD**, **KTX2 + ASTC** (astc-encoder; `engine_texpack`), **font atlası** (stb_truetype, Türkçe), **sahne veri modeli** (`.sahne` deterministik metin, işlem günlüğü geri al/yinele, gövde kurulumu), **SDF font atlası** (+ GPU'suz kalite ölçümü), **derlenmiş sahne blob** (`.sahneb` **sürüm 4**: deterministik, bütünlük denetimli; yerleşik küme + bake edilmiş **navmesh** + **küme DAG** + **GI sondaları**; `SceneRuntime` tüketici — ayrıştırma/ayırma yok), **`.tpak` pack + delta yama + içe aktarma önbelleği** | ✅ ilk dilim |
| L6 bridge | **Tulpar köprüsü** (`engine/bridge/`: **156** fonksiyonluk düz skaler C ABI — yaşam döngüsü, dünya/kamera, sahne + sıcak yükleme, varlık/fizik, girdi, HUD, **anlık-kip arayüz**, **kalıcı kayıt**, ses, animasyon, bloom, **ışın/örtüşme/navmesh sorguları**; üretilmiş bağlama, masaüstü + Android host) — oyun mantığı Tulpar'da, motor C++ | ✅ |
| L6 app | `engine_demo` (masaüstü pencere / headless), **Android NativeActivity host** (+ **Swappy** seçenek), sanal joystick, HUD, **`engine_editor`** (ImGui + ImGuizmo; sahne yükle/kaydet/geri al/yinele/ekle/sil, tıkla-seç, **çoklu seçim (grup işlem)**, **kaynak tarayıcı**, **ışık/gölge/güneş gizmoları**, **Dünya paneli** (güneş/ortam/gölge/kamera), **Derle** → `.sahneb`, oynat = gövdeler fizikte; headless), **`engine_demo --scene x.sahneb`** (yazar içeriği blob'dan) | ✅ |
| L6 bridge örnek | `examples/engine_ilk_oyun.tpr` (kod, 94 satır), **`engine_arena.tpr` + `assets/arena.sahne`** (editör sahnesi → derlenmiş blob → Tulpar oyunu, 209 satır), **`engine_aksiyon.tpr` "Gölge Salonları"** (Faz 7 dikey dilimi: iki bölüm, düşman durum makinesi, menü/duraklat/ayarlar, 1070 satır) | ✅ |
| araçlar | `layer_check.py` (katman kuralı = build hatası), `compile_shaders.py`, `clang_syntax_check.sh`, `android_run.sh`, `make_test_gltf.py`, `fetch_vvl_android.sh`, `tracy_check.sh`, **`engine_sahnec`** (`.sahne` → `.sahneb`; `--check`, `--dump`, `--kanonik`, `--kume=`, `--gi*`, `--pack`, `--yama`, `--onbellek`), **`gen_engine_bindings.py`** (köprünün tek `SPEC`'i → 4 dosya), `build_bridge_android.sh`, `tests/dist_archive_audit.py`, `tests/paket_boyut_audit.py` | ✅ |

## 2. Telefonda çalışan sahne (tek APK)
Zemin (dama doku), duvar, 40 dinamik Jolt kutusu (glTF dama küpü), 24 navmesh ajanı + eklem zinciri
animasyonu, oyuncu (dokunmatik joystick, kamera izler), gölge, 8 dönen nokta ışık, HUD.
**60 fps (FIFO)**, vsync açıkken ~223 fps / 3.8 ms; kare içinde **0 `operator new`**.

## 3. Ölçülmüş gerçekler (Mali-G72, Vulkan 1.1)
- Sahne özeti 600 tick: **masaüstü = telefon = emülatör bit eşit** (`1513845f8ca5afd9`).
- Jolt altın özeti x86_64 = arm64 = telefon.
- Bedeller: gölge ~%3, doku ölçülemez, 8 ışık ~%5. Kademeli gölge varsayılanı 3 x 1024 = **6 MB** (tek 2048 = 8 MB'tan az). CPU: kayıt ~1.2 ms, submit+present ~2.5 ms.
- `LAZILY_ALLOCATED` bellek var (TBDR doğrulandı). Zaman damgası, GPL, subpass merge feedback **yok**.
- Plan L2 "zorunlu" listesi (descriptorIndexing/timeline/BDA) bu cihazda **yok** → kapı rapora çevrildi (REV-3).
- **Swappy** (2026-09-15): `SwappyVk_setQueueFamilyIndex` şart (yoksa binder beklemesi, siyah ekran); açıkken p99 18–20 ms / max 19–22 (FIFO 21–22 / 23–26), bedel kare başına 7 `new`; `VK_GOOGLE_display_timing` var; FIFO 10 s: 0 geç kare (sonda). Ses: AAudio 253 callback, düşen komut 0. Varsayılan Swappy KAPALI.

## 4. Kapılar

**Kaynaktan sayım (bugün):** `engine/tests/*.cpp` içinde **141 `ENGINE_TEST`** bloğu (Android'de editör (5) +
köprü (1) derlenmediği için **135**); Tulpar tarafında `tests/engine_bridge.test.tpr` **17** test; katman
denetimi **159 dosya, 0 ihlal**; köprü `SPEC` = `engine_api.h` = **156 builtin**.

**Kayıtlı tam koşular (2026-09-15 akşamı, GI / Faz 4 UI / GPU cull dilimleri dahil):**
- masaüstü `engine_tests` **149/149**, **0 atlandı** (2026-09-16; PBR, stokastik ışıklandırma, PSO
  önbelleği ve glTF-PBR kapıları eklendikten sonra) — `141 RUN / 141 PASS` tek tek sayıldı. Vulkan
  yükleyicisi mevcut; `rhi_*`, `renderer_*`, `render_graph_*`, `temporal_*`, `editor_*`, `bridge_*`
  ailelerinin hepsi gerçekten koştu, yani 8al'deki "sonraki kapılar sessizce ATLANDI'ya düşer" sınıfı
  bu koşumda **görülmedi**.
- `./build.sh suites` **82/82** (iki paket denetimi + Faz 8 shader kapısı dahil), Tulpar suite'leri
  toplam **1295 iddia**; `Tests:` özet satırı basmayan (dolayısıyla asla
  kırmızı olamayacak) suite **yok**. `tests/engine_bridge.test.tpr` **17/17**.
- Örnekler **102/102** yeşil (`build.sh`'in `COMPILE_ONLY_TESTS` filtresiyle).
- Telefon **78/78** (2026-09-15 sabahı, katmanla, 3 görünür ATLANDI) — bu sayı hâlâ o günün kaydı;
  emülatör 67/67 (editör testi masaüstü).

`bridge_runs_a_scripted_game_headless` kapanışta **14 hata** logluyor ve yine de PASS: bunlar kapının
bilerek koşturduğu **hata yolları** (ölü nesil id'si, sabit gövdeye kuvvet, kare dışında `teng_text`,
bilinmeyen tuş adı, kapalı ses cihazı). Hata sayacının sıfır olmaması burada doğru davranıştır.

Her görsel özelliğin açık/kapalı karşılaştırmalı testi ve pozitif/negatif kontrolü var: gölge (koyulaşan
piksel + 6 m kaydırma kontrolü), doku (keskin geçiş oranı), nokta ışık (kırmızı piksel, görüş dışı 0),
UI metni (boş metin 0), UI sıralaması (opak-önce piksel aynı, yanlış sıra kontrolü), kümeleme
(yerel/konservatif), joystick, glTF sayıları, adanmış bellek serbest bırakma (blok ayırıcı pozitif
kontrolü), fizik altın özeti, GI sondası (güneşi kapatınca oran çöker), çökme adı basma (`--cokme-kontrol`).

## 5. Öğrenilen cihaz tuzakları (Tuzaklar 8k–8ao)
y ters çevirme + CW; kare yuvası tek kaynaktan; SUBOPTIMAL = recreate değil (20→60 fps); adb shell GPU
görmez; plan "zorunlu" dedi cihaz vermedi; bump ayırıcı + pencere ömrü; `depthBias` sürücüye bağlı
(Mali'de gölge yok); UI framebuffer uzayında yatık, atlas taşınca "font yok"; katman "etkin" ama mesaj kanalı yok =
sahte yeşil (8s); `compositeAlpha OPAQUE` Huawei'de yok; katmanın seyrek-indeks taraması alt-ayırma offset'ini atlar.
Sonrakiler (8aa–8ao): istatistik kayıtta sayılır; ön-döndürme unutulunca 3B yatar ama HUD düzgün görünür;
profiler tamponu kare kapasitesinin iki katı olmalı; APK varlıkları alt dizine montaj edilir; bloom kendi
temizleme rengini kullanır; **denetim tek sembol ailesine bakınca "temiz" derken web hedefi tamamen kırıktı**
(8aj); wasm32'de işaretçi 4 bayt, nesne başlığı 20 (8ak); her kapı kendi `VkInstance`'ını açarsa sonrakiler
sessizce ATLANDI'ya düşer (8al); türetilmiş dosya bayatlayınca kapı sessizce atlanır (8am); yeni bir kapının
ilk sonucu "hepsi bozuk" ise bozuk olan genelde ölçümdür (8ao); aynı modülü iki hedef için
emit etmek ikinciyi bozar (8ap).

## 5.1 Web ve Android hedefi (2026-09-15)
Köprü masaüstünde ve Android'de koşarken **web hedefi tamamen kırıktı ve hiçbir kapı söylemiyordu**: üç
sessiz hata üst üste binmişti (kör denetim, wasm32 `ObjArray` yerleşimi, eksik `runtime_net.cpp`). Üçü de
onarıldı; `dist_archive_audit.py` artık **codegen'in adıyla bildirdiği** sembol kümesini denetliyor ve yeni
`tests/paket_boyut_audit.py` paket boyutu + açılış süresi + **SPIR-V tazeliği** (21 shader) için eşik
koyuyor. Ayrıntı: [FAZ3.md](FAZ3.md) "Web ve Android hedefleri onarıldı".

## 5.2 Oyun bir Android yüzeyinde koştu — ve bir derleyici hatası ortaya çıktı (2026-09-15)
"Gölge Salonları" Android 16 x86_64 emülatöründe açıldı: ana menü + `salon1.sahneb` sahnesi (13 varlık,
2 ışık, 10 gövde, 28 poligon navmesh), kademeli gölgeler, bloom, AAudio sesi. `examples/arcade_zipla.tpr`
de aynı emülatörde koşuyor, yani tame hattı da sağlam.

Yolda iki hata çıktı. Birincisi bayat bir ikili: depo kökündeki `./tulpar` kopyası `lib/engine.tpr`
güncellemesinden eskiydi, stdlib ikilinin *içine* gömülü olduğu için derleme var olan bir fonksiyonu
"bulunamadı" diyordu. İkincisi asıl bulgu: **aynı `LLVMModule`'den iki ABI üretmek ikincisini bozuyordu.**
`LLVMTargetMachineEmitToFile` modülü yerinde değiştirdiği için x86_64 emisyonu arm64'ün alçaltılmış IR'i
üstüne biniyor, hizalı bir `movapd` hizasız yuvaya düşüyor ve uygulama ilk karede çöküyordu. Emit artık
her hedef için `LLVMCloneModule` üstünde çalışıyor; `aot_pipeline.cpp` ABI döngüsünün iki ucunda IR özeti
karşılaştırıp klonlama kaldırılırsa derlemeyi kırmızıya çeviriyor (pozitif kontrol yapıldı).
**İlk ABI (arm64) her zaman doğruydu** — telefonda yapılmış doğrulamalar geçerli; şüpheli olan, bu
tarihten önce emülatör için üretilmiş her şeydir. Ayrıntı: [FAZ3.md](FAZ3.md), tuzak kaydı
[Tuzaklar](../mindmap/Tuzaklar.md) 8ap.

## 5.3 Faz 6 kalanı ve Faz 8 fizibilitesi (2026-09-16)
PBR (Cook-Torrance/GGX, malzeme başına seçilen gölgeleme modeli, glTF'ten akan metallic/roughness),
düşük segment için **stokastik tile ışıklandırma** (varsayılan kapalı, kapalıyken bit-aynı), ve
**PSO ön-üretimi** (`VkPipelineCache` + diske kalıcılık + kurulumda ön ısınma) geldi. Faz 8 için
fizibilite dilimi bitti ve backend kararı **Slang'den GLSL+glslc'ye** döndü (PLAN ⚠️ REV-4).

Android'de PSO önbelleğinin iki ayrı yoldan sessizce kapandığı bulundu ve kapatıldı; emülatörde
süreçler-arası kazanç ölçüldü: boru hattı kurulumu soğuk **6.5 ms** → sıcak **1.1 ms**.
Ayrıntı: [FAZ3.md](FAZ3.md) "Faz 6 kalanı + Faz 8 fizibilitesi", tuzaklar 8aq–8au.

## 6. Fazların durumu (PLAN §7'ye karşı)

| faz | durum | not |
|---|---|---|
| Faz 0 — Ölçüm ve Temel | ✅ kapandı | `FAZ0.md`; kare içinde 0 ayırma, pozitif kontrollü |
| Faz 1 — RHI, ilk piksel, Android host | 🟡 yazılım tarafı bitti | Kapı ("G-buffer DRAM'e inmedi", 3 cihaz) **cihaz bekliyor**; Kotlin host/JNI iskeleti derlenmedi (SDK yok) |
| Faz 2 — Simülasyon | ✅ yazılım tarafı kapandı | ECS, zamanlayıcı, Jolt, navmesh, animasyon, replay bit eşit |
| Faz 3 — Renderer çekirdeği | 🟡 cihaz gerektirmeyen kalemler kapandı | CSM, paketlenmiş vertex, render graph + bloom, kümelenmiş ışık, sRGB, skinning, **PBR (dokular dahil)**. **Açık:** bandwidth < 8 GB/s (3 cihaz), vis buffer A/B, VRS/FDM |
| Faz 4 — UI ve Ses | ✅ kapı geçildi | UI: tek batch, opak-önce, retained, SDF, **ölçülmüş overdraw**, < 1,5 ms. Ses: uzamsal + DSP + oklüzyon. **Açık:** çok dilli metin shaping, gerçek HRTF |
| Faz 5 — Temporal | 🟡 yazılım tarafı bitti | Jitter, hareket vektörü, dinamik çözünürlük, yükseltici arayüzü — hepsi varsayılan kapalı. Kapı (10 dk sustained, 99p < 18 ms, 3 cihaz) **cihaz bekliyor** |
| Faz 6 — İçerik boru hattı | 🟡 büyük kısmı bitti | `.tpak` + delta yama + önbellek, blob v2/v3/v4 (yerleşik küme, navmesh, küme DAG, **GI sondaları**), KTX2 kopyasız, cihaz sınıfı başına bake. **PSO ön-üretimi geldi** (VkPipelineCache + diske kalıcılık; Android'de ölçülen kazanç 6.5 → 1.1 ms). **Açık:** virtual texture, mağaza/servis kalemleri (hesap gerekiyor) |
| Faz 7 — İlk oyunun dikey dilimi | 🟡 oynanabilir dilim var | "Gölge Salonları" (`engine_aksiyon.tpr`) + oyun kabuğu. **Açık:** cihazda perf CI (3 cihaz) |
| Faz 8 — Tulpar shader stage | 🟡 8.0 fizibilite + 8.1 gramer + 8.4 yerleşim geldi | Backend kararı **GLSL + glslc** (⚠️ REV-4). 21 shader'ın 19'u Tulpar alt kümesine taşındı, SPIR-V'leri bayt aynı; bit işlemleri/`T[N]`/`const` dile girdi (18/19 ayrışıyor); CPU-GPU yerleşim doğrulaması koşuyor. **Açık:** 8.2 GPU tip sistemi (= PLAN §11 sistem alt kümesi), `uint`, 8.5 permutation/reflection |
| Faz 9 — Cluster geometri, VSM, tooling | 🟡 iki dilim geldi | Küme DAG builder + GPU cull/indirect (varsayılan kapalı), editör. **Açık:** VSM, üçgen/piksel oranı ölçümü |
| Faz 10 — Metal / iOS | ⛔ başlanmadı | Mac + geliştirici hesabı gerekiyor |

## 6.1 Ne yok (sonraki aşama adayları; tarama belgesine karşı tam liste: `BOSLUK-TARAMASI.md`)
1. ~~Sahne veri modeli + dosya formatı~~ ✅, ~~runtime blob derleyici~~ ✅, ~~bake çıktıları blob'a (navmesh, ışık haritası)~~ ✅ 2026-09-15 (navmesh v2, küme DAG v3, GI sondası v4). Kalan: **telefon demosunda blob**.
2. ~~**Editör**~~ ✅ çoklu seçim, kaynak tarayıcı, ışık/gölge/güneş gizmoları geldi. Kalan: oynat/durdur sim geri sarımı, implot ile kare zamanı grafiği.
3. ~~**Tulpar bağlaması**~~ ✅ 2026-09-15: `engine/bridge/` + `lib/engine.tpr` + üç örnek oyun; köprü **175 builtin** (ses, animasyon, navmesh, ışın/örtüşme sorguları, anlık-kip arayüz, kalıcı kayıt, sıcak yükleme, **çarpışma olayları** dahil — bkz. [KOPRU.md](KOPRU.md)).
   **Çarpışma olayı geldi (2026-09-16):** callback FFI olmadığı için geri çağrım değil **kuyruk** — fizik
   adımında oluşan temaslar sabit boy halkaya yazılır, oyun karede okur. Halka dolarsa olaylar sessizce
   kırpılmaz, `carpisma_dusen()` sayar ve motor karede bir kez hata logluyor.
   **Ajan/yol takibi geldi (2026-09-16):** yol ARAMA motorda (Detour), yol TAKİBİ `lib/engine.tpr`
   içinde saf Tulpar'da (`ajan_olustur` / `ajan_hedef` / `ajan_ilerlet`). Ayrım bilinçli: takip oynanış
   politikasıdır (hız, varış yarıçapı, yeniden arama sıklığı) — motora gömülseydi her oyun aynı davranışa
   mahkûm olurdu. Yollar ajan başına **kopyalanır**, çünkü motorun nokta tamponu tektir. Köprüye ayrıca
   `eng_nav_nearest` (mesh dışındaki konumu yapıştır — yoksa yol sorgusu 0 döner) ve `eng_nav_raycast`
   (doğru görüş; yol aramadan çok ucuz) eklendi.
4. Karakter modeli yok (iskelet/animasyon içe aktarma ve GPU skinning var; sanatçı varlığı gerek).
4b. **Normal haritası mip zinciri (2026-09-16):** blit ile üretilen mip'lerde ortalama normalin boyu 1
   değildi (ölçüldü: ters eğimli dama deseninde **0.60**) — uzaktaki yüzey sessizce düzleşiyordu. Normal
   haritaları artık CPU'da mip'lenip hazır seviye olarak yükleniyor (`build_normal_mips`), yeniden
   normalleştirilmiş en kötü boy **1.0000**. ORM bu yolu ALMAZ (pürüzlülük/metaliklik skalerdir).
   Kalan: ayrı `occlusionTexture` hâlâ okunmuyor — dördüncü sampler'a değmediği için bilinçli, sayılıyor.
5. PBR, vis buffer A/B, VRS/FDM, VSM, virtual texture, PSO üretimi, gerçek HRTF/Steam Audio,
   GameActivity göçü, Memory Advice, mağaza/servis kalemleri (hesap gerekiyor),
   **üç fiziksel cihaz** (kullanıcı kararı: şimdilik pas), macOS CI çökmesi (yerelden ulaşılamıyor).
6. Her iki denetim de `build.sh suites` içinde koşuyor ve kırmızıysa suite düşüyor (`build.sh:276` dist arşiv, `build.sh:292` paket boyutu/SPIR-V/açılış).

## 7. Çalışma kuralları (kullanıcı)
CI yok, push yok ("gönder" denene kadar); doğrulama yerel + telefon (+ emülatör yalnız işlevsel);
pencereyi ben açmam, ekran görüntüsü `adb screencap`; sayı yoksa iddia yok, her kapının kontrolü var.
PR #321 main'e birleşti (2026-09-14, squash; CI Linux + macOS yeşil). Yeni dal: `engine/faz3-sahne`
(3 yerel commit + **commit'lenmemiş büyük çalışma ağacı**: köprü, CSM, paketlenmiş vertex, render graph +
bloom, temporal, GPU cull, Faz 4 UI, GI, pack/delta, Faz 7 oyunu, web/Android onarımı — push yok,
kullanıcı kararı).
