# Tulpar Engine — Durum Özeti (2026-09-15, sonraki aşama için)

> Tek sayfada "ne var, ne ölçüldü, ne yok". Ayrıntı: FAZ0–FAZ3.md, CIHAZ-MATRISI.md, PLAN.md.
> Kural: her sayı ya masaüstünde (RTX 5080, Linux) ya telefonda (Huawei P20 Pro, Mali-G72) ölçüldü.

## 1. Ne var (katman katman)

| katman | içerik | durum |
|---|---|---|
| L0 platform | zaman, bellek, thread, çökme raporu, **GLFW dlopen pencere**, **dokunmatik durum** | ✅ |
| L1 core | arena ailesi + slotmap, **fiber job sistemi** (x86_64 + AArch64 asm), profiler (+ **Tracy** istemcisi, seçenek), math, konteynerler, `AllocGate` | ✅ |
| L2 rhi | Vulkan **dlopen**, `Device` (iki aşama, yüzey), **Swapchain** (ön-döndürme, sunum kipi, **sunum kancaları**), offscreen, PSO cache, `allocate_dedicated`, **Mali linter** (BestPractices+Arm), **tile bütçesi** (kodla zorlanır) | ✅ |
| L3 renderer | depth prepass → renk; **gölge haritası** (D16, PCF, dünya-uzayı normal eğilimi); **doku + malzeme** (klasik descriptor set, mip blit); **8–32 kümelenmiş nokta ışık** (CPU atama); **2B arayüz kuyruğu**; **doğrusal aydınlatma + sRGB hedef**; **GPU skinning** (gölge dahil) | ✅ ilk dilim |
| L3 audio | **miniaudio** cihaz (AAudio/Pulse/ALSA/CoreAudio/null), kilitsiz **karıştırıcı** (32 ses, 0 ayırma), klip yükleme | ✅ ilk dilim |
| L4 sim | archetype ECS, sistem zamanlayıcı, sabit adım + replay, **Jolt** fiber job'larda, **Recast/Detour** navmesh, sıkıştırılmış animasyon | ✅ yazılım tarafı |
| L6 content | **glTF 2.0** (cgltf + stb_image; mesh, malzeme, doku, **iskelet + animasyon**), **meshoptimizer + ayrık LOD**, **KTX2 + ASTC** (astc-encoder; `engine_texpack`), **font atlası** (stb_truetype, Türkçe), **sahne veri modeli** (`.sahne` deterministik metin, işlem günlüğü geri al/yinele, gövde kurulumu) | ✅ ilk dilim |
| L6 app | `engine_demo` (masaüstü pencere / headless), **Android NativeActivity host** (+ **Swappy** seçenek), sanal joystick, HUD, **`engine_editor`** (ImGui + ImGuizmo; sahne yükle/kaydet/geri al/yinele/ekle/sil, tıkla-seç, oynat = gövdeler fizikte; headless) | ✅ |
| araçlar | `layer_check.py` (katman kuralı = build hatası), `compile_shaders.py`, `clang_syntax_check.sh`, `android_run.sh`, `make_test_gltf.py`, `fetch_vvl_android.sh`, `tracy_check.sh` | ✅ |

## 2. Telefonda çalışan sahne (tek APK)
Zemin (dama doku), duvar, 40 dinamik Jolt kutusu (glTF dama küpü), 24 navmesh ajanı + eklem zinciri
animasyonu, oyuncu (dokunmatik joystick, kamera izler), gölge, 8 dönen nokta ışık, HUD.
**60 fps (FIFO)**, vsync açıkken ~223 fps / 3.8 ms; kare içinde **0 `operator new`**.

## 3. Ölçülmüş gerçekler (Mali-G72, Vulkan 1.1)
- Sahne özeti 600 tick: **masaüstü = telefon = emülatör bit eşit** (`1513845f8ca5afd9`).
- Jolt altın özeti x86_64 = arm64 = telefon.
- Bedeller: gölge ~%3, doku ölçülemez, 8 ışık ~%5. CPU: kayıt ~1.2 ms, submit+present ~2.5 ms.
- `LAZILY_ALLOCATED` bellek var (TBDR doğrulandı). Zaman damgası, GPL, subpass merge feedback **yok**.
- Plan L2 "zorunlu" listesi (descriptorIndexing/timeline/BDA) bu cihazda **yok** → kapı rapora çevrildi (REV-3).

## 4. Kapılar (engine_tests: masaüstü 80/80 (katmanla, 0 atlandı), emülatör 67/67 (katmanla; editör testi masaüstü), telefon 66/66 (skinning öncesi — USB düştü, telefon gelince tekrar))
Her görsel özelliğin açık/kapalı karşılaştırmalı testi ve pozitif/negatif kontrolü var: gölge (koyulaşan
piksel + 6 m kaydırma kontrolü), doku (keskin geçiş oranı), nokta ışık (kırmızı piksel, görüş dışı 0),
UI metni (boş metin 0), kümeleme (yerel/konservatif), joystick, glTF sayıları, adanmış bellek serbest
bırakma (blok ayırıcı pozitif kontrolü), fizik altın özeti, çökme adı basma (`--cokme-kontrol`).

## 5. Öğrenilen cihaz tuzakları (Tuzaklar 8k–8s)
y ters çevirme + CW; kare yuvası tek kaynaktan; SUBOPTIMAL = recreate değil (20→60 fps); adb shell GPU
görmez; plan "zorunlu" dedi cihaz vermedi; bump ayırıcı + pencere ömrü; `depthBias` sürücüye bağlı
(Mali'de gölge yok); UI framebuffer uzayında yatık, atlas taşınca "font yok"; katman "etkin" ama mesaj kanalı yok =
sahte yeşil (8s); `compositeAlpha OPAQUE` Huawei'de yok; katmanın seyrek-indeks taraması alt-ayırma offset'ini atlar.

## 6. Ne yok (sonraki aşama adayları; tarama belgesine karşı tam liste: `BOSLUK-TARAMASI.md`)
1. ~~Sahne veri modeli + dosya formatı~~ ✅ 2026-09-15 (`content/scene`, `.sahne`, işlem günlüğü). Kalan: runtime blob derleyici (PLAN §6).
2. **Editör** (var: paneller, gizmo, yörünge kamera, oynat/durdur, kaydet/yükle, geri al/yinele, ekle/sil, tıklamayla seçim): ışık/gölge/kamera paneli, runtime blob.
3. **Tulpar bağlaması**: oyun mantığı Tulpar'da (dil bugün kutusuz struct/işaretçi vermiyor; C ABI köprüsü).
4. Karakter modeli yok (iskelet/animasyon içe aktarma ve GPU skinning var; sanatçı varlığı gerek).
5. Uzamsal ses/Steam Audio, CSM, render graph (Granite referans),
   vis buffer A/B, pack formatı/lightmap (Faz 6), GameActivity göçü, Memory Advice,
   Adreno cihaz (Faz 1 kapısı), macOS CI çökmesi (yerelden ulaşılamıyor).

## 7. Çalışma kuralları (kullanıcı)
CI yok, push yok ("gönder" denene kadar); doğrulama yerel + telefon (+ emülatör yalnız işlevsel);
pencereyi ben açmam, ekran görüntüsü `adb screencap`; sayı yoksa iddia yok, her kapının kontrolü var.
PR #321 main'e birleşti (2026-09-14, squash; CI Linux + macOS yeşil). Yeni dal: `engine/faz3-sahne`.
