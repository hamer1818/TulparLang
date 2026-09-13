# Hedef Cihaz Matrisi ve İlk Oyun Tanımı

> Planın "İlk Üç Karar"ından ilki (PLAN.md §10). Her bütçe rakamı (§4, §5) buna göre kalibre edilir;
> bu belge dolmadan §4/§5'teki sayılar **tahmindir, bütçe değil**. Son güncelleme: 2026-09-14.

## 1. İlk oyunun tanımı — ✅ karar 2026-09-14

> Stüdyo kararı bana bırakıldı; seçim mevcut birikimden türetildi: scene3d'nin arena / karakter /
> labirent örnekleri, MTV çarpışma, engel-farkındalı yörünge kamerası ve dokunmatik kontrol bilgisi
> zaten var. Motor bu dilimin ihtiyacı kadar büyür (PLAN.md Faz 7).

**Tek cümle:** *Stilize düşük poligonlu, üçüncü şahıs yörünge kameralı, oda/arena ölçekli bölümlerden
oluşan, fizik tabanlı savaş ve hareket mekanikli tek oyunculu 3B aksiyon oyunu; 5–15 dakikalık
oturumlar, asenkron liderlik tablosu, orta segmentte 60 fps, düşük segmentte 30 fps.*

| alan | değer | motora sonucu |
|---|---|---|
| Tür | 3B aksiyon, arena/oda bölümleri (labirent ve devriye örüntüleri dahil) | Sahne akışı yok: bölüm = resident set (§8/5 uyumlu) |
| Kamera | Yörünge (orbit), engel farkındalı (röntgen → yükselt → yakınlaştır, Scene3D notu); orta hızda savruluş | Cull frustum payı orta; birinci şahıs savruluşu yok |
| Sanat yönü | Stilize düşük poligon, düz/az dokulu yüzeyler, güçlü siluet; PBR **yok** (basit BRDF + bake GI) | Vis buffer A/B'sinde bant genişliği kazancı küçük beklenir; forward+ varsayılanı güçlenir. Kurulum boyutu küçük (EK G.3) |
| Sahne ölçeği | Oda/arena: 30×30 m'ye kadar, 2–4 bölüm/bölüm grubu | VT düşük öncelik; VSM yerine CSM yeter (Faz 9 ertelenebilir) |
| Entity yoğunluğu | 20–40 hareketli gövde (oyuncu, düşman, mermi, parçalanan nesne), 200–500 statik, 8–16 dinamik ışık | Jolt + navmesh + GPU particle şart; clustered forward+ orta, stochastic tile düşük segment |
| Mekanikler | Fizik tabanlı vuruş/itme/fırlatma, zıplama/koşu/atılma, devriye ve kovalama AI, tetikleyici bölgeler, bölüm ilerleme | Deterministik fixed-step + replay (Faz 2 kapısı) |
| Çok oyunculu | v1 yok; asenkron liderlik tablosu (mevcut Wings skor sunucusu). Rollback **ertelendi** | Aynı-mimari determinizm yeter; sabit noktalı sim gerekmez |
| Oturum süresi | 5–15 dk | 10 dk termal pencere zorunlu (EK G.4) |
| Girdi | Dokunmatik joystick + eylem düğmeleri (arcade D-pad birikimi), gamepad opsiyonel | Timestamp'li callback input (Faz 1) |
| Hedef fps | Orta 60 (referans), düşük 30 (cihaz sınıfı bake: gölge/parçacık/çözünürlük), yüksek 60 + tam çözünürlük | §4 bütçesi orta segment 60 fps'e göre |
| Platform sırası | Android önce, iOS Faz 10 | — |

**Neden bu ve başka bir şey değil:** (1) mevcut motorun ulaşamadığı şey tam olarak bu tür (fizik, animasyon, AI, çok ışık); (2) açık dünya/sürekli akış istemez, planın "sahne düzeyinde streaming yok" kararıyla çelişmez; (3) stilize sanat düşük segmente ulaşır ve 200 MB sınırında rahat kalır; (4) yörünge kamera, çarpışma ve bölüm sistemi bilgisi scene3d'den taşınır; (5) tek oyunculu olması netcode'u v1'in dışında tutar ama deterministik sim replay/perf-CI için yine kurulur.

## 2. Cihaz sınıfları

Üç sınıf, üç fiziksel cihaz. Emülatör (x86_64, TBDR değil) yalnız işlevsel test içindir; hiçbir performans kapısı emülatörde geçmez.

| sınıf | ölçüt | aday SoC (doğrulanacak) | GPU ailesi | elde |
|---|---|---|---|---|
| **Düşük** | 3–4 GB RAM, AVP 2025 alt kademe, ~2021–2023 giriş segmenti | Snapdragon 680 / 4 Gen 1, Helio G85–G99, Unisoc T616 | Adreno 610/619, Mali-G52/G57 | ⛔ yok |
| **Orta** (referans) | 6–8 GB RAM, AVP 2025 orta kademe | Snapdragon 7 Gen 1–3, Dimensity 7050–8300, Exynos 1380 | Adreno 644/720, Mali-G68/G610/G615 | ⛔ yok |
| **Yüksek** | 8–12 GB RAM, AVP 2025 üst kademe, Vulkan 1.3 | Snapdragon 8 Gen 2/3, Dimensity 9300, Tensor G3 | Adreno 740/750, Immortalis-G715/G720 | ⛔ yok |

Kurallar:
- **Bir Mali ve bir Adreno şart** (tile boyutu, subpass merge davranışı, wave genişliği farklı; PLAN.md Faz 2 / EK F.1).
- Düşük sınıf cihaz **eldeki en yavaş** cihazdır; §4 kare bütçesi ona göre kurulur, yüksek sınıf sadece "daha iyi görünür" katmanı alır (cihaz sınıfı bake, EK A.2).
- Aday SoC'lerin Vulkan sürümü ve uzantı desteği **cihazda `vulkaninfo` ile doğrulanır**; bu tablo satın alma listesidir, garanti değil.
- Baseline: **AVP 2025** kademesi; hangi kademenin seçileceği Android Distribution Dashboard'daki kapsama yüzdesiyle karar verilir (hedef: ≥ %85 aktif cihaz). Sürüm numarası elle seçilmez (PLAN.md L2).

## 3. Ölçüm disiplini (EK G.4)

- Pencere **10–15 dakika** (termal throttle o aralıkta başlar), **3–5 koşu**, medyan raporlanır; ortalama değil, p99 (A7).
- Cihaz kontrollü durumda: ekran parlaklığı sabit, uçak modu, pil > %50, arka plan uygulamaları kapalı, soğumuş başlangıç.
- Her ölçüm satırı **cihaz kimliğiyle** kaydedilir (model, SoC, sürücü sürümü). "Bazen düşen" bir kapı gürültü sayılmadan önce cihaz kimliğiyle eşlenir — masaüstü CI'da bu ders EPYC 9V74 ile öğrenildi (Tuzaklar 1p).
- Masaüstü CI (Linux x86_64 + macOS arm64) yalnız **işlevsel** ve **mimari** (fiber geçişi iki mimaride) kapıdır; performans kapıları cihazda.

## 4. Bütçelerin bağımlılığı

| PLAN.md rakamı | neye bağlı | bu belgedeki kaynağı |
|---|---|---|
| §4 CPU 6 ms / GPU 12,5 ms | düşük sınıf cihazın sürdürülebilir saati | §2 düşük sınıf |
| §4 bant genişliği < 8 GB/s | düşük sınıf LPDDR tipi ve paylaşımı | §2 |
| §5 512 MB uygulama bütçesi | orta sınıf RAM ve OS baskısı | §2 orta |
| §5 kurulum boyutu 200 MB | Play sınırı; sabit | EK G.3 |
| Faz 5 "%55 render res" | yüksek sınıfta tam çözünürlük, düşükte %55 | §2 + cihaz sınıfı bake |

## 5. Açık işler

- [x] İlk oyun tanımı (§1) — 2026-09-14
- [ ] Üç cihazın temini (§2) — Faz 1 başlamadan
- [ ] AVP 2025 kademe seçimi, kapsama yüzdesiyle
- [ ] Cihaz farm'ı için adb üzerinden koşturucu (install_run.sh'ın perf modu) — Faz 1
