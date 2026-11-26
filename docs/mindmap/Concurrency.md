# Eşzamanlılık — P10 doğruluk kapısı (2026-09-09)

> **Güncel durum (2026-11-25, koda bakılarak):** muteks, **atomikler** ve
> **`@thread_local`** var (K040, 2026-09-28) ve bir bellek modeli cümlesi
> yazılabiliyor (aşağıda "Bellek modeli"). Wings sayaçları atomik. Açık kalan:
> TSan'lı CI işi ve `mutex_*` / `thread_create` için LSP açıklama metinleri
> (bugün boş; `atomic_*`'ınki var). Aşağıdaki bulgular tarihçedir; Bulgu 2'deki "dilin bellek modeli
> yok" cümlesi artık geçerli değil.

## Bellek modeli (sözleşme, 2026-11-25)

Paylaşılan değişebilir duruma ya bir **mutex** altında ya da **atomic**
yerleşiklerle erişilmelidir:
- Bir mutex altında yapılan yazmalar, aynı mutex'i sonradan alan thread'e
  görünür.
- Bir `"release"` atomic yazmasından önceki yazmalar, aynı değişkeni
  `"acquire"` ile okuyup o değeri gören thread'e görünür.
- `"seq_cst"` (varsayılan) tüm seq_cst işlemlerine tek bir toplam sıra verir;
  `"relaxed"` yalnız o değişkenin kendi atomikliğini garanti eder.
- **Korumasız paylaşım tanımsızdır**: güncelleme kaybı (Bulgu 1) ve görünürlük
  kaybı (Bulgu 2) ölçüldü. Bekleme döngüsü mutex altında ya da
  `atomic_load(f, "acquire")` ile okumalı — `atomic_store(f, 1)` + düz
  `while (f == 0)` aynı tuzaktır (`thread_lint` bunu uyarır).
- Paylaşılan bir `int[]` / `json`'u eşzamanlı **yalnız okumak** güvenli
  (Bulgu 3 düzeltmesi, `tests/debox_yaris.sh`).
- **Thread sınırı kopyalayarak geçilir:** `thread_create` argümanı ve sonucu
  `aot_persist` ile derin kopyalanır; her thread'in arenası ve bölgesi
  kendine özel. Global'e yazma bariyerden geçer ([[Memory Model]]).

| araç | kapsam | kanıt |
|---|---|---|
| `mutex_create/lock/unlock/destroy` | kritik bölge | 8 × 50 000 → 400 000 (aşağıda) |
| `atomic_load/store/add/sub/xchg/cas` | üst düzey `int` global; son argüman bellek sırası | `tests/atomik.test.tpr` (4 × 50 000 `atomic_add` → 200 000, release/acquire yayını), `tests/atomik_hatalari.sh` |
| `@thread_local int x` | thread başına global (int/float/bool, üst düzey) | aynı test |
| `thread_lint.cpp` | worker'da yazılıp senkronizasyonsuz okunan global | 2026-09-11'den beri |

> **Özet (2026-09-10'da DÜZELTİLDİ): dilde MUTEKS var ve çalışıyor.**
> `thread_create` ile paylaşılan değişebilir durum korumasız kullanılırsa ne
> atomik ne görünür — ama `mutex_lock`/`mutex_unlock` ikisini de çözüyor
> (ölçüldü, aşağıda). Sözleşme "paylaşım desteklenmiyor" değil, **"paylaşım
> muteks ister"**. İlkeller zaten vardı; tip katalogunda olmadıkları için
> ne typecheck görüyordu ne de ben.

## DÜZELTME — muteksler var, çalışıyor, ve katalogda yoktular

Bu belgenin ilk hâli "dilde bellek modeli yok, paylaşım tanımsız" diyordu.
Eksik olan parça, LSP tablosuyla tip katalogunun çapraz süpürülmesinde çıktı
(P27): `mutex_create` / `mutex_lock` / `mutex_unlock` / `mutex_destroy`
**dilde mevcut**, ama tip katalogunda kayıtlı değillerdi — yani hem typecheck
onları görmüyordu hem de builtin listesine bakan ben.

Bulgu 1'in birebir aynısı, muteksle (8 thread × 50 000 artırma):

| | `done` | `counter` |
|---|---|---|
| korumasız | 7 (kayıp) | değişken |
| **muteksli** | **8** | **400 000** — üç koşuda da tam |

Muteks **Bulgu 2'yi de** çözüyor: `mutex_lock` opak bir çağrı olduğu için
optimize edici global okumasını döngüden çıkaramıyor, yani `done`'ın güncel
değeri görünüyor.

**Geriye kalan gerçek eksik**, sözleşmenin kendisi: bu ilkellerin varlığı
hiçbir yerde belgelenmiyor, `thread_create` dokümanı paylaşımdan söz etmiyor
ve korumasız paylaşım sessizce yanlış sonuç veriyor. Yani sorun "araç yok"
değil, **"araç var ama kimse söylemiyor"**.

## Zemin: ARC AOT yolunda çalışmıyor

`runtime/tulpar_arc.cpp`'deki `arc_retain`/`arc_release` sayaçları **atomik
değil** (`obj->ref_count++`). Ama `runtime_bindings.cpp`'deki ölçülmüş nota
göre **AOT yolu `arc_release`i hiç çağırmıyor** (llvm_backend.cpp ve
runtime_bindings.cpp'de sıfır çağrı). Yani:

- Kullanıcı kodunda **refcount trafiği yok** → refcount yarışı da yok.
- Bellek yönetimi pratikte **arena** (toplu sıfırlama) + kalıcı değerler için
  ömür boyu yaşayan `malloc`. `arc_*` yalnız `tulpar_async.cpp`'de kullanılıyor
  ve orası tek iş parçacıklı (ucontext coroutine'leri, thread değil).
- **Sonuç:** "ARC throughput bedeli" tartışması AOT yolu için konusuz. Belgeler
  ARC'ı çalışan bir mekanizma gibi anlatıyorsa düzeltilmeli.

Runtime'ın thread'e duyarlı kısmı zaten sertleştirilmiş: arena, dizgi
tamponları, istisna bağlamı, SQLite bağlantıları, bölge takibi hepsi
`thread_local` ve kaynakta *"MUST be thread_local: under a multi-threaded
listener"* notu var. Yani **runtime'ın kendi iç durumu** güvenli.

Tehlike runtime'da değil, **kullanıcı seviyesindeki paylaşılan global'lerde**.

## Bulgu 1 — Global'ler paylaşılıyor ama atomik değil (güncelleme kaybı)

8 thread × 100 000 artırma, paylaşılan `int` global:

```
done=7 counter=800000 beklenen=800000   <- counter tam, ama done KAYIP
done=7 counter=700000 beklenen=800000
```

`counter` çoğu koşuda 800000'e ulaşıyor (yani global gerçekten **paylaşılıyor**,
thread başına kopya değil), ama 8 thread'in her birinin bir kez yaptığı
`done = done + 1` **7'de kalıyor**. Oku-değiştir-yaz atomik değil.

## Bulgu 2 — Yazmalar diğer thread'e GÖRÜNMÜYOR (döngüde önbelleğe alınıyor)

```tulpar
int fin = 0;
func w(int id) { fin = 1; return 0; }
thread_create(w, 0);
while (fin == 0 && s < 200000000) { s = s + 1; }   // ASLA cikmaz
print("fin=" + toString(fin));                      // fin=0
```

İşçi gerçekten koşuyor (çıktısı görünüyor) ve `fin`'e yazıyor, ama ana
thread'in döngüsü hiç görmüyor: optimize edici global okumasını döngüden
dışarı çıkarıp yazmaçta tutuyor. **Doğru davranış**, çünkü dilin bir bellek
modeli yok — `volatile`/atomik/ordering kavramı yok. Sonuç: bekleme döngüleri
(spin-wait) sessizce sonsuza kadar döner.

## Bulgu 3 — GERİ ÇEKİLDİ (test hatalıydı)

> ⚠️ **Bu bulgu 2026-09-09'da yayınlandı ve aynı gün geri çekildi.** Önce
> "8 thread paylaşılan bir `int[]`'i yalnız okurken program çöküyor" denmişti.
> Yanlıştı: test `shared = push(shared, i)` yazıyordu. **`push` diziyi yerinde
> değiştirir**, dönüş değeri dizi değildir — atama diziyi eziyordu. Sonuç: dizi
> tek thread'de bile boştu (`len=0`) ve çöküş thread'lerle ilgisizdi.
>
> Doğru yazımla (`push(shared, i);` deyim olarak) test **üç koşuda da temiz
> geçiyor**: `fin=8`, hatalı toplam `0`. **Paylaşılan bir `int[]`'i eşzamanlı
> okumak çalışıyor.**

Geriye kalan gerçek nokta, ölçümden değil **kaynak incelemesinden**: `arr_debox`
bir *okuma* yolundan (`arr_items`) tetikleniyor ve dizi başlığına yazıyor
(`malloc`, `free(idata)`, işaretçi güncellemesi), hiçbir senkronizasyon yok.
İki thread aynı anda oraya girerse yarış vardır — ama bunu **tetikleyen bir test
yok**. [[Tuzaklar#7b]] uyarınca tehlikeyi mekanizmadan çıkarıp sertleştirdik
(çift denetimli kilit + eski tamponu serbest bırakmama), ama bunun *kanıtlanmış
bir hatayı* değil, *incelemeyle görülen bir yarışı* kapattığını açıkça yazıyoruz.

**Güncelleme 2026-09-27 — artık tetikleyen test VAR** (`tests/debox_yaris.sh`,
`build.sh suites`). 8 thread × 60 taze dizi × 100 000 eleman bir bariyerde
bekleyip aynı anda `arr_items` çağırıyor; her thread dönen tamponu kaydediyor.
Tek çevrim = hepsi aynı tampon. Ölçü (Ryzen 7 9800X3D): gerçek `arr_debox`
0/60 dizide çift çevrim; **kilitsiz kopya (pozitif kontrol, sertleştirmeden
önceki algoritma) 60/60** — düzenek yarışı gerçekten üretiyor. Kilit geçici
olarak sökülünce gerçek `arr_debox` da 60/60 kırmızı döndü. Yani yarış artık
incelemeyle değil ölçümle görülüyor ve kilit onu kapatıyor.

**P15 (paylaşılan json okuması) de temiz** — aynı düzeltilmiş düzenekle hata yok.

## Bunun Wings / `listen_pool` başlığına etkisi

`lib/wings.tpr` ve `lib/router.tpr` değişebilir global tutuyor —
`_wings_requests_total`, `_wings_requests_2xx/4xx`, `_request` (json),
`_router_routes` (array). `listen_pool` bunları **çok iş parçacıklı** koşuyor.
**Bulgu 1 doğrudan uygulanır**: sayaçlar korumasız oku-değiştir-yaz yapıyor,
yani eksik sayarlar. Bulgu 3 geri çekildiği için "paylaşılan aggregate bozulur"
iddiası **artık yapılmıyor**; kalan risk incelemeyle görülen `arr_debox` yarışı
ve o da sertleştirildi.

**Bu yüzden `~36k req/s` başlığı bu kapı kapanana dek yıldızlı yayınlanmalı.**
Sayı yanlış demiyoruz — altındaki eşzamanlılık sözleşmesinin belgesiz ve
sınanmamış olduğunu söylüyoruz.

## Ne yapılmalı (öncelik sırası)

1. **Sözleşmeyi yaz.** Bugünkü gerçek: *"paylaşılan değişebilir duruma
   `mutex_lock`/`mutex_unlock` ile erişin; korumasız erişim hem güncelleme
   kaybeder hem görünürlük garantisi vermez."* Belgesiz bırakmak, kullanıcının
   sessiz veri kaybıyla tanışması demek — üstelik çözüm elinin altındayken.
2. ~~Wings sayaçlarını thread-güvenli yap~~ — **yapıldı**: derleyici
   `_wings_requests_*` için `name = name + delta` desenini tanıyıp tek bir
   `atomicrmw add` üretiyor (`global_needs_atomic_rmw`, `llvm_backend.cpp`).
   Bu özel durum kullanıcı global'lerine uygulanmaz; kullanıcı `atomic_add`
   kullanır.
3. ~~Tembel kutulama çevrimini kapıla~~ — **yapıldı** (çift denetimli kilit;
   eski tampon bilerek serbest bırakılmıyor ki hızlı yoldaki bir okuyucu
   use-after-free yaşamasın). Kanıtlanmış bir hatayı değil, incelemeyle görülen
   bir yarışı kapatıyor.
4. ~~Atomik yerleşikler~~ — **yapıldı** (K040, 2026-09-28; sıralama
   parametresiyle). Bellek modeli cümlesi yukarıda. Kalan: `mutex_*` ve
   `thread_create` için LSP açıklama metinleri (bugün boş) ve
   `thread_create` belgesinin bu sözleşmeye bağlanması.
5. TSan'lı bir CI işi: runtime'ı `-fsanitize=thread` ile derleyip yukarıdaki üç
   üretecin koşulması.

## Ölçüm dosyaları

`/tmp` altında üretildi (kalıcı değil): `mutate.tpr` (bulgu 1),
`min2.tpr`/`min3.tpr` (bulgu 2), `readonly_fix.tpr` (bulgu 3'ün düzeltilmiş,
temiz geçen hâli). Kalıcı gerileme
testi hâline getirilmeleri 5. maddeye bağlı — bugünkü davranış "hatalı" olduğu
için testler ancak sözleşme yazıldıktan sonra anlamlı olur.

## İlgili
[[Tuzaklar]] · [[Testing]] · [[Performance]] · [[Decisions]]
