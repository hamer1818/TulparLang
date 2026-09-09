# Bellek — P14 süzme testi (2026-09-09)

> **Özet: bellek geri alınabilir, ama doğru çağrı `arena_drop` — `arena_restore`
> tek başına yetmiyor.** Sunucu yolu (wings) `arena_drop` çağırdığı için 4,16
> milyon istekte düz kalıyor. `arena_drop` çağırmayan uzun ömürlü döngüler
> lineer tırmanıyor. Bu bir sızıntı değil, **belgesiz bir sözleşme**.

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

## DÜZELTME (aynı gün): eksik olan çağrı `arena_drop`'tu

> ⚠️ Bu belgenin ilk hâli "`arena_save`/`arena_restore` bu belleği kurtarmıyor,
> yani değerler geri alınamıyor" diyordu. **İkinci yarısı yanlıştı.** Sondam
> API'nin iki katmanından yalnız zayıf olanını kullanıyordu.

| varyant | N=200k | N=1M | oran |
|---|---:|---:|---:|
| arena çağrısı yok | 160 MB | 810 MB | 5,06× |
| `arena_save` + `arena_restore` | 23 MB | 132 MB | 5,7× |
| `arena_save` + `arena_restore` + **`arena_drop`** | **2 976 KB** | **2 972 KB** | **1,00× DÜZ** |

`arena_restore` tepe işaretçisini geri sarar ama **checkpoint'i ve blokları
serbest bırakmaz**; serbest bırakan çağrı `arena_drop` (kaynaktaki not bunu
zaten söylüyordu: *"like arena_restore, but RELEASES the checkpoint"*).

**API tuzağı:** `arena_restore` tek başına *çalışıyormuş gibi görünür* —
program doğru sonuç verir, hata çıkmaz, yalnız bellek geri gelmez. Bir
kaynağın "geri sarar" demesi "serbest bırakır" demek değildir; ikisi ayrı
çağrı ve zayıf olanı sessizdir.

## Sunucu yolu: DÜZ, ve sebebi biliniyor

`lib/wings.tpr` istek başına `arena_save` → `arena_restore` → **`arena_drop`**
çağırıyor (satır 2070–2174). Soak testi bunu doğruluyor:

| sunucu | süre | istek | RSS |
|---|---|---|---|
| `srv_int` (tamsayı handler) | 1050 s | 11 664 987 | **3 448 KB sabit** |
| `srv_json` (istek başına string + 4 anahtarlı json) | 360 s | 4 164 616 | **3 444 KB sabit** |

4,16 milyon istek, her biri bir dizgi ve bir json kuruyor. Sızsaydı ~1,7 GB
olurdu. **Sunucu yolu geri kazanıyor.**

## Geriye kalan gerçek sınır

Sızıntı riski **`arena_drop` çağırmayan uzun ömürlü döngülerde**: bağımsız bir
script, bir batch işi, ya da kendi döngüsünü kuran bir daemon. Bu kod için
bugünkü sözleşme belgesiz — kullanıcının `arena_drop`'u bilmesi gerekiyor ve
`arena_restore`'un yetmediğini ancak RSS grafiğinden öğreniyor.

## Yapılacaklar

1. ~~Sunucu süzme testi~~ — **yapıldı**, ikisi de düz (yukarıdaki tablo).
2. **Sözleşmeyi belgele** — bugünkü gerçek: "uzun ömürlü döngülerde üretilen
   heap değerleri geri alınmaz". Kullanıcının bunu RSS grafiğinden öğrenmesi
   kabul edilemez.
3. ~~`arena_restore` neden kapsamıyor~~ — **cevaplandı**: kapsıyor ama serbest
   bırakmıyor; serbest bırakan `arena_drop`. `arena_restore`'un tek başına
   sessizce yetersiz kalması API tarafında düzeltilmeli (ya `restore` bıraksın,
   ya belgeler ikisini birlikte anlatsın).
4. Uzun vadede: ya AOT yolunda `arc_release`i gerçekten devreye al (bedeli
   ölçülmeli — bugünkü bütün performans rakamları geri kazanım YAPMAYAN bir
   runtime'ın rakamları), ya da kapsamı belgelenmiş bir bölge modeli.

## İlgili
[[Concurrency]] · [[Tuzaklar]] · [[Performance]] · [[Decisions]]
