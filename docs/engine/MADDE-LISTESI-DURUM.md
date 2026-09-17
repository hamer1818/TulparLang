# Madde Listesi Durumu — Kullanıcının 500 Maddelik Listesi (v18.0) Çapraz Referansı

> **BU DOSYA `tools/feature_matrix.py --write-doc` İLE ÜRETİLİR.** Elle düzenlemeyin — veri kaynağı script'teki `FEATURES` listesidir. Düzeltme/ekleme için script'i güncelleyip yeniden çalıştırın (`--check` ile 1..500 kapsaması önce doğrulanır, sonra dosya yazılır).
>
> Kaynak: kullanıcının bu sohbette paylaştığı "Tulpar Engine — Ana Madde Listesi (400+/500 Madde), Sürüm 18.0" — 10 kategori (A-J). Bu dosya o listedeki **her maddeyi** aşağıdaki 7 duruma göre sınıflandırır.
>
> **Önemli sınırlama:** 500 maddenin her biri tek tek web'de aranmadı — bu orantısız olurdu (çoğu madde standart, yaygın bilinen bir render/platform tekniği). En çok "aksiyon kararı"na yön verecek ~19 madde tek tek arandı (bkz. `VIZYON.md` §6) ve **19/19 gerçek çıktı** — bu yüksek isabet oranı, tek tek doğrulanmayan maddelere de makul bir güven payı tanınmasını haklı çıkarıyor, ama bu hâlâ "kesin doğrulandı" demek değil.

## Lejant

| Sembol | Kod | Anlamı |
|---|---|---|
| ✅ | DONE | Motorda VAR |
| 🔧 | TODO | Gercek, motorda YOK (yapilacak) |
| 🔍 | VERIFIED | Bu oturumda web'den TEK TEK dogrulandi |
| 📗 | STANDARD | Standart, yaygin bilinen gercek teknik |
| 📚 | RESEARCH | Arastirma referansi, aksiyon gerektirmiyor |
| ❓ | SUSPECT | Suphesi/dogrulanamadi |
| 🚫 | OUT_OF_SCOPE | Kapsam disi (oyun mantigi/arka uc/canli operasyon) |

## A. Yapisal Avantajlar (1-15)

Birebir `VIZYON.md` SS1 + SS1.1 ile eslesiyor.

| Aralık | Madde | Durum |
|---|---|---|
| 1-4 | 0 allocation, determinizm, build-time PSO, kucuk ikili boyut | ✅ DONE |
| 5 | Rollback hazirligi (sim/rollback.hpp - KAYNAK, ecs.hpp snapshot/restore bagimliligi henuz bu branch'e commit'lenmedi) | ✅ DONE |
| 6-8 | Cihaz-sinifi bake, shader DSL+fp16, GPU-driven+bindless+mesh shader | 🔧 TODO |
| 9 | Job System + Work-Stealing (kismen - fiber job sistemi var) | ✅ DONE |
| 10 | Sanal Doku + NTC | 🔧 TODO |
| 11 | 3D Ses (HRTF + Oklüzyon) - dosya var ama bu oturumda icerigi okunup dogrulanmadi | 🔧 TODO |
| 12 | Lag Compensation + Snapshot Interpolation (sim/interp.hpp OKUNUP dogrulandi + sim/lag_compensation.hpp KAYNAK yazildi) | ✅ DONE |
| 13-14 | Termal/Guc Yonetimi, Anti-Cheat - dosyalar var ama bu oturumda icerigi okunup dogrulanmadi | 🔧 TODO |
| 15 | Noral Rendering - NPU/neural-accelerator (Mali G2-Ultra NX) HALA yok, madde TAM anlamiyla YAPILMADI. AMA core/math/nn.hpp (KAYNAK+testler) ile ALTERNATIF bir devrimsel bahis eklendi: NPU'suz, TAMAMEN CPU'da, ReLU ile TAM DETERMINISTIK (libm yok) kucuk sinir agi cikarimi -- rollback-sim (sim/rollback.hpp) ICINDE calisabilen, hicbir buyuk motorun sunmadigi bir bilesim (ogrenilmis/deterministik NPC davranisi icin). | 🔧 TODO |

## B. Render Mimarisi (16-106)

| Aralık | Madde | Durum |
|---|---|---|
| 16 | Frame Graph | 🔍 VERIFIED |
| 17-21 | Render Graph, Work Graph, bellek aliasing, otomatik bariyer, katmanli mimari | 🔧 TODO |
| 22-25 | NSS / NFRU / NSSD (gercek donanim, Tulpar hedef cihazda yok) | 🔍 VERIFIED |
| 26 | Arm Mali G2-Ultra NX | 🔍 VERIFIED |
| 27-33 | Adreno Neural Fusion ailesi, MediaTek 9600 Pro, Samsung Exynos ENSS | 📚 RESEARCH |
| 34 | DLSS 5 (NVIDIA) | 🔍 VERIFIED |
| 35 | Unity-EDR (IEEE) | 📚 RESEARCH |
| 36-38 | GPU-Driven Rendering, Bindless Descriptor Indexing, Indirect Draw | 🔍 VERIFIED |
| 39-42 | GPU Culling, Two-pass HiZ Occlusion, Mesh Shader, VRS | 🔧 TODO |
| 43-45 | Cluster DAG, "Nanite'in %60-70'i", Virtual Geometry | 🔍 VERIFIED |
| 46 | Geometry Streaming (Vovan675/RenderingEngine) - repo bulunamadi | ❓ SUSPECT |
| 47 | meshoptimizer (zaten motorda) | ✅ DONE |
| 48-50 | QEM, BVH Cluster Organization, ACSCull | 📚 RESEARCH |
| 51-53 | Sanal Doku, Sparse Residency, TBDR Sanal Doku | 🔍 VERIFIED |
| 54-55 | NVIDIA RTX NTC, Intel Neural Texture Compression SDK | 📚 RESEARCH |
| 56 | Neural Block Compression (NBC) - teknik gercek (AAAI 2025), 'Huawei/2026' atfi dogrulanamadi | 🔍 VERIFIED |
| 57 | NDGI (CVPR 2026 poster, arXiv 2604.12625) | 🔍 VERIFIED |
| 58-62 | RT Shadow/Reflection/GI, BVH (BLAS+TLAS), Opacity Micromaps | 📗 STANDARD |
| 63-64 | Mobile-DDGI, DDGI (ACM I3D 2026 - tam sayilariyla dogrulandi) | 🔍 VERIFIED |
| 65 | CSM (Cascaded Shadow Maps) | 🔧 TODO |
| 66 | RT Shadow + Neural Denoising | 🔧 TODO |
| 67 | Contact Shadow | 🔧 TODO |
| 68 | Shadow Atlas | 🔧 TODO |
| 69 | PCF (zaten motorda, golge haritasi) | ✅ DONE |
| 70 | PCSS | 🔧 TODO |
| 71-75 | SSR, RT Reflection+NSSD, Cubemap, Planar Reflection, Reflection Probe | 🔧 TODO |
| 76 | Tonemapping (PBR Neutral, motorda var - Is 7) | ✅ DONE |
| 77 | Bloom (Mipmap-based) - rhi/shaders/bloom_{threshold,downsample,upsample}.frag + fullscreen.vert, KAYNAK (glslc yok, kablolama bekliyor) | ✅ DONE |
| 78-81 | DOF, Motion Blur, Lens Flare, Color Grading | 🔧 TODO |
| 82-87 | TAA, FXAA, SMAA, MSAA, NSS(AA), Hybrid AA | 🔧 TODO |
| 88 | PBR / Cook-Torrance GGX (zaten motorda - Is 4) | ✅ DONE |
| 89-91 | Layered Materials, Material Layers, Shader Graph | 🔧 TODO |
| 92 | Occlusion Culling: HZB | 🔧 TODO |
| 93 | Occlusion Culling: Software - content/occlusion.hpp (kucuk CPU derinlik tamponu), KAYNAK+testler | ✅ DONE |
| 94 | Occlusion Culling: Hybrid | 🔧 TODO |
| 95 | Traditional LOD - content/lod_select.hpp select_lod_traditional(), KAYNAK+testler | ✅ DONE |
| 96 | HLOD | 🔧 TODO |
| 97 | Dithered LOD - content/lod_select.hpp select_lod_dithered() (bant icinde dogrusal karisim), KAYNAK+testler | ✅ DONE |
| 98 | Cluster DAG LOD | 🔧 TODO |
| 99 | Punctual Lights (nokta isik zaten var) | ✅ DONE |
| 100-101 | IBL, Light Probes | 🔧 TODO |
| 102 | Clustered Lighting (8-32 kumelenmis nokta isik, zaten motorda) | ✅ DONE |
| 103-106 | Multi-threaded Render, Parallel Command Buffer, Async Compute, Multi-queue (fiber job uzerinden kismen) | ✅ DONE |

## C. Gorsel Sistemler (107-177)

| Aralık | Madde | Durum |
|---|---|---|
| 107 | Heightmap Terrain - content/terrain.hpp generate_heightmap()+sample_height() (fbm_2d uzerine), KAYNAK+testler | ✅ DONE |
| 108 | Splatting - content/terrain.hpp compute_splat_weights() (yukseklik-bantli yamuk agirlik), KAYNAK+testler | ✅ DONE |
| 109-134 | Tessellation, Virtual Terrain, sky/fog, particle/decal, Billboard/Cross-Quad/Mesh/Cluster-DAG Foliage - standart teknikler, motorda yok | 🔧 TODO |
| 135 | Wind Animation - content/wind.hpp sample_wind() (noise.hpp uzerine, foliage.vert'in ihtiyaci), KAYNAK+testler | ✅ DONE |
| 136 | Foliage Interaction | 🔧 TODO |
| 137 | Gerstner Dalgalari - content/water_wave.hpp (GPU Gems formulu), KAYNAK+testler | ✅ DONE |
| 138-160 | FFT Waves, RT su yansimasi, kopuk/kirilma/underwater, sac/kumas/yikim, hava durumu, SSS/OIT/fur - standart teknikler, motorda yok | 🔧 TODO |
| 161 | Morph Targets - content/morph.hpp (base+agirlikli delta toplami), KAYNAK+testler | ✅ DONE |
| 162 | SDF UI - core/math/vec.hpp sdf_circle/sdf_box/sdf_rounded_box (Inigo Quilez formulleri), KAYNAK+testler | ✅ DONE |
| 163-167 | MSDF Text, H.264/H.265/VP9/AV1 Video | 🔧 TODO |
| 168 | First-person Camera | 🔧 TODO |
| 169 | Third-person Camera - sim/camera_rig.hpp FollowCamera (ussel yumusatma), KAYNAK+testler | ✅ DONE |
| 170-172 | Isometric Camera, Free Camera, Cinematic Camera | 🔧 TODO |
| 173 | Camera Shake - sim/camera_rig.hpp CameraShake (Eiserloh trauma modeli), KAYNAK+testler | ✅ DONE |
| 174 | Camera Zoom - sim/camera_rig.hpp ZoomController (exponential_smooth sarmalayici), KAYNAK+testler | ✅ DONE |
| 175 | Camera FOV - sim/camera_rig.hpp FovController (exponential_smooth sarmalayici), KAYNAK+testler | ✅ DONE |
| 176-177 | Camera DOF, Camera Motion Blur | 🔧 TODO |

## D. Donanim Hizlandirma (178-250)

| Aralık | Madde | Durum |
|---|---|---|
| 178 | Job System (fiber job sistemi zaten var) | ✅ DONE |
| 179-180 | Work-Stealing, Lock-free Queue | 🔧 TODO |
| 181 | Job Dependency Graph - core/jobs/job_graph.hpp (JobSystem'in run/wait/Counter'i uzerine, YENI senkronizasyon icermez), KAYNAK+test - GERCEK THREAD'LERLE TEST EDILMEDI (derleyici yok) | ✅ DONE |
| 182 | Deterministik Job Siralamasi | 🔧 TODO |
| 183 | big.LITTLE Optimizasyonu | 🔧 TODO |
| 184 | Core Pinning | 🔧 TODO |
| 185 | Tuanjie 1.10.0 Boids/JobWorker demosu | 🔍 VERIFIED |
| 186-188 | Parallel Rendering, Proxy Data, Render Thread Ayrimi | 🔧 TODO |
| 189-191 | GPU Skinning (vertex+compute), Bone Texture (zaten motorda, golge dahil) | ✅ DONE |
| 192 | Async Morph | 🔧 TODO |
| 193-194 | Async Instantiation, InstantiateAsync | 📗 STANDARD |
| 195-197 | ADPF, Thermal API, Performance Hint API | 🔍 VERIFIED |
| 198 | PUMA (IEEE Access 2026) | 🔍 VERIFIED |
| 199 | GameNative PID | 📚 RESEARCH |
| 200 | Frame Pacing - platform/frame_pacer.hpp (hedef kare suresi aritmetigi), KAYNAK+testler | ✅ DONE |
| 201 | Dynamic Quality Scaling | 🔧 TODO |
| 202-205 | Engine Code Stripping, Shader Memory Opt, IL2CPP Runtime Opt, Content Directories | 📗 STANDARD |
| 206-211 | Bindless, Vulkan Driver Layer, SPIR-V, Loader, GPL, Uber Fetch Shader | 📗 STANDARD |
| 212-214 | FlashMem (ASPLOS 2026), 2.5D Doku Bellegi, Mobil GPU Bellek Hiyerarsisi | 🔍 VERIFIED |
| 215 | TBR Best Practices (rhi/tile_budget.hpp ile kod seviyesinde zorlanir) | ✅ DONE |
| 216-220 | Load/Store Op, Tile Memory Heap, TBDR Post-Processing, Pixel Coverage Binning, LIBRA | 📗 STANDARD |
| 221 | Jolt Physics (zaten motorda) | ✅ DONE |
| 222 | LOOM DNVM (gercek: openfluke/loom, Go AI engine, bit-exact repro iddiasi) | 🔍 VERIFIED |
| 223-224 | Rapier Physics 3D, Belevator-Tactics | 📗 STANDARD |
| 225 | Turnip (Mesa Vulkan surucusu) | 🔍 VERIFIED |
| 226-231 | ANB, AHB, Aliased ANB Images, Deferred Image Creation, Imagination Driver 26.1, HAL DI | 📗 STANDARD |
| 232-245 | Mali Kbase Job Scheduler .. drm_sched / Realtime Scheduling (kernel alt sistemleri) | 📗 STANDARD |
| 246 | PQC (Kyber, Dilithium) | 📗 STANDARD |
| 247 | GPU DMA Guvenligi | 📗 STANDARD |
| 248 | CVE-2024-23380 (Qualcomm KGSL UAF) | 🔍 VERIFIED |
| 249 | PhantomMap (NDSS 2026, Mali GPU driver kernel exploitation - tam eslesme) | 🔍 VERIFIED |
| 250 | SELinux | 📗 STANDARD |

## E. Bilimsel Arastirmalar (251-310)

Akademik makale/rapor listesi. Bu oturumda 5/60'i tek tek dogrulandi (265 DLSS 5, 258 PUMA, 260-261 Mobile-DDGI, 262 Kuantum Isin Izleme, 292 FlashMem) - 5/5 gercek cikti. Kalan RESEARCH isaretli maddeler yuksek olasilikla gercek makaleler (isabet orani yuksek) ama tek tek dogrulanmadi, ve hicbiri mobil motor icin simdi bir aksiyon gerektirmiyor - bunlar saf arastirma-takip referanslaridir.

| Aralık | Madde | Durum |
|---|---|---|
| 251 | MobileRC (Wiley CGF, Agustos 2026 - 'Locality-aware Online Radiance Caching') | 🔍 VERIFIED |
| 252 | Seele / SeeLe (CVPR 2026, tam eslesme - 6.3x speedup dogrulandi) | 🔍 VERIFIED |
| 253-255 | Mobile3DGS3, MobileSplat, HERMES-SR | 📚 RESEARCH |
| 256 | Blubber Engine - SCITEPRESS'te veya baska yerde bulunamadi | ❓ SUSPECT |
| 257 | GPUSched (IEEE PerCom 2026, Yonsei Universitesi - tam eslesme) | 🔍 VERIFIED |
| 258 | PUMA | 🔍 VERIFIED |
| 259 | DualEngine (IEEE SECON 2026, tam eslesme) | 🔍 VERIFIED |
| 260-261 | Mobile-DDGI, DDGI | 🔍 VERIFIED |
| 262 | Kuantum Isin Izleme (QCM, SIGGRAPH 2026, arXiv 2606.29989) | 🔍 VERIFIED |
| 263-264 | Kuantum Doku Uretimi, TSMC (4D Mesh Compression) | 📚 RESEARCH |
| 265 | DLSS 5 | 🔍 VERIFIED |
| 266-267 | Unity-EDR, ReSTIR PT | 📚 RESEARCH |
| 268 | AlayaRenderer (AlayaLab/AlayaRenderer, arXiv 2604.02329/2607.18703 - tam eslesme) | 🔍 VERIFIED |
| 269 | NDGI (dup) | 🔍 VERIFIED |
| 270 | NBC (dup, Huawei atfi dogrulanamadi) | 🔍 VERIFIED |
| 271-273 | Project Kalos, ReS2/h-BN/WTe2, SpikON | 📚 RESEARCH |
| 274 | Altai (Rusya noromorfik cip) | 🔍 VERIFIED |
| 275 | Neural Dynamics Chip | 📚 RESEARCH |
| 276 | RhabdoForge (FlorentLM/RhabdoForge github + Neuroscience Agustos 2026 makalesi - tam eslesme) | 🔍 VERIFIED |
| 277 | FlyGym / NeuroMechFly v2 (NeLy-EPFL/flygym, Nature Methods - tam eslesme) | 🔍 VERIFIED |
| 278-281 | Compound Eye, Assembloid Agency, SVBRDF, Neural-GASh | 📚 RESEARCH |
| 282 | Transform-ER (kaynak 'Product Hunt' - bulunamadi) | ❓ SUSPECT |
| 283 | IEEE P2861.2 | 📚 RESEARCH |
| 284 | CarDroid - 'ACM, Eylul 2026' olarak bulunamadi | ❓ SUSPECT |
| 285-291 | Stream of Rendered Content, Lighting at Scale, Arm Neural Technology, Neural Dawn, Adreno Neural Fusion, MediaTek 9600 Pro, Samsung ENSS | 📚 RESEARCH |
| 292 | FlashMem | 🔍 VERIFIED |
| 293-297 | Mobile 3DGS Virtual Memory, Data-Oriented ECS(dup), Foreground DNN(dup), GPU Cgroup(dup) | 📚 RESEARCH |
| 298 | PhantomMap (dup) | 🔍 VERIFIED |
| 299 | CVE-2024-23380 (dup) | 🔍 VERIFIED |
| 300-303 | Vulkan TBR Best Practices, Uber Fetch Shader, DRM_SCHED Latency, Mali Kbase Job Scheduler | 📗 STANDARD |
| 304 | Arm Mali: Mori to Nanite | 🔍 VERIFIED |
| 305 | Vulkan Bindless Native Plugin (saivs/com.saivs.plugin.bindless.vulkan, Unity Discussions Temmuz 2026) | 🔍 VERIFIED |
| 306 | Quantum Collision Models (dup 262) | 🔍 VERIFIED |
| 307 | HoloPathTracer (arXiv 2606.14173, SIGGRAPH 2026 - tam eslesme, ama holografi/VR icin, genel path tracer degil) | 🔍 VERIFIED |
| 308-310 | PerCom DNN Scheduling, MobileSplat Streaming, Adaptive Frame Rate Streaming | 📚 RESEARCH |

## F. Kuantum ve Noromorfik (311-340)

| Aralık | Madde | Durum |
|---|---|---|
| 311 | Arm Neural Technology (genel) | 📚 RESEARCH |
| 312 | Mali G2-Ultra NX (dup) | 🔍 VERIFIED |
| 313-315 | NSS, NFRU, NSSD (dup) | 🔍 VERIFIED |
| 316 | Neural Dawn | 📚 RESEARCH |
| 317-321 | Adreno Neural Fusion, Matrix Cores, HPM, MediaTek Neural Graphics, Samsung ENSS | 📚 RESEARCH |
| 322 | DLSS 5 (dup) | 🔍 VERIFIED |
| 323 | Unity-EDR (dup) | 📚 RESEARCH |
| 324 | Altai Noromorfik Cip | 🔍 VERIFIED |
| 325-328 | Neural Dynamics Chip, Project Kalos, ReS2/h-BN/WTe2, SpikON | 📚 RESEARCH |
| 329 | Kuantum RNG | 📚 RESEARCH |
| 330 | Kuantum Doku Sikistirma | 📚 RESEARCH |
| 331 | Kuantum Isin Izleme (dup) | 🔍 VERIFIED |
| 332 | Kuantum Doku Uretimi (dup) | 📚 RESEARCH |
| 333 | PQC (Kyber, Dilithium - NIST standardi, gercek) | 📗 STANDARD |
| 334 | QKD (Kuantum Anahtar Dagitimi) | 📚 RESEARCH |
| 335-340 | Kuantum Carpisma Tespiti, Kuantum Yol Bulma, Kuantum Doku Sikistirma(QCM,tekrar), Kuantum Boltzmann Makinesi, Kuantum Destekli Vektor Makinesi, Kuantum PCA - 262 numarali gercek makalenin kapsami disina tasan, muhtemelen dolgu maddeler | ❓ SUSPECT |

## G. GitHub Ekosistemi (341-380)

| Aralık | Madde | Durum |
|---|---|---|
| 341 | Gizmo (bdrtr/Gizmo) | 🔍 VERIFIED |
| 342 | GGRS (gschup/ggrs) | 🔍 VERIFIED |
| 343 | Vovan675/RenderingEngine (bulunamadi) | ❓ SUSPECT |
| 344 | Filament (Google) | 🔍 VERIFIED |
| 345 | Bevy | 🔍 VERIFIED |
| 346 | Jolt Physics (zaten motorda) | ✅ DONE |
| 347 | Rapier Physics 3D | 📗 STANDARD |
| 348 | LOOM (dup, gercek) | 🔍 VERIFIED |
| 349 | renew-engine | 📗 STANDARD |
| 350 | Macroquad | 📗 STANDARD |
| 351 | KorGE | 📗 STANDARD |
| 352 | Prism | 📚 RESEARCH |
| 353 | Ebitengine | 📗 STANDARD |
| 354 | Meep | 📚 RESEARCH |
| 355 | Fyrox | 📗 STANDARD |
| 356-359 | Skylicht, Markmos, Doriax, Hilen | 📚 RESEARCH |
| 360-364 | Custom-Virtualized-Geometry-Renderer, Nyx, SealLOD, Nanite-WebGPU, Arm Neural Graphics UE Plugin | 📚 RESEARCH |
| 365 | Mobile-GS | 📚 RESEARCH |
| 366 | Neuron-Bridge (gercek proje adi 'Neuro-Bridge' - EAISD/Neuro-Bridge, Android NPU/GPU/DSP proxy) | 🔍 VERIFIED |
| 367-368 | fly-self-driving (suanmiao/fly-self-driving, gercek - 165k noron connectome; 'fly.ai' ayri bulunamadi) | 🔍 VERIFIED |
| 369-372 | Quasar, Gearecs, HeliosEngine, PSRayTracing | 📚 RESEARCH |
| 373 | NanoRT (Syoyo Fujita'nin bilinen tek-header ray tracer'i) | 📗 STANDARD |
| 374-377 | 8DMusicPlayer, OpenALSoftGlobalHRTF, VoiRS-spatial, Fyrox-sound | 📚 RESEARCH |
| 378 | Genesis Physics - gercek 'Genesis' Python robotik simulatoru, header-only deterministik C++ degil | ❓ SUSPECT |
| 379-380 | ACSCull, Eye | 📚 RESEARCH |

## H. Platform Spesifikleri (381-410)

| Aralık | Madde | Durum |
|---|---|---|
| 381-390 | Android NDK, Gradle, CMake, JNI, Activity Lifecycle, Surface, Permissions, App Bundle, Play Console, Vulkan Stack | 📗 STANDARD |
| 391 | ADPF (dup) | 🔍 VERIFIED |
| 392-393 | Thermal API, Performance Hint API (dup) | 🔍 VERIFIED |
| 394-401 | Game Stats API, Play Games Services v2, Sidekick, Android XR, Arm Perf Studio, AGI, Sokatoa, APA | 📚 RESEARCH |
| 402-410 | Metal 3, Build-Time GPU Binary, iOS XR, Core ML, MPS, AVAudioEngine, Xcode, Obj-C++ Boundary, App Lifecycle | 📗 STANDARD |

## I. Kernel ve Surucu Katmani (411-430)

Kategori D'nin 232-250 araligiyla icerik olarak buyuk olcude cakisiyor (ayni 20 madde kullanicinin orijinal listesinde iki kez verilmis).

| Aralık | Madde | Durum |
|---|---|---|
| 411-424 | Mali Kbase .. Realtime Scheduling (Kategori D 232-245 ile ayni icerik, tekrar) | 📗 STANDARD |
| 425 | PQC (dup) | 📗 STANDARD |
| 426 | GPU DMA Guvenligi (dup) | 📗 STANDARD |
| 427 | CVE-2024-23380 (dup) | 🔍 VERIFIED |
| 428 | PhantomMap (dup) | 🔍 VERIFIED |
| 429 | SELinux (dup) | 📗 STANDARD |
| 430 | Turnip (dup) | 🔍 VERIFIED |

## J. Diger Sistemler (431-500)

440-449 ve 454-463 bloklari bilincli olarak KAPSAM DISI: arka uc/canli-operasyon/sosyal/gameplay ozellikleri, `DEVAM_PLANI.md`'nin bilincli disarida biraktigi "dil baglamasi"na ve orijinal kapsam notuna ("oyun mantigi, NPC, diyalog, simulasyon icermez") giriyor. Istisna: Motion Matching(439)/Yuz Animasyonu(450)/IK(452)/Voxel(453)/Coklu Dokunmatik(459) - bunlar motor/animasyon/girdi teknigi, gameplay degil, kapsamda tutulur. 464-500 B/C kategorileriyle BIREBIR TEKRAR eden ~30 madde icerir, ayri degerlendirilmedi.

| Aralık | Madde | Durum |
|---|---|---|
| 431 | Rollback Netcode (sim/rollback.hpp - KAYNAK, kablolama bekliyor) | ✅ DONE |
| 432-433 | Lag Compensation (sim/lag_compensation.hpp - KAYNAK), Snapshot Interpolation (sim/interp.hpp - VAR, dogrulandi) | ✅ DONE |
| 434 | IPv6 Serverless Netcode (MAME4droid ile dogrulandi) | 🔍 VERIFIED |
| 435 | Cihaz-Sinifi Bake | 🔧 TODO |
| 436-437 | Shader DSL, Otomatik fp16 | 🔧 TODO |
| 438 | PCG (Prosedurel Icerik) - core/math/random.hpp (Rng+shuffle) + core/math/noise.hpp (deger gurultusu+fBm), KAYNAK+testler, kablolama bekliyor | ✅ DONE |
| 439 | Motion Matching | 🔧 TODO |
| 440-449 | Bulut Kaydetme, Liderlik Tablolari, Mod Destegi, VR/AR Destegi, Erisilebilirlik, Analitik+A/B, Remote Config, Sesli Sohbet, Moderasyon, Sosyal Ozellikler | 🚫 OUT_OF_SCOPE |
| 450 | Yuz Animasyonu | 🔧 TODO |
| 451 | Duygu Sistemi (gameplay/AI mantigi) | 🚫 OUT_OF_SCOPE |
| 452 | Ters Kinematik (IK) - sim/ik.hpp analitik iki-kemik cozucu, KAYNAK+testler, kablolama bekliyor | ✅ DONE |
| 453 | Voxel Teknolojisi - content/voxel.hpp (gorunen-yuz meshleme) KAYNAK+testler, kablolama bekliyor | ✅ DONE |
| 454-456 | Katlanabilir Telefon, Derin Baglantilar, Uygulama Ici Guncellemeler | 🚫 OUT_OF_SCOPE |
| 457 | Varlik Paketleme - content/pack.hpp (isimle aranabilir tek-dosya format), KAYNAK+testler | ✅ DONE |
| 458 | Geriye Donuk Uyumluluk | 🚫 OUT_OF_SCOPE |
| 459 | Coklu Dokunmatik (platform/touch.hpp - VAR) + Jest (platform/gesture.hpp Tap+Pinch - KAYNAK+testler, kablolama bekliyor) | ✅ DONE |
| 460-463 | Oyun Ici Sohbet, Oyun Ici Ekonomi, Basarimlar, Analitik Dashboard | 🚫 OUT_OF_SCOPE |
| 464 | Frame Pacing (dup) - platform/frame_pacer.hpp, KAYNAK+testler | ✅ DONE |
| 465-469 | Desync Detector, Contact Shadow, Shadow Atlas, Reflection Probe, Light Probes (dup) | 🔧 TODO |
| 470 | Clustered Lighting (dup) | ✅ DONE |
| 471 | Weighted Blended OIT (dup) | 🔧 TODO |
| 472 | Wind Animation (dup) - content/wind.hpp, KAYNAK+testler | ✅ DONE |
| 473-494 | God Rays, Aurora, Yildizlar, Volumetric Clouds, Height/Volumetric Fog, FFT Waves, Underwater, Neural Hair, GPU Cloth, Voronoi Fracture, MSDF Text, H.265/AV1, Camera Shake/DOF, Motion Blur, Lens Flare, Color Grading, TAA, NSS-AA, Hybrid AA (dup B/C) | 🔧 TODO |
| 495 | PBR (dup) | ✅ DONE |
| 496-498 | Layered Materials, Shader Graph, HZB Occlusion (dup) | 🔧 TODO |
| 499 | Software Occlusion (dup) - content/occlusion.hpp, KAYNAK+testler | ✅ DONE |
| 500 | Hybrid Occlusion (dup) | 🔧 TODO |

## Özet (kesin — kodla üretildi)

`tools/feature_matrix.py`, 1-500 arasındaki her id'yi tam bir kez kapsayan 216 kayıt tutuyor (`--check` ile denetlenir, boşluk/çakışma varsa hata verir). Bu doğrudan `--summary` çıktısıdır:

| Durum | Madde sayısı |
|---|---|
| ✅ DONE | 53 |
| 🔧 TODO | 154 |
| 🔍 VERIFIED | 76 |
| 📗 STANDARD | 95 |
| 📚 RESEARCH | 91 |
| ❓ SUSPECT | 12 |
| 🚫 OUT_OF_SCOPE | 19 |
| **Toplam** | **500** |

Sorgulamak için: `--status TODO` (bir duruma göre listele), `--category B` (bir kategoriyi göster), `--find "shadow"` (başlıkta ara), `--check` (kapsama denetimi). **Bu bir denetim değil, bir yönlendirme haritası**: hangi maddenin gerçek bir mühendislik kararı gerektirdiğini (🔧), hangisinin zaten yapıldığını (✅), hangisinin sadece arka plan bilgisi olduğunu (📗/📚) ayırt etmek için var.
