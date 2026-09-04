---
tags: [moc, performance, benchmark]
---

# Performance & Benchmarks

Araçlar `benchmarks/`: `loadtest.c` (çok-thread keep-alive/close HTTP yük üreteci, 1µs histogram), `run_stress.sh`, `stress_server.tpr`, `stress_db_server.tpr`. Detay: `benchmarks/WINGS_STRESS.md`, `WINGS_VS_FASTAPI.md`.

## Dil kıyaslaması — `benchmarks/fair/` (2026-09-03)

Adil kural: **her dil** `BENCH_N`'i ortamdan okur (yoksa `gcc -O2`/`rustc -O3`
kapalı forma katlıyor ve boş programı ölçmüş oluyorsun — [[Tuzaklar#1]]), aynı
algoritma, aynı veri yapısı, çıktı doğrulanıyor. En iyi/medyan + boş program
taban çizgisi. Çalıştır: `python3 benchmarks/fair/run.py [test]`.

| Test | C | Rust | Go | **Tulpar** | Java | Node | Sıra |
|---|--:|--:|--:|--:|--:|--:|:--:|
| intloop (50M) | 134.6 | 144.5 | 135.6 | **135.6** | 147.7 | 715.2 | **2.–3.** |
| fib(32) | 1.6 | 3.8 | 6.7 | **4.3** | 13.4 | 27.7 | **3.** |
| sieve (5M) | 7.7 | 8.2 | 8.5 | **~9.9** | 21.2 | 29.3 | 4. |
| strcat (2M) | 37.8 | 19.1 | 25.4 | **31.6** | 35.9 | 105.6 | **3.** |
| arrayiter (5M) | 2.3 | 1.7 | 4.0 | **6.6** | 19.5 | 21.3 | 4. |

Hedef "her alanda 2.–3. sıra" 5 testin 3'ünde tutuyor.

`arrayiter` sonradan eklendi (2026-09-04): önceki dördü Tulpar'ın **en yaygın
döngü kalıbını** hiç ölçmüyordu — `for (int i = 0; i < len(a); ...)`. O kalıp
şekil önbelleği + `len` katlamasıyla 2.4× hızlandı ama tabloda görünmüyordu.
Her dil kendi deyimsel uzunluk erişimini kullanıyor.

### Dizi/bellek (sieve) — 19.9 → 13.3 ms nasıl geldi
İki yapısal değişiklik, ikisi de [[Memory Model]]'de ayrıntılı:

1. **Kutulanmamış int depolama.** `ObjArray` artık ya `items_` (kutulu
   `VMValue`, 16 bayt/eleman) ya da `idata` (ham `int64`, 8 bayt/eleman)
   tutuyor. `array_fill(n, <int>)` doğrudan kutulanmamış üretiyor. Int olmayan
   her şey `arr_items()` üzerinden tek seferde kutuya dönüyor (`arr_debox`).
2. **TBAA.** Eleman deposu ile ObjArray başlığı/değişken yuvaları ayrı
   takma-ad sınıfı. Bu olmadan LLVM `a[k]=1` yazmasının başlığı ezebileceğini
   varsayıp diziyi, etiketini, `count`'unu ve `idata`'sını **her yinelemede**
   yeniden okuyordu. Etiketlemeden sonra bunlar döngü dışına çıktı.

3. **Döngü-değişmezi dizi şekli önbelleği** (`src/aot/llvm_array_shape.cpp`).
   Döngü başında dizinin `idata` + `count`'u **bir kez** okunup yerelde
   tutuluyor; gövdede yalnız sınır karşılaştırması kalıyor — Go'nun dilim
   uzunluğunu yazmaçta tutmasıyla aynı fikir. 13.3 → ~10 ms.

### Önce ölç, sonra yaz — bu işin karar anı
Makine kurmadan önce **tavan ölçüldü**: codegen'e geçici bir "hiçbir denetim
yok" yolu konup elek çalıştırıldı. 12.9 → 9.1 ms. Sonra denetimler tek tek
geri açılarak maliyet ayrıştırıldı:

| yapılandırma | ms | ek |
|---|--:|--:|
| hiçbir denetim | 9.1 | — |
| + sınır denetimi | 11.5 | **+2.4** |
| + tür denetimi | 12.4 | +0.9 |
| + idata denetimi | 12.9 | +0.5 |

Bu tablo tasarımı belirledi: en pahalısı sınır denetimiydi **ama** pahalı olan
denetimin kendisi değil, `count`'un her yinelemede **bellekten** okunmasıydı.
Onu yazmaca almak = şekil önbelleği. Ölçmeden başlansaydı muhtemelen i32'ye
daraltmaya girilecekti — oysa oran N ile küçülüyordu, yani darboğaz bant
genişliği değildi. **Yanlış işe girilmesini ölçüm engelledi.**

### Denendi ve ÇÜRÜDÜ (tekrar denemeyin)
İkisi de makul görünüyordu, ikisi de **iç içe ölçümde yavaşlattı**:

| deneme | beklenti | ölçüm |
|---|---|---|
| Üst düzey küreselleri `internal` linkage yapmak | LLVM yazmaca alır | 9.6 → **11.0 ms** |
| Salt-okunur int küreselleri döngü başında yerele kopyalamak | 2 yükleme eksilir | 9.2 → **10.2 ms** |

İkincisi özellikle şaşırtıcıydı: elekte `i` ve `n` gerçekten her turda bellekten
okunuyor ve o döngüde yazılmıyorlar. Yerel kopya çıkarmak yine de yavaşlattı
(muhtemelen LLVM'in kendi analizine karışıyor). **Kod doğruydu, testler yeşildi
— sadece daha yavaştı.** Ölçmeseydik "iyileştirme" diye girecekti.

Küresellerin ham adla yazılması ayrı bir **hata** olarak çıktı ve önekle
çözüldü (bkz. [[Tuzaklar#6h]]) — linkage değiştirmeden, bedelsiz.

**Kalan fark neden var (Tulpar ~10 / Go 8.5):**
- Tulpar'ın `int`'i 64 bit; C/Go/Java bu testte 4 baytlık eleman kullanıyor →
  aynı algoritmada **2× bellek trafiği**. Bu bir semantik farkı, gerileme değil.
- Şekil önbelleği yalnız **kanıtlanabilen** döngülerde açılıyor: gövdede
  beyaz listede olmayan bir çağrı varsa vazgeçiliyor (push/pop şekli
  değiştirir). Yani kullanıcı fonksiyonu çağıran sıcak döngüler hâlâ tam
  denetim ödüyor.
- Bir sonraki adım: `for-in` şeker açılımını da kapsamak, ve i32'ye daraltma
  (V8'in SMI dizileri gibi) — ama i32 ölçüme göre küçük bir kazanç.

### Kanıtın genişletilmesi (2026-09-04)
1. **`for` döngüleri.** Önbellek yalnız `while`a bağlıydı. `for`a da bağlandı;
   **artım ifadesi de kanıta dahil** (`for (...; ...; a.push(i))` şekli
   değiştirebilir). Ölçüldü: 7.5 → 5.1 ms.
2. **Saf yerleşikler.** Her çağrı kanıtı düşürüyordu, dolayısıyla
   `for (int i = 0; i < len(a); ...)` hiç yararlanamıyordu. Elle doğrulanmış
   kısa bir liste (`len`/`length`/`abs`/`min`/`max`/`sqrt`/`pow`/`floor`/
   `ceil`/`round`/`toInt`/`toFloat`/`ord`/`chr`) artık kanıtı düşürmüyor.
   Kararı **codegen** veriyor: kullanıcı aynı adı tanımladıysa yerleşik
   sayılmıyor. Listeye ekleme ölçütü "saf mı" DEĞİL — "bir dizinin
   count/items_/idata alanını değiştirebilir mi" ve "kullanıcı koduna geri
   dönebilir mi" (`call`, `map`, karşılaştırıcılı `sort` bu yüzden yok).
3. **`len` önbellekten.** Kanıt zaten uzunluğun sabit olduğunu söylüyor, o
   hâlde çağrıya gerek yok. Önbellekte **ayrı** bir gerçek-uzunluk yuvası var:
   `count_slot` yerine geçemez, çünkü o kutulanmamış olmayan dizide bilerek 0
   tutuyor. Dizi olmayan değer için −1 yazılıp eski çağrıya düşülüyor, yani
   `len(dizgi)` / `len(json)` birebir korunuyor. Ölçüldü: 14.2 → 5.9 ms.

**Yük taşıyan varsayım:** ayırma yapan yerleşikler önbelleği bozmuyor, çünkü
arena **öbek-zincirli bump ayırıcı** — `aot_arena_alloc` yeni bloğu zincire
ekliyor, `realloc` yok, bloklar asla taşınmıyor. Arena tek büyük blok olsaydı
bu liste güvensiz olurdu.

Ölçüm hijyeni: oran N ile **küçülüyor** (N=200k'da Go'nun 2.36×'i, N=5M'de
1.55×'i) — yani darboğaz saf bant genişliği değil, erişim başına sabit hesap.
Tek bir N'de ölçüp "bellek bağlı" demek yanıltıcı olurdu.

## In-memory (HTTP katmanı, 14 CPU)
| Mod | keep-alive tepe `/ping` | p50/p99 | RSS |
|-----|------:|:---:|---:|
| serve (tek bağlantı) | 4.2k RPS | 230µs/379µs | 6.9 MB |
| pool (14w) | ~39.8k RPS | 360µs/671µs | 8.4 MB |
| evented (tek thread) | **57.8k RPS** | 801µs/1.7ms | 7.0 MB |

- pool çekirdek sayısına **lineer** ölçeklenir, ~14'te plato. evented hafif handler'da en yüksek RPS.
- Tüm modlar 500+ eşzamanlı bağlantı / 500k+ istekte **RSS düz**, `err=0`. Dispatch 4–34µs.

## DB-bağlı (asıl darboğaz)
| | read PK | write rollback → WAL |
|--|--:|--:|
| pool | 23.8k | 8.8k → **20.4k** |
| evented | **29–32k** | 15.8k |

**Asıl tavan: SQLite paylaşımlı-handle serileştirmesi, HTTP değil.** evented (tek thread) read'de pool'u geçer (mutex çekişmesi yok). WAL write'ı 2.3× yapar (artık [[SQLite and DB]] varsayılanı).

## Kıyas
FastAPI'yi 5–15× geçer (Node Fastify ligi); Go/Rust altında. Asıl koz: **düşük bellek (8 MB) + sub-ms latency + tek dosya binary**.

## İlgili
[[Wings Serve Modes]] · [[Memory Leak Fixes]] · [[SQLite and DB]] · [[Roadmap]]
