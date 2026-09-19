---
tags: [moc, engine, mobile, vulkan]
---

# Tulpar Engine — Yeni Motor Çekirdeği (2026-09-14 →)

[[Tame]]/[[Scene3D]]/[[Editor]] raylib üstünde **dondurulmuş, gönderilmeye devam eden** hat.
Bu not, **ayrı** bir motor çekirdeğinin (mobil öncelikli, Vulkan/Metal, derleme zamanı ağırlıklı)
başlangıcı: `engine/`. Kaynak plan bir oyun geliştiriciden geldi, kod yazılmadan önce yargılandı
ve düzeltildi.

- Plan (revize, ⚠️ REV işaretli 12 düzeltme): [PLAN.md](../../engine/docs/PLAN.md)
- Cihaz matrisi + ilk oyun tanımı (⛔ oyun tanımı stüdyo dolduracak): [CIHAZ-MATRISI.md](../../engine/docs/CIHAZ-MATRISI.md)
- Faz 0 durumu ve ölçümler: [FAZ0.md](../../engine/docs/FAZ0.md)

## Kararlar (bkz. [[Decisions]])
- **Neden yeni çekirdek:** raylib GLES2 yolu fizik/animasyon/UI/platform servisi taşımıyor; "gelişmiş mekanik + çok cihaza ulaşan oyun" hedefi için tavan. Ekleyerek varılmaz (planı raylib'in içine yazmak olurdu).
- **Neden C++ (şimdilik):** Tulpar bugün kutusuz struct, işaretçi, atomik, ayırmasız fonksiyon vermiyor (`struct` → `vm_allocate_object`). L0/L1 C++17; alt küme gelince L1 Tulpar'a taşınır, L2+ dili o zaman. PLAN.md §11.
- **Planın düzeltilen yanlışları:** vis buffer "birinci öncelik" (masaüstü gerekçesi), hacim başına LOD bake, "streaming yok" ↔ VT çelişkisi, Nanite yoğunluk hedefi, faz sırası (oyun Faz 5'e kadar yoktu), Tulpar'ı hazır sayma, little-core pinleme çelişkisi, Jolt determinizm kapsamı.
- **Her AL bir hipotezdir** cihazda ölçülene kadar. Emülatör TBDR değil.

## Faz 0'da öğrenilenler → [[Tuzaklar]] 8a–8c
Fiber havuzu tükenince kilitlenme (satır içi yürütme ile çözüldü); `new/delete` elision'ı ayırma kapısını yanlış geçirir; GCC sabit null dereference'ı siler; 100 karede tek hitch p99'a girmez, max'la okunur; fiber'da TLS adresi bayatlar (noinline getter).

## Faz 1 (başladı 2026-09-14) → [FAZ1.md](../../engine/docs/FAZ1.md)
`engine/rhi/` L2: `VkApi` (libvulkan **dlopen**, link bağımlılığı yok; başlıklar `engine/third_party/vulkan` vendored v1.4.351), `Device` (cihaz seçimi + yetenek raporu, blok bellek ayırıcı), offscreen ilk piksel (depth prepass → renk subpass, transient depth, piksel kapısı, GPU zaman damgası, PSO cache dosyası, subpass merge feedback zinciri). Pencere **açılmaz**; masaüstü offscreen, gerçek yüzey Android host'ta. CI: Linux lavapipe, macOS MoltenVK; loader yoksa `atlandi` sayacı görünür. Kotlin host + JNI iskeleti (`android/host/`, `engine/platform/android/`) derlenmedi: SDK yok.
İlk oyun tanımı verildi (`CIHAZ-MATRISI.md` §1). **Tek bloke:** üç gerçek cihaz + NDK — Faz 1 kapısı ("G-buffer DRAM'e inmedi", merge feedback) cihazda ölçülür.
Shader: GLSL → glslc → depoya giren C dizileri (`engine/tools/compile_shaders.py`); Slang Faz 8.
Faz 1 tuzakları: [[Tuzaklar]] 8d (antipodal slerp, ikinci mimari), 8e (NDC kapsama hesabı), 8f (A2 kapısı sürücüyü de sayar), 8g (GCC'nin geçirdiğini Clang reddeder → `engine/tools/clang_syntax_check.sh`).

## Faz 2 (başladı 2026-09-14) → [FAZ2.md](../../engine/docs/FAZ2.md)
`engine/sim/` L4: archetype ECS (SoA chunk'lar, nesil etiketli entity, kapasiteler init'te), okuma/yazma maskeli **sistem zamanlayıcı** (aşamalar init'te, aşama içi paralel, kayıt sırası belirlenimli), sabit adım (fazla tick atılır ve sayılır), kayıt/replay. **Faz 2 kapısı:** 1000 tick × 500 entity seri = replay = paralel aynı özet. **Jolt 5.3.0** vendored (`engine/third_party/jolt`, MIT; `JPH_CROSS_PLATFORM_DETERMINISTIC`, x86_64 SSE4.2 / arm64 NEON), `Physics` sarmalayıcısı (Jolt tipleri sızmaz), Jolt job'ları **fiber job sisteminde** (`FiberJoltJobs`), thread sayısından ve job sisteminden bağımsız aynı özet; platformlar arası altın özet (x86_64 `ed7d5c5d0986ca44`) CI arm64'te sınanıyor (REV 8 sınaması). Bulgular: Jolt çarpışma job'ları 64 KB fiber yığınını taşırdı (bekçi sayfa yakaladı → 256 KB), adım içi ayırma `QuadTree::UpdatePrepare` (sınırlı, kabul). Kalan: animasyon, navmesh, GPU particle.
**Faz 2 yazılım tarafı kapandı (2026-09-14):** + Recast/Detour navmesh (bake + 0-ayırmalı sorgu), kendi animasyon formatı (sabit iz eleme, 16-bit niceleme, en-küçük-üç dönüş; 5.3×), entegre headless sahne (16 ajan + 20 kutu + animasyon, 600 tick, seri = paralel, 0 new). Jolt platformlar arası determinizmi CI'da bit eşit doğrulandı → PLAN.md REV-2 ve "Determinizm sözleşmesi" (§11 altı): `-ffp-contract=off` + libm'siz girdi şart.
Faz 2 tuzakları: 8h (fiber yığını, üçüncü parti job), 8i (auto-merge ilk yeşilde birleşir, sonraki push kaybolur), 8j ("cross-platform deterministic" define'ı tek başına yetmez: FMA birleştirmesi derleyicinin).

## Faz 3 ilk dilim (başladı 2026-09-14) → [FAZ3.md](../../engine/docs/FAZ3.md)
**İlk uygulama:** `engine_demo` (L5 `engine/app/`) — pencere (`platform/window`, GLFW 3 dlopen) → `Device::init_instance` → yüzey → `init_device(surface)` → `Swapchain` (2 kare uçuşta, FIFO, transient D32, prepass → renk) → `Renderer` (L3, forward Lambert, UBO + push sabiti) → Faz 2 sahnesi (24 ajan + eklem zinciri, 40 Jolt kutusu). `--headless N --out x.ppm` aynı renderer'ı offscreen render pass'e kaydeder (`offscreen_render_custom`): ben bununla doğrularım, pencereyi kullanıcı açar. Yerel: 600 kare, kare içi 0 `new`, 53/53 test. **Çalışma kuralı (kullanıcı, 2026-09-14):** adım başına CI/push yok; yerelde doğrula, uygulama elde olunca push.
Tuzaklar: 8k (y ters çevirme + `CLOCKWISE` = zemin kaybolur), 8l (swapchain/renderer kare yuvası tek kaynaktan; fence gönderimden önce sıfırlanır).

## İlk gerçek cihaz (2026-09-14) → [CIHAZ-MATRISI §2.1](../../engine/docs/CIHAZ-MATRISI.md), [FAZ3.md](../../engine/docs/FAZ3.md)
Huawei P20 Pro (Kirin 970, **Mali-G72**, Vulkan 1.1, Android 10) adb ile bağlı. Motor **NativeActivity host** olarak koşuyor (`engine/app/android_main.cpp` → `libtulparengine.so`; `engine/tools/android_run.sh` derler, paketler, kurar, logu çeker). Kip `debug.tulpar.mode` ile: `tests` (aynı süreçte `engine_tests_main`), `demo` (ekranda), `headless` (offscreen + PPM).
**Sonuç:** testler 53/53; demo **59.9 fps** (FIFO), MAILBOX'ta 241 fps / 3.48 ms → **CPU-submit bağlı, GPU değil**; kare içi 0 `new`; 600 tick sahne özeti masaüstüyle **bit eşit**; `LAZILY_ALLOCATED` var → TBDR doğrulandı.
**Plan düzeltmesi (⚠️ REV-3):** L2 "zorunlu feature" listesi (descriptorIndexing/timelineSemaphore/bufferDeviceAddress) bu cihazda **yok**; kapı **rapora** çevrildi (`DeviceCaps::missing_mandatory`).
Cihaz tuzakları: [[Tuzaklar]] 8m (SUBOPTIMAL'i recreate saymak = 20 fps), 8n (adb shell'den GPU görünmez), 8o (plan "zorunlu" dedi, cihaz vermedi).

## Gölge (2026-09-14) → [FAZ3.md](../../engine/docs/FAZ3.md)
Tek kademeli yönlü ışık gölge haritası: ayrı render pass, **D16_UNORM** (mobil), `sampler2DShadow` + donanım PCF 3×3, eğilim **dünya uzayında normal kaydırması**. Telefonda **%3 bedelle** geldi, 60 fps korundu. İlk sürüm NVIDIA'da doğru / Mali'de gölgesizdi ([[Tuzaklar]] 8q: `depthBias` birimi sürücüye bağlı) — bu yüzden kapı `renderer_shadow_map_actually_darkens` kendi negatif kontrolüyle eklendi ve iki cihazda aynı sayıyı veriyor.

## İçerik: doku + malzeme + glTF (2026-09-14) → [FAZ3.md](../../engine/docs/FAZ3.md)
Faz 6'nın içe aktarma dilimi Faz 3'e çekildi (dokusuz küple bant genişliği ölçülmez). `engine/content/` (L6): **cgltf + stb_image** vendored, glTF → `content::Model` (Arena) → `upload_model` → renderer doku/malzeme/mesh. Malzeme = **klasik descriptor set** (bindless yok, REV-3), mip zinciri blit ile. Test varlığı `tests/assets/checker_cube.gltf` (`make_test_gltf.py`, belirlenimli). Kapı: dokulu küp vs düz küp keskin-geçiş oranı ≥ 4× + iki dama rengi. `app` katmanı L6'ya alındı (birleştirme kökü).

## Çok ışık (2026-09-14) → [FAZ3.md](../../engine/docs/FAZ3.md)
Kümelenmiş nokta ışıklar: 16×9×24 grid, küme başına 32-bit maske, atama **CPU'da** (belirlenimli, compute/SSBO senkronu yok, Vulkan 1.1 yeter), shader `findLSB` döngüsü + pencereli ters-kare sönüm. Kapılar: küme ataması yerel ve konservatif; kırmızı ışık testi ışıksızda 0, görüş dışında 0. Mali'de 8 ışık ~%5. `set_render_size` ile grid framebuffer uzayında (ön-döndürme uyumlu).

## Girdi + oynanabilir karakter (2026-09-14) → [FAZ3.md](../../engine/docs/FAZ3.md)
`platform/touch.hpp` (10 nokta), Android `onInputEvent`, masaüstü fare/WASD; `app/virtual_stick` (sol yarım hareket, sağ yarım bakış/tap, testli); oyuncu = dinamik Jolt kutusu, komut tick başına latch (deterministik); kamera oyuncuyu izler; arena görünmez duvarla kapalı. Telefonda `adb shell input swipe` ile uçtan uca doğrulandı: oyuncu duvara kadar yürüdü, -9.62'de durdu.

## 2B arayüz + font (2026-09-14) → [FAZ3.md](../../engine/docs/FAZ3.md)
Immediate-mode dörtgen kuyruğu (`Renderer::ui_*`, aynı subpass, alfa), **mantıksal** uzayda ve ön-döndürmeyle döndürülmüş; stb_truetype atlas (Türkçe dahil), DejaVuSans bundle; HUD ve joystick göstergesi. Kullanıcı yönü: işler **editörden** yapılacak → sıra UI çekirdeği ✅ → sahne veri modeli → editör. Tuzaklar 8r.

## Editör + sahne veri modeli (2026-09-14/15) → [FAZ3.md](../../engine/docs/FAZ3.md), [[Editor]] (raylib hattı ayrı)
`engine_editor` = motor düzenleme kipinde (Dear ImGui + ImGuizmo, yalnız masaüstü). Tek gerçek: `content::SceneDesc` (`.sahne` deterministik metin — bit-tam en kısa ondalık; `editor.sahne` kanonik, test diff'ler); sim ve GPU kaynakları ondan türetilir. `SceneHistory` (her sürükleme tek işlem), ekle/sil, ışın–AABB tıkla-seç (headless uçtan uca kapı), `T·Rz·Ry·Rx·S` ImGuizmo'yla ölçülerek sabitlendi. Tuzaklar 8x.

## Sahne → runtime blob (2026-09-15) → [FAZ3.md](../../engine/docs/FAZ3.md), PLAN §6
`.sahne` runtime'a gitmez: `engine_sahnec` / editörde **Derle** → `.sahneb` (magic, sürüm, endian, FNV-1a özeti, 16 hizalı 4-baytlık tablolar; aynı desc → aynı bayt; matris/kuaterniyon/ölçekli gövde/ışık konumu derlemede). `SceneRuntime` blob'u işaretçiyle okur (ayrıştırma/ayırma yok), `engine_demo --scene` yazar içeriğini oradan çizer ("sahne = blob + kod"; kare içi 0 `new`). Dünya paneli (`SceneWorld`, `SceneOp::World`). Kapılar 5 (bozulma reddi 10 durum + pozitif kontrol, piksel + fizik). Tuzaklar 8aa.

## Tulpar köprüsü (2026-09-15) → [KOPRU.md](../../engine/docs/KOPRU.md), [FAZ3.md](../../engine/docs/FAZ3.md)
**Karar (kullanıcı):** motor C++, oyun betikleri Tulpar — aynı ikilide, script sınırı yok (PLAN §11'in L5'i).
`engine/bridge/` düz skaler C ABI (`teng_*`, **156 fonksiyon**: struct/callback yok, bugünkü FFI'nin taşıdığı kadar);
`runtime/engine_bindings.cpp` + backend tablosu + typeinfer imzaları + LSP girdileri **tek `SPEC`'ten üretilir**
(`engine/tools/gen_engine_bindings.py`) — "5 noktada bağlama" artık mekanik. `lib/engine.tpr` TR/EN sarmalayıcı,
`examples/engine_ilk_oyun.tpr` ilk oyun. Emülatörde 60 fps, dokunmatik + skor döngüsü Tulpar'da. Her çağrı
loglanır (`TULPAR_ENGINE_LOG=0..3`), kapanışta hata varsa 64 satırlık halka dökülür. Tuzaklar 8ab–8ae.
Aileler: yaşam döngüsü, dünya/kamera, derlenmiş sahne + **sıcak yükleme**, varlık/fizik, girdi, HUD,
**anlık-kip arayüz**, **kalıcı kayıt**, ses, animasyon, bloom, **ışın/örtüşme/navmesh sorguları**, ölçüm.

## Faz 4 UI dilimi (2026-09-15) → [FAZ3.md](../../engine/docs/FAZ3.md)
PLAN Faz 4'ün UI maddesi kapandı: **SDF font atlası** (`content/font.*`, GPU'suz kalite ölçümüyle),
**tek batch** çizim (atlasa göre kararlı gruplama), **güvenli opak-önce** sıralama (yalnız örtüşmeyen opak
dörtgen öne alınır → piksel aynı; yanlış sıra kontrol kipi), **retained blok** (değişmeyen blok yeniden
hesaplanmaz), **gerçekten ölçülen overdraw** (fragment sayan boru hattı, tahmin değil) ve tam ekran harmanlı
katman uyarısı. Kapı: 7000 dörtgen / 2 batch / **< 1,5 ms** (PLAN §4 bütçesi), zaman damgası şart.

## Faz 5 Temporal + Faz 9 GPU cull (2026-09-15) → [FAZ3.md](../../engine/docs/FAZ3.md)
Jitter (Halton, projeksiyonun solundan), hareket vektörü geçişi, dinamik çözünürlük ve yükseltici arayüzü;
ayrıca **küme DAG builder** (kenar kilitli sadeleştirme) + **GPU görünürlük elemesi + dolaylı çizim**
(tek dispatch dört frustum: kamera + 3 gölge kademesi). Hepsi **varsayılan kapalı**, açık/kapalı piksel farkı
0 bayt — yani bugünkü yol bit bit aynı.

## GI sondaları + blob sürüm 4 (2026-09-15) → [FAZ3.md](../../engine/docs/FAZ3.md), PLAN Faz 6
Statik ışık **derleme anında** CPU ışın izlemesiyle çözülüp `.sahneb`'e giriyor; runtime yalnız okuyor.
Gösterim **6 yönlü ambient cube** — SH-L1 değil, çünkü güçlü tek yönlü güneşte SH lobu negatife düşer ve
ölçüm göstergenin kendi hatasını ölçmeye başlar. `gi_flags` hangi terimlerin içeride olduğunu söyler
(çift sayım yasağı). 7 kapı, hepsi kolu kapatıp aynı ölçümü tekrarlayan kontrollerle.

## Web/Android hedefi onarıldı + paket denetimleri (2026-09-15) → [[Tuzaklar]] 8aj–8ao
Köprü masaüstünde koşarken **web hedefi tamamen kırıktı ve hiçbir kapı söylemiyordu**: denetim yalnız
`aot_tm_*` ailesine bakıyor, wasm32'de `ObjArray` başlığı 20 bayt olduğu hâlde codegen 28 yazıyor ve
`target_web` bayrağı tip kurulumundan **sonra** atanıyordu; ayrıca web runtime'ında `runtime_net.cpp` yoktu.
Üçü de onarıldı; `tests/dist_archive_audit.py` artık codegen'in adıyla bildirdiği **çekirdek** sembolleri de
denetliyor ve yeni `tests/paket_boyut_audit.py` boyut + açılış + **SPIR-V tazelik** eşikleri koyuyor
(⚠️ bu yeni denetim henüz hiçbir otomasyonda koşmuyor).

## Faz 7 — ilk oyunun dikey dilimi (2026-09-15) → [FAZ3.md](../../engine/docs/FAZ3.md)
"Gölge Salonları" (`examples/engine_aksiyon.tpr`, saf Tulpar): iki bölüm, fizik tabanlı vuruş, iki davranışlı
düşman durum makinesi (devriye ↔ kovalama; ışınla görüş hattı), navmesh varsa Detour yolu yoksa düz yol +
ışınla kaçınma, ana menü / duraklat / ayarlar, kalıcı rekor. Otopilot kapısı pencersiz koşuyor.
