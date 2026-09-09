# Bulgular — tek kaynak

Bir dış denetimle yürütülen ölçüm turlarının kaydı. **İddia burada yaşar;**
PR gövdeleri, `docs/mindmap/*` ve tulparlang.dev buraya referans verir, iddiayı
kopyalamaz. İki turdur yaşanan "bir yerde düzeltildi, ötekinde bayat kaldı"
sorununun çözümü bu dosya.

Durum: **açık** · **doğrulandı** · **çürütüldü** · **geri çekildi** · **düzeltildi**

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
| T1 | Paylaşılan global'ler atomik | **çürütüldü** — 8 thread × bir artırma → 7 | Concurrency |
| T2 | Thread yazmaları görünür | **çürütüldü** — spin-wait sonsuza döner; `sleep()` varken kazara çalışır | Concurrency |
| T3 | Paylaşılan dizinin eşzamanlı okunması bozuluyor | **GERİ ÇEKİLDİ** — test `push` dönüşünü atıyordu, dizi tek thread'de bile boştu | [Tuzaklar 7a](docs/mindmap/Tuzaklar.md) |
| T4 | `arr_debox` okuma yolundan yazıyor | **açık (incelemeyle)** — sertleştirildi, ama tetikleyen test yok | Concurrency |
| Y1 | Dil statik tipli | **daraltılmalı** — `var` çapraz-tip yeniden atamaya izin veriyor | `tests/typeinfer/` |
| Y2 | `void` dönüşün atanması denetleniyor | **çürütüldü, DÜZELTİLDİ** — `e = push(e,3)` geçiyordu; artık hata | `tests/typeinfer/fail/11_void_assignment.tpr` |

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
9. **Zayıf çağrı sessizdir** — `arena_restore` çalışıyormuş gibi görünüp
   serbest bırakmıyordu. API'nin iki katmanı varsa hangisini kullandığını
   ölç, adına güvenme.

## Açık kuyruk

`srv_json` soak · `thread_join` dönüş değeri · `thread_create` derin kopya
(5 koşulla onaylı) · global lint · donmuş 9 dilli CSV + checksum kolonu ·
llvm-mca ile `fib` atribüsyonu · gcc üstünlüğünün bayrak ikili araması ·
FP/SIMD.
