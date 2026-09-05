# Tulpar Engine — Mimari ve Teknoloji Planı

> Hedef: mobil-öncelikli (ARM, Vulkan/Metal), tek stüdyoya özel, compile-time ağırlıklı oyun motoru.
> Genel amaçlı değil. Kendi oyunlarımız için.

---

## 0. Tasarım Aksiyomları

Aşağıdaki her karar bu 7 aksiyomdan türetildi. Bir tasarım tartışması çıkarsa çözüm bu listeye dönmektir.

| # | Aksiyom | Sonucu |
|---|---|---|
| A1 | **Runtime'da yapılabilecek her şey build'de yapılır** | Streaming manager, shader compiler, scene loader, LOD selector runtime'dan çıkar |
| A2 | **Frame içinde allocation yoktur** | GC yok, malloc yok. Arena + frame allocator. Peak memory build'de hesaplanır |
| A3 | **Bandwidth birinci sınıf kaynaktır, ALU ikinci** | Tile memory disiplini, packed formatlar, fp16 default |
| A4 | **Bütçe peak'e göre değil sustained watt'a göre kurulur** | Termal throttle sonrası (%50-70) performans hedef alınır |
| A5 | **Generic olan her şey maliyet, spesifik olan her şey kazanç** | "İleride lazım olur" diye soyutlama yazılmaz |
| A6 | **CPU ve GPU tek dil, tek compiler, tek tip sistemi** | Layout uyumsuzluğu compile error olur, runtime bug olmaz |
| A7 | **Frame time varyansı, ortalama frame time'dan önemlidir** | Hitch = bug. 99th percentile ölçülür, ortalama değil |

---

## 1. Teknoloji Envanteri

Sektörde "devrimsel" sayılan her şey ve bizim kararımız.
**AL** = alıyoruz · **ADAPTE** = fikri alıyoruz, uygulaması değişiyor · **ERTELE** = sonraki faz · **ATLA** = almıyoruz, gerekçe yazılı

### 1.1 Geometri Pipeline

| Teknoloji | Kaynak | Ne çözüyor | Karar | Not |
|---|---|---|---|---|
| **Cluster DAG + continuous LOD** (Nanite'ın A yarısı) | UE5 Nanite | LOD popping, draw call, aşırı geometri | **ADAPTE** | ~128 tri cluster. DAG cut'ı runtime yerine **volume başına bake edilir** (A1) |
| **Software rasterizer + 64-bit atomic** (Nanite'ın B yarısı) | UE5 Nanite | Piksel-altı üçgen verimi | **ATLA** | TBDR tile avantajını yok eder, DRAM atomic mobilde pahalı, çözdüğü problem bizde yok |
| **GPU-driven rendering** (compute culling + indirect draw) | Ubisoft AC (Haar & Aaltonen, SIGGRAPH 2015) | CPU draw call darboğazı | **AL** | `drawIndexedIndirect` + compute cull. **Multi-draw indirect kullanma** — Android driver desteği güvenilmez |
| **Mesh shaders / primitive shaders** | UE5, Insomniac | Geometri amplification | **ATLA** | Sadece flagship. İkinci kod yolu = iki kat bakım. Compute+indirect yeterli |
| **Meshlet cone culling** | id Tech / Nanite | Backface cluster elemesi | **AL** | Cluster builder'ın zaten ürettiği veri. Bedava kazanç |
| **Vertex stream splitting** (position-only + attribute) | Konsol standardı | TBR binning pass bandwidth'i | **AL** | Binning sadece pozisyon okur → geometri bandwidth'i ~yarıya iner. Mobilde kritik |
| **Geometry quantization** | Nanite, Decima | Vertex bellek + bandwidth | **AL** | Compiler content value-range'ini bildiği için **bit sayısını kendi seçer** (A6) |

### 1.2 Shading & Lighting

| Teknoloji | Kaynak | Ne çözüyor | Karar | Not |
|---|---|---|---|---|
| **Visibility buffer** | Burns & Hunt 2013, Aaltonen | G-buffer bandwidth'i | **AL — birinci öncelik** | 8 byte/px vs deferred'ın 24-32'si. Mobilde masaüstünden daha değerli |
| **Clustered forward+** | id Tech 6, Avalanche | Çok ışıklı sahne | **AL** | Visibility buffer'ın material resolve pass'i ile birleşir |
| **Stochastic tile-based lighting** | **HypeHype** (SIGGRAPH 2025) | Düşük-uçlu mobilde **sabit maliyetli** dinamik ışık + gölge | **AL — bizim için tasarlanmış** | İki aşamalı reservoir sampling: big-tile SRS → small-tile resample. Wave coherence ve bandwidth için optimize. Işık sayısından bağımsız maliyet. **Işık seçimini temporal feedback ile stabilize et** (MegaLights'tan alınan tek fikir) — stokastik seçim frame'ler arası titrer |
| **Order-independent transparency (AVBOIT)** | Call of Duty (SIGGRAPH 2025) | Şeffaflık sıralama + maliyet | **ERTELE** | Particle/şeffaflık mobilde ana dert. Adaptive voxel yaklaşımı incelenmeli, Faz 5 |
| **Subpass / framebuffer fetch deferred** | ARM/Imagination best practice | G-buffer'ı DRAM'e indirmemek | **AL — zorunlu** | Tüm mobil kazancının kaynağı. G-buffer on-chip tile memory'de kalır |
| **Pixel Local Storage** | Mali extension | Aynı şey, GLES tarafı | **ATLA** | Vulkan subpass zaten karşılığı. GLES backend almıyoruz |
| **Lumen / realtime GI** | UE5 | Dinamik GI | **ATLA** | Watt bütçesi yok. Bake + küçük dinamik katman kalitede yener |
| **Neural dynamic GI (NDGI)** | Tencent MagicDawn, açık kaynak | Dinamik GI, sinir ağı inference ile | **ERTELE — Faz 6'da ölç** | ⚠️ §8/3'ün tek gerçek meydan okuyucusu. Açık kaynak, cross-platform, Arm ile mobil GPU optimizasyonu yapılmış. Taahhüt etme, **prototiple ve ölç** (bkz. EK F.3) |
| **SVOGI** | CryEngine | Dinamik GI | **ATLA** | Aynı gerekçe |
| **Irradiance volume + bake edilmiş GI** | Standart | GI, ucuz | **AL** | Probe grid, build'de bake. Dinamik obje probe'dan sample'lar. **Emissive yüzeylerin probe katkısı da build'de bake edilir** — emission sadece glow değil, ışık kaynağı |
| **Virtual Shadow Maps** | UE5 | Gölge çözünürlüğü + maliyeti | **ADAPTE** | Sparse memory yok → indirection texture + physical atlas ile emüle. **Page cache** en büyük kazanç: statik gölge her frame çizilmez. Düşük cihaz kademesi için **blob/projector gölge** fallback'i — ama bu bir *bake zamanı cihaz sınıfı kararı*, runtime termal anahtarı değil (görsel sıçrama yaratır) |
| **Cascaded shadow maps** | Standart | Baseline gölge | **AL** | VSM'e kadar köprü, sonra fallback olarak kalır |
| **Variable Rate Shading** | `VK_KHR_fragment_shading_rate` | Fillrate | **AL** | ⚠️ **Değişti.** Vulkan Roadmap 2026 VRS'i zorunlu kıldı — artık opsiyonel bir ekstra değil, baseline |
| **fp16 (mediump) default precision** | Mobil altın kural | ALU throughput 2x | **AL — zorunlu** | Compiler value-range analiziyle **otomatik** yapar, elle `half` yazılmaz (A6) |
| **Bindless / descriptor indexing** | Modern standart | Descriptor bind maliyeti | **AL** | `descriptorIndexing` Vulkan 1.2, Android'de yaygın. Baseline şartımız |
| **Neural texture compression** | NVIDIA 2023+ | Texture bellek | **ATLA** | Mobil NPU/GPU entegrasyonu olgun değil |
| **Gaussian splatting** | SIGGRAPH 2023 | Fotogerçekçi capture | **ATLA** | Bizim içerik pipeline'ımıza uymuyor |

### 1.3 Çözünürlük & Temporal

| Teknoloji | Kaynak | Ne çözüyor | Karar | Not |
|---|---|---|---|---|
| **Temporal upscaling** | **Arm ASR** (FSR2 türevi, MIT) | Fillrate — en büyük tek kazanç | **AL — ikinci öncelik, ama YAZMA, ENTEGRE ET** | Arm ASR custom engine'ler için generic library olarak MIT lisanslı yayınlandı. Kendi upscaler'ımızı yazmak gereksiz risk. Motion vector zorunlu, baştan planla |
| **TAA** | Standart | Aliasing | **AL** | Upscaler ile aynı altyapı (history buffer, motion vector, reprojection) |
| **Checkerboard rendering** | Decima, R6 Siege | Fillrate | **ATLA** | Temporal upscaler daha iyi ve daha genel |
| **MSAA** | — | Aliasing | **AL (opsiyonel path)** | Mobilde tile içinde resolve → neredeyse bedava. Masaüstü sezgisi burada ters |
| **Dynamic resolution scaling** | Konsol standardı | Termal throttle'a tepki | **AL — zorunlu** | A4'ün doğrudan sonucu. Frame budget'ı aşınca res düşer, fps düşmez |
| **ADPF Thermal Headroom API** | Google Android | Throttle'ı **olmadan önce** görmek | **AL — zorunlu** | Dynamic resolution'ın tetikleyicisi. Reaktif değil proaktif: headroom düşerken kalite düşer, throttle hiç başlamaz |
| **Frame pacing** | Android Swappy / CAMetalDisplayLink | Jitter | **AL — zorunlu** | A7. Kilitli 60 > oynak 75 |

### 1.4 Streaming & Content

| Teknoloji | Kaynak | Ne çözüyor | Karar | Not |
|---|---|---|---|---|
| **Virtual texturing / MegaTexture** | id Tech 5 (Carmack) | Texture bellek + bandwidth | **AL** | Feedback buffer ile. Mobilde bellek baskısı yüksek, değer yüksek |
| **Sampler feedback** | DX12U | VT feedback donanımda | **ATLA** | Mobilde yok, shader-side feedback yazacağız |
| **World Partition / One File Per Actor** | UE5 | Büyük dünya + merge conflict | **ADAPTE** | Grid partition alıyoruz. OFPA fikri content pipeline'a giriyor (bkz. §6) |
| **Runtime streaming manager** | Herkes | Bellek | **ATLA — kasıtlı** | A1. Sahne başına resident set **build'de hesaplanır ve bake edilir**. Runtime'da streaming kodu çalışmaz |
| **Addressables / content catalog** | Unity | DLC, patch | **ADAPTE** | Pack formatı bizim, catalog compile-time üretilir |

### 1.5 CPU Mimarisi

| Teknoloji | Kaynak | Ne çözüyor | Karar | Not |
|---|---|---|---|---|
| **Fiber-based job system** | Naughty Dog (Gyrling, GDC 2015) | İş parçacığı, bekleme, cache | **AL** | Fiber'lar mobilde de çalışır. `ucontext` / kendi ARM64 switch'imiz |
| **Archetype ECS (SoA)** | Unity DOTS, Bevy, flecs | Cache locality | **ADAPTE** | Archetype'lar runtime'da kurulmaz, **compiler üretir** (A6) |
| **Burst-style auto-vectorization** | Unity Burst | SIMD | **ADAPTE** | LLVM zaten bizde. NEON intrinsics + auto-vec. ⚠️ **`-ffast-math` deterministik sim ile çakışır** — reassociation ve FTZ platformlar arası farklı sonuç üretir. Sim kodunda strict FP zorunlu, fast-math sadece render/VFX'te |
| **Frame Graph / Render Graph** | Frostbite (O'Donnell, GDC 2017) | Barrier, transient memory, pass sırası | **ADAPTE** | Graph runtime'da kurulmaz, **build'de derlenir**. Barrier'lar sabit, aliasing planı sabit |
| **ADPF Performance Hint API** | Google Android | Frame time varyansı, core seçimi | **AL — elle pin ETME** | ⚠️ **Düzeltildi.** Elle affinity pinning yerine OS'a hedef frame süresi bildirilir, scheduler doğru core'u seçer. MediaTek ölçümü: FPS std sapması −%25, render thread −%10, sadece hint session açarak |
| **Servers architecture** | Godot | Thread ayrımı | **ATLA** | Job system daha genel, iki model gerekmez |
| **Compile-time reflection** | C++26 / Zig comptime | Serialization, editor, RPC | **AL — zorunlu** | Runtime reflection tablosu yok. Serializer, inspector, network codec compiler'dan çıkar |
| **Hot reload (Live++ sınıfı)** | Molecular Matters | Iterasyon hızı | **ERTELE** | Faz 3. AOT dil ile zor ama gameplay katmanında yapılabilir |

### 1.6 Simülasyon

| Teknoloji | Kaynak | Ne çözüyor | Karar | Not |
|---|---|---|---|---|
| **Jolt-style physics** | Horizon FW (Rouwe) | Fizik | **ADAPTE / entegre et** | Kendi fiziğini yazma. Jolt zaten SoA + job-friendly + deterministik. Bizim job system'imize bağla |
| **Motion matching** | Ubisoft For Honor (Clavet, GDC 2016) | Animasyon kalitesi | **ERTELE** | Bellek maliyeti yüksek. Faz 3, oyun türü gerektirirse |
| **Animation compression (ACL sınıfı)** | Nicholas Frechette | Anim bellek | **AL** | Bellek bütçesinde ciddi kalem |
| **GPU particle simulation** | Standart | Particle CPU maliyeti | **AL** | Compute + indirect draw |
| **Quarter-res particle RT + composite** | Mobil zorunluluk | **Overdraw — mobilde 1 numaralı katil** | **AL — zorunlu** | Particle'lar ayrı düşük-res target'a, sonra composite. Alpha-test'ten kaç (TBDR HSR'ı kırar) |
| **Recast/Detour navmesh** | Mikko Mononen | Pathfinding | **ADAPTE** | Navmesh build'de bake, runtime sadece query |
| **Rollback netcode** | GGPO | Multiplayer gecikme | **ERTELE** | Oyun türüne bağlı. Deterministik sim gerektirir — **fizik seçimini şimdiden buna göre kilitle** |
| **Deterministik fixed-step sim** | Lockstep RTS/fighting | Netcode + replay + test | **AL** | Replay = otomatik regression test altyapısı. Bedavaya gelmez ama çok kazandırır |

### 1.7 Tooling & Data

| Teknoloji | Kaynak | Ne çözüyor | Karar | Not |
|---|---|---|---|---|
| **"The Truth" data model** | Our Machinery | Undo/redo, collaboration, tooling | **AL** | Tek merkezi veri modeli. Editor'ün tamamı bunun üstüne. En değerli tooling fikri |
| **Prefab / scene diff** | Unity | İçerik yeniden kullanımı | **AL** | Diff-based override |
| **PSO precaching** | UE5 | İlk-çizim hitch'i | **ATLA — gereksiz** | A1 sayesinde **tüm PSO'lar build'de üretiliyor**. Problem tanım gereği yok |
| **Shader permutation reduction** | Herkesin derdi | Build süresi, bellek | **AL** | Sahnedeki material seti build'de bilindiği için compiler **tam gereken permutation'ı** üretir |
| **Uber shader** | Call of Duty | Permutation patlaması | **ATLA** | Yukarıdaki madde bu problemi ortadan kaldırıyor |
| **Async asset import + content hash cache** | UE/Unity | Iterasyon | **AL** | Deterministik hash, incremental build |
| **Kendi profiler'ımız (frame timeline + GPU counter)** | Herkes | Ölçüm | **AL — zorunlu** | Ölçemediğin şeyi optimize edemezsin. **Faz 0'da yazılır**, sonra değil |

---

## 2. Katman Mimarisi

```
┌─────────────────────────────────────────────────────────────┐
│ L7  TOOLING          Editor · Profiler · Asset Browser      │
│                      (The Truth data model üstünde)         │
├─────────────────────────────────────────────────────────────┤
│ L6  CONTENT PIPELINE Importer · Cluster Builder · Baker     │
│                      Scene Compiler · Shader Compiler       │  ← offline
├═════════════════════════════════════════════════════════════┤  ← BUILD / RUNTIME SINIRI
│ L5  GAMEPLAY         Tulpar kodu · Sistemler · Oyun         │  ← runtime
├─────────────────────────────────────────────────────────────┤
│ L4  SIMULATION       ECS · Physics · Animation · Audio      │
│                      Navigation · Particles                 │
├─────────────────────────────────────────────────────────────┤
│ L3  RENDERER         Compiled Render Graph · Vis Buffer     │
│                      Cluster LOD · VSM · Upscaler           │
├─────────────────────────────────────────────────────────────┤
│ L2  RHI              Vulkan 1.2 backend · Metal 3 backend   │
├─────────────────────────────────────────────────────────────┤
│ L1  CORE             Memory (arena) · Jobs (fiber) · Math   │
│                      Containers · Compile-time reflection   │
├─────────────────────────────────────────────────────────────┤
│ L0  PLATFORM         Android NDK · iOS · Window · Input · IO │
└─────────────────────────────────────────────────────────────┘
```

**Bağımlılık kuralları (ihlali build hatası olmalı):**

1. Katman sadece **altındaki** katmanı çağırır. Yukarı çağrı yok, callback yok, event bus yok.
2. L1 hiçbir şey allocate etmez — allocator **parametre olarak geçilir**. Global allocator yoktur.
3. L3 asla L4'ün tiplerini bilmez. Renderer'a ne çizeceği **veri olarak** verilir, sorgulanmaz.
4. L6 → L5 arası sınır tek yönlüdür: content pipeline runtime kodu import edemez.
5. L2'nin üstünde platform `#ifdef`'i yoktur. Backend farkı L2'de biter.

---

## 3. Katman Tasarımları

### L1 — Core

**Bellek modeli.** Tek genel amaçlı allocator yok. Dört tip:

| Allocator | Ömür | Kullanım |
|---|---|---|
| `SystemArena` | Uygulama ömrü | Boot'ta tek seferlik reserve. Subsystem'ler buradan alt-arena alır |
| `SceneArena` | Sahne ömrü | Sahne yüklenirken doldurulur, sahne değişince komple reset |
| `FrameArena` | 1 frame | Her frame `reset()`. Free çağrılmaz. Geçici her şey burada |
| `PoolAllocator<T>` | Değişken | Sabit boyutlu, free-list. Entity, particle, node. **Slotmap: index + generation counter.** Ham pointer değil handle dağıtılır; slot yeniden kullanılınca generation artar, eski handle sessizce yanlış objeye değil, tespit edilebilir bir hataya düşer |

Kural: **peak kullanım build'de hesaplanır**, boot'ta o kadar reserve edilir. Runtime'da genişleme yok. Arena dolarsa bu bir content hatasıdır, build'de yakalanır.

**Job system.** Fiber tabanlı (Naughty Dog modeli):
- Worker thread sayısı = big core sayısı (little core'lara pin edilmiş ayrı bir "async" pool)
- Job wait → fiber switch, thread bloklanmaz
- Counter tabanlı senkronizasyon, mutex yok
- ARM64 için kendi context switch (register save/restore ~20 instruction)

**Container'lar.** STL yok. `Array`, `HashMap`, `StaticString`, `Span`. Hepsi explicit allocator alır, hiçbiri kendi büyümez (capacity verilir).

**Exception ve RTTI yok — compiler zorlar.** Motor ve gameplay kodunda exception atılamaz, `dynamic_cast` benzeri runtime tip sorgusu yapılamaz. Hata yolu explicit dönüş değeri (`Result<T>`) ile taşınır. Gerekçe: exception unwinding kod boyutunu şişirir, hata yolunu görünmez kılar ve frame time'ı tahmin edilemez yapar. Tulpar bunu statik analizle doğrular, kural yorum satırında kalmaz.

**Compile-time reflection.** Tulpar compiler her `struct` için metadata üretir → serializer, editor inspector, network codec, hash **otomatik türetilir**. Runtime reflection tablosu **yoktur**, kod üretilir.

### L2 — RHI

- Baseline: **Android Vulkan Profile 2025 (AVP 2025)** / **Metal 3** (iOS)
  - ⚠️ Versiyon numarası seçme — Google'ın yayınladığı profili kullan. AVP üç kademe sunuyor ve her birinin gerçek cihaz kapsama yüzdesi Android Distribution Dashboard'da yayınlanıyor. Tahmin yerine veri
  - Android 13+ cihazlar Vulkan 1.3, Android 16+ cihazlar Vulkan 1.4 desteklemek **zorunda**. 2027 hedefi için 1.3 savunulabilir bir taban
- GLES backend **yok** (karar kilitli — ve artık Google da aynı yönde: Vulkan resmi API oldu, GLES aktif geliştirme dışı, ANGLE üzerinden sunuluyor)
- Zorunlu feature'lar: `descriptorIndexing`, `subpass` (veya `framebuffer_fetch`), `timelineSemaphore`, `drawIndirect`, `bufferDeviceAddress`
- ⚠️ **Descriptor sistemini fazla soyutlama.** `VK_EXT_descriptor_heap` Vulkan'ın descriptor sistemini komple değiştiriyor (descriptor'lar buffer memory'de). Bugünün descriptor set modelinin üstüne kalın bir soyutlama yazmak, bir yıl içinde çöpe atacağın kod demek. İnce tut
- Command buffer'lar **paralel kaydedilir** (thread başına pool)
- Memory: kendi sub-allocator'ımız. `vkAllocateMemory` çağrı sayısı sabit ve az
- **Transient attachment** kullanımı zorunlu: G-buffer, depth tile'da kalır, `STORE_OP_DONT_CARE`
- ⚠️ **`VK_EXT_subpass_merge_feedback` ile doğrula.** Subpass yazmak, driver'ın onları gerçekten birleştirdiği anlamına gelmez — bazı OEM/driver kombinasyonlarında merge sessizce devre dışı kalır ve tüm tile stratejisi çöker. Merge olmadıysa build/boot uyarısı ver

### L3 — Renderer

**Derlenmiş Render Graph.** Graph runtime'da kurulup çözülmez. Build'de:
- Pass sırası sabitlenir
- Barrier'lar hesaplanır ve koda gömülür
- Transient memory aliasing planı çıkarılır
- Tüm PSO'lar derlenir

Runtime'da render graph "çözme" maliyeti **sıfır**dır. Bu, Frostbite frame graph'ının fikri + A1.

**Frame pipeline:**

```
0. Cluster cull (compute)      → frame N-1 kamerasıyla, frame N için [pipelined]
1. (cull sonucu hazır)          → indirect buffer, bekleme yok
2. Depth prepass (indirect)    → tile'da, sadece pozisyon stream
3. Visibility buffer (indirect)→ 8 byte/px, tile'da
4. Material resolve            → clustered forward+, permutation başına
   ├─ subpass içinde, G-buffer DRAM'e inmez
5. VSM page update             → sadece dirty page'ler
6. Particle (quarter-res RT)   → GPU sim + indirect draw
                                  + reactive mask'e yaz (bkz. EK H.2)
7. Composite + temporal upscale→ %55 → %100  [tile DIŞI — zorunlu]
   girdi: HUD'suz renk, motion vector, depth, jitter, reactive mask
8. Post + UI                   → UI **upscaler'dan SONRA**, tam çözünürlükte
```

⚠️ **Upscaler girdi sözleşmesi ihlal edilemez.** UI upscaler'dan önce çizilirse metin ve HUD titrer; reactive mask verilmezse particle'lar ghost bırakır; motion vector konvansiyonu yanlışsa "çalışıyor ama bulanık" olur. Bu kurallar ASR, FSR2, DLSS ve XeSS için aynıdır — sözleşme evrensel, uygulama değişir (EK H).

### L4 — Simulation

- **ECS**: archetype tabanlı, layout compiler tarafından üretilir. Sistemler job olarak çalışır, bağımlılık grafiği compile-time çıkarılır → otomatik paralelleştirme
- **Physics**: Jolt entegrasyonu, kendi job system'imize bağlı. Deterministik fixed-step
- **Animation**: ACL-benzeri sıkıştırma, GPU skinning, job'lı pose evaluation
- **Audio**: lock-free ring buffer, ayrı yüksek öncelikli thread, DSP graph. **3D spatialization**: mesafe attenuation + HRTF panning; oklüzyon için kaynak→dinleyici arası birkaç ışın, **fizik job'ında** (GPU'da değil — bkz. EK E.1)
- **UI**: ⚠️ Planda tamamen eksikti (bkz. EK D.4). SDF font atlası, tek batch'te çizim, retained layout ağacı — layout yalnızca dirty olduğunda yeniden hesaplanır. Mobilde UI overdraw'ı ciddi bir fillrate kalemi: opak UI önce, blend edilen UI sonra, tam ekran şeffaf katman yasak
- **Particles**: GPU sim, quarter-res render

### L5 — Gameplay (Tulpar)

- Gameplay Tulpar'da yazılır, **AOT derlenir**, engine ile aynı binary'ye linklenir
- Whole-program optimization: cross-module inlining, devirtualization açık
- Gameplay kodunda **allocation yasak** (compiler enforce eder) — sadece `FrameArena`
- Script VM yok, interpreter yok, bridge maliyeti yok

### L6 — Content Pipeline

Bkz. §6 — asıl moat burada.

### L7 — Tooling

- **The Truth**: tek merkezi veri modeli. Her değişiklik bir transaction → undo/redo, çakışma çözümü, collaboration bedavaya gelir
- Editor engine'i **kütüphane olarak** kullanır, tersi değil
- Profiler **Faz 0'da** yazılır: frame timeline, job graph, GPU counter, bellek arena görünümü

---

## 4. Frame Bütçesi (60 fps, sustained thermal)

Toplam **16.6 ms**. Sustained clock'ta (peak'in ~%60'ı) planlanır.

**CPU (big cluster, paralel):**

| İş | Bütçe |
|---|---|
| Gameplay + ECS sistemleri | 3.0 ms |
| Physics step | 2.0 ms |
| Animation + skinning setup | 1.5 ms |
| Culling + render prep | 1.5 ms |
| Command buffer kaydı (paralel) | 1.5 ms |
| **CPU toplam (kritik path)** | **~6 ms** |

**GPU:**

| Pass | Bütçe |
|---|---|
| Cluster cull (compute) | 0.5 ms |
| Depth prepass | 1.0 ms |
| Visibility buffer | 2.0 ms |
| Material resolve + lighting | 4.0 ms |
| VSM update | 1.5 ms |
| Particles (quarter-res) | 1.5 ms |
| Upscale + post + UI | 2.0 ms |
| **GPU toplam** | **~12.5 ms** |

**Kurallar:**
- Bütçeyi aşan pass, dynamic resolution'ı tetikler — fps **düşmez**
- 99th percentile frame time hedefi: **< 18 ms**. Ortalama değil, percentile ölçülür (A7)
- Bandwidth bütçesi: **< 8 GB/s** ortalama. Bunun üstü termal problem demektir

---

## 5. Bellek Bütçesi (mid-tier hedef, 512 MB app budget)

| Kalem | Bütçe |
|---|---|
| Engine + Tulpar runtime baseline | **12 MB** |
| RHI + driver overhead | 40 MB |
| Render target'lar (vis buffer, VSM atlas, history) | 90 MB |
| Texture (virtual texture cache) | 140 MB |
| Geometri (resident cluster set) | 100 MB |
| Animation + audio | 45 MB |
| ECS + gameplay state | 25 MB |
| FrameArena | 8 MB |
| Pay | 52 MB |

**12 MB baseline** en somut farkımız: Unity IL2CPP baseline'ı 60-120 MB, Godot 40-60 MB. İçerik koymadan önceki fark bu.

### Kurulum boyutu — RAM'den daha sert bir kısıt

| Sınır | Değer |
|---|---|
| Google Play base modül (sıkıştırılmış indirme) | **200 MB** |
| Asset pack toplamı (AAB içinde) | ~2 GB, en fazla 100 pack |
| App Store (sıkıştırılmamış) | **4 GB**, aşılamaz |
| 200 MB üstü | Mobil veride kullanıcıya uyarı diyaloğu çıkar |

Ve asıl rakam: **her 6 MB artış, kurulum dönüşüm oranını ~%1 düşürüyor** (Google ölçümü). Bu, binary boyutunu bir mühendislik tercihinden **ticari metriğe** çeviriyor — ayrıntı için EK G.3.

---

## 6. Compile-Time Pipeline — Asıl Moat

Unity ve Godot bunu **yapamaz**, çünkü hangi oyunu çalıştıracaklarını bilmiyorlar. Biz biliyoruz.

```
Kaynak varlıklar          Tulpar kaynak kodu
      │                          │
      ▼                          ▼
┌──────────────┐        ┌──────────────────┐
│  Importer    │        │  Tulpar Frontend │
│  (mesh, tex, │        │  (parse, type)   │
│   anim, aud) │        └────────┬─────────┘
└──────┬───────┘                 │
       │                         ▼
       ▼                ┌──────────────────┐
┌──────────────┐        │ CPU IR │ GPU IR  │  ← tek tip sistemi
│ Cluster      │        └────┬───────┬─────┘
│ Builder      │             │       │
│ (DAG + LOD)  │             │       ▼
└──────┬───────┘             │  ┌─────────────────┐
       │                     │  │ Precision Pass  │  fp16 otomatik
       ▼                     │  │ Permutation Gen │  tam gereken set
┌──────────────┐             │  │ SPIR-V / MSL    │
│ Scene         │            │  └────────┬────────┘
│ Compiler      │◄───────────┘           │
│               │                        │
│ · Resident set hesabı                  │
│ · LOD cut bake (volume başına)         │
│ · Render graph derleme                 │
│ · Barrier planı                        │
│ · Tüm PSO'lar                          │
│ · Peak memory hesabı                   │
│ · GI probe / VSM statik bake           │
│ · Navmesh bake                         │
└──────┬────────────────────────────────┬┘
       ▼                                ▼
   data.pack                     tek native binary
```

**Bu pipeline'ın runtime'dan sildiği şeyler:**

| Silinen sistem | Normalde ne yapar | Neden gerekmiyor |
|---|---|---|
| Shader compiler | Runtime'da variant derler | Tüm PSO'lar build'de hazır |
| PSO cache / precache | İlk-çizim hitch'ini azaltır | Hitch yok |
| Streaming manager | Page request, residency | Resident set bake edildi |
| LOD selector | Per-frame screen error | Volume başına cut tabloda |
| Render graph çözücü | Pass sırası, barrier, aliasing | Build'de derlendi |
| Reflection tablosu | Serialize, inspect | Kod üretildi |
| Scene loader/parser | Sahne dosyasını yorumlar | Sahne bir blob + kod |
| GC / genel allocator | Bellek yönetir | Arena, peak hesaplandı |

Bu tablo motorun **neden hem daha hızlı hem daha küçük** olduğunun tamamıdır. "Daha iyi kod yazdık" değil — **daha az kod çalıştırıyoruz**.

---

## 7. Faz Planı

Her fazın çıktısı **ölçülebilir** ve bir sonraki faz onun üstüne kuruluyor. Faz atlanmaz.

### Faz 0 — Ölçüm ve Temel
- Arena allocator ailesi + leak/overflow tespiti
- Fiber job system (ARM64 context switch dahil)
- **Profiler** (frame timeline, job graph, arena görünümü)
- **Crash reporter + symbol server** (Breakpad/Crashpad sınıfı). Native motorda stack trace olmadan saha hatası çözülmez ve sonradan eklemek acı
- **Performans CI'ı: gerçek cihazda, her build'de.** Faz kapılarını yazmak yeterli değil, sürekli ölçülmeli. Deterministik replay + cihaz farm'ı. Ölçüm penceresi **10-15 dakika** (termal throttle o aralıkta başlıyor), her ölçüm 3-5 koşu ortalaması (bkz. EK G.4)
- Math + container kütüphanesi
- **Timestamp'li callback input** (polling değil). Dokunma olayı `ALooper` callback'inde yakalanır, sistem timestamp'i saklanır; sim o timestamp'i kullanır, frame başlangıcını değil
- **Çıktı:** boş bir pencere, 0 allocation/frame, profiler çalışıyor
- **Kapı:** frame'de allocation sayısı = 0, doğrulanmış

### Faz 1 — RHI ve İlk Piksel
- Vulkan backend, bindless, transient attachment
- **PSO yükleme stratejisi**: `VkPipelineCache` blob'u pack ile birlikte gönderilir + `VK_EXT_graphics_pipeline_library`. PSO'ları build'de üretmek yeterli değil — yüklemesi de ucuz olmalı
- **`VK_EXT_host_image_copy`**: texture upload'da staging buffer yok (Vulkan Roadmap 2026 baseline). Virtual texture page load'larında doğrudan kazanç
- Elle yazılmış tek pass ile üçgen
- **Çıktı:** ekranda üçgen, GPU timing okunuyor
- **Kapı:** subpass ile G-buffer'ın DRAM'e inmediği GPU counter'la kanıtlanmış

### Faz 2 — Tulpar Shader Stage
- Tulpar'a GPU stage'i: tip sistemi ortak
- ⚠️ **Backend: Slang.** Tulpar → Slang → SPIR-V / MSL / WGSL. Kendi SPIR-V ve MSL emitter'ını yazma (bkz. EK B)
- **Otomatik precision analizi** (fp16 promotion) — bu bizde kalıyor, moat burada
- CPU-GPU struct layout doğrulaması compile-time
- **Wave/warp genişliği uyumu**: compute `local_size` sabit kodlanmaz. Mali ve Adreno'nun native wave genişlikleri farklı; yanlış boyut SIMD şeritlerini boşa harcar. **Specialization constant + `VK_EXT_subgroup_size_control`** ile tek binary'de çözülür, iki ayrı derleme gerekmez. Cihaz-sınıfı bake matrisine GPU satıcısı eklenir (EK A.2)
- **Mali Offline Compiler build-gate:** her shader build'de cycle count / bandwidth / register pressure için analiz edilir, eşiği aşan shader **build hatası** verir
- **Çıktı:** shader Tulpar'da yazılıyor, üç hedefe birden derleniyor
- **Kapı:** aynı ALU işi Unity URP shader'ına karşı ölçülüp fp16 kazancı gösterilmiş
- **Not:** bu faz erken çünkü moat burada ve sonraki her şey buna bağımlı

### Faz 3 — Renderer Çekirdeği
- Visibility buffer + clustered forward+
- **Bloom: mipmap zinciri** (progressive downsample/upsample, donanım filtrelemesi). Compute bloom **yasak** — §8/10. Mip zincirini tek bir atlas RT'ye yerleştir, render pass geçişi sayısını düşür
- **Selective bloom**: tam ekran değil, emissive/parlak kaynaklar ayrı düşük-res target'a. Particle target'ı ile paylaşılabilir
- **Adreno yolu (opsiyonel, ölçülecek):** `VK_QCOM_tile_memory_heap` ile vis buffer'ı render pass'ler arasında tile memory'de tut (bkz. EK F.1)
- Derlenmiş render graph (ilk sürüm)
- CSM gölge
- **Çıktı:** ışıklı, gölgeli sahne
- **Kapı:** bandwidth < 8 GB/s ölçülmüş

### Faz 4 — Temporal
- Motion vector altyapısı
- TAA → **Arm ASR entegrasyonu** (kendi upscaler'ını yazma)
- **ADPF** entegrasyonu: `getThermalHeadroom(forecastSeconds)` ile **ileriye dönük** okuma + histerezis (çözünürlük salınmasın). Reaktif eşik değil, tahmin penceresi
- **Reactive / transparency mask**: particle ve şeffaf yüzeyler temporal reprojection'ı kırar; maskesiz upscaler onları ghost'lar. Particle pass'i maskeye de yazar
- **HDR çıkış** (üst cihaz kademesi, opsiyonel): dahili render zaten lineer fp16; çıkış transfer fonksiyonu resolve'da seçilir. ⚠️ 10-bit/fp16 swapchain bandwidth maliyeti var, cihaz sınıfı kararı
- Dynamic resolution + frame pacing
- **Çıktı:** %55 render res, kilitli 60 fps
- **Kapı:** 10 dakika sustained çalışma, 99p frame time < 18 ms

### Faz 5 — Simülasyon
- ECS (compiler üretimli archetype)
- Jolt entegrasyonu, deterministik fixed-step (**strict FP**, sim kodunda fast-math kapalı)
- Animation + GPU skinning
- GPU particle + quarter-res composite
- **Çıktı:** oynanabilir test sahnesi
- **Kapı:** replay determinizmi doğrulanmış

### Faz 6 — Content Pipeline
- Importer + content hash cache
- **Delta content paketi**: patch'te tüm pack değil, sadece hash'i değişen bloklar gönderilir. Sonradan eklemek pack formatını değiştirmek demek — formatı baştan blok-adreslenebilir tasarla
- **Asset pack eşlemesi**: pack formatı Play Asset Delivery asset pack'lerine ve iOS on-demand resources'a doğrudan eşlenebilmeli. Base modül 200 MB sınırının altında kalmalı (bkz. EK G.3). Cihaz-sınıfı bake çıktıları zaten ayrı — bunlar doğal asset pack sınırları
- Scene compiler (resident set, peak memory, PSO üretimi)
- Virtual texture + feedback
- **IO stratejisi**: pack `mmap`'lenir, ASTC blokları GPU'ya doğrudan gider (**dekompresyon adımı yok** — ASTC zaten GPU'nun tükettiği format). Konteyner seviyesi supercompression yalnızca ihtiyaç anında ve job'da açılır. IO thread'inde de allocation yok (A2)
- GI probe / navmesh bake
- **Çıktı:** varlıktan binary'ye tam otomatik zincir
- **Kapı:** runtime'da shader derlemesi ve streaming kodu **yok** — kanıtlanmış

### Faz 7 — Cluster Geometry
- Cluster DAG builder (`meshoptimizer` tabanlı, **kenar kilitli** simplification)
- GPU cull + indirect draw
- Volume başına LOD cut bake
- **Çıktı:** Nanite-sınıfı geometri yoğunluğu
- **Kapı:** cut geçişlerinde crack ve popping yok

### Faz 8 — VSM ve Tooling
- Virtual Shadow Map (indirection + page cache)
- The Truth veri modeli + editor
- Hot reload: gameplay **ve shader**. Editörde Tulpar değişikliği algılar, yeni SPIR-V'yi arka planda derler, hazır olunca pipeline'ı frame sınırında değiştirir. **A1'in editör istisnası** — ship build'de bu yol hiç derlenmez

### Faz 9 — Metal Backend
- iOS. **Bilinçli olarak en sonda** — iki backend'i paralel taşımak Faz 1-8'i yavaşlatır

---

## 8. Kilitli Kararlar (tartışmaya açılmayacaklar)

Bunlar tekrar tekrar gündeme gelir. Cevap burada yazılı:

1. **GLES backend yok.** AVP 2025 baseline. Cihaz kapsamı feature değil, pazar kararıdır. (Google 2025'te Vulkan'ı resmi API ilan etti; GLES artık ANGLE üzerinden sunuluyor ve aktif geliştirme dışı.)
2. **Software rasterizer yok.** TBDR mimarisiyle uyumsuz.
3. **Realtime GI yok.** Watt bütçesi yok. Bake + dinamik katman.
4. **Script VM yok.** Tulpar AOT, tek binary.
5. **Runtime streaming yok.** Bake edilmiş resident set.
6. **Runtime shader compilation yok.** Tüm PSO'lar build'de.
7. **GC yok, genel amaçlı allocator yok.** Sadece 4 arena tipi.
8. **Mesh shader path yok.** Compute + indirect tek yol.
9. **Genel amaçlı motor değil.** "İleride başkası kullanır" gerekçesiyle soyutlama eklenmez.
10. **Tile residency'yi kıran optimizasyon kabul edilmez.** Bir pass'i compute'a taşıma önerisi geldiğinde sorulacak tek soru: render pass zincirini bölüyor mu? Böyyorsa hayır. Material resolve, bloom ve ses raycast'i bu kuralla reddedildi.
    - ⚠️ **Tek istisna, Adreno 840+:** `VK_QCOM_tile_shading` (`vkCmdDispatchTileQCOM`) compute'un render pass *içinde*, tile memory'ye erişerek çalışmasına izin veriyor. Kural Mali'de ve varsayılan olarak geçerli; Adreno'da ölçülmüş bir kaçış yolu var (bkz. EK F.1). Bu istisnayı bilmeden kuralı uygulama, bilmeden de kuralı bozma.

---

## 9. Risk Kaydı

| Risk | Etki | Azaltma |
|---|---|---|
| **Cluster DAG builder'da crack/popping** | Faz 7 tıkanır | Kenar kilitli simplification, deterministik. Prototipi Faz 7 başında ayrı doğrula |
| **Tulpar GPU stage'i beklenenden büyük iş** | Faz 2 kayar, her şey kayar | ⚠️ **Azaltma değişti:** SPIRV-Cross yerine **Slang'i backend olarak kullan**. Tulpar → Slang IR → SPIR-V/MSL/WGSL. Kendi backend'ini yazma (bkz. EK B) |
| **Android driver farklılıkları** | Sahada bug | Cihaz matrisi Faz 1'de belirlenir, CI'da gerçek cihazda koşulur |
| **Peak memory build hesabı yanlış** | Runtime OOM | Arena overflow build hatası + runtime assert. Content CI'da her sahne doğrulanır |
| **Motor ve oyun paralel değişiyor** | İkisi birden çöker | **Oyun tasarımı Faz 5'te dondurulur.** Motor bitmeden oyun scope'u değişmez |
| **Tooling ihmali** | Motor çalışır ama içerik üretilemez | Profiler Faz 0'da, editor Faz 8'de — ikisi de kapı şartı |
| **Soyutlama sızması (A5 ihlali)** | Yavaş yavaş Unity'ye dönüşür | Katman bağımlılık kuralları CI'da kontrol edilir, ihlal = build hatası |

---

## 10. İlk Üç Karar

Yarın başlanacaksa sırayla bunlar:

1. **Hedef cihaz matrisini yaz.** Baseline SoC, GPU, RAM. Her bütçe rakamı buna göre yeniden kalibre edilir.
2. **Arena allocator + profiler'ı yaz.** Başka hiçbir şeye dokunmadan. Ölçemediğin şeyi optimize edemezsin ve bellek modeli sonradan değiştirilemez.
3. **Tulpar'ın GPU stage'i için tip sistemi tasarımını çıkar.** Faz 2'de yazılacak ama tasarımı şimdi netleşmeli, çünkü CPU tarafındaki tip sistemi kararları buna bağımlı.

---

# EK A — Araştırma Turu (2023-2026)

İlk sürümdeki envanteri kendi bilgimden yazmıştım. Bu ek, SIGGRAPH Advances 2025/2026, REAC, ARM Moving Mobile Graphics ve Android geliştirici kaynaklarının taranmasından çıktı. **Bazı maddeler ana dokümandaki kararları değiştirdi** — o satırlar yukarıda güncellendi ve ⚠️ ile işaretlendi.

## A.1 En kritik bulgu: HypeHype

Bizim yapmak istediğimiz şeyin **var olan en yakın örneği**. Sebastian Aaltonen (eski Ubisoft/Unity, GPU-driven rendering pipeline'ların mucitlerinden) HypeHype'ın mobil renderer'ını sıfırdan yeniden yazdı. Vulkan + Metal + WebGPU, TBDR için tasarlanmış, bindless, mobil-öncelikli.

Bizimle örtüşen tarafları rastlantı değil — aynı fiziksel kısıtlardan aynı sonuçlara varmışlar:

- **"Bandwidth ana limit"** — bizim A3 aksiyomumuz, bağımsız olarak doğrulandı
- **"Draw call frekansında tekrarlanan işlerin çoğu kaldırıldı"** — pipeline ve bind group'lar önceden oluşturuluyor. Bizim A1'in renderer karşılığı
- **Yazılım command buffer** — native command buffer'dan bir kat daha hızlı
- Kitbash edilmiş 100.000+ objelik sahneler, 10 MB depolama limiti içinde

### RHI tasarım kuralı (Aaltonen) — L2'ye ekleniyor

> Platform soyutlamalarının en yaygın hatası: **userland kavramlarının donanım API'sine sızması.** Platform kodunda `Mesh` ve `Material` bulunması en sık görülen problem — ikisi de sürekli değişim baskısı altında.

Bunu §2'nin bağımlılık kurallarına 6. madde olarak ekle:

> **6.** L2 (RHI) `Mesh`, `Material`, `Light`, `Camera` gibi hiçbir userland tipini bilmez. Mesh = index buffer binding + N vertex buffer binding. Material = N texture descriptor içeren bir bind group. Meshlet ve bindless geleceği belirsizken bir temsile bağlanmayız.

İkinci pillar: **sıfır ekstra API overhead.** Soyutlama katmanı ölçülebilir maliyet eklemeyecek. DX11 kadar kolay kullanılacak ama maliyeti olmayacak.

### Stochastic Tile-Based Lighting (SIGGRAPH 2025)

Düşük-uçlu mobil GPU'da **sabit maliyetli**, tam dinamik, gölgeli lokal aydınlatma. Işık sayısı arttıkça maliyet artmıyor.

1. **Big-tile sampling** — ekran büyük tile'lara bölünür, kaba bir PDF'e göre Stratified Reservoir Sampling ile ışık alt kümesi seçilir
2. **Small-tile resampling** — küçük tile'lar big-tile reservoir'larından daha ince PDF ile yeniden örneklenir

Işık örneklerinin küçük tile pikselleri arasında paylaşılması resampling maliyetini amorti ediyor ve GPU wave coherence'ı artırıyor.

Bu bizim §1.2'deki clustered forward+ kararımızın **düşük-uçlu cihaz karşılığı**. İkisi birlikte kullanılır: yüksek uçta clustered, düşük uçta stochastic tile-based.

## A.2 Roblox SLIM — A1'in cloud'a taşınmış hali

Roblox, milyonlarca kullanıcının kitbash ettiği, LOD'u elle yazılmamış, içerik bütçesi olmayan dünyaları milyonlarca farklı cihazda çalıştırmak zorunda. Çözümleri **SLIM**: bulut tabanlı bir sistem, dünyaların **cihaza uyarlanmış hafif runtime temsillerini otomatik üretiyor** — yazarın görünümünü, davranışını ve semantiğini koruyarak.

Bizim için önemi: bu tam olarak A1'in ("runtime'da yapılabilecek her şey build'de yapılır") ölçeklenmiş kanıtı. Ve bize somut bir yol açıyor:

> **Cihaz sınıfı başına ayrı bake.** Scene compiler tek bir çıktı üretmesin. Düşük/orta/yüksek cihaz sınıfı için ayrı resident set, ayrı LOD cut tablosu, ayrı material permutation seti üretsin. Cihaz sınıfı boot'ta tespit edilir, ilgili pack yüklenir. Runtime'da hiçbir scalability dalı çalışmaz.

Bu, "quality settings" kavramını runtime'dan komple siliyor. §6'daki "silinen sistemler" tablosuna eklenecek bir kalem daha.

## A.3 Yeni teknolojiler — karar tablosu

| Teknoloji | Kaynak | Karar | Gerekçe |
|---|---|---|---|
| **Arm ASR** | Arm, MIT lisans | **AL — entegre et** | FSR2'nin mobil için optimize edilmiş türevi. Custom engine'ler için generic library var. Immortalis-G720'de %53'e varan fps artışı, %20 güç tasarrufu. Kendi upscaler'ımızı yazmak saf risk |
| **ADPF Thermal + Performance Hint** | Google Android | **AL — zorunlu** | A4'ün somut uygulaması. Custom engine için native C++ örneği mevcut. Elle core pinning'i geçersiz kılıyor |
| **Stochastic tile-based lighting** | HypeHype | **AL** | Bkz. A.1 |
| **Adaptive tessellation (compute)** | Meta / John Hable, SIGGRAPH 2026 | **ERTELE — Faz 7 alternatifi** | Compute tabanlı, seam welding'li, ekran-adaptif tessellation. Cluster DAG'a **rakip veya tamamlayıcı**. Faz 7'ye girmeden ikisini karşılaştır — tessellation yolu çok daha ucuz olabilir |
| **AVBOIT** | Call of Duty | **ERTELE** | Şeffaflık mobilde ana dert, incelenmeye değer |
| **SLIM tarzı cihaz-sınıfı bake** | Roblox | **AL** | Bkz. A.2 |
| **id Tech 8 realtime GI** | id Software | **ATLA — kararı doğruladı** | DOOM The Dark Ages bake'den realtime GI'ya geçti ve tüm platformlarda 60Hz tutturdu. Ama konsol/PC hedefli. Bizim watt bütçemizde yok. §8/3 kararı değişmiyor |
| **MegaLights** | UE5 | **ATLA** | Stochastic direct lighting, ama ray tracing'e dayanıyor. Mobil karşılığı zaten HypeHype yaklaşımı |
| **ORCA radiance cache** | EA SEED, 2026 | **ATLA** | Path tracing hızlandırma. Bizim ligimiz değil. (Not: temporal history'ye bağımlı değil, veri yapıları frame'i aşmıyor — mimari olarak ilginç) |
| **ReSTIR / reservoir resampling** | NVIDIA, akademi | **ADAPTE — dolaylı** | Doğrudan almıyoruz ama HypeHype'ın stochastic tile lighting'i bu ailenin mobil türevi. Fikri oradan alıyoruz |
| **Variable Rate Ray Tracing** | CoD MW4, 2026 | **ATLA** | ⚠️ Ama bir fikri çal: **GPU-driven frame-level scheduler ile toplam bütçeyi sabit tutmak.** Kamera hareket etse de toplam ray sayısı sabit → performans spike'ı yok. Aynı prensip bizim particle ve ışık bütçemize uygulanabilir |
| **Strand-based hair** | MachineGames | **ATLA** | Oyun türü gerektirmiyor |
| **Volumetric VFX framework (Smolder)** | IO Interactive | **ATLA** | Watt bütçesi |
| **Neural upscaling (PSSR)** | Sony, 2026 | **ATLA** | ⚠️ Ama dersini al: PSSR'ın yükseltmesi modele **daha az iş vererek** kaliteyi artırdı ve maliyeti düşürdü — kapalı-form çözümü olan problemleri modele bırakmayı bıraktılar. Bu bizim A5'imizin ML versiyonu |

## A.4 Kararı Değiştiren Üç Bulgu

Ana dokümanda güncellenen yerler:

**1. Upscaler'ı yazma, entegre et.**
Faz 4'ün en büyük kalemi kendi temporal upscaler'ımızı yazmaktı. Arm ASR MIT lisanslı ve custom engine entegrasyonu için tasarlanmış. Bu Faz 4'ün maliyetini büyük ölçüde düşürüyor. Şart: motion vector ve jitter altyapısı yine bizde, onu baştan doğru kur.

**2. Core pinning yanlıştı.**
"Kritik path'i big cluster'a pinle" tavsiyesi eski. Doğrusu ADPF Performance Hint session: OS'a hedef frame süresini ve gerçekleşen süreyi bildirirsin, scheduler kararı verir. MediaTek ölçümünde yalnızca hint session açmak render thread süresini ~%10 düşürmüş, FPS standart sapmasını %25 azaltmış. Elle pinleme scheduler ile çatışır.

**3. Düşük-uçlu ışıklandırma için hazır bir cevap var.**
Clustered forward+ orta/yüksek uç için doğru. Düşük uç için HypeHype'ın stochastic tile-based yaklaşımı bizim tam senaryomuz için tasarlanmış ve slaytları yayında.

## A.5 Takip Edilecek Kaynaklar

Bu alan hızlı hareket ediyor, envanteri yıllık tazelemek gerekiyor:

| Kaynak | Neden |
|---|---|
| `advances.realtimerendering.com` | SIGGRAPH Advances — 20 yıllık arşiv, tüm slaytlar açık |
| `enginearchitecture.org` (REAC) | **Bizim için en değerlisi.** Rendering tekniği değil, *mimari kararlar ve gerekçeleri* anlatılıyor. Ücretsiz, online, sponsorsuz |
| ARM Moving Mobile Graphics (SIGGRAPH track) | Mobil-spesifik tek büyük forum. HypeHype slaytları burada |
| `developer.arm.com` mobile graphics blog | Arm ASR, ADPF, Mali/Immortalis best practice |
| `developer.android.com/games/optimize` | ADPF, frame pacing, Vulkan rehberleri |

**Not:** REAC'i özellikle takip et. "Nasıl doğru yapılır" değil, "biz nasıl yaptık ve ne öğrendik" anlatan tek yer — streaming sistemi, shader yönetimi, tooling'in motor tasarımına etkisi gibi bizim asıl dertlerimiz orada konuşuluyor.

---

# EK B — 2026 Durumu

Eylül 2026 taraması. **Sadece kararı değiştiren şeyler var.** Yeni ve parlak ama bizim ligimizde olmayan teknolojiler için B.4'e bak — orada neden almadığımız tek satırla yazılı, tekrar gündeme gelmesin diye.

## B.1 Slang — Faz 2'yi yeniden yazıyor

En önemli bulgu. Slang, NVIDIA'dan Khronos'a devredildi ve artık çok-şirketli açık yönetimde. 2026 Khronos anketinde %34 kullanım oranıyla HLSL'i (%41) yakalamak üzere.

**Neden bizi doğrudan ilgilendiriyor:** planımızda Faz 2'nin en büyük riski "kendi shader dilimizin SPIR-V ve MSL backend'lerini yazmak"tı. Slang bu işi zaten yapıyor:

- Tek kaynaktan **SPIR-V, MSL, WGSL, HLSL, CUDA, CPU** çıkışı
- **Capability system**: hedef platformlar arası feature farklarını *type-checking aşamasında* yönetiyor — kod, o platformda olmayan bir feature'ı kullanamıyor. Bizim cihaz-sınıfı bake planımızla birebir örtüşüyor
- **Modüler derleme** ve dinamik shader linking → derleme süreleri düşük
- Generics, interfaces, BufferPointer

**Kanıt:** Valve, Source 2'nin tüm production HLSL kod tabanını Slang ile derledi — 10 satır değişiklikle. Üretilen SPIR-V'yi CS2 ve Dota 2'de shipledi.

### Revize edilen Faz 2 mimarisi

```
Tulpar kaynak
     │
     ▼
Tulpar Frontend (parse, tek tip sistemi)
     │
     ├─── CPU IR ──────► LLVM ──► ARM64
     │
     └─── GPU IR ──────► Slang ──┬──► SPIR-V  (Vulkan)
              ▲                   ├──► MSL     (Metal)
              │                   └──► WGSL    (Web)
     ┌────────┴─────────┐
     │ BİZDE KALAN İŞ:  │
     │ · precision analizi (fp16 promotion)
     │ · permutation üretimi (sahne bilgisiyle)
     │ · CPU-GPU layout doğrulaması
     │ · uniform packing
     └──────────────────┘
```

**Moat kaybolmuyor.** Bizim farkımız hiçbir zaman "SPIR-V üretebiliyoruz" değildi — o zaten çözülmüş bir problem. Farkımız *CPU ve GPU tarafını aynı tip sisteminde tutmak ve sahne bilgisiyle compile-time optimizasyon yapmak*. Slang bu katmanın altında duruyor, yerine geçmiyor. Backend külfetini alıyor, moat'ı bırakıyor.

**Bonus:** WebGPU hedefi neredeyse bedavaya geliyor. HypeHype de tam olarak bu üçlüyü (Vulkan/Metal/WebGPU) hedefliyor — tesadüf değil.

Aynı ankette ilginç bir sinyal daha: geliştiriciler shading dillerinde **pointer, fonksiyon çağrısı ve gerçek adreslenebilirlik** istiyor, "C/C++ → SPIR-V" doğru soyutlama olarak görülmeye başlanmış. Tulpar'ın bir GPU stage'i olması fikri akıntıya karşı değil, akıntıyla aynı yönde.

## B.2 Vulkan tarafı — üç değişiklik

**1. Baseline'ı elle seçme, AVP 2025'i kullan.**
Google "Android Baseline Profile"ı **Android Vulkan Profile** olarak yeniledi; 2025 sürümü çıktı. Üç kademe var ve her birinin gerçek cihaz kapsama yüzdesi Distribution Dashboard'da yayınlanıyor. Bizim "Vulkan 1.2 baseline" tercihimiz tahmine dayanıyordu; AVP veriye dayanıyor. Zorunluluklar: Android 13+ cihazlar Vulkan 1.3, Android 16+ cihazlar Vulkan 1.4 desteklemek zorunda.

**2. GLES kararı Google tarafından onaylandı.**
Vulkan artık Android'in resmi grafik API'si. GLES hâlâ destekleniyor ama **aktif feature geliştirmesi durduruldu** ve giderek ANGLE üzerinden (yani Vulkan üstünde emülasyonla) sunuluyor. Zorunluluk Android 17 ile tam yürürlüğe girecek. §8'deki 1. kilitli kararımız artık tartışmaya bile açılamaz.

**3. Descriptor sistemini soyutlama — değişiyor.**
`VK_EXT_descriptor_heap` Vulkan'ın descriptor sistemini komple elden geçiriyor: descriptor'lar buffer memory'de tutulacak, diğer buffer objeleri gibi yönetilecek. Bugünün `VkDescriptorSet` modelinin üstüne kalın bir RHI soyutlaması yazmak, bir yıl içinde atacağın kod demek. L2'yi ince tut.

**Vulkan Roadmap 2026** ayrıca şunları baseline'a çekti: Variable Rate Shading (bizde ERTELE'den AL'a geçti), compute shader derivatives, host image copies, shader clock queries, daha yüksek descriptor limitleri, swapchain garantileri.

## B.3 Faz planına etkisi

| Faz | Değişiklik |
|---|---|
| **Faz 1** | Baseline AVP 2025'e göre belirlenecek. Descriptor soyutlaması ince tutulacak |
| **Faz 2** | **Küçüldü.** Backend Slang'e devredildi. Bizde kalan: precision analizi, permutation üretimi, layout doğrulaması. Ana risk büyük ölçüde kalktı |
| **Faz 3** | VRS artık baseline — renderer'a baştan dahil et, sonradan ekleme |
| **Faz 4** | Zaten Arm ASR'a devredilmişti (EK A). Değişiklik yok |
| **Faz 9** | Metal backend'i Slang üretiyor. Bu faz artık "backend yazmak" değil "Metal RHI'ı yazmak" — ciddi ölçüde küçüldü |

## B.4 Almadıklarımız ve nedeni

2026'nın parlak konuları. Hiçbiri bizim ligimizde değil, gündeme gelmesin diye gerekçesiyle yazıyorum:

| Teknoloji | Neden hayır |
|---|---|
| **Cooperative vectors / neural shaders** (SM 6.9, Vulkan) | Tensor/matris donanımına dayanıyor. Mobil GPU'da o donanım grafik pipeline'ına bu şekilde açılmıyor. Masaüstü konusu |
| **Shader Execution Reordering** | Ray tracing pipeline'ı için. Bizde ray tracing yok |
| **Mobil ray tracing** | Sadece flagship. Watt bütçesi yok. §8/3 ile aynı gerekçe |
| **Work graphs / GPU work creation** | Mobil desteği yok |
| **Opacity Micromaps** | Ray tracing eki |
| **Neural texture compression** | Mobil entegrasyonu hâlâ olgun değil (EK A'daki karar geçerli) |
| **Gaussian splatting** | İçerik pipeline'ımıza uymuyor. Capture tabanlı iş yapmıyoruz |
| **Mesh shaders** | §8/8 kararı geçerli. Compute+indirect tek yol |

Ortak payda: bunların hepsi **ya masaüstü donanımı ya da ray tracing** varsayıyor. Bizim kısıtımız watt ve bandwidth, bu listede ikisini de iyileştiren tek bir madde yok.

## B.5 Takip

EK A.5'teki listeye ekle:

| Kaynak | Neden |
|---|---|
| `shader-slang.org` | Faz 2 artık buna bağımlı |
| **Vulkanised** (yıllık, Şubat) | Vulkan'ın tek adanmış konferansı. Kayıtlar YouTube'da ücretsiz. Android on Vulkan oturumları AVP güncellemelerini veriyor |
| Khronos Shading Languages Symposium | Vulkanised'a bağlı, 2026'da ilk kez yapıldı. Bizim Faz 2 tasarımımızın tam konusu |
| `developer.android.com/ndk/guides/graphics/android-vulkan-profile` | AVP kademeleri ve kapsama yüzdeleri |

---

# EK C — Dış Öneri Listesi Değerlendirmesi

10 maddelik bir öneri listesi geldi. **Beşi alındı, biri koşullu, üçü zaten dokümanda vardı, biri reddedildi.** Reddedilen ve düzeltilenlerin gerekçesi aşağıda — tekrar gündeme gelmesin.

## C.1 Alınanlar

### 1. Mali Offline Compiler — build-gate olarak (Faz 2)

Listenin **en değerli maddesi**. Arm Performance Studio içindeki offline compiler, shader'ı çalıştırmadan cycle count, bandwidth tüketimi ve register pressure veriyor.

Bunu bir "profil aracı" olarak değil, **build kapısı** olarak kullan:

```
Tulpar shader → Slang → SPIR-V → Mali Offline Compiler
                                        │
                              cycle / bandwidth / register
                                        │
                            eşiği aşarsa → BUILD HATASI
```

Bu, A1'in ("runtime'da yapılabilecek her şey build'de yapılır") shader boyutundaki karşılığı ve şu ana kadar planda eksikti. Performansı ölçmek yerine **regresyonu imkânsız kılmak**. Faz 2'ye eklendi.

### 2. `VK_EXT_subpass_merge_feedback` (Faz 1)

Ciddi bir kör nokta yakaladı. Tüm mobil stratejimiz subpass merging'in gerçekleşmesine dayanıyor — ama **subpass yazmak, driver'ın onları birleştirdiği anlamına gelmiyor.** Bazı OEM/driver kombinasyonlarında merge sessizce devre dışı kalıyor ve G-buffer DRAM'e iniyor. Bandwidth bütçesi çöker ve nedenini bulamazsın.

Bu extension merge'in gerçekleşip gerçekleşmediğini sorguluyor. Faz 1'in kapı şartına eklendi.

### 3. Packed format disiplini (Faz 3)

Visibility buffer zaten 8 byte/px, ama render target'ların geri kalanı için aynı disiplin geçerli:
- Normal: 10:10:10:2 veya oktahedral 16:16
- Roughness + metallic: tek 8-bit kanalda paketle
- Motion vector: fp16 (fp32 israf)

A3'ün doğal uzantısı. Faz 3'te format seçerken bit bütçesi çıkar.

### 4. Crash reporter + symbol server (Faz 0)

Planda **yoktu ve olmalıydı**. Native motorda sahadan gelen bir çökmeyi stack trace olmadan çözemezsin, sembol altyapısını sonradan kurmak acı verici. Faz 0'a eklendi.

### 5. Strict FP ↔ determinizm çakışması (Faz 5)

Planda `-ffast-math` "fonksiyon başına opt-in" yazıyordu ve Faz 5'te deterministik fixed-step sim istiyorduk. **Bu ikisi çakışıyor:** fast-math reassociation ve FTZ yapar, sonuç platform ve derleme bayrağına göre değişir, replay ve lockstep netcode kırılır.

Kural netleştirildi: **sim kodunda strict FP zorunlu**, fast-math yalnızca render ve VFX'te.

## C.2 Koşullu: Arm Neural Technology (NSS / NFRU / NSSD)

Öneri "NPU entegrasyonu artık olgunlaştı" diyordu. **Doğrulanmadı — henüz olgunlaşmadı.** Gerçek durum:

| İddia | Gerçek |
|---|---|
| Donanım hazır | ⚠️ İlk silikon **2026 sonu**. Bugün sadece emülasyon ve SDK var |
| 540p→1080p, 4ms, %50 GPU tasarrufu | ⚠️ **Simülasyon verisi**, shipping silikon ölçümü değil |
| NFRU ve NSSD kullanılabilir | ⚠️ 2026 yol haritası, henüz yok |
| Mobilde MegaLights + ray tracing çalışıyor | ⚠️ Bu iddiaya kaynak bulamadım. Doğrulanmamış sayıyorum |

Ve **kritik olan:** NSS yalnızca **Mali/Immortalis**. Adreno telefonlarda yok. Pazarın büyük bir kısmı dışarıda kalıyor. Bir baseline olamaz.

**Karar:** bağımlılık değil, **soyutlama dikişi**.

> Upscaler'ı arayüz arkasına al. Arm ASR varsayılan yol olarak kalsın (bugün çalışıyor, MIT, her cihazda). NSS, cihaz sınıfı tespitiyle en üst kademede devreye giren *ikinci bir uygulama* olsun. NSS gelmese de motor eksiksiz çalışmalı.

Bunun ucuz olmasının sebebi: her ikisi de aynı girdileri istiyor — motion vector, jitter, depth, history. O altyapı zaten Faz 4'te kuruluyor. Faz 4'te yapılacak tek ek iş: upscaler'ı sabit bir çağrı değil, arayüz haline getirmek.

Arm'ın SDK'sı bugün emülasyonla erişilebilir. Faz 4 geldiğinde silikon çıkmış olacak, **o zaman ölç, şimdi taahhüt etme.**

## C.3 Zaten dokümanda olanlar

- **Arm ASR** — EK A.3'te "AL, entegre et", Faz 4'te
- **Swappy frame pacing** — §1.3'te zorunlu. (Ek fayda: auto-mode ile başla, gerekirse pipeline mode'a geç)
- **Render graph compilation caching** — §L3'te zaten bir adım ötesindeyiz. Unity graph'i runtime'da kurup cache'liyor; biz build'de koda çeviriyoruz, runtime'da "graph" kavramı yok

## C.4 Reddedilenler

### Fragment Density Map'i foveation olarak kullanmak — **hayır**

Öneri, düz telefon ekranında merkezi yüksek, kenarları düşük çözünürlükte render etmeyi söylüyordu. **Bu yanlış.** Foveated rendering VR'da çalışır çünkü göz konumu bilinir (headset optiği + eye tracking). Düz bir telefon ekranında oyuncu **her yere bakar** — kenardaki düşman, köşedeki HUD, alt taraftaki kontroller. Kenarları bulanıklaştırmak görsel hata olarak algılanır, hem de tam oyuncunun baktığı yerde.

**Ama FDM'nin gerçek bir kullanımı var:** Mali'de `VK_KHR_fragment_shading_rate` desteği tarihsel olarak `VK_EXT_fragment_density_map`ten daha zayıf. Yani FDM'yi *foveation olarak değil*, **VRS'in Mali üzerindeki uygulama yolu olarak** değerlendir. Oranı göz konumuna göre değil, **içerik karmaşıklığına göre** sür (düz duvar → düşük oran, detaylı yüzey → tam oran).

### Nanite için `VK_KHR_shader_image_atomic_int64` zorunlu tutmak — **hayır**

Öneri Faz 7'de bu extension'ı şart koşmayı söylüyordu. Ama 64-bit image atomic, Nanite'in **software rasterizer'ının** (visibility buffer'a atomic min yazımı) gereksinimi — yani §8/2'de bilinçli olarak attığımız parça. Cluster DAG + continuous LOD (aldığımız "A yarısı") bunu gerektirmiyor; compute cull + indirect draw yeterli.

Bu şartı koymak, attığımız kararı arka kapıdan geri sokmak olur. `compute_shader_derivatives` ise zaten Vulkan Roadmap 2026'da baseline (EK B.2).

### Unity Deferred+ — **marjinal**

Bizim visibility buffer + clustered forward+ planımız zaten daha agresif. Alınacak tek ders "material resolve'da compute shader düşün" — bu da zaten Faz 3'ün doğal tasarım sorusu, ayrı bir madde değil.

## C.5 Güncellenen faz özeti

| Faz | Bu ekten gelen |
|---|---|
| **Faz 0** | Crash reporter + symbol server |
| **Faz 1** | `VK_EXT_subpass_merge_feedback` doğrulaması → kapı şartı |
| **Faz 2** | Mali Offline Compiler build-gate |
| **Faz 3** | Packed format bit bütçesi. VRS'i Mali'de FDM ile uygulamayı değerlendir |
| **Faz 4** | Upscaler'ı **arayüz** yap (ASR varsayılan, NSS opsiyonel üst kademe) |
| **Faz 5** | Sim kodunda strict FP zorunlu |
| **Faz 7** | int64 atomic **şart değil** — o software rasterizer'ın gereksinimi, biz almadık |

---

# EK D — İkinci Öneri Listesi Değerlendirmesi

10 madde daha geldi. **Dördü alındı, biri doğrulandı ama kararı değiştirmedi, beşi reddedildi.** Reddedilenlerden biri kritik: uygulansaydı mobil stratejimizin tamamını çökertirdi.

## D.1 ⛔ En önemli ret: "Material resolve'u compute shader'a al"

Öneri, idTech 8'den yola çıkarak material resolve pass'ini fragment yerine compute shader'da yapmayı söylüyordu. **Masaüstünde doğru, TBDR'da tam tersi.**

Neden: bizim tüm mobil kazancımız visibility buffer'ın **tile memory'de kalmasına** dayanıyor — subpass zinciri sayesinde vis buffer DRAM'e hiç inmiyor (§1.2, "zorunlu").

Compute shader **render pass'in dışındadır.** Tile memory'ye erişemez. Material resolve'u compute'a taşımak şu zinciri zorunlu kılar:

```
vis buffer → DRAM'e YAZ → compute → DRAM'den OKU → sonuç DRAM'e YAZ
```

Yani `STORE_OP_DONT_CARE` gider, tam çözünürlükte bir round-trip gelir. 1080p'de 8 byte/px vis buffer için ~17 MB yaz + 17 MB oku, **frame başına ~34 MB ek trafik**. 60 fps'de **~2 GB/s**, toplam bütçemizin dörtte biri. Tasarrufu sıfır, maliyeti bu.

id Tech 8 bunu konsol ve PC için yapıyor — orada tile memory diye bir şey yok, kaybedecek bir şey de yok. Bizde var.

**Karar: material resolve fragment shader'da, subpass zinciri içinde kalır.** Bu §8'e 10. kilitli karar olarak eklenmeli.

> **10. Material resolve compute'a taşınmaz.** Tile residency'yi kıran hiçbir optimizasyon, ne kadar mantıklı görünürse görünsün, kabul edilmez. Bir pass'i compute'a taşıma önerisi geldiğinde sorulacak tek soru: bu, render pass zincirini bölüyor mu?

Aynı test post-processing için de geçerli — bkz. D.2.3.

## D.2 Alınanlar

### 1. Slotmap / generational handle (L1)

Gerçek bir boşluk. `PoolAllocator` planda ham pointer dağıtıyordu. Slot yeniden kullanıldığında eski pointer **sessizce yanlış objeyi** gösterir — mobilde reprodüksiyonu neredeyse imkânsız bir hata sınıfı.

Çözüm: index + generation counter. Slot geri kullanılınca generation artar, eski handle geçersizleşir ve **tespit edilebilir** olur. Maliyeti bir 32-bit alan ve bir karşılaştırma.

### 2. Exception ve RTTI yasağı — compiler zorlamalı (L1)

Planda hiç yazmamıştım, oysa örtük varsayımdı. Örtük varsayımlar ihlal edilir. Kural netleşti: exception atılamaz, runtime tip sorgusu yapılamaz, hata `Result<T>` ile taşınır, Tulpar bunu statik analizle doğrular.

(Öneri bunu Arknights: Endfield'e dayandırıyordu; oradaki ifade bulanıktı ama kuralın kendisi bağımsız olarak doğru.)

### 3. Post-processing'in tile-residency disiplini (Faz 3)

Öneri "post-processing'i subpass zincirine entegre et" diyordu. Doğru ama **eksik** — hepsi entegre edilemez. Faz 3'te her post adımı için kararı önceden ver:

| Adım | Tile'da kalabilir mi | Neden |
|---|---|---|
| Tone mapping | ✅ Evet | Per-piksel, komşuya bakmıyor. Resolve'a merge et |
| Color grading / LUT | ✅ Evet | Aynı |
| Vignette, film grain | ✅ Evet | Aynı |
| Bloom | ❌ Hayır | Çok geçişli downsample/blur, geniş yarıçap |
| Temporal upscale (ASR) | ❌ Hayır | History buffer + komşuluk + tam ekran |
| Motion blur, DOF | ❌ Hayır | Komşuluk örneklemesi |

Kural: **per-piksel olan her şey resolve'a merge edilir, komşuluk isteyen her şey ayrı pass olur.** Ayrı pass sayısı frame bütçesinde kalem olarak takip edilir.

### 4. Delta content paketi (Faz 6)

Planda patch/güncelleme hikâyesi yoktu. Öneri bunu "shader delivery" üzerinden getirdi; bizde PSO'lar zaten build'de üretildiği için asıl mesele **pack formatı**: patch'te tüm paket değil, hash'i değişen bloklar gönderilmeli.

Bunu sonradan eklemek pack formatını değiştirmek demek. Formatı Faz 6'da **blok-adreslenebilir** tasarla.

## D.3 Doğrulandı ama kararı değiştirmedi: Arm Neural Technology

Önceki turda doğrulayamadığım iddia **doğrulandı.** Neural Dawn gerçek: Arm + Sumo Digital, Unreal Engine 5.6.1, 17 kişilik ekip, 18 ay, dört-altı bölüm, 90-120 dakika oynanış. Mobilde ilk MegaLights kullanımı. NSSD ve NFRU birlikte çalışıyor.

**Ama EK C.2'deki kararımız aynen geçerli, hatta gerekçesi güçlendi:**

- Donanım hâlâ **çıkmadı** — "yıl içinde gelecek yeni nesil Mali GPU'lar" deniyor. Tüketicinin elinde cihaz yok
- **Sadece Mali.** Adreno telefonlarda yok. Baseline olamaz
- Bir **teknoloji demosu**, ticari ürün değil. Ve standart UE iş akışlarıyla yapılmış — yani bize mimari ders vermiyor, donanım + eklenti hikâyesi anlatıyor

Bizim için değişen tek şey: **soyutlama dikişini yapmaya değdiği kesinleşti.** Epic ve Arm bu yöne yatırım yapıyor, ekosistem oraya gidiyor. Ama Faz 4'te ASR varsayılan yol olarak kalır, NSSD/NFRU üst cihaz kademesinde devreye giren ikinci uygulama olur. Silikon çıktığında ölçülür.

**MegaLights'ı almıyoruz.** Neural Dawn'ın MegaLights'ı ray tracing + neural accelerator ikilisine dayanıyor; ikisi de bizim baseline'ımızda yok. HypeHype'ın stochastic tile-based lighting'i (§1.2) bizim cihaz aralığımız için doğru cevap olmaya devam ediyor. Öneri "ikisini harmanlayın" diyordu — harmanlanacak bir şey yok, aynı problemin iki farklı donanım varsayımıyla çözümü.

## D.4 Reddedilenler

### Mobile3DGS³ "TSGC fikrini VSM page update'inde kullan" — hayır

Tangent-space gradient caching, Gaussian splatting'in optimizasyon döngüsüne özgü. VSM page update'i ise görünürlük tabanlı bir cache invalidation problemi. İkisi arasında "cache" kelimesi dışında yapısal bir ilişki yok. Bu, teknoloji transferi değil kelime benzerliği.

### L2'ye "NeuralAccelerator" soyutlaması — yanlış katman

Öneri, Mali ve Adreno'nun neural birimlerini RHI'da ortak bir soyutlama altında toplamayı söylüyordu. Bu, **EK A.1'de yeni benimsediğimiz kurala doğrudan aykırı**: L2 userland kavramı bilmez. "NeuralAccelerator" bir donanım değil, bir kullanım amacıdır.

Doğrusu: neural birimler zaten **Vulkan ML uzantıları** üzerinden erişiliyor — yani L2 için sadece compute. Soyutlama L3'te, **upscaler arayüzü** seviyesinde olmalı (D.3'te tarif edildiği gibi). Adreno tarafındaki karşılığı ("Neural Fusion") doğrulayamadım; doğrulansa bile aynı yere oturur.

### Fluorite / Flutter-style reactive editor UI — hayır

Flutter + Filament tabanlı bir açık kaynak motor, bizim tasarım kararlarımız için kanıt değeri taşımıyor. Editor UI'ı Faz 8'in konusu ve reactive framework tartışması o zamana kadar ertelenir.

**Ama bu madde gerçek bir boşluğu ortaya çıkardı** — aşağıya bak.

### markmos'u "bağımsız doğrulama" saymak — hayır

Zero virtual dispatch ve SoA layout zaten planımızda ve sektör standardı. Küçük bir GitHub 2D motorunun aynı şeyi yapması doğrulama değil. Slotmap fikri alındı ama **kendi değeri üzerinden**, kaynağı nedeniyle değil.

## D.5 Listenin ortaya çıkardığı gerçek boşluk: UI sistemi yok

Fluorite maddesi "sizin UI planınız (SDF font + batch render)" diye başlıyordu. **Böyle bir plan yok** — dokümanda UI sistemi hiç geçmiyor. Öneri onu uydurmuş.

Ama işaret ettiği eksiklik gerçek. UI, mobil motorda küçük bir kalem değil:

- **Fillrate**: UI overdraw'ı mobilde ciddi bir maliyet. Tam ekran şeffaf katmanlar sessizce frame bütçesini yer
- **Draw call**: naif bir UI sistemi yüzlerce draw call üretir, GPU-driven pipeline'ın tüm kazancını geri verir
- **Font**: çok dilli metin, atlas yönetimi, shaping — sonradan eklenmesi zor
- **Layout**: her frame yeniden hesaplanan bir layout ağacı, CPU bütçesinde beklenmedik bir kalem olur

L4'e eklendi: SDF font atlası, tek batch çizim, retained layout ağacı (yalnızca dirty olduğunda yeniden hesap), opak UI önce / blend sonra, tam ekran şeffaf katman yasak.

**Genel ders:** iki öneri listesi de bize en çok, *önerdikleri şeyle değil, yokluğunu fark ettirdikleri şeyle* fayda sağladı. Crash reporter (EK C) ve UI sistemi (burada) planın kör noktalarıydı ve ikisi de listelerin yanlış maddelerinden çıktı.

---

# EK E — Üçüncü Öneri Listesi Değerlendirmesi

Bu liste öncekilerden **belirgin şekilde daha iyi** — dokümanın gerçek boşluklarını buluyor (PSO yükleme, IO stratejisi, input gecikmesi, renk yönetimi, ses spatialization). Ama beş maddede önerilen *mekanizma* yanlış, biri de az önce koyduğumuz kuralla doğrudan çelişiyor.

**Sonuç: beşi olduğu gibi alındı, ikisi düzeltilerek alındı, üçünde boşluk kabul edildi ama çözüm değiştirildi.**

## E.1 ⛔ Kendi kuralımızla çelişen madde: depth buffer'dan ses raycast'i

Öneri: "Depth prepass sonrası, **aynı tile memory'deki depth'i kullanarak (DRAM okumadan)** ses raycast'ini compute ile yap."

Bu, **EK D.1'de az önce yazdığımız kuralın birebir tekrarı olan hata.** Compute shader render pass'in dışındadır, tile memory'ye erişemez. "Tile memory'deki depth'i DRAM okumadan compute'ta kullanmak" mümkün değil — depth'i okumak için onu `STORE_OP_STORE` ile DRAM'e yazmak zorundasın, ki bu tam olarak kaçındığımız şey.

İkinci ve daha temel sorun: **depth buffer kameranın gördüğünü içerir, sesin gittiği yolu değil.** Kameranın arkasındaki ses kaynağı, frustum dışındaki duvar, oyuncunun sırtındaki kapı — hiçbiri depth buffer'da yok. Oklüzyon testi dünya uzayında bir problem, ekran uzayında değil.

Kaynak da hatalı: Project Acoustics **Microsoft**'un, Sony/Google'ın değil.

**Ama boşluk gerçek.** Ses katmanımda sadece "DSP graph" yazıyordu, 3D spatialization yoktu. Doğru çözüm ucuz ve sıradan: mesafe attenuation + HRTF panning, oklüzyon için kaynak→dinleyici arası birkaç ışın **fizik job'ında**. Jolt zaten sahnede, raycast zaten var, maliyeti önemsiz. L4'e eklendi.

## E.2 Olduğu gibi alınanlar

### 1. PSO yükleme stratejisi (Faz 1) — en iyi yakalama

Dokümanın gerçek bir açığı. "Tüm PSO'lar build'de üretilir" diyordum ama **runtime'da nasıl yükleneceğini** hiç yazmamıştım. Yüzlerce `vkCreateGraphicsPipelines` çağrısını açılışta sırayla yapmak, derlemeyi build'e almanın kazancını açılış süresine geri verir.

Çözüm iki katmanlı: **`VkPipelineCache` blob'u pack ile birlikte gönder** (driver'ın hazır formatı) + `VK_EXT_graphics_pipeline_library` ile parçalı linkleme. Faz 1'e eklendi.

*(Not: önerinin "Roadmap 2026 zorunlu kıldı" ifadesini doğrulayamadım. Teknik yine de geçerli, ama zorunluluk iddiasını AVP matrisinde kendin kontrol et.)*

### 2. `VK_EXT_host_image_copy` (Faz 1)

Doğru ve doğrudan işimize yarıyor. Texture upload'da staging buffer'ı ortadan kaldırıyor — iki kat bellek ve bir kopyalama gider. Vulkan Roadmap 2026 baseline'ında (EK B.2'de "host image copies" olarak zaten geçiyordu, kullanımını yazmamıştım). **Virtual texture page load'larında** özellikle değerli, çünkü orada sürekli küçük upload var.

### 3. Editörde shader hot-swap (Faz 8)

Faz 8'de "hot reload (gameplay katmanı)" yazıyordu, shader kasıtlı olarak dışarıdaydı — ama bu yanlıştı. Grafik programcısının ve teknik artistin zamanının çoğu shader iterasyonunda geçer.

Ve **A1'i ihlal etmiyor**: bu editör moduna özel bir yol. Ship build'de hiç derlenmez, orada her şey hâlâ önceden derlenmiş. Doğru çerçeveleme: A1'in editör istisnası.

### 4. Timestamp'li callback input (Faz 0)

L0'da "Input" yazıyordu ama polling mi callback mi belirtilmemişti — ve varsayılan (her frame poll) gerçekten gecikme ekliyor. Doğrusu: `ALooper` callback'i, olay timestamp'i saklanır, sim frame başlangıcını değil **o timestamp'i** kullanır.

⚠️ **Ama extrapolation'ı varsayılan yapma.** Öneri "hareketi 5-10ms ileri sür" diyordu. Bu overshoot üretir ve tür bağımlıdır — nişan alma ve hassas dokunmada kötü hissettirir. Timestamp doğruluğu bedava kazanç, extrapolation oyun başına açılacak bir seçenek.

### 5. Wave genişliği uyumu (Faz 2)

Prensip doğru: Mali ve Adreno'nun native wave genişlikleri farklı, `local_size`'ı sabit kodlarsan SIMD şeritlerini boşa harcarsın. Öneri "iki ayrı SPIR-V binary üret" diyordu — **gereksiz**. `VK_EXT_subgroup_size_control` + specialization constant ile tek binary'de çözülür.

Bizim mimarimize doğal oturuyor: cihaz-sınıfı bake matrisi zaten var (EK A.2), GPU satıcısı bir boyut daha ekliyor, o kadar. (Verilen "%40" rakamı kaynaksız; kazanç gerçek ama Mali Offline Compiler ile *ölç*, tabloya güvenme.)

## E.3 Düzeltilerek alınanlar

### 6. Async compute → pipelined cull (Faz 3)

Fikir doğru, mekanizma mobilde büyük ihtimalle çalışmaz.

**Sorun:** "Compute'u `VK_QUEUE_COMPUTE_BIT`'e, render'ı `VK_QUEUE_GRAPHICS_BIT`'e gönder" tavsiyesi masaüstü varsayımı. Birçok mobil GPU — özellikle Mali — **tek queue family, tek queue** sunar. Ayrı bir compute kuyruğu yoktur; gönderdiğin her şey aynı kuyrukta sıralanır. Hedef matrisinde `vkGetPhysicalDeviceQueueFamilyProperties` ile doğrula, varsayma.

**Ama önerinin ikinci yarısı — pipelining — mekanizmadan bağımsız olarak doğru ve değerli.** Cluster cull'u frame N-1 verisiyle koşturursan, frame N'in çizimi cull'u beklemez. Bu, ayrı kuyruk gerektirmez; sadece bağımlılığı bir frame ötelemek demek. Frame pipeline'ına eklendi.

⚠️ Bedeli var, yazıyorum: cull bir önceki kameraya göre yapılır, hızlı kamera hareketinde **ekran kenarında popping** olur. Standart azaltma: frustum'u bir miktar genişlet veya kamerayı tahmin et. Faz 3'te bunu ölç.

(Verilen "-1.5ms" rakamı kaynaksız.)

### 7. Termal tahmin (Faz 4)

Önerinin **dokümana yönelttiği eleştiri haklı**: "proaktif" yazmıştım ama mekanizma (headroom düşünce kaliteyi düşür) hâlâ reaktifti.

**Ama çözüm bir ML modeli değil.** Öneri LSTM/regresyon eğitmeyi söylüyordu; oysa Android'in `getThermalHeadroom()` API'si zaten **forecast parametresi alıyor** — işletim sistemi tahmini sana veriyor. Kendi modelini eğitmek, platformun sunduğu şeyi yeniden icat etmek olur (ve "%97 doğruluk" iddiasının kaynağı yok).

Doğru düzeltme: forecast penceresiyle oku + **histerezis** ekle, böylece çözünürlük eşik etrafında salınmaz. Faz 4'e yazıldı.

## E.4 Boşluk kabul edildi, çözüm değiştirildi

### 8. IO / yükleme stratejisi (Faz 6)

Eleştiri haklı — dokümanda "dosya aç" vardı ama **nasıl yükleneceği yoktu.**

Önerilen mekanizma ise yanlış: Android'de DirectStorage karşılığı bir `UFS_Read()` API'si yok, ve `VK_EXT_device_memory_report` bir **hata ayıklama/raporlama** uzantısı — bellek ayırma mekanizması değil. Kaynak tarihleri de hatalı (Snapdragon 8 Gen 4 diye bir ürün yok, o isim Snapdragon 8 Elite oldu; Dimensity 9400 2024 ürünü).

**Ve asıl mesele şu: mobilde texture için dekompresyon adımı zaten gerekmiyor.** ASTC, GPU'nun doğrudan tükettiği formattır — sıkıştırılmış halde örneklenir. "GPU'da dekomprese et" fikri, olmayan bir problemi çözüyor.

Doğru çözüm sade: pack `mmap`'lenir, ASTC blokları doğrudan gider, konteyner seviyesi supercompression yalnızca gerektiğinde ve job'da açılır, IO thread'inde de allocation yok. Faz 6'ya eklendi.

### 9. HDR / renk yönetimi (Faz 4, üst kademe)

Boşluk gerçek — dokümanda renk yönetimi hiç yoktu, tonemapping vardı ama gamut yönetimi yoktu.

Ama önerinin fazlaması (Faz 3, herkese) yanlış. **HDR bandwidth maliyeti demek**: 10-bit veya fp16 swapchain, piksel başına daha fazla byte — A3 ile doğrudan çelişiyor. Bu bir kalite özelliği, baseline değil.

Doğru kapsam: dahili render zaten lineer fp16, **çıkış transfer fonksiyonu resolve'da seçilir**, HDR çıkış üst cihaz kademesinde opsiyonel. Faz 4'e, bandwidth uyarısıyla birlikte yazıldı.

## E.5 Bu turun dersi

Üç listenin ortak örüntüsü netleşti:

> **Doğru buldukları: boşluklar. Yanlış oldukları: mekanizmalar.**

Bu listede beş gerçek boşluk vardı (PSO yükleme, IO, input, renk, ses) ve beşinin de önerilen çözümü ya yanlış ya gereksiz karmaşıktı. Boşlukları al, çözümleri kendin tasarla.

Ve tekrar eden bir hata sınıfı var: **"compute shader'a taşı" önerileri.** İki listede iki kez geldi (material resolve, ses raycast), ikisi de aynı nedenle yanlıştı — TBDR'da compute, render pass zincirini kırar. §8/10 kilitli kararı tam olarak bunun için yazıldı. Bir sonraki listede üçüncü kez gelirse, cevap hazır.

---

# EK F — Dördüncü Öneri Listesi (Işıklandırma / Gölge / Bloom / GI)

Bu listede **bir madde dört listenin en değerlisi** ve kilitli kararlarımızdan birini düzeltmemi gerektirdi. Bir madde de §8/3'ün ilk gerçek meydan okuyucusu.

**Sonuç: biri kritik (kural düzeltti), dördü alındı, biri ertelendi, dördü reddedildi.**

## F.1 🔴 Kritik: Adreno'da compute tile memory'ye erişebiliyor

`VK_QCOM_tile_memory_heap` ve kardeşi `VK_QCOM_tile_shading` gerçek, spec'te ve Adreno 840+ üzerinde çalışıyor. Qualcomm'un kendi dokümantasyonundan:

> Bu uzantılar, bir SSBO'yu birkaç render pass boyunca GMEM'de tutup compute dispatch'lerinden okuyup yazmak gibi senaryolara imkân veriyor. **Daha önce GMEM yalnızca driver'a açıktı ve compute shader'lara tamamen kapalıydı.**

Bu **§8/10 kilitli kararımızı doğrudan ilgilendiriyor.** Kuralı "compute tile memory'ye erişemez" diye yazmıştım — Mali'de ve varsayılan olarak doğru, ama Adreno artık bir kaçış yolu sunuyor:

| Uzantı | Ne veriyor |
|---|---|
| `VK_QCOM_tile_shading` | `vkCmdDispatchTileQCOM` — render pass **içinde**, tile boyutlu compute dispatch. Tile memory'ye erişimli |
| `VK_QCOM_tile_memory_heap` | Tile memory'yi **açıkça allocate et**. Kaynağı render pass'ler **arasında** tile'da tut |
| `VK_QCOM_tile_properties` | Tile boyutu ve konumunu sorgula |

İkincisi bizim en büyük yapısal ağrımıza değiyor. Vis buffer → material resolve → post zincirimiz her render pass sınırında tile'ı boşaltmak zorunda. Bu uzantı, kaynağı pass'ler arasında tile'da tutmaya izin veriyor.

**Kararı nasıl değiştirdim:** §8/10 duruyor ama artık bir istisna maddesi var. Gerekçe önemli — kuralı silmedim, çünkü:
- Sadece Adreno 840+. Mali'de yok, düşük uçta yok
- İkinci bir kod yolu demek, ve §8/8'de mesh shader'ı tam bu gerekçeyle reddetmiştik
- Ama buradaki kazanç mesh shader'dan farklı: **doğrudan bandwidth**, yani A3'ün merkezinde

**Karar:** Faz 3'te opsiyonel Adreno yolu olarak **ölç**. Kazanç %10'un altındaysa ikinci kod yoluna değmez, üstündeyse değer. Ölçmeden karar verme.

İlgili not: Qualcomm'un Vulkanised 2026 sunumunda geçen **Adreno HPM**, oyunlarda %10'a varan güç tasarrufu iddia ediyor. Aynı ailede, aynı faz.

## F.2 Alınanlar

### 1. Işık seçimini temporal olarak stabilize et

MegaLights'ı almıyoruz (RT + neural accel bağımlı) ama **bir mekanizmasını alıyoruz**: stokastik ışık seçimi doğası gereği frame'ler arası titrer. HypeHype'ın iki aşamalı reservoir sampling'i bu titremeyi *uzamsal* olarak amorti ediyor; temporal feedback eklemek *zamansal* kararlılığı veriyor. İkisi çelişmiyor, tamamlıyor. §1.2'ye eklendi.

### 2. Bloom: mipmap zinciri, compute değil

Verilen benchmark tablosundaki rakamlar şüpheli (frame time karşılaştırıyor, bloom maliyetini değil — sahne bilinmeden anlamsız). **Ama sonucu bizim bağımsız gerekçemizle örtüşüyor:** compute bloom render pass zincirini kırar, dispatch overhead ekler. §8/10'un doğrudan sonucu.

Mipmap zinciri (progressive downsample/upsample, donanım bilinear filtrelemesi) doğru seçim. Buna Com2uS fikrini ekle: **mip zincirini tek bir atlas RT'ye yerleştir**, render pass geçişi sayısını düşür.

⚠️ Ama önerinin "bloom'u tile memory'de tut" kısmı yanlış ve kendi içinde tutarsız: bloom tanımı gereği geniş yarıçaplı komşuluk gerektirir, tek tile'a sığmaz. EK D.2.3'teki tablomuz zaten bunu söylüyordu.

### 3. Selective bloom (Faz 3)

Tam ekran bloom yerine emissive/parlak kaynakları ayrı düşük-res target'a alıp composite etmek. Bizde zaten quarter-res particle target'ı var — **ikisi paylaşılabilir**, ayrı bir pass olmasına gerek yok.

### 4. Emissive'i probe grid'e bake et

Emission'ı sadece bir glow efekti değil, **GI'ya katkı veren ışık kaynağı** olarak ele almak doğru ve bizim için bedava: probe grid zaten build'de bake ediliyor, emissive yüzeylerin katkısı da orada hesaplanır. Runtime maliyeti sıfır. §1.2'ye eklendi.

### 5. Düşük kademe için blob/projector gölge

Makul. VSM ana strateji, düşük cihaz kademesinde blob/projector fallback.

⚠️ **Ama önerideki "termal headroom düşünce VSM'den projector'a geç" yanlış.** Gölge tipini oyun ortasında değiştirmek görsel sıçrama yaratır — oyuncu bunu bozulma olarak görür. Bu bir **bake zamanı cihaz sınıfı kararı** (EK A.2), runtime termal anahtarı değil. Termal tepki dynamic resolution ile verilir, sanat yönü değiştirilerek değil.

## F.3 Ertelenen: MagicDawn NDGI — §8/3'ün ilk gerçek meydan okuyucusu

Doğrulandı. Tencent MagicDawn NDGI gerçek: SPARK 2026'da **tam açık kaynak** yapıldı, cross-platform (mobil/PC/konsol), Arm ile mobil GPU'lar ve yerleşik AI hızlandırıcılar için derin optimizasyon yapılmış. Bir oyunda (Roco Kingdom) üretimde kullanılıyor.

*(Verilen "iPhone 12'de 0.83 ms" rakamını doğrulayamadım. Rakamı kaynak olarak kullanma.)*

**Neden ciddiye alıyorum:** §8/3'te ("realtime GI yok") gerekçem watt bütçesiydi. Sinir ağı inference'ına dayanan, mobil için optimize edilmiş, açık kaynak bir GI çözümü o gerekçeyi doğrudan test ediyor. Şimdiye kadar gelen "realtime GI" önerilerinin hepsi masaüstü varsayıyordu (Lumen, SVOGI, MegaLights). Bu ilki değil.

**Neden yine de taahhüt etmiyorum:**
- Kendi inference runtime'ını getiriyor — bizim "tek binary, runtime yok" felsefemize ek bir bağımlılık
- Pakete model ekliyor
- Bizim kontrolümüzde olmayan, hızla değişen bir açık kaynak projesine bağımlılık
- Ve muhtemelen yine neural accelerator'a yaslanıyor, yani cihaz kademesi sorunu geri geliyor

**Karar: ERTELE, Faz 6'da prototiple ve ölç.** Faz 6'da GI bake pipeline'ı zaten kuruluyor; NDGI'yi orada bake'e alternatif olarak ölçmek doğal. Ölçüm sonucu iyiyse §8/3 değişir. Bu, dört liste boyunca kilitli bir kararı gerçekten sarsan ilk madde.

**İlgili ikinci fikir:** MagicDawn'ın bulut tabanlı ışık bake servisi (hedefli senaryolarda 40x hızlanma iddiası). Bu, Roblox SLIM (EK A.2) ile aynı yöne işaret ediyor: **bake'i buluta taşımak.** Faz 6'da bake farm'ı hesaba kat — A1'in doğal uzantısı, çünkü build'de yapılan iş ne kadar ucuzsa o kadar çok şeyi build'e alabilirsin.

## F.4 Reddedilenler

### Gölge haritasında VRS — teknik olarak anlamsız

Öneri, shadow map üretiminde VRS ile shading rate düşürmeyi söylüyordu. Ama **shadow map pass'i zaten depth-only'dir ve fragment shader çalıştırmaz** (alpha-test'li geometri hariç). VRS *fragment shading rate*'i kontrol eder. Olmayan bir fragment shader'ın oranını düşürmek hiçbir şey kazandırmaz.

Gölge maliyetinin gerçek kaldıracı zaten planımızda: **VSM page cache** — statik gölgeler her frame yeniden çizilmez. Kazanç oradan gelir, VRS'ten değil.

(Kaynak olarak bir patent başvurusu gösterilmişti. Patent başvurusu bir tekniğin işe yaradığının kanıtı değil, birinin hak talep ettiğinin kanıtıdır.)

### AdaptiveGI'yi "2026'nın en iyisi" saymak

Unity Asset Store ürünü. Ürün sayfası, teknik kanıt değil. İçindeki tek işe yarar fikir (emissive'in GI'ya katkısı) zaten F.2.4'te alındı — kaynağı nedeniyle değil, kendi değeri üzerinden.

### MobileRC radiance caching'i "VSM page cache ile birleştir"

Üçüncü kez aynı hata sınıfı. Radiance caching path tracing'de ışın maliyetini düşürmek için ışıma bilgisini önbelleğe alır; VSM page cache görünürlük tabanlı bir invalidation mekanizmasıdır. Aralarında "cache" kelimesi dışında yapısal ilişki yok.

Daha önce aynı şey TSGC→VSM (EK D.4) ve MobileRC→VT feedback için de yapılmıştı. **Örüntü:** iki teknik aynı kelimeyi içeriyor diye birleştirilemez.

### "Bloom'u tile memory'de tut"

F.2.2'de açıklandı. Bloom cross-tile komşuluk ister, tek tile'a sığmaz.

## F.5 Bu turun bilançosu

Bu liste öncekilerden daha iyiydi çünkü **dokümanı okumuştu** — EK D.1'i doğru şekilde bize geri alıntıladı ve o kuralı ihlal eden bir öneri getirmedi.

İki gerçek katkısı oldu:

1. **`VK_QCOM_tile_shading` / `tile_memory_heap`** — dört listenin en değerli tek maddesi. Kilitli bir kararı yanlıştan çok *eksik* çıkardı: kural doğruydu ama istisnası vardı ve istisnayı bilmemek de kuralı bilmemek kadar pahalı.
2. **MagicDawn NDGI** — §8'deki bir kararı gerçekten sarsan ilk madde. Kapatmadım, ölçüme bağladım.

Geri kalan sekiz madde tanıdık örüntüyü sürdürdü: ya zaten dokümanda, ya kelime benzerliğine dayalı yanlış transfer, ya da mağaza sayfası kaynaklı.

---

# EK G — Araştırma Haritası: Nerede Sıkıştık

## G.0 Beşinci liste: sıfır yeni bilgi

Gelen beşinci liste tamamen tekrardı:

| Madde | Durum |
|---|---|
| Donanım-neural render | EK C.2 + EK F.1'de zaten değerlendirildi |
| MagicDawn NDGI | EK F.3'te ertelendi, ölçüme bağlandı |
| Mobile-DDGI | EK F'de değerlendirildi |
| MegaLights temporal stabilizasyon | EK F.2.1'de zaten **alındı** |
| MobileRC radiance caching | EK F.4'te reddedildi — bu **üçüncü** gelişi |

*(Not: listedeki bir başlıkta MegaLights'ı anlatmak için alakasız bir siyasi isim benzetmesi kullanılmış. Bu tür artıklar, metnin gözden geçirilmeden üretildiğinin işareti — içeriğe güvenirken bunu hesaba kat.)*

## G.1 Asıl sorun: beş liste, tek alan

Beş listenin tamamı **render ve ışıklandırma** etrafında dönüyor. O alanda artık getiri azalıyor çünkü doküman zaten doygun.

Oysa planın hiç dokunulmamış alanları var — ve bazıları render'dan **daha belirleyici**:

| Alan | Durum | Kritiklik |
|---|---|---|
| Render / ışıklandırma | 5 liste, doygun | Orta (zaten çözüldü) |
| **Tulpar dili ve derleyici tasarımı** | **Sıfır araştırma** | **En yüksek — tüm moat buna dayanıyor** |
| **Kurulum boyutu ve dağıtım** | **Sıfır** | **Yüksek — ticari metrik** |
| **Performans CI / cihaz farm'ı** | **Sıfır** | **Yüksek — kapıları uygulayan mekanizma** |
| Editor ve tooling mimarisi | Zayıf (L7 ince) | Yüksek — "işin %90'ı" demiştik ama planlamadık |
| İterasyon hızı / derleme süresi | Sıfır | Yüksek |
| Netcode ve determinizm | Ertelendi | Orta — mimariyi etkiler |
| Animasyon sistemi | Bir satır | Orta |
| Telemetri / live ops | Sıfır | Orta |
| Anti-cheat / güvenlik | Sıfır | Düşük-orta |
| Erişilebilirlik, lokalizasyon | Sıfır | Düşük |

Aşağıdaki üç bölüm bu boşluklardan en kritik üçünü açıyor.

## G.2 Jai ve Order of the Sinking Star — en yakın emsal, bir ay sonra açılıyor

Beş liste boyunca kimse **Tulpar'ın kendisinden** bahsetmedi. Oysa projenin tüm tezi "dil bizde" üzerine kurulu ve bu alanda çalışan bir emsal var.

**Jai**, Jonathan Blow'un 2014'ten beri geliştirdiği, özellikle oyun geliştirme için tasarlanmış, compile-time metaprogramming odaklı C++ alternatifi. Bizim A1 aksiyomumuzun ("runtime'da yapılabilecek her şey build'de yapılır") dil seviyesindeki karşılığı tam olarak Jai'nin merkezi fikri.

**Ve zamanlama dikkat çekici:**

- Blow'un oyunu **Order of the Sinking Star, 8 Ekim 2026'da çıkıyor** — bir aydan az kaldı
- Oyun tamamen Jai ile yazılmış **özel bir motor** üzerinde
- Thekla, Aralık 2025 duyurusunda oyun çıktıktan **kısa süre sonra motoru açık kaynak yapacaklarını** açıkladı
- Dilin kendisinin de eninde sonunda açık kaynak olacağı belirtilmiş

Yani birkaç ay içinde, **özel bir dille yazılmış, üretimde kullanılmış, tam bir oyun motorunun kaynak kodu** okunabilir olacak. Bu, dört listede gelen hiçbir maddeden daha değerli.

**Ne yapılmalı:**

1. **Jai'yi kopyalamak değil, tasarım kararlarını okumak.** Blow on iki yıldır aynı problemleri çözüyor: compile-time execution, build sisteminin dile gömülmesi, hızlı derleme, SoA/AoS dönüşümleri. Tulpar'ın çözmesi gereken problemlerin çoğu bunlar
2. **Motor açıldığında oku.** Özel dil + özel motor kombinasyonunun gerçekte nasıl göründüğünü, hangi yerlerde bedel ödendiğini görmek, bizim Faz 2 tasarımımızı doğrudan etkiler
3. **Ama bağımlılık kurma.** Jai kapalı beta, proprietary, tek kişiye bağlı ve mobil hedefi yok. Emsal olarak değerli, altyapı olarak değil

Aynı alanda ikinci referans: **Zig** ve **Odin** — ikisi de Jai'den etkilenmiş, ikisi de açık, Zig'in comptime modeli ve incremental derleme çalışması Tulpar'ın derleme süresi tasarımı için doğrudan okunabilir.

## G.3 Kurulum boyutu — RAM'den sert, ve ticari bir metrik

Planda bellek bütçesi vardı ama **kurulum boyutu hiç yoktu.** Oysa mobilde bu daha sert bir kısıt ve doğrudan gelire bağlı.

**Sert sınırlar:**

| Sınır | Değer |
|---|---|
| Google Play base modül (sıkıştırılmış indirme) | **200 MB** — aşılırsa yayınlanamaz |
| Asset pack toplamı (AAB içinde) | ~2 GB, en fazla 100 pack |
| App Store (sıkıştırılmamış) | **4 GB** — aşılamaz, çözüm sunucudan indirme |
| 200 MB üstü | Mobil veride kullanıcıya uyarı diyaloğu |

**Ve asıl rakam — bu, tüm projenin en güçlü ticari argümanı:**

> Google'ın ölçümü: **her 6 MB APK artışı, kurulum dönüşüm oranını yaklaşık %1 düşürüyor.**

Bunu bizim §5'teki bellek bütçesine uygula:

| Motor | Baseline | 12 MB'a göre fark | Dönüşüm etkisi |
|---|---|---|---|
| Tulpar | ~12 MB | — | referans |
| Godot | 40-60 MB | +28-48 MB | **~%5-8 daha düşük kurulum** |
| Unity (IL2CPP) | 60-120 MB | +48-108 MB | **~%8-18 daha düşük kurulum** |

Bu, herhangi bir render optimizasyonundan **daha değerli.** %10 daha fazla fps kimsenin indirme kararını değiştirmez; %10 daha fazla kurulum doğrudan gelirdir. Ve etki gelişmekte olan pazarlarda (depolama ve bağlantı kısıtlı) daha da büyük.

**Yani:** binary boyutu bir mühendislik tercihi değil, **ürün metriği.** §5'e eklendi ve Faz 0'dan itibaren ölçülmeli — tıpkı frame time gibi, bir eşiği aşınca build kırılmalı.

**Mimari sonuç (Faz 6):** pack formatı, Play Asset Delivery asset pack'lerine ve iOS on-demand resources'a **doğrudan eşlenebilmeli.** Cihaz-sınıfı bake çıktılarımız (EK A.2) zaten ayrı ayrı üretiliyor — bunlar doğal asset pack sınırları. Sonradan eşlemeye çalışmak format değişikliği demek.

## G.4 Performans CI — kapıları uygulayan eksik mekanizma

Dokümanda her fazın bir **kapı şartı** var ("bandwidth < 8 GB/s ölçülmüş", "99p frame time < 18 ms"). Ama bunları **sürekli** ölçen bir mekanizma yok. Kapıyı bir kez geçmek, o eşikte kalmak demek değil.

Eksik olan: **her build'de, gerçek cihazda, otomatik performans regresyon testi.**

Bu bizim mimarimize özellikle iyi oturuyor çünkü **deterministik replay zaten planda var** (Faz 5). Replay, tekrarlanabilir bir performans testidir — aynı girdi, aynı sahne, aynı ölçüm.

**Ölçüm disiplini** (sektör pratiğinden):

- **Ölçüm penceresi 10-15 dakika.** Termal throttle o aralıkta başlıyor. Kısa koşu, A4'ün ölçmek istediği şeyi hiç görmez
- **3-5 koşu ortalaması.** Tek koşu güvenilmez; 3+ koşu varyansı belirgin şekilde düşürüyor
- **Arka plan süreçleri ve düşük pil ölçümü bozar** — test cihazları kontrollü durumda tutulmalı
- Paralel cihaz farm'ı ile CI/CD entegrasyonu mümkün (PerfDog Service, GameBench, HeadSpin, Bitbar sınıfı araçlar)

**Ne ölçülmeli:** 99p frame time, bandwidth, sustained fps (10 dk sonrası), bellek tepe değeri, **kurulum boyutu**, açılış süresi. Hepsi eşikli, eşiği aşan build kırılır.

Faz 0'a eklendi — profiler ve crash reporter ile aynı sırada, çünkü üçü de "sonradan eklenmesi acı veren" altyapı.

## G.5 Sırada ne araştırılmalı

Render alanında arama. Sırayla bunlar:

1. **Derleme süresi ve iterasyon hızı.** Whole-program compilation güzel ama tam derleme 10 dakika sürerse motor kullanılamaz. Incremental derleme, modüler linkleme, Zig'in in-place binary patching çalışması. Bu, Faz 2'nin gizli riski
2. **Editor mimarisi ve The Truth veri modeli.** L7 hâlâ üç satır. "İşin %90'ı" dediğimiz alan en az planlanmış alan
3. **Netcode ve deterministik simülasyon.** Faz 5'te fizik seçimini kilitliyoruz; netcode kararı o seçimi etkiliyor, sonraya bırakılamaz
4. **Animasyon sistemi.** Planda tek satır. Sıkıştırma, blend ağacı, root motion, IK — hepsi mimariyi etkiler
5. **Telemetri ve saha ölçümü.** Crash reporter'ı ekledik ama performans telemetrisi yok. Gerçek cihaz dağılımını sahadan öğrenmeden cihaz sınıfı bake'i doğru ayarlanamaz

Bunlardan herhangi biri için araştırma isteyebilirsin — ama listelerden gelmesini bekleme. Beş listenin hepsi aynı alana bakıyordu çünkü o alan popüler, planın gerçek riskleri orada değil.

---

# EK H — DLSS: Ne Açıldı, Ne Alınabilir

Kısa cevap: **DLSS'in kendisi bize kapalı, ama etrafındaki iki şey işimize yarıyor** — ve biri gerçek bir boşluk kapattı.

## H.1 Durum tespiti — "açık kaynak" burada üç ayrı şey demek

Karıştırılan üç şey var:

| Şey | Durum | Bize yarar mı |
|---|---|---|
| **NVIDIA/DLSS reposu** | Açık — ama **SDK**: header'lar, programming guide, örnek uygulama | ⚠️ Dolaylı (bkz. H.2) |
| **DLSS algoritmasının kendisi** | **Kapalı.** `nvngx_dlss.dll` / `libnvidia-ngx-dlss.so` içinde, NVIDIA Tensor Core'larda çalışır | ❌ Hayır |
| **Streamline** | Gerçekten açık (SL 2.0+, DLSS-G eklentisi hariç tümü kaynaktan derlenebilir) | ✅ Tasarım referansı olarak |

Ayrıca 2026'da bir **DLSS kaynak kodu sızıntısı** haberi dolaştı. Sızıntı açık kaynak değildir — lisanssız tescilli kod kullanmak hukuki risk, ve zaten işe yaramaz: kod NVIDIA donanımına bağlı.

**Neden DLSS mobilde çalışamaz, net olarak:**
- NVIDIA Tensor Core gerektirir. ARM Mali ve Qualcomm Adreno'da yok
- Windows/Linux x86-64 binary. Android ARM64 veya Metal hedefi yok
- Kapalı NGX runtime'a bağlı

Yani "GitHub'da yayınlandı" doğru ama **yayınlanan şey algoritma değil, entegrasyon arayüzü.**

Bir de şu ironi var: bizim için önemli olan açık kaynak zaten elimizde. **Arm ASR, AMD'nin FSR2'sinden türetilmiş ve MIT lisanslı** (EK A.3). Yani temporal upscaling'in gerçekten açık ve mobil için optimize edilmiş kolu bizde. DLSS, kullanamayacağımız olan.

## H.2 ✅ Alınan: upscaler girdi sözleşmesi — gerçek bir boşluk kapandı

DLSS reposundaki **programming guide** herkese açık ve içinde asıl değerli şey var: **girdi sözleşmesi.** Ve bu sözleşme ASR, FSR2, XeSS ve DLSS için büyük ölçüde **aynı** — çünkü hepsi aynı temporal reprojection problemini çözüyor.

Planımızda "motion vector zorunlu, baştan planla" yazıyordu ama **sözleşmenin tamamı yazılı değildi.** NVIDIA'nın entegrasyon dokümantasyonu eksik girdilerin sonucunu açıkça sayıyor: bulanık objeler, kopuk karakterler, artan titreme, UI ve HUD'da kararsızlık.

Üç kural dokümana eklendi:

**1. UI, upscaler'dan SONRA çizilir — ve HUD'suz bir renk buffer'ı gerekir.**
UI düşük çözünürlükte render edilip upscale edilirse metin ve HUD titrer. Upscaler'a HUD'suz renk verilir, UI tam çözünürlükte üste bindirilir. Frame pipeline'ımızdaki sıra zaten doğruydu ama **gereklilik olarak yazılı değildi** — yazılmayan kural ihlal edilir.

**2. Reactive / transparency mask — bizde eksikti, gerçek bir hata kaynağıydı.**
Particle'lar ve şeffaf yüzeyler temporal reprojection'ı kırar: motion vector'ları yoktur veya arkalarındaki geometrininkini taşırlar. Maske verilmezse upscaler bunları **ghost'lar** — hareket eden particle'ların arkasında iz kalır.

Bizim planda **quarter-res particle target'ı** var (§1.6, mobilde zorunlu) ve hemen ardından temporal upscaler geliyor. Bu kombinasyon maske olmadan **kesinlikle artefakt üretir.** Particle pass'i artık reactive mask'e de yazıyor. Faz 4'e eklendi.

**3. Jitter dizisi ve motion vector konvansiyonu upscaler'la eşleşmeli.**
Jitter offset'i, motion vector'ın yönü ve ölçeği, depth'in ters olup olmadığı — hepsi upscaler'ın beklediği konvansiyonda olmalı. Yanlış konvansiyon "çalışıyor ama bulanık" üretir, ki teşhisi en zor hata sınıfıdır.

Bu üçü **ASR entegrasyonunu doğrudan kolaylaştırıyor** — DLSS'i hiç kullanmasak bile. Sözleşme evrensel, uygulama değişir.

## H.3 ✅ Alınan: Streamline bir tasarım referansı olarak

EK C.2'de upscaler'ı sabit çağrı değil **arayüz** yapmaya karar vermiştik (ASR varsayılan, NSSD/NFRU üst kademe). Streamline tam olarak bu problemi çözen bir katman ve artık kaynaktan derlenebilir durumda.

Kodunu kullanmayacağız — Windows/DX odaklı, bizim ihtiyacımızdan çok daha büyük. Ama **arayüz tasarımını okumak** Faz 4'te bir gün kazandırır: hangi sabitler her frame veriliyor, buffer'lar nasıl etiketleniyor, feature'lar nasıl sorgulanıyor, debug görselleştirmesi nasıl kurulmuş.

İkinci okunabilir referans: **Bevy'nin `dlss_wgpu` entegrasyonu.** Açık bir motorun upscaler'ı nasıl sardığını gösteriyor — bizim seam'imize daha yakın ölçekte.

## H.4 ❌ Alınmayanlar

**DLSS'in kendisi.** Tensor Core, x86, kapalı runtime. Mobilde çalışması mümkün değil.

**NVIDIA Image Scaling (NIS).** Gerçekten açık kaynak ve cross-platform (DX11/DX12/Vulkan compute shader'ları). Ama **uzamsal** bir upscaler — FSR1 sınıfı. Arm ASR temporal ve mobil için optimize; NIS ondan geriye adım olur. Almıyoruz.

**Sızdırılan kod.** Hukuki risk, ve teknik olarak da faydasız — donanıma bağlı.

## H.5 Özet

Sorunun cevabı: **evet, alınacak şey var ama sandığın yerde değil.**

Alınan, DLSS'in algoritması değil — **etrafındaki mühendislik disiplini.** Girdi sözleşmesi ve arayüz tasarımı, upscaler markasından bağımsız olarak geçerli. Ve bunlardan biri (reactive mask) bizim quarter-res particle planımızla temporal upscaler'ımızın çarpışacağı gerçek bir noktayı yakaladı — Faz 4'te bulup çözmek yerine şimdi yazılı olması, bir haftalık artefakt avını önler.

Bu, EK G'deki örüntüyü de doğruluyor: **en değerli bulgular teknoloji listelerinden değil, entegrasyon dokümantasyonundan geliyor.** Bir tekniğin ne yaptığını anlatan pazarlama sayfası değil, onu üretimde nasıl bağlayacağını anlatan programming guide.
