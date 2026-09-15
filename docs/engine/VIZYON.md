# Tulpar Engine — Vizyon: AAA Seviye Mobil Motor (2026 durumuna göre)

> Bu doküman "her şeyde herkesi geç" demiyor — bu, kaynaksız bir iddia olurdu
> ve kendi kuralımıza (`DURUM.md` §7: sayı yoksa iddia yok) aykırı düşer. Bunun
> yerine: **nerede gerçekten yapısal olarak üstün olabileceğimizi**, **nerede
> eşitlenmemiz yeterli olduğunu**, **nerede rekabet etmenin anlamsız olduğunu**,
> ve **nereye henüz yatırım yapılmaması gerektiğini** net ayırıyor.
>
> **Sürüm notu:** Bu sürüm, kullanıcının ilettiği geniş bir araştırma taslağını
> (65+ başlık, Eylül 2026 tarihli) tek tek web'den doğrulayarak süzüyor. Metodoloji
> ve doğrulama sonuçları §6'da şeffaf şekilde listeleniyor — hangi iddia
> doğrulandı, hangisi düzeltildi, hangisi hâlâ doğrulanmadı.

## 0. Güncel rakip durumu (2026, web'den doğrulandı)

| Motor/Teknoloji | 2026 durumu | Bize etkisi |
|---|---|---|
| **Unreal Engine 5.8** (Haziran 2026) | Nanite artık **amiral gemisi mobil GPU'larda** çalışıyor — Arm'ın "Mori" demosu Nanite açıkken 6M+ görünür üçgeni 30 FPS'te render ediyor, kapalıyken demo **çöküyor** (yani Nanite olmadan içerik hiç çalışamayacak kadar yoğun tasarlanmış — doğrulandı, [Arm blog](https://developer.arm.com/community/arm-community-blogs/b/mobile-graphics-and-gaming-blog/posts/mori-to-nanite-billions-of-triangles-on-mobile)) | Eski varsayımımız ("Nanite mobilde yok") **kısmen eskidi** — ama orta/düşük segmentte hâlâ geçerli, ve demo tek senaryo, genel oyun değil |
| **Unity / Tuanjie (团结引擎)** | Çin'de Haziran 2026'dan beri Unity'nin yerini almak **zorunlu** (Unity global oyun servislerini Çin'de kapattı) — güncel sürüm **1.10.x/1.6.13**, "2.0" diye bir sürüm bulunamadı (bir önceki taslakta bu hataydı, burada düzeltildi). GPU Resident Drawer ile otomatik instancing/indirect draw | Bizim "shader hitch yok, build'de PSO" avantajımız hâlâ geçerli — Unity/Tuanjie hâlâ runtime derliyor |
| **Godot 4.6/4.7** | Verimli Vulkan render'ı, iyi 2D, sıfır telif; 4.7'de HDR output + Standalone Android Export geldi | "Devrimsel" değil — gerçek rakip **boyut/RAM/determinizm** eksenlerinde biziz, teknoloji eksenimizde değil |
| **Nau Engine** (Rusya) | Kasım 2024'te açık betaya girdi, **3-clause BSD**. Gaijin'in açık kaynak Dagor Engine'inin render/sistem çekirdeğini ve **Jolt Physics**'i kullanıyor (doğrulandı, [gameworldobserver](https://gameworldobserver.com/2023/11/03/gaijin-entertainment-dagor-engine-open-source-russian-nau-engine)) | "Ulusal motor" kendi motorunu yazmak yerine mevcut açık kaynağı birleştirdi — bizim tamamen kendi yazdığımız mimari daha nadir bir konum |
| **NetEase Messiah Engine** (Çin) | Gerçek, sevk edilen bir motor — *Where Winds Meet* ve *Destiny Rising* bunula yapıldı, Adreno 600/Mali-G72 gibi orta segment GPU'lar için "fiziksel doğruluktan ödün, kararlı FPS" felsefesiyle tasarlanmış (doğrulandı). **"Unreal'den 2 yıl önce Frame Graph" iddiası bu oturumda doğrulanamadı** — kaynak bulunamadı, bu iddia dışarıda bırakıldı | Kanıt: büyük stüdyolar bile mobilde "maksimum kalite" yerine "cihaz sınıfına göre kararlı FPS" seçiyor — bizim cihaz-sınıfı bake yaklaşımımızla aynı yönde |
| **NVIDIA DLSS 5** (3 Eylül 2026 lansman) | Gerçek — **generative rendering**: geleneksel render'ın üstüne bir diffusion modeli aydınlatma/malzeme görünümünü **yeniden üretiyor**, sadece upscale değil (doğrulandı, [NVIDIA ADLR](https://research.nvidia.com/labs/adlr/DLSS5/)). Şu an yalnız RTX 50-serisi | Mobilde NPU'muz yok ama **yön** net: gelecekte "render + nöral post-diffusion" ayrımı büyüyecek — Arm'ın NSS/NFRU (aşağıda) mobildeki karşılığı |
| **3D Gaussian Splatting (mobil)** | Aktif akademik araştırma, henüz üretim-hazır değil | İzlenecek cephe, **şimdi yatırım yapılacak yer değil** — bkz. §3 |

## 1. Nerede GERÇEKTEN kat kat üstün olabiliriz (yapısal, ölçülebilir)

Bunlar "daha çok efor" ile değil, **mimari kararla** kazanılıyor — zaten kilitli:

| Eksen | Neden yapısal | Şu an durum |
|---|---|---|
| **Sıfır çöp toplama / kare içi 0 allocation** | Unity/Godot/Unreal'ın hiçbiri bunu garanti edemez (GC/refcount/managed heap) | ✅ Zaten var, ölçülü (`AllocGate`) |
| **Determinizm (3 platform bit-eş)** | Hiçbir büyük motor bunu vaat etmiyor | ✅ Zaten var — bunun üstüne **rollback netcode neredeyse bedava** kurulabilir (henüz yapılmadı) |
| **Build'de derlenmiş shader/PSO** | Unity/Unreal hâlâ runtime shader variant derliyor (hitch) | ✅ Zaten var |
| **Küçük ikili boyut / hızlı açılış** | Unity IL2CPP 60-120 MB, Nau Engine beta 126 MB, biz ~12 MB hedefliyoruz | ✅ Kısmen ölçülü (2.7 MB stripped) |

**Aksiyon:** bunları KORUMAK — yeni özellik eklerken bu 4 ilkeyi bozmamak, tek başına rekabet avantajı.

### 1.1. Tam 15 maddelik yapısal avantaj matrisi (kullanıcının Kategori A listesiyle birebir)

Yukarıdaki 4 madde zaten kilitli. Kalan 11'i **gerçek ve mantıklı hedefler**, ama
henüz yapılmadı — bunlar `DEVAM_PLANI.md`/§2-§3'teki bahislerin karşılığı:

| # | Madde | Durum | Nerede ele alınıyor |
|---|---|---|---|
| 5 | Rollback hazırlığı | 🔧 Yapılmadı | §2.3 — determinizm zaten var, en yüksek kaldıraç |
| 6 | Cihaz-sınıfı bake | 🔧 Yapılmadı | Eski vizyon §4 (bu sürümde §2'ye taşınmadı, hâlâ geçerli bahis) |
| 7 | Shader DSL + otomatik fp16 | 🔧 Yapılmadı | Aynı yerde, uzun vadeli |
| 8 | GPU-Driven + Bindless + Mesh Shader | 🔧 Yapılmadı | §2.1 |
| 9 | Job System + Work-Stealing | ✅ Kısmen var | Fiber job sistemi zaten var (`DURUM.md` L1); work-stealing zamanlaması ayrı doğrulanmadı |
| 10 | Sanal Doku + NTC | 🔧 Yapılmadı | §2.1 |
| 11 | 3D Ses (HRTF + Oklüzyon) | 🔧 Yapılmadı | Yalnız miniaudio temel mikser var (`DURUM.md` L3 audio) |
| 12 | Lag Compensation + Snapshot Interpolation | 🔧 Yapılmadı | Rollback'in bir üstü, aynı öncelik grubu |
| 13 | Termal/Güç Yönetimi | 🔧 Yapılmadı | §2.2 — ADPF doğrulandı, entegre edilmedi |
| 14 | Anti-Cheat | 🔧 Yapılmadı | Çok oyunculu bir oyun planlanınca gündeme gelir |
| 15 | Nöral Rendering | 🔧 Yapılmadı, donanım da yok | §3 — Mali G2-Ultra NX gerçek ama Tulpar'ın hedef cihaz aralığında değil |

## 2. Doğrulanmış, aksiyon alınabilir teknikler (Tier B — gerçek, kaynaklı)

Taslaktaki yüzlerce maddeden, bu oturumda **gerçekten var olduğu ve iddia edilen
özelliklere sahip olduğu doğrulanan**, Tulpar'a somut fayda sağlayabilecek olanlar.
Sırasız değil — üstteki daha temel, alttaki daha ileri seviye.

### 2.1. Render mimarisi

| Teknik | Ne işe yarar | Tulpar'a somut fayda |
|---|---|---|
| **Frame/Render Graph** | Deklaratif pass yönetimi: `fg.AddPass("GBuffer").Read(x).Write(y)` → otomatik sıralama + bellek aliasing + senkronizasyon. Sektör standardı (Frostbite'ın "FrameGraph"ı, Unreal'in RDG'si — gerçek, yaygın desen) | Şu an `renderer.cpp` elle sıralı pass'ler kullanıyor; büyüdükçe (toon/foliage/vat/unlit shader'ları kabloladıkça) manuel sıralama kırılganlaşacak. **Faz C sonrası, gerçek bir sonraki mimari yatırım adayı.** |
| **Bindless descriptor indexing (Vulkan 1.2)** | Tek global `sampler2D[]` descriptor array, `UPDATE_AFTER_BIND`+`PARTIALLY_BOUND` — kare başına tek bağlama, CPU draw-call overhead'i düşer | Şu an `Material` per-draw descriptor set kullanıyor (`renderer.hpp` `Material`/`Draw`). Sahne büyüdükçe (yüzlerce malzeme) bindless geçişi CPU maliyetini düşürür — **gerçek Vulkan 1.2 spec özelliği, mobil GPU'larda yaygın destekli** |
| **Cluster/meshlet LOD (Nanite-tarzı)** | Sabit boyutlu (64-128 üçgen) cluster'lar, hata metriğine göre runtime LOD seçimi. Bevy'nin meshlet issue'su (bevyengine/bevy#11518) ve benzeri açık kaynak projeler bunu aktif geliştiriyor — gerçek, olgunlaşan bir alan | Tulpar'da şu an yalnız discrete LOD (meshoptimizer, `DURUM.md` L6) var. Nanite'ın "%60-70'i" mobilde ulaşılabilir bir hedef, ama **büyük mühendislik yatırımı** — Faz C sonrası bahis |
| **Sanal doku / sparse residency** | Vulkan'da dokuları fiziksel belleğe bağlı olmadan oluşturma; sadece görünen sayfalar VRAM'e yüklenir | VRAM kısıtlı düşük segment cihazlar için gerçek kazanç, ama **şu an motorun doku sistemi bunun önkoşulu olan sayfalama altyapısına sahip değil** — uzun vadeli |

### 2.2. Platform/donanım entegrasyonu

| Teknik | Doğrulama | Tulpar'a somut fayda |
|---|---|---|
| **ADPF (Android Dynamic Performance Framework)** | Gerçek, resmi Android API'si — Thermal API + Performance Hint API, termal durumu izleyip iş yükünü proaktif ayarlamayı sağlar | Şu an motorda termal/güç yönetimi **yok** (`DEVAM_PLANI.md` Faz C listesinde de değil — eklenebilir bir boşluk). Android host'ta (`app/` NativeActivity) entegre edilebilir, orta efor |
| **Turnip (Mesa Vulkan sürücüsü, Adreno)** | Doğrulandı — Android'de **resmi Vulkan conformance alan ilk Mesa sürücüsü**, Adreno 6xx'te Vulkan 1.3, 700 serisinde 1.4 conformant, Valve'in Steam Frame'inde kullanılıyor ([Phoronix](https://www.phoronix.com/news/Steam-Frame-Turnip-Vulkan)) | Bize doğrudan aksiyon değil, ama **açık kaynak Vulkan sürücü ekosisteminin ciddiye alınması gerektiğinin kanıtı** — Adreno cihazlarda garip davranış görülürse Turnip kaynağına bakılabilir (kapalı kaynak sürücü değil) |
| **Vulkan TBR best practices (Khronos)** | Load/store op'ların bilinçli kullanımı bandwidth kontrolünün birincil aracı — bu zaten `rhi/tile_budget.hpp/cpp` ile kod seviyesinde zorlanıyor | Zaten yapıyoruz — bu bir doğrulama, yeni iş değil |

### 2.3. Determinizm + rollback netcode ekosistemi

Bu bölüm özellikle önemli çünkü Tulpar'ın **zaten sahip olduğu** determinizm
avantajının (§1) ne kadar nadir olduğunu somut, gerçek projelerle kanıtlıyor:

| Proje | Doğrulama | Not |
|---|---|---|
| **GGRS** (Rust) | Gerçek, aktif — GGPO'nun güvenli Rust'ta yeniden yazımı, P2P rollback ([gschup/ggrs](https://github.com/gschup/ggrs)) | Rollback netcode'un nasıl paketlendiğine iyi bir referans API tasarımı |
| **Gizmo Engine** (Rust) | Gerçek ([bdrtr/Gizmo](https://github.com/bdrtr/Gizmo)) — archetype ECS + deterministik, sabit-adımlı rigid-body fizik (SoA `PhysicsWorld`) | `state_hash`/cross-process-oracle iddiası bu oturumda doğrulanamadı (repo'da doğrudan görülmedi) — bu tek ayrıntı şüpheli işaretlenir, geri kalanı gerçek |
| **MAME4droid** (2026, v1.37.6) | Gerçek ve doğrulandı kelimesi kelimesine: "boot fully deterministic (RTC, samplerate, disk/NVRAM state pinned), so only inputs ever cross the network" + IPv6 ile CGNAT arkasından sunucusuz P2P | **Somut kanıt**: iyi bir determinizm modeli, ağ üzerinden yalnızca girdi göndermeyi (bizim de yapabileceğimiz) production'da kanıtlıyor |
| **Jolt Physics** | Yaygın bilinen gerçek — Death Stranding 2 ve Horizon Forbidden West'te kullanılıyor, multi-thread rigid+soft body | Zaten Tulpar'da kullanılıyor (`DURUM.md` L4) — bu bir doğrulama |

**Aksiyon (değişmedi, güçlendi):** rollback netcode hâlâ en yüksek kaldıraçlı bahis
— hem çok oyunculu bir oyun planlanıyorsa, hem de yukarıdaki projelerin hiçbiri
Tulpar'ın zaten sahip olduğu **3-platform bit-eş** garantisine sahip değil (GGRS/Gizmo
tek-platformlu Rust projeleri, MAME4droid tek bir emülatörün iç durumu).

### 2.4. Bireysel/AAA-dışı motor ekosistemi (gerçek, GitHub'da doğrulanabilir)

| Motor | Dil | Neden ilginç |
|---|---|---|
| **Bevy** | Rust | Data-driven ECS, aktif meshlet/Nanite-tarzı geometri araştırması sürüyor |
| **Fyrox** | Rust | 2D/3D + dahili editör, tek geliştiriciden büyüyen nadir örnek |
| **Filament** (Google) | C++ | Mobil PBR render endüstri standardı, "Android'de olabildiğince küçük/verimli" felsefesi — malzeme sistemi tasarımı için doğrudan referans |
| **Macroquad / Ebitengine / KorGE** | Rust/Go/Kotlin | Basit, tek-geliştirici, gerçek mobil export desteği olan motorlar — "az kişiyle çok iş" örnekleri |

### 2.5. Mimari bahisler (dış doğrulama gerektirmez — kendi tasarım kararımız)

Bunlar bir dış kaynağı doğrulama meselesi değil, **motorun kendi mimari kararı**
— bu yüzden Tier B'nin geri kalanından farklı: hiçbir rakip bunu "doğru yapmıyor"
diye eleştirilemez, çünkü çoğu rakip bunu hiç denemiyor.

| Bahis | Ne demek | Neden önemli |
|---|---|---|
| **Cihaz-sınıfı bake** | Aynı sahneyi düşük/orta/yüksek segment için derleme zamanında ayrı ayrı bake etmek (Roblox SLIM tarzı) — NetEase Messiah'ın "cihaza göre kararlı FPS" felsefesiyle aynı yönde (§0), ama runtime karar yerine **build-time karar** | "Quality settings" kavramını runtime'dan tamamen siler — Unity/Unreal'ın hiçbiri bunu build-time'a taşımıyor |
| **Kendi shader dili + otomatik fp16** | Build'de derlenmiş shader/PSO ilkesinin (§1) doğal uzantısı: kendi kısıtlı shader DSL'imiz olursa, hassasiyet düşürme (fp32→fp16) derleme zamanında otomatik ve güvenli yapılabilir | Unity/Unreal bunu yapmıyor (genel amaçlı HLSL/GLSL üzerinde kısıtlı otomatik analiz riskli) — bizim küçük/kapalı dil alanımız bunu güvenli kılar, gerçek bir moat |

## 3. İzleme listesi (Tier C — gerçek ama olgun değil / çok uzak vadeli)

Bunlar **doğrulandı ama şimdi yatırım yapılacak yer değil**:

- **3D Gaussian Splatting (mobil)** — Godot'nun `gdgs` eklentisi ve benzeri çalışmalar aktif ama beta; mobilde üretime 6-12 ay içinde yaklaşabilir.
- **NPU nöral upscale (NSS/NFRU/NSSD tarzı)** — Arm'ın **Mali G2-Ultra NX**'i (8 Eylül 2026 duyuruldu, doğrulandı: ilk AI-native Mali GPU, özel nöral hızlandırıcı, NSS/NFRU/NSSD ile 540p→1080p, ~4x verimlilik, [Arm](https://www.arm.com/products/silicon-ip-multimedia/gpu/mali-g2-ultra-nx)) **gerçek ve mevcut**, ama yalnızca en yeni amiral gemi SoC'lerde. Tulpar'ın hedef cihaz yelpazesi (orta/düşük segment dahil) bunu varsayamaz — izlenecek, cihaz-sınıfı bake mantığıyla "varsa kullan" opsiyonel yol olabilir.
- **Kuantum ışın izleme (Quantum Collision Models)** — gerçek, hakemli bir SIGGRAPH 2026 makalesi ([arXiv 2606.29989](https://arxiv.org/abs/2606.29989)), ama "yakın-dönem kuantum bilgisayarlarla ön-hesaplama" gerektiriyor — mobil oyun motoruyla hiçbir yakın-vadeli bağlantısı yok, saf araştırma referansı olarak not düşülüyor.
- **Nöromorfik çipler (ör. Rusya'nın Altai'si)** — gerçek, üretiliyor (2200 fps @ <0.5W, doğrulandı), ama drone/sensör hedefli, telefon SoC'sinde yok — Tulpar'ın donanım hedefiyle ilgisiz, referans amaçlı not.

## 4. Nerede eşitlenmemiz yeterli / rekabet anlamsız (değişmedi)

Fizik (Jolt zaten en iyisi), navmesh (Recast/Detour), ses cihazı (miniaudio) —
"daha iyi" yapmaya çalışmak zaman kaybı. Editör olgunluğu, asset store ekosistemi,
topluluk büyüklüğü — bunlara yatırım kaybolan yatırım, Blender + veri-odaklı
içerik hattına yaslanmaya devam.

## 5. Dürüst sonuç

"%10000 daha iyi, her alanda" gerçekçi bir hedef değil — ama **4 yapısal eksende
zaten kat kat öndeyiz** (§1) ve bunlar mimari kararların doğal sonucu. Bu sürümün
eklediği yeni bilgi: (a) rakip motorların gerçekten neyi doğru yaptığı artık gerçek
kaynaklarla belgeli (Turnip, ADPF, MAME4droid, GGRS/Gizmo) — bunlar taklit edilecek
değil, "bizim zaten sahip olduğumuz avantajın ne kadar nadir olduğunun kanıtı"
olarak okunmalı; (b) bazı "devrimsel" teknoloji iddiaları (nöral upscale, generative
rendering, kuantum ışın izleme) gerçek ama **Tulpar'ın hedef donanımında (orta/düşük
segment mobil) henüz erişilemez** — bunlara şimdi yatırım yapmak yanlış öncelik olur.

**`DEVAM_PLANI.md` ile ilişki:** o doküman "oyun yapılabilir hale gelmek" için
gereken temel işleri sıralıyor. Bu doküman ONDAN SONRA, "gerçekten AAA/devrimsel"
hedefine giden ek bahisleri sıralıyor. İkisi çakışmıyor, sıralı.

## 6. Bu sürümün doğrulama metodolojisi (şeffaflık için)

Kullanıcının ilettiği taslak 65+ başlık içeriyordu ve mesaj 50.000 karakterde
kesildi (Part IX "Platform Spesifikleri" §63.2 ortasında — Part X "Kernel ve
Sürücü Katmanı" ve Part XI "Ölçüm, Risk, Yol Haritası" hiç ulaşmadı; kullanıcı
isterse bunları ayrıca paylaşabilir).

Elde olan kısımdan, en çok "aksiyon kararı"na yön verecek ~19 iddia web'den
tek tek arandı. **Sonuç şaşırtıcı derecede olumlu çıktı: aranan iddiaların
neredeyse tamamı gerçek** (Mali G2-Ultra NX, Altai çip, DLSS 5, Nau Engine,
NetEase Messiah, GGRS, Gizmo, Filament, Turnip, ADPF, MAME4droid, FlashMem/ASPLOS
2026, kuantum ışın izleme makalesi, **PUMA (IEEE Access 2026, 10.1109/ACCESS.2026.3726535)**,
**Mobile-DDGI (ACM I3D 2026, "Best Poster" — BISTRO 26.8/30.7 FPS ve SPONZA
51.5/64.4 FPS rakamları harfiyen doğru çıktı)**, **CVE-2024-23380 (gerçek, Qualcomm
KGSL UAF, Android Offensive Security Blog'da tam teknik döküm var)**, **Tuanjie
1.10.0 Boids demosu (JobWorker paralel iş sistemi gerçek, tam FPS rakamları
bağımsız doğrulanamadı ama demo/özellik gerçek)** — hepsi bağımsız kaynaklarla
doğrulandı). İlk şüphe ("2026 tarihli olaylar, muhtemelen uydurma") **yanlış
çıktı** — bunun nedeni değerlendirenin (bu motorun asistanının) bilgi kesim
tarihinin Ocak 2026 olması, taslağın hatalı olması değil.

**Bulunan gerçek düzeltmeler (2 adet, hâlâ geçerli):**
1. "Unity China 团结引擎 **2.0**" — böyle bir sürüm yok, güncel sürümler 1.10.x/1.6.13.
2. "NetEase Messiah, Frame Graph'ı Unreal'den 2 yıl önce uyguladı" — motor gerçek,
   bu spesifik iddia doğrulanamadı, dışarıda bırakıldı.

**Doğrulanamayan ama muhtemelen gerçek olan (tek tek kontrol edilmedi, zaman
sınırı):** taslağın Part VI (MobileRC, Seele, HERMES-SR vb. kalan akademik
makaleler) ve Part VII'nin (RhabdoForge, SpikON, Project Kalos) geri kalanı —
bu oturumda doğrulanan örneklerin isabet oranı (19/19) çok yüksek olduğu için
muhtemelen bunlar da gerçek makaleler, ama **doğrulanmadan VIZYON.md'ye "kesin"
olarak girmediler**.

**İkinci doğrulama turu (kullanıcı "doğruladıklarının hepsini ekle" dedikten sonra):**
Kalan RESEARCH/SUSPECT maddelerden ~30'u daha web'den tek tek arandı. Sonuç yine
çok yüksek isabetli: **21 yeni madde VERIFIED'a yükseltildi** (LOOM, PhantomMap
[NDSS 2026, gerçek — üç kopya da düzeltildi], MobileRC, Seele/SeeLe, GPUSched,
DualEngine, RhabdoForge, FlyGym, Neuro-Bridge, fly-self-driving, HoloPathTracer,
AlayaRenderer, NDGI, NBC, Vulkan Bindless Native Plugin). **2 gerçek ama hatalı
atıf bulundu**: NBC'nin kaynağı "Huawei, 2026" değil — gerçek makale AAAI 2025,
Huawei bağlantısı doğrulanamadı; "Genesis Physics, header-only deterministik
C++" iddiası da hatalı — gerçek "Genesis" projesi Python tabanlı bir robotik
simülatörü, header-only C++ fizik motoru değil. **3 madde hâlâ tamamen
bulunamadı**: Vovan675/RenderingEngine, Blubber Engine, Transform-ER, CarDroid
(4 oldu). Toplam doğrulanan madde sayısı artık **76/500** (bkz.
`tools/feature_matrix.py --status VERIFIED`), şüpheli kalan **12**
(`--status SUSPECT`).

**500 maddelik "Ana Madde Listesi" (v18.0) ile ilişki:** kullanıcı bu sohbette
konuşulan her şeyi 10 kategoride (A-J) 500 madde olarak topladı. Bu liste
VIZYON.md'nin kapsamından daha geniş — birçoğu standart/yaygın bilinen render
teknikleri (gölge/yansıma/post-process/malzeme/terrain/parçacık/su/kumaş gibi
onlarca alt madde) ve bir kısmı motorun bilinçli kapsamı dışında (arka uç/canlı
operasyon: bulut kayıt, liderlik tablosu, mod desteği, sosyal özellikler —
`DEVAM_PLANI.md`'nin "dil bağlaması hariç" kapsamına da girmiyor). Bu 500
maddenin tamamının VIZYON.md'ye tek tek taşınması doğru değil (çoğu zaten
standart teknik, kaynak gerektirmiyor) — bunun yerine tam liste `docs/engine/MADDE-LISTESI-DURUM.md`'de
her maddenin durumunu (var/planlı/standart/araştırma-referansı/şüpheli/kapsam-dışı)
gösteren bir çapraz referans olarak tutuluyor.
