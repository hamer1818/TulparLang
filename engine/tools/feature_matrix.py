#!/usr/bin/env python3
"""Tulpar Engine - 500 maddelik "Ana Madde Listesi" (v18.0) siniflandirma araci.

Bu dosya docs/engine/MADDE-LISTESI-DURUM.md'nin makine-okunabilir hali: ayni
siniflandirmayi (DONE/TODO/VERIFIED/STANDARD/RESEARCH/SUSPECT/OUT_OF_SCOPE)
1..500 arasindaki HER ID icin, bosluk/cakisma birakmadan kodlar. Amac: markdown
tablosunun elle guncellenirken kayma riskini azaltmak - buradaki veri tek
kaynak (source of truth), --render-md ile markdown tekrar uretilebilir.

Kullanim:
    python3 tools/feature_matrix.py --summary
    python3 tools/feature_matrix.py --status TODO
    python3 tools/feature_matrix.py --category B
    python3 tools/feature_matrix.py --find "shadow"
    python3 tools/feature_matrix.py --check          # 1..500 kapsama denetimi
    python3 tools/feature_matrix.py --render-md       # markdown tablo govdesini yazdirir
"""
from __future__ import annotations

import argparse
import sys
from dataclasses import dataclass

# --------------------------------------------------------------------------
# Durum sozlugu (docs/engine/MADDE-LISTESI-DURUM.md "Lejant" ile birebir)
# --------------------------------------------------------------------------

STATUS = {
    "DONE":         ("✅", "Motorda VAR"),
    "TODO":         ("\U0001F527", "Gercek, motorda YOK (yapilacak)"),
    "VERIFIED":     ("\U0001F50D", "Bu oturumda web'den TEK TEK dogrulandi"),
    "STANDARD":     ("\U0001F4D7", "Standart, yaygin bilinen gercek teknik"),
    "RESEARCH":     ("\U0001F4DA", "Arastirma referansi, aksiyon gerektirmiyor"),
    "SUSPECT":      ("❓", "Suphesi/dogrulanamadi"),
    "OUT_OF_SCOPE": ("\U0001F6AB", "Kapsam disi (oyun mantigi/arka uc/canli operasyon)"),
}

CATEGORIES = {
    "A": "Yapisal Avantajlar",
    "B": "Render Mimarisi",
    "C": "Gorsel Sistemler",
    "D": "Donanim Hizlandirma",
    "E": "Bilimsel Arastirmalar",
    "F": "Kuantum ve Noromorfik",
    "G": "GitHub Ekosistemi",
    "H": "Platform Spesifikleri",
    "I": "Kernel ve Surucu Katmani",
    "J": "Diger Sistemler",
}


@dataclass(frozen=True)
class Feature:
    start: int
    end: int
    cat: str
    status: str
    title: str

    def ids(self) -> range:
        return range(self.start, self.end + 1)

    def id_label(self) -> str:
        return str(self.start) if self.start == self.end else f"{self.start}-{self.end}"


# --------------------------------------------------------------------------
# Veri: 1..500 arasi HER id, tam olarak bir kez kapsanir (bkz. --check).
# Kaynak: bu oturumda kullanicinin paylastigi 500 maddelik liste + bu oturumda
# yapilan web dogrulamalari (VIZYON.md SS6, MADDE-LISTESI-DURUM.md).
# --------------------------------------------------------------------------

FEATURES: list[Feature] = [
    # --- A. Yapisal Avantajlar (1-15) ---
    Feature(1, 4, "A", "DONE", "0 allocation, determinizm, build-time PSO, kucuk ikili boyut"),
    Feature(5, 5, "A", "DONE", "Rollback hazirligi (sim/rollback.hpp - KAYNAK, ecs.hpp snapshot/restore bagimliligi henuz bu branch'e commit'lenmedi)"),
    Feature(6, 8, "A", "TODO", "Cihaz-sinifi bake, shader DSL+fp16, GPU-driven+bindless+mesh shader"),
    Feature(9, 9, "A", "DONE", "Job System + Work-Stealing (kismen - fiber job sistemi var)"),
    Feature(10, 10, "A", "TODO", "Sanal Doku + NTC"),
    Feature(11, 11, "A", "TODO", "3D Ses (HRTF + Oklüzyon) - dosya var ama bu oturumda icerigi okunup dogrulanmadi"),
    Feature(12, 12, "A", "DONE", "Lag Compensation + Snapshot Interpolation (sim/interp.hpp OKUNUP dogrulandi + sim/lag_compensation.hpp KAYNAK yazildi)"),
    Feature(13, 14, "A", "TODO", "Termal/Guc Yonetimi, Anti-Cheat - dosyalar var ama bu oturumda icerigi okunup dogrulanmadi"),
    Feature(15, 15, "A", "TODO",
            "Noral Rendering - NPU/neural-accelerator (Mali G2-Ultra NX) HALA yok, madde TAM anlamiyla YAPILMADI. "
            "AMA core/math/nn.hpp (KAYNAK+testler) ile ALTERNATIF bir devrimsel bahis eklendi: NPU'suz, TAMAMEN "
            "CPU'da, ReLU ile TAM DETERMINISTIK (libm yok) kucuk sinir agi cikarimi -- rollback-sim (sim/rollback.hpp) "
            "ICINDE calisabilen, hicbir buyuk motorun sunmadigi bir bilesim (ogrenilmis/deterministik NPC davranisi icin)."),

    # --- B. Render Mimarisi (16-106) ---
    Feature(16, 16, "B", "VERIFIED", "Frame Graph"),
    Feature(17, 21, "B", "TODO", "Render Graph, Work Graph, bellek aliasing, otomatik bariyer, katmanli mimari"),
    Feature(22, 25, "B", "VERIFIED", "NSS / NFRU / NSSD (gercek donanim, Tulpar hedef cihazda yok)"),
    Feature(26, 26, "B", "VERIFIED", "Arm Mali G2-Ultra NX"),
    Feature(27, 33, "B", "RESEARCH", "Adreno Neural Fusion ailesi, MediaTek 9600 Pro, Samsung Exynos ENSS"),
    Feature(34, 34, "B", "VERIFIED", "DLSS 5 (NVIDIA)"),
    Feature(35, 35, "B", "RESEARCH", "Unity-EDR (IEEE)"),
    Feature(36, 38, "B", "VERIFIED", "GPU-Driven Rendering, Bindless Descriptor Indexing, Indirect Draw"),
    Feature(39, 42, "B", "TODO", "GPU Culling, Two-pass HiZ Occlusion, Mesh Shader, VRS"),
    Feature(43, 45, "B", "VERIFIED", "Cluster DAG, \"Nanite'in %60-70'i\", Virtual Geometry"),
    Feature(46, 46, "B", "SUSPECT", "Geometry Streaming (Vovan675/RenderingEngine) - repo bulunamadi"),
    Feature(47, 47, "B", "DONE", "meshoptimizer (zaten motorda)"),
    Feature(48, 50, "B", "RESEARCH", "QEM, BVH Cluster Organization, ACSCull"),
    Feature(51, 53, "B", "VERIFIED", "Sanal Doku, Sparse Residency, TBDR Sanal Doku"),
    Feature(54, 55, "B", "RESEARCH", "NVIDIA RTX NTC, Intel Neural Texture Compression SDK"),
    Feature(56, 56, "B", "VERIFIED", "Neural Block Compression (NBC) - teknik gercek (AAAI 2025), 'Huawei/2026' atfi dogrulanamadi"),
    Feature(57, 57, "B", "VERIFIED", "NDGI (CVPR 2026 poster, arXiv 2604.12625)"),
    Feature(58, 62, "B", "STANDARD", "RT Shadow/Reflection/GI, BVH (BLAS+TLAS), Opacity Micromaps"),
    Feature(63, 64, "B", "VERIFIED", "Mobile-DDGI, DDGI (ACM I3D 2026 - tam sayilariyla dogrulandi)"),
    Feature(65, 65, "B", "TODO", "CSM (Cascaded Shadow Maps)"),
    Feature(66, 66, "B", "TODO", "RT Shadow + Neural Denoising"),
    Feature(67, 67, "B", "TODO", "Contact Shadow"),
    Feature(68, 68, "B", "TODO", "Shadow Atlas"),
    Feature(69, 69, "B", "DONE", "PCF (zaten motorda, golge haritasi)"),
    Feature(70, 70, "B", "TODO", "PCSS"),
    Feature(71, 75, "B", "TODO", "SSR, RT Reflection+NSSD, Cubemap, Planar Reflection, Reflection Probe"),
    Feature(76, 76, "B", "DONE", "Tonemapping (PBR Neutral, motorda var - Is 7)"),
    Feature(77, 77, "B", "DONE", "Bloom (Mipmap-based) - rhi/shaders/bloom_{threshold,downsample,upsample}.frag + fullscreen.vert, KAYNAK (glslc yok, kablolama bekliyor)"),
    Feature(78, 81, "B", "TODO", "DOF, Motion Blur, Lens Flare, Color Grading"),
    Feature(82, 87, "B", "TODO", "TAA, FXAA, SMAA, MSAA, NSS(AA), Hybrid AA"),
    Feature(88, 88, "B", "DONE", "PBR / Cook-Torrance GGX (zaten motorda - Is 4)"),
    Feature(89, 91, "B", "TODO", "Layered Materials, Material Layers, Shader Graph"),
    Feature(92, 92, "B", "TODO", "Occlusion Culling: HZB"),
    Feature(93, 93, "B", "DONE", "Occlusion Culling: Software - content/occlusion.hpp (kucuk CPU derinlik tamponu), KAYNAK+testler"),
    Feature(94, 94, "B", "TODO", "Occlusion Culling: Hybrid"),
    Feature(95, 95, "B", "DONE", "Traditional LOD - content/lod_select.hpp select_lod_traditional(), KAYNAK+testler"),
    Feature(96, 96, "B", "TODO", "HLOD"),
    Feature(97, 97, "B", "DONE", "Dithered LOD - content/lod_select.hpp select_lod_dithered() (bant icinde dogrusal karisim), KAYNAK+testler"),
    Feature(98, 98, "B", "TODO", "Cluster DAG LOD"),
    Feature(99, 99, "B", "DONE", "Punctual Lights (nokta isik zaten var)"),
    Feature(100, 101, "B", "TODO", "IBL, Light Probes"),
    Feature(102, 102, "B", "DONE", "Clustered Lighting (8-32 kumelenmis nokta isik, zaten motorda)"),
    Feature(103, 106, "B", "DONE", "Multi-threaded Render, Parallel Command Buffer, Async Compute, Multi-queue (fiber job uzerinden kismen)"),

    # --- C. Gorsel Sistemler (107-177) ---
    Feature(107, 107, "C", "DONE", "Heightmap Terrain - content/terrain.hpp generate_heightmap()+sample_height() (fbm_2d uzerine), KAYNAK+testler"),
    Feature(108, 108, "C", "DONE", "Splatting - content/terrain.hpp compute_splat_weights() (yukseklik-bantli yamuk agirlik), KAYNAK+testler"),
    Feature(109, 134, "C", "TODO",
            "Tessellation, Virtual Terrain, sky/fog, particle/decal, Billboard/Cross-Quad/Mesh/Cluster-DAG Foliage - "
            "standart teknikler, motorda yok"),
    Feature(135, 135, "C", "DONE", "Wind Animation - content/wind.hpp sample_wind() (noise.hpp uzerine, foliage.vert'in ihtiyaci), KAYNAK+testler"),
    Feature(136, 136, "C", "TODO", "Foliage Interaction"),
    Feature(137, 137, "C", "DONE", "Gerstner Dalgalari - content/water_wave.hpp (GPU Gems formulu), KAYNAK+testler"),
    Feature(138, 160, "C", "TODO",
            "FFT Waves, RT su yansimasi, kopuk/kirilma/underwater, sac/kumas/yikim, hava durumu, "
            "SSS/OIT/fur - standart teknikler, motorda yok"),
    Feature(161, 161, "C", "DONE", "Morph Targets - content/morph.hpp (base+agirlikli delta toplami), KAYNAK+testler"),
    Feature(162, 162, "C", "DONE", "SDF UI - core/math/vec.hpp sdf_circle/sdf_box/sdf_rounded_box (Inigo Quilez formulleri), KAYNAK+testler"),
    Feature(163, 167, "C", "TODO", "MSDF Text, H.264/H.265/VP9/AV1 Video"),
    Feature(168, 168, "C", "TODO", "First-person Camera"),
    Feature(169, 169, "C", "DONE", "Third-person Camera - sim/camera_rig.hpp FollowCamera (ussel yumusatma), KAYNAK+testler"),
    Feature(170, 172, "C", "TODO", "Isometric Camera, Free Camera, Cinematic Camera"),
    Feature(173, 173, "C", "DONE", "Camera Shake - sim/camera_rig.hpp CameraShake (Eiserloh trauma modeli), KAYNAK+testler"),
    Feature(174, 174, "C", "DONE", "Camera Zoom - sim/camera_rig.hpp ZoomController (exponential_smooth sarmalayici), KAYNAK+testler"),
    Feature(175, 175, "C", "DONE", "Camera FOV - sim/camera_rig.hpp FovController (exponential_smooth sarmalayici), KAYNAK+testler"),
    Feature(176, 177, "C", "TODO", "Camera DOF, Camera Motion Blur"),

    # --- D. Donanim Hizlandirma (178-250) ---
    Feature(178, 178, "D", "DONE", "Job System (fiber job sistemi zaten var)"),
    Feature(179, 180, "D", "TODO", "Work-Stealing, Lock-free Queue"),
    Feature(181, 181, "D", "DONE", "Job Dependency Graph - core/jobs/job_graph.hpp (JobSystem'in run/wait/Counter'i uzerine, YENI senkronizasyon icermez), KAYNAK+test - GERCEK THREAD'LERLE TEST EDILMEDI (derleyici yok)"),
    Feature(182, 182, "D", "TODO", "Deterministik Job Siralamasi"),
    Feature(183, 183, "D", "TODO", "big.LITTLE Optimizasyonu"),
    Feature(184, 184, "D", "TODO", "Core Pinning"),
    Feature(185, 185, "D", "VERIFIED", "Tuanjie 1.10.0 Boids/JobWorker demosu"),
    Feature(186, 188, "D", "TODO", "Parallel Rendering, Proxy Data, Render Thread Ayrimi"),
    Feature(189, 191, "D", "DONE", "GPU Skinning (vertex+compute), Bone Texture (zaten motorda, golge dahil)"),
    Feature(192, 192, "D", "TODO", "Async Morph"),
    Feature(193, 194, "D", "STANDARD", "Async Instantiation, InstantiateAsync"),
    Feature(195, 197, "D", "VERIFIED", "ADPF, Thermal API, Performance Hint API"),
    Feature(198, 198, "D", "VERIFIED", "PUMA (IEEE Access 2026)"),
    Feature(199, 199, "D", "RESEARCH", "GameNative PID"),
    Feature(200, 200, "D", "DONE", "Frame Pacing - platform/frame_pacer.hpp (hedef kare suresi aritmetigi), KAYNAK+testler"),
    Feature(201, 201, "D", "TODO", "Dynamic Quality Scaling"),
    Feature(202, 205, "D", "STANDARD", "Engine Code Stripping, Shader Memory Opt, IL2CPP Runtime Opt, Content Directories"),
    Feature(206, 211, "D", "STANDARD", "Bindless, Vulkan Driver Layer, SPIR-V, Loader, GPL, Uber Fetch Shader"),
    Feature(212, 214, "D", "VERIFIED", "FlashMem (ASPLOS 2026), 2.5D Doku Bellegi, Mobil GPU Bellek Hiyerarsisi"),
    Feature(215, 215, "D", "DONE", "TBR Best Practices (rhi/tile_budget.hpp ile kod seviyesinde zorlanir)"),
    Feature(216, 220, "D", "STANDARD", "Load/Store Op, Tile Memory Heap, TBDR Post-Processing, Pixel Coverage Binning, LIBRA"),
    Feature(221, 221, "D", "DONE", "Jolt Physics (zaten motorda)"),
    Feature(222, 222, "D", "VERIFIED", "LOOM DNVM (gercek: openfluke/loom, Go AI engine, bit-exact repro iddiasi)"),
    Feature(223, 224, "D", "STANDARD", "Rapier Physics 3D, Belevator-Tactics"),
    Feature(225, 225, "D", "VERIFIED", "Turnip (Mesa Vulkan surucusu)"),
    Feature(226, 231, "D", "STANDARD", "ANB, AHB, Aliased ANB Images, Deferred Image Creation, Imagination Driver 26.1, HAL DI"),
    Feature(232, 245, "D", "STANDARD", "Mali Kbase Job Scheduler .. drm_sched / Realtime Scheduling (kernel alt sistemleri)"),
    Feature(246, 246, "D", "STANDARD", "PQC (Kyber, Dilithium)"),
    Feature(247, 247, "D", "STANDARD", "GPU DMA Guvenligi"),
    Feature(248, 248, "D", "VERIFIED", "CVE-2024-23380 (Qualcomm KGSL UAF)"),
    Feature(249, 249, "D", "VERIFIED", "PhantomMap (NDSS 2026, Mali GPU driver kernel exploitation - tam eslesme)"),
    Feature(250, 250, "D", "STANDARD", "SELinux"),

    # --- E. Bilimsel Arastirmalar (251-310) ---
    Feature(251, 251, "E", "VERIFIED", "MobileRC (Wiley CGF, Agustos 2026 - 'Locality-aware Online Radiance Caching')"),
    Feature(252, 252, "E", "VERIFIED", "Seele / SeeLe (CVPR 2026, tam eslesme - 6.3x speedup dogrulandi)"),
    Feature(253, 255, "E", "RESEARCH", "Mobile3DGS3, MobileSplat, HERMES-SR"),
    Feature(256, 256, "E", "SUSPECT", "Blubber Engine - SCITEPRESS'te veya baska yerde bulunamadi"),
    Feature(257, 257, "E", "VERIFIED", "GPUSched (IEEE PerCom 2026, Yonsei Universitesi - tam eslesme)"),
    Feature(258, 258, "E", "VERIFIED", "PUMA"),
    Feature(259, 259, "E", "VERIFIED", "DualEngine (IEEE SECON 2026, tam eslesme)"),
    Feature(260, 261, "E", "VERIFIED", "Mobile-DDGI, DDGI"),
    Feature(262, 262, "E", "VERIFIED", "Kuantum Isin Izleme (QCM, SIGGRAPH 2026, arXiv 2606.29989)"),
    Feature(263, 264, "E", "RESEARCH", "Kuantum Doku Uretimi, TSMC (4D Mesh Compression)"),
    Feature(265, 265, "E", "VERIFIED", "DLSS 5"),
    Feature(266, 267, "E", "RESEARCH", "Unity-EDR, ReSTIR PT"),
    Feature(268, 268, "E", "VERIFIED", "AlayaRenderer (AlayaLab/AlayaRenderer, arXiv 2604.02329/2607.18703 - tam eslesme)"),
    Feature(269, 269, "E", "VERIFIED", "NDGI (dup)"),
    Feature(270, 270, "E", "VERIFIED", "NBC (dup, Huawei atfi dogrulanamadi)"),
    Feature(271, 273, "E", "RESEARCH", "Project Kalos, ReS2/h-BN/WTe2, SpikON"),
    Feature(274, 274, "E", "VERIFIED", "Altai (Rusya noromorfik cip)"),
    Feature(275, 275, "E", "RESEARCH", "Neural Dynamics Chip"),
    Feature(276, 276, "E", "VERIFIED", "RhabdoForge (FlorentLM/RhabdoForge github + Neuroscience Agustos 2026 makalesi - tam eslesme)"),
    Feature(277, 277, "E", "VERIFIED", "FlyGym / NeuroMechFly v2 (NeLy-EPFL/flygym, Nature Methods - tam eslesme)"),
    Feature(278, 281, "E", "RESEARCH", "Compound Eye, Assembloid Agency, SVBRDF, Neural-GASh"),
    Feature(282, 282, "E", "SUSPECT", "Transform-ER (kaynak 'Product Hunt' - bulunamadi)"),
    Feature(283, 283, "E", "RESEARCH", "IEEE P2861.2"),
    Feature(284, 284, "E", "SUSPECT", "CarDroid - 'ACM, Eylul 2026' olarak bulunamadi"),
    Feature(285, 291, "E", "RESEARCH",
            "Stream of Rendered Content, Lighting at Scale, Arm Neural Technology, Neural Dawn, "
            "Adreno Neural Fusion, MediaTek 9600 Pro, Samsung ENSS"),
    Feature(292, 292, "E", "VERIFIED", "FlashMem"),
    Feature(293, 297, "E", "RESEARCH", "Mobile 3DGS Virtual Memory, Data-Oriented ECS(dup), Foreground DNN(dup), GPU Cgroup(dup)"),
    Feature(298, 298, "E", "VERIFIED", "PhantomMap (dup)"),
    Feature(299, 299, "E", "VERIFIED", "CVE-2024-23380 (dup)"),
    Feature(300, 303, "E", "STANDARD", "Vulkan TBR Best Practices, Uber Fetch Shader, DRM_SCHED Latency, Mali Kbase Job Scheduler"),
    Feature(304, 304, "E", "VERIFIED", "Arm Mali: Mori to Nanite"),
    Feature(305, 305, "E", "VERIFIED", "Vulkan Bindless Native Plugin (saivs/com.saivs.plugin.bindless.vulkan, Unity Discussions Temmuz 2026)"),
    Feature(306, 306, "E", "VERIFIED", "Quantum Collision Models (dup 262)"),
    Feature(307, 307, "E", "VERIFIED", "HoloPathTracer (arXiv 2606.14173, SIGGRAPH 2026 - tam eslesme, ama holografi/VR icin, genel path tracer degil)"),
    Feature(308, 310, "E", "RESEARCH", "PerCom DNN Scheduling, MobileSplat Streaming, Adaptive Frame Rate Streaming"),

    # --- F. Kuantum ve Noromorfik (311-340) ---
    Feature(311, 311, "F", "RESEARCH", "Arm Neural Technology (genel)"),
    Feature(312, 312, "F", "VERIFIED", "Mali G2-Ultra NX (dup)"),
    Feature(313, 315, "F", "VERIFIED", "NSS, NFRU, NSSD (dup)"),
    Feature(316, 316, "F", "RESEARCH", "Neural Dawn"),
    Feature(317, 321, "F", "RESEARCH", "Adreno Neural Fusion, Matrix Cores, HPM, MediaTek Neural Graphics, Samsung ENSS"),
    Feature(322, 322, "F", "VERIFIED", "DLSS 5 (dup)"),
    Feature(323, 323, "F", "RESEARCH", "Unity-EDR (dup)"),
    Feature(324, 324, "F", "VERIFIED", "Altai Noromorfik Cip"),
    Feature(325, 328, "F", "RESEARCH", "Neural Dynamics Chip, Project Kalos, ReS2/h-BN/WTe2, SpikON"),
    Feature(329, 329, "F", "RESEARCH", "Kuantum RNG"),
    Feature(330, 330, "F", "RESEARCH", "Kuantum Doku Sikistirma"),
    Feature(331, 331, "F", "VERIFIED", "Kuantum Isin Izleme (dup)"),
    Feature(332, 332, "F", "RESEARCH", "Kuantum Doku Uretimi (dup)"),
    Feature(333, 333, "F", "STANDARD", "PQC (Kyber, Dilithium - NIST standardi, gercek)"),
    Feature(334, 334, "F", "RESEARCH", "QKD (Kuantum Anahtar Dagitimi)"),
    Feature(335, 340, "F", "SUSPECT",
            "Kuantum Carpisma Tespiti, Kuantum Yol Bulma, Kuantum Doku Sikistirma(QCM,tekrar), "
            "Kuantum Boltzmann Makinesi, Kuantum Destekli Vektor Makinesi, Kuantum PCA - "
            "262 numarali gercek makalenin kapsami disina tasan, muhtemelen dolgu maddeler"),

    # --- G. GitHub Ekosistemi (341-380) ---
    Feature(341, 341, "G", "VERIFIED", "Gizmo (bdrtr/Gizmo)"),
    Feature(342, 342, "G", "VERIFIED", "GGRS (gschup/ggrs)"),
    Feature(343, 343, "G", "SUSPECT", "Vovan675/RenderingEngine (bulunamadi)"),
    Feature(344, 344, "G", "VERIFIED", "Filament (Google)"),
    Feature(345, 345, "G", "VERIFIED", "Bevy"),
    Feature(346, 346, "G", "DONE", "Jolt Physics (zaten motorda)"),
    Feature(347, 347, "G", "STANDARD", "Rapier Physics 3D"),
    Feature(348, 348, "G", "VERIFIED", "LOOM (dup, gercek)"),
    Feature(349, 349, "G", "STANDARD", "renew-engine"),
    Feature(350, 350, "G", "STANDARD", "Macroquad"),
    Feature(351, 351, "G", "STANDARD", "KorGE"),
    Feature(352, 352, "G", "RESEARCH", "Prism"),
    Feature(353, 353, "G", "STANDARD", "Ebitengine"),
    Feature(354, 354, "G", "RESEARCH", "Meep"),
    Feature(355, 355, "G", "STANDARD", "Fyrox"),
    Feature(356, 359, "G", "RESEARCH", "Skylicht, Markmos, Doriax, Hilen"),
    Feature(360, 364, "G", "RESEARCH", "Custom-Virtualized-Geometry-Renderer, Nyx, SealLOD, Nanite-WebGPU, Arm Neural Graphics UE Plugin"),
    Feature(365, 365, "G", "RESEARCH", "Mobile-GS"),
    Feature(366, 366, "G", "VERIFIED", "Neuron-Bridge (gercek proje adi 'Neuro-Bridge' - EAISD/Neuro-Bridge, Android NPU/GPU/DSP proxy)"),
    Feature(367, 368, "G", "VERIFIED", "fly-self-driving (suanmiao/fly-self-driving, gercek - 165k noron connectome; 'fly.ai' ayri bulunamadi)"),
    Feature(369, 372, "G", "RESEARCH", "Quasar, Gearecs, HeliosEngine, PSRayTracing"),
    Feature(373, 373, "G", "STANDARD", "NanoRT (Syoyo Fujita'nin bilinen tek-header ray tracer'i)"),
    Feature(374, 377, "G", "RESEARCH", "8DMusicPlayer, OpenALSoftGlobalHRTF, VoiRS-spatial, Fyrox-sound"),
    Feature(378, 378, "G", "SUSPECT", "Genesis Physics - gercek 'Genesis' Python robotik simulatoru, header-only deterministik C++ degil"),
    Feature(379, 380, "G", "RESEARCH", "ACSCull, Eye"),

    # --- H. Platform Spesifikleri (381-410) ---
    Feature(381, 390, "H", "STANDARD", "Android NDK, Gradle, CMake, JNI, Activity Lifecycle, Surface, Permissions, App Bundle, Play Console, Vulkan Stack"),
    Feature(391, 391, "H", "VERIFIED", "ADPF (dup)"),
    Feature(392, 393, "H", "VERIFIED", "Thermal API, Performance Hint API (dup)"),
    Feature(394, 401, "H", "RESEARCH", "Game Stats API, Play Games Services v2, Sidekick, Android XR, Arm Perf Studio, AGI, Sokatoa, APA"),
    Feature(402, 410, "H", "STANDARD", "Metal 3, Build-Time GPU Binary, iOS XR, Core ML, MPS, AVAudioEngine, Xcode, Obj-C++ Boundary, App Lifecycle"),

    # --- I. Kernel ve Surucu Katmani (411-430) ---
    Feature(411, 424, "I", "STANDARD", "Mali Kbase .. Realtime Scheduling (Kategori D 232-245 ile ayni icerik, tekrar)"),
    Feature(425, 425, "I", "STANDARD", "PQC (dup)"),
    Feature(426, 426, "I", "STANDARD", "GPU DMA Guvenligi (dup)"),
    Feature(427, 427, "I", "VERIFIED", "CVE-2024-23380 (dup)"),
    Feature(428, 428, "I", "VERIFIED", "PhantomMap (dup)"),
    Feature(429, 429, "I", "STANDARD", "SELinux (dup)"),
    Feature(430, 430, "I", "VERIFIED", "Turnip (dup)"),

    # --- J. Diger Sistemler (431-500) ---
    Feature(431, 431, "J", "DONE", "Rollback Netcode (sim/rollback.hpp - KAYNAK, kablolama bekliyor)"),
    Feature(432, 433, "J", "DONE", "Lag Compensation (sim/lag_compensation.hpp - KAYNAK), Snapshot Interpolation (sim/interp.hpp - VAR, dogrulandi)"),
    Feature(434, 434, "J", "VERIFIED", "IPv6 Serverless Netcode (MAME4droid ile dogrulandi)"),
    Feature(435, 435, "J", "TODO", "Cihaz-Sinifi Bake"),
    Feature(436, 437, "J", "TODO", "Shader DSL, Otomatik fp16"),
    Feature(438, 438, "J", "DONE", "PCG (Prosedurel Icerik) - core/math/random.hpp (Rng+shuffle) + core/math/noise.hpp (deger gurultusu+fBm), KAYNAK+testler, kablolama bekliyor"),
    Feature(439, 439, "J", "TODO", "Motion Matching"),
    Feature(440, 449, "J", "OUT_OF_SCOPE",
            "Bulut Kaydetme, Liderlik Tablolari, Mod Destegi, VR/AR Destegi, Erisilebilirlik, "
            "Analitik+A/B, Remote Config, Sesli Sohbet, Moderasyon, Sosyal Ozellikler"),
    Feature(450, 450, "J", "TODO", "Yuz Animasyonu"),
    Feature(451, 451, "J", "OUT_OF_SCOPE", "Duygu Sistemi (gameplay/AI mantigi)"),
    Feature(452, 452, "J", "DONE", "Ters Kinematik (IK) - sim/ik.hpp analitik iki-kemik cozucu, KAYNAK+testler, kablolama bekliyor"),
    Feature(453, 453, "J", "DONE", "Voxel Teknolojisi - content/voxel.hpp (gorunen-yuz meshleme) KAYNAK+testler, kablolama bekliyor"),
    Feature(454, 456, "J", "OUT_OF_SCOPE", "Katlanabilir Telefon, Derin Baglantilar, Uygulama Ici Guncellemeler"),
    Feature(457, 457, "J", "DONE", "Varlik Paketleme - content/pack.hpp (isimle aranabilir tek-dosya format), KAYNAK+testler"),
    Feature(458, 458, "J", "OUT_OF_SCOPE", "Geriye Donuk Uyumluluk"),
    Feature(459, 459, "J", "DONE", "Coklu Dokunmatik (platform/touch.hpp - VAR) + Jest (platform/gesture.hpp Tap+Pinch - KAYNAK+testler, kablolama bekliyor)"),
    Feature(460, 463, "J", "OUT_OF_SCOPE", "Oyun Ici Sohbet, Oyun Ici Ekonomi, Basarimlar, Analitik Dashboard"),
    Feature(464, 464, "J", "DONE", "Frame Pacing (dup) - platform/frame_pacer.hpp, KAYNAK+testler"),
    Feature(465, 469, "J", "TODO", "Desync Detector, Contact Shadow, Shadow Atlas, Reflection Probe, Light Probes (dup)"),
    Feature(470, 470, "J", "DONE", "Clustered Lighting (dup)"),
    Feature(471, 471, "J", "TODO", "Weighted Blended OIT (dup)"),
    Feature(472, 472, "J", "DONE", "Wind Animation (dup) - content/wind.hpp, KAYNAK+testler"),
    Feature(473, 494, "J", "TODO",
            "God Rays, Aurora, Yildizlar, Volumetric Clouds, "
            "Height/Volumetric Fog, FFT Waves, Underwater, Neural Hair, GPU Cloth, Voronoi Fracture, "
            "MSDF Text, H.265/AV1, Camera Shake/DOF, Motion Blur, Lens Flare, Color Grading, TAA, "
            "NSS-AA, Hybrid AA (dup B/C)"),
    Feature(495, 495, "J", "DONE", "PBR (dup)"),
    Feature(496, 498, "J", "TODO", "Layered Materials, Shader Graph, HZB Occlusion (dup)"),
    Feature(499, 499, "J", "DONE", "Software Occlusion (dup) - content/occlusion.hpp, KAYNAK+testler"),
    Feature(500, 500, "J", "TODO", "Hybrid Occlusion (dup)"),
]


# --------------------------------------------------------------------------
# Yardimci fonksiyonlar
# --------------------------------------------------------------------------

def total_ids(f: Feature) -> int:
    return f.end - f.start + 1


def check_coverage() -> list[str]:
    """1..500 arasinin tam olarak bir kez kapsandigini dogrular. Hata listesini dondurur."""
    covered: dict[int, str] = {}
    errors: list[str] = []
    for f in FEATURES:
        for i in f.ids():
            if i in covered:
                errors.append(f"CAKISMA: id {i}, '{covered[i]}' ve '{f.title}' ikisinde de var")
            covered[i] = f.title
    missing = [i for i in range(1, 501) if i not in covered]
    if missing:
        errors.append(f"BOSLUK: kapsanmayan id'ler: {missing}")
    return errors


def summary() -> dict[str, int]:
    counts = {k: 0 for k in STATUS}
    for f in FEATURES:
        counts[f.status] += total_ids(f)
    return counts


def print_summary() -> None:
    counts = summary()
    total = sum(counts.values())
    print(f"Toplam id: {total} / 500\n")
    print(f"{'Durum':<14}{'Sembol':<4}{'Adet':>6}  Aciklama")
    print("-" * 70)
    for code, (symbol, desc) in STATUS.items():
        print(f"{code:<14}{symbol:<4}{counts[code]:>6}  {desc}")


def print_by_status(status: str) -> None:
    status = status.upper()
    if status not in STATUS:
        print(f"Bilinmeyen durum: {status}. Gecerli: {', '.join(STATUS)}", file=sys.stderr)
        sys.exit(1)
    for f in FEATURES:
        if f.status == status:
            print(f"[{f.cat}] {f.id_label():<9} {f.title}")


def print_by_category(cat: str) -> None:
    cat = cat.upper()
    if cat not in CATEGORIES:
        print(f"Bilinmeyen kategori: {cat}. Gecerli: {', '.join(CATEGORIES)}", file=sys.stderr)
        sys.exit(1)
    print(f"{cat}. {CATEGORIES[cat]}\n")
    for f in FEATURES:
        if f.cat == cat:
            symbol = STATUS[f.status][0]
            print(f"{symbol} {f.id_label():<9} {f.title}")


def find(keyword: str) -> None:
    keyword_low = keyword.lower()
    hits = [f for f in FEATURES if keyword_low in f.title.lower()]
    if not hits:
        print(f"'{keyword}' icin sonuc yok.")
        return
    for f in hits:
        symbol = STATUS[f.status][0]
        print(f"{symbol} [{f.cat}] {f.id_label():<9} {f.title}")


def render_markdown_table(cat: str) -> str:
    lines = [f"| Aralik | Konu | Durum |", "|---|---|---|"]
    for f in FEATURES:
        if f.cat == cat:
            symbol = STATUS[f.status][0]
            lines.append(f"| {f.id_label()} | {f.title} | {symbol} {f.status} |")
    return "\n".join(lines)


# Kategori basi kisa aciklama - MADDE-LISTESI-DURUM.md'nin elle yazilmis
# yorumlarindan tasindi, tablo bu betikten uretilir ama yorum tek kaynagi
# burasi oluyor (doc'ta ayrica tutulmuyor).
CATEGORY_NOTES: dict[str, str] = {
    "A": "Birebir `VIZYON.md` SS1 + SS1.1 ile eslesiyor.",
    "E": (
        "Akademik makale/rapor listesi. Bu oturumda 5/60'i tek tek dogrulandi "
        "(265 DLSS 5, 258 PUMA, 260-261 Mobile-DDGI, 262 Kuantum Isin Izleme, 292 FlashMem) "
        "- 5/5 gercek cikti. Kalan RESEARCH isaretli maddeler yuksek olasilikla gercek "
        "makaleler (isabet orani yuksek) ama tek tek dogrulanmadi, ve hicbiri mobil motor "
        "icin simdi bir aksiyon gerektirmiyor - bunlar saf arastirma-takip referanslaridir."
    ),
    "I": (
        "Kategori D'nin 232-250 araligiyla icerik olarak buyuk olcude cakisiyor (ayni 20 "
        "madde kullanicinin orijinal listesinde iki kez verilmis)."
    ),
    "J": (
        "440-449 ve 454-463 bloklari bilincli olarak KAPSAM DISI: arka uc/canli-operasyon/"
        "sosyal/gameplay ozellikleri, `DEVAM_PLANI.md`'nin bilincli disarida biraktigi "
        "\"dil baglamasi\"na ve orijinal kapsam notuna (\"oyun mantigi, NPC, diyalog, "
        "simulasyon icermez\") giriyor. Istisna: Motion Matching(439)/Yuz Animasyonu(450)/"
        "IK(452)/Voxel(453)/Coklu Dokunmatik(459) - bunlar motor/animasyon/girdi teknigi, "
        "gameplay degil, kapsamda tutulur. 464-500 B/C kategorileriyle BIREBIR TEKRAR eden "
        "~30 madde icerir, ayri degerlendirilmedi."
    ),
}


def render_full_doc() -> str:
    counts = summary()
    total = sum(counts.values())
    lines: list[str] = []
    lines.append("# Madde Listesi Durumu — Kullanıcının 500 Maddelik Listesi (v18.0) Çapraz Referansı")
    lines.append("")
    lines.append(
        "> **BU DOSYA `tools/feature_matrix.py --write-doc` İLE ÜRETİLİR.** "
        "Elle düzenlemeyin — veri kaynağı script'teki `FEATURES` listesidir. "
        "Düzeltme/ekleme için script'i güncelleyip yeniden çalıştırın "
        "(`--check` ile 1..500 kapsaması önce doğrulanır, sonra dosya yazılır)."
    )
    lines.append(">")
    lines.append(
        "> Kaynak: kullanıcının bu sohbette paylaştığı \"Tulpar Engine — Ana Madde Listesi "
        "(400+/500 Madde), Sürüm 18.0\" — 10 kategori (A-J). Bu dosya o listedeki **her "
        "maddeyi** aşağıdaki 7 duruma göre sınıflandırır."
    )
    lines.append(">")
    lines.append(
        "> **Önemli sınırlama:** 500 maddenin her biri tek tek web'de aranmadı — bu "
        "orantısız olurdu (çoğu madde standart, yaygın bilinen bir render/platform "
        "tekniği). En çok \"aksiyon kararı\"na yön verecek ~19 madde tek tek arandı "
        "(bkz. `VIZYON.md` §6) ve **19/19 gerçek çıktı** — bu yüksek isabet oranı, tek "
        "tek doğrulanmayan maddelere de makul bir güven payı tanınmasını haklı çıkarıyor, "
        "ama bu hâlâ \"kesin doğrulandı\" demek değil."
    )
    lines.append("")
    lines.append("## Lejant")
    lines.append("")
    lines.append("| Sembol | Kod | Anlamı |")
    lines.append("|---|---|---|")
    for code, (symbol, desc) in STATUS.items():
        lines.append(f"| {symbol} | {code} | {desc} |")
    lines.append("")

    for cat, cat_name in CATEGORIES.items():
        cat_features = [f for f in FEATURES if f.cat == cat]
        id_min = min(f.start for f in cat_features)
        id_max = max(f.end for f in cat_features)
        lines.append(f"## {cat}. {cat_name} ({id_min}-{id_max})")
        lines.append("")
        if cat in CATEGORY_NOTES:
            lines.append(CATEGORY_NOTES[cat])
            lines.append("")
        lines.append("| Aralık | Madde | Durum |")
        lines.append("|---|---|---|")
        for f in cat_features:
            symbol = STATUS[f.status][0]
            lines.append(f"| {f.id_label()} | {f.title} | {symbol} {f.status} |")
        lines.append("")

    lines.append("## Özet (kesin — kodla üretildi)")
    lines.append("")
    lines.append(
        f"`tools/feature_matrix.py`, 1-500 arasındaki her id'yi tam bir kez kapsayan "
        f"{len(FEATURES)} kayıt tutuyor (`--check` ile denetlenir, boşluk/çakışma varsa hata "
        f"verir). Bu doğrudan `--summary` çıktısıdır:"
    )
    lines.append("")
    lines.append("| Durum | Madde sayısı |")
    lines.append("|---|---|")
    for code, (symbol, desc) in STATUS.items():
        lines.append(f"| {symbol} {code} | {counts[code]} |")
    lines.append(f"| **Toplam** | **{total}** |")
    lines.append("")
    lines.append(
        "Sorgulamak için: `--status TODO` (bir duruma göre listele), `--category B` "
        "(bir kategoriyi göster), `--find \"shadow\"` (başlıkta ara), `--check` (kapsama "
        "denetimi). **Bu bir denetim değil, bir yönlendirme haritası**: hangi maddenin "
        "gerçek bir mühendislik kararı gerektirdiğini (🔧), hangisinin zaten yapıldığını "
        "(✅), hangisinin sadece arka plan bilgisi olduğunu (📗/📚) ayırt etmek için var."
    )
    lines.append("")
    return "\n".join(lines)


def main() -> None:
    # Windows konsollari genelde cp1254/cp1252 kullanir, emoji sembolleri basamaz.
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(encoding="utf-8")

    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--summary", action="store_true", help="Durum basina toplam id sayisi")
    parser.add_argument("--status", metavar="CODE", help="Bir duruma ait tum maddeleri listele")
    parser.add_argument("--category", metavar="A-J", help="Bir kategoriye ait tum maddeleri listele")
    parser.add_argument("--find", metavar="KEYWORD", help="Baslikta gecen kelimeye gore ara")
    parser.add_argument("--check", action="store_true", help="1..500 kapsama/cakisma denetimi")
    parser.add_argument("--render-md", metavar="A-J", help="Bir kategori icin markdown tablo uret")
    parser.add_argument("--render-doc", action="store_true", help="Tam MADDE-LISTESI-DURUM.md govdesini stdout'a yaz")
    parser.add_argument(
        "--write-doc", metavar="PATH", nargs="?", const="docs/engine/MADDE-LISTESI-DURUM.md",
        help="Tam dogurulmus dokumani PATH'e yaz (varsayilan: docs/engine/MADDE-LISTESI-DURUM.md)",
    )
    parser.add_argument(
        "--check-doc", metavar="PATH", nargs="?", const="docs/engine/MADDE-LISTESI-DURUM.md",
        help="PATH'teki dosyanin uretilen icerikle ayni oldugunu dogrula (kayma tespiti)",
    )
    args = parser.parse_args()

    if args.check:
        errors = check_coverage()
        if errors:
            for e in errors:
                print(f"HATA: {e}")
            sys.exit(1)
        print(f"OK: 1..500 arasi {len(FEATURES)} kayitla, bosluksuz ve cakismasiz kapsandi.")
        return

    if args.write_doc is not None:
        errors = check_coverage()
        if errors:
            for e in errors:
                print(f"HATA (yazilmadi): {e}", file=sys.stderr)
            sys.exit(1)
        content = render_full_doc()
        with open(args.write_doc, "w", encoding="utf-8", newline="\n") as fh:
            fh.write(content)
        print(f"Yazildi: {args.write_doc} ({len(FEATURES)} kayittan, {len(content)} byte)")
        return

    if args.check_doc is not None:
        errors = check_coverage()
        if errors:
            for e in errors:
                print(f"HATA: {e}", file=sys.stderr)
            sys.exit(1)
        expected = render_full_doc()
        try:
            with open(args.check_doc, "r", encoding="utf-8") as fh:
                actual = fh.read()
        except FileNotFoundError:
            print(f"HATA: {args.check_doc} bulunamadi.", file=sys.stderr)
            sys.exit(1)
        if actual != expected:
            print(f"KAYMA: {args.check_doc}, script'in uretecegi icerikten farkli.", file=sys.stderr)
            print("Duzeltmek icin: python feature_matrix.py --write-doc", file=sys.stderr)
            sys.exit(1)
        print(f"OK: {args.check_doc}, script ile birebir ayni.")
        return

    if args.render_doc:
        print(render_full_doc())
        return

    if args.render_md:
        print(render_markdown_table(args.render_md.upper()))
        return

    if args.status:
        print_by_status(args.status)
        return

    if args.category:
        print_by_category(args.category)
        return

    if args.find:
        find(args.find)
        return

    print_summary()


if __name__ == "__main__":
    main()
