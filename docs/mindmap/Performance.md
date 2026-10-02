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

> ⚠ Aşağıdaki tablo 2026-09-03 durumudur, **bayattır**. Güncel sonuç için
> bu notun sonundaki "Sonuç (resmî koşum, r23)" bölümüne bak.

| Test | C | Rust | Go | **Tulpar** | Java | Node | Sıra |
|---|--:|--:|--:|--:|--:|--:|:--:|
| intloop (50M) | 134.6 | 144.5 | 135.6 | **135.6** | 147.7 | 715.2 | **2.–3.** |
| fib(32) | 1.6 | 3.8 | 6.7 | **4.3** | 13.4 | 27.7 | **3.** |
| sieve (5M) | 7.7 | 8.2 | 8.5 | **~9.9** | 21.2 | 29.3 | 4. |
| strcat (2M) | 37.8 | 19.1 | 25.4 | **31.6** | 35.9 | 105.6 | **3.** |
| arrayiter (5M) | 2.3 | 1.7 | 4.0 | **6.6** | 19.5 | 21.3 | 4. |

Hedef "her alanda 2.–3. sıra" bu koşumda (2026-09-03) 5 testin 3'ünde tutuyordu.

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
**Tek ölçüm (2026-09-28, Ryzen 7 9800X3D, LLVM 22, fair N'leri, en iyi 9,
süreç dahil):** `arrayiter` generic 1,71 / native 1,77 ms; `intloop`
134,85 / 134,85 ve `sieve` 7,67 / 7,75 — ikisinin ikilisi native ile bayt
bayt aynı. `llvm_backend.cpp`'deki "generic 2,8 / native 1,0" yorumu eski
kod yolundandı, bu ölçüme indirildi.

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

**SQLite de ayrıldı (K214, 2026-09-27):** `src/vm/runtime_db.cpp`.
Ölçü (x86_64, GCC 15, `tulpar build`): `print(1)` ikilisi **3 005 736 →
1 437 680 bayt** (−%52; tahmin edilen 636 KB değil — SQLite'ın çektiği
statik bağımlılıklar da gitti). Açılış 0,251 → 0,243 ms (400 tur, eşlenmiş),
fib(38) 2,28 → 2,30 ms, matmul 55,1 → 54,5 ms, strcat 14,5 → 13,8 ms —
hız değişmedi. DB kullanan program eskisiyle aynı boyda.

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

### Sonuç (resmî koşum, r23 — özyineleme zinciri sonrası)

En iyi / (ortanca), 7 tekrar:

| | C | C++ | Rust | Go | **Tulpar** | sıra |
|---|---|---|---|---|---|---|
| **fib** | 1,6 | 1,9 | 3,7 | 6,7 | **0,6** | **1.** |
| intloop | 134,6 | 134,9 | 144,3 | 134,9 | **134,6** | **1.–2.** |
| arrayiter | 2,1 (2,5) | 2,9 (3,2) | 1,5 (2,4) | 3,9 (4,3) | **1,6 (2,2)** | 2. / **ortancada 1.** |
| strcat | 37,8 | 14,8 | 19,2 | 24,6 | **18,7** | **2.** |
| sieve | 7,6 | 8,1 | 8,2 | 8,4 | **8,6** | 5. |

**fib'de birinciyiz** — bu koşumda (n=32) C'nin 2,7, Rust'ın 6, Go'nun 11
katı hızlı. Oran bir SABİT değil: fark `n` ile büyüyen bir eğri üstünde bir
nokta (C ve Tulpar farklı üslerle ölçekleniyor; DOGRULAMA "Bu bir sabit
değil"). `fair/RESULTS.md`'nin son koşumunda aynı n'de 4×. Sebebi gcc'nin
bile yapmadığı kadar derin özyineleme açılımı (aşağıdaki bölüm).

Go'ya karşı 4 galibiyet 1 yenilgi (elek, 0,2 ms), Rust'a karşı 4 galibiyet
1 yenilgi (elek, 0,4 ms). Boş program tabanı: C 0,19 · Tulpar 0,28.

Kalan tek açık **elek**; ölçülmüş tek kaldıraç i32 dizi elemanı (0,60 ms).

## Runtime ile LTO: ölçüldü, kazanç yok (2026-09-28)

Tulpar modülleri zaten tek LLVM modülünde (import edilenler dahil, O3 bütün
programda) — modüller arası satır içi alma var. Açık olan Tulpar kodu ↔
`libtulpar_runtime.a` (C++) arası. `benchmarks/lto_olcum.py` aynı O3 sonrası
IR'ı iki kez linkliyor: clang runtime ile LTO'suz (B) ve tam LTO ile (C).
Ryzen 7 9800X3D, LLVM 22, 5 tur ortanca, C/B: intloop 1,01 · fib 0,96 · sieve
1,06 · strcat 0,97 · arrayiter 0,83 (2 ms, gürültü) · mandelbrot 0,97 · matmul
1,03 · nbody 0,96. **Ölçülebilir kazanç yok** — sıcak yolların runtime
çağrıları zaten IR'da satır içi; kayan nokta açığı kutulamadan. LTO altyapısı
(bitcode runtime dağıtımı, lld zorunluluğu) yapılmadı.

> ⚠️ **Tuzak:** `-march=native`'siz ilk ölçüm "LTO matmul'u 1,77x
> hızlandırıyor" dedi. LTO'suz kol jenerik x86-64 için derleniyordu, LTO
> kolu IR'daki `target-cpu` özniteliğiyle yerel CPU için. Fark LTO değil
> hedef CPU'ydu. İki kolu AYNI bayraklarla derle, tek değişken bırak.

## Özyineleme: LLVM'in yapmadığı işi biz yapıyoruz (2026-09-07)

fib'de gcc her LLVM dilini 2,4 kat geçiyordu ve bu "gcc işte" diye
geçiliyordu. Sayı tutmuyordu: fib(32) ≈ 7 milyon çağrı, gcc 1,6 ms — çevrim
başına bir çağrı, imkânsız. `objdump` cevabı verdi: gcc'nin `fib` gövdesi
**266 komut**, clang'ınki **22**. gcc özyinelemeyi satır içine alıp çağrı
sayısını düşürüyor; LLVM'in satır içi alıcısı bir SCC kenarını kendi içine
açmayı reddediyor. clang, Rust ve biz aynı tavanda takılıydık.

**Çözüm:** her özyinelemeli native fonksiyonun K kopyasını üretip halka kur —
`f → f.rec1 → … → f.recK → f`. Her kenar iki *farklı* fonksiyon arası çağrı
olduğu için sıradan alıcı onları kendi bütçesiyle açıyor. Derinliği biz değil
LLVM sınırlıyor, kod patlaması olmuyor (derleme süresi 58 → 62 ms, `fib`
sembolü 1,4 KB'de kaldı). Kod: `llvm_backend.cpp` `selfrec_*`.

### K'yı fib'e bakarak seçmek TUZAK
Gerçek derleyicide fib N=44: K=4 → 67 ms, K=6 → **3,7 ms**. K=6 açık ara
görünüyor. Beş ayrı özyineleme şekliyle ölçünce tablo değişiyor
(zincirsize göre kat):

| şekil | zincirsiz | K=4 | K=6 |
|---|--:|--:|--:|
| fib (iki çağrı) | 25,22 | 2,25 (11×) | 0,40 (63×) |
| fact (tek çağrı) | 13,40 | 0,26 (52×) | 0,23 (58×) |
| ack (iç içe) | 0,84 | 0,90 (0,9×) | 1,02 (0,8×) |
| tak (üç param) | 0,45 | 0,35 (1,3×) | 0,36 (1,3×) |
| **deep (200K derinlik)** | 1,23 | 0,75 (1,7×) | **73,68 (0,02×)** |

`deep`te K=6 **60 kat gerileme** yapıyor: kare büyümesi 200 000 seviyede yığın
trafiğini patlatıyor. K=4 beş şeklin hiçbirinde gerilemiyor → **K=4**.

K'ya bağımlılık ayrıca monoton değil (fib N=44: K=6 3,7 · K=7 82,5 · K=8 75,7
· K=10 10,7 · K=12 81,7) — alıcının bütçesi belirli K'larda zincirin ortasında
bitiyor. "Daha derin daha iyi" yanlış.

### Denenip ELENEN: `alwaysinline`
Ara klonları `alwaysinline` yapmak derinliği tam K yapar; LLVM sürümünden
bağımsız, kulağa daha ilkeli geliyor. Ölçüm çürüttü: fib N=44'te 82–260 ms
(bütçeye bırakılan sürüm 3,7). Tam açılım kodu şişiriyor ve ortak alt ifade
eleme ağacı toplayamıyor.

### Anlam koruyuculuk nasıl kanıtlandı
`TULPAR_NO_SELFREC=1` kapatma anahtarı eklendi ve **bütün külliyat iki kez**
derlenip çalıştırıldı (tek değişken bu bayrak): 103 örnek/paket bayt bayt
aynı, 64 yalnız-derle (pencere açanlar), 0 derlenemeyen. Ayrılan iki satır da
programların kendi ölçtüğü süre değerleriydi.

Sezgiye aykırı yan bulgu: **yığın derinliği gerilemedi, arttı** — en derin
başarılı çağrı 250 968 → 641 544. Satır içine alma 4 mantıksal seviyeyi 4
katından küçük tek kareye topluyor.

Bkz. [[Tuzaklar]] 6u.

### İç içe öz-çağrı şekli ZİNCİRLENMİYOR (2026-09-13)
Yukarıdaki tabloda `ack` K=4'te 0,9×, K=6'da 0,8× — bu şekil zincirden hiç
kazanmıyordu; K=1 yerelde 1,1× verdiği için "nötr" sayıldı. CI filosu bunu
çürüttü — **aynı ikili**, koşucuya göre (N=11, tur eşli medyan):

| CPU | K=1 zincirli / zincirsiz |
|---|--:|
| EPYC 9V74 (Zen 4) | 1,25 · 1,26 · 1,34 · **1,49** |
| EPYC 7763 (Zen 3) | 1,07–1,10 |
| EPYC 9V45 (Zen 5) | 0,66 |
| Ryzen 9800X3D (Zen 5, yerel) | 0,87–0,93 |
| Apple arm64 | 0,62 |

Zen 4'te %49 gerileme → ilkeye göre hata. Karlılık modeli: bir öz-çağrının
**argümanı içinde** başka bir öz-çağrı varsa (`ack`, `tak`) zincir kurulmaz
(`selfrec_scan` `nested`). `fib(n-1)+fib(n-2)` yan yana, etkilenmez (oran %5
aynen). Bedel (yerel, zorla K=1'e göre): ack %9, tak %25; arm64'te ack %38.
`TULPAR_SELFREC_DEPTH` açık verilirse kural atlanır (K taraması / teşhis).
Mekanizma bilinmiyor: K=1 `ack` 76→139 B, çağrı yeri 1→3, çerçeve aynı.
Kapı IR düzeyine indi (öz-çağrı sayısı + pozitif kontrol); zorla-K=1 oranı
her CI koşumunda `[bilgi]` satırıyla makine adına bağlı birikiyor.
Bkz. [[Tuzaklar]] 1p.

## i32 dizi elemanı ÖLÇÜLDÜ ve BIRAKILDI (2026-09-07)

Elekte C/Rust/Go'nun üçü de **32-bit** eleman kullanıyor (`int*`,
`vec![0i32]`, `make([]int32,n)`); Tulpar 64-bit. Yani bu bir eşitleme, hile
değil — ve uzun süredir "kalan tek ölçülmüş kaldıraç" diye duruyordu.

### C modeli 0,59 ms dedi, dilin içi 0,19 dedi
`width.c` (tek `#define`, sadece eleman genişliği değişiyor):

| | 8 bayt | 4 bayt | kazanç |
|---|--:|--:|--:|
| clang -O3 | 8,72 | 8,05 | **0,59** |
| gcc -O2 | 8,05 | 7,49 | **0,75** |

Ama bu model **C'nin** elek döngüsünü ölçüyor, Tulpar'ınkini değil. Gerçek
tavan için derleyiciye geçici, doğruluğu umursamayan bir "hep i32" hack'i
kondu (array_fill + altı eleman erişim yolu) ve elek koşturuldu:

| | ms |
|---|--:|
| Tulpar i64 (bugün) | 8,33 |
| **Tulpar i32 (hack)** | **8,14** |

**Kazanç 0,19 ms** — modelin vaat ettiğinin üçte biri. Üstelik bu bir ÜST
SINIR: gerçek uygulamada `int` 64-bit kalmak zorunda olduğu için taşan
değerde diziyi genişleten bir kontrol gerekir; hack'te o yok.

Bedeli ise: `ObjArray`da üçüncü bir durum, 35 çalışma zamanı erişim yeri,
taşmada genişletme, ve muhtemelen üçüncü bir döngü sürümü. 0,19 ms elek'i
8,6'dan ~8,4'e taşır — Go ile berabere, Rust'ın (8,2) hâlâ gerisinde. Yani
**hedefe ulaştırmıyor bile**. Bırakıldı.

### Asıl bulgu: elekte zaten LLVM tavanındayız
Aynı ölçümün yan ürünü daha değerli:

- Tulpar i64 **8,33** < clang'ın i64 C modeli **8,72** → bizim erişim
  yolumuz düz C'ye göre ölçülebilir bir maliyet EKLEMİYOR.
- Tulpar i32 **8,14** ≈ clang i32 **8,05**.
- gcc i32 **7,49** — aradaki 0,56 ms **gcc'nin LLVM'e üstünlüğü**, bizim
  açığımız değil.

İç döngü karşılaştırması bunu doğruluyor: gcc 4 komut (ölçekli indeks
adresleme, tek sayaç), clang 5 (LSR fazladan bir gösterici sayacı yaratıyor).

Yani elekteki 1,0 ms'lik açık şöyle bölünüyor: ~0,2 eleman genişliği
(alınmaya değmez), ~0,56 gcc-LLVM farkı (bizim elimizde değil), kalanı
gürültü. **Elek bitti.**

### İç içe döngü sürümlemesi: 4 komutluk döngü, YAVAŞ program
`TULPAR_X_NEST=1` ile iç döngü de sürümleniyor ve üretilen gövde gcc'ninkiyle
KOMUT KOMUT AYNI oluyor:

```
movq $0x1,(%r12,%rbp,8)      ; bugünkü 7 komutluk sürümde:
add  %rcx,%rbp               ;   add 0x0(%rbp),%r15   <- i BELLEKTEN
cmp  %rdx,%rbp               ;   cmp (%r12),%r15      <- n BELLEKTEN
jle  ...                     ;   + sınır denetimi
```

Yine de program yavaşlıyor: 8,54 → 9,21 (en iyi), 9,66 → 10,03 (ortanca),
30 ölçüm, araya sokularak. Sebep iç döngü değil, **dış döngü gövdesinin
ikiye katlanması**. Bu, 2026-09-06'daki aynı sonucun bağımsız tekrarı — o
zaman tek ikiliye bakıldığı için şüpheliydi, artık değil.

Globallerin bellekten okunmasının sebebi de belli: soğuk yolda
`call vm_set_element_ptr` var, çağrı her şeyi yazabileceği için LLVM
`i`/`n`'i yazmaçta tutamıyor. `internal` bağlantı denenmişti (2026-09-06) ve
IR'ı hiç değiştirmemişti — GlobalsAA kanıtı üretmiyor.

## Tipsiz ("kolay yazma") yol: iki gereksiz runtime çağrısı (2026-09-07)

`func fib(n)` tipli ikizinden 21 kat yavaştı. "Kutulama pahalı" diye
geçiştirilecek bir şey değil — ölçünce maliyetin nerede olduğu çıktı.

### Önce ölç: çağrı mı, aritmetik mi?
Dört sonda, 20M yineleme. **İlk denemem hiçbir şey ölçmedi**: tipli
sürümlerin ikisi de 0,26 ms çıktı, yani boş program seviyesi — LLVM
`t = t + i*3 - 1` döngüsünü SCEV ile kapalı forma katlamıştı. Gövde
katlanmaya dirençli hale getirildi (`% 1000003`), sonra:

| | önce | sonra |
|---|--:|--:|
| tipli aritmetik | 46,4 | 46,33 |
| **tipsiz aritmetik** | **134,6** (2,9×) | **46,51** (1,0×) |
| tipli çağrı | 46,4 | 46,35 |
| **tipsiz çağrı** | **90,8** (2,0×) | **46,32** (1,0×) |

### Bulunan iki şey
1. **`%` operatörünün kutulu satır içi yolu YOKTU.** `+ − * /` ve bütün
   karşılaştırmalar `op_int` bloğunda satır içi işleniyor; modulo `switch`in
   `default`ına düşüp `vm_binary_op`a gidiyordu. Dilin ikili operatör kümesi
   13 taneydi ve eksik olan tek operatör buydu. Düzeltme tek satır:
   `build_checked_div(..., 1)` — bölme yolunun zaten kullandığı yardımcı.
2. **`aot_persist` her kutulu global atamasında KOŞULSUZ çağrılıyordu.**
   Fonksiyon yığın değerlerini kalıcı kopyaya çıkarıyor; skalerde değeri
   olduğu gibi döndürüyor. Yani `var t = 0; t = t + 1;` her turda bir runtime
   çağrısı ödüyordu, hiçbir iş yapmayan. Etiket denetimi satır içine alındı:
   yalnız `VM_VAL_OBJ` ise çağrıya gidiliyor.

Çağrının kendi maliyetinden **daha pahalı olan şey, LLVM'in onu aşamaması**:
opak bir çağrı her şeyi yazabilir sayıldığı için döngü değişmezleri yazmaçta
kalamıyor. Sıcak döngüden bir çağrı kaldırmak, o çağrının süresinden fazlasını
geri veriyor.

### Kalan: tipsiz fib hâlâ yavaş, sebebi ABI
Bu iki düzeltme `fib`i değiştirmedi (11,92 ms) — orada `%` yok ve sıcak yolda
kutulu global ataması yok. Doğru kıyas zincir olmadan yapılmalı:

| | ms |
|---|--:|
| tipli fib (zincirli) | 0,56 |
| tipli fib (zincirsiz) | 4,90 |
| **tipsiz fib** | **11,92** |

Yani kutulamanın gerçek çağrı maliyeti **2,4×**; geri kalan fark özyineleme
zincirinin kutulu yola uygulanmamasından (zincir denendi, 1,4× geriledi).

2,4×'in kaynağı `t_f` ABI'si: `void t_f(VMValue* ret, VMValue* arg0)`.
Argümanlar ve dönüş BELLEKTEN geçiyor (çağrı başına ~6 bellek işlemi), ve
`t_fib` 312 baytlık kare açıyor. Kayıtla geçen bir ABI (`{i64,i64}` çifti —
`llvm_values.cpp` bunu çalışma zamanı fonksiyonları için zaten yapıyor) bunu
kaldırır, ama `call()` kayıt defteri, async coroutine motoru, struct
parametreleri ve wasm sret yolu aynı imzaya bağlı. Güvenli biçimi: gövde
kayıt-ABI'li `t_f$fast`e taşınır, `t_f` ince bir sarmalayıcı olarak kalır.
~~Henüz yapılmadı.~~ Yapıldı (2026-09-08): aşağıdaki "Kutulu fonksiyonlar
DEĞER ABI'sine geçti" bölümü (`t_<ad>.f`).

## Elek: BEŞ deneme, hepsi ölçüldü, hiçbiri ödemedi (2026-09-08)

"C'yi her alanda geçelim" hedefiyle elek yeniden ele alındı. Açık 0,8 ms
(Tulpar 8,5 · C 7,7). Denenen ve **ölçümle elenen** yollar:

| deneme | sonuç |
|---|---|
| i32 eleman (geçici hack) | +0,19 ms — maliyetine değmez, bkz. üstteki bölüm |
| iç içe döngü sürümleme (`TULPAR_X_NEST`) | **−0,57 ms**; 30 ölçüm, iki bağımsız koşum |
| döngü-değişmezi global önbelleği | **−0,58 ms** (aşağıda) |
| globalleri `internal` yapmak | IR **hiç değişmiyor** |
| boru hattına `require<globals-aa>` | IR **hiç değişmiyor** |

### Teşhis: darboğaz iç döngü değil, DIŞ döngü
Dış tarama 5M kez koşuyor ve bizde şöyle:

```
mov 0x0(%rbp),%rdx   ; i BELLEKTEN
inc %rdx
mov %rdx,0x0(%rbp)   ; i BELLEĞE
cmp (%r12),%rdx      ; n BELLEKTEN
...
xor %ecx,%ecx / test %ecx,%ecx / je    ; hep alınan ÖLÜ etiket denetimi
```

gcc'nin karşılığı 4 komut. Sebep: `f[i] == 0` karşılaştırmasının geri düşüş
dalında `vm_binary_op` çağrısı duruyor. Çalışma zamanında **hiç yürütülmüyor**
(`xor ecx,ecx` + `test` + `je` her zaman atlıyor) ama IR'de olduğu için LLVM
globalleri yazmaca alamıyor.

`internal` bağlantı + GlobalsAA bunu çözmüyor (ikisi de denendi, IR aynı
kalıyor). Döngü-değişmezi global önbelleği iki bellek okumasını kaldırıyor ama
LSR fazladan bir sayaç ekliyor (7→8 komut) ve net −0,58.

**Gerçek çözüm** çağrıyı IR'den kaldırmak olurdu: dizi kanıtla kutusuzken
`f[i]` doğrudan i64 döndürmeli, yani `codegen_typed_expr` AST_ARRAY_ACCESS'i
tanımalı (şu an tanımıyor — yalnız literal/tanımlayıcı/çağrı/ikili işlem).
O zaman `f[i] == 0` düz bir i64 karşılaştırması olur, etiket makinesi ve geri
düşüş çağrısı hiç üretilmez. ~~Yapılmadı.~~ Yapıldı (2026-09-08): aşağıdaki
"Elek: 5. sıradan 2. sıraya — çağrıyı IR'DEN kaldırmak" bölümü.

### Nereye kadar gidilebilir
- Tulpar i64 **8,33** < clang'ın i64 C modeli **8,72** → erişim yolumuz düz
  C'ye göre maliyet eklemiyor.
- gcc'nin clang'a üstünlüğü **0,56 ms** ve sebebi belirlendi: clang'ın LSR'si
  iç döngünün adres hesaplarını dış döngüye çıkarıyor — 3 fazladan sayaç, 5M
  dış yinelemenin hepsinde güncelleniyor, oysa iç döngü yalnız 348K kez
  giriliyor. gcc bunu yapmıyor (4 komut vs 8).

Yani elekteki açığın çoğu bizim ürettiğimiz IR'da değil, LLVM'in arka ucunda.

### Yan ürün: bir DOĞRULUK hatası
Bu kazının asıl getirisi hız değil, `int y = yan() + 0;` ifadesinin fonksiyonu
İKİ KEZ çağırdığının bulunması oldu. Bkz. [[Tuzaklar]] 6x.

## Elek: 5. sıradan 2. sıraya — çağrıyı IR'DEN kaldırmak (2026-09-08)

Bir önceki bölüm "elek bitti" diyordu. **Yanlıştı** — teşhis doğruydu ama
çözüm yolu denenmemişti. Teşhis şuydu: dış döngüde (5M yineleme)
`f[i] == 0` karşılaştırmasının geri düşüş dalında `vm_binary_op` çağrısı
duruyor; çalışma zamanında hiç yürütülmüyor ama IR'de olduğu için LLVM
`i` ve `n`'i yazmaçta tutamıyor.

Çözüm çağrıyı **hiç üretmemek**. Üç parça gerekti; hiçbiri tek başına yetmiyor:

1. **Kutulu ikili işlem artık TİPLİ YOLU önce soruyor.** `codegen_expression`
   AST_BINARY_OP'ta koşulsuz etiket makinesi üretiyordu (iki tag okuması, dört
   temel blok, geri düşüş çağrısı). Artık `codegen_typed_expr`e soruyor; iki
   operand da statik int ise düz i64 işlem çıkıyor.
2. **`codegen_typed_expr` dizi erişimini öğrendi.** Kanıtlı (`shape_access_proven`)
   okuma ham i64'tür; kutulanmasına gerek yok. Önce yalnız literal /
   tanımlayıcı / çağrı / ikili işlem tanınıyordu.
3. **`while` kanıtı sabit adımı kabul ediyor.** `stmt_is_step` adımın bir AD
   olmasını şart koşuyordu ("sabit adım zaten `for` kanıtında") — ama `for`
   kanıtı yalnız `for` döngülerine bakıyor. `while (i <= n) { ...; i = i + 1; }`
   ikisinin de dışında kalıyordu ve **hiç sürümlenmiyordu**; elek'in dış
   döngüsü tam bu biçimde. Sabit adım üstelik daha kolay: `STEP > 0` sınavı
   derleme zamanında katlanıyor.

### Sonuç
Dış döngü **11 komut / 3 bellek erişimi → 6 komut**:

```
inc  %rsi                 ; i++            (yazmaçta)
mov  %rsi,0x0(%rbp)       ; i -> global
cmp  %rdx,%rsi            ; i <= n         (n YAZMAÇTA)
jg   ...
cmpq $0x0,(%r14,%rsi,8)   ; f[i] == 0      (etiket makinesi YOK)
jne  ...
```

gcc'nin karşılığı 5 komut (tek fark: `i` bizde global olduğu için yazılıyor).

| | ms |
|---|--:|
| Tulpar önceki | 8,43 |
| **Tulpar yeni** | **8,06** |
| gcc -O2, **8 bayt** eleman | 8,19 |
| gcc -O2, 4 bayt eleman | 7,51 |

**Aynı eleman genişliğinde gcc'yi geçiyoruz.** Resmî koşumda elek 8,1 —
5. sıradan **2. sıraya**; C++ (8,4), Rust (8,3) ve Go (8,8) geride, yalnız
C (7,9) önde ve fark 0,2 ms. O 0,2, C'nin `int*` (4 bayt) kullanmasından
geliyor — i32 ölçümüyle (0,19) birebir tutuyor.

### Ders
"LLVM tavanındayız" sonucuna varmadan önce, sıcak döngüde **çalışmayan ama
duran** bir çağrı olup olmadığına bak. Ölü bir dal bile optimize ediciyi
durduruyor; onu kaldırmak çağrının süresinden fazlasını geri veriyor.

## i32 eleman: TAM UYGULANDI, ölçüldü, GERİ ALINDI (2026-09-08)

Elekte C'ye kalan 0,2 ms'lik açığı kapatmak için 32-bit eleman deposu **baştan
sona uygulandı** (temsil, çalışma zamanı, sekiz codegen erişim yolu,
genişletme, testler) ve sonra geri alındı. Sebep tek cümleyle: **her tasarım ya
kazandığından çok kaybettiriyor ya da 64-bit dizilerde sessiz bir uçurum
açıyor.**

### Önce: tavan ölçümü BAYATLAMIŞTI
Eski ölçüm (2026-09-07) i32'yi 0,19 ms diye eledi. O ölçüm, dış döngünün
bellek yeniden okumalarıyla boğulduğu **eski codegen'de** yapılmıştı. Döngü
sıkılaştıktan sonra yeniden ölçülünce:

| | ms |
|---|--:|
| Tulpar i64 | 8,17 |
| **Tulpar i32 (hack)** | **7,53** |
| gcc -O2 4B | 7,62 |

**0,64 ms** — üç katı. Ve i32'li Tulpar gcc'yi *geçiyor*. Ders: bir tavan
ölçümü, ölçtüğü kodun etrafı değişince geçersizleşir.

### Uygulanan iki tasarım ve ikisinin de düştüğü yer
`ObjArray`a `elem_bits` eklendi; dizi 32-bit başlıyor, i32'ye sığmayan bir
değer yazılınca **genişletiliyor** (kutulanmıyor — `aot_arr_widen`). Çalışma
zamanı okuma/yazma genişlik farkında; `vm_array_get/set` artık kutusuz diziyi
kutulamadan işliyor (bu tek başına bir iyileştirme).

Ayrım codegen'de: hızlı yollar hangi genişliği varsayacak?

| tasarım | elek (i32 dizi) | elek (64-bit dizi) |
|---|--:|--:|
| bugünkü (i32 yok) | 8,06 | 8,06 |
| **V1** — şekil önbelleği yalnız i32 kabul eder | **7,56** | **15,12** |
| **V2** — önbellek iki genişliği de destekler (dal) | 8,46 | 10,92 |

- **V1** kazanıyor ama 64-bit diziler önbellekten tamamen düşüyor: **1,87 kat
  gerileme**. `int[]` içinde bir zaman damgası (`now()` ms) tutmak yeter.
- **V2** güvenli ama iç döngüye giren dal, kazancın tamamını yiyor —
  8,46 > 8,06, yani **net kayıp**.

### Dalın maliyeti yeniden okuma DEĞİL
V2'deki dalın pahalı olmasını "genişlik yuvası döngü içindeki tazelemeler
yüzünden her turda yeniden okunuyor" diye açıkladım (daha önce `i`/`n` ile
yaşanan şeyin aynısı). Sınadım: tazelemenin genişliği yazmasını kaldırınca
8,34 → 8,30. **Hiçbir şey değişmedi.** Maliyet dalın kendisi; dolayısıyla
"sayacın işaretine gömelim, fazladan yükleme olmasın" fikri de ölü.

### Neden yine de gönderilmedi
0,2 ms'lik bir sıralama farkı için, `int[]` içinde büyük sayı tutan her
programa 1,35–1,87 katlık sessiz bir yavaşlama koymak, "C kadar hızlı, Python
kadar kolay" ile bağdaşmıyor. Görülemeyen uçurum, kolay değildir.

### İşe yarayabilecek tasarım (yapılmadı)
Döngü sürümlemesi zaten gövdeyi ikiye ayırıyor. Hızlı sürüm i32 varsayıp
dalsız üretilebilir, **genel sürüm** ise i64 varsayıp yine dalsız — yeter ki
her sürüm kendi genişliğine özel bir TAZELEME fonksiyonu kullansın (genişlik
değişirse `count = 0` yazıp erişimleri bekçili yola düşürsün). Böylece dal
sürüm seçimine taşınır ve iki genişlik de tam hızda kalır. Maliyeti: sürüm
başına özel refill fonksiyonu + `shape_assume32` bayrağı.

Yan ürün olarak kalan iyileştirme fikri: `vm_array_get/set`in kutusuz diziyi
KUTULAMADAN işlemesi bu çalışmada yazıldı ve tek başına doğru bir kazanç —
bugün genel yoldan tek bir okuma bile diziyi 8 bayttan 16 bayta çıkarıyor.

## i32 eleman GÖNDERİLDİ — elekte 1. sıra (2026-09-08, ikinci deneme)

Bir önceki bölüm i32'yi "her tasarım ya kaybettiriyor ya uçurum açıyor" diye
geri almıştı. O bölümün son paragrafında yazılan tasarım **kuruldu ve işe
yaradı**.

### Fikir: dal, erişimden SÜRÜM SEÇİMİNE taşındı
Döngü sürümlemesi gövdeyi zaten ikiye ayırıyor. Artık:

- **hızlı sürüm** — koşul `count != 0 && is32`; erişimler 32-bit, **dalsız**
- **genel sürüm** — şekil yuvaları `want=64` ile yeniden dolduruluyor
  (32-bit ya da kutulu dizide `count = 0` yazılır); erişimler 64-bit, **dalsız**
- **sürümlenmemiş döngü** — `shape_want32 = -1`, erişim yerinde dallanır

Genişlik değişirse (widen) o sürümün tazeleme fonksiyonu `count = 0` yazıyor ve
erişimler bekçili yola düşüyor — doğruluğu sağlayan mekanizma bu. Her sürümün
kendi genişliğine özel bir refill fonksiyonu var (`fn_shape_refill[eager][want]`).

### Ölçüm
| | i32 dizi | 64-bit dizi |
|---|--:|--:|
| önceki (i32 yok) | 8,06 | 8,06 |
| V1 (önbellek yalnız i32) | 7,56 | **15,12** |
| V2 (erişimde dal) | **8,46** | 10,92 |
| **V3 — sürüm başına uzmanlaştırma** | **7,73** | **9,44** |

V3 gönderildi. 64-bit dizilerde kalan %17, genişletilmiş dizinin bekçisiz
sürüme girememesinden (sayısal kanıt geçerli ama sürüm koşulu `is32` istiyor);
kapatmak üçüncü bir gövde kopyası gerektirir, o da ölçülmüş bir kayıp.

### Resmî sonuç
| | C | C++ | Rust | Go | **Tulpar** | sıra |
|---|--:|--:|--:|--:|--:|:--:|
| fib | 1,6 | 1,9 | 3,8 | 6,9 | **0,6** | **1.** |
| **sieve** | 7,9 | 8,1 | 8,4 | 8,8 | **7,8** | **1.** (bu koşumda; `fair/RESULTS.md`'nin son koşumunda Go ile eşit 8,0, C 7,7) |
| strcat | 37,7 | 14,7 | 18,8 | 25,1 | **13,8** | **1.** |
| arrayiter | 2,4 | 3,1 | 1,6 | 4,2 | **1,3** | **1.** |
| intloop | 135,0 | 135,6 | 144,5 | 135,0 | **135,4** | 3. (%0,3) |

arrayiter de kazandı (1,5 → 1,3): bellek yarıya inince yalnız elek değil her
dizi yükü kazanıyor.

### Yan kazanç: `vm_array_get/set` artık KUTULAMIYOR
Genel yoldan tek bir okuma bile diziyi kutuya çeviriyordu (8 → 16 bayt/eleman,
geri dönüşsüz). Artık genişlik farkında ve kutulamadan okuyup yazıyor; i32'ye
sığmayan değer diziyi **genişletiyor**, kutulamıyor.

## Kutulu fonksiyonlar DEĞER ABI'sine geçti (2026-09-08)

Tipsiz kod ("Python kadar kolay" yarısı) `void t_f(VMValue* ret, VMValue* a0,
...)` imzasıyla çağrılıyordu: argümanlar ve dönüş **bellekten** geçiyor,
`t_fib` 312 baytlık kare açıyordu.

**Çözüm — sarmalayıcı ayrımı:** gövde `t_<ad>.f` adında, VMValue'yu DEĞER
olarak alıp döndüren bir fonksiyona taşındı (SysV'de `{i64,i64}` = iki
yazmaç). `t_<ad>` ince bir sarmalayıcı olarak kaldı, böylece **call() kayıt
defteri, async coroutine motoru ve wasm sret yolu hiç değişmedi**.

Altyapı zaten vardı: `llvm_make_vmvalue_func_type` / `llvm_call_vmvalue_func`
iki hedefte de doğru ABI'yi kuruyor (SysV çifti · wasm/Win64 sret+byval).

**Ölçüm:** tipsiz `fib(32)` **11,86 → 7,69 ms** (1,54 kat).

### Dar tutulan uygunluk
Dışarıda kalanlar imzaya bağlı oldukları için: async (coroutine motoru `t_<ad>`i
tam o imzayla çağırıyor), struct parametre/dönüş (o yuvalar işaretçi ABI'sini
anlamlı kullanıyor), `main`. Çağrı tarafında ayrıca varsayılan-argüman
doldurması olan çağrılar sarmalayıcıya gidiyor.

### İlk denemede LAMBDALAR kırıldı
`fn_value_abi` bayrağı çevreleyen fonksiyondan **miras alınıyordu**, oysa
lambdalar işaretçi ABI'si kullanıyor. Modül doğrulaması *"Found return instr
that returns non-void in Function of void return type"* ile düşüyor ve program
**optimize edilmeden** derleniyordu. Dört closure testi kırmızıya döndü ve
sebebi oydu. Bayrak artık gövde sınırında temizleniyor.

Ders: bir codegen bayrağı "şu an hangi fonksiyonu üretiyoruz" bilgisini
taşıyorsa, **iç içe gövde üreten her yol** onu kaydedip geri yüklemeli —
lambda, native fonksiyon, klon zinciri.

### Yan bulgu: asıl maliyet ABI değildi
Bu işten önce "kutulamanın çağrı maliyeti 2,4 kat" diye tahmin etmiştim
(tipsiz fib 11,9 vs tipli-zincirsiz 4,9). ABI düzeltilince 7,69'a indi, yani
ABI payı ~1,5 kat; kalan fark **kutulu aritmetiğin etiket dağıtımı**. Onu
kapatacak şey ABI değil, tip özelleştirmesi (`n`in int olduğunu bilmek).

## Wings TLS yük altında (2026-06-22)

`examples/api_wings_tls.tpr`, OpenSSL 3.5.5, 1000 istek / 50 paralel: **0 hata,
~663 istek/sn**, keep-alive gecikmesi ~1,5 ms, sunucu kararlı, günlük temiz.
Makine kaydı o gün yazılmamış; sayı STATUS.md "Küçük cila turu (2026-06-22)"
kaydından. Roadmap'teki "TLS yük altında test edildi → [[Performance]]"
bağlantısı bu bölüme bakar (2026-09-28'e dek bu belgede TLS ölçümü yoktu).
Düz HTTP tavanı ve Node kıyası için `benchmarks/WINGS_STRESS.md`.

## `json` nesnesi O(N²) idi — hash indeksi (2026-10-01)

Hız karnesinin `hashmap` çekirdeği (1M dizgi anahtar, ekleme + arama) Tulpar'da
60 s sınırında bitmedi. Sebep tek döngü: `vm_object_set` ve `vm_object_get`
anahtarları baştan sona `strcmp` ile tarıyordu. Ölçüm: 12 500 anahtar 0,25 s,
25 000 0,95 s, 50 000 3,8 s — iki kat anahtar, dört kat süre.

Çözüm ve sınırları:
- 16+ anahtarda açık adreslemeli FNV-1a indeksi, yuva = (hash, keys[] indisi).
  Anahtar silme yolu yok, indisler kararlı.
- Indeks **yalnız yazma yollarında** kurulur (vm_object_set, AOT nesne
  kurucusu, HTTP nesnesi, `fromJson`, iki kopya yolu). `vm_object_get`'in
  "okuma saf kalmalı" sözleşmesi (FINDINGS T7: paylaşılan json eşzamanlı
  okunuyor) korunur: okuma indeksi yalnız okur.
- Indeksin görmediği kuyruk okumada doğrusal taranır: indeksi güncellemeyen
  bir ekleme yolu kalsa bile sonuç yanlış olmaz, yalnız yavaş olur.
- Alan alan kopyalanan nesne indeks işaretçisini paylaşabilir; indeks
  `owner` taşır, kopya onu yok sayar.

Sonuç (Ryzen 7 9800X3D): `hashmap` >60 s → 170 ms (2.; C 73). 24 anahtarlı
nesne okuma 184 → 60 ms. 5 anahtarlı nesne kur/oku 612 → 544 ms (esik altı,
gerileme yok). Kalan: tepe bellek 450 MB (C 66 MB).


## `split` tek ayırmada, `toInt` atoll'suz — `parse` 200 → 126 ms (2026-10-01)

Hız karnesinin `parse` çekirdeği (5M sayıyı virgülle birleştir, böl,
`toInt`, topla): Tulpar 196 ms, C 57 ms, tepe bellek 443 MB (C 32 MB).
Hipotez "split parçaları döngü boyunca geri alınmıyor" idi. Önce ölçüldü
(Ryzen 7 9800X3D, `taskset -c 10,11`, en iyi 5; programı aşama aşama kesip):

| aşama | süre | tepe RSS |
|---|---:|---:|
| metni kur (`sb_append` ×5M + `sb_tostring`) | 48 ms | 89 MB |
| `split` | +112 ms | +363 MB |
| `toInt(parts[i])` döngüsü | +39 ms | +0 |

Hipotez **çürüdü**: döngü hiç ayırmıyor; bellek split'in CANLI parçaları —
`parts` hepsini tutuyor. Parça başına 64 bayt arena (56 baytlık `ObjString`
başlığı + ortalama 4,9 karakter + NUL, 8'e yuvarlı) + 16 baytlık `VMValue`
eleman: 5M × 80 = 400 MB. Go aynı işi 16 baytlık dilim başlıklarıyla yapıyor
(118 MB). Başlık temsili değişmeden bu taban inmez.

Süre ise düştü. Split parça başına: `strstr`, geçici `malloc` + `strncpy` +
`free` (yalnız NUL için — `aot_allocate_string` zaten uzunlukla kopyalıyor),
ayrı arena ayırması, büyüyen diziye `push`. Yeni yol iki geçiş: say + boyu
topla, sonra eleman deposunu tam boyda bir kez ve parçaları TEK arena
ayırmasında bitişik kur. Beklenmeyen kazanç sayfa hatasında: 1 MB'lik arena
blokları THP'ye hiç uymuyordu; tek 320 MB'lik blok 2 MB sayfalarla doluyor.

| | önce | sonra |
|---|---:|---:|
| split | 112 ms | 57 ms |
| küçük sayfa hatası (bütün program) | 85 251 | 4 618 |
| çekirdek zamanı | 59 ms | 22 ms |
| `toInt` döngüsü (düz ondalık hızlı yol) | 37 ms | 22 ms |
| **`parse` toplam** | **200,5 ms** | **126,2 ms** |
| tepe RSS | 442 MB | 440 MB |

`toInt`: `[+-]?[0-9]{1,18}` biçimindeki dizgi `atoll`a (strtoll: yerel ayar,
taban, taşma denetimi) gitmeden çözülüyor; 18 hane int64'e taşmadan sığıyor,
yani sonuç bayt bayt aynı. Başka her biçim `atoll`.

Kalanlar:
- **Bellek tabanı**: `Obj` başlığı 32 bayt (`next` işaretçisinin iki yanında
  dolgu); alanları yeniden sıralamak onu 24'e, `ObjString`'i 48'e indirir —
  `parse`'ta −40 MB. ABI değişikliği (codegen GEP'leri, `obj_header_pad`,
  wasm32 düzeni), ayrı iş.
- ~~Üst düzeyde `str s = sb_tostring(sb)` 29 MB'lık metni İKİ kez tutuyor:
  arena kopyası + global bariyerinin kalıcı kopyası.~~ Kapandı (2026-10-01,
  "Geçici dizgiler" aşağıda): bariyer yerleşmiş arena dizgisini
  kopyalamıyor, 440 → 412 MB.
- Linux dağıtımlarının çoğunda THP varsayılanı `madvise`: orada büyük arena
  bloğu `madvise(MADV_HUGEPAGE)` istemeden 2 MB sayfa almaz (bu makine
  `always`). Sayfa hatası kazancı o sistemlerde ölçülmedi.

`hashmap` belleği (450 MB, C 66) **aynı kök neden değil** — orada çöp gerçek:
ekleme başına `toString` + birleştirme (2 × 64 B), `vm_object_set` anahtarı
önce arenaya sonra kalıcıya kopyalıyor (64 B fazladan), arama başına yine
2 × 64 B geçici anahtar; hiçbiri geri alınmıyor. Yalnız çift kopyayı kaldıran
bir deneme 439 → 378 MB, 190 → 155 ms ölçtü (gönderilmedi; ayrı iş).
Kapandı: aşağıda "Geçici dizgiler" (439 → 118,8 MB).

## `call(f, ...)`: ayırmasız fonksiyon referansı + satır içi havuz çağrısı (2026-10-01)

Hız karnesinin `callfn` çekirdeği (iki fonksiyonluk tablodan, verinin seçtiği
girişe dolaylı çağrı): Tulpar 299 ms, C 91 ms — çağrı başına ~15 ns. IR'den
okunan yol: fonksiyon referansı bir DİZGİ (`"f"`), ve

1. **her değerlendirmede ayırma**: bare `f` → `vm_alloc_string_aot` → 64
   baytlık arena dizgisi, hiç geri alınmıyor. Tablo bir kez kurulduğu için
   `callfn`'de görünmüyor, ama `acc = call(f, acc)` döngüsü 20M turda
   **1,26 GB** tepe bellek ve 401 ms ölçtü (çağrı başına 64 B).
2. **her çağrıda ad araması**: `aot_call_dynamic_1` → FNV hash → atomik
   önbellek yoklaması + `memcmp` → kutulu giriş noktası (`tb_f`) → `f`.

Düzeltme iki katlı:
- **Referans havuzu** (runtime): bare `f` artık site başına bir kez
  `aot_fn_ref` ile çözülüyor (dizgi sabitlerinin `tulpar_strlit_N` kalıbı) ve
  statik bir havuzdaki kalıcı `ObjString`'i döndürüyor; kaydın yanında giriş
  noktası + arite var. Değer her yerde yine dizgi. `call()` adresin havuz
  aralığında olduğunu görünce ad aramasını atlıyor. Havuz yalnız ekleniyor,
  kayıtlar ölümsüz — okuyan thread'ler kilitsiz (FINDINGS T7'nin okuma
  saflığı korunuyor: okuma yolu hiçbir şey yazmıyor).
- **Satır içi yol** (codegen): `call(x, a1..an)` (n ≤ 8, web hariç) önce
  `x`in havuzda ve aritesinin n olduğunu denetliyor, öyleyse giriş noktasını
  doğrudan çağırıyor; değilse eski runtime çağrısı. Kayıt düzeni (fp @56,
  arite @64, 72 bayt × 1024) runtime'da `static_assert` ile kilitli.

| | önce | havuz (runtime) | + satır içi |
|---|---:|---:|---:|
| `callfn` (tablodan) | 298,6 ms | 231,5 ms | **209,6 ms** |
| `call(f, acc)` 20M (doğrudan) | 401 ms / 1,26 GB | 160 ms / 2,5 MB | **145 ms / 2,7 MB** |

Kalan fark (10,5 ns vs C 4,5 ns): `tb_f` sarmalayıcısı `f`'i bilerek satır
içine almıyor (ikili boyutu; K092 notu), sonuç bellek yuvasından geçiyor ve
verinin seçtiği hedef dolaylı dal tahminini bozuyor (C de aynı cezayı ödüyor).
`acc` üst düzey global olduğu için her tur belleğe yazılıyor (üst düzey global
maliyeti ayrı iş).

Yan ürün: `call()` kapanış da alıyor (`aot_call_closure`'a, `cl(a)` ile aynı
sözleşme). Eskiden "call() string bekler".

### İkinci adım: tümü-int hedefte sarmalayıcısız yol — 174 → 65,5 ms (2026-10-01)

Kalan farkı IR'den okuyunca (callfn döngüsü, LLVM 23 -O3): `acc` zaten
döngüde yazmaçta (phi; tur başına yalnız global'e bir saklama), yani "üst
düzey global" kalan farkın küçük parçası. Asıl bağımlılık zinciri
`tb_f`'te: argüman yuvaya yazılıp sarmalayıcıda geri okunuyor
(saklama→yükleme iletimi), INT/FLOAT çevirisi `fptosi` + `select` olarak
zincirde, ikinci çağrı (`tb_f` → `f`, `noinline`), sonuç yine yuvaya yazılıp
okunuyor ve çağıranda ikinci `fptosi`/`select`. C'de zincir `and → yükle →
çağır → f`.

Düzeltme: havuz kaydına (`AOTFnRef`) çıplak giriş noktası `nfp` (@72, kayıt
80 bayt) eklendi; main'in girişi tümü-int her hedef için
`aot_register_func_native(tb_f, f)` çağırıyor. Satır içi yol arite
tuttuğunda `nfp` doluysa ve argümanların HEPSİ çalışma zamanında INT ise
`i64 f(i64)`yi doğrudan çağırıyor, sonucu INT olarak kutuluyor; LLVM
statik INT argümanın etiket sınavını katlıyor ve sonuç tarafında
jump-threading ile `fptosi`/`select`i yalnız kutulu yola bırakıyor. INT
olmayan argüman (float → kırpma, bool) sarmalayıcıya gidiyor: anlam aynı.

| (Ryzen 7 9800X3D, `taskset -c 10,11`, en iyi 7–11) | önce | sonra |
|---|---:|---:|
| `callfn` (tablodan) | 174,0 | **65,5** (koşuma göre 65–76) |
| `call(f, acc)` 20M (doğrudan) | 145,5 | **58,2** |
| aynı düzenekte C: gcc -O2 / clang -O2 | 91,4 / 58,2 | |

Tulpar'ın gcc C'sinden hızlı çıkması bir dil iddiası değil: iki C
derleyicisinin döngüsü komut komut aynı (`and`, `call *(%r12,%rax,8)`),
fark `f`/`g` gövdelerinde ve yerleşimde; hedefi veriye bağlı dolaylı çağrı
dal tahminine duyarlı. İddia "C sınıfında". Kalan: `acc` main yereli olunca
63,5 ms (`TULPAR_MAIN_LOCALS_ALL=1`), ama `int` kuralı elek yüzünden global.
Kapılar: `tests/call_yerel_int.sh` (IR + runtime tanısı `yerel=N`, iki
bağımsız ayak; `TULPAR_NO_CALL_NATIVE=1` pozitif kontrol; iki sabotajla
kırmızı) ve `tests/call_yerel_int.test.tpr` (iki yol aynı sonuç).

## Float dizisi: double depo + iç içe döngüde kanıtlı erişim (2026-10-01)

Hız karnesinde `matmul` C'nin 26, `nbody` 11 katıydı (2026-09-29); dizisiz
`mandelbrot` 1,00× olduğu için aritmetik suçlu değildi. Önce ölçüldü
(Ryzen 7 9800X3D, `taskset -c 2,3`, 5 tekrarın en iyisi):

| | ms |
|---|--:|
| matmul üst düzey (kıyasın kendisi) | 821,6 |
| aynı kod fonksiyon içinde | 852,4 |
| aynı kod `int[]` ile, fonksiyon içinde | 530,2 |
| C (`gcc -O2`) | 31,3 |

Yani global maliyeti değil (fonksiyon içinde de aynı), ve tamsayı dizisinde
bile 17× — iki ayrı açık. IR (`TULPAR_AOT_EMIT_LL=1`) ikisini de gösterdi:

1. **Kanıtlı erişim yalnız en dış döngüde, yalnız `a[i]` biçiminde.**
   matmul'ün sıcak döngüsü üçüncü seviyede ve indeksi `i * n + j`; her erişim
   şekil önbelleğinden sınır denetimi + genişlik dalı ödüyor.
2. **Float dizi şekil önbelleğine hiç girmiyor.** 16 baytlık kutulu depo
   (count=0) → her erişim tam genel yol: etiket, nesne türü, sınır, kutu
   denetimi, 16 baytlık yükleme; sonuç dinamik etiketli → her `+`/`*` için
   `both_int / both_float / vm_binary_op` dallanması.

Önce yalnız depoyu değiştirmek (ham double, `ARR_ELEM_F64`) **ölçüldü ve
yetmedi**: matmul 821 → 863 ms (genel okuma yoluna bir genişlik dalı daha
eklendi, 9,8 MB'lik veri zaten 96 MB önbelleğe sığıyordu). Kaldıraç döngü
sürümüydü:

- **Plan** (`llvm_array_shape.cpp`, `tulpar_float_loop_plan`): en içteki
  `for` (gövdede döngü yok — iç içe sürüm katlanarak büyümesin), her erişim
  `X[j]` / `X[B]` / `X[B + j]` (B: değişmez ad, sabit, `+ - *`), her eleman
  yazması KESİN float (float sabiti/okuması, gövdede `float` bildirilip tek
  kez atanan yerel, değişmez ad, `+ - * /`, tekli eksi, `sqrt`).
- **Sınav** (`fv_try_version`): döngü başında bir kez — j ve UB int ve
  i32'ye sığar, her dizi double depoda (`tulpar.f64_probe`), her erişimde
  `0 <= B + j0` ve `B + UB' <= count`, değişmez adların etiketi FLOAT.
- **Hızlı gövde:** erişim tek GEP + double load/store; döngü koşulu döngü
  başındaki UB ile. Bu son madde ölçümle geldi: ilk sürüm 98,7 ms'de kaldı,
  çünkü koşuldaki global `n` her turda bellekten okunuyordu (eleman yazmasıyla
  takma ad sanılıyor) ve LLVM tur sayısını bilmediği için vektörleştirmiyordu.
  Değişmez UB ile **37,4 ms** ve `<2 x double>` döngü.
- `sqrt` satır içi `llvm.sqrt` (runtime ile aynı kural: int ise `(double)x`,
  değilse bit deseni); iki float operandlı `+ - * /` tipli yolda doğrudan.

Sonuç (dönüşümlü A/B, 11 tur, en iyi): matmul **819,8 → 37,1 ms** (1,19× C; tepe bellek 22,1 → 12,3 MB, C
11,8), nbody **1301,1 → 187,3 ms** (1,62× C). Tamsayı dizilerine dokunulmadı:
sürüm yalnız programda `float[]` bildirilmiş adlarda deneniyor (ipucu;
doğruluk çalışma zamanı sınavından), çünkü koşmayan kopya bile dış döngüyü
yavaşlatabiliyor (yukarıdaki "İç içe döngü sürümlemesi").

**Gerileme denetimi** ve `sieve`'deki %2'nin hizalama olduğu: CHANGELOG ve
[[Tuzaklar]] 7i.

**Kalan:**
- ~~nbody 1,6×: beş cisimde iç döngü 0–4 tur dönüyor ve HER girişte yedi
  global dizinin deposu yeniden sınanıyor (çağrı başına altı giriş).~~
  **Kapandı (2026-10-01)**, aşağıdaki "nbody: iç döngü sınavı dış döngü
  başına" bölümü.
- ~~Tamsayı dizisinde iç içe/afin kanıt yok (`matmul_int` 530 ms).~~
  **Kapandı (2026-10-01):** aşağıdaki "Fonksiyon içi `int` yereller" bölümü
  (`matmul_int` 586 → 121 ms).
- `X[j - 1]` biçimi (`-` ile ofset) plana girmiyor; `X[j + -1]` giriyor.
- `a[i] += x` (bileşik eleman yazması) float sürümünde reddediliyor.

## nbody: iç döngü sınavı dış döngü başına — 187,5 → 115,3 ms, C ile aynı (2026-10-01)

Önce ÖLÇ: en iç sürümün sınavı sabit `true`ya çekilince (deney ikilisi,
genel kopyalar ölü kod) nbody 187 → 115 ms = C. Yani kalan farkın tamamı
sınavdı, gövde değil. İki parça:

1. **Sınav yeri.** `advance`'te i döngüsü içindeki j döngüsü (0–4 tur) her
   girişte yedi dizinin deposunu (`tulpar.f64_probe`: VMValue + otype +
   idata + eb + count) ve 24 erişimin aralığını sınıyordu; genel kopya i
   döngüsünün İÇİNDE kaldığı için (çağrılı) LLVM başlıkları dışarı
   taşıyamıyordu. **İç içe sürüm** (`tulpar_float_nest_plan` +
   `fvn_try_version`): dış döngü O'nun gövdesi yalnız plan kabul eden en iç
   `for`lardan ve dizisiz deyimlerden oluşuyorsa, iç döngülerin sınavı O'nun
   başında BİR KEZ, i'nin [I0, UBo) aralığının uç noktalarıyla yapılıyor
   (J0 = `i + c` ve j'siz taban `i + c` i'de artan; j'li taban ve iç UB O'da
   değişmez). Tutarsa hızlı O gövdesinde iç döngüler sınavsız ve genel
   kopyasız; tutmazsa genel O gövdesi bugünkü gibi (iç döngüler kendi
   sınavlarıyla; sınır dışı orada hâlâ hata). Muhafazakâr: O'da iç döngü
   dışında dizi erişimi varsa (matmul'ün k döngüsü, energy) dokunulmuyor.
2. **Boşa doldurulan int şekil önbelleği.** `AST_FOR` her döngünün başında
   int dizi şekil önbelleğini (4 başlık okuması + yenileme yuvaları) float
   sürümünden ÖNCE kuruyordu; hızlı gövde onu hiç okumuyor. Artık float
   sürümlü döngüde yalnız genel gövdenin başında kuruluyor.

| (Ryzen 7 9800X3D, `taskset -c 10,11`, en iyi 5–11) | nbody ms |
|---|---:|
| önce | 187,5 |
| yalnız 2 (`TULPAR_NO_FVNEST=1`) | 172,4 |
| 1 + 2 | **115,3** |
| aynı düzenekte C (gcc -O2) | 114,8 |

Yalnız 1 (önbellek düzeltmesi olmadan) 140 ms ölçüldü: dış döngü başındaki
int önbellek dolumu tek başına ~25 ms'ydi. On üç kıyastan yalnız matmul ve
nbody'nin ikilisi değişti (öteki on bir bayt bayt aynı); matmul 37,2 → 37,1
(15 tur). Denenip bırakılan: f64 sondasının yüklemelerine TBAA etiketi
(ikinci döngünün sondalarını CSE'lesin diye) — 141,4 → 140,0, gürültü.
Kapılar: `tests/float_ic_ice.sh` (IR: `fvn_fast` / `fvn_ic_done`, dallanma
öncesi `shape.ty` yok, matmul biçimi dokunulmuyor; `TULPAR_NO_FVNEST=1`
pozitif kontrol; üç sabotajla kırmızı) ve `tests/float_ic_ice.test.tpr`
(7 test: nbody biçimi elle hesapla, iki iç döngü + adım 2 + `<=`, `len()`
sınırı ve parametre dizileri, uç nokta tutmayan dört durumda genel yol +
hata).

## particles: üst düzey global + struct dizisi başlığı — 336 → 55 ms (2026-10-01)

Hız karnesinin oyun döngüsü çekirdeği (`benchmarks/fair/particles`, 1M
parçacık × 50 adım, `P[] ps` üzerinde `ps[i].vy = ps[i].vy - g * dt` …) C'nin
7,8 katıydı; aynı kod bir fonksiyona alınınca 4,4 katı. İki ayrı maliyet,
ikisi de IR'dan okundu (`TULPAR_AOT_EMIT_LL=1`):

1. **Üst düzey değişkenler LLVM global'iydi.** `float dt/g/w/sum` kutulu
   VMValue global'i: döngüde `g * dt` her turda iki etiket okuyor, etiket
   makinesi (int/float/geri düşüş) ve `vm_binary_op` çağrısı üretiyordu; `ps`
   global'i her erişimde yeniden yükleniyordu. Fonksiyonda aynı değişkenler
   alloca → SROA etiketi sabite katlıyor (`g * dt` → `-9.81e-2` sabiti).
2. **Struct dizisi erişimi başlığı her seferinde okuyordu** (tür, count,
   data): alan saklaması `store double` etiketsizdi, yani LLVM için başlığı
   da yazabilirdi; üstelik döngüdeki yavaş yol çağrısı (`aot_sarr_elem_ptr`)
   başlık yüklemelerinin döngü dışına çıkmasını engelliyordu. Her erişim
   ayrıca dizi tutamacını 16 baytlık bir yuvaya yazıyordu (yalnız yavaş yol
   kullanıyor).

Yapılanlar ve pay (taskset -c 6,7, 7 koşu en iyi, aynı turda):

| | üst düzey | fonksiyon içi |
|---|--:|--:|
| eski | 336,2 | 193,1 |
| TBAA "elem" + spill yalnız yavaş yolda | 294,8 | — |
| + önbellek (terfi yok) | 178,3 | — |
| + terfi (önbellek yok) | 119,7 | 121,3 |
| **hepsi** | **54,8** | **54,8** |
| C (gcc -O2) | 42,7 | |

- **Main yereline terfi** (`main_local_declare`): adı hiçbir fonksiyonda /
  lambdada / modülde geçmeyen üst düzey bildirim alloca. Muhafazakâr tarama:
  adın GEÇMESİ yeter (okuma, yazma, gölgeleyen yerel, çağrı adı). Üst düzey
  `try` gövdesindeki adlar ilk sürümde global kalıyordu (setjmp/longjmp
  arasında değişen yerel eski değerine dönüyordu, [[Tuzaklar]] 7i); hata
  2026-10-01'de düzeldi (`try_volatile_finalize`) ve kısıt kalktı: adım
  döngüsü üst düzey `try`a sarılmış particles (N=200k) 35,7 → 10,9 ms
  (try'sız 11,3). Güvenlik ağı: terfi edilmiş yuvaya main dışından erişilirse derleme
  "iç hata" ile durur (geçersiz IR üretilmez — doğrulama hatası emit'i
  durdurmuyor).
- **`int` ve dizi global kalıyor — ölçüldü.** İlk sürüm hepsini terfi
  ediyordu ve elek 7,6 → 9,4 ms geriledi. Yalnız int'leri global tutmak 9,5,
  yalnız dizileri 9,6, ikisini birden 7,6. Sebep LSR: dış döngü sayacı, sınır
  ve dizi tutamacı yazmaca girince soğuk yolun (kutulu / 64-bit eleman) adres
  hesapları sıcak iç döngüye ayrı sayaç olarak taşınıyor (iç döngü 4 → 8
  komut). Aynı elek fonksiyon içinde ESKİ derleyicide de 9,7 ms — yani elek'in
  üst düzeyde hızlı olması global'in bellek trafiğinin LSR'ı durdurmasından
  geliyordu, açık dizi erişim yolunda. `TULPAR_MAIN_LOCALS_ALL=1` o işi
  ölçmek için hepsini terfi ettirir. Float dizisi (#432) ve `call()` (#430)
  birleştikten sonra yeniden ölçüldü: hepsini terfi etmek elek 8,0 → 9,6,
  `callfn` 173 → 188 ms; kuralla ikisi de kapalı derlemeyle eşit.
- **Struct dizisi şekil önbelleği** (`SarrCacheScope`): ObjArray
  önbelleğinin kanıtı aynen (`tulpar_loop_shape_stable` + ad yeniden
  bağlanmıyor); başlık döngü başında okunur, struct dizisi değilse count 0 —
  her erişim yavaş yola, aynı tanıya gider. Ayrı yığın; ObjArray
  önbelleğinin 4 girdisine ve sürümleme kanıtına karışmıyor.

Kalan 12 çekirdekte gerileme yok (en iyi oranı yeni/eski 0,98–1,03; `hashmap`
21 koşuda gürültü içinde eşit). Motor deposunun yedi örnek oyunu, köprüyü
geri bağlayan derleyiciyle penceresiz 30 kare: çıkış kodu, kapanış raporu ve
son karenin PPM özeti eski derleyiciyle birebir; `engine_bridge.test.tpr`
27/27.

Kalan açık: `particles` C'nin 1,3 katı. İç döngüde tur başına bir `i <u
count` sınavı kalıyor (sınır `n`, `len(ps)` değil — struct dizisi için
kanıtlı erişim yok); kalan farkın nerede olduğu ölçülmedi.

## `try` doğruluğu: volatile yerel — maliyet ölçüldü (2026-10-01)

[[Tuzaklar]] 7i'nin düzeltmesi (`try_volatile_finalize`) try gövdesinde
yazılıp dışarıda okunan yerelin bütün erişimlerini `volatile` yapıyor. Bu
tasarım gereği bellek trafiği ekler; try kullanan kod yavaşlıyor mu diye dört
mikro ölçüm (Ryzen 7 9800X3D, `taskset -c 10,11`, en iyi 5, eski ve yeni
derleyici kendi izole dizinlerinde):

| ölçüm | ne | eski | yeni |
|---|---|---:|---:|
| `tloop` | fonksiyonda try içinde 100M turluk float toplama, akümülatör try DIŞINDA bildirilmiş (volatile) | 230,6 | 207,2 |
| `tdizi` | try içinde 200×500k `a[i] = a[i] + 1.0; acc = acc + a[i]` | 331,7 | 274,8 |
| `tcall` | 20M kez try'lı küçük fonksiyon, her 1000'de throw | 898,1 *(yanlış sonuç)* | 906,8 |
| `ttop` | `tloop` üst düzeyde (akümülatör artık main yereli) | 225,1 | 212,8 |

`tcall`'da eski derleyici YANLIŞ toplam veriyor (catch, try'da yazılan
`r`'yi eski değeriyle görüyor). `tloop`/`tdizi`'de volatile sürüm daha
hızlı çıktı; bu bir kazanç iddiası DEĞİL (kutulu yerelin SROA'lı hâli ile
volatile hâlinin makine kodu farklı, sebep incelenmedi) — iddia yalnız
"gerileme yok". Try'ın kendisi pahalı: `tcall`'da çağrı başına ~45 ns
(`aot_try_push` + `setjmp`); sıcak döngüde try açıp kapatmak hâlâ önerilmez.

Değerlendirilip bırakılan: alloca'yı bir kez kaçırmak (adresi bir
global'e). Bütün çağrılar bariyer olur, çağrısız döngüde değer yazmaçta
kalabilir — teoride daha ucuz; ölçüldü: `tloop` 244,4, `tdizi` 311,1 ms
(volatile'dan yavaş: kaçan alloca her bilinmeyen saklamayla örtüşebilir
sayılıyor). On üç adil kıyasın ikilisi düzeltmeden önce ve sonra bayt bayt
AYNI (hiçbiri try kullanmıyor).

## Geçici dizgiler: sözlük anahtarı ve `toString` birleştirmesi — hashmap 439 → 118 MB (2026-10-01)

Hız karnesinin `hashmap` çekirdeği hash indeksinden (#428) sonra hızlıydı ama
439 MB tepe bellek ölçüyordu (C 65). Ekleme başına ölçülen yol
(`m["k" + toString(i)] = i`, kalıcı `m`): `toString` 64 B arena + birleştirme
64 B arena + `vm_object_set`'in anahtar kopyası 64 B arena + kalıcı kap
olduğu için bir de malloc kopyası (~80 B) + tablo. Arama başına yine iki
64 B'lık geçici. Hiçbiri geri alınmıyordu: arena yalnız checkpoint geri
sarmasıyla küçülüyor ve döngüde checkpoint yok.

Dört ayrı düzeltme, her biri ayrı ölçülebilir:

1. **Birleştirme zorlaması ayırmıyor.** `aot_string_concat_fast` dizgi
   olmayan tarafı `aot_to_string` ile arenaya kurup kopyalıyordu; artık
   `aot_value_text` (toString ile AYNI fonksiyon) yığındaki tampona yazıyor.
   Codegen `S + toString(x)`'i `S + x` olarak buraya getiriyor. Eşitlik her
   S için geçerli (toString(x) dizgi olduğundan `+` hep birleştirme, o da iki
   tarafı aynı kuralla zorluyor) — iki taraf da toString ise ikisi de
   açılıyor. Kutusuz struct (`Ad { ... }` biçimi) ve kullanıcı tanımlı
   `toString` hariç.
2. **Geçici anahtar geri bırakılıyor.** Anahtar ifadesi SAF ise (literal,
   değişken, `+ - * / %`, tekli eksi, toString — çağrı, dizi erişimi, atama
   YOK: `arr_debox` okuma yolunda dizi başlığına yazabiliyor) codegen
   anahtardan önce `aot_arena_mark`, erişimden sonra runtime yardımcısı
   `aot_arena_release`. Okuma hiçbir şey tutmaz; yazma yalnız anahtar
   zaten varsa ya da kalıcı kabın malloc kopyası alındıysa bırakır.
   Checkpoint değil: bölgeye ve yığına dokunmuyor, yalnız arena ucunu geri
   çekiyor (bloklar arası geçişte `aot_arena_rewind_to` ile aynı değişmez).
3. **Anahtar kopyası.** Önce arama — mevcut anahtarda hiçbir şey
   ayrılmıyor. Yeni anahtar tek kopya; dizgi NESNESİ verildiyse (`m[k] = v`
   yolu, `vm_object_set_key`) ve ARC'nin asla serbest bırakmayacağı bir
   dizgiyse (arena ya da ölümsüz interned literal) kopya hiç yok. Sıradan
   malloc dizgisi (ref_count 1) kopyalanıyor: async'in `arc_free_object`'i
   anahtarları bırakıyor.
4. **Yerleşmiş arena dizgisi.** `aot_arena_reset`/`aot_arena_destroy`'un
   çağıranı yok; geri sarma yalnız bir `arena_save`'in kaydettiği uca kadar
   gidiyor. Yani bu thread'de açık checkpoint YOKKEN arenanın canlı kısmında
   (ucun altında) duran nesne bir daha geri alınamaz. Yazma bariyeri bunu
   bilmiyordu ve kalıcı kaba yazılan her arena dizgisini kopyalıyordu.
   Artık yalnız DİZGİ için (değişmez, çocuksuz — paylaşım gözlemlenemez)
   kopya yok. "Ucun altında" şartı ölü dizgiyi ayırıyor: `arena_drop` ile
   bırakılmış ama üstüne yazılmamış dizgi ucun ÜSTÜNDE, eskisi gibi
   kopyalanıp kurtarılıyor. Başka thread'in arenası aranmıyor (kopya), blok
   yürüyüşü 64 adımla sınırlı (aşılırsa kopya).

| (Ryzen 7 9800X3D, `taskset -c 6,7`, izole dizin, dönüşümlü A/B) | taban `d6ad680f` | yeni |
|---|---:|---:|
| `hashmap` en iyi (15 tur) | 196,8 ms | 122,2 ms |
| `hashmap` ortanca (15 tur) | 230 ms | 162 ms |
| `hashmap` tepe bellek | 439 MB | 118,8 MB |
| `parse` en iyi / tepe bellek | 125,6 ms / 440 MB | 124,7 ms / 412 MB |
| `strcat` tepe bellek | 24,7 MB | 17,4 MB |
| kapı: 2,9M ek arama | +354 MB | +0 MB |
| kapı: anahtar başına (iki tur yazma) | 602 B | 122 B |

Makine başka derlemelerle paylaşımlıydı (taban hashmap o gün 170 değil 197
ms); oran dönüşümlü koşumdan.

**`callfn` 174 → 213 ms bir gerileme DEĞİL.** IR bayt bayt aynı; runtime'ın
`.cold` bölümleri (kullanıcı kodundan ÖNCE bağlanıyor) büyüdüğü için `main`
32 bayt kaydı (`main % 64`: 48 → 16). Kaynağa bir yardımcı fonksiyon
eklenince taban 195,5, yeni 173,8 ms; 0–5 ek fonksiyonla iki derleyici de
173–213 aralığında geziyor. `call()` döngüsü kod yerleşimine bu kadar
duyarlı — `callfn` sayısını tek koşumdan okumayın (Tuzaklar 7i ailesi).

**Başlık küçültme — yarısı yapıldı (aynı gün, ayrı PR).** `parse`'ın kalan
412 MB'ı temsil: parça başına 64 B (56 B `ObjString` + ~6 karakter, 8'e
yuvarlı). `ObjString::capacity` yalnız YAZILIYORDU (dizgiler değişmez; okuyan
tek satır yok — alan silinip derlenince çıkan hataların hepsi atamaydı).
Kaldırıldı, `hash` dolgu boşluğuna kaydı: 56 → 48 B (wasm32 36 → 32). Boy
codegen'e gömülü tek yerde, `AOTFnRef` (`call()` satır içi yolu: `fp`@48,
`arity`@56, `nfp`@64, 72 B; #435 sonrası düzen @56/@64/@72, 80 B idi) — static_assert'ler iki genişlikte de kilitli,
`tests/split_toplu.sh` beklenen parça boyunu 48'den hesaplıyor (kapı boyu
ölçüyor). Pozitif kontrol: codegen'in `fp` ofseti eski 56'da bırakılınca
`call_fnref.test.tpr` özete varmadan çöküyor. Ama `nfp` ofseti eski 72'de
bırakılınca (#435 üstüne yeniden temellendirirken tam bu oldu) HİÇBİR kapı
kırmızı olmadı: 72 bir sonraki kaydın tip alanına (0) düşüyor, `nfp` null,
yerel int yolu sessizce tutmuyor — doğru ama yavaş. Kök neden sayıların iki
dosyada ayrı yazılması; runtime kilidi yalnız kendi tarafını görüyordu.
Artık ikisi de `src/vm/fnref_layout.h`'yi okuyor (başlıkta yanlış sayı →
runtime static_assert derlemeyi kırıyor, denendi).

| (taban `e3c601f1`, dönüşümlü, en iyi 5) | 56 B | 48 B |
|---|---:|---:|
| `parse` tepe bellek | 412,3 MB | 374,1 MB |
| `parse` | 124,7 ms | 122,5 ms |
| `hashmap` tepe bellek | 118,3 MB | 111,1 MB |
| `callfn` ortanca (15 tur ×2) | 81,7 · 81,6 ms | 81,4 · 81,9 ms |

Yapılmayan yarı: `Obj`'u yeniden sıralamak (32 → 24, `next`'in iki yanındaki
dolgu) bir −40 MB daha verir ama `ObjArray`/`ObjStructArray` ofsetlerini
(codegen `obj_header_pad` 28/16, satır içi dizi yolları, wasm32 düzeni)
değiştiriyor; o yollar aynı gün paralel bir dizi işinde değişiyordu.

Test: `tests/gecici_dizgi.test.tpr` + `tests/gecici_dizgi.sh` (bkz. CHANGELOG).

## Fonksiyon içi `int` yereller: döngüde native gölge + `int[]` iç içe döngü sürümü — qsort 122 → 70 ms (2026-10-01)

Hız karnesinde `qsort` C'nin 2,1 katıydı (122,8 ms / 57,4; dokuz dil arasında
8.), aynı kod `int[]` ile matmul yazılınca fonksiyon içinde 586 ms. Önce
ölçüldü (Ryzen 7 9800X3D, `taskset -c 2,3`, 5 tekrarın en iyisi):

| | ms |
|---|--:|
| qsort (kıyasın kendisi: `qs` fonksiyonu) | 122,7 |
| aynı bölme, özyinelemesiz, ana programda — değişkenler blok içinde | 127,0 |
| aynı, değişkenler ÜST DÜZEYDE (main yereli → native, #431) | 84,9 |
| C (`gcc -O2`) | 57,4 |

Yani maliyetin büyüğü dizide değil **tamsayı değişkenlerde**. IR
(`TULPAR_AOT_EMIT_LL=1`) nedenini gösterdi: fonksiyon içindeki `int i = lo;`
kutulu bir VMValue yuvası. `int` bildirimi değeri ZORLAMIYOR — `int k =
d["x"]` bir dizgi tutabilir, `k + 1` `"abc1"` verir (ölçüldü; bugünkü anlam,
değişmedi) — o yüzden codegen her `i + 1`, `a[i] < p`, `i <= j` için iki
etiket okuması + tür dallanması + `vm_binary_op` geri düşüş çağrısı üretiyor.
SROA etiketleri phi'ye çeviriyor ama geri düşüş yolunun dinamik etiketi her
phi'ye karışıyor, katlanamıyor.

**Int yerel gölge sürümü** (`llvm_array_shape.cpp` `tulpar_int_local_plan`,
codegen `iv_try_version`): en dış uygun `while`/`for` döngüsü için kanıt —
adın döngüdeki HER bağlanması kesin INT (int sabiti, kanıtlı ad, native int,
`+ - *`, tekli eksi, kanıtlı `int[]` okuması), `int[]` okuması ancak dizi
`int[]` ipuçlu, döngüde yeniden bağlanmıyor, döngü şekil-kararlı ve HER eleman
yazması kesin INT ise kanıtlı (takma ad: başka bir ad aynı diziyi
gösterebilir). Sayısal kısım döngü başında bir kez: adların etiketi INT,
okunan dizilerin deposu kutusuz int (`tulpar.int_probe`). Tutarsa hızlı kopya
(adlar native i64 gölgede; `LocalVar` geçici olarak native'e çevriliyor,
çıkışta kutulu yuvaya geri yazılıyor), tutmazsa bugünkü kod. Reddedilen:
kapanış/try/match/await/for-in içeren döngü; döngüde bildirilen ad bir `for`
kapsamında değilse fonksiyonda döngü dışında geçmemeli (`while` kapsam
açmıyor).

**Int dizi döngü sürümü** (float planının int kipi, `tulpar_int_loop_plan`):
iç içe döngünün en içteki `for`u, `X[B + j]` erişimleri ve kesin INT TEK
eleman yazmasıyla; döngü başında sınır + 32-bit int depo (`tulpar.i32_probe`)
+ değişmez adların INT etiketi. Hızlı gövdede erişim tek GEP + i32
load/store. i32'ye sığmayan yazma YAPILMADAN genel gövdenin koşuluna
atlanıyor (tur orada baştan, dizi genişliyor) — bu yalnız yazma gövdenin ilk
bildirim-dışı deyimiyse doğru (K201'in kuralı).

Ölçülüp atılanlar:
- **Genişlik dalını kaldırmak** (hızlı kopyayı 32-bit varsayımıyla dalsız
  üretmek, `shape_want32 = 1`): qsort 70,0 / 70,6 ms — kazanç yok, dal iyi
  tahmin ediliyor. Gönderilmedi.
- **Kesin-INT dizi okuması olmayan döngüyü de kopyalamak:** kazanç tur
  başına bir etiket sınavı; bedel kod. `scene3d_editor` derlemesi 10,4 →
  11,6 s, ikili %2,4 büyüdü. Plan artık en az bir kanıtlı okuma istiyor:
  derleme 10,42 s (taban 10,48), ikili tabanla aynı boyutta. Ayrıca
  1500 düğümden büyük döngü kopyalanmıyor (qsort'un bölme döngüsü 56,
  int matmul'ün dış döngüsü 66 düğüm).

Sonuç (dönüşümlü A/B, taban `d6ad680f`, ikisi de repo DIŞINDA kendi runtime
arşiviyle, `taskset -c 2,3`, en iyi):

| | önce | sonra | C |
|---|--:|--:|--:|
| `qsort` (1M, 21 tur) | 122,7 | **70,0** | 57,4 (2,14× → 1,22×) |
| `int[]` matmul fonksiyonda (N=640, 21 tur) | 585,6 | **120,5** | 69 (int64) · 41 (int32) |

Gerileme denetimi, 13 çekirdek, 7 tur: **11'inin ikilisi tabanla bayt bayt
aynı** (`cmp`) — `intloop` 135,00/135,01 · `fib` 0,65/0,60 · `sieve`
8,06/8,24 · `strcat` 13,52/13,62 · `mandelbrot` 158,48/158,41 · `matmul`
37,03/37,31 · `nbody` 187,26/187,46 · `hashmap` 198,12/188,60 ·
`particles` 52,82/52,67 · `callfn` 170,39/169,11 · `parse` 125,13/124,52;
bu satırlardaki ±%2–5 aynı ikilinin gürültüsü (elekteki %2,2 dahil — bkz.
[[Tuzaklar]] 7i). Değişen ikisi: `qsort` ve `arrayiter` (1,37 → 1,33; 41
turda 1,28/1,30 — 5M'de süreç başlatma baskın; 50M'de 16,29 → 15,55).

Kalan:
- qsort 1,22× C. Bölme döngüleri C'ninkine yakın (sınır `i <u count` +
  genişlik dalı + yükleme); fark büyük olasılıkla çağrı başına: 863 199
  özyinelemeli çağrının her biri kutulu ABI, 1,2 KB'lık yığın çerçevesi,
  `a[(lo + hi) / 2]` genel okuma, şekil doldurma ve gölge sınavı ödüyor
  (ölçülmedi — perf bu makinede yok).
- int matmul 1,7× C (int64): iç döngü i32 sığma sınavı (deopt) yüzünden
  VEKTÖRLEŞMİYOR. Sınavı döngü dışına almak (aralık kanıtı) ayrı iş.
- `int[]` ipucu ADA bağlı (float sürümüyle aynı): programda bir yerde
  `int[] a` varsa her `a` aday; doğruluk depo sınavından geliyor.

## `parse`: `Obj` başlığı 32 → 8 B, tek geçişli `split`, iki haneli `itoa` — 121 → 75 ms, 374 → 261 MB (2026-10-02)

Karnenin en büyük kalan açığı `parse` idi: C'nin 2,14 katı, tepe bellek
374 MB (C 32). Önce aşama aşama ölçüldü (Ryzen 7 9800X3D, `taskset -c 2,3`,
programın içinde `time_ms()` ile; taban 9dddaf38):

| aşama | taban | C (aynı iş) |
|---|---:|---:|
| metni kur (`sb_append` ×10M) | 41 ms | 27 ms |
| `sb_tostring` | 2 ms | — |
| `split` (5M parça) | 55 ms | — |
| `toInt(parts[i])` döngüsü | 21 ms | 29 ms (`strtol` yürüyüşü) |

Üç ayrı maliyet, üç ayrı düzeltme:

**1. Bellek: `Obj` başlığı 32 → 8 bayt.** Eski düzen `ObjType type` (4) +
dolgu (4) + `Obj *next` (8) + `arena_allocated` / `ref_count` / `is_moved`
ve dolguları idi. `next` silinmiş VM'in nesne listesiydi: AOT'ta yalnız
`nullptr` yazılıyordu, okuyan tek yer `vm_create`'i hiç çağrılmayan VM.
Kaldırıldı; tür tek bayt (`enum : uint8_t`), üç bayt alan yan yana, `ref_count`
@4. Başlık **her hedefte** 8 bayt (wasm32'de de; eskiden 20). `ObjString`
48 → 24 B (wasm32 32 → 20), `ObjArray` 64 → 40, `ObjStructArray` 80 → 56.
5 haneli bir `split` parçası 56 + 16 → 32 + 16 bayt.

Başlık boyu codegen'e gömülü (ObjArray/ObjStructArray GEP'leri ve satır içi
tür sınavları). `src/vm/obj_layout.h` tek kaynak (fnref_layout.h dersiyle):
codegen dolguyu (`HEADER - TYPE`) ve tür yüklemesinin genişliğini oradan
okuyor, runtime `static_assert`'leri struct'la sayının aynı olduğunu
kilitliyor. Sabotajla denendi: başlığı 16 ya da tür genişliğini 4 yazmak
derlemeyi kırıyor. Tür sınavı yapan 10 site tek yardımcıdan geçiyor
(`llvm_load_obj_type`); biri hâlâ `load i32` yapsaydı komşu baytları
(arena, is_moved, ilklenmemiş dolgu) da okur ve satır içi yol SESSİZCE
kapanırdı — `tests/obj_baslik.sh` üretilen IR'de bütün tür yüklemelerinin
`i8` olduğunu ve IR tiplerinin `{ i8, [7 x i8], ... }` olduğunu denetliyor
(eski derleyicide kırmızı: 14 yüklemenin 14'ü `i32`). Yan bulgu: gdb
pretty-printer'ı (`tools/gdb/tulpar_printers.py`, `tulpar debug`'a gömülü)
düzeni elle yazılı tutuyordu; DAP denetimi değerleri `<4143972352 @0x…>`
diye okuyunca kırmızıya döndü (kapı işini yaptı), düzeltildi.

**2. `split`: tek baytlık ayırıcıda parça başına bir arama.** Eski iki
geçiş parça başına iki `memchr` çağrısı yapıyordu (ilki boyu toplamak için).
~6 baytlık parçada çağrı işin kendisinden pahalı. Yeni: 1. geçiş yalnız
ayırıcıyı SAYAR (dallanmasız, derleyici vektörleştiriyor), ayırma ÜST
SINIRLA yapılır (`sayı × (sizeof(ObjString) + 8) + karakterler`), 2. geçiş
parçaları kurar ve artan kuyruk arenaya iade edilir (`aot_arena_trim_last`;
arena thread_local, ayırma son ayırmaysa `used` geri çekilir). Ayırıcı
araması ilk 32 bayt için 8'er baytlık kelimeyle (SWAR), sonrası `memchr` —
uzun satırlı CSV'de vektörlü `memchr` korunur. Tanı satırı iade sonrası boyu
basıyor; `tests/split_toplu.sh` beklenen boyu kendisi hesaplıyor ve kuyruğun
iade edildiğini sınıyor (iade kapatılınca "kuyruk iade hayır" → kırmızı;
sabotajla denendi). İade RSS'i değiştirmiyor — dokunulmayan kuyruk zaten
yerleşik değil — arenanın sonraki ayırmaları için boşluğu geri veriyor.

**3. Metni kurma: iki haneli `itoa`.** Aynı kodun izole C düzeneğinde 17 ms,
runtime'da 30 ms sürdüğü sanıldı; ölçüm hatasıydı (düzenek yanlış dalı
koşuyordu) ve asıl fark algoritmadaydı: eski `aot_itoa` her haneyi bir
`% 10` / `/ 10` ile geçici tampona tersten yazıp ters kopyalıyordu. Yeni:
önce hane sayısı (karşılaştırmayla), sonra hedefe sondan başa, her adımda
`% 100` ve "00".."99" tablosu. 5M beş haneli `sb_append(sb, int)`: 30 →
17 ms. `toString(int)` ve `"x" + i` aynı fonksiyondan geçiyor
(`tests/itoa_esdeger.test.tpr`: aot_itoa'dan bağımsız Tulpar başvurusuyla
~40 bin değer × üç yol; bir tablo hücresini bozan sabotaj kırmızı). Ayrıca
`sb_append(sb, "<literal>")` artık havuz dizgisi + VMValue + tür sınavı
yerine doğrudan `aot_stringbuilder_append(sb, baytlar, uzunluk)`: 5M `","`
ekleme 13–15 → 10–11 ms.

Aşama aşama (aynı düzenek, `parse` en iyi 5):

| adım | `parse` | tepe RSS | split | metin kur |
|---|---:|---:|---:|---:|
| taban (9dddaf38) | 120,6 ms | 374,3 MB | 55 ms | 41 ms |
| + `Obj` 8 B | 114,7 ms | 259,6 MB | 50 ms | 41 ms |
| + tek geçişli split | 90,7 ms | 260,4 MB | 26 ms | 41 ms |
| + iki haneli itoa + literal ekleme | **75,2 ms** | **260,5 MB** | 25 ms | 27 ms |
| C (gcc -O2) | 57,0 ms | 31,7 MB | | 27 ms |

Bellek kapısı (`tests/split_toplu.sh`, rsswrap ile aynı 1M parçalık metnin
split'li ve split'siz tepe RSS farkı): 5 baytlık parça başına **49 B** (32
nesne + 16 eleman; eşik 60), eski derleyicide **71 B** (kırmızı). Kapının
pozitif kontrolü: 21 baytlık parça +15 B ölçülüyor (nesne 32 → 48; en az 10
beklenir) — kapı gerçekten parça boyunu ölçüyor. İlk sürüm metni
`StringBuilder` ile kurup 100k / 1M farkına bakıyordu; Linux'ta doğru
(54 B / +16) ama macOS CI'da 61 B / +8 ölçtü — bırakılan büyüyen tampon
split'e yeniden veriliyor ve fark ölçümü bozuluyordu. Metin artık `repeat`
ile tek seferde, serbest bırakılmadan kuruluyor ve iki koşum yalnız split'te
ayrışıyor.

Gerileme denetimi (13 çekirdek, dönüşümlü A/B, en iyi 5, aynı gün): intloop
134,7/134,8 · fib 0,6/0,6 · mandelbrot 158,4/158,5 · matmul 37,0/37,2 ·
nbody 114,9/115,1 · particles 52,8/52,7 · arrayiter 1,3/1,3 (15 tur) —
aynı. Değişenler: **strcat 13,5 → 9,9** (itoa), **callfn 79,9 → 74,9**,
**hashmap bellek 111 → 88 MB** (süre 130,4/130,1, 15 tur). `sieve` 7,6 →
7,8 ve `qsort` 70,0 → 71,5 göründü; makine kodu yalnız `cmpl`→`cmpb` ve alan
ofsetlerinde farklı, kaynağa zararsız önek konunca fark kayboluyor ya da
tersine dönüyor (qsort önekli: 68,9/70,3 · 70,1/69,6 · 70,1/69,1; sieve
önekli: 7,6/7,7 · 7,6/7,5 · 7,6/7,6) — [[Tuzaklar]] 7i, yerleşim, kod değil.

wasm32 düzeni: emsdk bu makinede yok; `vm.hpp` `clang++
--target=wasm32-unknown-unknown -ffreestanding -fsyntax-only` ile (en az
`cstdint`/`cstdlib` şimiyle) derlenip `ObjString` 20 / `ObjArray` 28 /
`ObjStructArray` 40 ve bütün alan ofsetleri `static_assert`'le doğrulandı;
eski 32 iddiası kırmızı.

Kalan (yapılmadı):
- Parça hâlâ 32 B nesne + 16 B eleman. Sonraki adımlar temsil değişikliği:
  karakterleri nesnenin içine alan (işaretçisiz) `ObjString` (−8 B, ama
  `chars`'a dokunan her yer), ya da dizgi dizisi için 8 baytlık işaretçi
  deposu (−8 B/eleman; okuma yollarının hepsi `arr_items`'tan geçiyor ve
  deboxing'e döner) — ikisi de geniş ABI işi.
- `toInt` döngüsü 20 ms, büyük ölçüde 240 MB'ı baştan sona okumak.


## hashmap: indeks büyük tabloda iki kat, büyümede yuva taşıma — 104 → 89 ms, 88,6 → 72,6 MB (2026-10-02)

Resmî koşum (2026-10-02, main a1d5688f): `hashmap` 106,1 ms / 88 MB, C 64,3 /
64. Bu makinede `perf` yok ve `ptrace_scope=1` (gdb süreç ekleyemiyor); ölçüm
`LD_PRELOAD` ile yüklenen `ITIMER_PROF` örnekleyicisiyle (her ~1 ms CPU'da
RIP, `nm` ile çözüm) ve çekirdeği parçalara bölerek yapıldı (Ryzen 7
9800X3D, `taskset -c 6,7`, izole dizin, en iyi / 9 tur):

| parça (1M) | taban |
|---|---:|
| yalnız anahtar kur (`"k" + toString(i)`, `len`) | 13,9 ms |
| kur + ekle | 43,6 ms |
| kur + ekle + ara (tam çekirdek) | 100,5 ms |

Arama eklemeden pahalı. Arama ağırlıklı varyantta (1M ekle + 20M ara)
örnekler: `obj_find` %65 — bunun 716/1465'i TEK komut, indeks yuvasının
yüklemesi (`mov 0x18(%rcx,%rax,8)`: 32 MB indekste önbellek ıskası) —,
libc `strcmp` %21 (157 örnek ilk yüklemede: saklı anahtar dizgisinin ıskası),
FNV döngüsü ~%7, anahtar kurma (`aot_itoa` + birleştirme) ~%7. Yani süre
bellek: indeks 4M yuva × 8 B = 32 MB + keys 8 + values 16 + dizgiler 32 = 88 MB
çalışma kümesi (L3 96 MB).

Kök neden indeksin büyüme kuralı: her büyümede kapasite `>= 4 × anahtar`
(1M'de 4M yuva, doluluk 0,24) ve her büyümede bütün anahtarlar baştan
hash'leniyordu (`keys[p]` → `ObjString` → `chars`).

Denenen adımlar (aynı düzenek):

| sürüm | 1M | 1M bellek | 300k | ekleme 300k |
|---|---:|---:|---:|---:|
| taban | 103,8 ms | 88,6 MB | 25,3 ms | 13,2 ms |
| v1: her boyda x2 + yuva taşıma + hash bir kez | 94,8 | 72,9 | 28,2 | 16,3 |
| v2: v1 + 8 baytlık sözcük hash'i (fmix64) | 106,6 | 72,7 | — | — |
| v3: v1 + `calloc` (boş yuva = 0) | 96,9 | 72,5 | 28,7 | 16,5 |
| **v4: v3 + 1M yuvanın altında x4** | **88,9** | **72,6** | **25,5** | **13,1** |
| C (gcc -O2) | 66,6 | 64,5 | | |

- **v1'in 300k gerilemesi** (+%26 ekleme) yükle ilgili değil: 300k'de iki
  kural da 1M yuvada bitiyor (benzetim: arama yoklaması ikisinde 1,20). Fark
  büyüme SAYISI: x2 kuralı iki kat yeniden kurma ve her seferinde taze
  bellek (sayfa hatası + sıfırlama); örneklemede `obj_index_note` 81 → 149.
  Küçük tabloda (8 MB altı) x4 korununca kayboldu; büyük tabloda bellek
  önemli, x2 kaldı.
- **v2 daha yavaş.** FNV-1a'nın son adımı `(X ^ c) * P`: ortak önekli
  anahtarlar (`k12340`..`k12349`) birbirine ~403 yuva uzaklıkta, yani
  sıralı ekleme/arama aynı birkaç sayfada geziyor. İyi karıştıran hash bu
  yerelliği yok ediyor. C kıyası da FNV-1a kullanıyor; değişmedi.
- `calloc` tek başına ölçülebilir fark vermedi (v1 → v3); kaldı çünkü büyük
  tabloda sıfır sayfalarını doldurma geçişini kaldırıyor.

N taraması (taban → v4, en iyi): 100k 8,7 → 9,0 ms (indeks boyu aynı; fark
yerleşim/gürültü), 300k 25,3 → 25,5, 700k (v1) 61,9 → 65,1 ms ama 75 → 59 MB
(x2 rejimine yeni girmiş), 1M 103,8 → 88,9, 3M (v1) 613 → 510 ms, 309 → 245
MB. Arama ağırlıklı varyant: örnek sayısı 1465 → 996 (−%32).

Gerileme denetimi (13 çekirdek, taban 6d0dc633 → v7 [v4 + tanı], dönüşümlü,
7 tur, en iyi; makine o saatte paralel ölçümlerle paylaşımlıydı, bellek
yoğun çekirdeklerde mutlak sayılar şişik, oran dönüşümlü koşumdan): intloop
138,3/138,4 · fib 1,5/1,5 · sieve 8,9/9,0 · strcat 11,0/11,1 · arrayiter
2,2/2,2 · mandelbrot 162,0/162,1 · matmul 38,5/38,4 · nbody 118,1/118,0 ·
particles 55,2/55,8 · parse 76,5/76,7 (261,6 MB ikisinde) · **hashmap
150,9 → 113,5** (88,4 → 72,6 MB). Kullanıcı nesnesi 13'ünde de bayt bayt
AYNI (`.o` karşılaştırması). İki fark yerleşim: **qsort 71,4 → 73,5** —
`t_qs.f` makine kodu aynı, yalnız runtime'ın soğuk bölümü büyüdüğü için 32
bayt kaydı; kaynağa zararsız önek konunca (p2) taban da 73,7 ms, aynı
adreste (…500) iki derleyici aynı süre: p0 71,5/73,8 · p1 71,3/72,3 · p2
73,7/71,9 ([[Tuzaklar]] 7i). **callfn 82,6 → 77,0** aynı sebeple
"kazanç" sayılmıyor.

Kapı `tests/sozluk_indeksi.sh` (build.sh suites): `TULPAR_OBJ_TANI=1`
büyüme başına taşınan yuva / yeniden hash'lenen anahtar / en büyük indeks
boyunu basıyor (yalnız yeniden kurmada sayılır — sıcak eklemeye maliyeti
yok). 1M anahtarda 2 097 152 yuva + yeniden hash 16 (ilk kuruluş) ve 300k'de
1 048 576 iddia ediliyor; RSS ayağı anahtar başına 72 B (#454 satır içi
dizgi karakteriyle birlikte 64 B; eşik 73, Linux). Pozitif kontrol aynı
sondayı `TULPAR_OBJ_INDEKS_X4=1` (eski kural) ile koşuyor: 4 194 304 yuva,
90 B (#454 ile 82 B — eşik ikisinin ortasına bu yüzden çekildi). Taban derleyicide kırmızı (tanı yok, 90 B);
`kObjIndexBig = 1 << 30` sabotajında kırmızı (4 194 304, 90 B).
`tests/gecici_dizgi.sh`'nin ekleme ayağı da (iki tur yazma) 89 → 71 B
gösteriyor; eşiği (192) bu PR'da sıkılaştırılmadı, kendi kapısı var.

#454 (satır içi dizgi karakteri, önce birleşti) üstüne yeniden
temellendirildikten sonra (aynı gün, dönüşümlü 15 tur, makine paylaşımlı):
main cde985df 101,1 ms / 81,0 MB → bu dal **94,9 ms / 65,1 MB** — tepe
bellek C ile aynı (64,5 MB).

Kalan: aramanın yarısı hâlâ yuva ıskası + anahtar dizgisi ıskası — C ile
aynı yapı (C de `keys[h]` → dizgi). Anahtar kurma (`aot_itoa` + arena
birleştirme, ~7 ns) C'nin elle `kfmt`inden pahalı.

## `ObjString` karakterleri nesnenin içinde — parse 261,7 → 223,4 MB, 76,0 → 72,5 ms (2026-10-02)

#449'un "kalan" listesindeki iki temsil adımı kıyaslandı (parse'ta parça =
24 B nesne + ~6 B karakter → 32 B, + 16 B dizi elemanı):

| | (a) satır içi karakter | (b) dizgi dizisi için 8 B işaretçi deposu |
|---|---|---|
| kazanç | her dizgi −8 B (parse −38 MB **ölçüldü**; hashmap −8 MB) | yalnız dizi elemanı −8 B (parse −40 MB, hesap) |
| değişen sözleşme | 8 atama + ölü VM + ARC + `AOTFnRef` + LLVM tip gövdesi | `idata`'ya dördüncü eleman türü |
| sessiz bozulma riski | düşük: `chars`'a ATAYAN her yer derleme hatası verdi (esnek diziye atanamaz); okuyanlar değişmedi | yüksek: `idata`'ya dokunan 76 runtime + 57 codegen sitesi `elem_bits`'e bakmak zorunda (F64'te "idata = tamsayı" varsayımı bir kez kırıldı); her genel okuma (`arr_items`) diziyi kutuya çevirir — geçişte iki depo birden canlı |
| codegen | dizgi karakterine dokunmuyor (yalnız `call()` havuz ofsetleri) | parça okuması için yeni hızlı yol gerekir |

(a) seçildi. Codegen dizginin içine hiç bakmıyor: `struct.ObjString` yalnız
işaretçi tipi; karakterlerin ofsetini kullanan tek gömülü sayı `AOTFnRef`'in
(`call()` satır içi yolu) ofsetleri. Esnek dizili struct başka bir struct'ın
ortasında duramadığı için kayıt başlığı alan alan taşıyor + adın 88 baytı +
giriş noktaları (`fp`@104, `arity`@112, `nfp`@120, 128 B); sayılar
`fnref_layout.h`'de, runtime static_assert'leri başlık ofsetlerini
`ObjString`'le kilitliyor. 88+ karakterlik ad havuza girmiyor (yavaş ama
doğru yol, testli).

Aşama aşama ölçüm gerekmedi — tek mekanizma. Dönüşümlü A/B (taban
6d0dc633, 7 tur, en iyi): `parse` 76,0 → 72,5 ms, 261,7 → 223,4 MB; kaynağa
0/1/2 önek satırıyla 76,6/73,4 · 76,7/73,0 · 77,5/73,6 — kazanç yerleşimden
bağımsız (split'te 5M × 8 B daha az yazma). `hashmap` bellek 88,4 → 80,7 MB
(`"k123456"` anahtarı 32 → 24 B). Öteki çekirdekler (intloop 137,0/136,5 ·
fib 1,0/1,0 · sieve 8,1/8,2 · strcat 10,3/10,4 · arrayiter 1,7/1,8 ·
mandelbrot 160,5/160,6 · matmul 37,7/37,6 · nbody 118,3/118,3 · qsort
71,1/71,3 · particles 55,8/55,3) aynı; kullanıcı `.o`'ları callfn dışında
bayt bayt aynı. **callfn 80,4 → 66,8 bir kazanç DEĞİL:** `.o` yalnız havuz
ofsetlerinde farklı; 0–7 önek satırıyla taban {81,4 64,5 75,8 75,6 65,5 66,8
119,4 65,5}, yeni {67,3 119,4 64,9 65,3 66,6 81,6 65,4 76,2} — aynı dört
düzey, ikisinde de 119'luk kötü yerleşim var ([[Tuzaklar]] 7i; callfn'in
yerleşim duyarlılığı "Geçici dizgiler" bölümünde de ölçülmüştü).

Kapılar: `tests/split_toplu.sh` (S = 16, 5 baytlık parça 40 B < 45; taban
derleyicide 48 B + tanı boyları tutmuyor → kırmızı; parça boyuna +8 B
sabotajında kırmızı), `tests/dap_audit.py` (eski printer'la `ad`/`j`
okunamıyor → kırmızı; yeni printer karakterleri ofsetten okuyor). wasm32:
`clang++ --target=wasm32-unknown-unknown -ffreestanding -fsyntax-only` ile
(`cstdint`/`cstdlib` şimi) `sizeof(ObjString) == 16`, `chars` @16, hizalama
4; eski `20` iddiası kırmızı.

Yan bulgu: `arc_free_string` malloc'lu dizgide `free(str->chars)` da
çağırıyordu; persist / `string_pin` / anahtar kopyası karakterleri `p + 1`'e
koyduğu için bu bir iç işaretçiyi serbest bırakmaktı. Kod okumasıyla
bulundu; tetikleyen program ölçülmedi. Artık tek blok, tek `free`.

Kalan: (b) — dizgi dizisi işaretçi deposu, parse'ta −40 MB daha; yukarıdaki
risk sütunu yüzünden ayrı iş.

## particles: struct dizisi döngü sürümü + satır içi `push` / `toFloat` — 53,4 → 37,3 ms, Rust ile aynı (2026-10-02)

`particles` Rust'ın (36,9) ve C'nin (42,1) gerisindeydi (53,4 ms). Önce maliyet
ikiye ayrıldı: aynı program `s < 0` ile (yalnız kurulum) ve `s < 150` ile
(adım başına eğim) — Ryzen 7 9800X3D, `taskset -c 10,11`, en iyi:

| | kurulum (1M `push`) | 50 adım | toplam |
|---|--:|--:|--:|
| Tulpar (önce) | 9,1 | ~44 | 53,4 |
| C (gcc -O2) | 3,3 | ~39 | 42,1 |
| Rust | 3,3 | ~33 | 36,1 |

İki ayrı açık, ikisi de IR'dan okundu (`TULPAR_AOT_EMIT_LL=1`):

1. **İç döngüde kanıtsız struct dizisi erişimi.** `SarrCacheScope` başlığı
   döngü başında bir kez okuyordu ama her `ps[i]` yine `i <u count` sınavı +
   yavaş yol (`aot_sarr_elem_ptr` çağrısı) kopyası taşıyordu. LLVM sınavların
   çoğunu birleştiriyor ama turda üç dal ve döngü sınırı `n`'in (üst düzey
   `int` → global) her turda bellekten okunması kalıyordu: yavaş yoldaki çağrı
   her şeyi yazabilir sayılıyor.
2. **Kurulumda opak çağrılar.** Eleman başına dört `aot_to_float_ptr`
   (`toFloat(i % 1000)` — argüman kesin INT, ama çağrı runtime'a) ve bir
   `aot_sarr_push_ptr`.

Yapılanlar:

- **Struct dizisi döngü sürümü** (`tulpar_sarr_loop_plan` + `sv_try_version`,
  float sürümünün struct karşılığı). Biçim: `for (i = E; i < UB; i = i + K)`
  (ya da `<=`, `i++`; K > 0 sabit), UB sabit / döngüde atanmayan ad /
  `len(X)`, `i` gövdede hiç atanmıyor / artırılmıyor (`tulpar_loop_rebinds_name`
  yalnız atama / bildirime bakıyor; burada `++` / `--` da sayılıyor), gövde
  şekli değiştiremiyor (`tulpar_loop_shape_stable`).
  Döngü başında bir kez: `i` INT ve `>= 0`, UB INT ve i32'ye sığar, her A
  için `UB' <= count(A)` (önbellekteki count; struct dizisi değilse 0 → sınav
  ancak gövde hiç dönmezken tutar). Tutarsa HIZLI gövde: `A[i]` (indeks ÇIPLAK
  `i`, AYNI yuva — gölge değil) tek `inbounds` GEP, sınav da yavaş yol da yok;
  koşul döngü başındaki UB ile (global `n` her turda okunmuyor). Tutmazsa
  GENEL gövde — bugünkü bekçili yol, sınır dışı hata orada. İç içe en fazla
  iki kat; genel gövdede iç içe sürüm yok; 1500 düğüm sınırı (#438 ile aynı).
- **`push(d, e)` satır içi:** yer varsa (`count < capacity`, eleman boyu ve
  alan sayısı derleme zamanındakiyle aynı) `memcpy` + `count++`; yoksa
  (büyüme, yanlış tür) eski çağrı.
- **`toFloat` satır içi:** INT → `sitofp`, FLOAT → olduğu gibi, gerisi
  (bool, dizgi) runtime kuralı. Etiket derleme zamanında biliniyorsa LLVM
  dalları katlıyor.

Pay (ardışık turlar, her biri 7 koşu en iyi; kurulum `s < 0` ile):

| | kurulum | toplam |
|---|--:|--:|
| önce | 8,6 | 52,6 |
| + struct dizisi sürümü | 8,6 | 47,5 |
| + `toFloat` satır içi | 7,1 | 45,4 |
| + `push` satır içi | 4,7 | **36,2** |
| Rust / C | 3,5 / 3,4 | 36,0 / 41,9 |

İç döngünün makine kodu sürümden sonra Rust'ınkiyle aynı biçimde (iki
`movupd` yükleme, `addpd`/`mulpd`, iki `ucomisd`, seyrek yollarda skaler
saklama; sınav yok). ⚠ **Son satırdaki 9 ms'nin yalnız ~2,4'ü kurulum**:
`push` satır içi olunca 50 adımlık döngü de hızlandı (adım başına 0,76 →
0,63 ms; L2'de kalan N=10k'da da %7), oysa iki ikilinin iç döngüsü komut
komut aynı, veri adresleri (`realloc` dizisi) aynı. Sayaçlar (perf yok;
`perf_event_open` ile kullanıcı alanı): yavaş ikili daha AZ komutla daha çok
döngü harcıyor, dal kaçırma / L1 kaçırma / ön-uç beklemesi aynı — yani
arka uç; sebep bulunamadı. Üç ayrı kod hizalamasında (kaynağa önek) satır
içi `push` 36,1 / 40,9 / 36,9, çağrılı 45,9 / 45,9 / 45,6 — sonuç tutarlı,
ama "kurulum 6 ms kazandırdı" diye okunmamalı.

Pozitif kontroller (`tests/struct_dizi_surum.sh`, 23 kapı): karar iki yönde
(11 biçim; `push`, kullanıcı fonksiyonu, `i = i + 1`, `i++`, sınır ataması,
`a[k]` kopya indeks, `a[i + 1]` → sürüm YOK), sınır dışı hızlı sürümlü
döngüde hâlâ yakalanıyor (üst / `<=` / negatif başlangıç / üst düzey global
sınır), IR'da `sv.ep` / `spush.fast` / `tof.i2f` var ve `TULPAR_NO_SVER=1` /
`TULPAR_NO_SPUSH_INLINE=1` ile yok, üç derlemenin çıktısı aynı. Sabotaj:
`UB' <= count` sınavı kaldırılınca kapı kırmızı (`malloc(): invalid size` —
sınır dışı yazma yığını bozuyor). Anlam: `tests/struct_dizi_surum.test.tpr`
(9 test; her senaryo bekçili ikiziyle ALAN ALAN karşılaştırılıyor).

Gerileme denetimi (13 çekirdek, dönüşümlü A/B, 9 tur, en iyi; taban
`6d0dc633`, ikisi de repo dışında kendi runtime arşiviyle): IR'ı DEĞİŞEN
yalnız üç çekirdek — **particles 53,4 → 37,3**, **mandelbrot 158,7 → 154,4**
(piksel başına iki `toFloat`), **matmul 37,2 → 36,8** (kurulumdaki
`toFloat`). Kalan on çekirdeğin IR'ı bayt bayt aynı (intloop 135,2/135,0 ·
sieve 7,7/7,8 · qsort 69,8/70,0 · callfn 80,5/80,2 · parse 73,9/74,0 …).
Derleme: `scene3d_editor` 9,16 → 9,22 s (3 koşu en iyi), ikili bayt bayt aynı boyutta (6 153 688 B).

Kalan: kurulum 4,7 ms (Rust 3,5) — büyüyen dizi (`realloc` + yarısı 4 KB
sayfada: THP kapsaması Tulpar 14 MB, Rust/C 30 MB), Rust'ın `collect`'i tek
ayırma. `int` üst düzey değişkenleri terfi etmek bu çekirdekte de geriletiyor
(36 → 43 ms; iç döngü yine aynı makine kodu) — `int` kuralı global kalıyor.
