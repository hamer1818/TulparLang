---
tags: [runtime, memory, important]
---

# Memory Model — Arena + Region + Checkpoint

AOT'ta GC yok. Bellek üç mekanizmayla yönetilir (`src/vm/runtime_bindings.cpp`).

## 1) Arena (string'ler)
Per-thread blok allocator. `arena_save()` → checkpoint handle; `arena_restore(wm)` → o noktadan sonrasını geri sarar (blokları free etmez, `used=0` yapar → O(1) reuse). Wings her istek sonrası restore ile belleği sabit tutar.

## 2) Per-request malloc region (obje/dizi)
AOT'ta her obje/dizi literali `malloc`'lanır (`arena_allocated=0`); GC sweep yok → eskiden **istek-başına sızıntı**. Çözüm: literaller `g_region` (thread_local) + `g_region_set`'e izlenir, arena_save/restore bunları bracket'ler. Yalnız arena scope içinde (`g_arena_checkpoint_top > 0`) allocate edilenler izlenir; top-level global'ler ve `persist()`/`string_pin` kopyaları izlenmez (kalıcı). → [[Memory Leak Fixes]]

## 3) Runtime write barrier
`wb_persist_escape(container, v)`: transient bir değer **kalıcı** bir container'a (global / zaten-persist'lenmiş obje) yazılırsa derin kopyalanır (`aot_persist`). Değer-akışı tabanlı; alias'lar üzerinden de yakalar. Wings'te global'e yazma otomatik kalıcılaşır ([[Wings]] auto-persist).

## ⚠️ Checkpoint disiplini (kritik)
Stack **32 slot** (`AOT_ARENA_CHECKPOINT_MAX`). `arena_restore` checkpoint'i **korur** (`top=idx+1`) — tek-save-çok-restore döngüsü (varsayılan `listen()`) buna güvenir. Ama per-connection/per-request `arena_save` yapan kalıcı thread'ler (pool/evented) serbest bırakmazsa 32'den sonra `arena_save → -1`, restore no-op → **sınırsız sızıntı**. Çözüm: **`arena_drop(wm)`** = rewind + POP (`top=idx`). → [[Memory Leak Fixes]] · [[Wings Serve Modes]]

## 🧵 Çok-thread güvenliği (pool/async invariant'ları, 2026-06-18 audit)
`listen_pool`/`listen_async` worker'ları aynı runtime'ı paylaşır. Kural: per-call scratch = **TLS**, paylaşılan tablo = **mutex VEYA atomic release/acquire**. ARM/aarch64 gerçek target (Apple Silicon) → "x86 TSO'da çalışır" akıl yürütmesi bug'dır. Audit'te bulunan 3 gerçek bug (hepsi kapandı):
- `aot_string_buffer` (toString) non-TLS'di → bozuk SQL → sahte 404. → `thread_local`. → [[Wings Serve Modes]]
- `eh_main`/`eh_cur` (exception context) global'di → pool handler'da `try`/`throw` cross-thread longjmp (crash). → `thread_local`.
- `g_call_cache` plain-store publish'liyordu → ARM'da stale `ptr` → yanlış handler. → `key` `std::atomic` release/acquire.
Zaten güvenli: arena/checkpoint/region/`js_small_buffer`/`g_wings_current_fd` TLS; `g_db_registry`/`g_call_cache` mutex; `_request` LLVM-TLS.

## Dizi temsili: kutulu / kutulanmamış (2026-09-03)

`ObjArray` (56 bayt) iki depodan **tam olarak birini** tutar:
- `items_` — `VMValue*`, 16 bayt/eleman, her tür değer.
- `idata` — `long long*`, 8 bayt/eleman, yalnız int.

Diğeri **NULL**. Bu bilinçli: kutulanmamış bir dizide `items_` NULL olduğu için
atlanan her yol sessizce bozulmak yerine **gürültüyle patlar**.

Genel amaçlı her yol `arr_items(a)` üzerinden geçer; o da gerekiyorsa diziyi
**tek seferde** kutuya çevirir (`arr_debox`) ve öyle bırakır — yani dönüşüm
eleman başına tek yazma ve yalnız bir kez. Yalnız codegen'in sıcak yolu
`idata`ya doğrudan bakar. `arr_debox` **kapasiteyi korur**: büyüme kodu
`old_capacity`'yi önceden okumuş olabilir.

Üreticiler: `array_fill(n, <int>)` ve `push(<int>)`. Float/bool/nesne yazımı
diziyi kutuya döndürür (codegen bu durumda yavaş yola dalar, orası `arr_items`
çağırır) — anlam birebir korunur.

Serbest bırakma: `idata` da bırakılmalı ve bu, `items_` denetiminin **dışında**
olmalı (kutulanmamış dizide `items_` NULL'dır). Üç yol: `region_free_one`,
`freeObject`, `arc_free_array` — üçü de `!arena_allocated` korumalı, yani
`idata` `items_`'in ayırıcısını aynalar.

Alan eklerken düşülen tuzak: [[Tuzaklar#6d]]. TBAA'nın neden testle
doğrulanamadığı: [[Tuzaklar#6e]].

## Faydalı builtin'ler
`persist(v)` — değeri kalıcı belleğe derin kopyalar. `arena_save/restore/drop` — codegen'de isimle tanınır.

## İlgili
[[Runtime]] · [[Memory Leak Fixes]] · [[Wings Serve Modes]] · [[Performance]]
