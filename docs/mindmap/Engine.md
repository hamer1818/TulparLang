---
tags: [moc, engine, mobile, vulkan]
---

# Tulpar Engine — Yeni Motor Çekirdeği (2026-09-14 →)

[[Tame]]/[[Scene3D]]/[[Editor]] raylib üstünde **dondurulmuş, gönderilmeye devam eden** hat.
Bu not, **ayrı** bir motor çekirdeğinin (mobil öncelikli, Vulkan/Metal, derleme zamanı ağırlıklı)
başlangıcı: `engine/`. Kaynak plan bir oyun geliştiriciden geldi, kod yazılmadan önce yargılandı
ve düzeltildi.

- Plan (revize, ⚠️ REV işaretli 12 düzeltme): [PLAN.md](../engine/PLAN.md)
- Cihaz matrisi + ilk oyun tanımı (⛔ oyun tanımı stüdyo dolduracak): [CIHAZ-MATRISI.md](../engine/CIHAZ-MATRISI.md)
- Faz 0 durumu ve ölçümler: [FAZ0.md](../engine/FAZ0.md)

## Kararlar (bkz. [[Decisions]])
- **Neden yeni çekirdek:** raylib GLES2 yolu fizik/animasyon/UI/platform servisi taşımıyor; "gelişmiş mekanik + çok cihaza ulaşan oyun" hedefi için tavan. Ekleyerek varılmaz (planı raylib'in içine yazmak olurdu).
- **Neden C++ (şimdilik):** Tulpar bugün kutusuz struct, işaretçi, atomik, ayırmasız fonksiyon vermiyor (`struct` → `vm_allocate_object`). L0/L1 C++17; alt küme gelince L1 Tulpar'a taşınır, L2+ dili o zaman. PLAN.md §11.
- **Planın düzeltilen yanlışları:** vis buffer "birinci öncelik" (masaüstü gerekçesi), hacim başına LOD bake, "streaming yok" ↔ VT çelişkisi, Nanite yoğunluk hedefi, faz sırası (oyun Faz 5'e kadar yoktu), Tulpar'ı hazır sayma, little-core pinleme çelişkisi, Jolt determinizm kapsamı.
- **Her AL bir hipotezdir** cihazda ölçülene kadar. Emülatör TBDR değil.

## Faz 0'da öğrenilenler → [[Tuzaklar]] 8a–8c
Fiber havuzu tükenince kilitlenme (satır içi yürütme ile çözüldü); `new/delete` elision'ı ayırma kapısını yanlış geçirir; GCC sabit null dereference'ı siler; 100 karede tek hitch p99'a girmez, max'la okunur; fiber'da TLS adresi bayatlar (noinline getter).

## Faz 1 (başladı 2026-09-14) → [FAZ1.md](../engine/FAZ1.md)
`engine/rhi/` L2: `VkApi` (libvulkan **dlopen**, link bağımlılığı yok; başlıklar `engine/third_party/vulkan` vendored v1.4.351), `Device` (cihaz seçimi + yetenek raporu, blok bellek ayırıcı), offscreen ilk piksel (depth prepass → renk subpass, transient depth, piksel kapısı, GPU zaman damgası, PSO cache dosyası, subpass merge feedback zinciri). Pencere **açılmaz**; masaüstü offscreen, gerçek yüzey Android host'ta. CI: Linux lavapipe, macOS MoltenVK; loader yoksa `atlandi` sayacı görünür. Kotlin host + JNI iskeleti (`android/host/`, `engine/platform/android/`) derlenmedi: SDK yok.
İlk oyun tanımı verildi (`CIHAZ-MATRISI.md` §1). **Tek bloke:** üç gerçek cihaz + NDK — Faz 1 kapısı ("G-buffer DRAM'e inmedi", merge feedback) cihazda ölçülür.
Shader: GLSL → glslc → depoya giren C dizileri (`engine/tools/compile_shaders.py`); Slang Faz 8.
Faz 1 tuzakları: [[Tuzaklar]] 8d (antipodal slerp, ikinci mimari), 8e (NDC kapsama hesabı).
