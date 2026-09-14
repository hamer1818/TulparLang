# Boşluk Taraması — "GitHub Derinlik Taraması (Mobil)" belgesine karşı (2026-09-14)

> Kullanıcının verdiği tarama belgesi (Filament, Defold, The Forge, Granite, Vulkan-Samples/PerfDoc, AGDK,
> Tracy, ozz, Oboe, xatlas, ImGui, Performance Tuner) madde madde `engine/`'in gerçek durumuyla karşılaştırıldı.
> Üç sütun: belgenin istediği, bizde ne var (ölçülmüş), ne yapıldı / ne sırada. Kural: iddia yok, sayı var.

## 0. Bugün kapatılanlar

| # | Belge maddesi | Yapılan | Kanıt |
|---|---|---|---|
| 1 | **PerfDoc = CI kapısı** (§5, §7.2, İP-B) | PerfDoc arşivlenmiş; ardılı Khronos doğrulama katmanının **BestPractices + Arm satıcı kuralları**. `DeviceConfig::best_practices` katman ayarlarını `VK_EXT_layer_settings` zinciriyle açıyor; uyarılar kimlik başına sayılıyor (`Device::best_practice_id`). Test `renderer_mali_best_practices_gate`: tam kare (gölge + doku + nokta ışık + UI) → **Arm uyarısı 0 = kapı**, genel uyarılar rapor; **pozitif kontrol** LOD kırpan sampler'ın Arm uyarısı vermesi | İlk koşumda **2 gerçek Mali ihlali** buldu: iki sampler `maxLod` kırpıyordu (`BestPractices-Arm-vkCreateSampler-lod-clamping`). Düzeltildi (`VK_LOD_CLAMP_NONE`). Masaüstü 64/64 katmanla; telefon: aşağıda |
| 2 | **Mali birleştirme bütçesi ≤8 attachment, ≤128 bit/px** (§5, §7.1) | `rhi/tile_budget.hpp`: bütçe **kodla zorlanıyor** — swapchain ve offscreen geçişleri yaratılırken aşım = init hatası; bilinmeyen biçim sessizce 0 sayılmaz, hata | Test `rhi_mali_tile_budget_rule`: ana geçiş 1 att / 32 bit renk / 32 bit derinlik; plan zinciri (vis 64 + ışık 32 + hareket 16 + maske 8 = **120**) sığıyor; 2×RGBA32F (256) ve 9 attachment reddediliyor |
| 3 | Katman telefona | `android_run.sh tests` Khronos Android katmanını (1.4.357.0, Apache-2.0, `engine/tools/fetch_vvl_android.sh`, gitignore'lu) APK'ya koyuyor; yükleyici debuggable uygulamanın lib dizininden alıyor | Telefonda: testler 64/64, pozitif kontrol 0→1 (Arm kuralları gerçekten açık); demo 0 doğrulama hatası. Yol boyunca **3 gerçek hata** çıktı: sampler LOD ×2, mesaj kanalı yok (sahte yeşil), `compositeAlpha` desteklenmiyor. Katmanın bir sahte pozitifi (VVL 45) CPU ölçümüyle açıkça düşülüyor |
| 4 | **Defold 1 MB** karşılaştırması (§2, §7.3) | `KARSILASTIRMA.md` yazıldı, **ölçülmüş** boyutla | `libtulparengine.so` arm64 Release strip: **2.7 MB** (Jolt + Recast + renderer + testler dahil) |
| 6 | **Filament renk uzayı** (İP-A) | Doğrusal aydınlatma + sRGB hedef/doku; yazar renkleri dönüştürülür | `renderer_srgb_roundtrip_is_identity`: 32 128 200 255 aynen; kontrol 4 55 147 255 (masaüstü + telefon + emülatör) |
| 5 | Masaüstünde doğrulama katmanı yoktu (testler ATLANDI diyordu) | LunarG SDK'dan yalnız katman `~/.local/lib/vulkan/` + `~/.local/share/vulkan/explicit_layer.d/` (kullanıcı düzeyi, açık katman; başka uygulamayı etkilemez) | Masaüstü artık **64/64, 0 atlandı** |

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
| **GameActivity** (yaşam döngüsü + IME + insets) | `NativeActivity` + `native_app_glue`, `hasCode=false` (Java/DEX yok), APK `aapt2` ile elle | 🟠 **İP-P planlandı, henüz değil.** Gerektirdiği şey: Java sınıfı (`GameActivity` türevi) → `hasCode=true`, `javac`+`d8` (build-tools'ta var), `androidx.games:games-activity` AAR (Maven'dan indirilir, prefab `.a` + başlıklar). Gradle şart değil. Öncesinde bugünkü host'un yaşam döngüsü kapısı: `TERM_WINDOW → INIT_WINDOW` yüzey değişimi çalışıyor (`replace_surface`), `LOW_MEMORY` yalnız loglanıyor |
| **GameTextInput** | Yok (metin girişi yok; editör masaüstünde) | 🟡 İP-P ile birlikte |
| **GameController** | Yok | 🟢 sonra |
| **Swappy** kare temposu | Yok; FIFO ile 60 fps ölçüldü, MAILBOX 223 fps | 🟠 `games-frame-pacing` AAR prefab ile `SwappyVk_queuePresent`; sabit 60/30 hedefi ve termal düşüşte adım. İP-P'den bağımsız yapılabilir |
| **Memory Advice API** | Yok; bellek tepe değeri statik (arena rezervleri) | 🟡 AAR + JNI; `APP_CMD_LOW_MEMORY` bugün log. İP-T |
| **Oboe** | ✅ **bugün, miniaudio ile**: AAudio doğrudan (Oboe'nin sardığı API) + OpenSL yedek; FAZ4.md | Oboe yalnız cihaz tuzağı görülürse |
| **Performance Tuner** | Yok (saha telemetrisi yok) | 🟡 L8, oyun yayınlanınca |
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
| xatlas → lightmapper → seamoptimizer | Yok (dinamik gölge + nokta ışık var, lightmap yok) | 🟢 Faz 6; zincir hazır, tek dosya kütüphaneler |
| meshoptimizer | ✅ **bugün**: vendored v1.2; yüklemede cache/overdraw/fetch + %50/%25 LOD, uzaklıkla seçim | Kapı: ACMR 1.06→0.71, LOD2 silueti %1.5 içinde, hata sınırı kontrolü. Meshlet/cluster DAG Faz 9 |
| astc-encoder + libktx | Yok (RGBA8 + blit mip) | 🟡 Faz 6 pack; Mali'de ASTC ölçümü telefonda yapılır |

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
| L0 | 🟡 | 🟡 NativeActivity çalışıyor, GameActivity/Swappy/MemAdvice yok | — |
| L1 | 🟢 | 🟢 **Tracy bugün** | ✅ |
| L2 | 🟢 | 🟢 **+ Mali linter kapısı + tile bütçesi** | ✅ |
| L3 | 🟢 | 🟡 **sRGB/doğrusal bugün**, render graph yok, 2 sampler ihlali **düzeltildi** | ✅ |
| L4 | 🟡 | 🟢 **iskelet/animasyon + skinning**, **ses (miniaudio) bugün**; Steam Audio/ağ yok | ✅ |
| L5 | 🔴 | 🔴 Tulpar bağlaması yok | — |
| L6 | 🟢 | 🟡 glTF + **meshopt/LOD bugün**; lightmap/ASTC/pack yok | ✅ |
| L7 | 🟢 | 🟡 **editör iskeleti bugün** (ImGui + ImGuizmo); veri modeli/kaydet yok | ✅ |
| L8 | 🟡 | 🔴 yok (ilk oyun öncesi gerekmez) | — |

## 10. Sıra (kullanıcı onayına sunulan)
1. ~~sRGB / doğrusal aydınlatma~~ ✅ bugün.
2. **Sahne veri modeli + format** → editörün kaydet/yükle/geri al katmanı (editör iskeleti ✅ bugün, ImGui kararı verildi).
3. ~~glTF iskelet + GPU skinning~~ ✅ bugün (kendi runtime; ozz gerekmedi). Karakter modeli: sanatçı varlığı bekliyor.
4. ~~Tracy (İP-R)~~ ✅ bugün.
5. **Swappy** kare temposu, sonra **GameActivity** göçü (İP-P).
6. ~~Faz 4 ses~~ ✅ ilk dilim bugün (miniaudio); Faz 6 içerik (ASTC/KTX2/lightmap).
