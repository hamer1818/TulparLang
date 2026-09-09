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

## Açık kalan

`ackermann` gerilemesi giderilmedi. Zincir muhtemelen çağrı sayısı / iç içe
çağrı biçimine göre kapılanmalı (`ack` gövdesinde özyinelemeli çağrı **iç içe**
geçiyor: `ack(m-1, ack(m, n-1))`; `fib`'de yan yana). Koruma da yalnız `fib`'i
sınadığı için bu gerilemeyi göremedi — aileyi kapsayacak şekilde genişletilmeli.
