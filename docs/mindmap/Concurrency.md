# Eşzamanlılık — P10 doğruluk kapısı (2026-09-09)

> **Özet: Tulpar'ın bir bellek modeli yok.** `thread_create` ile paylaşılan
> değişebilir durum ne atomik ne de görünür. Üç ayrı hata sınıfı ölçümle
> gösterildi. Bu bir performans notu değil, **doğruluk** notu.

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

1. **Sözleşmeyi yaz.** Bugünkü gerçek: *"`thread_create` ile paylaşılan
   değişebilir durum desteklenmiyor; her thread kendi verisiyle çalışmalı,
   iletişim işletim sistemi ilkelleriyle (soket/dosya) yapılmalı."* Belgesiz
   bırakmak, kullanıcının sessiz veri kaybıyla tanışması demek.
2. **Wings sayaçlarını thread-güvenli yap** (atomik yerleşik ya da thread başına
   toplama + okuma anında birleştirme).
3. ~~Tembel kutulama çevrimini kapıla~~ — **yapıldı** (çift denetimli kilit;
   eski tampon bilerek serbest bırakılmıyor ki hızlı yoldaki bir okuyucu
   use-after-free yaşamasın). Kanıtlanmış bir hatayı değil, incelemeyle görülen
   bir yarışı kapatıyor.
4. Atomik yerleşikler (`atomic_add`, `atomic_load`) ve bir `volatile`/bariyer
   kavramı — bellek modelinin en küçük hali.
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
