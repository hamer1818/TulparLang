# Boşluk Taraması — "GitHub Derinlik Taraması (Mobil)" belgesine karşı (2026-09-14)

> Kullanıcının verdiği tarama belgesi (Filament, Defold, The Forge, Granite, Vulkan-Samples/PerfDoc, AGDK,
> Tracy, ozz, Oboe, xatlas, ImGui, Performance Tuner) madde madde `engine/`'in gerçek durumuyla karşılaştırıldı.
> Üç sütun: belgenin istediği, bizde ne var (ölçülmüş), ne yapıldı / ne sırada. Kural: iddia yok, sayı var.

## 0. Bugün kapatılanlar (özet — ayrıntı katman tablolarında)

| # | Belge maddesi | Yapılan | Kanıt |
|---|---|---|---|
| 1 | **PerfDoc = CI kapısı** | Khronos BestPractices + Arm kuralları, `DeviceConfig::best_practices`, kapı + pozitif kontrol | 3 gerçek hata bulundu/düzeltildi; masaüstü + telefon + emülatör |
| 2 | **Mali tile bütçesi** | `rhi/tile_budget.hpp`, geçiş yaratılırken zorlanır | test + kontroller |
| 3 | Katman telefona | `fetch_vvl_android.sh`, APK'da katman | telefon 64/64 (o adımda) |
| 4 | **Defold boyut** | `KARSILASTIRMA.md`: 2.7 MB strip'li arm64 | ölçüldü |
| 5 | Masaüstü doğrulama katmanı | `~/.local` altında | 0 atlandı |
| 6 | **Filament renk uzayı** | doğrusal aydınlatma + sRGB hedef/doku | gidiş-dönüş birim (3 ortam) |
| 7 | **meshoptimizer + LOD** | vendored; cache/overdraw/fetch + %50/%25 LOD | ACMR 1.06→0.71, siluet %1.5 |
| 8 | **The Forge SRT** | `FrameUbo` std140 `static_assert` | derleme |
| 9 | **Tracy** | istemci + fiber + `tracy_check.sh` | telefon 2187 bölge |
| 10 | **glTF iskelet + skinning** (ozz'suz) | kendi runtime; GPU skinning | analitik + siluet; emülatör |
| 11 | **Dear ImGui + ImGuizmo** | `engine_editor` iskeleti | offscreen kapı |
| 12 | **Ses (miniaudio; AAudio)** | karıştırıcı + cihaz + klip | 3 kapı; emülatör AAudio |
| 13 | **ASTC + KTX2** | astc-encoder, kendi KTX2, `engine_texpack` | PSNR; emülatör donanım ASTC |
| 14 | **Swappy** | hooks + host + fetch (kapalı) | emülatörde asılı (8v); telefon bekliyor |

**Yapılmayanlar ve neden:** GameActivity, Memory Advice, Performance Tuner (Gradle host / Play; İP-P, İP-T),
lightmap zinciri (OpenGL bake), ozz (kendi runtime yeterli), libktx (kendi okuyucu), Oboe (AAudio doğrudan),
GGPO/ağ (ilk oyunda yok), RenderDoc/AGI (araç, kod değil). Telefon USB düştüğünden 10–14 Mali'de doğrulanmadı.

## 1. Renderer katmanı (belge §1–8)

| Belge | Bizde | Durum / karar |
|---|---|---|
| **Filament** — doğrusal uzay + pozlama zinciri, `matc` offline malzeme | ✅ **bugün**: swapchain/offscreen sRGB biçim, albedo SRGB doku, aydınlatma doğrusal, yazar renkleri sRGB→doğrusal; kapı gri gidiş-dönüş birim + kodlamasız kontrol (FAZ3 "Renk uzayı"). Shader'lar build'de derleniyor | Pozlama/tonemap yok (HDR hedef yok; Faz 5). Filament PBR belgesi BRDF için sırada |
| **Defold** boyut | 2.7 MB strip (ölçüldü) | ✅ KARSILASTIRMA.md. Hedef "12 MB" tahmindi; gerçek çok altında. Motor büyüdükçe bu satır her milestone'da yeniden ölçülür |
| **The Forge** FSL/SRT — CPU-GPU tek kaynak tablosu | `FrameUbo` std140 ofsetleri **static_assert** ile derlemede sabit (bugün) | 🟡 Tam SRT Faz 8 (Tulpar shader stage) |
| The Forge "shader dili GPU'ya benzemeli" uyarısı | — | Not alındı (PLAN EK B.1 karşı görüşü) |
| **Granite** render graph | Elle kurulmuş 2 geçiş (gölge → ana; 2 subpass) | 🟡 Faz 3 sonu / Faz 5 öncesi. Granite MIT referans; şimdilik geçiş sayısı 2, graph gerekmiyor |
| **Vulkan-Samples** transient + `LAZILY_ALLOCATED` | ✅ derinlik transient + lazily (Mali'de tür var, ölçüldü) | ✅ |
| Vulkan-Samples subpass birleştirme koşulları | ✅ bugün kodla zorlandı (madde 0.2) | ✅ |
| **PerfDoc** kapısı | ✅ bugün (madde 0.1) | ✅ |
| Kapoulkine "Writing an efficient Vulkan renderer" | Descriptor set 1 klasik, bindless yok (Mali 1.1'de descriptorIndexing yok) | Okuma listesinde; karar REV-3 ile uyumlu |

## 2. L0 Platform — AGDK (belge §9)

| Bileşen | Bizde | Durum / karar |
|---|---|---|
| **GameActivity** (yaşam döngüsü + IME + insets) | 🟠 **İP-P planlı**, yapılmadı: Java sınıfı + `games-activity` AAR (AppCompat/lifecycle bağımlılık ağacı → Gradle host; `android/host/` iskeleti var, `gradle` bu makinede yok). Swappy Java-sim tıkanması (8v) da bununla çözülür. Bugünkü host: `TERM_WINDOW→INIT_WINDOW` yüzey değişimi çalışıyor, `LOW_MEMORY` log | Sıra: 6 |
| **GameTextInput** | Yok (metin girişi yok; editör masaüstünde) | 🟡 İP-P ile birlikte |
| **GameController** | Yok | 🟢 sonra |
| **Swappy** kare temposu | 🟡 **bugün derlendi**: `SwapchainConfig::Hooks` + Android host + AAR fetch; emülatörde Java simi lib bulamıyor (asılı, Tuzaklar 8v); telefon (API 29, sim yok) ölçümü bekliyor | `TULPAR_SWAPPY=ON`, varsayılan kapalı |
| **Memory Advice API** | 🟡 Yapılmadı: `games-memory-advice` AAR (prefab C API + TFLite modeli, JNI context) — GameActivity/Gradle host ile birlikte (İP-T); bugün `APP_CMD_LOW_MEMORY` yalnız log | Sıra: 7 |
| **Oboe** | ✅ **bugün, miniaudio ile**: AAudio doğrudan (Oboe'nin sardığı API) + OpenSL yedek; FAZ4.md | Oboe yalnız cihaz tuzağı görülürse |
| **Performance Tuner** | 🟡 Yapılmadı: Play + protobuf + yayınlanmış oyun ister (L8); ilk oyunla | — |
| **AGI** | Kullanılmadı; G72'de timestamp yok | 🟡 Araç, kod değil: bir sonraki telefon ölçüm turunda denenecek (`adb` üstünden, pencere açılmadan) |
| **ADPF** termal | Yok | 🟡 Faz 5 (termal sürdürülebilir 60 fps kapısı) |

## 3. L1 Core (belge §10)

| Belge | Bizde | Karar |
|---|---|---|
| **Tracy** | ✅ **bugün**: istemci vendored, `ENGINE_TRACY=ON`; profiler bölgeleri + kare + fiber bağlama; `tracy_check.sh desktop|phone` uçtan uca (telefon 2187 bölge) | GPU bölgeleri zaman damgası olan cihazda (G72 yok) |
| enkiTS | Kendi fiber job sistemi (x86_64 + AArch64), Jolt'u sürüyor | ✅ referans; İP-C ek yük ölçümü FAZ2'de var |
| xxHash / zstd / LZ4 | Kendi FNV özeti; sıkıştırma yok | 🟢 pack formatı (Faz 6) ile |

## 4. L4 Simülasyon (belge §11)

| Belge | Bizde | Karar |
|---|---|---|
| **ozz-animation + ACL** | ✅ **bugün, ozz'suz**: `sim/animation` (ACL sınıfı klip, örnekleme, to_model) zaten vardı; eksik olan glTF iskelet/animasyon içe aktarma + GPU skinning eklendi (`content_skinned_gltf_bends`) | ozz yalnız karıştırma/IK/SoA gerekince; karar FAZ3 "glTF iskelet" |
| Oboe / miniaudio / Steam Audio / opus | ✅ **miniaudio bugün** (karıştırıcı + cihaz + klip, 3 kapı); Steam Audio/Opus sonra | FAZ4.md |
| GameNetworkingSockets / GGPO | Ağ yok; sim deterministik (üç platform bit eşit) | 🟢 GGPO neredeyse bedava, ama ilk oyunda ağ yok |

## 5. L5 Dil (belge §12)
Kütüphane yok, emsal var (Jai, Zig, Odin). Bizim durum PLAN §11: Tulpar bugün kutusuz struct/işaretçi/atomic vermiyor;
köprü C ABI ile. Sırada "Tulpar bağlaması" (DURUM §6.3). Emsaller okuma listesine eklendi.

## 6. L6 İçerik (belge §13)

| Belge | Bizde | Karar |
|---|---|---|
| cgltf | ✅ | ✅ |
| xatlas → lightmapper → seamoptimizer | 🟢 Yapılmadı (karar): `lightmapper` OpenGL tabanlı, Vulkan'a taşınmaz; xatlas + kendi Vulkan compute bake Faz 6'nın ikinci yarısı; bugün dinamik gölge + kümelenmiş ışık | — |
| meshoptimizer | ✅ **bugün**: vendored v1.2; yüklemede cache/overdraw/fetch + %50/%25 LOD, uzaklıkla seçim | Kapı: ACMR 1.06→0.71, LOD2 silueti %1.5 içinde, hata sınırı kontrolü. Meshlet/cluster DAG Faz 9 |
| astc-encoder + libktx | ✅ **bugün**: astc-encoder vendored, **kendi KTX2 okuyucu/yazıcı** (libktx yok), `engine_texpack`, GPU ASTC ya da CPU çözümü; kapı PSNR | FAZ6.md; Mali donanım yolu telefon gelince |

## 7. L7 Tooling (belge §14)

| Belge | Bizde | Karar |
|---|---|---|
| **Dear ImGui + ImGuizmo + implot** | ✅ **bugün**: ImGui + ImGuizmo vendored, `engine_editor` (paneller, gizmo, oynat/durdur, headless), kapı `editor_imgui_draws_into_offscreen_pass`; oyun içi HUD kendi çekirdek | implot sonra (kare grafiği); editörün veri modeli/kaydet sırada |
| RenderDoc | Kullanılmadı | 🟢 masaüstünde `renderdoccmd capture` headless; Android'de RenderDoc Android sürümü |
| AGI / Tracy / PerfDoc | Yukarıda | — |

## 8. L8 Operasyon (belge §15)
Hiçbiri yok; ilk oyun yayınlanmadan gerekmiyor. Sıra: Performance Tuner + Memory Advice (İP-T) ilk oyunla.

## 9. Kapanış tablosu (bizim ölçümümüz)

| Katman | Belge | Bizim | Bugün değişen |
|---|---|---|---|
| L0 | 🟡 | 🟡 NativeActivity çalışıyor, **Swappy derlendi** (telefon ölçümü bekliyor), GameActivity/MemAdvice yok | 🟡 |
| L1 | 🟢 | 🟢 **Tracy bugün** | ✅ |
| L2 | 🟢 | 🟢 **+ Mali linter kapısı + tile bütçesi** | ✅ |
| L3 | 🟢 | 🟡 **sRGB/doğrusal bugün**, render graph yok, 2 sampler ihlali **düzeltildi** | ✅ |
| L4 | 🟡 | 🟢 **iskelet/animasyon + skinning**, **ses (miniaudio) bugün**; Steam Audio/ağ yok | ✅ |
| L5 | 🔴 | 🔴 Tulpar bağlaması yok | — |
| L6 | 🟢 | 🟢 glTF + meshopt/LOD + iskelet + **ASTC/KTX2 bugün**; lightmap/pack yok | ✅ |
| L7 | 🟢 | 🟡 **editör iskeleti bugün** (ImGui + ImGuizmo); veri modeli/kaydet yok | ✅ |
| L8 | 🟡 | 🔴 yok (ilk oyun öncesi gerekmez) | — |

## 10. Sıra (kullanıcı onayına sunulan)
1. ~~sRGB / doğrusal aydınlatma~~ ✅ bugün.
2. ~~Sahne veri modeli + format~~ ✅ 2026-09-15 (`.sahne` deterministik metin, işlem günlüğü, editörde kaydet/geri al/yinele/ekle/sil). Tıklamayla seçim ✅. Kalan: runtime blob.
3. ~~glTF iskelet + GPU skinning~~ ✅ bugün (kendi runtime; ozz gerekmedi). Karakter modeli: sanatçı varlığı bekliyor.
4. ~~Tracy (İP-R)~~ ✅ bugün.
5. ~~Swappy~~ derlendi (telefon ölçümü bekliyor); **GameActivity** göçü (İP-P) Gradle host ile.
6. ~~Faz 4 ses~~ ✅; ~~ASTC/KTX2~~ ✅ bugün; Faz 6 kalan: pack formatı, lightmap.
