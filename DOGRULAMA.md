# Doğrulama listesi — 2026-09-11 ölçüm turu

Bu dosya **bağımsız denetim içindir.** Aşağıdaki her madde bir iddiadır; her
birinin yanında onu **yeniden üretecek komut** ve **beklenen sonuç** var.
Amaç, iddiaların bu oturumun hafızasına değil, koşulabilir bir düzeneğe
dayandığını göstermek.

- Referans commit: `origin/main` = `27302c3` (PR #313 merge'ü)
- Ayrıntılı gerekçeler: [`FINDINGS.md`](FINDINGS.md) (32 kural, 11 sözleşme)
- Ortam: Linux, LLVM 22 yerel / **LLVM 18 CI**. Sürüm farkı önemlidir — 32. maddeye bak.

> **Denetleyene not:** "yeşil gördüm" yetmez. Bir nöbetçinin kırmızıya
> dönebildiğini görmeden onun bir şey ölçtüğünü varsayma (kural #10). Aşağıda
> nerede enjeksiyon yapılacağı yazılı.

---

## 0. Ön koşul — her şey yeşil mi?

```bash
./build.sh && ./build.sh suites && ./build.sh test && LC_ALL=C bash tests/typeinfer/run.sh
```

Beklenen: **80/80 suite**, örnekler yeşil, typeinfer **13 pass + 17 fail-fixture**,
ve suites çıktısında şu satırlar:

```
S4 kilitli · S3 kilitli · Yigin temiz (13 sekil + 50 builtin) ·
TypedValue ilklendirmesi tam · korpus tani tabani: 14 tani, degismedi
```

---

## A. Düzeltilen hatalar — her biri yeniden üretilebilir

### 1. Dizi literali döngüde yığın sızdırıyordu (R11, birinci dalga)

`alloca` döngü gövdesine düşüyordu; `alloca` ancak fonksiyon dönünce çözülür.

```bash
printf 'int i = 0;\nwhile (i < 2000000) { array j = [1,2,3]; i = i + 1; }\nprint("bitti");\n' > /tmp/r11.tpr
./tulpar /tmp/r11.tpr          # beklenen: "bitti", exit 0
```

Eskiden **175 000** yinelemede SIGSEGV. Aritmetik: 8 MB yığın / (3 eleman ×
16 bayt) = 174 762.

### 2. Aynı hata 50 builtin'de (R11, ikinci dalga)

`MATH1_FUNC` / `MATH2_FUNC` / `STR1_FUNC` / `STR2_FUNC` makrolarının dördü de
ham `LLVMBuildAlloca` kullanıyordu.

```bash
printf 'int i = 0; int n = 0;\nwhile (i < 1500000) { n = n + mod(i,10) + toInt(sqrt(2.0)); i = i + 1; }\nprint(n);\n' > /tmp/r11b.tpr
./tulpar /tmp/r11b.tpr         # beklenen: sayı basar, exit 0
```

### 3. `TypedValue.boxed` ilklendirilmemişti ⚠ EN SİNSİ

L1 kısa-devre düzeltmesinde `TypedValue sc;` yazılmıştı; `.boxed` yığın çöpü
kalıyordu ve dosyada 30 yerde okunuyor.

**Yerelde Release/LLVM22'de GÖRÜNMÜYORDU**; CI'da (LLVM 18) `01_hello_world`
dahil 16 örnekte derleyici SEGV etti.

```bash
# Sanitizer'lı derleyiciyle doğrula (hatanın ortamdan bağımsız olduğunun kanıtı):
cmake -S . -B build-asan -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DCMAKE_C_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer" \
  -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer"
cmake --build build-asan -j --target tulpar
ASAN_OPTIONS=detect_leaks=0 ./build-asan/tulpar --aot examples/wings_simple_test.tpr
```

Beklenen: **sanitizer bulgusu yok**, exit 0.

### 4. EOF token'ı kapasite denetimsiz yazılıyordu (önceden vardı)

`aot_pipeline.cpp`: döngü tam kapasitede bitince tampon bir eleman dışına
taşıyordu. `realloc`'un boş payına düştüğü için sessizdi.

```bash
ASAN_OPTIONS=detect_leaks=0 ./build-asan/tulpar --aot examples/23_struct_heap.tpr
```

Beklenen: `runtime error: store to address ... insufficient space` **YOK**.

### 5. `&&` / `||` kısa devre yapmıyordu (L1)

```bash
./tulpar tests/short_circuit.test.tpr     # 9 test, hepsi PASS
```

### 6. Akış ortası throw, gövdeye ham HTTP enjekte ediyordu (S4)

```bash
DISPLAY= WAYLAND_DISPLAY= python3 tests/stream_contract_smoke.py
```

Beklenen: 16 kontrol PASS, `S4 kilitli`.

⚠ **Kırmızıya dönebilirliği kanıtla:** `lib/wings.tpr` içindeki iki
`_wings_stream_started = 1;` satırını yorum satırı yap, `./build.sh` ile
yeniden derle, sondayı koş. **4 kontrol kırmızı vermeli** ve WS kuyruğunda
`\x81\x03ilk` ardına ham `HTTP/1.1 500` enjekte edilmeli. Sonra geri al.

### 7. Sembol çözümü bildirim sırasına bağlıydı (P23)

```bash
printf 'func oku(): int { return length(sayac); }\nint sayac = 5;\nprint(oku());\n' > /tmp/p23.tpr
LC_ALL=C ./tulpar typecheck /tmp/p23.tpr
```

Beklenen: **1 tip hatası** (`length` int'e uygulanamaz). Eskiden sessizce
geçiyordu. Bildirimi yukarı taşıyınca da aynı hata gelmeli — yani **simetrik**.

Başlatma tarafı ayrı denetlenir:

```bash
printf 'print(sayac);\nint sayac = 5;\n' > /tmp/p23b.tpr
LC_ALL=C ./tulpar typecheck /tmp/p23b.tpr
```

Beklenen: `NOT YET INITIALISED ... move the declaration above` — **"bulunamadı"
DEMEZ** (ad çözüldü, değeri henüz yok).

### 8. Thread sözleşmesi: kopyayla girer, join'le çıkar

```bash
./tulpar tests/thread_copy.test.tpr        # 7 test
```

Kapsanan: argüman derin kopyası · join handle'ı tüketir · **ikinci join
hatadır** (eskiden çift-free ile çekirdek dökümü) · detach edilmiş handle
join edilemez.

### 9. `thread_create` tam tipli fonksiyonla çöp veriyordu (T9)

Eskiden ham sembole düşüp işçiye işaretçiyi tamsayı diye veriyordu —
ölçüldü: `thread_create(isci, 42)` için işçi **140166943450080** gördü.

```bash
printf 'func isci(int n): int { return n + 1; }\nint t = thread_create(isci, 41);\nprint(thread_join(t));\n' > /tmp/t9.tpr
./tulpar /tmp/t9.tpr           # beklenen: 42
```

### 10. `at` / `json_get` erişimcileri

```bash
./tulpar tests/accessors.test.tpr          # 8 test
```

⚠ Sınır politikası: **negatif indeks "sondan sayma" DEĞİL, sınır dışı.**

### 11. Çalışma zamanı hataları sessizdi (strict flip)

```bash
printf 'int[] a = [1,2,3];\nprint(a[99]);\n' > /tmp/strict.tpr
./tulpar /tmp/strict.tpr; echo "exit=$?"
```

Beklenen: tanı **stderr**'e, `exit≠0`. Eskiden stdout'a yazıp `0` ile
çıkıyordu. Kaçış kapısı: `TULPAR_SOFT_RUNTIME=1`.

### 12. `[typecheck]` ile S1 truthiness tablosu çelişiyordu

```bash
printf 'float f = 1.5;\nif (f) { print(1); }\n' > /tmp/s1a.tpr
printf 'str s = "";\nif (s) { print(1); }\n' > /tmp/s1b.tpr
LC_ALL=C ./tulpar typecheck /tmp/s1a.tpr    # beklenen: UYARI YOK (eskiden vardı — yanlış pozitif)
LC_ALL=C ./tulpar typecheck /tmp/s1b.tpr    # beklenen: "ALWAYS true ... length(x) > 0"
```

Eski mesaj *"Condition must be boolean or integer"* **yanlıştı**: `if("")`
izinli ve tanımlı.

### 13. Paylaşılan global lint'i (yeni özellik)

```bash
printf 'int fin = 0;\nfunc w(id) { fin = 1; return 0; }\nint t = thread_create(w, 0);\nwhile (fin == 0) { }\n' > /tmp/lint.tpr
LC_ALL=C ./tulpar typecheck /tmp/lint.tpr
```

Beklenen: `WRITTEN in thread worker 'w' but READ without synchronisation`.

Susması gerekenler (yanlış pozitif denetimi): mutex korumalı · thread yok ·
yalnız `thread_join` · yerel gölgeleme. Ayırt edici: bir global korumalı,
öbürü değilse **yalnız korumasız olan** uyarı almalı.

---

## B. Ölçüm iddiaları — yeniden ölçülebilir

```bash
DISPLAY= WAYLAND_DISPLAY= python3 benchmarks/fair/run.py     # ~15-20 dk (Python yavaş)
```

Üretilen tablo: `benchmarks/fair/RESULTS.md` (elle yazılmıyor).

| iddia | beklenen |
|---|---|
| `mandelbrot` (dizi yok) | Tulpar ≈ C, **1,00×** |
| `matmul` (float dizisi) | Tulpar ≈ **26,6×** geride |
| `nbody` | ≈ **11,6×** |
| dokuz dilin çıktısı | **aynı** — ayrışırsa satır geçersiz |

**Float unboxing kârlılık kapısı (iş yapılmadı, gerekçesi ölçüldü):**
`matmul`ü int dizisiyle koş — int yolu *zaten kutusuz*, yani ulaşılabilir
tavan odur. Ölçülen **5,95×**; kapı ≤4× olduğu için refactor **yapılmadı**
(S11).

**`fib` kazanımı algoritmik:** süreç açılışı *aynı ikiliden* çıkarılarak
büyüme tabanı ölçülür — clang 1,604 · gcc 1,607 · Tulpar zincirsiz 1,612 ·
**Tulpar 1,488**. Aynı sınıf (üstel), **küçük taban**. Oran n ile büyür
(12,3×@34 → 17,3×@40).

⚠ **Bu bir sabit değil.** Yayında "fib'de C'den 2,7× hızlı" diye duran cümle
ıraksayan bir eğri üstünde **bir noktadır**; site bu şekilde düzeltildi.

---

## C. Nöbetçiler — hepsi kırmızıya dönebilmeli

`./build.sh suites` içinde koşanlar:

| nöbetçi | neyi kilitler | nasıl kırmızı yapılır |
|---|---|---|
| `stream_contract_smoke.py` | S4 üç fazlı akış | `_wings_stream_started` kancalarını sök |
| `arena_contract_smoke.py` | `restore` bırakmaz / `drop` bırakır | kolları aynı yap |
| `stack_growth_smoke.py` | R11, 13 şekil + **kaynaktan türetilen** 50 builtin (etiket `50/50` = ölçülen/keşfedilen) | bir makroyu ham `LLVMBuildAlloca`ya çevir · ya da bir `OVERRIDE`ı derlenmez yap (artık **sessiz atlamıyor**) |
| `typecheck_corpus_scan.py` | korpus tanı tabanı (**metin** tutar), 233 dosya — `examples/**` özyinelemeli, `tests/typeinfer/` hariç | bir dosyaya tip hatası ekle (`examples/en/` dahil) |
| `truthiness.test.tpr` | S1 tablosu | — |
| `thread_copy.test.tpr` | S7/S8 thread + handle | — |
| `shared_json_read.test.tpr` | S6 paylaşılan okuma | — |
| `accessors.test.tpr` | S5 sınır politikası | — |
| `build.sh` #19 kapısı | tanı tek kapıdan çıkar (`printf` · `fprintf(stdout,…)` · `puts`; `stderr` muaf) | `runtime_bindings.cpp`'ye ham `printf("... Hatasi ...")` ekle |
| `build.sh` TypedValue kapısı | ilklendirme (virgüllü/dizili/işaretçili biçimler dahil) | `TypedValue x;` · `TypedValue a, b;` · `TypedValue d[2];` |

⚠ `typecheck_corpus_scan.py` **iş yapmadan önce kendini sınar**: tanı üretmesi
kesin bir fikstürde tanı göremezse "temiz korpus" demek yerine hata verir.
Bunun sebebi: tabanın ilk üç sürümü "195 dosyada 0 tanı" dedi ve üçü de
yanlıştı (stdout/stderr · `LC_ALL=C` dil çevirisi · büyük/küçük harf).

---

## D. AÇIK olanlar — bunlar hata değil, adlandırılmış iş kalemleri

1. **Eleman yazma yolu.** `a[i] = a[i] + b[i]` tek başına **2,84×**; üç erişim
   şeklinin en büyüğü ve `matmul` açığının kökü. (Lineer okuma 0,97×, hesaplı
   indeks 0,88× — yani indeks aritmetiği **bedava**.)
2. **Float-dizi unboxing, kendi kapısıyla.** Tarama ≤2×, eleman ≤8 bayt.
   Ulaşılabilir (int lineer 0,97×). `matmul` gerekçesiyle **değil** (S11).
3. **14 korpus tanısı** — `tests/typecheck_corpus_baseline.txt`'te kayıtlı,
   bilinen ve kabul edilmiş. Sayı değişirse kapı kırmızı verir.
4. `ws_masked_client_smoke.py` + `wings_tls_smoke.py` hâlâ otomasyon dışı.
5. **Sürüm etiketi kesilmedi.** `main`'de 5 kırıcı değişiklik birikti; SemVer
   politikası (`CHANGELOG.md`) **MAJOR** diyor.

---

## E. Bu oturumda benim yaptığım hatalar — denetlenmesi gerekenler

Bunları ayrıca yazıyorum çünkü **bulguların çoğu bu hatalardan çıktı** ve
bir sonraki denetim aynılarını arayabilir:

| # | hata | nasıl yakalandı | ders (kural) |
|---|---|---|---|
| 1 | `TypedValue sc;` ilklendirilmemiş | CI (LLVM 18) + ASAN | #32 |
| 2 | Korpus taramasının ilk **üç** sürümü "0 tanı" dedi — stderr okunmadı, `LC_ALL=C` çevirdi, `Type Error` büyük E | bilinen-pozitif fikstür | #26, #9b |
| 3 | Korpus kapısı **sayı** düzeyindeydi, içerik değil | sonradan metin diff'i alındı | #26 |
| 4 | `fib` sonucunu "farklı karmaşıklık sınıfı" diye **yayınladım** — yanlış, aynı sınıf küçük taban | kullanıcı düzeltti | #31 |
| 5 | S11 kaydı FINDINGS gövdesindeydi ama **S-listesinde yoktu** | kapanış denetimi | #31b |
| 6 | Yığın tarayıcısının ilk hâli gövdeleri kullanmıyordu → LLVM ölü kodu atıyordu, şekiller **hiçbir şey ölçmeden** "TEMİZ" diyordu | süre şüphesi (14 program 1,5 sn) | #23 |
| 7 | `TypedValue` nöbetçisinin ilk deseni satır başına bağlıydı, **enjeksiyonu kaçırdı** | enjeksiyon testi | #10 |
| 8 | `fib` üssünü ilk ölçümde **1,66** buldum — süreç açılışı çıkarılmamıştı | tutarlılık kontrolü | #5 |
| 9 | `join(sep, xs)` argüman sırasını ters yazdım, "sızıntı" diye raporladım | typecheck zaten uyarıyordu | #12 |
| 10 | Lint'in gezicisi `Assignment`ın **`name`** (dizgi) biçimini görmüyordu → `fin = 1;` hiç sayılmıyordu | kırmızı-olması-gereken şekil | #27 |
| 11 | Lint'te üst düzey bildirim global'i "yerel" işaretliyordu → tüm okumalar susuyordu | aynı | — |
| 12 | `$?` boru ardından `head`'in kodunu verdi (iki kez) | — | #9b |

---

## Denetleyene son not

Bu listede **"yapılmayan iş" de bir madde** (D.2, float unboxing). Kârlılık
kapısı iş başlamadan ölçüldü ve tavan kapının altında çıktı; ~117 değinmelik
bir refactor bu yüzden **yapılmadı**. O kararın kaydı olmasaydı bir yıl sonra
aynı soru sıfırdan sorulurdu.

Bir maddeyi doğrulayamazsan, **iddiayı değil düzeneği önce sorgula** — bu
oturumda ölçüm aleti en az beş kez yalan söyledi (E bölümü).

---

## F. Bağımsız denetim — 2026-09-11, ikinci göz

Yukarıdaki liste bağımsız olarak denetlendi: ön koşul yeniden koşuldu
(**80/80 suite** · örnekler yeşil · typeinfer **13+17**), **A'nın 13
maddesinin 13'ü** yeniden üretildi, B'nin üç oranı yeniden ölçüldü
(mandelbrot 1,00× · matmul 26,7× · nbody 11,6×; dokuz dilin çıktısı aynı),
ve **beş nöbetçi gerçekten kırmızıya döndürülüp geri alındı**. S4'ün
enjeksiyonu, dosyanın önceden yazdığı sonucu birebir verdi: tam 4 kontrol
düştü ve WS kuyruğunda `\x81\x03ilk` ardına ham `HTTP/1.1 500` geldi.

**İddialarda yanlış çıkan olmadı.** Altı uyuşmazlığın hepsi ölçüm
aletinin kendi kör noktasıydı — yani E bölümünün aradığı sınıf. Hepsi
kapatıldı:

| # | bulgu | kapanış | kırmızı kanıtı |
|---|---|---|---|
| F1 | Yığın tarayıcısı **"50 builtin" deyip 49 ölçüyordu**: `join`in argümanı tek tırnaklıydı, program derlenmiyordu, `rc is None` dalı sessizce atlıyordu | tırnak düzeltildi; atlama artık **kırmızı**, etiket `ölçülen/keşfedilen` yazıyor | bir `OVERRIDE` bozuldu → `builtin taramasi (49/50) repeat (DERLENMEDI — olculmedi)` |
| F2 | #19 kapısı `fprintf(stdout, …)` ve `puts(…)`'u **görmüyordu** (`\bprintf` bütün `fprintf`leri eliyor) | desen `f?printf\|f?puts`'a genişletildi, `stderr` muaf | üç sızıntı enjekte edildi, üçü de yakalandı; yetkili `stderr` muaf kaldı |
| F3 | TypedValue kapısı `TypedValue a, b;` ve `TypedValue d[2];` biçimlerini **kaçırıyordu** | desen ilklendiricisiz HER bildirimi yakalar hâle getirildi | üç biçim enjekte edildi, üçü de yakalandı; temiz ağaçta 0 yanlış pozitif |
| F4 | Korpus taraması `examples/en/`'i (30 dosya) **hiç görmüyordu** | `examples/**` özyinelemeli, `tests/typeinfer/` hariç — 198 → **233 dosya**, taban yine 14 | `examples/en/breakout.tpr`'ye tanı eklendi → kapı kırmızı |
| F5 | `async.test.tpr`'nin `assert(dt < 50)` **duvar saati eşiği** macOS/arm64 CI'ı dört koşumda kırmıştı (gather bozulmadan) | iki kollu **fark** ölçümüne çevrildi (`esz * 2 < seri`) — orantılı yük altında düşmez | gather kolu seri yapıldı → kırmızı; marj: seri 60 ms / eşzamanlı 20 ms |
| F6 | D.1/D.2'nin sayıları **elle yazılmış tabloydu**, koşucusu yoktu — oysa S11 kararı ona dayanıyor | `benchmarks/fair/shapes.py` eklendi: dört şekil, C tabanı, çıktı mutabakatı, sıralama iddiası | çıktıyı ayırma → `AYRISIYOR`; sıralamayı bozma → `IDDIA CURUDU` |

**F5, defterde ZATEN yazılı bir dersti** ([[Tuzaklar]] 6z, aynı sınıf, aynı
platform) — bir korumada düzeltilmiş, `async.test.tpr`'ye uygulanmamıştı.
Ders: *bir tuzak yazıldığında aynı sınıftan bütün yerler taranır.* Tarandı;
`.tpr` süitlerinde başka mutlak zaman eşiği kalmadı.

**Denetim sırasında düzeneğin kendisi iki kez yalan söyledi** (F6'yı ölçerken):
ölçülen döngüyü bir tekrar sarmalayıcısına koymak Tulpar'ı **20 kat** yavaş
gösterdi, ve döngü sınırını `n` yazmak (`len(a)` yerine) **3,5 kat**
gösterdi — ikisi de `shapes.py`'nin başlığında yazılı, ikisi de
[[Tuzaklar]]'a eklendi (1k, 1l, 6ş).

**Geri çekilen hipotez:** referans commit `27302c3`'ün CI'sinde bütün test
adımları `skipped` görünüyor. Bu bir boşluk DEĞİL: `tree(897c22c) ==
tree(27302c3)`, yani birebir aynı ağaç PR koşumunda iki platformda da tam
sınanmış. Artefakt yeniden kullanımı meşru. (Yine de: `main`'deki yeşil bir
push koşumu "burada yeniden sınandı" demek değildir — ağaç hash'ine bak.)
