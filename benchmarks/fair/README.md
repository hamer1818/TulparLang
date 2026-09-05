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

En iyi duvar saati (ms), 3 tekrar, 2026-09-05. **Düşük olan hızlı.**
Bütün diller aynı çıktıyı bastı.

| Dil | intloop 50M | fib(32) | sieve 5M | strcat 2M | arrayiter 5M |
|---|---|---|---|---|---|
| C (gcc -O2) | **134,4** | **1,6** | **7,9** | 37,8 | 2,1 |
| C++ (g++ -O2) | 134,6 | 1,9 | 8,0 | **14,8** | 3,1 |
| Rust (-O3) | 144,3 | 3,8 | 8,3 | 18,8 | **1,6** |
| Go | 134,8 | 6,7 | 8,5 | 24,8 | 4,0 |
| C# (.NET) | — | — | — | — | — |
| Java | 144,7 | 12,9 | 20,2 | 32,4 | 17,7 |
| Node.js | 708,8 | 25,8 | 29,1 | 101,3 | 19,6 |
| Python | 3187,4 | 143,1 | 451,6 | 200,7 | 414,6 |
| **Tulpar AOT** | 135,4 | 4,4 | 9,8 | 31,7 | 6,6 |

İş yükleri: `intloop` N=50M · `fib` N=32 · `sieve` N=5M · `strcat` N=2M ·
`arrayiter` N=5M. C# satırı boş: bu makinede .NET SDK kurulu değil
(`sudo pacman -S dotnet-sdk-9.0` sonrası kendiliğinden dolar, `run.py`
tarafı hazır).

**C++ eklenmesi sıralamayı değiştirdi** ve bunu gizlemenin anlamı yok:
`strcat`te C++ `std::string` bütün dilleri geçti (14,8 ms — C'nin
`realloc`+`snprintf` döngüsünün 2,5 katı hızlı), `fib`/`sieve`/`arrayiter`te
Tulpar bir sıra geriledi. Ölçüm ne diyorsa o.

## İlk ölçümden sonra yapılan iyileştirmeler

İlk tur üç zayıflık gösterdi; ikisi kapatıldı.

| | İlk ölçüm | Şimdi | Ne değişti |
|---|---|---|---|
| `strcat` | 233,5 | **166,3** | dizgi sabitleri internleniyor |
| `sieve` | 60,4 | **42,1** | interning + `array_fill` |
| `fib` | 5,8 | **4,3** | interning |
| `intloop` | 135,2 | 135,1 | değişmedi (beklendiği gibi) |

**1. Dizgi sabitleri internleniyor.** Her literal *değerlendirmesi* yeni bir
`ObjString` ayırıyordu — arena'dan, geçici işaretli — ve kalıcı bir kaba
konulduğunda yazma bariyeri onu ayrıca derin kopyalıyordu. Ölçüldü:
`push(dizi, "sabit")` ×4M **140,3 → 20,8 ms**; artık `push(dizi, int)` ile
(19,9 ms) aynı sınıfta.

**2. `array_fill(n, deger)` geldi.** n elemanlı diziyi kurmanın tek yolu n kez
`push`ti (~4,5 ns/çağrı → 5M'lik dizide 22 ms, iş yapmadan önce). Rakiplerin
hepsinde tek çağrılık karşılığı vardı; Tulpar'da yoktu, yani bu takım
Tulpar'ı gereksiz yere döngüye mahkûm ediyordu.

**3. Kutulanmamış sayısal dizi — YAPILMADI.** Kalan `sieve` farkı (42,1 vs
7,6 = 5,5×) burada. `array` hâlâ kutulu bir `VMValue` vektörü ve her indeksli
erişim çalışma zamanı yardımcısından geçiyor (~3 ns; C'nin ham `int*`
yüklemesi ~0,5 ns). `int[]` sözdizimi var ama depolamayı değiştirmiyor —
gerçek çözüm yeni bir `Obj` türü + codegen hızlı yolu, yani ayrı bir proje.
