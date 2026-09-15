# TULPAR_TAM_DOKUMAN.md Kontrolü — Görev Listesi + Düzeltmeler (2026-09-15)

> `BOSLUK-TARAMASI.md` ile aynı desen: dışarıdan gelen bir denetim/plan belgesi (bu kez kullanıcının
> 18 önceki denetimi birleştirdiği TULPAR_TAM_DOKUMAN.md) madde madde koda karşı doğrulandı.
> Orijinal metin (16 bölüm + EK A-H, ~80K+ kelime) burada **aynen kopyalanmadı** — çoğu zaten
> `PLAN.md`'nin EK'lerinde karşılığı var ya da arşiv niteliğinde. Bu dosya, ondaki **doğru ve yeni**
> kısımları süzüp kalıcı, aksiyona dönüştürülebilir hâle getiriyor. Kural değişmedi: sayı yoksa iddia yok.

## 0. Misyon

> *"Tüm oyun motorunu geliştiriyoruz, spesifik değil: en performanslı, en kaliteli, sadece mobil için
> oyun motoru. CryEngine/Unity/Godot/Source2/Unreal Engine'den her alanda daha kaliteli ve hepsinden
> daha performanslı; devrimsel teknolojilerin benzerini ya da daha iyisini mobil için yapıyoruz."*

Bu, aşağıdaki §1'deki 10. maddeyi çözüyor: PBR/GGX **motor yeteneği** olarak tam kapsamda — `CIHAZ-MATRISI.md`'deki "ilk oyun: PBR yok, basit BRDF + bake GI" kararı hâlâ geçerli ama **o belirli ilk sahne** için bir sanat yönü seçimi, motorun üst sınırı değil. Motor PBR'ı destekleyecek, ilk oyun onu kullanmamayı seçebilir.

---

## 1. Düzeltmeler — dokümanın kendi iddialarıyla kod çakıştığı yerler

| # | İddia | Gerçek durum | Kanıt |
|---|---|---|---|
| 1 | 🔴 "60 dosya/6.485 satır → PR #321 → 151/15.485" büyüme anlatısı | Dokümanın "taban" tablosu kendi toplamıyla uyuşmuyor (109 dosya/15.241 satır eder, 60/6.485 değil) ve 6/9 klasörü **bugünkü kodla birebir aynı**; kalan 3'ündeki fark tam olarak birkaç üretilmiş dosyanın boyutu. Kanıtlanamıyor. | rhi:+9 `*_spv.h`=778 satır, content:+`vendored_impl.c`=12, audio:+`miniaudio_impl.c`=20 |
| 2 | 🔴 `Mat4::ortho` eksik, eklenecek | **Zaten var**, test edilmiş (`math_ortho_matches_vulkan_depth_range`) | `core/math/vec.hpp` |
| 3 | 🔴 ECS `get<T>` eksik | **Zaten var** | `sim/ecs.hpp:116` |
| 4 | 🔴 Dear ImGui + ImGuizmo "eklenecek" | **Zaten tam entegre**, test edilmiş | `CMakeLists.txt:234-244`, `BOSLUK-TARAMASI.md:21` |
| 5 | 🔴 Tracy "eklenecek" | **CPU tarafı zaten entegre**, telefonda 2187 bölge ölçülmüş | `core/profiler/profiler.cpp`, `BOSLUK-TARAMASI.md:19` |
| 6 | 🔴 astc-encoder "eklenecek" | **Zaten tam entegre** (`engine_texpack` + testler) | `BOSLUK-TARAMASI.md:23` |
| 7 | 🟡 PerfDoc'u CI kapısı yap | Gereksiz — Arm arşivlemiş, zaten daha iyisi (Khronos BestPractices+Arm kuralları) var, 3 hata yakalamış | `rhi/device.hpp:55-56`, `BOSLUK-TARAMASI.md:11` |
| 8 | 🟡 ozz-animation / libktx "eklenecek" | Proje bunları **kasıtlı reddetmiş** ("kendi runtime yeterli", "kendi okuyucu") | `BOSLUK-TARAMASI.md:27` |
| 9 | 🟠 Faz numaralandırması "Faz 5 = Simülasyon" | Proje 2026-09-14'te yeniden numaralandırdı: **yeni Faz 2 = Simülasyon** (kapandı), **yeni Faz 5 = Temporal** (henüz başlamadı) | `PLAN.md:460`, `docs/engine/FAZ2.md` başlığı |
| 10 | 🟠 PBR en yüksek öncelik | §0'daki misyon netleşmesiyle çözüldü: motor yeteneği olarak devam, ilk oyun sahnesi ayrı karar | `CIHAZ-MATRISI.md:20` |

**Hâlâ gerçekten geçerli olan (kod bu oturumda doğrulandı):** Math primitifleri, PBR/tonemap/MSAA/AO/normal-map/skybox-fog/bloom, ECS add/remove/parent/command-buffer, GameActivity göçü, VMA, Mali Offline Compiler build-gate, sahne formatı/editör kalıcılığı.

---

## 2. Düzeltilmiş görev listesi (uygulama sırası)

Aşağıdaki 16 iş, orijinal Bölüm 0'daki sıra ve kapı testleriyle **aynı** — sadece §1'deki düzeltmeler uygulandı. Her iş: **amaç · yöntem (özet) · kapı**.

| # | İş | Amaç | Kapı |
|---|---|---|---|
| **1 [M0]** | Matematik tamamlama | `Mat4::inverse`+`inverse_affine`, `Aabb`, `Plane`, `Frustum`, `Ray`+`intersect`, `decompose` ekle. ~~`ortho`~~ (zaten var, çıkarıldı) | `math_inverse_roundtrip`, `math_frustum_culls_known_aabbs` (poz. kontrol: kamera dönünce sonuç değişir) |
| **2 [CULL]** | Frustum culling | SoA AABB, dünya uzayı, gölge pass'i ayrı frustum | `cull_reduces_draw_count` (sayı raporlu) |
| **3 [FALL]** | Fiziksel ışık düşüşü | ters kare + yumuşak yarıçap kesme, `eps≈0.01` | `light_falloff_is_inverse_square`, yarıçap dışında tam 0 |
| **4 [PBR]** | GGX specular + roughness/metallic | Cook-Torrance (Filament mobil kısaltması), fp16, ORM tek doku (R=AO,G=Rough,B=Metal) | `pbr_metal_vs_dielectric_differs`, `pbr_roughness_spreads_highlight` — **İŞ 5 ile birlikte** (metal yansımasız siyah çıkar) |
| **5 [PROBE]** | Yansıma probe | Skybox'tan prefiltered cubemap (build'de), split-sum + BRDF LUT | `probe_metal_reflects_environment` |
| **6 [SKY][FOG]** | Skybox + analitik sis | Prosedürel gökyüzü (Hosek-Wilkie/Preetham) build'de bake; sis **doğrusal uzayda, tonemap'ten önce** | `fog_is_applied_in_linear_space`, `sky_does_not_write_depth` |
| **7 [TONE]** | Tonemapping + exposure | **Khronos PBR Neutral** (bkz §3) | `tonemap_no_double_srgb`, `tonemap_compresses_highlights` |
| **8 [AA]** | MSAA | 4×, resolve attachment, `STORE_OP_DONT_CARE` multisample'da | `msaa_reduces_edge_aliasing` + bandwidth ölçümü |
| **9 [TILE]** | Tile bütçesi denetimi | ≤8 attachment, ≤128 bit/px, build'de hesapla | `tile_budget_within_limits`. **Ek:** Mali Offline Compiler (`malioc`) build-gate (§1/#7) hâlâ yapılmadı, buraya eklenmeli |
| **10 [NORM]** | Normal map + tangent | glTF'ten oku / build'de MikkTSpace üret, `_UNORM` doku | `normal_map_perturbs_lighting` |
| **11 [AO]** | Ambient occlusion | ORM'deki bake AO (bedava) + opsiyonel quarter-res SSAO | `ao_darkens_creases_only` |
| **12 [CONTACT]** | Contact shadow | Ekran uzayı kısa ray march (8-12 adım) | `contact_shadow_grounds_objects` |
| **13 [POST][BLOOM]** | Post zinciri + Dual Filtering bloom | Kawase tabanlı downsample+upsample, **compute bloom yasak** | `bloom_dual_filter_vs_gaussian_ms` |
| **14 [M1]** | Math SIMD + culling hızlandırma | NEON, SoA toplu `Frustum::intersects` | ölçülü kazanç |
| **15 [BAKE0]** | Bake hattı başlangıcı | Prefiltered cubemap + BRDF LUT + AO bake + probe, hepsi build'de | — |
| **16 [ADAPT]** | Oto-exposure + dynamic resolution | Histogram exposure; ADPF thermal + histerezis (bkz §3) | 🔴 temporal reset sinyali şimdiden tasarlanmalı |

**Yasaklar (değişmedi):** compute'a pass taşıma yok · runtime shader/PSO üretimi yok · kare içi allocation yok · determinizm bozulmaz · katman kuralı ihlali yok · `depthBias`'a güvenme (normal-offset bias zaten var) · ölçmeden iddia yok.

---

## 3. Somut sayısal kararlar (hiçbir mevcut dosyada yoktu, buraya taşındı)

| Konu | Karar | Gerekçe |
|---|---|---|
| **Tonemap** | **Khronos PBR Neutral** (ACES değil) | Demo'nun sorunu doygun ışıkların patlaması; ACES ton kaydırır (magenta→pembe), PBR Neutral tonu korur |
| **Işık birimi** | Fotometrik: nokta/spot **lümen**, yönlü **lux**, emissive **nit** (Filament konvansiyonu) | Sanatçı değerleri taşınabilir olur, "ışığı 5000 yaptım karanlık" tartışması biter |
| **Skybox kaynağı** | Prosedürel (Hosek-Wilkie/Preetham), build'de 128-256² cubemap'e bake | 0 kaynak doku; aynı bake skybox+probe+irradiance SH'i birden verir |
| **Dynamic res histerezis** | Azalt: `frame_time > budget×1.10`. Artır: `<budget×0.85`. %10'luk kademeler, değişimden sonra **≥30 kare** dokunma, ölçüm son 10 karenin **medyanı** | Salınımı önler. 🔴 Her çözünürlük değişimi temporal history'yi geçersiz kılar — reset sinyali şart |
| **Mip bias / aniso sırası** | 1) mipmap var mı doğrula → 2) aniso 4× (zemin/duvar/yol) → 3) aniso 2× (diğer) → 4) pozitif bias +0.2~0.5 **yalnız 1-3 yetmezse**, ikisi birden değil | Aniso zaten çözüyorsa bias sadece keskinlik kaybı |
| **Vertex format** | Pozisyon `R16G16B16A16_SNORM` (bbox normalize) · normal oktahedral `R10G10B10A2` (+2 bit tangent işareti) · UV `R16G16_UNORM` · kemik idx `R8G8B8A8_UINT` · ağırlık `R8G8B8A8_UNORM` | fp16 pozisyonda orijinden uzakta hassasiyet kaybı; SNORM+bbox tam 16 bit verir |
| **Present mode** | `VK_PRESENT_MODE_FIFO_KHR` (Mailbox/Immediate değil) | A7: varyans ortalamadan önemli; Swappy zaten FIFO üstünde pacing yapıyor |
| **Pre-rotation** | `preTransform = currentTransform`, döndürme projeksiyona/scissor/viewport/dokunmatiğe uygulanır | `currentTransform` identity bırakılırsa compositor ekstra pass yapar |
| **Shader debug bilgisi** | Debug: `OpLine`+`OpSource` korunur. Release: strip + smol-v + zstd, semboller ayrı `.shadersym` | Kurulum boyutu ürün metriği |

⚠️ **Not:** Bu tablodaki malzeme/roughness/metallic örnek değerleri (orijinal dokümanın §0B/8'i) İP-PBR'ın engine-level test verisi olarak kalır; **ilk oyun sahnesi** `CIHAZ-MATRISI.md` kararına göre bunları kullanmayabilir (§0).

---

## 4. GPU mikro-mimari notları (referans, cihaz geldiğinde ölçülecek)

| Not | Aksiyon |
|---|---|
| **Adreno FlexRender** | GMEM (tile) modundan direct moda mid-frame düşebilir. `CIHAZ-MATRISI.md`'ye Adreno kademeleri için GMEM boyutu kolonu eklenmeli, attachment ayak izi buna karşı hesaplanmalı |
| **UBWC** | Render target'a `VK_IMAGE_USAGE_STORAGE_BIT` eklemek framebuffer sıkıştırmasını **sessizce** kapatabilir. Storage usage yalnız gerekince eklensin, bandwidth ölçülsün |
| **LRZ (Adreno)** | `discard`/alpha-test veya fragment'ta depth yazmak LRZ'yi devre dışı bırakır — opak geometride `discard` yasağının 3. gerekçesi |
| **VMA notu** | VMA (alınırsa) `VkDeviceMemory` yönetir — bizim `core/memory` CPU arena'larıyla (host bellek) **karıştırılmaz**, ayrı kategoriler |
| **bufferImageGranularity** | Aynı pool'da buffer+image karıştırmak sessiz bellek israfı — ayrı buffer/image pool |

---

## 5. Kütüphane / birey küratörlüğü (referans okuma listesi)

| Kim/Ne | Neden bize göre |
|---|---|
| Filament (Google) | PBR + Materials dokümanı — İş 4/5'in doğrudan tarifi |
| Granite (Arntzen) | Render graph referans uygulaması (İP-H, henüz sırada değil) |
| jms55 (Bevy Virtual Geometry) | Cluster DAG ayarları — motor bu ölçeğe gelince |
| Kapoulkine (meshoptimizer, niagara, volk) | Zaten `meshoptimizer` vendored; culling/RHI tasarımı referansı |
| Bjørge (Dual Filtering bloom) | İş 13'ün doğrudan kaynağı, ölçülü 14× kazanç |
| Ryan Brucks (Octahedral impostor) | LOD merdiveninin uzak ucu, motor o aşamaya gelince |
| Sander Mertens / Michele Caini (Flecs/EnTT yazarları) | ECS add/remove + hiyerarşi tasarımı — İş listesinde olmayan ama §1/#10'da işaretli ECS eksiği için |
| Guillaume Blanc / Nicholas Frechette (ozz/ACL) | **Kasıtlı alınmadı** (§1/#8) — referans olarak kalsın, karar değişmedi |

*(Tam 18 isim orijinal dokümanın Bölüm XVI'sında; burada motorun mevcut aşamasıyla doğrudan ilgili olanlar seçildi.)*

---

## 6. Editör — gerçek durum (bu oturumun bulgusu)

`app/editor_app.cpp` (350 satır) tam okundu. `DURUM.md`'nin "editör iskeleti" ifadesi **yanıltıcı** — gerçekte:

**Zaten çalışıyor:** "Sahne" paneli (entity listesi, tıkla-seç — bir hiyerarşi paneli), "Özellikler" paneli (`DragFloat3` ile konum/dönüş/ölçek — bir inspector paneli), ImGuizmo (T/R/S tuşlarıyla taşı/döndür/ölçekle, Vulkan Y-flip düzeltilmiş), play/stop, orbit kamera, headless PPM ekran görüntüsü modu.

**Gerçekten eksik:** entity'ler `editor_app.cpp:170-174`'te koda gömülü — **kaydet/yükle yok**, **undo yok**, UI'dan **ekle/sil yok**, **content/asset browser yok**, çoklu seçim yok. `DURUM.md §6`'nın "sahne veri modeli + dosya formatı" maddesi bunun önkoşulu — doğru sırada.

---

## 7. Windows portu — ertelendi (bu oturumun bulgusu)

Kullanıcı kararı: şimdilik ele alınmayacak, Linux'tan test edilecek. Yine de backlog'a kayıt:

Native Windows desteklenmiyor — **MSVC/clang farkı değil, kod hiç yazılmamış.** Yedi dosya sıfır Windows dalı içeriyor:

| Dosya | Kullandığı POSIX API | Windows karşılığı |
|---|---|---|
| `platform/thread.cpp` | `pthread_create`, `sched_yield`, `nanosleep` | `CreateThread`, `SwitchToThread`, `Sleep`/bekleyen zamanlayıcı |
| `platform/memory.cpp` | `mmap`/`munmap`/`mprotect` | `VirtualAlloc`/`VirtualFree`/`VirtualProtect` |
| `platform/window.cpp` | `dlopen("libglfw.so.3")` | `LoadLibrary("glfw3.dll")` (mantığın gerisi fonksiyon işaretçisi üzerinden zaten taşınabilir) |
| `platform/time.cpp` | `clock_gettime(CLOCK_MONOTONIC)` | `QueryPerformanceCounter` |
| `platform/crash.cpp` | `sigaction`/`sigaltstack`/`_Unwind_Backtrace` | `SetUnhandledExceptionFilter` + `CaptureStackBackTrace`/DbgHelp — en kapsamlı yeniden yazım |
| `rhi/vk_api.cpp` | `dlopen("libvulkan.so.1")` | `LoadLibrary("vulkan-1.dll")` |
| `core/jobs/fiber.cpp` (+ `fiber_switch_x86_64.S`) | El yazması assembly, **SysV x86_64 ABI**'ye gömülü (callee-saved r12-r15/rbx/rbp) | Windows x64 ABI farklı (farklı callee-saved kayıtlar + shadow space + XMM6-15). **Öneri:** el yazması assembly'yi Windows'ta taşımak yerine **`CreateFiber`/`SwitchToFiber`** (WinAPI'nin yerleşik fiber desteği) kullan — ABI riski sıfırlanır |

---

## 8. Faz numaralandırma uyarısı

`PLAN.md:12`'deki 2026-09-14 revizyonundan sonra: **yeni Faz 2 = Simülasyon** (eski Faz 5, kapandı), **yeni Faz 5 = Temporal** (eski Faz 4: HDR/tonemap/ADPF/dynamic-res, henüz başlamadı). Bu doküman veya orijinal TULPAR_TAM_DOKUMAN.md'de geçen her "Faz 5" referansı **hangi şemayı kastettiğine göre okunmalı** — güncel kod tabanı için doğrusu **Temporal**.
