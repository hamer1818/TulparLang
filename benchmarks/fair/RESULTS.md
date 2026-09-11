# Adil dil karsilastirmasi — sonuclar

Uretildi: `benchmarks/fair/run.py` · 5 tekrar, en iyi deger · **dusuk = hizli** (ms).

Her satir ayni algoritmayi ayni veri yapisiyla kosar ve **ciktilar dogrulanir** — diller ayni sonucu basmazsa satir gecersiz sayilir.

| Dil | intloop | fib | sieve | strcat | arrayiter | mandelbrot | matmul | nbody |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| C (gcc -O2) | 134.6 | 1.6 | 7.7 | 38.0 | 2.5 | 158.6 | 31.0 | 114.8 |
| C++ (g++ -O2) | 135.0 | 1.9 | 7.9 | 14.8 | 3.1 | 159.1 | 30.7 | 115.0 |
| Rust (-O3) | 144.5 | 3.7 | 8.4 | 19.0 | 2.1 | 155.7 | 35.8 | 114.8 |
| Go | 134.9 | 6.7 | 8.0 | 24.7 | 4.4 | 155.5 | 83.1 | 151.8 |
| C# (.NET) | 150.5 | 20.5 | 21.5 | 32.7 | 17.7 | 169.8 | 105.4 | 281.0 |
| Java | 144.9 | 12.8 | 20.7 | 33.0 | 18.5 | 166.0 | 72.4 | 120.6 |
| Node.js | 712.1 | 26.4 | 28.7 | 102.3 | 20.5 | 173.8 | 174.4 | 181.3 |
| Python | 3158.7 | 141.7 | 456.9 | 201.3 | 409.3 | 15721.3 | 18995.5 | 10980.3 |
| Tulpar AOT | 134.8 | 0.4 | 8.0 | 13.3 | 1.4 | 158.4 | 824.5 | 1326.1 |

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

## Bos program taban cizgisi (ms)

Olctugumuz seyin ne kadari surec baslatma? Is yukleri bunu golgede birakacak kadar buyuk secildi.

| C (gcc -O2) | Python | Node.js | C++ (g++ -O2) | C# (.NET) | Tulpar AOT |
|---:|---:|---:|---:|---:|---:|
| 0.2 | 5.8 | 11.1 | 0.5 | 8.2 | 0.3 |

