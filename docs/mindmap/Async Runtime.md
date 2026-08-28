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

## Testler
`tests/async.test.tpr`, `examples/34_async.tpr`, `35_gather.tpr`, `37_async_http.tpr`.

## İlgili
[[Runtime]] · [[HTTP Client]] · [[Wings Serve Modes]]
