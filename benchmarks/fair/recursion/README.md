# Atribüsyon koşusu — özyineleme ailesi, clang tabanı, aynı-itoa kontrolü

Dış bir denetim, `benchmarks/fair/` tablosundaki "C'den hızlı" iddiasının
**atribüsyonunu** sorguladı ve üç yanlışlanabilir test önerdi. Üçü de koşuldu.
**İkisi iddiayı çürüttü.** Ham veri: [`results_recursion.csv`](results_recursion.csv).

Protokol: tek donmuş koşu, **12 dönüşümlü tekrar, ortanca ± MAD**, iş yükü
`BENCH_N`'den, C ve Tulpar çıktıları denk. Makine: AMD Ryzen 7 9800X3D (Zen 5,
5,27 GHz, 96 MB 3D V-Cache), Linux. gcc 16.2.1 · clang 22.1.8 · Tulpar AOT
(LLVM 22 — yani **clang, birebir eşleşen arka uç tabanı**).

## Test 1 — Özyineleme zinciri genelleşiyor mu, yoksa fib'e mi overfit?

`fib`'deki 25,2 → 2,3 ms sıçraması "self-recursion clone chain" pass'ine
dayanıyordu. Eğer bu pass yalnız `fib`'de kazandırıyorsa süite overfit demektir.
Aynı aileden dört çekirdek daha koşuldu (ms, ortanca):

| Çekirdek | gcc -O2 | gcc en iyi | clang en iyi | **Tulpar** | Tulpar zincirSİZ | zincir kazancı |
|---|---:|---:|---:|---:|---:|---:|
| `fib(32)` | 1,84 | 1,80 | 3,97 | **0,67** | 4,24 | **6,33×** |
| `tak(18)` | 10,27 | 4,90 | 10,44 | 7,22 | 11,31 | 1,57× |
| `treesum(24)` | 5,92 | 5,81 | 20,00 | 14,76 | 20,15 | 1,37× |
| `ackermann(3,9)` | 3,31 | 3,30 | 11,60 | 14,00 | **11,11** | **0,79× — GERİLEME** |
| `mutual(30M)` | 0,18 | 0,16 | 0,15 | 0,22 | 0,23 | 1,04× |

**Sonuç: zincir genelleşmiyor.** `fib` 6,33× ile ailenin geri kalanından
(1,04–1,57×) kopuk bir aykırı değer, ve `ackermann`'da zincir programı **%21
YAVAŞLATIYOR** (14,00 vs 11,11 ms; fark 2,9 ms, MAD 0,28 — yaklaşık 10 MAD,
gürültü değil). Bu, önceden bilinmeyen gerçek bir gerileme: `build.sh`'taki
koruma yalnız `fib`'i sınıyordu.

Mekanizma IR'den doğrulandı — klon zinciri `fib`/`ack`/`tak`/`tsum`'da
üretiliyor, `mutual`'da **üretilmiyor** (`klon=0`), çünkü zincir yalnız
öz-özyinelemeyi ele alıyor. `mutual` bu yüzden temiz bir negatif kontrol.
(Uyarı: `mutual` süreleri 0,15–0,23 ms, yani süreç açılışı hâkim — bu satır
zincirin yokluğunu doğrular, hız hakkında bir şey söylemez.)

**gcc bu ailede Tulpar'ı da clang'ı da eziyor**: `ackermann` 4,2×, `treesum`
2,5×, `tak` 1,47× önde. Yani "özyinelemede C'den hızlıyız" **desteklenmiyor**;
desteklenen tek şey `fib`.

## Test 2 — Bilimsel taban clang, gcc değil

Tulpar LLVM 22 ile derliyor; frontend transformunun kendi başına değer katıp
katmadığını gösteren karşılaştırma **clang**. Sonuç iki yönlü:

- Tulpar clang'ı `fib`'de 5,92×, `tak`'ta 1,45×, `treesum`'da 1,36× geçiyor →
  frontend gerçekten değer katıyor.
- Ama **clang bu çekirdeklerde gcc'den çok zayıf** (`ackermann` 11,60 vs 3,30;
  `treesum` 20,00 vs 5,81). Yani clang tabanı Tulpar'ı **daha iyi** gösteriyor.

Dürüst yayın bu yüzden **iki tabanı da** vermek zorunda: yalnız clang vermek
iddiayı şişirir, yalnız gcc vermek frontend katkısını gizler.

## Test 3 — `strcat`: C'ye AYNI itoa verilirse ne olur?

Denetimin öngörüsü: "C aynı itoa'yı alınca strcat'i geri alır." **Doğrulandı.**

| Varyant | ortanca ± MAD |
|---|---:|
| C, `snprintf` (yayınlanan taban) | 37,11 ± 0,28 ms |
| **C, elle itoa** | **11,07 ± 0,10 ms** |
| C, elle itoa (clang) | 11,64 ± 0,13 ms |
| Tulpar | 13,85 ± 0,39 ms |

`strcat`'teki 2,8×'lik "kazanç" **tamamen** `snprintf` ↔ özel `itoa`
asimetrisiydi. Aynı araç verilince **C 1,25× öne geçiyor**. Kaynak:
[`strcat_itoa.c`](strcat_itoa.c) — `strcat.c` ile birebir aynı algoritma, tek
fark biçimlendirici.

## Geriye kalan savunulabilir iddia

| İddia | Durum |
|---|---|
| Node/Python/Java/C#'tan çok daha hızlı | ✅ her turda doğrulandı |
| `sieve` / `intloop`'ta C ile başa baş | ✅ ölçüldü (MAD içinde) |
| `fib`'de C'den hızlı | ✅ gcc 1,80 → Tulpar 0,67 (2,7×); clang'a karşı 5,9× |
| `strcat`'te C'den hızlı | ❌ **çürüdü** — aynı itoa ile C 1,25× önde |
| `arrayiter`'de C'den hızlı | ❌ çürüdü — C `-march=native` ile geri alıyor |
| Özyinelemede genel olarak C'den hızlı | ❌ çürüdü — `ackermann`/`treesum`/`tak`'ta gcc önde |
| Zincir pass'i genel bir kazanç | ❌ çürüdü — `ackermann`'da %21 gerileme |

Özet: Tulpar **tam sayı çekirdeklerinde C sınıfında** ve Node/Python/Java/C#'ın
açık ara önünde; C'yi geçtiği tek doğrulanmış çekirdek `fib`. "Düz döngü ve
özyineleme codegen'i C-sınıfıdır, C-üstü değildir."

## Yeniden üretme

```bash
cd benchmarks/fair/recursion
for k in fib ackermann tak treesum mutual; do
  gcc -O3 -march=native -flto $k.c -o /tmp/g_$k
  clang -O3 -march=native -flto $k.c -o /tmp/c_$k
  ../../../tulpar build $k.tpr /tmp/t_$k
  TULPAR_NO_SELFREC=1 ../../../tulpar build $k.tpr /tmp/tn_$k
done
```

## Tur 2 — `ackermann` gerilemesi teşhis edildi ve GİDERİLDİ

Üç hipotez sıralanmıştı. **İkisi elendi, sebep üçüncüsü çıktı.**

**H1 (argüman pozisyonu) — ÇÜRÜDÜ.** `ack(m-1, ack(m,n-1))` iç içe çağrısı
yerel değişkene alındı (`ackermann_hoist.tpr`, hesap birebir aynı). Gerileme
**aynen sürdü**: orijinal 0,77×, hoist 0,75×. gcc her iki yazımda da 3,18 ms —
yani yeniden yazım yapısal olarak nötr, sebep argüman pozisyonu değil.

**H2 (kod şişmesi / icache) — ÇÜRÜDÜ.** Sıcak fonksiyonların toplam boyutu
K=4'te `ack` için 1435 B, `fib` için 1018 B. Zen 5'in L1i'si 32 KB; iki
mertebe uzakta. Dahası desen **monoton değil** — icache baskısı boyutla
düzgün kötüleşirdi, oysa K=2 0,59× iken K=4 0,83×, K=5 0,61×.

**H3 (yerleşim/dönüşüm kaosu) — DESTEKLENDİ, ve pratik cevabı verdi.**
`TULPAR_SELFREC_DEPTH` anahtarı eklenip K taratıldı (K=0 zincirsiz taban):

| çekirdek | K=1 | K=2 | K=3 | K=4 (eski varsayılan) |
|---|---:|---:|---:|---:|
| `fib` | **8,53×** | 6,76× | 5,57× | 6,30× |
| `ackermann` | **1,15×** | 0,59× | 0,70× | 0,82× ⚠ |
| `tak` | 1,47× | 1,89× | 1,38× | 1,60× |
| `treesum` | 0,99× | 2,00× | 1,25× | 1,37× |
| **en kötü durum** | **1,03×** | 0,59× | 0,70× | 0,78× |

Hiçbir K her çekirdekte en iyi değil — ama **K=1'in en kötü durumu nötr
(1,03×), K=4'ünki zarar (0,78×)**. Bir optimizasyonun bazı programları
yavaşlatması hatadır; bazılarına daha az kazandırması değil. `SELFREC_DEPTH`
**4 → 1** yapıldı. Bu bir takas: `treesum` ve `tak` kazançlarının bir kısmını
kaybediyor, `fib` ve `ackermann` kazanıyor.

Mekanik açıklama: zincirin bütün işi **öz-özyineleme SCC kenarını kırmak**;
bunun için bir klon yetiyor. Daha derin zincirler yalnız kod ve yerleşim
kaosu ekliyor.

### Korumadaki ikinci hata: artefakt sınamak

Koruma IR'de `@fib.rec` sembolünü arıyordu. K=1'e inince klon **tamamen satır
içi alınıp yok oluyor** — yani optimizasyon her zamankinden iyi çalışırken
(8,4×) denetim düştü. Sembol bir *uygulama artefaktı*; sınanması gereken
**etki**. Kalıcı değişmezle değiştirildi: zincirli `@fib` gövdesi zincirsizden
daha çok öz-çağrı içerir (3 vs 1), çünkü özyinelemenin bir seviyesi açılır.

Ayrıca `build.sh`'a **gerileme kapısı** eklendi: `ackermann` zincirli sürümü
zincirsizden %25'ten fazla yavaş olamaz. Koruma artık yalnız "hızlandırıyor
mu" değil, "**başka şekillere zarar veriyor mu**" sorusunu da soruyor — asıl
boşluk buydu.

## Donmuş koşu — 2026-09-09T13:56:13

Tek koşu, tek timestamp, tüm alıntılar buradan.
Ham veri: [`results_recursion.csv`](results_recursion.csv) (25 satır).
**15 dönüşümlü tekrar, ortanca (ms).** Beş dilin hepsi aynı çıktıyı verdi.

| çekirdek | gcc | clang | rustc | **Tulpar** | Tulpar zincirsiz | zincir | Tulpar/gcc |
|---|---:|---:|---:|---:|---:|---:|---:|
| `fib(32)` | 1,66 | 3,91 | 3,88 | **0,47** | 3,94 | 8,38× | **3,54× önde** |
| `ackermann(3,9)` | **3,08** | 11,78 | 11,06 | 9,78 | 10,59 | 1,08× | 0,31× |
| `tak(18)` | **4,93** | 10,76 | 10,27 | 7,55 | 11,22 | 1,49× | 0,65× |
| `treesum(24)` | **5,84** | 19,70 | 19,30 | 21,05 | 19,83 | 0,94× | 0,28× |
| `mutual(20k)` | **1,17** | 1,42 | 1,49 | 1,53 | 1,52 | 1,00× | 0,76× |

Hepsi `-O3 -march=native -flto` / `-C target-cpu=native`; Tulpar jenerik hedefle.

**Gürültü tabanı satırı.** Aynı C kaynağının gcc↔clang yayılımı bu ailede
**1,21×–3,82×**. Yani *aynı dilin iki derleyicisi* arasında 3,8 kata varan
salınım var. Diller arası farkları bu ölçekle okuyun: 3,8×'in altındaki
herhangi bir fark "dil farkı" değil, "derleyici farkı" bandındadır.

**`mutual` düzeltildi.** İlk sürümde döngü `i`'yi 1000'er artırırken
`even_(i % 1000)` çağırıyordu — yani argüman hep 0'dı ve çekirdek anında
dönüyordu (0,15 ms, süreç açılışı hâkim). Artık gerçek iş yapıyor (1,2–1,5 ms).
Zincir burada hâlâ üretilmiyor (`klon=0`, karşılıklı özyineleme) — temiz
negatif kontrol.

## Güncel okuma

- `fib`: Tulpar gcc'nin **3,54×** önünde; clang ve rustc'nin 8,3×. Doğrulanmış
  tek kazanç, ve zincir düzeltmesiyle daha da güçlendi (0,67 → 0,47 ms).
- Ailenin geri kalanında Tulpar **gcc'nin 0,28×–0,76× ardında**, ama clang ve
  rustc ile başa baş ya da önünde.
- Ham özyineleme codegen'i (zincirsiz) gcc'nin her yerde 2–3,4× ardında.
- Zincir artık **hiçbir çekirdeği anlamlı şekilde yavaşlatmıyor** (en kötü
  0,94×, ölçüm oynamasının içinde).

Dürüst cümle: **döngü ağırlıklı kod C sınıfında; özyinelemede tablo şekle göre
değişiyor.** Taban gcc'nin belirgin ardında; otomatik özelleştirme pass'i
`fib`'de öne geçiriyor, geri kalanında açığı kapatmaya yetmiyor. gcc'nin bu
ailedeki üstünlüğü kapatılmamış açık bir konu.
