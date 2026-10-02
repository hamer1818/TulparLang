# Adil dil karşılaştırması

`benchmarks/` kökündeki eski takım "hangi dil hızlı" sorusunu **cevaplayamıyordu**.
Üç ayrı kusuru vardı ve üçü de sonucu geçersiz kılıyordu (2026-09-02'de ölçüldü):

1. **Sabit parametre.** Yalnız Tulpar iş yükünü ortamdan okuyordu; C/Rust/Go/JS/Py
   derleme-zamanı sabiti alıyordu. `gcc -O2` ve `rustc -O3` `loopsum`u **kapalı
   forma katlıyor** — `objdump` ile doğrulandı, `main` içinde **sıfır atlama
   komutu**. Yani C ve Rust hiç döngü koşmadan "kazanıyordu".
2. **Farklı algoritma.** `sieve`de C `char*` kullanırken Tulpar `json`a push
   ediyordu; `struct_array_push`ta C tek `malloc` yapıp indeksle yazarken Tulpar
   büyüyen diziye push ediyordu. Aynı işi ölçmüyorlardı.
3. **Ölçüm gürültüsü.** "best of 1" ve bütün süreler 22–60 ms — ölçülenin çoğu
   süreç başlatma maliyetiydi. Yayınlanan tabloda ayrıca silinmiş **VM** satırı
   ve `tak`ta "0× faster" gibi bozuk oran çıktıları vardı.

## Bu takımın kuralları

- **İş yükü `BENCH_N` ortam değişkeninden**, her dilde. Kimse katlayamaz.
- **Aynı algoritma, aynı veri yapısı.** Kaynaklar yan yana okunacak kadar kısa.
- **Her dile kendi en iyi aracı**: Java/C# `StringBuilder`, C++ `std::string`,
  JS `Int32Array`, Go `strings.Builder`, Rust `String`, Tulpar `int[]`. Birine
  naif yol dayatmak dili değil o tuzağı ölçerdi. `arrayiter`de aynı kural
  uzunluk erişimine de uygulanıyor: `a.size()` / `a.Length` / `a.len()` /
  `len(a)` — C'de dilde uzunluk yok, `n` elle taşınıyor.
- **Araç zinciri yoksa satır düşer, koşum durmaz.** C# için `dotnet` (ya da
  `mono`+`mcs`) gerekiyor; yoksa o satır `DERLENEMEDI` görünür ve kalan
  diller normal ölçülür.
- **Çıktı doğrulaması**: bütün diller aynı şeyi basmazsa satır **geçersiz**.
  "Aynı işi yapıyorlar mı" sorusunun tek dürüst cevabı bu.
- **Isıtma koşumu sayılmıyor**, 5 tekrar, en iyi + ortanca.
- **Boş program taban çizgisi** ayrıca raporlanıyor (C 0.2 · Tulpar 0.8 ·
  Python 5.8 · Node 11.4 ms) ve iş yükleri onu gölgede bırakacak kadar büyük.

`kapalı form` tuzağı ikinci kez ısırdı: ilk düzeltmede `loopsum`u ortamdan
okutmak **yetmedi** — LLVM `n` çalışma zamanında bile `n*(n-1)/2`yi türetiyor,
yani Rust ve Tulpar boş program hızında koşuyordu. Yerine **zincirleme
bağımlılığı olan** bir döngü kondu (`t = (t*31 + i) % 1000000007`); kapalı formu
yok, hiçbir derleyici katlayamıyor.

## Çalıştırma

```bash
cd benchmarks/fair && REPEATS=5 python3 run.py
```

Ölçüm makinesi sonuçları değiştirir; tablo `results.json`'a yazılır, ondan
`RESULTS.md` (insan için) ve `results.csv` (araçlar için) üretilir.
`results.csv` dondurulmuş, makinece okunur tablodur: kıyas × dil başına bir
satır — `kernel,n,lang,best_ms,median_ms,output,agree,repeats`. `output`
dokuz dilin ORTAK bastığı sonuç (checksum), `agree=0` o kıyasın geçersiz
olduğu anlamına gelir. Yeniden ölçmeden JSON'dan üretmek için:
`python3 run.py --csv`. Depodaki `results.csv`, `results.json`'daki
2026-09-11 turundan (#313, 5 tekrar, aşağıdaki makine) üretildi.

## Sonuçlar

**Canlı sıralama ve gelişim eğrisi burada:**
<https://claude.ai/code/artifact/a582fb28-0ecb-4e8b-a8b9-f12878b50215>
— her ölçüm oraya bir satır ekliyor, aşağıdaki tablo o anlık görüntünün
kopyası ve bayatlayabilir.

En iyi duvar saati (ms), 2026-09-08 · commit `3edb4f4` · 7 tekrar.
**Düşük olan hızlı.** Dokuz dilin **hepsi aynı çıktıyı bastı** (beş kıyasta
da `agree: true`).

| Dil | intloop 50M | fib(32) | sieve 5M | strcat 2M | arrayiter 5M |
|---|---|---|---|---|---|
| C (gcc -O2) | **134,4** | 1,6 | **7,6** | 37,6 | 2,2 |
| C++ (g++ -O2) | 134,9 | 1,9 | 8,0 | 14,7 | 2,7 |
| Rust (-O3) | 144,0 | 3,8 | 8,1 | 18,7 | 1,5 |
| Go | 134,5 | 6,7 | 8,5 | 24,3 | 4,3 |
| C# (.NET) | 149,3 | 20,3 | 20,9 | 31,2 | 18,3 |
| Java | 144,1 | 12,4 | 19,7 | 32,9 | 18,2 |
| Node.js | 712,2 | 24,6 | 28,2 | 96,4 | 18,5 |
| Python | 3127,3 | 140,8 | 448,1 | 200,0 | 410,2 |
| **Tulpar AOT** | 134,5 | **0,6** | 7,7 | **13,2** | **1,2** |

Tulpar sırası: `intloop` 3. · `fib` 1. · `sieve` 2. · `strcat` 1. · `arrayiter` 1. (dokuz dil arasında).

Makine: **AMD Ryzen 7 9800X3D** (Zen 5, 8 çekirdek / 16 iş parçacığı,
5,27 GHz zirve, 96 MB 3D V-Cache), Linux. Araç zincirleri: gcc 16.2.1 ·
rustc 1.89.0 · go 1.27.1 · Tulpar AOT (LLVM 22).

### "C'den hızlı" iddiası C'nin EN İYİ bayraklarına dayanıyor mu?

Yukarıdaki tablo C'yi `gcc -O2` ile derliyor — Tulpar'ın kendisinin
hedeflediği jenerik tabanla aynı (`TULPAR_TARGET_CPU` verilmedikçe LLVM
hedef CPU'su `"generic"`; `-march=native` karşılığı **opt-in ve bu
sayılarda kullanılmıyor**). Bu adil bir varsayılan, ama "C'den hızlı"
sıra dışı bir iddia; o yüzden C'ye en iyi bayrakları verip yeniden
ölçüldü. **12 dönüşümlü tekrar, ortanca ± MAD** (2026-09-09):

| Kıyas | C `-O2` | C `-O3 -march=native -flto` | Tulpar (generic) | Sonuç |
|---|---:|---:|---:|---|
| `fib(32)` | 2,49 | 2,08 ± 0,04 | **0,77 ± 0,02** | Tulpar 2,7× — **duruyor** |
| `strcat(2M)` | 37,95 | 36,98 ± 0,37 | **13,27 ± 0,21** | Tulpar 2,8× — **duruyor** |
| `sieve(5M)` | 7,86 | 7,75 ± 0,14 | 7,77 ± 0,10 | berabere (MAD içinde) |
| `arrayiter(5M)` | 2,50 | **1,85** | 2,03 | **C `native` ile geri alıyor** |

Yani dürüst ifade tek bir "C'yi geçtik"ten dar: `fib` sağlam bir kazanç ve
C'nin en iyi bayraklarına karşı da duruyor; `sieve`/`intloop` berabere;
**`arrayiter` yalnız eşit jenerik bayrakta Tulpar'ın, `-march=native` ile
C'nin.**

> ⚠️ **`strcat` kazancı sonradan GERİ ÇEKİLDİ.** Bir kontrol koşusunda C'ye
> `snprintf` yerine Tulpar'ın kullandığı elle yazılmış tamsayı→dizgi
> yordamının aynısı verildi: C **11,07 ± 0,10 ms**, Tulpar **13,85 ± 0,39** —
> C 1,25× önde. Yukarıdaki 2,8×'lik fark tamamen biçimlendirici
> asimetrisiydi, yani derleyici değil **stdlib** kıyası. Ayrıntı ve ham veri:
> [`recursion/`](recursion/README.md).

> ⚠️ **`fib` kazancı GENELLEŞMİYOR.** Özyineleme ailesinin tamamı
> (`ackermann`, `tak`, `treesum` + karşılıklı özyineleme negatif kontrolü)
> koşuldu: klon zinciri `fib`'de 6,33× kazandırırken ailenin geri kalanında
> 1,04–1,57×'te kalıyor ve **`ackermann`'ı %21 geriletiyor**. `gcc` bu ailede
> `ackermann` 4,2×, `treesum` 2,5×, `tak` 1,47× önde. "Özyinelemede C'den
> hızlıyız" desteklenmiyor. Eşleşen `clang` tabanı ve CSV yine
> [`recursion/`](recursion/README.md) altında.

`-O2` → `-O3` farkı ölçüldü ve **ihmal edilebilir** (fib 2491→2290 µs,
strcat 37951→38133, sieve 7861→7797); sıralamayı değiştiren şey `-O3`
değil, `-march=native`.

### İki açıklama, sayıların taşımadığı

- **`strcat` farklı araçları kıyaslıyor.** C tarafı naif değil — geometrik
  büyümeli tampon (`cap*=2`) — ama her sayıyı `snprintf` ile biçimliyor;
  Tulpar tam bu yol için optimize edilmiş özel bir tamsayı→dizgi yordamı
  kullanıyor. Elle `itoa` yazan bir C programcısı farkın çoğunu kapatır.
- **`fib` etiketine rağmen çağrı maliyetini değil satır içi almayı
  ölçüyor.** İki dil de üstel ağacı gerçekten koşuyor — süre `n`'deki her
  +2 için φ² katına çıkıyor (ölçüldü: C 2,67×, Tulpar 2,30×), yani hiçbiri
  ağacı doğrusala çökertmiyor. Ama Tulpar'ın daha düşük üsteli, kazancın
  bir kısmının özyineleme klon zincirinin sağladığı altifade
  paylaşımından geldiğini gösteriyor.

### Bu takımın KANITLAMADIĞI şeyler

Tek makinede beş tam sayı/dizgi çekirdeği "en hızlı dil" iddiasını
taşıyamaz. Kapsam dışı: kayan nokta ve SIMD (matmul, n-body, mandelbrot),
tahsis baskısı ve hash-map/JSON yükleri — **ARC ile GC farkı ancak orada
görünür** — işaretçi takibi, sıralama, çok iş parçacıklı ölçekleme, RSS,
ve sürekli yük altında p99. Savunulabilir okuma: Tulpar **tam sayı
çekirdeklerinde C sınıfında** ve Node/Python/Java/C#'ın açık ara önünde.
Düz döngü ve özyineleme codegen'i C-sınıfıdır, C-üstü değildir; C'yi
geçtiği doğrulanmış tek çekirdek `fib`.

İş yükleri: `intloop` N=50M · `fib` N=32 · `sieve` N=5M · `strcat` N=2M ·
`arrayiter` N=5M.

Okurken üç şeye dikkat:

- **`strcat`te C sondan üçüncü** (37,6 ms). Elle `realloc`+`snprintf`
  döngüsü, C++ `std::string`in 2,5 katı yavaş. "C her zaman en hızlı"
  değil — kap seçimi dili yener.
- **C# ve Java satırlarında başlatma maliyeti var**: boş program taban
  çizgisi C# için 8,2 ms (C 0,17 · C++ 0,44 · **Tulpar 0,23** · Python 5,6
  · Node 10,7). AOT diller bu maliyeti ödemiyor; kıyas duvar saatini
  ölçtüğü için fark tabloda görünüyor.
- **`intloop` ve `sieve`de ilk dört dil 0,1–0,9 ms içinde.** O aralıkta
  sıra numarası ölçüm oynamasıyla değişiyor; anlamlı olan bant, sıra değil.

## İlk ölçümden bugüne

İlk adil ölçüm (2026-09-02) Tulpar'ı beş kıyasın hiçbirinde ilk üçe
sokmuyordu. Bugün üçünde birinci, birinde ikinci.

| | ilk ölçüm | bugün | kazanç |
|---|---|---|---|
| `strcat` 2M | 233,5 | **13,2** | 17,7× |
| `sieve` 5M | 60,4 | **7,7** | 7,8× |
| `arrayiter` 5M | 6,6 | **1,2** | 5,5× |
| `fib(32)` | 5,8 | **0,6** | 9,7× |
| `intloop` 50M | 135,2 | 134,5 | değişmedi (beklendiği gibi) |

Sıçramaların hepsi ölçümle bulundu; her adımın gerekçesi ve elenen
denemeler `docs/mindmap/Performance.md`'de. Ana kaldıraçlar:

**1. Kutulanmamış sayısal dizi.** `ObjArray` artık ya kutulu `VMValue`
vektörü ya da ham tamsayı dizisi tutuyor; erişim codegen'de satır içi
GEP+load. Üstüne TBAA (eleman deposu ile dizi başlığı ayrı takma-ad
sınıfı) ve **döngü-değişmezi şekil önbelleği** — dizinin işaretçisi ve
uzunluğu döngü başında bir kez okunuyor.

**2. Döngü sürümleme.** `for`/`while` gövdesi iki kez üretiliyor; hızlı
sürümde sınır denetimi kanıtla gereksiz olduğu için hiç yok. Kanıt
sözdizimsel değil, döngü başındaki tek bir sınavla kuruluyor.

**3. 32-bit eleman deposu.** C/Rust/Go elekte 4 baytlık eleman kullanıyor,
biz 8 kullanıyorduk. Dizi artık 32-bit başlıyor ve i32'ye sığmayan bir
değer yazılınca **genişletiliyor** (kutulanmıyor) — dilin `int`i 64-bit
kalıyor. Genişlik dalı erişimde değil **sürüm seçiminde**, yani iki
genişlik de sıcak yolda dalsız.

**4. Özyineleme zinciri.** LLVM kendini çağıran bir fonksiyonu satır içine
almaz (gcc alır — `fib`de bütün LLVM dillerini 2,4 kat geçmesinin tek
sebebi buydu). Arka uç fonksiyonun dört kopyasını üretip halka kuruyor;
artık her kenar iki *farklı* fonksiyon arası çağrı olduğu için sıradan
satır içi alıcı onları açıyor.

**5. Açılış maliyeti.** Her ikili OpenSSL yüklüyordu ve libstdc++ dinamik
bağlanıyordu: boş program 1,15 → 0,23 ms.

**6. Dizgi kurma.** `StringBuilder` + kutulamasız `sb_append` + hızlı
`itoa`; ayrıca dizgi sabitleri internleniyor.

**7. Tipsiz yol.** `%` operatörünün kutulu satır içi hızlı yolu yoktu ve
her kutulu global ataması koşulsuz bir çalışma zamanı çağrısı ödüyordu;
ikisi de kapatıldı. Kutulu fonksiyonlar ayrıca **değer ABI'sine** geçti
(argüman ve dönüş yazmaçta): tipsiz `fib` 11,9 → 7,7 ms.


## Kayan nokta seti (2026-09-11)

Üç çekirdek **bilerek farklı yerleri** sınar; "float yavaş mı" tek sayıyla
cevaplanamaz çünkü iki ayrı maliyet vardır:

* **değer** temsili — her işlemde kutu aç/kapa
* **depolama** temsili — eleman başına 16 bayt `VMValue` vs 8 bayt `double`

| çekirdek | ne ölçer | dizi |
|---|---|---|
| `mandelbrot` | saf kayan nokta aritmetiği | **yok** |
| `matmul` | kayan nokta dizisi (depolama + bant genişliği) | yalnız o |
| `nbody` | ikisinin karışımı + `sqrt` | küçük |

Ayrım kurulmadan ölçülürse "float 10× yavaş" denir ve **hangi yarının** suçlu
olduğu bilinmez. Ölçüm bunu gösterdi: mandelbrot **1,00× C**, matmul **26,6×**.

**Çıktı mutabakatı nasıl sağlandı.** Kayan nokta kıyaslarında asıl zorluk
sürelerin değil, **sonuçların** eşleşmesi:

* `matmul` değerleri **küçük tam sayı** (double içinde birebir) — böylece
  gcc'nin FMA'ya kaynaştırması sonucu değiştirmiyor ve toplam `2^53`in çok
  altında kalıyor.
* `mandelbrot` çıktısı **yineleme sayısı**, yani tam sayı.
* `nbody` enerjiyi `1e9` ile ölçekleyip **yuvarlayarak tam sayı** basıyor;
  böylece biçimlendirme (`%.9f` ↔ `toString` ↔ `println!`) ölçümün dışında
  kalıyor. Dokuz dil de aynı değeri veriyor.

Bir şey daha: **kaynak dosyası olmayan dil satırı düşer, koşum durmaz.**
Önceden bu kural yalnız *araç zincirini* kapsıyordu; `<bench>.cs` yokken
koşucu `FileNotFoundError` ile **tamamen** çöküyordu — yani tek bir dilin
eksik kaynağı ötekilerin ölçümünü de götürüyordu. C'nin `-lm` bağlantısı da
eklendi (`nbody` yalnız C satırında "DERLENEMEDI" oluyordu; referans dilin
eksik olduğu bir tablo yayınlanacaktı).

## Genişletilmiş set (2026-09-29)

İlk sekiz çekirdek tamsayı, dizgi ve kayan nokta ağırlıklıydı. Yukarıdaki
"KANITLAMADIĞI şeyler" listesinden beş alan eklendi, her biri dokuz dilde
aynı algoritmayla ve çıktı doğrulamasıyla:

| çekirdek | alan | ne ölçer |
|---|---|---|
| `hashmap` | veri yapıları | dizgi anahtarlı sözlük: N ekleme + N arama; her dil kendi standart sözlüğü, C'de elle açık adresleme + FNV-1a |
| `qsort` | diziler | elle Hoare hızlı sıralama (orta pivot, özyinelemeli); kütüphane sıralaması farklı algoritmaları kıyaslardı |
| `particles` | oyun döngüsü | struct dizisi üzerinde yerçekimi + sekme, 1M parçacık × 50 adım; Tulpar'da kutusuz `P[]` |
| `callfn` | soyutlama | fonksiyon değeri tablosundan dolaylı çağrı; indis bir önceki sonuca bağlı, satır içi alınamaz (Rust'ta `black_box`, C'de const olmayan global tablo) |
| `parse` | metin | virgüllü metin kur + böl + tamsayıya çevir; C'de `split` yok, `strtol` ile yerinde yürüme |

Koşucu artık süreden başka dört şey ölçüyor: **tepe bellek** (`rsswrap.c` ile
ayrı bir koşumda `ru_maxrss`; Python'dan doğrudan alınan değer exec öncesi
çatallanan Python kopyasını da saydığı için her küçük program 17 MB
görünüyordu), **başlatma** (Rust, Go ve Java da eklendi), **derleme süresi** ve
**ikili boyutu**. Tek koşum `TIMEOUT_S` (varsayılan 60 s) saniyeyi aşarsa satır
"ZAMAN ASIMI" olarak yazılır ve tekrarlar koşmaz; sessizce düşmez.
`python3 run.py --rss` süreleri yeniden ölçmeden yalnız bellek sütununu doldurur.

Sonuç (Ryzen 7 9800X3D, Linux, 2026-09-29, 5 tekrar, en iyi; tam tablo
`RESULTS.md`, canlı karne yukarıdaki bağlantıda):

| | Tulpar | C | Tulpar / C | sıra (9 dil) |
|---|---:|---:|---:|---:|
| `intloop` | 134,6 | 134,7 | 1,00× | 1. |
| `fib` | 0,4 | 1,7 | 0,24× | 1. |
| `sieve` | 7,7 | 8,0 | 0,96× | 1. |
| `strcat` | 14,1 | 37,8 | 0,37× | 1. |
| `arrayiter` | 1,2 | 2,3 | 0,52× | 1. |
| `mandelbrot` | 158,4 | 158,7 | 1,00× | 3. |
| `qsort` | 121,2 → **70,0** (2026-10-01) | 57,4 | 2,1× → **1,2×** | 8. → **5.** |
| `callfn` | 298,6 → 209,6 → **65,5** (2026-10-01) | 91,1 | 2,3× → **0,7×** | 8. → ~3.* |
| `parse` | 196,3 → 126,2 (2026-10-01) → 75,2 (2026-10-02) → **72,5** (2026-10-02, satır içi dizgi karakteri; bellek 261,7 → 223,4 MB) | 56,7 | 2,2× → **1,3×** | 6. → 4. → ~3.* |
| `particles` | 329,6 → **54,8** (2026-10-01) | 42,2 | 7,8× → 1,3× | 8. → **4.** |
| `nbody` | 1304,8 → 187,3 → **115,3** (2026-10-01) | 114,5 | 11,4× → 1,6× → **1,0×** | 8. → 7. → ~2.* |
| `matmul` | 821,3 → **37,1** (2026-10-01) | 31,1 | 26,4× → **1,2×** | 8. → **4.** |
| `hashmap` | > 60 s → **170,3** (2026-10-01; bellek 439 → 119 MB ve A/B'de −38 % aynı gün) | 72,7 | 2,3× | 9. → **2.** |

Bulgular, sırayla ele alınması önerilen:

1. ~~**`hashmap` karesel.**~~ **Kapandı (2026-10-01):** `json` nesnesi her
   eklemede ve aramada anahtarları baştan sona `strcmp` ile tarıyordu
   (12 500 anahtar 0,25 s, 50 000 3,8 s, 1M zaman aşımı). 16+ anahtarlı
   nesneye yazma yollarında kurulan hash indeksi eklendi: 1M anahtar
   **170 ms**, dokuz dil arasında 2. ~~Tepe bellek hâlâ yüksek (450 MB, C
   66).~~ **Bellek de kapandı (2026-10-01): 439 → 118,8 MB**, aynı A/B'de
   süre en iyi 196,8 → 122,2 ms (makine yük altındaydı; taban o koşumda
   170 değil 197). `"k" + toString(i)` artık tek ayırma, arama anahtarı
   erişimden sonra arenaya geri bırakılıyor, yeni anahtar tek kopya (mevcut
   anahtarda hiç), açık checkpoint yokken arenadaki dizgi yazma bariyerinde
   kopyalanmıyor. Ayrıntı: `docs/mindmap/Performance.md` "Geçici dizgiler".
2. ~~**Float dizileri** (`matmul`, `nbody`) 11 Eylül'den beri yerinde
   sayıyor.~~ **Kapandı (2026-10-01):** sorun aritmetik değil depolama ve iç
   içe döngüde kanıtlı erişimdi — `float[]` 16 baytlık VMValue tutuyordu ve
   kanıtlı erişim yalnız tamsayı dizide, yalnız en dış döngüde vardı
   (matmul'ün sıcak döngüsü üçüncü seviyede). Artık `array_fill(n, 0.0)`
   ham double tutuyor ve en içteki döngü, hangi derinlikte olursa olsun,
   `X[B + j]` erişimleri için döngü başında bir kez sınanıp sürümleniyor;
   `sqrt` satır içi. `matmul` 820 → **37,1 ms** (1,2× C, iç döngü
   vektörleşiyor; tepe bellek 22,1 → 12,3 MB, C 11,8), `nbody` 1301 →
   **187,3 ms** (1,6× C). Sıralar 2026-09-29 tablosunun öteki dillerine
   göre. **nbody'nin kalan farkı da kapandı (2026-10-01):** beş cisimde iç
   döngü 0–4 tur dönüyor ve her girişte yedi dizinin deposu + ~20 erişimin
   aralığı sınanıyordu, üstüne int şekil önbelleği dört başlığı yeniden
   okuyordu. Sınav artık dış döngü başında bir kez (iç içe sürüm, uç nokta
   sınavı) ve int şekil önbelleği float sürümlü döngüde yalnız genel
   gövdede: 187,5 → **115,3 ms** (aynı düzenekte C gcc -O2 114,8).
   *Sıra resmî koşumda ölçülmedi; `results.json`'daki öteki dillerle
   (C/Rust 114,7, C++ 115,1, Java 122,8) yan yana ~2. — C ile aynı sınıf.
3. ~~**`particles`** (oyun döngüsü) C'nin 7,8 katı.~~ **Kapandı
   (2026-10-01):** iki ayrı maliyet vardı. (a) Üst düzey değişkenler kutulu
   LLVM global'iydi — yalnız main'de görülenler artık main'in yereli; (b)
   struct dizisi alan erişimi her seferinde başlığı (tür, count, data)
   yeniden okuyordu — alanlar TBAA etiketli ve sekli değişmeyen döngüde başlık
   bir kez okunuyor. Üst düzey 336 → 55 ms, fonksiyon içi 193 → 55 ms (C 42,7,
   Rust 36; dokuz dil arasında 4.). Ayrıntı ve pay:
   `docs/mindmap/Performance.md` "particles" bölümü.
4. **`parse`** — **hız kapandı (2026-10-01):** `split` parçaları tek arena
   ayırmasında bitişik kuruyor (parça başına geçici malloc + ayrı ayırma +
   büyüyen dizi yerine), `toInt` düz ondalık dizgide `atoll`a gitmiyor:
   200 → **126 ms** (Go 116, Rust 77, C 57). Tepe bellek AYNI (440 MB, C
   32): parçalar canlı ve her biri 56 baytlık `ObjString` başlığı taşıyor —
   geri alınmayan çöp değil, temsil maliyeti (`docs/mindmap/Performance.md`).
   2026-10-01: üst düzey `str s = sb_tostring(sb)` metni artık tek kopya
   (yazma bariyeri yerleşmiş arena dizgisini kopyalamıyor): **440 → 412 MB**;
   aynı gün `ObjString` 56 → 48 bayt (okunmayan `capacity` alanı): **412 →
   374 MB**. **2026-10-02:** `Obj` başlığı 32 → 8 bayt (`ObjString` 24),
   tek baytlık ayırıcıda `split` parça başına tek arama (üst sınırla ayır,
   kuyruğu iade et), iki haneli `itoa`: **121 → 75 ms, 374 → 261 MB**
   (aynı düzenekte C 57,0 ms / 31,7 MB). *Sıra resmî koşumda ölçülmedi;
   `results.json`'daki öteki dillerle (C++ 60,6, Rust 76,6, Go 117,0) yan
   yana ~3.; bellekte ~5. (Go 118, Python 308).* Kalan: parça hâlâ 32 B
   nesne + 16 B eleman (Performance.md "parse ... 2026-10-02").
5. **`callfn`** — **kısmen kapandı (2026-10-01):** fonksiyon referansı her
   değerlendirmede yeni bir arena dizgisiydi (döngüde `call(f, x)` 20M kez →
   1,26 GB) ve `call()` her çağrıda adı hash'leyip önbelleği yokluyordu.
   Referans artık site başına bir kez çözülen havuz dizgisi; `call()` onu
   satır içinde, doğrudan işaretçiyle çağırıyor: 299 → 210 ms (C 91).
   **İkinci adım (2026-10-01):** hedef tümü-int ise (`func f(int x): int`)
   havuz kaydı çıplak `i64 f(i64)` giriş noktasını da taşıyor ve argümanlar
   INT ise satır içi yol kutulu sarmalayıcıyı (`tb_f`: argüman belleğe
   yazılıp okunuyor, iki `fptosi`/`select`, ikinci çağrı, sonuç yuvası)
   atlıyor: 174 → **65,5 ms** (aynı düzenekte gcc -O2 C 91,4, clang -O2 C
   58,2; Tulpar ikilisi düzenine duyarlı, koşumlar arasında 65–76 ms).
   *Sıra resmî koşumda (`run.py`) yeniden ölçülmedi: `results.json`'daki
   öteki dillerle (Rust 58,5, Go 64,9, Java 76,2) yan yana konunca ~3. Kalan: `acc` üst düzey `int`
   → global, tur başına bir saklama (main yereli olunca 63,5 ms; `int`
   kuralı elek yüzünden global, bkz. `main_local_declare`).
6. **`qsort`** — **büyük ölçüde kapandı (2026-10-01):** maliyet dizide değil
   tamsayı değişkenlerdeydi — fonksiyon içindeki `int i` kutulu bir yuva ve
   her `i + 1` / `a[i] < p` etiket dallanması + geri düşüş çağrısı ödüyordu
   (aynı bölme döngüsü üst düzeyde, main yerelleriyle 85 ms). Döngü başında
   bir kez etiket + `int[]` depo sınavıyla native gölgeye iniyor: 122,7 →
   **70,0 ms** (C 57,4; dokuz dil arasında 8. → 5., Go 58,9'un hemen
   arkasında). Kalan fark büyük olasılıkla çağrı başına (863 bin özyinelemeli
   çağrı, kutulu ABI). Ayrıntı: `docs/mindmap/Performance.md`.
7. **Başlatma 0,25 ms, derleme ortancası 61 ms, ikili 1,4 MB**; belleğin çoğu
   çekirdekte C ile aynı (`arrayiter`'de 32 bitlik dizi sayesinde yarısı).
