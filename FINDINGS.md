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
| Y1 | Dil statik tipli | **daraltılmalı** — `var` çapraz-tip yeniden atamaya izin veriyor | `tests/typeinfer/` |
| Y2 | `void` dönüşün atanması denetleniyor | **çürütüldü, DÜZELTİLDİ** — `e = push(e,3)` geçiyordu; artık hata | `tests/typeinfer/fail/11_void_assignment.tpr` |
| Y3 | Eleman ataması tip denetleniyor | **çürütüldü, DÜZELTİLDİ (branch)** — `str[] s; s[0]=5;` geçiyor VE çalışıyordu | `fail/13_element_assign_type.tpr` |
| Y4 | `var` çıkarıldığı tipte kalıyor | **çürütüldü, DÜZELTİLDİ (branch)** — `var b=[1,2]; b=5;` geçiyordu | `fail/12_var_cross_type.tpr` |
| Y5 | `call("ad")` adın varlığını doğruluyor | **çürütüldü** — literal adda bile doğrulamıyor; hata çalışma zamanına kalıyor | P25 |
| R1 | Çalışma zamanı hatası süreci başarısız kılıyor | **çürütüldü; KISMEN düzeltildi** — tanılar artık stderr'e gidiyor (stdout temiz); çıkış kodu ≠ 0 `TULPAR_STRICT_RUNTIME=1` ile **seçime bağlı** | P25 |
| R5 | "Sınır dışı → 0, devam" bir kaza | **çürütüldü** — TEST EDİLMİŞ sözleşme (`loop_versioning::run_sinir_disi_indeks`); strict'i varsayılan yapmak 2 suite'i kırıyor → **karar kullanıcıya** | R1 seti |
| R6 | Sondalar tanıyı doğru yerde arıyor | **çürütüldü, DÜZELTİLDİ** — `silent_failure_probe.py` tanıyı stdout'ta bekliyordu; stderr süzgeci eklendi | R1 seti |
| R7 | Site'nin "invalid body → 422" vaadi tutuyor | **doğrulandı** — 422 + alan detayı (`name: required`, `expected str, got int`) | P28 |
| R2 | `build.sh test` çalışma zamanı hatasını yakalıyor | **çürütüldü** — enjeksiyonla ölçüldü: bir örneğe `kk[999]` eklendi, suite "All tests passed" dedi | P25 |
| T5 | `thread_join` işçinin sonucunu taşıyor | **çürütüldü** — 0 dönüyor; katalogda `void` olarak kaydedildi, artık atanması hata | P20 |
| T6 | Dilde senkronizasyon ilkeli yok | **çürütüldü** — `mutex_*` var ve çalışıyor; yalnız katalogda yokmuş | P27 |
| C9 | Suite'ler çalışma zamanı hatasını yakalıyor | **doğrulandı (enjeksiyonla)** — tek dosya çıkış 1, suite çıkış 1, paket sayısı 75'te kalıyor | P26 |
| R3 | `try/catch` çalışma zamanı hatasını yakalıyor | **çürütüldü** — yakalamıyor; akış `try` içinde devam ediyor | P26b |
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

**Durum: açık.** Codegen ameliyatı; kendi audit'iyle yürümeli.

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

## Açık kuyruk

`srv_json` soak · `thread_join` dönüş değeri · `thread_create` derin kopya
(5 koşulla onaylı) · global lint · donmuş 9 dilli CSV + checksum kolonu ·
llvm-mca ile `fib` atribüsyonu · gcc üstünlüğünün bayrak ikili araması ·
FP/SIMD.
