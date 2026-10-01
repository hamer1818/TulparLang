# Adil dil karsilastirmasi — sonuclar

Uretildi: `benchmarks/fair/run.py` · 5 tekrar, en iyi deger · **dusuk = hizli** (ms).

Her satir ayni algoritmayi ayni veri yapisiyla kosar ve **ciktilar dogrulanir** — diller ayni sonucu basmazsa satir gecersiz sayilir.

| Dil | intloop | fib | sieve | strcat | arrayiter | mandelbrot | matmul | nbody | hashmap | qsort | particles | callfn | parse |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| C (gcc -O2) | 134.4 | 1.6 | 7.8 | 37.8 | 2.2 | 158.7 | 30.7 | 114.7 | 68.2 | 57.4 | 42.1 | 92.1 | 56.0 |
| C++ (g++ -O2) | 135.1 | 1.8 | 8.0 | 14.7 | 3.3 | 159.1 | 30.9 | 115.1 | 274.1 | 58.7 | 42.5 | 92.3 | 61.0 |
| Rust (-O3) | 144.3 | 3.7 | 8.2 | 19.6 | 1.5 | 155.6 | 36.0 | 114.7 | 386.7 | 57.4 | 36.2 | 58.5 | 77.1 |
| Go | 134.9 | 6.7 | 8.1 | 24.6 | 4.0 | 155.5 | 81.8 | 151.5 | 188.5 | 58.9 | 60.1 | 64.9 | 115.9 |
| C# (.NET) | 150.8 | 20.5 | 21.6 | 30.4 | 18.2 | 170.6 | 105.5 | 282.3 | 248.9 | 119.6 | 60.3 | 167.2 | 250.0 |
| Java | 146.4 | 12.8 | 20.9 | 32.9 | 18.4 | 171.6 | 74.2 | 122.8 | 180.2 | 89.2 | 86.2 | 76.2 | 188.5 |
| Node.js | 717.9 | 24.5 | 28.3 | 102.1 | 18.8 | 174.3 | 171.0 | 185.5 | 349.8 | 105.8 | 284.4 | 128.8 | 702.7 |
| Python | 3649.3 | 142.1 | 457.0 | 203.9 | 484.7 | 16903.0 | 18976.7 | 10999.1 | 493.1 | 1107.8 | 7057.8 | 1593.2 | 931.1 |
| Tulpar AOT | 134.6 | 0.4 | 7.9 | 13.4 | 1.1 | 158.2 | 37.2 | 187.3 | 183.4 | 122.8 | 52.6 | 170.1 | 126.3 |

## Tepe bellek (MB, rsswrap ile ayri bir kosumda ru_maxrss)

Dusuk = az bellek. Sure ile ayni kosumdan olculur.

| Dil | intloop | fib | sieve | strcat | arrayiter | mandelbrot | matmul | nbody | hashmap | qsort | particles | callfn | parse |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| C (gcc -O2) | 2.1 | 2.3 | 21.1 | 9.7 | 40.1 | 2.2 | 11.6 | 2.2 | 64.6 | 6.2 | 32.8 | 2.2 | 32.1 |
| C++ (g++ -O2) | 4.3 | 4.2 | 23.0 | 12.7 | 42.0 | 4.3 | 13.3 | 4.2 | 76.0 | 7.7 | 34.5 | 4.1 | 35.0 |
| Rust (-O3) | 2.3 | 2.4 | 21.5 | 10.4 | 40.6 | 2.3 | 11.6 | 2.5 | 129.4 | 6.1 | 32.9 | 2.6 | 32.0 |
| Go | 3.4 | 3.5 | 23.6 | 26.6 | 43.6 | 3.5 | 15.8 | 3.5 | 100.9 | 9.6 | 37.8 | 3.5 | 143.2 |
| C# (.NET) | 24.3 | 23.7 | 43.1 | 56.4 | 62.5 | 23.9 | 34.1 | 24.3 | 186.5 | 29.5 | 55.4 | 24.4 | 329.1 |
| Java | 42.5 | 42.3 | 66.4 | 71.8 | 88.4 | 43.2 | 59.7 | 44.1 | 216.2 | 52.1 | 110.5 | 43.7 | 483.1 |
| Node.js | 50.4 | 48.6 | 69.8 | 166.1 | 89.1 | 52.5 | 63.4 | 52.8 | 134.8 | 57.0 | 196.9 | 49.9 | 564.9 |
| Python | 9.5 | 9.2 | 47.3 | 139.0 | 200.7 | 9.4 | 56.6 | 9.8 | 115.2 | 47.6 | 201.1 | 9.3 | 306.9 |
| Tulpar AOT | 2.6 | 2.5 | 21.8 | 24.8 | 22.0 | 2.5 | 12.2 | 3.0 | 439.5 | 6.7 | 34.3 | 2.8 | 440.3 |

## Is yukleri ve cikti mutabakati

| Kiyas | BENCH_N | Ne olcer | Ortak cikti |
|---|---:|---|---|
| `intloop` | 50000000 | tamsayı aritmetiği (zincirleme bağımlılık) | `723378784` |
| `fib` | 32 | özyineleme (çağrı maliyeti) | `2178309` |
| `sieve` | 5000000 | dizi/bellek erişimi | `348513` |
| `strcat` | 2000000 | dizgi kurma + tarama | `7780000 2000000` |
| `arrayiter` | 5000000 | dizi yineleme (deyimsel uzunlukla) | `12499997500000` |
| `mandelbrot` | 2000 | kayan nokta aritmetigi (dizi YOK) | `97631088` |
| `matmul` | 640 | kayan nokta dizisi (depolama + bant genisligi) | `3027049920` |
| `nbody` | 3000000 | kayan nokta + kucuk dizi + sqrt | `-169019082` |
| `hashmap` | 1000000 | dizgi anahtarli sozluk: N ekleme + N arama | `1000000 499999500000` |
| `qsort` | 1000000 | elle hizli siralama (rastgele erisim + ozyineleme) | `0 500621 999999 735541235 0` |
| `particles` | 1000000 | struct dizisi uzerinde fizik adimi (50 adim) | `345506039590` |
| `callfn` | 20000000 | fonksiyon degeri tablosundan dolayli cagri | `336713` |
| `parse` | 5000000 | metin kur + bol + tamsayiya cevir | `5000000 249997500000` |

## Bos program taban cizgisi (ms)

Olctugumuz seyin ne kadari surec baslatma? Is yukleri bunu golgede birakacak kadar buyuk secildi.

| C (gcc -O2) | Python | Node.js | C++ (g++ -O2) | C# (.NET) | Tulpar AOT | Rust (-O3) | Go | Java |
|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| 0.1 | 5.7 | 10.9 | 0.4 | 8.1 | 0.2 | 0.2 | 0.3 | 8.2 |

## Derleme suresi ve ikili boyutu (cekirdekler uzerinde ortanca)

| Dil | derleme (ms) | ikili (KB) | bos program ikilisi (KB) |
|---|---:|---:|---:|
| C (gcc -O2) | 32 | 16 | 15 |
| C++ (g++ -O2) | 88 | 16 | 15 |
| Rust (-O3) | 89 | 3732 | 3722 |
| Go | 15 | 2367 | 1906 |
| C# (.NET) | 632 | 5 | — |
| Java | 187 | 1 | — |
| Node.js | — | — | — |
| Python | — | — | — |
| Tulpar AOT | 69 | 1402 | 1402 |

