# Adil dil karsilastirmasi — sonuclar

Uretildi: `benchmarks/fair/run.py` · 5 tekrar, en iyi deger · **dusuk = hizli** (ms).

Her satir ayni algoritmayi ayni veri yapisiyla kosar ve **ciktilar dogrulanir** — diller ayni sonucu basmazsa satir gecersiz sayilir.

| Dil | intloop | fib | sieve | strcat | arrayiter | mandelbrot | matmul | nbody | hashmap | qsort | particles | callfn | parse |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| C (gcc -O2) | 134.7 | 1.7 | 8.0 | 37.8 | 2.3 | 158.7 | 31.1 | 114.5 | 72.7 | 57.4 | 42.2 | 91.1 | 56.7 |
| C++ (g++ -O2) | 134.9 | 1.9 | 8.1 | 14.7 | 2.8 | 159.2 | 30.9 | 114.8 | 236.7 | 58.5 | 42.3 | 91.4 | 60.5 |
| Rust (-O3) | 144.2 | 3.8 | 8.3 | 18.9 | 2.0 | 155.3 | 35.9 | 114.5 | 338.5 | 56.8 | 36.0 | 58.5 | 77.3 |
| Go | 134.8 | 6.8 | 8.4 | 25.0 | 4.0 | 155.4 | 81.9 | 152.5 | 175.0 | 58.7 | 59.5 | 67.5 | 116.4 |
| C# (.NET) | 150.8 | 20.9 | 21.4 | 31.9 | 18.9 | 170.3 | 106.9 | 281.4 | 257.8 | 118.3 | 59.6 | 166.6 | 250.7 |
| Java | 146.0 | 13.9 | 20.8 | 35.6 | 21.1 | 172.0 | 74.8 | 121.3 | 178.5 | 89.5 | 85.5 | 75.9 | 188.3 |
| Node.js | 708.5 | 24.6 | 29.3 | 109.5 | 20.4 | 174.9 | 172.4 | 182.8 | 311.9 | 106.8 | 277.8 | 129.2 | 708.2 |
| Python | 3136.2 | 140.7 | 450.0 | 201.4 | 410.0 | 15772.4 | 19346.9 | 11015.6 | 413.7 | 1095.2 | 7086.1 | 1551.3 | 842.9 |
| Tulpar AOT | 134.6 | 0.4 | 7.7 | 14.1 | 1.2 | 158.4 | 821.3 | 1304.8 | > 60 s | 121.2 | 329.6 | 298.6 | 196.3 |

## Tepe bellek (MB, rsswrap ile ayri bir kosumda ru_maxrss)

Dusuk = az bellek. Sure ile ayni kosumdan olculur.

| Dil | intloop | fib | sieve | strcat | arrayiter | mandelbrot | matmul | nbody | hashmap | qsort | particles | callfn | parse |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| C (gcc -O2) | 2.2 | 2.2 | 21.1 | 9.7 | 40.2 | 2.2 | 11.5 | 2.1 | 64.7 | 6.1 | 32.8 | 2.2 | 32.0 |
| C++ (g++ -O2) | 4.1 | 4.2 | 23.0 | 11.6 | 41.8 | 4.2 | 13.3 | 4.1 | 76.0 | 7.7 | 34.5 | 4.2 | 35.0 |
| Rust (-O3) | 2.5 | 2.4 | 21.4 | 10.0 | 40.5 | 2.5 | 11.6 | 2.4 | 129.4 | 6.2 | 33.0 | 2.4 | 32.4 |
| Go | 5.4 | 5.5 | 25.5 | 23.6 | 45.6 | 5.5 | 15.8 | 5.6 | 99.0 | 9.8 | 37.5 | 5.5 | 118.4 |
| C# (.NET) | 24.3 | 23.9 | 43.5 | 56.7 | 62.4 | 24.0 | 33.9 | 24.9 | 174.2 | 29.6 | 55.4 | 24.8 | 329.3 |
| Java | 42.7 | 42.8 | 67.1 | 71.8 | 88.3 | 42.9 | 57.6 | 44.0 | 216.0 | 52.0 | 110.4 | 43.2 | 482.9 |
| Node.js | 49.9 | 47.7 | 69.3 | 164.9 | 88.3 | 51.2 | 59.8 | 52.5 | 133.2 | 55.8 | 194.9 | 48.6 | 563.6 |
| Python | 9.0 | 9.3 | 47.3 | 139.3 | 200.7 | 9.6 | 56.3 | 9.8 | 115.3 | 47.6 | 201.2 | 9.5 | 307.0 |
| Tulpar AOT | 2.7 | 2.7 | 22.0 | 25.0 | 21.9 | 2.6 | 21.7 | 3.0 | — | 6.6 | 34.7 | 2.8 | 442.9 |

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
| 0.2 | 5.7 | 11.0 | 0.4 | 8.3 | 0.2 | 0.2 | 0.4 | 7.9 |

## Derleme suresi ve ikili boyutu (cekirdekler uzerinde ortanca)

| Dil | derleme (ms) | ikili (KB) | bos program ikilisi (KB) |
|---|---:|---:|---:|
| C (gcc -O2) | 32 | 16 | 15 |
| C++ (g++ -O2) | 88 | 16 | 15 |
| Rust (-O3) | 85 | 3732 | 3722 |
| Go | 16 | 2367 | 1906 |
| C# (.NET) | 694 | 5 | — |
| Java | 191 | 1 | — |
| Node.js | — | — | — |
| Python | — | — | — |
| Tulpar AOT | 61 | 1398 | 1398 |

