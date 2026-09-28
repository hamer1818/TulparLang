---
tags: [component, runtime, async]
---

# Async Runtime

`runtime/tulpar_async.cpp/.h` — tek-thread, **cooperative event loop**, stackful coroutine'lerle. Yalnız AOT yolunda yaşar (VM yok).

## Model
`async func` hemen bir **promise** döner; `await` o promise settle olana kadar coroutine'i askıya alır, bu arada başka coroutine'ler çalışır. JS / Python asyncio modeli — OS thread kilitleri olmadan eşzamanlılık.

## Bilinmesi gerekenler
- Stackful coroutine, üç bağlam geçişi arka ucu (2026-09-28, K233): **x86_64 / AArch64 (Linux, macOS, Android) el yazımı geçiş** (`tulpar_ctx_swap`: yalnız callee-saved yazmaçlar + yığın işaretçisi; x86_64'te MXCSR/x87 CW de), Windows fiber'ları, diğer POSIX'te `ucontext`. `<thread>` bağımlılığı MinGW win32-threads build için düşürüldü.
- **Neden el yazımı:** (1) bionic `makecontext`/`swapcontext`'i kaldırdı — async Android'de hiç yoktu, artık var (iki ABI de bu yolda; `android_stubs.cpp` kaldırıldı). (2) glibc `swapcontext` her geçişte sinyal maskesini kaydedip yüklüyor (`rt_sigprocmask` sistem çağrısı). Ölçü (Ryzen 7 9800X3D, 2026-09-28, turla eşlenmiş 11 tur): spawn+await **356 → 118 ns**, 4'lü `gather` **4578 → 1230 ns**.
- **AddressSanitizer:** el yazımı geçişi ASan yakalayamaz; `__sanitizer_start/finish_switch_fiber` bildirimleri ASan derlemesinde açık (async paketi ASan altında 21/21 temiz). **Web (2026-09-28):** wasm'da `emscripten_fiber_*` arka ucu (coroutine başına C yığını + 128 KB ASYNCIFY tamponu; ASYNCIFY web linkinde zaten açık). `aot_event_loop_run` `runtime_bindings.cpp`'de, gövde zamanlayıcı ilk kullanımda kaydoluyor — async kullanmayan ikiliye `tulpar_async.o` girmiyor (girseydi web oyunu +38 KB wasm / +15,6 KB js). Web'de bilinen sınır: `try/catch` linklenmiyor (`setjmp`, async'ten bağımsız).
- `gather()` — birden çok promise'i paralel bekler.
- `sleep_async`, non-blocking HTTP client ([[HTTP Client]]) bu loop üzerinde.
- **Threads (`thread_create`) ≠ async:** thread'ler gerçek paralel OS thread'leri (pool worker'lar bunu kullanır → [[Wings Serve Modes]]); async tek-thread kooperatif.
- Async tamamlama (`aot_*` settle) **main thread'de** çalışmalı — VM obje/string allocate ediyor, worker thread'den güvenli değil.
- **Zamanlayıcı THREAD BAŞINA (2026-09-27, K265 (2)):** hazır kuyruğu, zamanlayıcılar, G/Ç kaynakları ve zamanlayıcı bağlamı (`g_main_ctx` / `g_main_fiber`) `thread_local`. Eskiden süreç-global'di: iki thread aynı anda async kod koşturunca (`thread_create` içinde `await`, `listen_pool` işçisinde async handler) biri ötekinin bağlamına `swapcontext` ile dönüyordu — ölçüldü: 4 thread × 50 tur `gather`, üç koşumda üç çökme. Her thread kendi olay döngüsünü koşar; bir thread'de oluşan promise başka thread'de **beklenemez**. `tests/async.test.tpr` "async in 4 threads". Maliyet ölçüldü, yok: spawn+await 355,3 → 355,1 ns, 4'lü gather 4569 → 4524 ns (turla eşlenmiş 9 tur).
- **Zaman aşımı + iptal (K112, 2026-09-28):** `await with_timeout(p, ms)` — `ms` içinde yerine gelmezse `"zaman asimi / timeout"` ile reddedilir ve `p`'nin işi **iptal edilir** (asyncio.wait_for gibi). `cancel(p)` → bool: kooperatif, tek atış — görev bir sonraki `await`'inde `"iptal edildi / cancelled"` fırlatır (yakalanabilir); park etmiş görev hemen uyandırılır, hiç başlamamışsa gövdesi hiç koşmaz; `gather` iptali çocuklarını da iptal eder; görevsiz promise (sleep, HTTP) doğrudan reddedilir. Mekanizma: `ObjPromise.task` (üreten görev), `Task.waiting_on` (bekleme listesinden çıkarmak için), `with_timeout` için yığınsız **bağ görevi** (kaynağın bekleyen listesinde, `resume()` bağlam değiştirmeden sonucu aktarır) + `kind 1` zamanlayıcı. Program sonu boşaltması bekleyeni olmayan sleep zamanlayıcısını beklemiyor (iptal edilen görevin `sleep_async(1000)`'i çıkışı 1 sn geciktiriyordu).
- **Yığın havuzu (2026-09-28):** biten coroutine'in 256 KB yığını thread başına havuza (≤32) döner. 4'lü `gather` 4560 → 3463 ns; spawn+await ~360 ns değişmedi (orada zaman `swapcontext`'in sinyal maskesi sistem çağrılarında).
- **Açık (K265 (1)):** Wings dağıtıcısı async handler'ı hâlâ çağıran thread'de sonuna kadar bloklayarak koşturuyor (`call()` → `aot_await` döngüyü sürer); `listen_evented`'te istekler arası interleave yok.

## Testler
`tests/async.test.tpr`, `examples/34_async.tpr`, `35_gather.tpr`, `37_async_http.tpr`.

## İlgili
[[Runtime]] · [[HTTP Client]] · [[Wings Serve Modes]]
