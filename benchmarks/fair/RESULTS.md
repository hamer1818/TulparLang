# Adil dil karsilastirmasi — sonuclar

Uretildi: `benchmarks/fair/run.py` · 5 tekrar, en iyi deger · **dusuk = hizli** (ms).

Her satir ayni algoritmayi ayni veri yapisiyla kosar ve **ciktilar dogrulanir** — diller ayni sonucu basmazsa satir gecersiz sayilir.

| Dil | intloop | fib | sieve | strcat | arrayiter | mandelbrot | matmul | nbody | hashmap | qsort | particles | callfn | parse |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| C (gcc -O2) | 134.6 | 1.6 | 7.5 | 37.8 | 2.6 | 158.5 | 30.8 | 114.5 | 59.8 | 57.3 | 41.9 | 91.3 | 56.4 |
| C++ (g++ -O2) | 134.8 | 1.9 | 7.9 | 14.8 | 2.6 | 158.9 | 30.7 | 114.8 | 196.4 | 57.9 | 42.9 | 91.5 | 60.2 |
| Rust (-O3) | 144.1 | 3.7 | 8.4 | 19.0 | 2.2 | 155.3 | 35.7 | 114.4 | 290.5 | 56.8 | 36.2 | 59.2 | 77.0 |
| Go | 134.7 | 6.6 | 8.3 | 24.4 | 3.6 | 155.4 | 83.0 | 151.1 | 148.9 | 58.7 | 58.9 | 65.3 | 118.2 |
| C# (.NET) | 149.1 | 20.4 | 21.0 | 29.3 | 18.1 | 169.1 | 104.0 | 280.5 | 226.8 | 118.0 | 60.5 | 166.5 | 248.2 |
| Java | 143.7 | 12.4 | 19.8 | 32.5 | 19.6 | 165.5 | 72.4 | 120.4 | 167.3 | 87.4 | 84.1 | 74.9 | 185.7 |
| Node.js | 708.0 | 24.7 | 27.3 | 104.9 | 19.2 | 169.3 | 168.5 | 182.1 | 268.6 | 104.6 | 276.1 | 128.3 | 698.6 |
| Python | 3204.6 | 142.7 | 452.2 | 201.1 | 412.6 | 16159.9 | 19117.7 | 11589.2 | 376.4 | 1075.3 | 7012.2 | 1571.8 | 872.4 |
| Tulpar AOT | 134.5 | 0.4 | 7.8 | 9.5 | 1.3 | 154.1 | 36.1 | 114.8 | 77.8 | 70.8 | 36.3 | 81.4 | 70.7 |

## Tepe bellek (MB, rsswrap ile ayri bir kosumda ru_maxrss)

Dusuk = az bellek. Sure ile ayni kosumdan olculur.

| Dil | intloop | fib | sieve | strcat | arrayiter | mandelbrot | matmul | nbody | hashmap | qsort | particles | callfn | parse |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| C (gcc -O2) | 2.2 | 2.1 | 21.1 | 9.8 | 40.1 | 2.2 | 11.8 | 2.2 | 64.6 | 6.2 | 32.8 | 2.2 | 31.4 |
| C++ (g++ -O2) | 4.2 | 4.2 | 23.0 | 11.7 | 42.0 | 4.3 | 13.2 | 4.1 | 76.0 | 7.8 | 34.6 | 4.2 | 34.4 |
| Rust (-O3) | 2.5 | 2.4 | 21.5 | 10.4 | 40.6 | 2.4 | 11.8 | 2.5 | 129.3 | 6.3 | 33.0 | 2.5 | 31.4 |
| Go | 5.6 | 3.5 | 25.7 | 28.4 | 45.7 | 5.6 | 17.6 | 5.5 | 99.0 | 9.8 | 37.8 | 3.5 | 143.2 |
| C# (.NET) | 24.4 | 23.8 | 43.6 | 56.6 | 62.5 | 23.9 | 33.9 | 24.6 | 186.5 | 29.5 | 55.7 | 24.9 | 329.2 |
| Java | 42.5 | 42.6 | 66.5 | 71.7 | 88.1 | 42.8 | 59.4 | 44.1 | 217.4 | 52.0 | 110.4 | 43.5 | 479.2 |
| Node.js | 50.5 | 47.4 | 69.6 | 165.0 | 88.4 | 51.5 | 62.0 | 52.6 | 134.0 | 55.7 | 196.7 | 49.1 | 562.7 |
| Python | 9.5 | 9.3 | 47.6 | 140.7 | 200.6 | 9.5 | 56.3 | 9.9 | 115.5 | 47.5 | 201.3 | 9.1 | 306.7 |
| Tulpar AOT | 2.7 | 2.7 | 21.9 | 17.5 | 22.0 | 2.7 | 12.2 | 3.0 | 64.6 | 6.6 | 33.6 | 2.8 | 222.2 |

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
| 0.2 | 5.8 | 11.0 | 0.4 | 8.3 | 0.2 | 0.2 | 0.4 | 7.6 |

## Derleme suresi ve ikili boyutu (cekirdekler uzerinde ortanca)

| Dil | derleme (ms) | ikili (KB) | bos program ikilisi (KB) |
|---|---:|---:|---:|
| C (gcc -O2) | 31 | 16 | 15 |
| C++ (g++ -O2) | 85 | 16 | 15 |
| Rust (-O3) | 81 | 3732 | 3722 |
| Go | 14 | 2367 | 1906 |
| C# (.NET) | 628 | 5 | — |
| Java | 180 | 1 | — |
| Node.js | — | — | — |
| Python | — | — | — |
| Tulpar AOT | 63 | 1421 | 1413 |

