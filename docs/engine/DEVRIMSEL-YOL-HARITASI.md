# Tulpar Engine — Devrimsel Yol Haritası

> **Kapsam:** iki PR (#322 CagriKibar + #323 hamer1818) **birleştikten sonra** motorda
> ne olacağını dürüstçe sayar, sonra *hâlâ eksik* olanı önceliklendirir.
> **Kural:** temeller atlanmaz. "Devrimsel" olan, temelin **üzerine** gelir.
>
> **Yöntem:** bu belgedeki her iddia ya (a) iki PR'ın dosya listesinden, ya da
> (b) GitHub API'siyle varlığı + lisansı doğrulanmış gerçek bir depodan gelir.
> Doğrulanamayan hiçbir şey "var" diye yazılmadı.

---

## 0. Neden bu belge

Motorun satış argümanı tek cümlede: **"aynı girdi, her platformda bit-eş aynı sonuç."**

Bu iddia bugün *disiplinle* ayakta duruyor — strict FP, `-ffp-contract=off`, libm'den
kaçınma, altın özet karşılaştırması. Disiplin işe yarıyor ama **kırılgan**: bir derleyici
sürümü, bir `-ffast-math`, bir `std::sin` çağrısı iddiayı sessizce bozar.

Yol haritasının ana fikri: bu iddiayı **disiplinden yapıya** taşımak. Gerisi (nöral,
nöromorfik, GPU-driven) bunun üzerine kurulur — çünkü hepsi "simülasyon tekrar
üretilebilir mi?" sorusuna dayanıyor.

---

## 1. Birleşme sonrası envanter — **zaten var**

Bunları tekrar yazmak israf olur.

| Alan | Durum | Nerede |
|---|---|---|
| **ECS / DOD** | Archetype + chunk depolama, bitmask sorgu | `sim/ecs.*` (+ flecs) |
| **Sıfır tahsis** | `AllocGate` — kare içi ayırma sayılır, kapı testi var | `core/memory/` |
| **Job system** | Fiber tabanlı, bağımlılık grafiği | `core/jobs/` |
| **Determinizm (FP)** | 3 platform altın özet, strict FP | `sim/physics`, `sim/desync` |
| **Rollback / replay** | GGPO deseni, `World::snapshot/restore` | `sim/rollback`, `sim/replay` |
| **GPU-driven culling** | Gerçek compute shader (#323) + CPU portu (#322) | `cull.comp`, `indirect_cull` |
| **Render graph** | Derlenmiş geçiş grafiği (#323) + CPU bağımlılık grafiği (#322) | `renderer/graph`, `rhi/frame_graph` |
| **PBR + gölge + bloom** | ORM/normal/ışıma dokuları, kademeli gölge | #323 |
| **Sahne** | `.sahne` metin + `.sahneb` blob, editör (çoklu seçim, geri al) | #323 |
| **Dil köprüsü** | 169 builtin, bağlama katmanı üretiliyor | #323 `bridge/` |
| **Uzamsal yapı** | SAH BVH | #322 `core/math/bvh` |
| **Prosedürel** | WFC, değer gürültüsü, FBM | #322 |
| **Akışkan / IK / AI** | SPH, FABRIK, davranış ağacı | #322 `sim/` |
| **Mobil güç** | Termal, ADPF ipucu, cihaz sınıfı, dinamik kalite | `platform/`, `content/` |

**Sonuç:** listedeki 12 maddenin **8'i zaten var.** Asıl boşluk 4 tanede.

---

## 2. İkisinde de **YOK** — öncelik sırası

Aşağıdaki dört madde her iki PR'ın dosya listesinde de geçmiyor (grep ile doğrulandı).

### P0 (kritik) — Sabit noktalı (fixed-point) deterministik matematik

**Sorun:** determinizm bugün float'a dayanıyor. Float determinizmi *mümkün* ama şartlı:
aynı derleyici bayrakları, FMA kapalı, libm yok, aynı işlem sırası. Bir tanesi kayarsa
desenkron olur ve **sebebini bulmak günler sürer.**

**Çözüm:** Q-formatlı tamsayı aritmetiği. `a*b` bir tamsayı çarpımıdır — x86'da, ARM'da,
derleyici ne yaparsa yapsın **aynı bittir.** Determinizm *disiplin* olmaktan çıkıp
*tip sistemi* olur.

**Gerçek kaynaklar (varlığı + lisansı doğrulandı):**

| Depo | Lisans | Not |
|---|---|---|
| [MikeLankamp/fpm](https://github.com/MikeLankamp/fpm) | MIT, 840★ | C++ başlık-only, en olgunu |
| [mas-bandwidth/fixed](https://github.com/mas-bandwidth/fixed) | MIT | Q48.16. **Test felsefesi bizimkiyle birebir aynı:** her sonucu hash'leyip kaynağa donduruyor, tek bit değişirse her platformda test düşüyor — bizim "altın özet" disiplinimiz |
| [DragonJawad/ProjectNomad-Core](https://github.com/DragonJawad/ProjectNomad-Core) | MIT | Sabit noktalı 3B fizik |
| [JiepengTan/Lockstep.Math](https://github.com/JiepengTan/Lockstep.Math) | **lisans yok** | Kod kopyalanmamalı, yalnız fikir |

**Tulpar'a katkısı:** rollback/lockstep çok oyunculu **gerçekten** güvenli olur; replay
dosyaları yıllar sonra bile aynı sonucu verir; desenkron ayıklaması biter.

---

### P1 — Uzamsal hash ızgarası (broadphase)

**Sorun:** BVH var, ama BVH **statik/yarı-statik** geometri içindir. Her kare hareket eden
binlerce cisim için yeniden inşa maliyeti yüksek. Şu an geniş-faz yalnızca Jolt'un
*içinde* — motorun **kendi** sorgusu için yok: "şu yarıçaptaki tüm ajanlar", sürü
davranışı, tetikleyici hacimler, SPH komşu arama.

**Somut kazanç:** `sim/fluid_system.cpp`'deki SPH şu an **O(n²)** komşu araması yapıyor
(kodda dürüstçe yazılı). Izgarayla O(n·k) olur — 2000 parçacıkta **4.000.000 → ~60.000**
karşılaştırma.

**Gerçek kaynak:** Teschner ve ark. 2003, *"Optimized Spatial Hashing for Collision
Detection of Deformable Objects"* — SPH/çarpışma literatürünün standart referansı;
Jolt ve Bullet'in geniş-fazı da aynı fikrin türevi.

---

### P1 — SIMD / NEON açık vektörleştirme

**Sorun:** mobil ARM çipte NEON birimi var, motor kullanmıyor. Derleyicinin otomatik
vektörleştirmesi `-ffp-contract=off` ve strict FP altında çoğu yerde devreye girmiyor —
yani determinizm disiplini **performansı kısıtlıyor.**

**Kritik bağlantı:** sabit nokta (P0) geldiğinde bu sorun **kendiliğinden çözülür** —
tamsayı SIMD'inde yeniden ilişkilendirme (reassociation) sorunu yoktur, `vmlaq_s32` her
platformda aynı biti verir. Yani P0 → P1 sırası tesadüf değil: **P0, P1'i güvenli hale
getirir.**

---

### P2 — Nöromorfik / spiking sinir ağı (SNN)

**Ne:** nöronlar sürekli değer yerine *zaman içinde spike* üretir; hesaplama olay-güdümlü
(değişim yoksa sıfır enerji). Mobil/edge için enerji verimliliği klasik ağlardan yüksek.

**Gerçek kaynak (doğrulandı):** [H4V1K-dev/axicor](https://github.com/H4V1K-dev/axicor)
— Apache-2.0, 95★, *"Neuromorphic Computing Engine, SNN AI Architecture based on biology"*.
Backpropagation yok, statik graf yok; aksonlar 3B uzayda büyüyor, zayıf sinapslar budanıyor.

**Neden P2:** ilginç, ama oyun motoru için **henüz kanıtlanmış bir kazanç değil.** Mevcut
`sim/behavior_tree` + `core/math/nn` çoğu oyun AI'ı için yeterli. Buraya P0/P1 bitmeden
girmek "temeli atlamak" olur.

---

## 3. Bunlara **girmiyoruz** (ve nedeni)

Dürüst olmak, uzun liste yapmaktan önemli.

| Konu | Neden hayır |
|---|---|
| **Gerçek kuantum hesaplama** | Mobilde kuantum donanımı **yok.** Daha önce eklenen `qpp` bağımlılığı bir kuantum kapısı uygulayıp **sonucu atıyordu** — sıfır fayda, MB'larca boyut; kaldırıldı. Kuantum-*ilhamlı* olan WFC ise zaten yazıldı ve çalışıyor. |
| **Nöral render (NSSD/NFRU)** | Arm Mali'nin nöral hızlandırıcıları gerçek, ama **o donanıma sahip cihaz sayısı bugün sıfıra yakın.** Motorun hedefi "her cihazda çalışsın". Donanım yaygınlaşınca yeniden bakılır. |
| **ncnn / FastNoiseSIMD** | İkisi de çalışma zamanında CPU'ya göre farklı SIMD yolu seçer → **bit-eş determinizmi çiğner.** Motorun ana iddiasıyla doğrudan çelişir. |

---

## 4. Ölçüm olmadan iddia yok

Bu yol haritasının her maddesi şu kapıyı geçmek zorunda (`DURUM.md` §7 kuralı):

1. **Sayı** — "daha hızlı" değil, "2000 parçacıkta 4.0M → 60K karşılaştırma".
2. **Kapının kontrolü** — testi bozan bir değişiklik testi *gerçekten* düşürmeli.
3. **Üç platform** — x86_64 + arm64 + gerçek telefon.

> **UYARI:** bu belgenin yazıldığı ortamda C++ derleyicisi yok. Buradaki hiçbir performans
> sayısı ölçülmüş değildir; hepsi **hedef**tir. Ölçüm, derleyicili ortamda yapılana kadar
> hiçbir sayı `DURUM.md`'ye geçmemelidir.

---

## 5. İncelenen diğer motorlar (varlığı doğrulandı)

Fikir kaynağı olarak bakıldı; kod kopyalanmadı.

| Depo | Lisans / Yıldız | Öne çıkan |
|---|---|---|
| [ejoy/ant](https://github.com/ejoy/ant) | MIT, 3.9k★ | LuaECS — performans-kritik veri C struct'ında |
| [galacean/engine](https://github.com/galacean/engine) | MIT, 5.9k★ | Mobil odaklı, Alibaba |
| [cocos/cocos-engine](https://github.com/cocos/cocos-engine) | 9.8k★ | Mobil GPU'ya göre tasarlanmış boru hattı |
| [KaijuEngine/kaiju](https://github.com/KaijuEngine/kaiju) | 4.7k★ | Go + Vulkan |
| [skylicht-lab/skylicht-engine](https://github.com/skylicht-lab/skylicht-engine) | MIT, 771★ | C++ mobil odaklı, Irrlicht tabanlı |
| [H4V1K-dev/axicor](https://github.com/H4V1K-dev/axicor) | Apache-2.0, 95★ | Nöromorfik SNN motoru |

---

## 6. Sıradaki somut adım

**P0: `core/math/fixed.hpp`** — Q32.32 sabit noktalı tip + temel fonksiyonlar, `fpm` ve
`mas-bandwidth/fixed` yaklaşımı referans alınarak, Tulpar'ın kendi tipleriyle.

Neden önce bu:

- **Yeni dosya** → iki PR'la da çakışmaz, birleştirmeyi zorlaştırmaz.
- **Tamamen tamsayı** → derleyicisiz ortamda bile elle doğrulanabilir.
- Motorun **bir numaralı iddiasını** disiplinden yapıya taşır.

---

## 7. Dış belgelerden süzülenler (20 katmanlık iki doküman)

Kullanıcı iki büyük "katmanlı bilimsel mimari" belgesi getirdi (Katman 0-10 ve
11-20). İkisi de aynı desende: **gerçek bilim + çalışmayan kod + yanlış yere
uygulama.** Aşağıda süzme sonucu.

### 7.1 Neden belgelere olduğu gibi güvenilmedi

Kod örneklerinde **15+ somut, doğrulanabilir hata** bulundu. Kaynak
güvenilirliği tartışması değil — dosyayı okuyan herkesin görebileceği türden:

- "Nöral denoise" GLSL'i aslında **sabit ağırlıklı kutu bulanıklığı** (ağ yok).
- `PredictiveCodingAgent.act`: `predict(action)` parametresini hiç kullanmıyor →
  tüm eylemler aynı enerjiyi veriyor → `argmin` hep 0. **Ajan karar veremez.**
- Kuantum tünelleme kodu, metnin kendi formülündeki bariyer kalınlığı `L`'yi
  ve `ħ`'yi düşürmüş.
- `FibonacciAnyon`: `4j * Math.PI` — Python sözdizimi, JS'te parse edilmez.
- `ORSet.remove` yalnız kendi etiketini siliyor → **CRDT semantiği bozuk.**
- Şifreli (CKKS) skorlar `<` ile karşılaştırılıyor — **mümkün değil.**
- `ThermalBudget`: hesaplanan `min_sigma` hiç kullanılmıyor, `self.kB` tanımsız.
- `lyapunov_exponent`: ardışık noktaların mesafesini alıyor; Lyapunov üssü
  *yakın iki ayrı yörüngenin* ıraksamasıdır.
- Müzik `synthesize`: akorlar aynı `t` üzerinde → progresyon değil küme akoru.

### 7.2 Belgelerin yapısal iddiası YANLIS

Her iki belge de "her katman, altındakinin matematiksel genellemesidir" diyor.
**Değil.** ECS, kuantum durum uzayının özel hali değil; SPH, tensör ağlarının
özel hali değil. Bunlar matematiksel ilişki değil, retorik benzetme.

Ayrıca belgeler kendi içlerinde çelişiyor: **Kaos (Katman 8) ile determinizm
(Katman 0.7) yan yana konmuş.** Kaos = başlangıç koşullarına aşırı duyarlılık;
determinizm = aynı girdi, aynı çıktı. Lockstep netcode ikincisi üzerine kurulu.

### 7.3 SÜZÜLENLER — gerçekten alınacaklar

Hiçbiri iki PR'da da yok (grep ile doğrulandı).

| # | Ne | Neden uyuyor | Kaynak katman |
|---|---|---|---|
| 1 | **Uzamsal hash ızgarası** | Aşağıdaki 1, 2, 5'in **ortak önkoşulu**; SPH'i O(n²)→O(n·k) yapar | P1 (bu belge §2) |
| 2 | **Boids (sürü davranışı)** | Ucuz, standart, kalabalık/düşman sürüsü. Komşu sorgusu gerektirir | 15 |
| 3 | **Kum yığını / SOC** | **Tamsayı ızgarası → tam deterministik.** Güç yasası dağılımlı emergent olaylar | 16.2 |
| 4 | **Reaksiyon-difüzyon** | Turing desenleri; prosedürel doku. Ucuz | 10.3 |
| 5 | **Lotka-Volterra + SIR** | Birkaç ODE; ekosistem, salgın, kaynak tükenmesi | 19.2 |
| 6 | **Neo-Riemannian müzik** | **Modüler tamsayı aritmetiği → deterministik.** Motorların çoğunda müzik teorisi HİÇ yok | 20.1 |
| 7 | **TD-hatası = NPC motivasyonu** | Dopamin ≡ ödül tahmin hatası (Schultz 1997). Ucuz, yorumlanabilir | 18.2 |

**İzleme listesi (kod değil, takip):**
- **NVIDIA MotionBricks** — gerçek (SIGGRAPH 2026, doğrulandı: 350k klip, 9.300
  skill, 15.000 FPS, 2 ms). Motorun en büyük eksiklerinden biri olan
  animasyon geçişi/blend sorununa doğrudan bakıyor.

**Çevrimdışı araç olarak değerli (runtime değil):** optimal transport (stil/renk
transferi), kalıcı homoloji (bölüm topolojisi doğrulama), ağ analizi (sosyal graf).

### 7.4 ALINMAYANLAR ve nedeni

| Konu | Neden hayır |
|---|---|
| **Monad / Functor / Topos** (11) | Monadik soyutlama veri-odaklı tasarımın **tersi**: işaretçi takibi + tahsis. `AllocGate` tam da bunu engellemek için var. |
| **Doğal gradyan / Fisher** (12) | Fisher matrisi O(n²), tersi O(n³), üstelik adım başına 1000 örnekleme. Mobilde imkânsız. |
| **QFT / anyonlar** (13) | Klein-Gordon kodu aslında klasik 3B dalga denklemi — görsel efekt olarak kullanılabilir ama "QFT" değil. Anyon kodu derlenmiyor. |
| **PBFT / blok zinciri** (14.2) | O(n²) mesajlaşma. Mobil oyun için fahiş. |
| **PSO / ACO** (15) | PSO bir *optimizasyon* algoritması; oyuncunun yerini zaten biliyoruz. ACO, navmesh+A*'tan yavaş. |
| **ZKP / FHE** (17) | Mobilde ispat üretimi saniyeler-dakikalar; FHE ~1000x yavaşlama. |
| **FBA / DFT** (19.1, 7) | Karakter başına kare başına lineer program / molekül başına saatler. Oyunlar malzeme parametrelerini **yazar**. |
| **Kuantum donanımı, metafizik** (3, 4) | Telefonda QPU yok; "déjà vu → bellek sızıntısı" test edilebilir bir mühendislik iddiası değil. |
