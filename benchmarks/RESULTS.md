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

Tulpar AOT lands in the **1.3–1.4× C** range — same neighbourhood as
Rust and Go on identical hardware. The earlier 2026-04-28 record had
Tulpar at 166 ms on `loopsum` and 185 ms on `fib`; the gap closed once
LLVM had room to optimise the typed AOT path's tight loops (`break` /
`continue` codegen no longer leaves dead-code phi nodes in front of
the loop's induction-variable updates, and the static `recv` buffer
removed a malloc/free hot pair from any benchmark that touches sockets).

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
