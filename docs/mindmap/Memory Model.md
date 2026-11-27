---
tags: [runtime, memory, important]
---

# Memory Model — Arena + Region + Checkpoint

AOT'ta GC yok. Bellek üç mekanizmayla yönetilir (`src/vm/runtime_bindings.cpp`).

## 1) Arena (string'ler)
Per-thread blok allocator. `arena_save()` → checkpoint handle; `arena_restore(wm)` / `arena_drop(wm)` → o noktadan sonrasını geri sarar (blokları free etmez, `used=0` yapar → O(1) reuse). Wings her istekten sonra geri sararak belleği sabit tutar (desenler aşağıda, "Checkpoint disiplini").

**Bloklar işletim sistemine geri verilmez.** Geri sarma blokları yalnız yeniden kullanıma açar; RSS ulaştığı en yüksek seviyede kalır. Tek bir büyük tepe (bir kerelik büyük iş) RSS'i kalıcı olarak yükseltir.

**Ölümsüz dizgiler arenada değil:** dizgi sabitleri ve (2026-10-02'den beri) `typeof()`
sonuçları `aot_intern_string` ile malloc'lu, `arena_allocated = 0`, `ref_count = 1<<28`.
Bariyer onları kopyalamaz, restore/drop onlara dokunmaz. `typeof` eskiden her çağrıda arenaya
kopyalıyordu: 20M çağrılık döngü 335 ms / 1,07 GB RSS → 77 ms / 2,9 MB (Ryzen 7 9800X3D,
`taskset -c 10,11`, 2026-10-02). Kapılar `tests/typeof_sabit.{sh,test.tpr}`.

## 2) Per-request malloc region (obje/dizi)
AOT'ta her obje/dizi literali `malloc`'lanır (`arena_allocated=0`); GC sweep yok → eskiden **istek-başına sızıntı**. Çözüm: literaller `g_region` (thread_local) + `g_region_set`'e izlenir, arena_save/restore bunları bracket'ler. Yalnız arena scope içinde (`g_arena_checkpoint_top > 0`) allocate edilenler izlenir; top-level global'ler ve `persist()`/`string_pin` kopyaları izlenmez (kalıcı). → [[Memory Leak Fixes]]

## 3) Runtime write barrier
`wb_persist_escape(container, v)`: transient bir değer **kalıcı** bir container'a (global / zaten-persist'lenmiş obje) yazılırsa derin kopyalanır (`aot_persist`). Değer-akışı tabanlı; alias'lar üzerinden de yakalar. Wings'te global'e yazma otomatik kalıcılaşır ([[Wings]] auto-persist).

## 4) Küresel depolama bariyeri (codegen, 2026-09-25)
Bir **global değişkene** yazmak kap mutasyonu değil, düz bir LLVM `store` —
runtime bariyeri onu göremez. Bu yüzden global'e yazan **her** codegen yolu
(düz atama, `+=` vb. bileşik atama, `D[] g = [...]` yeniden kurma, üst düzey
bildirim) `emit_global_store_barrier`'dan geçer; etiket denetimi satır içi,
yalnız yığın değerinde çağrı:
- `aot_persist_global` (atama): dizgi + struct dizisi **yalnız geçiciyse**
  kopya; dizi/json **her zaman** derin kopya (tarihsel anlam).
- `aot_persist_escape` (üst düzey bildirim): yalnız geçiciyse kopya.

Eskiden yalnız düz atama bariyerliydi; `g += x` ve struct dizisi yolları
geri sarmadan sonra sarkıyordu. Eski kalıcı değer üzerine yazılınca
**serbest bırakılamıyor** (takma adlar izlenmiyor) — kare başına yeniden
atanan hesaplanmış değer sızar. Kapanışın ortamı (env) yerinde kalıcılaşır — kopyalanmaz, paylaşım korunur
(K129). Ayrıntı, ölçüm ve sınırlar (kapanış,
kareyi aşan yereller): [[Tuzaklar#7f. Bariyer TEK yolda yazılıydı — aynı global'e yazan öbür yollar onu hiç görmedi]].

## ⚠️ Checkpoint disiplini (kritik)
**`arena_restore` ile `arena_drop` aynı geri sarmayı yapar** — ikisi de `aot_arena_rewind_to`'yu çağırır: dizgi arenası geri sarılır, bölgedeki obje/diziler serbest kalır. **Tek fark checkpoint yığınının tepesi** (doğrulandı 2026-11-25, `runtime_bindings.cpp`):

| çağrı | yığın |
|---|---|
| `arena_save()` | `top++` (en fazla **32**, `AOT_ARENA_CHECKPOINT_MAX`); yığın doluysa **yakalanabilir hata** (2026-11-26'ya dek sessizce -1) |
| `arena_restore(wm)` | checkpoint **korunur** (`top = idx + 1`) |
| `arena_drop(wm)` | checkpoint **kaldırılır** (`top = idx`) |

Yani "restore tek başına yetmez" genel olarak doğru değil: **tek save + çok restore geçerli bir desen** (varsayılan `listen()` bunu kullanır). Yetersiz kaldığı tek durum `arena_save`'in tekrar tekrar çalışması: eskiden 33. kayıt -1 dönüyor, `arena_restore(-1)` no-op'tu → **sınırsız sızıntı, hata yok**. [[Memory]]'deki P14 "save + restore" satırının büyümesinin gerçek sebebi bu; "restore serbest bırakmıyor" açıklaması yanlıştı. **2026-11-26'dan beri (RFC C3)** 33. kayıt yakalanabilir bir hata atıyor (`arena_save: arena kontrol noktasi yigini dolu (32) …`); yakalanmazsa süreç `Uncaught Exception` ile 1'le biter — sessiz sızıntı artık gürültülü. Kapı `tests/arena_yigin_dolu.test.tpr` (drop'suz tekrar, iç içe derinlik, dengeli desenler etkilenmez, `@frame` mesajı); hata geri alınınca 2 test kırmızı.

Kural: tekrar çalışan her `arena_save`, `throw` dahil **her** çıkış yolunda bir `arena_drop` ile kapanmalı. `throw` aradaki checkpoint'leri geri almaz (`aot_throw`) — drop, try bloğunun **dışında** olmalı.

**Tercih edilen yol: `@frame func f(...)` (K038, 2026-09-28).** Derleyici girişte `arena_save`, her `return`'de dönüş değerini kalıcılaştırıp `arena_drop`, `throw`'da kalıcılaştır + drop + yeniden fırlat yerleştirir; yığın doluysa yakalanabilir hata verir (sessiz -1 değil). Ölçüm: 20 000 çağrı, tepe RSS 733 MB → 3 MB; çağrı başına ~50 ns. Kapılar `tests/frame.test.tpr`, `frame_istisna.test.tpr`, `frame_hatalari.sh`. `@frame`'in şekeri iç adı `__frame_arena_save`'i çağırır (dolu yığında -1, atmaz) ki kendi hatası **fonksiyonun adını** taşısın (`@frame 'f': …`).

Wings desenleri (`lib/wings.tpr`):

| mod | desen |
|---|---|
| `listen()` | başta tek `arena_save`, her istekten sonra `arena_restore`, drop yok |
| `listen_pool` / `listen_async` (`_wings_serve_connection`) | bağlantı başına save, istek başına restore, bağlantı kapanınca **drop** |
| `listen_evented` | tick başına save/drop + istek başına save/drop (`_wings_serve_one_request`) |

→ [[Memory Leak Fixes]] · [[Wings Serve Modes]]

**Atama kuralı (RFC açık soru 2, kapandı 2026-11-27 — [SPEC §8.1](../SPEC.md)):**
paylaşım varsayılandır, global değişkene atama (`g = x`, `g += x`) derin kopyadır.
Ölçülen tablo (2026-11-27): yerel atama/parametre/dönüş/`push` dizi, json ve kutulu
struct'ı paylaşır; kutusuz struct her yerde kopyalanır; global'e atama (yerel ya da
global kaynaktan) kopyalar; üst düzey **bildirim** `T g = h;` paylaşır (yalnız geçiciyi
kopyalar — `tests/main_yerel.test.tpr` 9. bölüm bunu takma ad olarak sabitler); global
kabın elemanına yazma (`g[i] = x`) kontrol noktası içinde geçiciyse kopyalar, dışında
paylaşır → SPEC'te **tanımsız**. Struct'ın değer/referans davranışının alan tiplerine
bağlı olması v0'ın bilinen tutarsızlığı (SPEC Ek A).

**Bilinen delikler** (kapatılmadı, belgelendi — Bellek Sözleşmesi v0 RFC taslağı, 2026-11-25):
- Üzerine yazılan **eski kalıcı değer serbest bırakılamaz** (takma adlar izlenmiyor: `json x = g; g = yeni; print(x);`) → kare/istek başına yeniden atanan hesaplanmış global sızar. Kural: sıcak döngüde global'i yeniden atama, yerinde değiştir.
- **Async sınırı kapandı (2026-11-26):** checkpoint içinde başlatılan async görevin argümanları ve promise'in değeri artık `aot_persist_escape`'ten geçiyor (yalnız geçiciyse kopya). Önceden yalnız `arc_retain` vardı — geri sarma refcount'a bakmıyor, drop'tan sonra çalışan görev serbest belleği okuyordu (sessizce `0`). Bunun sonucu: checkpoint içinde async'e verilen json **kopya** olarak girer; görevin onda yaptığı değişiklik çağırana görünmez (thread_create ile aynı kural). Kapı: `tests/sarkan_isaretci.sh`, Tuzaklar 7w.
- **Sarkan okumayı görünür kılmak:** `TULPAR_ARENA_ZEHIR=1` geri sarılan arenayı 0xDB ile doldurur, bölge kaplarını karantinaya alır (bellek geri dönmez — yalnız test). Bir programın Kural 2.1'i çiğneyip çiğnemediğini anlamanın yolu bu; ASan göremez.
- Checkpoint'i **aşan yerel** değişkene yazılan geçici değer bariyerden geçmez (`acc = acc + x`) → geri sarmadan sonra sarkan pointer. Kural: böyle bir yerelde geçici değer tutma ya da `persist()` ile kopyala. Bu yüzden döngülere otomatik save/drop eklemek kaçış analizi olmadan güvenli değil.

## 🧵 Çok-thread güvenliği (pool/async invariant'ları, 2026-06-18 audit)
`listen_pool`/`listen_async` worker'ları aynı runtime'ı paylaşır. Kural: per-call scratch = **TLS**, paylaşılan tablo = **mutex VEYA atomic release/acquire**. ARM/aarch64 gerçek target (Apple Silicon) → "x86 TSO'da çalışır" akıl yürütmesi bug'dır. Audit'te bulunan 3 gerçek bug (hepsi kapandı):
- `aot_string_buffer` (toString) non-TLS'di → bozuk SQL → sahte 404. → `thread_local`. → [[Wings Serve Modes]]
- `eh_main`/`eh_cur` (exception context) global'di → pool handler'da `try`/`throw` cross-thread longjmp (crash). → `thread_local`.
- `g_call_cache` plain-store publish'liyordu → ARM'da stale `ptr` → yanlış handler. → `key` `std::atomic` release/acquire.
Zaten güvenli: arena/checkpoint/region/`js_small_buffer`/`g_wings_current_fd` TLS; `g_db_registry`/`g_call_cache` mutex; `_request` LLVM-TLS.

**Thread sınırı kopyalayarak geçilir:** `thread_create` argümanı ve dönüş değeri `aot_persist` ile derin kopyalanır (`aot_thread_create` / `aot_thread_entry`); global yolu bariyerden geçer. Her thread'in arenası ve bölgesi kendine özel.

**Kullanıcı düzeyi senkronizasyon (K040, 2026-09-28):** `mutex_*` yanında `atomic_load/store/add/sub/xchg/cas` (üst düzey `int` global; isteğe bağlı sıra `"relaxed"`/`"acquire"`/`"release"`/`"acq_rel"`/`"seq_cst"`, varsayılan seq_cst) ve `@thread_local int x` (thread başına global). Bellek modeli cümlesi: mutex altındaki yazmalar aynı mutex'i sonra alan thread'e, bir release atomic yazmasından önceki yazmalar o değeri acquire ile okuyan thread'e görünür; korumasız paylaşım tanımsız (güncelleme ve görünürlük kaybı ölçüldü). → [[Concurrency]]

## Dizi temsili: kutulu / kutulanmamış (2026-09-03)

`ObjArray` (64 bayt; `sizeof` ile ölçüldü 2026-09-29, x86_64 Linux —
`elem_bits` eklenince 56'dan büyüdü) iki depodan **tam olarak birini** tutar:
- `items_` — `VMValue*`, 16 bayt/eleman, her tür değer.
- `idata` — kutusuz depo; ne tuttuğunu `elem_bits` söyler:
  - **32 / 64** — tamsayı. Dizi **4 bayt/eleman (i32)** başlar, i32'ye
    sığmayan bir değer yazılınca 8 bayta **genişler** (`aot_arr_widen`) —
    kutulanmaz. Tür `long long*` ama 32-bit dizide bellek i32 dizisi olarak
    okunur.
  - **`ARR_ELEM_F64` (−64)** — ham double (2026-10-01; `float[]` dolgusu).
  - **`ARR_ELEM_STR` (−8)** — `ObjString *` tablosu (2026-10-06; `split`
    sonucu): eleman 16 yerine 8 bayt (wasm32'de 4).
  **Tamsayı depo = `elem_bits > 0`**; tamsayı olmayan bütün işaretler
  NEGATİF. Eskiden tamsayı sınavı `elem_bits != ARR_ELEM_F64` idi ve dizgi
  deposunu tamsayı sanırdı ([[Tuzaklar]] 7q). Kutusuz depodan okuma TEK
  yerden: `arr_unboxed_get` / `arr_get` (vm.hpp) — depoyu değiştirmez, yeni
  bir işaret oraya ve `arr_debox`'a eklenir. Negatif işaretli depo, işaretin
  türünden OLMAYAN bir değer yazılınca kutuya çevrilir.

Diğeri **NULL**. Bu bilinçli: kutulanmamış bir dizide `items_` NULL olduğu için
atlanan her yol sessizce bozulmak yerine **gürültüyle patlar**.

Genel amaçlı her yol `arr_items(a)` üzerinden geçer; o da gerekiyorsa diziyi
**tek seferde** kutuya çevirir (`arr_debox`) ve öyle bırakır — yani dönüşüm
eleman başına tek yazma ve yalnız bir kez. Yalnız codegen'in sıcak yolu
`idata`ya doğrudan bakar. `arr_debox` **kapasiteyi korur**: büyüme kodu
`old_capacity`'yi önceden okumuş olabilir.

Üreticiler: `array_fill(n, <int>)` ve `push(<int>)` (tamsayı),
`array_fill(n, <float>)` (double), `split` (dizgi; `TULPAR_NO_STRARR=1` kapatır).
İşaretin türünden olmayan yazım diziyi kutuya döndürür (codegen bu durumda
yavaş yola dalar, orası `arr_items` çağırır) — anlam birebir korunur. Yalnız
OKUYAN yollar (print/toString, json, `join`, `contains`, `indexOf`, `at`,
`pop`, `remove_at`, kalıcı kopya) kutuya çevirmez.

Serbest bırakma: `idata` da bırakılmalı ve bu, `items_` denetiminin **dışında**
olmalı (kutulanmamış dizide `items_` NULL'dır). Üç yol: `region_free_one`,
`freeObject`, `arc_free_array` — üçü de `!arena_allocated` korumalı, yani
`idata` `items_`'in ayırıcısını aynalar.

Alan eklerken düşülen tuzak: [[Tuzaklar#6d]]. TBAA'nın neden testle
doğrulanamadığı: [[Tuzaklar#6e]].

## Görünürlük: `mem_stats()` (RFC C4, 2026-11-26)
Sızıntı RSS grafiğinden değil programdan görünsün diye. Dönen json'un alanları (arena/bölge/checkpoint/sayaçlar **bu iplik**, `heap_in_use` süreç):

| alan | ne | hangi deliği gösterir |
|---|---|---|
| `arena_used` | arenada kullanımdaki bayt (geri sarma düşürür) | checkpoint'siz döngü (D2) |
| `arena_reserved`, `arena_blocks` | arenanın ayırdığı bloklar — OS'a hiç geri verilmez | tepe sonrası RSS (D6) |
| `region_objects` | bölgede izlenen geçici obje/dizi | — (sonraki geri sarmada boşalır) |
| `checkpoint_depth`, `checkpoint_max` | açık `arena_save` sayısı / 32 | drop'suz save birikimi |
| `untracked_objects` | checkpoint **açık değilken** kurulan obje/dizi başlıkları (kalıcı) | D2 |
| `persist_bytes` | `persist()` / yazma bariyerinin kalıcıya kopyaladığı bayt (`aot_persist`'in her ayırması) | yeniden atanan global (D1) |
| `heap_in_use` | ayırıcının kullanımdaki baytı (glibc ≥ 2.33 `mallinfo2` — zayıf sembol, eski glibc'de link düşmez; macOS `malloc_zone_statistics`; Android/web `mallinfo`); bilinmiyorsa **-1** (Windows, musl/Alpine) | toplam |

Sınır: `untracked_objects` **başlık sayısı**, bayt değil — büyüyen `items_`/`values` dizileri sayılmaz (onları saymak her büyüme yolunu enstrümanlamak demek). Dönen nesne sıradan bir json: checkpoint içinde çağrıldıysa geçici, drop'tan sonra okunmaz (Kural 2.1 — testin ilk yazımı tam bu yüzden `null` okudu; sayıları drop'tan önce `int`'e al). Kapı `tests/mem_stats.test.tpr` (5 test); sayaçlar sökülünce D1/D2 testleri kırmızı.

## Faydalı builtin'ler
`persist(v)` — değeri kalıcı belleğe derin kopyalar. `mem_stats()` — yukarıdaki sayaçlar. `arena_save/restore/drop` — codegen'de isimle tanınır. `@frame` — fonksiyon kapsamında güvenli save/drop (yukarıda). `@no_alloc` — gövdenin ayırmadığını derleme zamanında denetler (K041).

## İlgili
[[Runtime]] · [[Memory Leak Fixes]] · [[Wings Serve Modes]] · [[Performance]]
