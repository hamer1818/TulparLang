# Adil dil karsilastirmasi — sonuclar

Uretildi: `benchmarks/fair/run.py` · 5 tekrar, en iyi deger · **dusuk = hizli** (ms).

Her satir ayni algoritmayi ayni veri yapisiyla kosar ve **ciktilar dogrulanir** — diller ayni sonucu basmazsa satir gecersiz sayilir.

| Dil | intloop | fib | sieve | strcat | arrayiter | mandelbrot | matmul | nbody | hashmap | qsort | particles | callfn | parse |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| C (gcc -O2) | 134.4 | 1.6 | 8.1 | 37.7 | 2.2 | 158.5 | 30.8 | 114.5 | 64.3 | 60.2 | 42.1 | 91.8 | 56.6 |
| C++ (g++ -O2) | 134.9 | 1.9 | 7.9 | 14.7 | 3.6 | 158.9 | 30.5 | 114.9 | 250.5 | 58.8 | 43.1 | 92.1 | 60.5 |
| Rust (-O3) | 144.1 | 3.7 | 8.8 | 18.6 | 1.8 | 155.2 | 35.8 | 114.5 | 322.5 | 57.4 | 36.9 | 59.1 | 77.8 |
| Go | 134.7 | 6.7 | 8.8 | 24.5 | 4.1 | 155.3 | 82.3 | 151.5 | 177.4 | 59.3 | 59.4 | 64.1 | 117.3 |
| C# (.NET) | 149.0 | 20.7 | 21.7 | 30.6 | 18.3 | 168.6 | 104.7 | 281.6 | 248.2 | 117.2 | 60.3 | 167.1 | 252.6 |
| Java | 145.3 | 13.0 | 21.9 | 35.2 | 18.5 | 170.8 | 73.5 | 121.0 | 171.2 | 90.2 | 85.6 | 75.0 | 186.8 |
| Node.js | 707.5 | 25.2 | 29.1 | 108.0 | 18.6 | 172.5 | 168.0 | 184.8 | 296.9 | 103.8 | 291.7 | 130.5 | 694.1 |
| Python | 3159.7 | 142.7 | 463.2 | 200.8 | 409.0 | 15531.8 | 18854.0 | 11801.2 | 395.8 | 1085.5 | 7236.9 | 1597.4 | 854.0 |
| Tulpar AOT | 134.6 | 0.5 | 7.6 | 10.0 | 1.2 | 158.0 | 36.7 | 114.7 | 106.1 | 69.6 | 52.7 | 78.4 | 75.3 |

## Tepe bellek (MB, rsswrap ile ayri bir kosumda ru_maxrss)

Dusuk = az bellek. Sure ile ayni kosumdan olculur.

| Dil | intloop | fib | sieve | strcat | arrayiter | mandelbrot | matmul | nbody | hashmap | qsort | particles | callfn | parse |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| C (gcc -O2) | 2.3 | 2.2 | 21.1 | 9.8 | 40.0 | 2.2 | 11.6 | 2.3 | 64.8 | 6.2 | 32.8 | 2.2 | 31.5 |
| C++ (g++ -O2) | 4.2 | 4.3 | 23.0 | 11.7 | 42.1 | 4.3 | 13.3 | 4.3 | 75.8 | 7.7 | 34.5 | 4.1 | 34.9 |
| Rust (-O3) | 2.5 | 2.4 | 21.5 | 9.9 | 40.5 | 2.4 | 11.7 | 2.5 | 129.5 | 6.2 | 33.0 | 2.6 | 32.2 |
| Go | 5.5 | 5.5 | 25.7 | 26.1 | 45.7 | 5.5 | 15.8 | 3.5 | 99.0 | 9.6 | 37.8 | 5.4 | 121.6 |
| C# (.NET) | 24.3 | 23.5 | 43.1 | 56.2 | 62.7 | 23.7 | 34.1 | 25.0 | 186.1 | 29.4 | 55.1 | 24.7 | 329.0 |
| Java | 43.0 | 42.6 | 66.4 | 71.2 | 88.1 | 43.4 | 59.8 | 44.2 | 210.4 | 54.4 | 110.9 | 43.2 | 483.2 |
| Node.js | 50.3 | 47.4 | 68.8 | 165.2 | 87.9 | 51.5 | 61.7 | 52.3 | 133.3 | 55.7 | 195.1 | 49.1 | 562.7 |
| Python | 9.5 | 9.3 | 47.4 | 139.2 | 200.7 | 9.4 | 56.3 | 9.8 | 115.3 | 47.7 | 201.3 | 9.2 | 307.2 |
| Tulpar AOT | 2.6 | 2.7 | 21.9 | 17.4 | 21.9 | 2.5 | 11.9 | 3.0 | 88.3 | 6.6 | 34.6 | 2.8 | 260.6 |

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
| 0.1 | 5.8 | 10.8 | 0.4 | 8.3 | 0.2 | 0.2 | 0.3 | 7.8 |

## Derleme suresi ve ikili boyutu (cekirdekler uzerinde ortanca)

| Dil | derleme (ms) | ikili (KB) | bos program ikilisi (KB) |
|---|---:|---:|---:|
| C (gcc -O2) | 31 | 16 | 15 |
| C++ (g++ -O2) | 88 | 16 | 15 |
| Rust (-O3) | 85 | 3732 | 3722 |
| Go | 15 | 2367 | 1906 |
| C# (.NET) | 634 | 5 | — |
| Java | 190 | 1 | — |
| Node.js | — | — | — |
| Python | — | — | — |
| Tulpar AOT | 61 | 1413 | 1413 |

