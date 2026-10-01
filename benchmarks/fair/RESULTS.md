# Adil dil karsilastirmasi — sonuclar

Uretildi: `benchmarks/fair/run.py` · 5 tekrar, en iyi deger · **dusuk = hizli** (ms).

Her satir ayni algoritmayi ayni veri yapisiyla kosar ve **ciktilar dogrulanir** — diller ayni sonucu basmazsa satir gecersiz sayilir.

| Dil | intloop | fib | sieve | strcat | arrayiter | mandelbrot | matmul | nbody | hashmap | qsort | particles | callfn | parse |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| C (gcc -O2) | 134.5 | 1.6 | 7.6 | 37.8 | 2.9 | 158.8 | 30.4 | 114.8 | 67.1 | 60.1 | 42.1 | 91.1 | 55.9 |
| C++ (g++ -O2) | 134.8 | 1.9 | 8.1 | 14.8 | 3.7 | 159.1 | 31.0 | 115.2 | 199.5 | 58.3 | 42.8 | 91.8 | 60.6 |
| Rust (-O3) | 144.1 | 3.8 | 8.2 | 18.7 | 1.4 | 155.7 | 35.8 | 114.7 | 301.5 | 57.3 | 36.1 | 58.5 | 76.6 |
| Go | 134.7 | 6.7 | 8.5 | 24.4 | 4.4 | 155.8 | 84.1 | 151.6 | 157.9 | 58.7 | 59.6 | 66.8 | 117.0 |
| C# (.NET) | 150.5 | 20.7 | 22.1 | 29.9 | 19.0 | 170.2 | 107.4 | 280.9 | 232.5 | 118.5 | 59.5 | 165.7 | 247.1 |
| Java | 144.6 | 13.1 | 21.6 | 33.9 | 21.0 | 170.8 | 74.6 | 121.3 | 169.2 | 90.3 | 85.0 | 74.3 | 189.1 |
| Node.js | 708.9 | 25.3 | 29.0 | 109.3 | 20.4 | 173.5 | 174.7 | 185.1 | 290.7 | 103.9 | 277.8 | 128.1 | 719.5 |
| Python | 3619.8 | 142.7 | 458.0 | 201.3 | 479.1 | 15824.2 | 19455.4 | 10994.3 | 417.0 | 1071.6 | 6992.4 | 1572.4 | 908.4 |
| Tulpar AOT | 134.5 | 0.4 | 7.4 | 13.6 | 1.2 | 158.2 | 37.1 | 115.3 | 103.2 | 69.8 | 52.1 | 75.0 | 119.6 |

## Tepe bellek (MB, rsswrap ile ayri bir kosumda ru_maxrss)

Dusuk = az bellek. Sure ile ayni kosumdan olculur.

| Dil | intloop | fib | sieve | strcat | arrayiter | mandelbrot | matmul | nbody | hashmap | qsort | particles | callfn | parse |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| C (gcc -O2) | 2.2 | 2.3 | 21.2 | 9.7 | 40.2 | 2.2 | 11.7 | 2.1 | 64.7 | 6.1 | 32.8 | 2.0 | 31.9 |
| C++ (g++ -O2) | 4.2 | 4.1 | 23.0 | 11.7 | 42.1 | 4.2 | 13.2 | 4.4 | 75.9 | 7.5 | 34.5 | 4.2 | 34.3 |
| Rust (-O3) | 2.4 | 2.4 | 21.6 | 10.2 | 40.7 | 2.5 | 11.6 | 2.5 | 129.3 | 6.3 | 33.0 | 2.6 | 32.0 |
| Go | 5.5 | 5.5 | 25.8 | 22.2 | 43.8 | 3.5 | 17.8 | 5.6 | 105.0 | 9.8 | 37.8 | 5.6 | 118.0 |
| C# (.NET) | 24.8 | 23.8 | 43.6 | 56.7 | 63.0 | 24.4 | 34.2 | 24.6 | 174.1 | 29.8 | 55.0 | 24.4 | 329.0 |
| Java | 42.7 | 42.7 | 66.6 | 71.1 | 87.8 | 43.1 | 59.4 | 43.9 | 216.7 | 52.1 | 110.5 | 43.5 | 482.6 |
| Node.js | 50.1 | 47.8 | 69.2 | 164.6 | 88.0 | 51.5 | 59.6 | 52.7 | 133.7 | 55.5 | 194.9 | 49.4 | 563.1 |
| Python | 9.5 | 9.3 | 47.4 | 139.5 | 200.2 | 9.2 | 56.3 | 9.9 | 115.3 | 47.7 | 201.3 | 9.2 | 307.6 |
| Tulpar AOT | 2.6 | 2.7 | 22.0 | 17.4 | 22.0 | 2.6 | 12.0 | 3.0 | 111.2 | 6.7 | 33.5 | 2.8 | 374.1 |

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
| 0.1 | 5.7 | 11.7 | 0.4 | 8.2 | 0.2 | 0.2 | 0.3 | 7.4 |

## Derleme suresi ve ikili boyutu (cekirdekler uzerinde ortanca)

| Dil | derleme (ms) | ikili (KB) | bos program ikilisi (KB) |
|---|---:|---:|---:|
| C (gcc -O2) | 31 | 16 | 15 |
| C++ (g++ -O2) | 88 | 16 | 15 |
| Rust (-O3) | 94 | 3732 | 3722 |
| Go | 15 | 2367 | 1906 |
| C# (.NET) | 636 | 5 | — |
| Java | 189 | 1 | — |
| Node.js | — | — | — |
| Python | — | — | — |
| Tulpar AOT | 69 | 1408 | 1407 |

