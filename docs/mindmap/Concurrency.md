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

## Bulgu 3 — "Salt-okur paylaşım" runtime seviyesinde salt-okur DEĞİL

8 thread paylaşılan bir `int[]`'i yalnız **okuyor**. Sonuç:

```
Calisma Zamani Hatasi: get islemi icin gecersiz hedef veya indeks
```

Sebep: `ObjArray` kutulanmamış (`idata`) ya da kutulu (`items_`) tutuluyor ve
**genel yoldan ilk erişimde tembel olarak kutuluya çevriliyor** — kaynaktaki
not: *"bir dizi, kutulanmamış hızlı yolun dışında ilk kez kullanıldığı anda bir
kez çevriliyor"*. Bu çevrim dizi başlığına **yazar**. Yani iki thread aynı
diziyi "okurken" ikisi de başlığı yazmaya çalışıyor → bozulma.

**Genel ilke: bu runtime'da okuma her zaman okuma değildir.** Tembel temsil
değişimi olan her yapı için "salt-okur paylaşım güvenlidir" varsayımı yanlış.

## Bunun Wings / `listen_pool` başlığına etkisi

`lib/wings.tpr` ve `lib/router.tpr` değişebilir global tutuyor —
`_wings_requests_total`, `_wings_requests_2xx/4xx`, `_request` (json),
`_router_routes` (array). `listen_pool` bunları **çok iş parçacıklı** koşuyor.
Bulgu 1 ve 3 doğrudan uygulanır: sayaçlar eksik sayar, ve istek başına
paylaşılan `json`/`array` durumu tembel çevrim yarışına açıktır.

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
3. **Tembel kutulama çevrimini** paylaşılan dizilerde kapıla ya da dizi
   oluşturulurken kesinleştir.
4. Atomik yerleşikler (`atomic_add`, `atomic_load`) ve bir `volatile`/bariyer
   kavramı — bellek modelinin en küçük hali.
5. TSan'lı bir CI işi: runtime'ı `-fsanitize=thread` ile derleyip yukarıdaki üç
   üretecin koşulması.

## Ölçüm dosyaları

`/tmp` altında üretildi (kalıcı değil): `mutate.tpr` (bulgu 1),
`min2.tpr`/`min3.tpr` (bulgu 2), `readonly.tpr` (bulgu 3). Kalıcı gerileme
testi hâline getirilmeleri 5. maddeye bağlı — bugünkü davranış "hatalı" olduğu
için testler ancak sözleşme yazıldıktan sonra anlamlı olur.

## İlgili
[[Tuzaklar]] · [[Testing]] · [[Performance]] · [[Decisions]]
