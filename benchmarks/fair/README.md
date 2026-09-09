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
