# Bellek — P14 süzme testi (2026-09-09)

> **Özet: döngü içinde üretilen her heap değeri birikiyor ve geri alınmıyor.
> `arena_save`/`arena_restore` bu belleği kurtarmıyor.** Tam sayı yükleri düz
> kalıyor. Uzun ömürlü süreçler için bu bir tasarım sınırı, ve bugün belgesiz.

## Zemin

[[Concurrency]]'de kayda geçen ölçüm: **AOT yolu `arc_release`i hiç
çağırmıyor.** Yani "otomatik referans sayımı" AOT'ta çalışmıyor; kullanıcının
gördüğü model arena + kalıcı `malloc`. Doğal soru: arena dışında kalan değerler
ne oluyor?

## Ölçüm

İş yükü 5 kat artırılıp RSS zirvesi ölçüldü (`/proc/<pid>/status` `VmHWM`):

| Yük | N=200k | N=1M | oran |
|---|---:|---:|---:|
| yalnız tamsayı aritmetiği | 16 KB | 12 KB | **0,75× (düz)** |
| yalnız dizgi birleştirme | 22 MB | 135 MB | **6,06×** |
| yalnız json kurma | 78 MB | 399 MB | **5,15×** |
| dizgi + json | 160 MB | 810 MB | 5,06× |

Tam sayı satırı düz — ölçüm düzeneği sağlam, sızan şey özellikle **heap
değerleri**. Yaklaşık maliyet: kısa bir dizgi başına ~135 bayt, üç anahtarlı
küçük bir json başına ~400 bayt, hiç geri alınmıyor.

## `arena_save`/`arena_restore` yardım ETMİYOR

Aynı dizgi yükü, tur başına açık `arena_save()` / `arena_restore()` ile:

| | N=200k | N=1M |
|---|---:|---:|
| arena_save/restore **ile** | 23 292 KB | 132 488 KB |
| arena_save **olmadan** | 24 624 KB | 131 452 KB |

Fark yok. `arena_save()` geçerli bir tanıtıcı döndürüyor (`0`, `-1` değil),
yani çağrı başarılı — ama bu bellek checkpoint ile geri sarılabilen arenada
değil. Değerler ya doğrudan kalıcı (`malloc`) ayrılıyor ya da yazma bariyeri
onları kalıcıya terfi ettiriyor; her iki durumda da `arc_release`
çağrılmadığı için hiç serbest bırakılmıyorlar.

## Neden önemli

`tulpar` kısa ömürlü betikler için sorunsuz: süreç biter, işletim sistemi
belleği alır. Sorun **uzun ömürlü süreçlerde**: bir Wings sunucusu istek başına
dizgi/json üretiyorsa RSS toplam üretilen değerle birlikte tırmanır.

⚠ **Bu ölçüm sunucuyu değil, düz döngüleri kapsıyor.** Wings'in istek başına
patikası `arena_restore` dışında `arena_drop` da kullanıyor olabilir (kaynakta
"RELEASES the checkpoint" notu var). Sunucunun gerçekten sızıp sızmadığı
**ayrıca ölçülmeli** — 1 saatlik sabit yük altında RSS eğrisi. Bu ölçüm
yapılana dek "Wings sızdırıyor" DENMEMELİ; söylenebilecek olan, dilin genel
değer üretme yolunun geri kazanım yapmadığı.

## Yapılacaklar

1. **Sunucu süzme testi** — 1 saat sabit yük, RSS eğrisi, iki handler: (a) saf
   arena patikası, (b) istek başına dizgi/json kuran. Ürün tanımını bu belirler.
2. **Sözleşmeyi belgele** — bugünkü gerçek: "uzun ömürlü döngülerde üretilen
   heap değerleri geri alınmaz". Kullanıcının bunu RSS grafiğinden öğrenmesi
   kabul edilemez.
3. `arena_restore`'un neden bu değerleri kapsamadığını netleştir: kalıcıya terfi
   mi, yoksa ayırma zaten arena dışında mı? İkisi farklı düzeltme gerektirir.
4. Uzun vadede: ya AOT yolunda `arc_release`i gerçekten devreye al (bedeli
   ölçülmeli — bugünkü bütün performans rakamları geri kazanım YAPMAYAN bir
   runtime'ın rakamları), ya da kapsamı belgelenmiş bir bölge modeli.

## İlgili
[[Concurrency]] · [[Tuzaklar]] · [[Performance]] · [[Decisions]]
