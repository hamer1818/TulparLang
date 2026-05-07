# TulparLang Benchmark Results

Run on 2026-05-07 12:27 (Microsoft Windows NT 10.0.26200.0).
Best wall time of 5 runs per language. Times in milliseconds.

## After optimization PR (2026-05-07)

| Benchmark | Best (ms) | Output |
|-----------|----------:|--------|
| loopsum:Tulpar AOT | 31.4 | 49999995000000 |
| loopsum:Tulpar VM | 384.3 | 49999995000000 |
| loopsum:Python | 510.6 | 49999995000000 |
| loopsum:Node.js | 47.1 | 49999995000000 |
| loopsum:Go | 30.2 | 49999995000000 |
| loopsum:Rust | 19.5 | 49999995000000 |
| loopsum:C(gcc -O2) | 20.5 | 49999995000000 |
| loopsum:Java | 121 | 49999995000000 |
| fib:Tulpar AOT | 55.6 | 9227465 |
| fib:Tulpar VM | 568 | 9227465 |
| fib:Python | 959.7 | 9227465 |
| fib:Node.js | 106.8 | 9227465 |
| fib:Go | 62.3 | 9227465 |
| fib:Rust | 41.2 | 9227465 |
| fib:C(gcc -O2) | 31.6 | 9227465 |
| fib:Java | 123.2 | 9227465 |
| struct_sum:Tulpar AOT (struct) | 32.5 | 299999970000000 |
| struct_sum:Tulpar AOT (boxed) | 2936.7 | 299999970000000 |
| struct_sum:C(gcc -O2) | 18.4 | 299999970000000 |
| struct_array_push:Tulpar AOT (heap struct) | 117.8 | 2999997000000 |
| struct_array_push:Tulpar AOT (boxed json) | 628.7 | 2999997000000 |
| struct_array_push:C(gcc -O2) | 24.5 | 2999997000000 |

## What changed in the AOT pipeline

Three knobs in `src/aot/llvm_backend.cpp` were holding back the typed
AOT path. Fixing them moved Tulpar materially closer to gcc -O2:

| Benchmark        | Before  | After   | Δ        | vs C/Rust |
|------------------|---------|---------|----------|-----------|
| loopsum AOT      | 42.2 ms | 31.4 ms | **−25.6 %** | 1.53× (was 2.14×) |
| fib AOT          | 72.4 ms | 55.6 ms | **−23.2 %** | 1.76× (was 2.29×) |
| struct_sum AOT   | 32.0 ms | 32.5 ms | noise    | 1.77× (no change) |

### 1. `noinline,optnone` removed from typed-int functions

`codegen_native_func_def` (the fast path for `func f(int n): int`)
was tagging every emitted function with `noinline` AND `optnone`.
The comment in the source said it was there "to prevent LLVM from
eliminating loops via closed-form solutions" — i.e. to protect
the loopsum micro-benchmark from being SCEV-folded to its
analytic result. The cost: every typed function paid the price.

The IR before, for `fibonacci`:

```
attributes #0 = { noinline nounwind optnone uwtable ... }
define i64 @fibonacci(i64 %0) {
entry:
    %n = alloca i64
    store i64 %0, ptr %n
    %n1 = load i64, ptr %n        ; reload of param
    %le = icmp sle i64 %n1, 1
    %zext = zext i1 %le to i64
    %cond = icmp ne i64 %zext, 0  ; redundant zext+icmp on a bool
    ...
```

After (replaced with `inlinehint`, runs through full O3):

```
attributes #0 = { inlinehint nofree nosync nounwind memory(none) uwtable ... }
define i64 @fibonacci(i64 %0) {
entry:
    %le10 = icmp slt i64 %0, 2
    br i1 %le10, label %common.ret, label %merge
merge:
    %.tr12 = phi i64 [ %sub5, %merge ], [ %0, %entry ]
    %accumulator.tr11 = phi i64 [ %add, %merge ], [ 0, %entry ]
    %sub = add nsw i64 %.tr12, -1
    %call_native = tail call i64 @fibonacci(i64 %sub)
    ...
```

`mem2reg` promoted the alloca, GVN folded the redundant
load/zext/icmp, the inliner recognised the trivial base case as
unrollable, one of the two recursive calls became a `tail call`,
and LLVM proved the function pure (`memory(none)`) so call sites
can hoist/cse it.

### 2. Optimization pipeline switched from `default<O2>` to `default<O3>`

The pre-existing string was `default<O2>,function(sroa,gvn,instcombine)`
with a printf that lied (`"Optimizations (O3) applied successfully"`).
Now it's actually `default<O3>` — buys SLP + loop vectorization,
inliner aggressiveness, and SCC-based passes.

### 3. `LoopUnrolling` flipped on

`LLVMPassBuilderOptionsSetLoopUnrolling(options, 0)` was paired with
the `optnone` gag for the same reason. With both gone, real loops
get the unrolling speedup that matters in user code.

## Keeping the loopsum benchmark honest

LLVM at O3 with full optimization happily folds
`for (int i = 0; i < 10000000; i++) total += i;` into the analytic
`n*(n−1)/2` via `LoopIdiomRecognize`. The fix is upstream: the
benchmarks now read their iteration count from `TULPAR_BENCH_N`
(default 10M for loopsum, 35 for fib) so the bound is opaque to
the optimizer:

```tpr
int n = toInt(env("TULPAR_BENCH_N"));
if (n <= 0) {
    n = 10000000;
}
print(compute(n));
```

The VM compiler also gained an `env()` builtin (opcode 62) so the
same source compiles and runs under both backends.

## Notes

- `loopsum`: tight `while` loop summing 0..N (default 10M).
- `fib`: classic recursive Fibonacci(N) (default 35) — function-call /
  branch heavy.
- Tulpar AOT for `fib` uses the typed AOT path (explicit `: int`
  return type) for native LLVM i64 codegen.
- Native compilers used optimization flags: `gcc -O2`, `rustc -C opt-level=3`.
- Java timing includes JVM startup; consider it pessimistic for short runs.
- Go binaries are statically optimized via `go build` defaults.

## Where Tulpar AOT now stands

For `fib(35)`: **Rust 41 → Tulpar 56 → Go 62 → Node 107 → Java 123 → C++ alone faster**.

For `loopsum(10M)`: **Rust 20 → C 21 → Go 30 → Tulpar 31 → Node 47 → Java 121 → Python 511**.

The remaining gap to gcc/Rust is mostly allocation overhead in
struct-heavy paths (`struct_array_push` at 4.8× C); the integer
hot loops are now within ~1.5×.
