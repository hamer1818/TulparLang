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
- **Her dile kendi en iyi aracı**: Java `StringBuilder`, JS `Int32Array`, Go
  `strings.Builder`, Tulpar `int[]`. Birine naif yol dayatmak dili değil o
  tuzağı ölçerdi.
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

En iyi duvar saati (ms), 5 tekrar. **Düşük olan hızlı.** Bütün diller aynı çıktıyı bastı.

| Dil | intloop · tamsayı aritmetiği (zincirleme bağımlılık) | fib · özyineleme (çağrı maliyeti) | sieve · dizi/bellek erişimi | strcat · dizgi kurma + tarama |
|---|---|---|---|---|
| C (gcc -O2) | 134.4 | 1.6 | 7.5 | 37.6 |
| Rust (-O3) | 144.1 | 3.7 | 8.1 | 18.9 |
| Go | 134.8 | 6.6 | 8.2 | 24.7 |
| Java | 144.4 | 12.2 | 20.2 | 32.9 |
| Node.js | 709.6 | 25.1 | 28.9 | 100.4 |
| Python | 3174.1 | 141.7 | 455.8 | 199.3 |
| **Tulpar AOT** | 135.2 | 5.8 | 60.4 | 233.5 |

İş yükleri: `intloop` N=50000000 · `fib` N=32 · `sieve` N=5000000 · `strcat` N=2000000

## Okuma — nerede güçlü, nerede zayıf

**Tamsayı aritmetiği: C sınıfı.** 135.2 vs C 134.4 ms — **%1 içinde**, üstelik
Rust (144.1) ve Java'dan (144.4) hızlı. Tipli fonksiyon imzası (`func f(int n): int`)
LLVM'in kutulanmamış i64 yolunu açıyor ve orada gerçekten native kod çıkıyor.
Mottonun ("C kadar hızlı") karşılandığı yer burası.

**Özyineleme: orta.** 5.8 ms, C'nin 3.6 katı — ama **Go'dan (6.6), Java'dan
(12.2), Node'dan (25.1) hızlı.** Çağrı başına ek maliyet var, felaket değil.

**Dizi/bellek: 8× yavaş.** 60.4 vs C 7.5 ms. Kırılımı ölçüldü:

| Aşama | Süre |
|---|---|
| 5M `push` ile diziyi kurmak | 14.0 ms |
| + 5M indeksli okuma | +15.3 ms (~3 ns/erişim) |
| + elek işaretleme (~11.9M yazma) | +31.1 ms |

Sebep yapısal: `array` **kutulu `VMValue` vektörü** ve her erişim bir çalışma
zamanı yardımcısından geçiyor — C'nin ham `int*` yüklemesi ~0.5 ns.
`int[]` sözdizimi yalnız **%4** kazandırıyor (57.8 ms): yazım tipli ama
**depolama hâlâ kutulu**.

**Dizgi/koleksiyon: en zayıf nokta — Python'dan da yavaş.** 233.5 vs Python
199.3 ms. Kırılım suçluyu tek başına gösteriyor:

| Aşama | Süre |
|---|---|
| 4M `push` (sabit dizgi) | 141.6 ms |
| + 2M `toString(int)` | +58.1 ms |
| + `join` | +26.9 ms |

Yani darboğaz dizgi işleme değil, **`push` çağrısının kendisi: ~35 ns**.
`join` (4M parça, 27 ms) gayet iyi.

## Buradan çıkan iş listesi

1. **Kutulanmamış sayısal dizi.** En büyük tek kazanç: `sieve` 8×'ten
   ~2×'e inebilir. `int[]`/`float[]` sözdizimi zaten var, arkasındaki
   depolama yok.
2. **`push` maliyeti (~35 ns).** Amortize büyüme var ama çağrı başına sabit
   maliyet yüksek; `strcat`in %61'i bu.
3. **Dizi ön-ayırma yok.** `arrayInt` bir *tip adı*, kurucu değil —
   n elemanlı bir diziyi kurmanın tek yolu n kez `push`. Ön-ayıran bir
   kurucu `sieve`den doğrudan 14 ms (%23) siler.

## Dürüstlük notları

- Bunlar **mikro-kıyaslama**: dar, sentetik döngüler. Gerçek program
  karışımını temsil etmezler ve tek makinede (16 çekirdek, 5 GHz sınıfı,
  Linux) ölçüldüler.
- Python ve JS satırları **yorumlayıcı/JIT** karşılaştırması; Node'un JIT'i
  kısa koşumlarda ısınmaya vakit bulamıyor, uzun koşumlarda arayı kapatır.
- Java'nın taban çizgisi (JVM başlatma) ölçülmedi; `fib` gibi kısa işlerde
  payı büyüktür, `intloop`ta ihmal edilebilir.
- `strcat`te her dil kendi stdlib sayacını kullanıyor (Go `strings.Count`,
  Python `.count`, Tulpar `count`); C ve Java elle tarıyor. Bu fark toplamın
  küçük bir parçası ama sıfır değil.
