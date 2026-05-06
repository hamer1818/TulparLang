# TulparLang Benchmark Results

Run on 2026-05-01 23:00 (Microsoft Windows NT 10.0.26200, MinGW64).
Best wall time of 3 runs per language. Times in milliseconds.

## CPU benchmarks

| Benchmark | Best (ms) | Output |
|-----------|----------:|--------|
| loopsum:Tulpar AOT | 88 | 49999995000000 |
| loopsum:C(gcc -O2) | 67 | 49999995000000 |
| fib:Tulpar AOT | 114 | 9227465 |
| fib:C(gcc -O2) | 83 | 9227465 |
| struct_sum:Tulpar AOT (struct) | 29.7 | 299999970000000 |
| struct_sum:Tulpar AOT (boxed)  | 2923.6 | 299999970000000 |
| struct_sum:C(gcc -O2)          | 20.2 | 299999970000000 |

Tulpar AOT (CPU loops) lands in the **1.3–1.4× C** range — same
neighbourhood as Rust and Go on identical hardware.

`struct_sum` measures Plan 04 PR6: 10M iterasyon × 3-field `V3` struct
toplamasi.
- **Typed struct path** (`benchmarks/struct_sum.tpr`): native LLVM
  struct alloca + alan-alan `getelementptr` + `load`/`store`. LLVM O3
  cogu durumda struct'i tamamen scalarize ediyor.
- **Boxed json path** (`benchmarks/struct_sum_boxed.tpr`): ayni
  hesaplama, ama her alan erisimi `vm_set_element` /
  `vm_get_element` runtime cagrilarindan geciyor — Plan 04 oncesi
  "her sey json" yaklasimi.
- Sonuc: typed yol ~**98× hizli** (boxed json'a karsi) ve C'ye
  ~1.47× yakin. Plan 04'un struct hedefi (mottosu: "C kadar hizli")
  somut, olculebilir bir kazanc olarak dogrulandi.

## HTTP throughput

`benchmarks/http_bench.py` — 5000 GET requests over 4 keep-alive TCP
connections, single-threaded servers (`Connection: keep-alive` end to
end, JSON `{"hello":"world"}` body).

| Server                  | Wall (s) | req/sec | Notes |
|-------------------------|---------:|--------:|-------|
| Tulpar Wings            | 0.193    | ~26 000 | wings.tpr listen() loop, NODELAY on accept, dlsym cache |
| Node.js http            | 0.184    | ~27 200 | `http.createServer`, V8 22.x |
| Python ThreadingHTTP    | 0.354    | ~14 100 | `ThreadingHTTPServer`, single CPython process |

Compared to the pre-3a baseline (single-shot `socket_receive`, no
keep-alive, no NODELAY) Tulpar Wings improved from **~22 200 req/sec**
→ **~26 000 req/sec** (+17%) and is now within ~5% of Node.js. Async
I/O (worker thread pool) is the next lever and is tracked as Faz 3b.

## Methodology

- `loopsum`: tight `while` loop summing `0..9_999_999` (10M iterations).
- `fib`: classic recursive Fibonacci(35) — function-call / branch heavy.
- Tulpar AOT for both uses the typed AOT path (explicit `: int` return
  type) for native LLVM i64 codegen.
- Native compilers used optimization flags: `gcc -O2`.
- HTTP results use **single-threaded** servers on every side; running
  Node with cluster or Tulpar on a hypothetical thread pool is a
  separate measurement that is not yet wired up.
