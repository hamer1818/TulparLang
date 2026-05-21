# Tulpar Benchmarks & Reproduction

The benchmark numbers shown in [README.md](../README.md) and [RESULTS.md](RESULTS.md) are baked from local runs performed on developer machines. We do not run the benchmarks automatically on GitHub Actions CI to avoid putting unnecessary load on the CI system and to ensure we get reliable, low-variance timings that are free from the noisy co-tenant scheduling issues of shared cloud VMs.

Anyone can run these benchmarks on their own computer using the exact same orchestrator scripts.

## Methodology & Design

The orchestrator [benchmarks/ci_run.py](ci_run.py) compiles and runs:
1. **CPU benchmarks** across 7 workloads:
   - `loopsum`: Stresses loop codegen and integer ALU.
   - `fib(35)`: Stresses recursive function-call overhead.
   - `ackermann(3, 8)`: Stresses deep call stack and recursive dispatch.
   - `tak(18, 12, 6)`: Stresses function-call overhead and inlining.
   - `sieve(100K)`: Stresses array access and branch-heavy loops.
   - `struct_sum`: Stresses struct field access codegen (native AOT path vs boxed helpers).
   - `struct_array_push`: Stresses heap allocation, struct packing, and array readback.
2. **HTTP throughput** benchmarks:
   - Compares the 5 Wings server listener models and evented cache mode with Node.js and Python HTTP servers.

Timings are collected using `time.perf_counter()` best of N runs (default 5 runs, configurable) to filter out system jitter.

## Local Reproduction Steps

To run the benchmark suite on your own machine:

### Prerequisites

Ensure you have the required compilers and interpreters installed:
- **Tulpar** (built in the repository root)
- **gcc** (C compiler)
- **rustc** (Rust compiler)
- **go** (Go compiler)
- **node** (Node.js runtime)
- **python** (Python 3 runtime)

### Running the Orchestrator

1. **Build Tulpar first:**
   - On Linux/macOS: `./build.sh`
   - On Windows: `./build.bat` or `./build.ps1`

2. **Execute the benchmark runner:**
   ```bash
   python benchmarks/ci_run.py --repeats 5
   ```
   This will build the comparator binaries, compile the Tulpar variants, run the CPU workloads 5 times (retaining the best time), run the HTTP throughput test, and write the raw payload to `benchmarks/RESULTS.json` while splicing the results into `benchmarks/RESULTS.md`.

3. **Update the README.md tables:**
   ```bash
   python benchmarks/update_readme.py
   ```
   This reads `benchmarks/RESULTS.json` and updates the markdown tables inside [README.md](../README.md) automatically.

## Notes on Jitter & Variance

No environment is perfectly quiet. For the most accurate comparisons:
- Close heavy applications (browser, IDE, Docker, etc.) before running the benchmarks.
- Use a high number of repeats (e.g. `--repeats 5` or `--repeats 10`).
- Remember that relative ratios (e.g. "Tulpar AOT vs C") are always much more stable and representative than absolute timings.
