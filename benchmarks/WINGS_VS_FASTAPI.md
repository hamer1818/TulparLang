# Wings (Tulpar AOT) vs FastAPI (uvicorn) — CRUD benchmark

Same Users CRUD API on both sides:
- Wings: [tests/compare_wings_users_api.tpr](../tests/compare_wings_users_api.tpr)
- FastAPI: [tests/compare_fastapi_users_api.py](../tests/compare_fastapi_users_api.py)

Reproduce: `python3 benchmarks/wings_vs_fastapi.py` (needs a venv with
`fastapi`+`uvicorn`; pass it via `--fastapi-python`).

## Setup

- Box: 14 vCPU WSL2 Linux. Wings `serve()` (single-threaded **serial** accept
  loop). FastAPI on uvicorn, default (1 async worker).
- Load: dependency-free Python client (multi-process + threads, keepalive),
  `GET /users/1`. **No native load tool (wrk/hey) was available**, so the
  client itself is a soft RPS ceiling — treat absolute RPS as a lower bound,
  latency as the reliable signal.

## Results

| Metric | Wings (Tulpar AOT) | FastAPI (uvicorn) | Notes |
|---|---|---|---|
| RPS (`GET /users/1`) | **3151** | 2248 | both client-limited; Wings serial ceiling |
| p50 latency | **0.31 ms** | 28.3 ms | ~91× lower; Wings per-request work ≈ 10 µs, rest is serial queue wait |
| p99 latency | **0.73 ms** | 34.4 ms | ~47× lower |
| Peak RSS under load | **6.7 MB** | 54.3 MB | **8.1× lighter** (before the leak fix Wings was ~60 MB here) |
| Idle RSS | **6.7 MB** | 51 MB | ~7.6× lighter at rest |
| Binary / footprint | **2.1 MB** self-contained | needs Python + ~50 MB deps | |

(Numbers from a 6 s run, 8 procs × 8 threads, after the per-request leak fix.
Wings peak RSS under load now equals its idle RSS — flat.)

## Finding (now FIXED): per-request memory leak in Wings

The benchmark exposed a leak: under sustained `GET /users/1` (a read-only path,
no global writes), Wings RSS grew **linearly and without bound** — idle 6.7 MB
→ 33 MB after 10k requests (~2.6 KB/req). FastAPI stayed flat.

**Root cause (architectural):** in the AOT model every object/array is
`malloc`'d with `arena_allocated = 0`, with no ARC/GC pass at the request
boundary — the per-request arena only reclaimed arena-allocated *strings*. So
each request's parsed request object, intermediate dicts, and response
envelope leaked.

**Fix (2026-06-17) — per-request malloc region + runtime write barrier:**
malloc'd object/array literals created *inside* an arena scope are chained into
a per-thread region; `arena_save` snapshots it and `arena_restore` frees the
request's transient containers (and their malloc buffers) alongside the arena
string rewind. Top-level globals and `aot_persist`/`string_pin` copies are
never tracked, so persistent data is untouched. A runtime write barrier in the
container mutators deep-copies any transient value stored into a persistent
container (global, or an already-persisted object), which — being value-flow
based — also covers globals reached through a local alias. After the fix RSS is
**flat** under sustained load, and **AddressSanitizer reports zero memory
violations** (no use-after-free / double-free / overflow) under heavy mixed
CRUD. See STATUS.md → "Runtime + codegen".

## Honest reading

- **Latency / per-request cost:** Wings wins decisively (~10 µs vs tens of ms
  under load) — native AOT doing what it should.
- **RPS:** inconclusive here (client-bound). A native load tool is needed to
  find Wings' true ceiling, and `serve()` is single-threaded — `listen_pool()`
  would scale across cores.
- **Footprint at rest + deploy:** Wings clearly lighter (single 2 MB binary,
  no runtime).
- **Memory under load:** the benchmark caught a per-request leak in Wings; it
  was **fixed the same day** (per-request malloc region + runtime write
  barrier). RSS is now flat and ASan-clean under heavy mixed CRUD.
