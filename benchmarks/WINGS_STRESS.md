# Wings stress test — maximum load & latency under load

**Date:** 2026-06-18 · **Host:** WSL2, 14 logical CPU, 15.8 GB RAM ·
**Build:** AOT/LLVM Release · **Server:** `benchmarks/stress_server.tpr`

Tooling (this directory):
- `loadtest.c` — multi-threaded keep-alive/close HTTP load generator with a
  1µs-bucket latency histogram (`gcc -O2 -pthread -o loadtest loadtest.c`).
- `run_stress.sh <serve|pool|evented>` — launches the server, sweeps
  concurrency, reports RPS + latency percentiles + server RSS.
- `stress_server.tpr` — representative app: `/ping` (tiny), `/users` (small
  JSON), `/users/:id` (param route), `POST /users` (body+validation),
  `/big` (50-item JSON).

Wings is **thread-per-connection**: `serve()`/`listen()` serve one connection
at a time; `listen_pool` runs one accept-worker per CPU; `listen_evented`
multiplexes all connections on a single `poll()` thread. So a measurement has
two regimes: **keep-alive** (persistent connections — peak per-connection
throughput) and **Connection: close** (many short-lived clients).

## Node.js karşılaştırması (2026-09-28)

`benchmarks/http_vs_node.sh` — aynı iş (`/ping` "pong", `/users` 3 kayıtlı
JSON), aynı ölçüm aracı (`loadtest.c`, keep-alive, 50 bağlantı, 5 sn × 3
tur, ORTANCA), iki sunucu turla eşli ölçülüyor ve cevap gövdeleri önce
karşılaştırılıyor. İstemci aynı makinede, çekirdekleri paylaşıyor — mutlak
RPS makineye bağlı, oran aynı koşula bakıyor.

| Wings | Node | `/ping` | `/users` |
|---|---|---:|---:|
| `listen_evented` (tek iş parçacığı) | `http` tek süreç | 274 919 / 207 622 = **1,32×** | 265 814 / 205 815 = **1,29×** |
| `listen_pool` (CPU başına işçi) | `cluster` (CPU başına süreç) | 1 013 745 / 1 220 873 = **0,83×** | 1 004 537 / 1 246 261 = **0,81×** |

AMD Ryzen 7 9800X3D (16 iş parçacığı), Linux, node v26.10.0, tulpar main
(2026-09-28). STATUS'taki eski "Node'un 1.7-2.1 katı" bu ölçümle
üretilemedi; olgunluk kriteri 2'nin "HTTP Node'u 2×+ geçiyor" ayağı
karşılanmıyor.

**Ölçüm tuzağı (betikte kapatıldı):** ilk koşumda Node'un bütün "RPS"i
hataydı — iki ayrı sebeple: arka planda `node` stdin bir TTY olduğu için
SIGTTIN ile DURDU; ve `writeHead` + `end` Node'u chunked kodlamaya
geçiriyor, `loadtest.c` ise Content-Length ile çerçeveliyor. Betik artık
`loadtest`'in `other`/`err` sayaçları sıfır değilse sayı yerine `HATALI`
basıyor (oranı `GECERSIZ`), sunucuların stdin'i `/dev/null`, Node açıkça
Content-Length yazıyor, ve ölçümden önce iki gövde karşılaştırılıyor.

## Headline numbers

| Mode | keep-alive peak `/ping` | p50 / p99 | close `/ping` (200 conn) | peak RSS |
|------|------------------------:|:---------:|-------------------------:|---------:|
| `serve` (default, 1 conn) | **4,164** RPS | 230µs / 379µs | 18,314 RPS | **6.9 MB** |
| `listen_pool` (14 workers) | **39,800** RPS | 360µs / 671µs | 25,969 RPS | **8.4 MB** |
| `listen_evented` (1 thread) | **57,811** RPS | 801µs / 1.7ms | 20,312 RPS | **7.0 MB** |

- **Throughput ceiling ≈ 58k RPS** (`evented`, light handler, 50 keep-alive
  clients) — single thread, poll-multiplexed, no per-connection thread cost.
- **`pool` scales linearly to core count**: 4.1k→7k→16k→27k→38k RPS at
  conc 1→2→4→8→14, then plateaus (~39–40k) — all 14 cores saturated. Best p99
  consistency and best for **CPU-bound** handlers (`/big`: pool 32.8k vs
  evented 30.4k, because pool parallelizes the JSON build across cores).
- **`serve`** is the simple default: one in-flight connection, ~4k RPS, 230µs
  p50 — fine for dev and low-traffic, not for concurrent load.
- **Latency under load stays sub-millisecond** for serve/pool keep-alive;
  per-request dispatch is **4–34µs** (see the access log). Even at 500
  concurrent short-lived connections the server never errored (`err=0`).

## RAM: flat under all load (the important part)

Peak RSS stayed **6.9–8.4 MB** across every mode, every concurrency level, and
500k+ requests. No mode grows with traffic.

## Critical bug found & fixed: arena-checkpoint exhaustion leak

The stress test surfaced a **severe memory leak under connection churn**. In
`Connection: close` mode, `pool` RSS exploded **7.5 MB → 1.25 → 2.8 → 4.35 →
9.1 GB** and never recovered; `evented` crept ~160 B/request.

**Root cause** (`src/vm/runtime_bindings.cpp`): the per-thread arena checkpoint
stack is capped at 32 (`AOT_ARENA_CHECKPOINT_MAX`). `arena_restore(wm)` keeps
the checkpoint (`top = idx+1`) so the `listen()` loop can roll back to the same
handle every request. But `listen_pool` (save per *connection*) and
`listen_evented` (save per *request*) call `arena_save()` repeatedly in a
**persistent** thread and never release the checkpoint. After 32 connections/
requests, `arena_save()` returns `-1` without saving and every subsequent
`arena_restore(-1)` no-ops → the per-request arena/region is never reclaimed →
unbounded leak.

**Fix:**
- New runtime primitive **`arena_drop(handle)`** = rewind **and** pop the
  checkpoint (`top = idx`), the scope-exit counterpart of `arena_save`. So a
  function that does `wm = arena_save(); …; arena_drop(wm)` is balanced and the
  stack no longer grows. (`aot_arena_drop`, wired through `llvm_backend`.)
- `_wings_serve_connection` (pool/async) drops its checkpoint when the
  connection ends; `_wings_serve_one_request` (evented) drops on every exit.
- `listen_evented` additionally wraps each `poll()` tick in an arena scope and
  commits surviving fds into the one **persistent** `fds` array **in place**
  (clear + refill) instead of reassigning it to a transient array — so the
  per-tick `ready`/`next_fds`/`closed` garbage is reclaimed each iteration.

**After the fix:** identical workloads are flat — `pool` close-mode held
**8.4 MB across 481k requests** (20s soak, sampled every 4s: 8408 KB the whole
time); `evented` held **6.9 MB across 293k requests** (was 19→66 MB). Default
`serve()`/`listen()` was never affected (it saves once outside the accept loop).

## Recommendations

- **Default / low traffic:** `serve()` — simplest, 6.9 MB, sub-ms latency.
- **CPU-bound handlers / max consistency:** `listen_pool(port)` — scales to
  cores, tight p99.
- **Many concurrent keep-alive clients, light handlers:** `listen_evented` —
  highest raw RPS on one thread.

Reproduce: `cd benchmarks && gcc -O2 -pthread -o loadtest loadtest.c &&
./run_stress.sh pool` (or `serve` / `evented`).

---

# DB-backed load test (the real bottleneck)

The in-memory numbers above measure the HTTP layer. Real APIs hit a database,
so `stress_db_server.tpr` adds a **SQLite** backend: 1000-row `users` table,
one **global** `db_open` handle shared by all workers (exactly what
`lib/orm.tpr` does with `_orm_db`). SQLite's amalgamation default is
`THREADSAFE=1` (serialized) — the shared handle is safe, but every query
serializes through SQLite's internal mutex.

| Mode / journal | read by PK (c14 / c50) | list 50 (c14) | write INSERT (c14 / c50) | RSS |
|----------------|:----------------------:|:-------------:|:------------------------:|----:|
| `pool` (14w), rollback | 23.8k / 23.7k | 14.0k | **8.8k** / 8.5k | 12 MB |
| `pool` (14w), **WAL**  | 25.8k / 25.5k | 14.0k | **20.4k** / 20.5k | 12 MB |
| `evented` (1 thread)   | **29.4k / 32.3k** | **23.3k** | 15.8k / 14.5k | 11 MB |

**Findings:**
1. **Reads don't scale with pool workers.** 23.8k RPS flat from 14→50
   connections — the shared handle's mutex serializes every query, so extra
   worker threads add *contention*, not throughput.
2. **`evented` (single thread) beats `pool` (14 threads) on DB reads**
   (29–32k vs 23.8k) precisely *because* there's no inter-thread mutex
   contention. Counter-intuitive but it's the shared-handle serialization
   showing through. For DB-bound endpoints, fewer threads can be faster.
3. **WAL is a big win for writes**: rollback 8.8k → **WAL 20.4k RPS** (2.3×),
   and tighter p99 (5.1ms → 2.2ms). **As of 2026-06-18 `db_open` enables WAL +
   `synchronous=NORMAL` + `busy_timeout=5000` by default** for file-backed DBs
   (opt out with `TULPAR_DB_NO_WAL=1`), so this win is now on out of the box.
4. Even DB-bound, Wings does **24–32k reads / 9–20k writes per second** —
   comfortably above a typical Python+ORM stack (~1–5k). RSS stayed 11–12 MB
   (includes SQLite page cache); no leak in the DB path.

**The real ceiling is SQLite shared-handle serialization, not the HTTP layer.**
`db_open` now sets `busy_timeout` + WAL by default (above), which lifts writes.
The remaining highest-leverage step for DB *reads* is a **handle per
thread/connection** so independent connections read in parallel under WAL —
Tulpar's `db_open` API doesn't expose per-thread handles yet.

Reproduce: `WINGS_MODE=pool ./tulpar benchmarks/stress_db_server.tpr`
then `benchmarks/loadtest 127.0.0.1 8585 14 5 GET /users/500` (writes:
`… 14 5 POST /users '{"name":"X"}'`). `TULPAR_DB_NO_WAL=1` reverts to rollback.

> Resolved 2026-06-18: `db_last_insert_id`/`db_error` were declared in codegen
> with the by-pointer db type while their runtime impls take a VMValue by value,
> so every DB program failed LLVM module verification with a benign "Call
> parameter type does not match function signature" (output was still correct).
> Fixed by declaring them with the by-value signature.
