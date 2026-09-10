# Bulgular — tek kaynak

Bir dış denetimle yürütülen ölçüm turlarının kaydı. **İddia burada yaşar;**
PR gövdeleri, `docs/mindmap/*` ve tulparlang.dev buraya referans verir, iddiayı
kopyalamaz. İki turdur yaşanan "bir yerde düzeltildi, ötekinde bayat kaldı"
sorununun çözümü bu dosya.

Durum: **açık** · **doğrulandı** · **çürütüldü** · **geri çekildi** · **düzeltildi**

## #0 — Bir kimlik bir gerçek taşır; ikinci gerçek geldiğinde isim bölünür

Yedi turun tek yasası. Aşağıdaki bulguların çoğu bunun ayrı ayrı örnekleri:

| Olay | Paylaşılan kimlik | Sonuç |
|---|---|---|
| `TYPE_VOID` × 4 anlam | tek sentinel | denetimsiz atama, yanlış yayınlanmış bulgu |
| `arena_restore` ≈ `arena_drop` | "benziyor" | zayıfı sessiz kullanıldı, sahte sızıntı raporu |
| `push` × 2 katalog satırı | tek isim, iki kayıt | hangisi kazanır belirsiz |
| "ARC", "statik tipli", "C kadar hızlı" | tek cümle, birden çok gerçek | üçü de daraltıldı |

**Ön koşul (kural 7'nin tamamlayıcısı): sentinel üzerine değişmez kurulamaz;
önce anlamlar ayrılır.** Değişmezi kurmadan önce sentinel'in kaç anlam
taşıdığını say — P21 bunu üçten dörde çıkardı.

## Performans iddiaları

| # | İddia | Durum | Kanıt |
|---|---|---|---|
| C1 | Node/Python/Java/C#'tan çok daha hızlı | **doğrulandı** | [fair/README](benchmarks/fair/README.md) |
| C2 | `sieve`/`intloop`'ta C ile başa baş | **doğrulandı** (MAD içinde) | fair/README bayrak matrisi |
| C3 | `fib`'de C'den hızlı | **doğrulandı** — gcc-best 1,66 vs 0,47 ms (3,54×); clang/rustc'ye 8,3× | [recursion/](benchmarks/fair/recursion/README.md) |
| C4 | `strcat`'te C'den hızlı | **çürütüldü** — C'ye aynı `itoa` verilince 11,07 vs 13,85 ms | recursion/README |
| C5 | `arrayiter`'de C'den hızlı | **çürütüldü** — C `-march=native` ile 1,85 vs 2,03 ms | fair/README |
| C6 | Özyinelemede genel olarak C'den hızlı | **çürütüldü** — gcc `ackermann` 3,2×, `treesum` 3,6×, `tak` 1,5× önde | recursion/README |
| C7 | Klon zinciri genel bir kazanç | **çürütüldü, sonra düzeltildi** — K=4'te `ackermann` %21 gerilerdi; K=1 ile en kötü durum nötr | recursion/README |
| C8 | `fib` kazanımı frontend'in kendi katkısı | **doğrulandı** — zincirsiz Tulpar clang/rustc ile ±%11 bandında; zincirli 8,3× dışında | recursion/README |

**Gürültü tabanı:** aynı C kaynağının gcc↔clang yayılımı bu ailede 1,21×–3,82×.
Bundan küçük diller arası farklar derleyici farkıdır.

## Doğruluk / runtime

| # | İddia | Durum | Kanıt |
|---|---|---|---|
| M1 | Bellek yönetimi ARC (referans sayımı) | **çürütüldü** — AOT yolu `arc_release`i hiç çağırmıyor; model arena + ömür boyu malloc | [Concurrency](docs/mindmap/Concurrency.md) |
| M2 | Döngüde üretilen heap değerleri geri alınıyor | **koşullu** — `arena_drop` ile DÜZ (2 976→2 972 KB); onsuz 5–6× tırmanıyor | [Memory](docs/mindmap/Memory.md) |
| M3 | `arena_save`/`restore` bu belleği kurtarıyor | **çürütüldü** — restore serbest BIRAKMAZ; bırakan çağrı `arena_drop` | Memory |
| M4 | Sunucu istek yolu düz kalıyor | **doğrulandı** — json handler 4,16M istekte 3 444 KB sabit; wings istek başına `arena_drop` çağırıyor | Memory |
| T1 | Paylaşılan global'ler atomik | **çürütüldü** — korumasız 8 thread × bir artırma → 7; **`mutex_*` ile tam 400 000** | Concurrency |
| T2 | Thread yazmaları görünür | **çürütüldü** — spin-wait sonsuza döner; `sleep()` varken kazara çalışır | Concurrency |
| T3 | Paylaşılan dizinin eşzamanlı okunması bozuluyor | **GERİ ÇEKİLDİ** — test `push` dönüşünü atıyordu, dizi tek thread'de bile boştu | [Tuzaklar 7a](docs/mindmap/Tuzaklar.md) |
| T4 | `arr_debox` okuma yolundan yazıyor | **açık (incelemeyle)** — sertleştirildi, ama tetikleyen test yok | Concurrency |
| T7 | json okuma yolu da yazma içeriyor (P15) | **çürütüldü (incelemeyle)** — `vm_get_element`→`vm_object_get` saf doğrusal tarama; `ObjObject`'te tembel/önbellek alanı yok. Dizilerdeki `arr_items`→`arr_debox` yazmasının karşılığı json'da **yok** | P15 |
| R10 | Tanı sızıntısı kalmadı | **çürütüldü, DÜZELTİLDİ** — `vm_get_element`/`vm_set_element`'in "gecersiz hedef" tanıları stdout'a yazıyordu; ilk #19 koruması **satır-bazlı olduğu için göremedi** | P15 turu |
| Y1 | Dil statik tipli | **daraltılmalı** — `var` çapraz-tip yeniden atamaya izin veriyor | `tests/typeinfer/` |
| Y2 | `void` dönüşün atanması denetleniyor | **çürütüldü, DÜZELTİLDİ** — `e = push(e,3)` geçiyordu; artık hata | `tests/typeinfer/fail/11_void_assignment.tpr` |
| Y3 | Eleman ataması tip denetleniyor | **çürütüldü, DÜZELTİLDİ (branch)** — `str[] s; s[0]=5;` geçiyor VE çalışıyordu | `fail/13_element_assign_type.tpr` |
| Y4 | `var` çıkarıldığı tipte kalıyor | **çürütüldü, DÜZELTİLDİ (branch)** — `var b=[1,2]; b=5;` geçiyordu | `fail/12_var_cross_type.tpr` |
| Y5 | `call("ad")` adın varlığını doğruluyor | **çürütüldü** — literal adda bile doğrulamıyor; hata çalışma zamanına kalıyor | P25 |
| R1 | Çalışma zamanı hatası süreci başarısız kılıyor | **çürütüldü, DÜZELTİLDİ (FLIP 2026-09-10)** — strict artık **varsayılan**: tanı stderr'e, yakalanmazsa exit≠0; `TULPAR_SOFT_RUNTIME=1` bir sürüm döngüsü kaçış kapısı | P25 |
| R5 | "Sınır dışı → 0, devam" bir kaza | **çürütüldü; SÖZLEŞME DEĞİŞTİ** — `loop_versioning` 8 testi yeni sözleşmeye göre yazıldı (fırlatma = bekçinin kanıtı) | R1 seti · P36/P37 |
| R9 | Tüm çalışma zamanı hataları aynı sözleşmede | **çürütüldü, DÜZELTİLDİ** — `aot_div_error`'ün taşma dalı flip'te gözden kaçmıştı (farklı `printf` biçimi); sıfıra bölme fırlatırken taşma stdout'a yazıp 0 ile çıkıyordu | flip |
| R8 | strict'te longjmp runtime çerçevelerini bozar | **çürütüldü** — 76 suite strict altında koşuldu, **0 çökme** | P26b |
| R6 | Sondalar tanıyı doğru yerde arıyor | **çürütüldü, DÜZELTİLDİ** — `silent_failure_probe.py` tanıyı stdout'ta bekliyordu; stderr süzgeci eklendi | R1 seti |
| R7 | Site'nin "invalid body → 422" vaadi tutuyor | **doğrulandı** — 422 + alan detayı (`name: required`, `expected str, got int`) | P28 |
| R2 | `build.sh test` çalışma zamanı hatasını yakalıyor | **çürütüldü, DÜZELTİLDİ** — harness artık `TULPAR_STRICT_RUNTIME=1` ile koşuyor (dil varsayılanı değişmeden); enjeksiyon kırmızı | P25 |
| T5 | `thread_join` işçinin sonucunu taşıyor | **çürütüldü** — 0 dönüyor; katalogda `void` olarak kaydedildi, artık atanması hata | P20 |
| T8 | `thread_create` argümanı paylaşılıyor | **DEĞİŞTİ (sözleşme)** — artık **derin kopya**; işçi kendi kopyasıyla çalışır (P47 fikstürle kilitli) | thread_copy |
| T9 | `thread_create` tipli fonksiyonla çalışıyor | **çürütüldü, DÜZELTİLDİ** — `t_<ad>` şimi yalnız tipsiz fonksiyonlar için vardı; tipli olanda ham sembole düşüp **işaretçiyi tamsayı diye geçiriyordu** (42 yerine 140166943450080). Artık çağrı anında **normalleştirici sarmalayıcı** üretiliyor: entry'ye ulaşan **tek ABI** var | thread_copy |
| T11 | join kaydı serbest bırakılabilir | **çürütüldü** — ilk yazımda `join` kaydı `free` ediyordu ve ikinci join **çekirdek dökümü** verdi (`double free`). Kayıt artık hiçbir yolda serbest bırakılmıyor; `consumed` bayrağı güvenli hata veriyor | thread_copy |
| T12 | detach edilen thread join edilebilir | **çürütüldü** — bayrak yokken **kazara** çalışıyordu (pthread düzeyinde tanımsız). Artık açık hata | thread_copy |
| T10 | join-dönüşü ücretsiz | **çürütüldü (ölçülmüş), KISMEN düzeltildi** — detach dalı artık sonucu hiç üretmiyor; join dalı genel kalıcı-değer sızıntısına tabi (M2) | P43/P49 |
| T6 | Dilde senkronizasyon ilkeli yok | **çürütüldü** — `mutex_*` var ve çalışıyor; yalnız katalogda yokmuş | P27 |
| C9 | Suite'ler çalışma zamanı hatasını yakalıyor | **doğrulandı (enjeksiyonla)** — tek dosya çıkış 1, suite çıkış 1, paket sayısı 75'te kalıyor | P26 |
| R3 | `try/catch` çalışma zamanı hatasını yakalıyor | **çürütüldü, DÜZELTİLDİ (strict'te)** — strict modda hata `aot_throw` ile fırlıyor: `catch` yakalıyor, yakalanmazsa stderr + exit 1 | P26b |
| R4 | LSP tablosu ile tip katalogu tutarlı | **çürütüldü, DÜZELTİLDİ** — 20 native builtin katalogda yoktu (tip+arite denetimsiz); `sb_append` imzası LSP'de yanlıştı | P27 |

## 🔴 L1 — `&&` ve `||` KISA DEVRE YAPMIYOR

En ağır bulgu. Her iki operatör de **iki operandı da değerlendiriyor**:

```tulpar
int sayac = 0;
func yan(): int { sayac = sayac + 1; return 1; }
bool r1 = (1 == 2) && (yan() == 1);   // sayac=1  — sol FALSE, sag YINE calisti
bool r2 = (1 == 1) || (yan() == 1);   // sayac=1  — sol TRUE,  sag YINE calisti

int[] bos = [];  int n = 0;
if (n > 0 && bos[n - 1] == 5) { }     // "Dizi indeksi sinir disinda"
```

Yani **evrensel koruma deyimi kırık**: `i < len(a) && a[i] == x`, `p != 0 && ...`
gibi her yazım sağ tarafı yine çalıştırıyor.

**Kök neden** (`src/aot/llvm_backend.cpp`): hem tipli yol (`TOKEN_AND`/`TOKEN_OR`
case'leri) hem kutulu yol (`emit_boxed_binary_op`) **zaten değerlendirilmiş**
`L` ve `R` alıp düz `LLVMBuildAnd`/`Or` yapıyor. Kısa devre o noktada zaten
imkânsız; dal + phi, sağ operand üretilmeden ÖNCE kurulmalı.

**Nasıl bulundu:** R1'in `TULPAR_STRICT_RUNTIME=1` anahtarı dedektör olarak
koşuldu (P31). `scene3d_engine` 654/654 geçerken 9 tanı yutuyordu; izole edilip
daraltıldı → `_t_sort_str3`'ün insertion sort'u → `while (j > 0 && o[j-1] > v)`
→ `j=0` iken `o[-1]` değerlendiriliyor. Sıralama kodu **doğruydu**; dil yanlıştı.

**Etki:** doğruluk (koruma deyimleri), performans (gereksiz değerlendirme) ve
yan etki (sağ taraf koşulsuz çalışıyor — `no_double_eval` ailesinin kardeşi).
Semantiği hiçbir yerde belgelenmemiş, yani bilinçli bir karar değil.

**Durum: DÜZELTİLDİ (2026-09-10).** `emit_logical_shortcircuit_i64` — ternary'nin
deseni (phi yerine giriş bloğunda alloca yuvası, böylece iki yol aynı yardımcıyı
paylaşabiliyor). Sol taraf tam bir kez, kısaltma kimliği operatöre göre
(`&&`→false, `||`→true), sonuç `INFERRED_BOOL` (int deseydi `toString(t && f)`
"1" basardı). Fikstür: `tests/short_circuit.test.tpr` (9 test, phi kimliği ve
sol-tek-kez dahil).

**P32 doğrulaması:** strict dedektör koşumunda `scene3d_engine` **9 → 0**;
`loop_versioning` 30 → 30 (değişmedi — onlar gerçekten kasıtlı sınır testleri).

**Yan bulgu — ternary zaten lazy** (P35): `1==1 ? 5 : yan()` sağ tarafı
çalıştırmıyor. Yani kusur koşullu değerlendirme ailesinin tamamında değil,
yalnız `&&`/`||`'de idi.

## Belgelenmemiş sözleşmeler

| # | Sözleşme | Durum |
|---|---|---|
| S1 | **Truthiness tablosu** — yalnız SAYISAL SIFIR yanlış | **ölçüldü, belgesiz** |
| S2 | `mutex_*` ile paylaşım | **ölçüldü, belgesiz** ([[Concurrency]]) |
| S3 | `arena_drop` gerekliliği (`arena_restore` serbest bırakmaz) | **ölçüldü, belgesiz** ([[Memory]]) |
| S4 | SSE/WS akışında handler ortası throw | **kod kapandı, cümle kapanmadı** — üç fazlı sözleşme (P44/P46) yazıldı ve düzeltildi, ama **fikstürü yok** (#21 borcu, aşağıdaki retrofit sayımı) |
| S5 | `at` / `json_get` sınır politikası | **belgelendi** — negatif indeks "sondan" değil, sınır dışı |
| S7 | Thread sözleşmesi: **kopyayla girer, join'le çıkar** | **yazıldı** — argüman derin kopya, join sonucu taşır; paylaşmak isteyen `mutex_*` kullanır |
| S9 | Uzun ömürlü süreçte **değer-başı geri kazanım yok** (join-dönüş dahil) | **ölçüldü** — join-dönüş değerleri sürecin ömrü boyunca yaşar; binlerce join içeren süreçte RSS ~N×değer-boyu artar (P43). M2'nin çözülmesi bu sınıfın **tamamını** kapatır; yamayı her ekleme noktasına serpmek değil, kök düzeltme tek yerde |
| S8 | Handle sözleşmesi | **yazıldı + fikstür** — *join handle'ı tüketir; ikinci join hatadır; detach edilmiş handle join edilemez* |

**Audit (derin kopya göçü, 7. madde):** korpusta **13** `thread_create` sitesi.
5'i aggregate *görünüyor*; `lib/wings.tpr`'deki 3'ü aslında **json'a sarılmış
int** — kaynaktaki yorum sebebini söylüyor: *"Box the fd into a json so
`thread_create` passes it through the canonical VMValue ABI rather than a
native i64 register."* Yani T9'un düzelttiği ABI boşluğunun geçici çözümü.
`aot_persist` heap olmayan değerde **no-op**, dolayısıyla derin kopya onlara
sıfır maliyet (ölçüldü). Kalan 2 aggregate fikstürün kendi testleri.
**Üretim kodunda 0 gerçek aggregate sitesi → göç bedelsiz.**

**P50 — fosil göçü (yapıldı).** O boxing hilesi T9'dan sonra **gereksiz**:
`tw_<ad>` sarmalayıcısı tipli/native işçiyi normalleştiriyor, yani argümanı
elle `json`'a sarmanın tek gerekçesi ortadan kalktı. Dört site de düz `int`e
geçti — `_wings_serve_connection`, `_wings_pool_worker` (`lib/wings.tpr`) ve
`_wings_tls_serve` (`lib/wings_tls.tpr`) artık `int` parametre alıyor, üç
çağrı yerindeki `json c = client;` ara değişkeni silindi.

*Müze notu:* boxing yorumu (*"Box the fd into a json so `thread_create` passes
it through the canonical VMValue ABI rather than a native i64 register"*)
teşhis olarak **doğruydu** — `a56f273` (T9) öncesi ham sembole düşen çağrı
işçiye işaretçiyi tamsayı diye veriyordu, ve `thread_create` yalnız `t_<ad>`
shim'i olan (yani tipsiz/kutulu) işçiyi taşıyabiliyordu. Yorum bir hatayı
değil, **eksik bir ABI'yi** belgeliyordu; ABI tamamlanınca fosilleşti. Bu
yüzden koddan çıkarılıp buraya taşındı: silinen çözüm değil, **artık
geçersizleşmiş bir kısıt** kaydı.

**Doğrulama — derlenmiş değil, koşulmuş.** Örnek suite'i wings'i yalnız
*derliyor* (`COMPILE_ONLY_TESTS`), yani göç orada sınanmış olmazdı. İki ayrı
ikili (`listen_pool(port, 4)` ve `listen_async(port)`) ayağa kaldırılıp her
birine **20 eşzamanlı** `GET /ping` sürüldü: her ikisinde de **200=20/20**,
**tek** gövde (thread'ler arası karışma yok) ve bağlantı başına **tam bir**
HTTP zarfı (P48 sınıfı çift zarf yok). Sonda kırmızıya dönebiliyor: var olmayan
rotada 0/6, dinlemeyen portta bağlantı reddi (#10).

⚠ Göç güvenliğinin ön koşulu ölçüldü, varsayılmadı: `native_abi_eligible()`
native (i64) yola çıkmak için `: int` **dönüş tipi** şart koşuyor; bu dört
işçinin hiçbirinde yok, dolayısıyla hepsi kutulu `t_<ad>` ABI'sinde kalıyor ve
`thread_create` sarmalayıcıya hiç düşmüyor. Düşseydi sorun çıkardı: `tw_`
sarmalayıcısı dönüşü koşulsuz `llvm_vm_val_int_val` ile kutuluyor, yani
**void dönen** bir native işçiyi saramazdı. Bugün o dal erişilemez — parametre
tipi değiştirmenin ABI'yi de değiştirebileceği not edilsin.

**P43/P49 — önce/sonra (RSS zirve, spawn döngüsü):**

| | N=1000 | N=4000 | oran | |
|---|---:|---:|---:|---|
| join, **önce** | 7660 | 21840 | 2,85× | |
| join, **sonra** | 6932 | 21528 | 3,11× | değişmedi — beklendiği gibi |
| detach, **önce** | 7960 | 24380 | 3,06× | |
| detach, **sonra** | 7564 | **20868** | 2,76× | **−%14**, sonuç hiç üretilmiyor |

**İki dal ayrı:** *detach atar* fiziksel olarak gerçekleşti — sonuç kopyası
üretilmiyor. *join dalı düzeltilemez*: dönen değer artık **çağıranın** ve genel
kalıcı-değer ömrüne tabi (M2 — `arc_release` AOT yolunda hiç çağrılmıyor,
değer-başı geri kazanım yok). Kalan büyüme her iki dalda da argüman kopyası +
kayıt; o da aynı genel işle kapanır, join/detach'e özgü değil.

⚠ **#21 istisnası, gerekçesiyle:** S7'nin üçüncü cümlesi (*detach atar*) bir
bellek özelliği — Tulpar'dan gözlemlenemiyor, fikstürle değil **ölçümle**
doğrulanıyor (yukarıdaki tablo). Diğer iki cümlenin fikstürü var.
| S6 | Paylaşılan-değer katmanı mutasyona uğratılmaz | **sabitlendi** — `vm_object_get` üstünde sözleşme yorumu + `tests/shared_json_read.test.tpr`; uğratılacaksa T7 ve derin kopya yeniden değerlendirilir |

### Retrofit sayımı — hangi sözleşmenin fikstürü var (#21 denetimi)

| Sözleşme | Fikstür | Otomasyonda |
|---|---|---|
| S1 truthiness | ❌ **yok** — korpusta tek eşleşme bir *yorum* satırı | — |
| S2 `mutex_*` | ✅ `thread_copy.test.tpr` (P30 sayaç) + `shared_json_read.test.tpr` | ✅ suites |
| S3 `arena_drop` | ❌ **yok** — `arena_restore` üç testte *geçiyor* ama hepsi "kalıcı değer hayatta kalır" yönünü sınıyor; *"restore serbest bırakmaz"* cümlesini hiçbiri sınamıyor. `arena_drop` hiçbir fikstürde geçmiyor | — |
| S4 üç fazlı akış | ❌ **yok** — `9f81587` ve `eda9ae1` ikisi de tek satır test eklemedi (stat ile doğrulandı); ölçüm tek kullanımlık düzeneklerdeydi | — |
| S5 `at`/`json_get` | ✅ `accessors.test.tpr` (8 test) | ✅ suites |
| S6 paylaşılan okuma | ✅ `shared_json_read.test.tpr` | ✅ suites |
| S7 thread sözleşmesi | ✅ `thread_copy.test.tpr` — 3 cümlenin 2'si; 3.'sü (*detach atar*) ölçüm istisnası | ✅ suites |
| S8 handle sözleşmesi | ✅ `thread_copy.test.tpr` (çift-join, detach sonrası join) | ✅ suites |
| S9 geri kazanım yok | ⚖ **ölçüm** (P43 RSS tablosu) — gözlemlenemeyen bellek özelliği, #21 istisnası | — |

**Sayım: 9 sözleşmenin 5'i fikstürlü, 1'i meşru ölçüm istisnası, 3'ü borçlu**
(S1, S3, S4). En ağır borç **S4**: defterde "KAPANDI" yazıyordu, oysa #21'e
göre kapanmamıştı — *kod* düzeldi, *cümle* sınanmadı. Kuralın kendisi bunu
yakaladı; satır yukarıda düzeltildi.

⚠ İkinci bulgu: `tests/ws_masked_client_smoke.py` ve `tests/wings_tls_smoke.py`
**hiçbir otomasyonda koşmuyor** — `build.sh` yalnız `builtin_audit`,
`wedge_mesh_check`, `dist_archive_audit`, `ast_child_fields_audit`,
`silent_failure_probe` ve `lsp_audit`'i çağırıyor. Yani S4'e en yakın duran iki
harness bile kırmızıya dönemez (kural #10: hiç kırmızı görülmemiş düzenek
yeşil değil, **bilinmiyor**).

**S1'in ampirik yarısı (P38a):** korpusta `if(<ad>)` deseninde **165 site**,
bunların **39'u** bool bildirimi olmayan değerler — yani truthiness'e yaslanıyor.
⚠ **Erişimci tasarımına doğrudan girdi:** o 39'un bir kısmı (`if (_schema)`,
`if (q)`, `if (hdrs)` — lib/wings) eksik `json[k]`'nin sessizce `0` (falsy)
dönmesine yaslanıyor.

**P41 audit'i (2026-09-10) yükü ölçtü — 38'e 1:**

| grup | tanım | sayı | göç |
|---|---|---:|---|
| A | tek atamalı yerel; throw **atama satırında** patlar, `if (v)` tanımlı kalır | **38** | gerekmiyor |
| B | global ya da yeniden atanan | **1** | gerçek iş (`lib/scene3d.tpr:11783`) |

Yani `json[k]` strict'te fırlatır yapılırsa pratik göç yükü **tek site**.

**S1 — truthiness (P38, 2026-09-10):**

| değer | `if()` sonucu |
|---|---|
| `int 0` · `float 0.0` | **false** |
| `int 7` · `"a"` · `[1]` | true |
| `""` boş dizgi | **true** |
| `[]` boş dizi | **true** |
| `{}` boş json | **true** |

Yani **yalnız sayısal sıfır yanlıştır**; boşluk/uzunluk doğruluğu etkilemez.
Bu savunulabilir bir tasarım (Python/JS'ten farklı) ama hiçbir yerde yazılı
değil — L1'in doğum hikâyesinin birebir aynısı: *belgelenmemiş operatör
semantiği*. Boşluk sınamak isteyen `len(x) > 0` yazmalı.

## 🟡 P39 — strict flip'in ÜÇÜNCÜ kapısı: sunucu dayanıklılığı

Yumuşak çağ, sunucu dayanıklılığını **sessiz ikameyle satın almıştı**:
handler'da sınır dışı → 0 ikamesi → handler devam → sunucu yaşıyor. Strict'te
aynı patika throw üretiyor ve wings dispatch'inde sınır yoksa süreç ölüyor.

**Ölçüldü (flip öncesi):** `/oku?i=999` → süreç **öldü** (exit 1), sonraki
istekler bağlantı hatası. **Tek bozuk istek sunucuyu indiriyordu.**

**Yapıldı:** `_wings_dispatch_cached` istek-başına try/catch sınırı oldu
(gövde `_wings_dispatch_inner`'a taşındı). Tek nokta yetti — wings'in kendi
notuna göre orası *"the single dispatch entry point"*; `listen`, `listen_pool`,
`listen_async`, `listen_evented` dördü de oradan geçiyor (doğrulandı). Sarmalın
`.tpr` katmanında olması şart: longjmp aynı yığın üzerinde çalışıyor.

**Durum: KAPANDI (2026-09-10).**

| | önce | sonra |
|---|---|---|
| süreç bozuk istekten sonra | **öldü** | **CANLI** ✅ |
| durum kaydı / log | 200 (yanlış) | **500** ✅ |
| istemcinin aldığı yanıt | — | **500 + JSON gövde** ✅ |

**Kök neden (P40 yanlışlandı):** ne "socket_send hiç çağrılmıyor" (H-A) ne de
"Content-Length tutmuyor" (H-C) idi. `curl -v` üçüncü bir karakter gösterdi —
bağlantı açık kalıyor, **0 bayt**, zaman aşımı. Sebep bir **tip uyumsuzluğu**:
`_wings_dispatch_cached` bir dict değil, `_wings_build_response`'un ürettiği
**tel dizgisi** döndürüyor. `catch`'ten ham dict dönünce çağıran boş dizgi
görüyor — ve boş dizgi `{"_stream":1}` nöbetçisiyle **aynı şey** demek
("handler zaten yazdı, socket_send yapma"). Yani bağlantı ne yazılıyor ne
kapanıyor. **Boş dizginin iki iş birden taşıması: #0'ın ta kendisi.**

**Çözüm:** hatayı normal handler'ın üreteceğiyle aynı şekle sentezleyip mevcut
zarf yolundan geçirmek — `_wings_build_response(server_error(...), keep)`.
Downstream'de sıfır özel durum; yanıt da tek kapıdan çıkıyor.

**Doğrulandı:** iyi 200 · kötü **500** + `{"error":"handler error: ..."}` ·
süreç CANLI · sonraki 200 · /healthz 200.

**P48 — sözleşme ihlalinin bedeli ölçüldü (2026-09-10).** Sokete **ham yazan**
ama `{"_stream":1}` **döndürmeyen** bir handler throw ederse, keep-alive
bağlantısında **iki HTTP yanıtı** üst üste gider:

```
toplam 411 bayt · HTTP yanıtı sayısı: 2
HTTP/1.1 200 OK ... ham   HTTP/1.1 500 Internal Server Error ...
```

İstemcinin **sonraki** isteği bu 500'ü kendi yanıtı sanar — yanıt
desenkronizasyonu. **Fix yok, sözleşme var:** sokete doğrudan yazan handler
bağlantıyı devraldığını `{"_stream":1}` ile bildirmek zorundadır; bildirmezse
throw'da ikinci zarf üretilir. Akış başlatıcıları (`wings_sse_headers`,
`wings_ws_upgrade`) bunu zaten yapıyor; ham yazan kod etmiyorsa sözleşme dışı.

**S4 kapandı (P44, 2026-09-10) — ve tanımsızlık zararsız değilmiş.** Ölçüm:

| faz | flip sonrası, fix ÖNCESİ | fix SONRASI |
|---|---|---|
| Stream başlamadan | 500 + JSON ✓ | 500 + JSON ✓ |
| **Stream ortası** | **200, 371 bayt — gövdeye gömülü `HTTP/1.1 500 ...`** ❌ | 200, 27 bayt, temiz SSE ✓ |
| süreç | CANLI | CANLI ✓ |

Sentez-500 "ulaşmıyor" değildi — **akışın içine yazılıyordu**: istemci 200 SSE
akışı alıp gövdesinde gömülü bir HTTP yanıtı görüyordu. Protokol bozulması.

**Üç fazlı sözleşme:** akış başlamadan → 500 + log; akış ortasında → **yalnız
log + bağlantı kapanır, zarf YAZILMAZ**; her iki fazda → süreç yaşar.
Kanca: akışın başladığı **iki nokta** — `wings_sse_headers()` ve
`wings_ws_upgrade()`; dispatch her istekte sıfırlıyor.

**P46 — WS tarafı ayrıca ölçüldü (2026-09-10), ve SSE'den beterdi.** İlk fix
yalnız SSE kancasını kurmuştu; `wings_ws_upgrade` bayrağı kaldırmıyordu:

| | fix öncesi | fix sonrası |
|---|---|---|
| WS akış ortası | 101 + `\x81"ilk"` + **ham `HTTP/1.1 500 ...` çerçeve akışına enjekte** | 101 + `\x81"ilk"`, **5 bayt, temiz** |
| WS 400 dalı (anahtar yok) | 400 | 400, 0 bayt — akış hiç başlamadığı için normal yol korundu |

Hiçbir WS istemcisi enjekte edilmiş HTTP'yi çerçeve olarak çözemez. Bayrak
**yalnız başarı yolunda** kalkıyor; 400 dalında akış başlamadığı için sentez
geçerli kalıyor.

**Kalan kenar (gözlemlenemedi):** 400 dalında handler tam bir HTTP yanıtı
göndermiş oluyor ve catch ikinci bir zarf yazabilir; keep-alive'da teorik
kalıntı. Bu testte 0 bayt artık ölçüldü, bağlantı kapanıyor.

**Yan bulgu:** `_wings_last_status` sözleşme gereği "her dönüşte" yazılmalıymış
(tanımındaki not); catch yolu onu atlayınca istek 500 dönerken 200 loglanıyordu.
Eklendi. Aynı sınıf: **bir fonksiyonun belgelenmiş yan etkisi, yeni bir çıkış
yolu eklendiğinde sessizce atlanır.**

## Yöntem kuralları (turlardan çıkan)

1. **Tek thread'li kontrol** — eşzamanlılık hatası bildirmeden önce aynı
   düzeneği tek thread'de koştur (T3 bu adım atlandığı için yanlış yayınlandı).
2. **Düzeneğin ön koşullarını bastır** — `len`, checksum, kurulum değerleri.
3. **Etkiyi sına, artefaktı değil** — sembol aramak yerine değişmez ara.
4. **Asla zarar verme** — bir optimizasyonun bazı programları yavaşlatması hata;
   bazılarına daha az kazandırması değil.
5. **Ortak sabiti çıkar** — iki süreyi oranlıyorsan süreç açılışını ölç ve çıkar.
6. **Temiz koşu kanıt değil** — yarış olasılıksaldır; tehlikeyi mekanizmadan
   çıkar, çıktıdan değil.
22. **Tanı aracı da tanının parçası.** P48'de `curl` tek yanıt gördü, ham
   soket **iki** yanıt gördü — aynı bayt akışı, iki farklı sonuç. Araç
   protokolü *sizin adınıza yorumladığı* için kanıtı siliyordu:
   `Content-Length: 3`'te okumayı bıraktı, ihlal o sınırın hemen ardındaydı.
   Kural: bir sözleşme ihlali *aranırken* ölçüm ham katmanda olmalı;
   yorumlayan araç ancak ihlal **bulunduktan sonra**, "kullanıcı ne görür"
   sorusunu yanıtlamak için meşrudur. Ters sırada kullanılırsa araç, aradığınız
   şeyi tanım gereği gizler.
21. **Sözleşme cümlesi, onu test eden fikstür doğmadan kapanmaz.** S-listesi
   *belgelenmemiş* sözleşmeleri avladı; T11 tersini gösterdi: **belgelenen
   sözleşme de tehlikeli** — sınanmadan yazılanın ihlali sessizce kabul edilir.
   *"join handle'ı tüketir"* cümlesi yazıldığında kaydı `free` eden kod iki tur
   boyunca sessizce duruyordu; cümleyi doğrulayan test yazılınca çift-join
   **çekirdek dökümü** verdi. Cümle, yazarını kendi hatasına götürdü.
   Gözlemlenemeyen cümleler (bellek özellikleri) ölçümle doğrulanır ve bu
   **gerekçesiyle** kaydedilir.
20. **Yanlış gözlem gerçek sinyal taşıyabilir; sinyali çerçeveden ayır.**
   Bulgu 3'te üç şey vardı: hatalı test (`push` yanlış kullanımı) ❌, hatalı
   teori (`arr_debox` yarışı) ❌, ve **doğru tanı** ✅ — teslim katmanı
   (stdout + fırlatmama) onu çöpe çeviriyordu. Geri çekerken içeriği de atmak
   ikna ediciydi ama yarım güvenlikti; sinyal R10 ile geri geldi.
19. **Metin-deseniyle yapılan dönüşüm, envanteri yeniden üretmez.** Flip'in
   printf→throw dönüşümü regex'le yapıldı ve farklı biçimli bir `printf`i
   kaçırdı. Garanti mekanik olmalı: `build.sh` artık `runtime_bindings.cpp`'de
   yetkili çıkış dışında hata metni taşıyan `printf` ararsa **build'i kırar**.
   ⚠ Ve o denetimin ilk hâli **satır-bazlıydı** — yani korumak için yazıldığı
   hatanın aynısını yaptı: iki çok-satırlı sızıntıyı göremedi ve yanlış yeşil
   verdi. Şimdi parantez dengesiyle tarıyor; enjeksiyonla kırmızı üretildi.
   **Denetleyicinin körlüğü, denetlediği kodunkiyle aynı sınıftır; denetleyici
   de enjeksiyonla sınanır** — ve desen-güvenliğinin yanına sayısal
   beklenen-değer konur (yetkili kapı sayısı eşiğin altına düşerse build kırılır).
16. **Yeni çıkış yolu = tüm belgelenmiş yan etkilerin yeniden envanteri.**
   `catch`, kontrol akışına eklenen bir kapıdır; longjmp atladığı her satırın
   yan etkisini iptal eder. Fonksiyonun sözleşmesi N kapıda da aynı olmalı.
   (`_wings_last_status` "her dönüşte yazılır" notu tam olarak bu envanterdi ve
   işini yaptı.) R1'in "tanı tek kapıdan çıkar" ilkesinin ayna hâli: **yan etki
   de tek envanterden denetlenir; yanıt da tek kapıdan çıkar.**
15. **Göç aleti hedef sözleşmeyi test eder, mevcut olanı değil** — göç
   süresince iki sözleşme eşzamanlı yaşar; hedefin harness'ı flip'ten ÖNCE
   yeşile oturur, böylece flip günü hiçbir şey değişmez. (`build.sh test`
   strict koşuyor, dil koşmuyor; Y1 branch'i de aynı kalıp.)
13. **Ölçüm aleti dil varsayılanından sıkı olabilir** — dilin sözleşmesi
   yumuşak kalabilir, ama harness hatayı görmezden gelmemeli. `build.sh test`
   strict koşuyor; dil koşmuyor.
14. **Yeşil bir test kusursuz kod demek değildir; kusurlar bileşik kurup doğru
   üretebilir.** En tehlikeli yeşil, kusurların birbirini sildiği yeşildir:
   L1 (kısa devre yok) + yumuşak OOB (0 ikamesi) birlikte `o[-1]` okumasını
   `0 > v = false` yapıp insertion sort'u DOĞRU çalıştırıyordu. Tek birini
   düzeltmek bileşiği bozar — düzeltme sırası bağımlılık grafiğiyle yürür.
7. **Başarısızlık işareti meşru değerle aynı sentinel'i paylaşamaz** — `TYPE_VOID`
   hem "değer üretmiyor" hem "çıkaramadım" hem "dönüş yazılmamış" demekti;
   üçü de aynı yerde oturunca başarısızlık geçerli davranış kisvesi kazandı.
8. **Muafiyet listesi yerine kaynağı düzelt** — iki hakikat kaynağı er geç
   ayrışır; 12 adlık muafiyet listesinin 11'i kurguydu.
9b. **Ölçüm aleti de bir programdır** — ona uyguladığın şüphe sahibine de
   uygulanır. `$?` boru hattında son komutundur; bu oturumda iki kez yanılttı.
10. **Hiç kırmızı görülmemiş bir ölçüm düzeneği yeşil değil, bilinmiyor** —
   enjeksiyonla kanıtla. (P26 iç suite için hak edilmiş yeşili verdi; R2 örnek
   suite'inin kör olduğunu gösterdi.)
11. **Bilinmeyen araç, eksik araçtır** — katalog envanterini gerçek envanterden
   bağımsız doğrula; uyuşmazlık = bulgu. `mutex_*` üç tur boyunca "yok"
   sanıldı, meğer katalogda yokmuş.
12. **API'yi yanlış kullanan test, özelliği yanlış suçlar** — `push`,
   `body_schema`/`req.json`: ikisi de önce "dil bozuk" diye raporlanacaktı.
9. **Zayıf çağrı sessizdir** — `arena_restore` çalışıyormuş gibi görünüp
   serbest bırakmıyordu. API'nin iki katmanı varsa hangisini kullandığını
   ölç, adına güvenme.

**Global lint neden ertelendi (yöntem notu).** Yazma tarafı ucuz: hangi
global'in hangi `thread_create` erişiminden sonra yazıldığını bulmak birkaç
saatlik iş. Ama tek başına yazma tarafı **yanlış mesaj** üretir — "bu global
yazılıyor" bir kusur değil, programın olağan hâli. Doğru cümle *"bu global
başka bir thread'de **okunuyor** — senkronizasyon nerede?"*, ve o cümle okuma
tarafını, yani genel bir AST gezgini gerektiriyor. Yarım lint, gürültü üretip
kendi kapatılmasına yol açardı (#10'un tersi: hiç kırmızıya dönmeyen değil,
**sürekli kırmızıya dönen** düzenek de bilgi taşımaz). Erteleme, yapılamadığı
için değil; **eksik hâli zararlı olduğu için**.

## Açık kuyruk

`srv_json` soak · `thread_join` dönüş değeri · `thread_create` derin kopya
(5 koşulla onaylı) · global lint · donmuş 9 dilli CSV + checksum kolonu ·
llvm-mca ile `fib` atribüsyonu · gcc üstünlüğünün bayrak ikili araması ·
FP/SIMD.
