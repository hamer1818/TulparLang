#!/usr/bin/env bash
# TulparLang Benchmark Runner (bash). Times each program with milliseconds
# resolution using bash's $EPOCHREALTIME, takes the best of N runs.

set -e
cd "$(dirname "$0")/.."

REPEATS="${REPEATS:-3}"

export PATH="/c/msys64/mingw64/bin:$PATH"

bench() {
    local label="$1"; shift
    local best="999999.999"
    local out=""
    for ((i=0; i<REPEATS; i++)); do
        local t0 t1
        t0=$EPOCHREALTIME
        out=$("$@" 2>&1)
        t1=$EPOCHREALTIME
        local elapsed
        elapsed=$(awk "BEGIN { printf \"%.3f\", ($t1 - $t0) * 1000 }")
        # awk numeric compare
        if awk "BEGIN { exit !($elapsed < $best) }"; then
            best=$elapsed
        fi
    done
    # Save to RESULTS file
    printf '| %-30s | %10s | %s |\n' "$label" "$best" "$(echo "$out" | tr '\n' ' ' | head -c 60)"
}

echo "Building native targets..."
gcc -O2 benchmarks/fib.c     -o benchmarks/fib_c.exe
gcc -O2 benchmarks/loopsum.c -o benchmarks/loopsum_c.exe

echo "Compiling Go..."
go build -o benchmarks/fib_go.exe     benchmarks/fib.go
go build -o benchmarks/loopsum_go.exe benchmarks/loopsum.go

echo "Compiling Rust..."
rustc -C opt-level=3 benchmarks/fib.rs     -o benchmarks/fib_rs.exe     2>/dev/null
rustc -C opt-level=3 benchmarks/loopsum.rs -o benchmarks/loopsum_rs.exe 2>/dev/null

echo "Compiling Java..."
(cd benchmarks && javac fib.java loopsum.java)

echo "Building Tulpar AOT..."
rm -f benchmarks/loopsum_tulpar.* benchmarks/fib_tulpar.*
./tulpar.exe build benchmarks/loopsum.tpr benchmarks/loopsum_tulpar >/dev/null 2>&1 || true
./tulpar.exe build benchmarks/fib.tpr     benchmarks/fib_tulpar     >/dev/null 2>&1 || true

REPORT="benchmarks/RESULTS.md"
{
    echo "# TulparLang Benchmark Results"
    echo
    echo "Run: $(date '+%Y-%m-%d %H:%M')"
    echo "Repeats per language: $REPEATS (best wall time reported, in ms)."
    echo
    echo "| Benchmark                      |  Best (ms) | Output |"
    echo "|--------------------------------|-----------:|--------|"
} > "$REPORT"

run() { bench "$@" | tee -a "$REPORT"; }

echo
echo "--- loopsum (10M iterations) ---"
run "loopsum:Tulpar AOT (LLVM)"  ./benchmarks/loopsum_tulpar.exe
run "loopsum:Tulpar VM"          ./tulpar.exe --vm benchmarks/loopsum.tpr
run "loopsum:C (gcc -O2)"        ./benchmarks/loopsum_c.exe
run "loopsum:Rust (-O3)"         ./benchmarks/loopsum_rs.exe
run "loopsum:Go"                 ./benchmarks/loopsum_go.exe
run "loopsum:Java 17"            java -cp benchmarks loopsum
run "loopsum:Node.js 22"         node benchmarks/loopsum.js
run "loopsum:Python 3.12"        python benchmarks/loopsum.py

echo
echo "--- fib(35) recursive ---"
run "fib:Tulpar AOT (LLVM)"      ./benchmarks/fib_tulpar.exe
run "fib:Tulpar VM"              ./tulpar.exe --vm benchmarks/fib.tpr
run "fib:C (gcc -O2)"            ./benchmarks/fib_c.exe
run "fib:Rust (-O3)"             ./benchmarks/fib_rs.exe
run "fib:Go"                     ./benchmarks/fib_go.exe
run "fib:Java 17"                java -cp benchmarks fib
run "fib:Node.js 22"             node benchmarks/fib.js
run "fib:Python 3.12"            python benchmarks/fib.py

{
    echo
    echo "## Notes"
    echo
    echo "- \`loopsum\`: tight \`while\` loop summing 0..9_999_999 (10M iterations)."
    echo "- \`fib\`: classic recursive Fibonacci(35) — function-call / branch heavy."
    echo "- Tulpar AOT uses the typed AOT path (explicit \`: int\` return type) for"
    echo "  native LLVM i64 codegen on both benchmarks."
    echo "- Native compilers: \`gcc -O2\`, \`rustc -C opt-level=3\`."
    echo "- Java numbers include JVM startup, so they are pessimistic for short runs."
} >> "$REPORT"

echo
echo "Report written to $REPORT"
