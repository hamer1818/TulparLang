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

Ölçüm makinesi sonuçları değiştirir; tablo `results.json`'a yazılır.

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
