# Bellek — P14 süzme testi (2026-09-09)

> **Özet (2026-11-25'te düzeltildi): bellek geri alınabilir. `arena_restore` ile
> `arena_drop` aynı geri sarmayı yapar; fark yalnız checkpoint yığınının tepesi.**
> P14'teki "save + restore" sızıntısının sebebi restore değil, döngüde
> **tekrar tekrar `arena_save`** çağrılmasıydı: 33. kayıt -1 döner ve ondan
> sonraki her restore no-op olur. Tercih edilen yol bugün `@frame` (K038).
> Checkpoint kurmayan uzun ömürlü döngüler hâlâ lineer tırmanıyor.

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

> ⚠️ **İkinci düzeltme (2026-11-25, koda bakılarak).** Bu bölüm önce
> "`arena_restore` checkpoint'i ve blokları serbest bırakmaz, serbest bırakan
> `arena_drop`" diyordu. Yanlıştı. İkisi de `aot_arena_rewind_to`'yu çağırır:
> dizgi arenası geri sarılır, bölgedeki obje/diziler serbest kalır. "Releases
> the checkpoint" yığındaki **slotu** bırakmak demek, belleği değil
> (`restore`: `top = idx + 1`, `drop`: `top = idx`). Arena blokları hiçbir
> çağrıda işletim sistemine dönmez.

Sondanın döngüsü her iterasyonda `arena_save` çağırıyordu. Yığın 32 slot
(`AOT_ARENA_CHECKPOINT_MAX`): 33. kayıt **-1** döner, `arena_restore(-1)`
sessizce hiçbir şey yapmaz — tablodaki 23 MB → 132 MB bundan. `arena_drop`
slotu bıraktığı için yığın dolmuyor ve satır düz kalıyor.

**Gerçek API tuzağı:** tekrar çalışan bir `arena_save`'i drop ile kapatmamak.
Program doğru sonuç verir, hata çıkmaz, yalnız 33. çağrıdan sonra bellek geri
gelmez. Tek save + çok restore (aynı handle'a) ise geçerli ve düz bir desen.
`@frame` (K038) bunu derleyiciye bırakır ve yığın doluysa yakalanabilir hata
verir. 2026-11-26'dan beri (RFC C3) elle çağrılan `arena_save` de 33. kayıtta
sessiz -1 yerine yakalanabilir hata atıyor — P14'teki "save + restore" sondası
bugün 33. iterasyonda gürültüyle durur.

## Sunucu yolu: DÜZ, ve sebebi biliniyor

Wings'in üç deseni de yığını dengeli tutuyor (2026-11-25'te koda bakıldı;
eski hâli "istek başına save → restore → drop" diyordu, bu yalnız evented için
doğru):

| mod | desen |
|---|---|
| `listen()` | başta tek `arena_save`, her istekten sonra `arena_restore`, drop yok (500k+ istekte RSS 6,9 MB'ta düz) |
| `listen_pool` / `listen_async` | bağlantı başına save, istek başına restore, bağlantı kapanınca drop |
| `listen_evented` | tick başına ve istek başına save / drop |

Soak testi bunu doğruluyor:

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
2. **Sözleşmeyi belgele** — taslak var: Bellek Sözleşmesi v0 RFC'si
   (2026-11-25; depolama sınıfları, checkpoint kuralları, bariyer, bilinen
   delikler). Özeti [[Memory Model]]'de.
3. ~~`arena_restore` neden kapsamıyor~~ — **cevaplandı, ikinci kez**: kapsıyor
   ve serbest bırakıyor; sızan, drop ile kapatılmayan tekrarlı `arena_save`'di
   (yukarıda). Fonksiyon kapsamı için `@frame` var (K038).
4. ~~Elle çağrılan `arena_save` yığın doluyken sessiz -1 yerine yakalanabilir
   hata versin (RFC C3)~~ — **yapıldı** (2026-11-26, `tests/arena_yigin_dolu.test.tpr`).
5. **Açık:** bellek sayacı yerleşiği (`mem_stats()`: arena kullanımı, bölgedeki
   nesne sayısı, kalıcı heap baytı, checkpoint derinliği) — sızıntı RSS
   grafiğinden değil programdan görünsün (RFC C4).
6. Uzun vadede: ya AOT yolunda `arc_release`i gerçekten devreye al (bedeli
   ölçülmeli — bugünkü bütün performans rakamları geri kazanım YAPMAYAN bir
   runtime'ın rakamları; yalnız kalıcı katmana refcount **reddedildi**, bariyer
   dışındaki takma adları göremiyor), ya da kapsamı belgelenmiş bir bölge
   modeli.

## İlgili
[[Concurrency]] · [[Tuzaklar]] · [[Performance]] · [[Decisions]]
