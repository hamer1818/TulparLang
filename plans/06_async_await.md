# Plan 06 — `async` / `await` keywords (state-machine transform)

**Durum:** PROPOSED
**Tahmin:** 4-6 PR
**Risk:** Yüksek — state machine transform AOT'da yeni bir lowering;
ABI + heap allocation kararları zincirde 5+ noktayı etkiler
**Mottoya katkı:** Python kolay (sync-look-alike async code)

## Hedef

Kullanıcı şunu yazabilsin:

```tpr
async func fetch_user(int id): json {
    json a = await http_get("https://api/users/" + toString(id));
    json b = await http_get("https://api/users/" + toString(id) + "/posts");
    return {"user": a, "posts": b};
}

// Üst seviye fetch — wings handler'ı bir co-routine yürütücüye eklenir
json u = await fetch_user(42);
```

İki `await http_get` paralel network I/O yapsa da, gövde **sync gibi
okunur**. Derleyici fonksiyonu bir state machine'e çevirir: her
`await` noktası bir state, yeniden giriş yapıldığında kaldığı yerden
devam eder. Stack frame heap-allocated bir "frame" struct'ına serpilir.

Bu Python `async def`, JS `async function`, Rust `async fn` ile aynı
felsefe — co-routine fundamentals + compiler-generated state machine.

## Mevcut durum

- `lib/async.tpr`'da **library-seviyesi** await taklidi var: çağrılar
  callback / thread-spawn ile sıralı çalıştırılıyor ama dil-seviyesi
  desugaring yok.
- `listen_async` Wings listener thread-per-connection model'i —
  cooperative scheduling yok, her bağlantı OS thread'i tüketir.
- `lib/wings.tpr` evented variant (`listen_evented`) poll()-multiplex
  ediyor ama handler'ları C tarzı state machine ile yazamıyor.

## Tasarım kararları (RFC)

### Syntax

```tpr
async func name(args): RetType { ... }     // funkcsiyon işaretleme
await expr                                 // suspension point
```

- `async` keyword fonksiyon başında zorunlu (sadece içeride `await`
  varsa). `async` olmadan `await` parse hatası.
- `await` bir unary postfix operator (Rust tarzı `expr.await`) yerine
  prefix keyword (Python/JS tarzı). Daha tanıdık.
- `async fn → Future<RetType>` (Rust) gibi tip değişikliği yapılır mı?
  RFC: hayır, **bağlam-duyarlı** — `await fetch()` `RetType` döner,
  bir `async` fonksiyon dışında çağırırsa sync fallback (block).

### Runtime modeli

İki yol var:

**A. Tek-thread cooperative scheduler** (Node.js / Python asyncio):
- Tek event loop, `epoll`/`IOCP`/`kqueue`.
- I/O wait noktasında scheduler başka coroutine'i çalıştırır.
- Avantaj: deterministik, lock-free, lokal state safe.
- Dezavantaj: CPU-bound handler tüm event loop'u tutar.

**B. Multi-thread work-stealing** (Tokio / Go):
- N worker thread, runnable coroutines arasında load balance.
- Avantaj: CPU paralleliği bedava.
- Dezavantaj: shared state için lock disiplini, lokal-thread varsayımı
  kırılır.

**RFC önerisi:** MVP **A** (tek-thread). PR 5+ `--threads N` ile
B'ye geçiş. Wings `listen_evented` zaten poll-based; async runtime'ı
onun üstüne kurmak doğal.

### State machine lowering (AOT)

Her `async` fonksiyon için derleyici:

1. Lokal değişkenleri bir `Frame` struct'ına spille eder.
2. Fonksiyon gövdesini `await` noktalarında bölüp her segmenti
   ayrı bir basic block / sub-function yapar.
3. `Frame.state` field'ı switch dispatch ile bir sonraki segmente
   atlar.
4. `await expr` lowering: `expr` bir `Future` döner; Future'ın
   `poll()` metodunu çağır, "pending" dönerse frame'i scheduler'a
   geri ver, sonra yeniden giriş yapıldığında value oku.

Bu LLVM coroutine intrinsics (`@llvm.coro.*`) ile yapılabilir — Rust
async tam olarak bunu kullanıyor. Alternatif: kendi state machine
codegen'imiz, daha kontrol ama daha çok iş.

### Heap allocation

Frame struct'ı her async çağrıda bir allocation. Arena allocator
(`aot_arena_alloc`) zaten thread-local; coroutine frame'leri orada
yaşar, request bittiğinde otomatik reclaim.

### Cancellation

Wings handler timeout veya client disconnect → çalışmakta olan
coroutine cancel edilmeli. MVP: kooperatif cancellation (her `await`
noktasında flag check). PR 6: structured cancellation
(Trio / Kotlin Job iptal).

## Adımlar

### PR 1 — Lexer + parser

- `lexer/`: `async`, `await` keyword'leri.
- `parser/`: `async func` ve `await expr` AST node'ları.
- Test: parse-only `tests/typeinfer/pass/async_parse.tpr`.

### PR 2 — Typeinfer

- `async` fonksiyonun return tipi `Future<T>` olarak işaretlenir
  (internal — kullanıcı `T` yazmaya devam eder).
- `await expr` zorunluluğu: `expr` `Future<T>` olmalı, sonuç `T`.
- `await` dışında `async` çağrısı = `Future<T>`.

### PR 3 — Runtime (event loop + scheduler)

- `runtime/tulpar_async.cpp` (yeni dosya).
- Mini event loop: poll() / epoll / kqueue / IOCP üstüne thin layer.
- `Future` struct: state + result slot + waker callback.
- `tulpar_io_read_async`, `tulpar_io_write_async`, `tulpar_sleep_async`
  primitives.

### PR 4 — AOT state machine lowering

- LLVM `@llvm.coro.*` intrinsics + lowering passes.
- Frame allocation arena-backed.
- `await` → poll + suspend + resume.
- Test: `examples/async_fetch.tpr` AOT compile + run end-to-end.

### PR 5 — Wings handler entegrasyonu

- `async func handler(req): json` → wings dispatcher coroutine olarak
  yürütür, response başka bir handler ile interleave eder.
- `lib/wings.tpr`: `listen_async_real()` veya `listen_evented`'a
  coroutine awareness.

### PR 6 — Cancellation + timeout

- `await with_timeout(fetch(), 5)` benzeri primitives.
- Wings handler timeout entegrasyonu.

## Açık sorular

1. LLVM coroutine intrinsics vs kendi codegen. Önerim: intrinsics
   (test edilmiş, daha az iş).
2. Single-thread vs multi-thread runtime. MVP single, sonra opt-in.
3. `await` keyword postfix mi prefix mi? RFC prefix önerir.
4. Implicit `async` (her fonksiyon Future dönen)? Hayır, explicit.
5. `Future` user-facing mi internal mi? RFC internal (Python gibi).

## Risk değerlendirmesi

- **Yüksek:** LLVM coroutine lowering subtle — frame layout
  hataları segfault üretir, debugger yok (Plan 07 ön-koşul).
- **Yüksek:** Wings handler'larında shared state yarış durumu.
  `_request` zaten TLS olmalı (STATUS açık eksik).
- **Orta:** Stack trace okunaksızlığı — async stack'ler frame
  chain'ine bakılarak rebuild edilmeli; sonraki faz.
- **Düşük:** Performans — coroutine frame allocation ölçülmeli;
  arena-backed olduğu için per-coro malloc yok.

## İlgili işler

- Plan 07 (Debugger MVP) — async stack'lerin debug deneyimi için
  şart.
- STATUS açık: LLVM thread-local globals (`_request`/`_response`) —
  cooperative scheduler altında bile gerekli.
- `lib/wings.tpr` `listen_evented` — async runtime'ı buradan kurar.
