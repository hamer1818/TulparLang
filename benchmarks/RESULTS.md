# TulparLang Benchmark Results

Run on 2026-05-06 17:01 (Microsoft Windows NT 10.0.26200.0).
Best wall time of 3 runs per language. Times in milliseconds.

| Benchmark | Best (ms) | Output |
|-----------|----------:|--------|
| loopsum:Tulpar AOT | 49.5 | 49999995000000 |
| loopsum:Tulpar VM | 326.9 | 49999995000000 |
| loopsum:Python | 567.3 | 49999995000000 |
| loopsum:Node.js | 184.1 | 49999995000000 |
| loopsum:Go | 31.4 | 49999995000000 |
| loopsum:Rust | 22.6 | 49999995000000 |
| loopsum:C(gcc -O2) | 23.7 | 49999995000000 |
| loopsum:Java | 123.6 | 49999995000000 |
| fib:Tulpar AOT | 76.6 | 9227465 |
| fib:Tulpar VM | 532.9 | 9227465 |
| fib:Python | 993 | 9227465 |
| fib:Node.js | 246.1 | 9227465 |
| fib:Go | 64.9 | 9227465 |
| fib:Rust | 43.8 | 9227465 |
| fib:C(gcc -O2) | 34 | 9227465 |
| fib:Java | 141.3 | 9227465 |
| struct_sum:Tulpar AOT (struct) | 34.5 | 299999970000000 |
| struct_sum:Tulpar AOT (boxed) | 3076.6 | 299999970000000 |
| struct_sum:C(gcc -O2) | 20.9 | 299999970000000 |
| struct_array_push:Tulpar AOT (heap struct) | 129.7 | 2999997000000 |
| struct_array_push:Tulpar AOT (boxed json) | 670.1 | 2999997000000 |
| struct_array_push:C(gcc -O2) | 29.6 | 2999997000000 |

## Notes

- `loopsum`: tight `while` loop summing 0..9_999_999 (10M iterations).
- `fib`: classic recursive Fibonacci(35) — function-call / branch heavy.
- Tulpar AOT for `fib` uses the typed AOT path (explicit `: int` return type)
  for native LLVM i64 codegen.
- Native compilers used optimization flags: `gcc -O2`, `rustc -C opt-level=3`.
- Java timing includes JVM startup; consider it pessimistic for short runs.
- Go binaries are statically optimized via `go build` defaults.
