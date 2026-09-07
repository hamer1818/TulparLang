---
tags: [moc, performance, benchmark]
---

# Performance & Benchmarks

Araçlar `benchmarks/`: `loadtest.c` (çok-thread keep-alive/close HTTP yük üreteci, 1µs histogram), `run_stress.sh`, `stress_server.tpr`, `stress_db_server.tpr`. Detay: `benchmarks/WINGS_STRESS.md`, `WINGS_VS_FASTAPI.md`.

## Dil kıyaslaması — `benchmarks/fair/` (2026-09-03)

Adil kural: **her dil** `BENCH_N`'i ortamdan okur (yoksa `gcc -O2`/`rustc -O3`
kapalı forma katlıyor ve boş programı ölçmüş oluyorsun — [[Tuzaklar#1]]), aynı
algoritma, aynı veri yapısı, çıktı doğrulanıyor. En iyi/medyan + boş program
taban çizgisi. Çalıştır: `python3 benchmarks/fair/run.py [test]`.

| Test | C | Rust | Go | **Tulpar** | Java | Node | Sıra |
|---|--:|--:|--:|--:|--:|--:|:--:|
| intloop (50M) | 134.6 | 144.5 | 135.6 | **135.6** | 147.7 | 715.2 | **2.–3.** |
| fib(32) | 1.6 | 3.8 | 6.7 | **4.3** | 13.4 | 27.7 | **3.** |
| sieve (5M) | 7.7 | 8.2 | 8.5 | **~9.9** | 21.2 | 29.3 | 4. |
| strcat (2M) | 37.8 | 19.1 | 25.4 | **31.6** | 35.9 | 105.6 | **3.** |
| arrayiter (5M) | 2.3 | 1.7 | 4.0 | **6.6** | 19.5 | 21.3 | 4. |

Hedef "her alanda 2.–3. sıra" 5 testin 3'ünde tutuyor.

`arrayiter` sonradan eklendi (2026-09-04): önceki dördü Tulpar'ın **en yaygın
döngü kalıbını** hiç ölçmüyordu — `for (int i = 0; i < len(a); ...)`. O kalıp
şekil önbelleği + `len` katlamasıyla 2.4× hızlandı ama tabloda görünmüyordu.
Her dil kendi deyimsel uzunluk erişimini kullanıyor.

### Dizi/bellek (sieve) — 19.9 → 13.3 ms nasıl geldi
İki yapısal değişiklik, ikisi de [[Memory Model]]'de ayrıntılı:

1. **Kutulanmamış int depolama.** `ObjArray` artık ya `items_` (kutulu
   `VMValue`, 16 bayt/eleman) ya da `idata` (ham `int64`, 8 bayt/eleman)
   tutuyor. `array_fill(n, <int>)` doğrudan kutulanmamış üretiyor. Int olmayan
   her şey `arr_items()` üzerinden tek seferde kutuya dönüyor (`arr_debox`).
2. **TBAA.** Eleman deposu ile ObjArray başlığı/değişken yuvaları ayrı
   takma-ad sınıfı. Bu olmadan LLVM `a[k]=1` yazmasının başlığı ezebileceğini
   varsayıp diziyi, etiketini, `count`'unu ve `idata`'sını **her yinelemede**
   yeniden okuyordu. Etiketlemeden sonra bunlar döngü dışına çıktı.

3. **Döngü-değişmezi dizi şekli önbelleği** (`src/aot/llvm_array_shape.cpp`).
   Döngü başında dizinin `idata` + `count`'u **bir kez** okunup yerelde
   tutuluyor; gövdede yalnız sınır karşılaştırması kalıyor — Go'nun dilim
   uzunluğunu yazmaçta tutmasıyla aynı fikir. 13.3 → ~10 ms.

### Önce ölç, sonra yaz — bu işin karar anı
Makine kurmadan önce **tavan ölçüldü**: codegen'e geçici bir "hiçbir denetim
yok" yolu konup elek çalıştırıldı. 12.9 → 9.1 ms. Sonra denetimler tek tek
geri açılarak maliyet ayrıştırıldı:

| yapılandırma | ms | ek |
|---|--:|--:|
| hiçbir denetim | 9.1 | — |
| + sınır denetimi | 11.5 | **+2.4** |
| + tür denetimi | 12.4 | +0.9 |
| + idata denetimi | 12.9 | +0.5 |

Bu tablo tasarımı belirledi: en pahalısı sınır denetimiydi **ama** pahalı olan
denetimin kendisi değil, `count`'un her yinelemede **bellekten** okunmasıydı.
Onu yazmaca almak = şekil önbelleği. Ölçmeden başlansaydı muhtemelen i32'ye
daraltmaya girilecekti — oysa oran N ile küçülüyordu, yani darboğaz bant
genişliği değildi. **Yanlış işe girilmesini ölçüm engelledi.**

### Denendi ve ÇÜRÜDÜ (tekrar denemeyin)
İkisi de makul görünüyordu, ikisi de **iç içe ölçümde yavaşlattı**:

| deneme | beklenti | ölçüm |
|---|---|---|
| Üst düzey küreselleri `internal` linkage yapmak | LLVM yazmaca alır | 9.6 → **11.0 ms** |
| Salt-okunur int küreselleri döngü başında yerele kopyalamak | 2 yükleme eksilir | 9.2 → **10.2 ms** |

İkincisi özellikle şaşırtıcıydı: elekte `i` ve `n` gerçekten her turda bellekten
okunuyor ve o döngüde yazılmıyorlar. Yerel kopya çıkarmak yine de yavaşlattı
(muhtemelen LLVM'in kendi analizine karışıyor). **Kod doğruydu, testler yeşildi
— sadece daha yavaştı.** Ölçmeseydik "iyileştirme" diye girecekti.

Küresellerin ham adla yazılması ayrı bir **hata** olarak çıktı ve önekle
çözüldü (bkz. [[Tuzaklar#6h]]) — linkage değiştirmeden, bedelsiz.

**Kalan fark neden var (Tulpar ~10 / Go 8.5):**
- Tulpar'ın `int`'i 64 bit; C/Go/Java bu testte 4 baytlık eleman kullanıyor →
  aynı algoritmada **2× bellek trafiği**. Bu bir semantik farkı, gerileme değil.
- Şekil önbelleği yalnız **kanıtlanabilen** döngülerde açılıyor: gövdede
  beyaz listede olmayan bir çağrı varsa vazgeçiliyor (push/pop şekli
  değiştirir). Yani kullanıcı fonksiyonu çağıran sıcak döngüler hâlâ tam
  denetim ödüyor.
- Bir sonraki adım: `for-in` şeker açılımını da kapsamak, ve i32'ye daraltma
  (V8'in SMI dizileri gibi) — ama i32 ölçüme göre küçük bir kazanç.

### Kanıtın genişletilmesi (2026-09-04)
1. **`for` döngüleri.** Önbellek yalnız `while`a bağlıydı. `for`a da bağlandı;
   **artım ifadesi de kanıta dahil** (`for (...; ...; a.push(i))` şekli
   değiştirebilir). Ölçüldü: 7.5 → 5.1 ms.
2. **Saf yerleşikler.** Her çağrı kanıtı düşürüyordu, dolayısıyla
   `for (int i = 0; i < len(a); ...)` hiç yararlanamıyordu. Elle doğrulanmış
   kısa bir liste (`len`/`length`/`abs`/`min`/`max`/`sqrt`/`pow`/`floor`/
   `ceil`/`round`/`toInt`/`toFloat`/`ord`/`chr`) artık kanıtı düşürmüyor.
   Kararı **codegen** veriyor: kullanıcı aynı adı tanımladıysa yerleşik
   sayılmıyor. Listeye ekleme ölçütü "saf mı" DEĞİL — "bir dizinin
   count/items_/idata alanını değiştirebilir mi" ve "kullanıcı koduna geri
   dönebilir mi" (`call`, `map`, karşılaştırıcılı `sort` bu yüzden yok).
3. **`len` önbellekten.** Kanıt zaten uzunluğun sabit olduğunu söylüyor, o
   hâlde çağrıya gerek yok. Önbellekte **ayrı** bir gerçek-uzunluk yuvası var:
   `count_slot` yerine geçemez, çünkü o kutulanmamış olmayan dizide bilerek 0
   tutuyor. Dizi olmayan değer için −1 yazılıp eski çağrıya düşülüyor, yani
   `len(dizgi)` / `len(json)` birebir korunuyor. Ölçüldü: 14.2 → 5.9 ms.

**Yük taşıyan varsayım:** ayırma yapan yerleşikler önbelleği bozmuyor, çünkü
arena **öbek-zincirli bump ayırıcı** — `aot_arena_alloc` yeni bloğu zincire
ekliyor, `realloc` yok, bloklar asla taşınmıyor. Arena tek büyük blok olsaydı
bu liste güvensiz olurdu.

Ölçüm hijyeni: oran N ile **küçülüyor** (N=200k'da Go'nun 2.36×'i, N=5M'de
1.55×'i) — yani darboğaz saf bant genişliği değil, erişim başına sabit hesap.
Tek bir N'de ölçüp "bellek bağlı" demek yanıltıcı olurdu.

### Üç hipotez daha çürüdü (2026-09-05)

Hepsi makuldü, hiçbiri işe yaramadı. Yazıyorum ki bir daha girilmesin.

| deneme | beklenti | ölçüm |
|---|---|---|
| `count` yerine ham count + ayrı "kutusuz" bayrağı (`ok && i < count`) | bayrak döngü-değişmezi → LLVM **unswitch** eder, kalan `i < count` döngü sınırıyla özdeş olduğu için silinir | arrayiter 5,55 → **8,13 ms** |
| Sınır denetimini elemek | yinelemede 2 komut eksilir | **hiç kazanç yok** (aşağıda) |
| Üst düzey değişkenleri fonksiyona taşımak (küresel → yerel) | `i`/`n` yazmaca girer, 2 bellek okuması eksilir | sieve 11,85 → **13,36 ms** |

**Unswitch neden olmadı:** yavaş yolda `emit_shape_refresh_all` var ve o
şekil yuvalarına **yazıyor** — yani `ok` döngü içinde yazılan bir yuvadan
okunuyor, döngü-değişmezi *değil*. Unswitch yapısal olarak imkânsız.
Geriye yalnız yinelemede fazladan bir `and` + dal kaldı.

**Sınır denetiminin bedava olduğu nasıl ölçüldü:** aynı döngü C'de iki
biçimde yazıldı — denetimsiz ve `if (i < cnt) ... else abort()` ile.
İkisi de **2,0 ms**. Dal mükemmel tahmin ediliyor ve sıra-dışı yürütme
gizliyor. Aynısı elek iç döngüsü için de yapıldı (küresellerden okuma +
fazladan denetim dahil): 10,8 vs 10,2 ms — Tulpar'ın "fazla" komutları
**ölçülebilir bir bedel değil**.

### Kalan farkın gerçek kaynağı: ELEMAN GENİŞLİĞİ (ölçüldü)

Elekte `sieve.c` `int` (4 bayt), Tulpar `int[]` 64 bit. Aynı programı C'de
`long long` ile derleyip ölçtük:

| | süre |
|---|---|
| C, 32-bit eleman | 9,51 ms |
| C, **64-bit** eleman | 10,33 ms |
| Tulpar (64-bit) | 10,99 ms |

Yani **eleman genişliği eşitlenince Tulpar C'nin ~%7 gerisinde.** Görünen
1,7 ms'lik farkın yaklaşık yarısı codegen değil, dilin `int`inin 64 bit
olması. Bu bir tasarım kararının bedeli, gerileme değil.

**Buradan çıkan tek büyük kaldıraç i32 dizi gösterimi** — ama üçüncü bir
depo biçimi demek (`items_` / `idata` / `idata32`) ve bugün tam bu sınıfta
sarkan-işaretçi hatası çıktı ([[Tuzaklar#6l]]). Ölçüm kazancı ~0,8 ms;
risk yüksek. Girilecekse ayrı ve dikkatli bir tur olmalı.

### İşe yarayan: sıfır dolgusunda `calloc` (2026-09-05)

`array_fill(n, 0)` 40 MB'lik bir yazma geçişi yapıyordu. `calloc` büyük
istekte mmap'e gidiyor ve çekirdek sayfaları zaten sıfır veriyor.

| | önce | sonra |
|---|---|---|
| `array_fill` (izole, n=5M) | 1,93 ms | **1,02 ms** |
| sieve (A/B, aralıklı, 12 tekrar) | 9,81 ms | **9,14 ms** |
| arrayiter (aynı) | 6,25 ms | **5,33 ms** |

Arena yolu hariç: arena bloğu geri dönüştürülmüş ve **kirli** olabilir.

**Ölçüm notu:** koşucunun kendi rakamında (diller ardışık, aralıklı değil)
elek farkı gürültüye giriyor — 9,3 → 9,4, yani orada **görünmüyor**.
Aralıklı A/B görüyor. İkisini de yazıyorum; birini seçip diğerini saklamak
[[Tuzaklar#6f]]'nin tam tersi hata olurdu.

### Asıl bulgu: bedel DAL değil, DÖNGÜ GÖVDESİNDEKİ KOD (2026-09-05)

Yukarıdaki "sınır denetimi bedava" ölçümü doğruydu ama **yanlış soruyu**
cevaplıyordu. Codegen'in içine geçici tavan yolları koyunca gerçek resim
çıktı (arrayiter, n=5M):

| yapılandırma | ms |
|---|--:|
| normal | 5,53 |
| **A**: bütün bekçiler kaldırıldı | 3,51 |
| **B**: dal DURUYOR, yavaş yol ölü (`unreachable`) | **3,42** |
| **C**: yalnız satır içi tazeleme çıkarıldı | 4,27 |

**B ile A'nın aynı çıkması belirleyici.** Dal duruyor ama kazanç aynı →
maliyet dalın kendisi değil, **yanındaki kod**. `emit_shape_fill` 4 temel
blok + ~25 komut üretiyordu ve bunu her yavaş yolda, yani **döngü
gövdesinin içinde** yapıyordu; o boyut LLVM'in sıcak yolu açmasını
engelliyordu. Tek başına 1,26 ms.

**Genel ders:** bir sıcak döngüde soğuk yolun *çalışma* maliyeti sıfır
olabilir ama *varlığı* bedava değil. "Bu dal hiç alınmıyor, önemsiz"
demeden önce gövdenin büyüklüğüne bak.

#### İki ara adım, ikisi de ölçümle elendi
1. **Runtime'da dışsal çağrı** (`aot_shape_refill`): arrayiter 6,51 →
   5,66 **ama** sieve 9,64 → **10,68**. Küçük bir döngüde her dışsal
   çağrı LLVM'in gözünde bütün belleği kirletiyor; kazandığından çoğunu
   geri veriyor.
2. **Tek modül-yerel fonksiyon**: arrayiter 6,77 → 4,81, sieve yine
   9,07 → **9,60**. Sebebi ince: gövdedeki `aot_len` çağrısı (dışsal)
   **bütün fonksiyonun** çıkarılan bellek etkilerini zehirliyor, yani
   `len` kullanmayan sıcak döngüler de bedelini ödüyor.

**Çözüm: iki varyant.** `len` kullanmayan döngüler için gövdesinde hiç
dışsal çağrı olmayan temiz sürüm, `len` için `aot_len` çağıran sürüm.
LLVM temiz sürümün etkilerini kendi çıkarıyor ve nerede açacağına kendi
karar veriyor.

| | önce | sonra | tavan |
|---|--:|--:|--:|
| arrayiter (A/B, aralıklı, 12 tekrar) | 6,02 | **4,78** | 3,43 |
| sieve (aynı) | 9,77 | **9,28** | 8,52 |

**Doğruluk riski ve nasıl kapatıldı:** tazeleme mantığının **ikinci bir
kopyası** oluştu (`emit_shape_fill` ve `get_shape_refill_fn`); ikisi
ayrışırsa önbellek sarkan işaretçi tutar ([[Tuzaklar#6l]]). Enjeksiyonla
sınandı: modül-yerel sürümdeki `select(ok, count, 0)` kaldırılınca
`element_step` çöküyor ve 4 sonda kırmızıya dönüyor.

#### i32'ye girmemenin gerekçesi (ölçüldü)
Elemanı daraltmanın kazancı **teste göre değişiyor**, tek bir sayı değil:

| | 32-bit | 64-bit | fark |
|---|--:|--:|--:|
| sieve (C) | 9,51 | 10,33 | **0,82 ms** |
| arrayiter (C) | 2,12 | 2,28 | 0,16 ms |

Elekte rastgele erişim var (önbellek/TLB baskısı) → genişlik önemli;
arrayiter sıralı akış → donanım öngetiricisi hallediyor, genişlik neredeyse
bedava. Yani i32 **elekte** ~0,8 ms değerinde, arrayiter'de değil — ve
üçüncü bir depo biçimi demek. Bu tur yerine tazeleme dışarı alındı: aynı
büyüklükte kazanç, sıfır yeni değişmez.

### `strcat` 31,3 → 19,0 ms — söz verilen 3. sıra (2026-09-05)

Önce ayrıştırıldı (2M ekleme):

| adım | ek |
|---|--:|
| boş döngü | 0,96 ms |
| + 2M `sb_append(sb, ",")` | +4,97 |
| + 2M `sb_append(sb, i % 1000)` | **+17,09** |
| + `count(s, ",")` | **+8,20** |

**1. `count()` — uyarlanabilir oldu.** Tek karakterlik ayraçta `memchr`
her **isabette** yeniden çağrılıyordu, yani maliyet isabet *sayısıyla*
orantılıydı. Yoğun ayraçta felaket, seyrekte mükemmel:

| ayraç aralığı | memchr | sayma döngüsü |
|---|--:|--:|
| her 4 baytta | 10,20 ms | **1,89** |
| her 16 baytta | 2,57 | **1,88** |
| her 64 baytta | **0,64** | 1,89 |
| her 1 MB'de | **0,06** | 1,88 |

Yani döngüye körü körüne geçmek seyrek aramada **30 kat** gerileme
olurdu. İlk 64 isabetin ortalama aralığı ölçülüp 32 baytın altındaysa
sayma döngüsüne geçiliyor. `count()`: 8,13 → **0,82 ms**.

**2. `sb_append(int)` — doğrudan tampona.** Rakamlar önce yığındaki
geçici tampona yazılıp sonra `memcpy`'leniyordu; uzunluk değişken olduğu
için `memcpy` satır içi alınmıyor, üstüne ikinci bir sınır hesabı
gerekiyordu. Artık doğrudan yazılıyor, yer ayırma sabit 24 bayt (int64:
20 hane + işaret + NUL). 24,0 → **18,9 ms**.

**Sonuç:** C++ 14,7 · Rust 18,7 · **Tulpar 19,0** · Go 24,5 · C# 29,9 ·
Java 32,5 · C 37,7. Beş alanın ikincisinde hedef tutuldu.

**Bu işin asıl kazancı ölçüm değil, bulunan boşluktu:** `sb` yer
ayırmasını 24 → 4 bayta düşüren enjeksiyon **hiçbir paketi kırmadı**
([[Tuzaklar#6m]]) ve bu, `tests/run_asan.sh`'ın doğmasına yol açtı.

### Döngü sürümleme — `arrayiter` 5,7 → 3,8 ms, Go geçildi (2026-09-05)

`for (int i = C; i < len(a); i = i + K)` içinde `a[i]` sınır denetimi
**kanıtla** gereksiz: dizi kutusuzken `count == len`, koşul üstten,
`C >= 0` ve `K > 0` alttan sınırlıyor, şekil kanıtı uzunluğun sabit
olduğunu zaten söylüyor.

"Dizi kutusuz" kısmı döngü-değişmezi **ama LLVM dışarı çıkaramıyor** —
yavaş yoldaki tazeleme şekil yuvalarına yazıyor, unswitch yapısal olarak
imkânsız (aynı gün ölçülüp çürütülmüştü). O yüzden dallanmayı ve iki
gövdeyi codegen üretiyor. Sınav `count_slot != 0`; kutuluysa yuva zaten 0,
boş dizide de genel sürüme düşüyor (gövde hiç koşmuyor).

| | ms |
|---|--:|
| önce | 5,73 |
| **sürümlü** | **3,37** |
| tavan (hiç bekçi yok) | 3,21 |

Koşucuda 4,6 → **3,8**; Go 4,1. Beş alanın üçünde hedefe yaklaşıldı.

**Yük taşıyan kısıt — gövdede HİÇ eleman yazması olmamalı.** Kutusuz bir
diziye int olmayan bir değer yazmak (`a[i] = 2.5`) diziyi kutuluyor,
`idata` free ediliyor ve önbellektekini **sarkıtıyor**; bunu bugün yalnız
tazeleme kurtarıyor, bekçisiz sürümde tazeleme yok. Yazmanın **hangi
isimden** olduğu önemsiz: `array b = a;` takma adı aynı diziyi kutular.
Bu yüzden koşul "bu diziye yazılıyor mu" değil, "gövdede **herhangi bir**
eleman yazması var mı".

`sieve` yararlanmıyor: iç döngüsü `while (k <= n)`, yani sınır `len(f)`
değil `n` — ikisini statik olarak ilişkilendiremiyoruz.

Enjeksiyon sonuçları ve sınanamayan koşul: [[Tuzaklar#6o]].

## In-memory (HTTP katmanı, 14 CPU)
| Mod | keep-alive tepe `/ping` | p50/p99 | RSS |
|-----|------:|:---:|---:|
| serve (tek bağlantı) | 4.2k RPS | 230µs/379µs | 6.9 MB |
| pool (14w) | ~39.8k RPS | 360µs/671µs | 8.4 MB |
| evented (tek thread) | **57.8k RPS** | 801µs/1.7ms | 7.0 MB |

- pool çekirdek sayısına **lineer** ölçeklenir, ~14'te plato. evented hafif handler'da en yüksek RPS.
- Tüm modlar 500+ eşzamanlı bağlantı / 500k+ istekte **RSS düz**, `err=0`. Dispatch 4–34µs.

## DB-bağlı (asıl darboğaz)
| | read PK | write rollback → WAL |
|--|--:|--:|
| pool | 23.8k | 8.8k → **20.4k** |
| evented | **29–32k** | 15.8k |

**Asıl tavan: SQLite paylaşımlı-handle serileştirmesi, HTTP değil.** evented (tek thread) read'de pool'u geçer (mutex çekişmesi yok). WAL write'ı 2.3× yapar (artık [[SQLite and DB]] varsayılanı).

## Kıyas
FastAPI'yi 5–15× geçer (Node Fastify ligi); Go/Rust altında. Asıl koz: **düşük bellek (8 MB) + sub-ms latency + tek dosya binary**.

## İlgili
[[Wings Serve Modes]] · [[Memory Leak Fixes]] · [[SQLite and DB]] · [[Roadmap]]

## Optimize edici HEDEF MAKİNEYİ ve kendi BAĞLAMINI görmüyordu (2026-09-06)

İki ayrı kusur, ikisi de "üretilen kod doğru ama sessizce yavaş"
sınıfından. İkisi de çıktı testiyle görünmez.

### 1. `LLVMRunPasses(..., nullptr, ...)`
TargetMachine geçilmiyordu → `TargetTransformInfo` yok → vektörleştiricinin
maliyet modeli yok → hiç ateşlenmiyor. `arrayiter`'in `main`'inde SIMD
komutu **0 → 7**.

### 2. Temel bloklar KÜRESEL bağlamda yaratılıyordu
`LLVMAppendBasicBlock` küresel bağlamı kullanıyor (bkz. [[Tuzaklar]] 6r).
Vektörleşen döngüde doğrulayıcı düşüyor, derleyici O3'ten **O1'e** iniyor.
Yani (1) düzeltildikten sonra vektörleşen her döngü, tam da vektörleştiği
için optimizasyonsuz kalıyordu.

### 3. Kanıtlı ELEMAN YAZMASI
Döngü sürümleme "gövdede eleman yazması varsa vazgeç" diyordu, çünkü
kutusuz bir diziye float yazmak diziyi kutuluyor ve önbellekteki `idata`
sarkıyor. Artık yazılan değerin **kesin tamsayı** olduğu kanıtlanabiliyorsa
yazma da bekçisiz üretiliyor. Kanıt dar: int sabitleri, **döngü değişkeni**
ve bunlar üzerinde `+ - * / %`.

Döngü değişkeni dışında hiçbir ad kabul edilmiyor. Denendi ve **geri
alındı**: codegen'e "bu ad native i64 yuvasında mı" diye soran bir geri
çağrı yazıldı, ölçüldü, **pratikte hiç "evet" demiyor** — `int k = 7`
diyen bir yerel bile kutulu bir `VMValue` yuvasında duruyor. Sınanamayan
bir kanıt yolu taşımaktansa kaldırıldı. Genişletmenin doğru yolu:
`a[i] = k` gibi döngü-DEĞİŞMEZİ bir adın etiketi de döngü değişmezidir,
yani `tag(k) == INT` sınavı döngü BAŞINA (sürümleme koşulunun yanına)
eklenebilir. Hiçbir kıyas buna bağlı olmadığı için yapılmadı.

### Ölçüm (arrayiter, BENCH_N=5M, 11 tur, ortanca)

| aşama | ms |
|---|---|
| başlangıç | 3,45 |
| yazma kanıtı (O1'e düşerek) | 3,88 — **gerileme** |
| + bağlam düzeltmesi (O3 korunuyor) | **2,83** |

Ortadaki satır dersin kendisi: yeni optimizasyon tek başına ölçülseydi
"işe yaramıyor, geri al" denirdi. İşe yarıyordu; boru hattı onu
cezalandırıyordu.

### LLVM 18 ile LLVM 22 aynı şeyi yapmıyor
Docker'da (`ubuntu:24.04` + `llvm-18-dev`) ölçüldü: aynı doldurma
döngüsünde **LLVM 22 vektörleştiriyor, LLVM 18 vektörleştirmiyor** —
`main` içinde 4 vs 0 vektör komutu. İkisinde de üretilen kod doğru,
ikisinde de O3 korunuyor, ikisinde de bekçisiz depo üretiliyor. Yani
buradaki kazancın bir kısmı **yerel makineye özgü**; CI'ın gördüğü
Tulpar daha yavaş. Kıyas sayıları yerel (LLVM 22) ölçümlerdir.

### Denenip BIRAKILAN
`TULPAR_TARGET_CPU=native` — Zen4'te AVX-512 seçimleri kazandırmıyor
(arrayiter 2,91 vs generic 2,77; intloop 151,9 vs 135). Kaçış kapısı
olarak duruyor, varsayılan `generic` (rustc tabanıyla aynı).

## `while` döngü sürümlemesi ve ELEK'İN GERÇEK açığı (2026-09-06)

### Elek tavanı — düzeltilmiş sayılar
Aynı kaynak, tek `#define` değişiyor (`width.c` / `width2.c`; önceki iki
ölçümüm geçersizdi, bkz. [[Tuzaklar]] 6f-2). N=5M, pinlenmiş, en iyi:

| | ms |
|---|---|
| C, int64 eleman, bekçisiz | 8,38 |
| C, int64 eleman, **bekçi + soğuk yol** | 9,35 |
| C, int32 eleman, bekçisiz | 7,78 |
| C, int8 eleman, bekçisiz | 7,39 |
| **Tulpar** | **9,36** |

Okunacak şey: Tulpar **bekçili C ile aynı hızda**. Kalan açık iki
kalemden ibaret — bekçi 0,97 ms, eleman genişliği 0,60 ms — ve üçüncü
bir "codegen kalitesi" kalemi yok.

### `while` sürümlemesi: kod mükemmel, sonuç ters
`while (v <= UB) { a[v] = ...; v = v + STEP; }` biçimi için kanıt
sözdiziminden çıkmıyor (elek'te başlangıç `i*i`, adım `i`, sınır `n`).
Çözüm: sayısal koşulları döngü BAŞINDA bir kez sınamak —
`v >= 0 && STEP > 0 && UB < count` — ve döngüyü sürümlemek.

Üretilen hızlı gövde gcc'ninkiyle **komut komut aynı**:

```
movq $0x1,(%r12,%rbp,8)      ; gcc:  movq $0x1,(%rcx,%rax,8)
add  %rcx,%rbp               ;       add  %rdx,%rax
cmp  %rdx,%rbp               ;       cmp  %rbx,%rax
jle  ...                     ;       jle  ...
```

İkili yamalanıp (`0xCC`) doğrulandı: hızlı dal koşuyor, genel dal **hiç**
koşmuyor. Buna rağmen:

| | ms |
|---|---|
| elek, sürümleme yok | 9,53 |
| elek, iç döngü sürümlü (derinlik 1) | **10,22** ← gerileme |
| elek, yalnız en dış seviyede | 9,54 |
| tek döngülü doldurma, sürümleme yok | 8,65 |
| tek döngülü doldurma, sürümlü | **8,01** |

Yani dönüşüm doğru ve döngüyü hızlandırıyor; İÇ İÇE açıldığında dış
döngünün gövdesine ikinci bir kopya girmesi kazancı yiyor. Mekanizma tam
olarak aydınlatılamadı — dış döngünün makine kodu komut komut aynı,
fark yalnız yazmaç ataması ve 13 komutluk büyüme — ama etki N ile
ÖLÇEKLENIYOR (N=20M'de 80,3 → 83,5), yani bellek sistemine bağlı.

**Karar:** `for` sürümlemesiyle aynı kural — yalnız `loop_depth == 0`.
Elek değişmiyor, tek döngülü doldurma %7 kazanıyor. Kısıt artık keyfi
değil, ÖLÇÜLMÜŞ.

### Denenip BIRAKILAN: global'leri `internal` yapmak
"İçeri alırsak GlobalsAA kanıt üretir, sıcak döngüde her tur yeniden
okunmazlar" — **yanlış**. Üretilen IR birebir aynı kalıyor (globaller yine
her tur okunuyor), yalnız makine kodu değişiyor: dışa açıkken LLVM adresi
bir yazmaca alıyor (`mov $ADDR,%r12` + `(%r12)`), içeri alınınca
RIP-göreli adresleme üretiyor ve sıcak döngüde daha uzun kodlanıyor.
Elek 9,66 → 10,54 ms. Geri alındı; içeri alınan modüllerin globalleri
tarihsel olarak `internal` ve onlara dokunulmadı.

## Açılış maliyeti: her ikiliye 0,4 ms'lik OpenSSL vergisi (2026-09-06)

Ayrıntı ve ölçüm tablosu [[Tuzaklar]] 6s'de. Özet: `print(1)` ikilisi 13
paylaşımlı nesne yüklüyordu; OpenSSL'e dokunan kod
`src/vm/runtime_net.cpp`e ayrılıp `-Wl,--as-needed` eklenince 6'ya indi.

| | önce | sonra |
|---|---|---|
| boş program | 1,15 ms | **0,76** |
| fib | 5,08 | **4,54** |
| ikili boyutu | 2,04 MB | 2,04 MB |

Bu, kıyasların TAMAMINI etkileyen tek değişiklik — her Tulpar programı
hiçbir şey yapmadan önce o kadar bekliyordu.

**Sırada duran, ölçülmüş ama yapılmamış:** SQLite de aynı durumda
(`runtime_bindings.cpp` `sqlite3_*` çağırıyor). Ayrılırsa her ikili
636 KB küçülür; hız etkisi yok, çünkü statik bağlanıyor.

### Elek: bekçi kaldırmanın kazancı SIFIR (ölçüldü)
C modelinde bekçi + soğuk yol 0,97 ms tutuyordu (`width2.c`). Tulpar'da
aynı şeyi yapmak — iç döngünün YALNIZ bekçisiz sürümünü üretmek, genel
gövde hiç yok, ikili yamalanarak doğrulandı — 10,74 → 10,85 ms verdi.
Yani **kazanç yok**. O döngü bellek sınırlı; komut saymak orada işe
yaramıyor. Elek'te kalan tek gerçek kaldıraç eleman genişliği (0,60 ms)
ve tek başına Rust'ı geçmeye yetmiyor.

## Açılış: 1,15 → 0,35 ms (2026-09-06/07)

Üç adımda, hepsi bağlama satırında:

| adım | boş program |
|---|---|
| başlangıç | 1,15 ms |
| ağ/TLS ayrı TU + `--as-needed` (OpenSSL yüklenmiyor) | 0,76 |
| `-static-libstdc++ -static-libgcc` | 0,53 |
| `--exclude-libs,ALL --gc-sections` (boyutu geri alır) | **0,35** |

C'nin boş programı 0,2–0,4 ms; artık aynı bantdayız. İkili 2,04 → 2,97 MB
(statik libstdc++ 4,01 yapıyordu, `--gc-sections` 1 MB'ını geri aldı).

⚠ İkinci adımın kararı bir kez YANLIŞ verildi çünkü tek kıyasa bakıldı;
kod yerleşimi ölçümü ±%27 oynatıyor. Bkz. [[Tuzaklar]] 6t.

### Sonuç (resmî koşum, r22)

| | C | Rust | Go | **Tulpar** | sıra |
|---|---|---|---|---|---|
| arrayiter | 2,3 | 2,3 | 4,2 | **1,7** | **1.** |
| intloop | 134,5 | 144,4 | 134,9 | **134,6** | **2.** |
| strcat | 37,8 | 18,9 | 24,6 | **18,5** | **2.** |
| fib | 1,6 | 3,7 | 6,6 | **3,9** | 4. |
| sieve | 7,5 | 8,3 | 8,5 | **8,9** | 5. |

Go'ya karşı 4 galibiyet 1 yenilgi (elek), Rust'a karşı 3 galibiyet
2 yenilgi (fib 0,2 · elek 0,6).
