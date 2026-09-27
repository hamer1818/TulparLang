---
tags: [component, runtime, async]
---

# Async Runtime

`runtime/tulpar_async.cpp/.h` — tek-thread, **cooperative event loop**, stackful coroutine'lerle. Yalnız AOT yolunda yaşar (VM yok).

## Model
`async func` hemen bir **promise** döner; `await` o promise settle olana kadar coroutine'i askıya alır, bu arada başka coroutine'ler çalışır. JS / Python asyncio modeli — OS thread kilitleri olmadan eşzamanlılık.

## Bilinmesi gerekenler
- Cross-platform stackful coroutine: macOS `ucontext`, Windows fiber'ları, Linux ucontext. `<thread>` bağımlılığı MinGW win32-threads build için düşürüldü.
- `gather()` — birden çok promise'i paralel bekler.
- `sleep_async`, non-blocking HTTP client ([[HTTP Client]]) bu loop üzerinde.
- **Threads (`thread_create`) ≠ async:** thread'ler gerçek paralel OS thread'leri (pool worker'lar bunu kullanır → [[Wings Serve Modes]]); async tek-thread kooperatif.
- Async tamamlama (`aot_*` settle) **main thread'de** çalışmalı — VM obje/string allocate ediyor, worker thread'den güvenli değil.
- **Zamanlayıcı THREAD BAŞINA (2026-09-27, K265 (2)):** hazır kuyruğu, zamanlayıcılar, G/Ç kaynakları ve zamanlayıcı bağlamı (`g_main_ctx` / `g_main_fiber`) `thread_local`. Eskiden süreç-global'di: iki thread aynı anda async kod koşturunca (`thread_create` içinde `await`, `listen_pool` işçisinde async handler) biri ötekinin bağlamına `swapcontext` ile dönüyordu — ölçüldü: 4 thread × 50 tur `gather`, üç koşumda üç çökme. Her thread kendi olay döngüsünü koşar; bir thread'de oluşan promise başka thread'de **beklenemez**. `tests/async.test.tpr` "async in 4 threads". Maliyet ölçüldü, yok: spawn+await 355,3 → 355,1 ns, 4'lü gather 4569 → 4524 ns (turla eşlenmiş 9 tur).
- **Açık (K265 (1)):** Wings dağıtıcısı async handler'ı hâlâ çağıran thread'de sonuna kadar bloklayarak koşturuyor (`call()` → `aot_await` döngüyü sürer); `listen_evented`'te istekler arası interleave yok.
- Coroutine başına 256 KB yığın `malloc` + `makecontext` (spawn+await ~355 ns); yığın havuzu ölçülmedi.

## Testler
`tests/async.test.tpr`, `examples/34_async.tpr`, `35_gather.tpr`, `37_async_http.tpr`.

## İlgili
[[Runtime]] · [[HTTP Client]] · [[Wings Serve Modes]]
