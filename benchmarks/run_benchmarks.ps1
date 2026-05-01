# TulparLang Benchmark Runner
# Compares Tulpar vs Python, Node.js, Go, Rust, C, Java for two workloads:
#   - loopsum: tight integer loop (10M iterations summing 0..N)
#   - fib   : recursive Fibonacci(35)

param(
    [int]$Repeats = 3
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
Set-Location $root

# Make sure mingw is on PATH for clang++/g++/rustc helpers if needed
$env:PATH = "C:\msys64\mingw64\bin;$env:PATH"

function Measure-Run {
    param(
        [string]$Label,
        [string]$Cmd,
        [string[]]$CmdArgs
    )
    $best = [double]::MaxValue
    $output = $null
    for ($i = 0; $i -lt $Repeats; $i++) {
        $sw = [System.Diagnostics.Stopwatch]::StartNew()
        $output = & $Cmd @CmdArgs 2>&1 | Out-String
        $sw.Stop()
        $ms = $sw.Elapsed.TotalMilliseconds
        if ($ms -lt $best) { $best = $ms }
    }
    [pscustomobject]@{
        Benchmark = $Label
        Best_ms   = [math]::Round($best, 1)
        Output    = ($output -replace '\s+', ' ').Trim()
    }
}

function Compile-Native {
    param([string]$Source, [string]$Out, [string[]]$Flags)
    & gcc -O2 $Source -o $Out @Flags
    if ($LASTEXITCODE -ne 0) { throw "gcc failed for $Source" }
}

# Build native targets first.
Write-Host "Compiling native programs..."
Compile-Native -Source 'benchmarks/fib.c'     -Out 'benchmarks/fib_c.exe'     -Flags @()
Compile-Native -Source 'benchmarks/loopsum.c' -Out 'benchmarks/loopsum_c.exe' -Flags @()

Write-Host "Compiling Go..."
& go build -o 'benchmarks/fib_go.exe'     'benchmarks/fib.go'
& go build -o 'benchmarks/loopsum_go.exe' 'benchmarks/loopsum.go'

Write-Host "Compiling Rust..."
& rustc -C opt-level=3 'benchmarks/fib.rs'     -o 'benchmarks/fib_rs.exe'     2>$null
& rustc -C opt-level=3 'benchmarks/loopsum.rs' -o 'benchmarks/loopsum_rs.exe' 2>$null

Write-Host "Compiling Java..."
Push-Location 'benchmarks'
& javac fib.java
& javac loopsum.java
Pop-Location

Write-Host "Building Tulpar AOT binaries..."
Remove-Item 'benchmarks/fib_tulpar.exe', 'benchmarks/loopsum_tulpar.exe' -ErrorAction SilentlyContinue
& .\tulpar.exe build benchmarks\loopsum.tpr benchmarks\loopsum_tulpar 2>&1 | Out-Null
& .\tulpar.exe build benchmarks\fib.tpr     benchmarks\fib_tulpar     2>&1 | Out-Null

$results = @()

# loopsum benchmark
Write-Host "`n--- loopsum (10M iterations) ---"
$results += Measure-Run -Label 'loopsum:Tulpar AOT' -Cmd '.\benchmarks\loopsum_tulpar.exe' -CmdArgs @()
$results += Measure-Run -Label 'loopsum:Tulpar VM'  -Cmd '.\tulpar.exe' -CmdArgs @('--vm','benchmarks\loopsum.tpr')
$results += Measure-Run -Label 'loopsum:Python'     -Cmd 'python' -CmdArgs @('benchmarks\loopsum.py')
$results += Measure-Run -Label 'loopsum:Node.js'    -Cmd 'node'   -CmdArgs @('benchmarks\loopsum.js')
$results += Measure-Run -Label 'loopsum:Go'         -Cmd '.\benchmarks\loopsum_go.exe' -CmdArgs @()
$results += Measure-Run -Label 'loopsum:Rust'       -Cmd '.\benchmarks\loopsum_rs.exe' -CmdArgs @()
$results += Measure-Run -Label 'loopsum:C(gcc -O2)' -Cmd '.\benchmarks\loopsum_c.exe'  -CmdArgs @()
$results += Measure-Run -Label 'loopsum:Java'       -Cmd 'java' -CmdArgs @('-cp','benchmarks','loopsum')

# fib benchmark
Write-Host "`n--- fib(35) recursive ---"
$results += Measure-Run -Label 'fib:Tulpar AOT'  -Cmd '.\benchmarks\fib_tulpar.exe' -CmdArgs @()
$results += Measure-Run -Label 'fib:Tulpar VM'   -Cmd '.\tulpar.exe' -CmdArgs @('--vm','benchmarks\fib.tpr')
$results += Measure-Run -Label 'fib:Python'      -Cmd 'python' -CmdArgs @('benchmarks\fib.py')
$results += Measure-Run -Label 'fib:Node.js'     -Cmd 'node'   -CmdArgs @('benchmarks\fib.js')
$results += Measure-Run -Label 'fib:Go'          -Cmd '.\benchmarks\fib_go.exe' -CmdArgs @()
$results += Measure-Run -Label 'fib:Rust'        -Cmd '.\benchmarks\fib_rs.exe' -CmdArgs @()
$results += Measure-Run -Label 'fib:C(gcc -O2)'  -Cmd '.\benchmarks\fib_c.exe'  -CmdArgs @()
$results += Measure-Run -Label 'fib:Java'        -Cmd 'java' -CmdArgs @('-cp','benchmarks','fib')

Write-Host "`n=== RESULTS (best of $Repeats runs) ==="
$results | Format-Table Benchmark, Best_ms, Output -AutoSize

# Save report
$report = "benchmarks\RESULTS.md"
@"
# TulparLang Benchmark Results

Run on $(Get-Date -Format 'yyyy-MM-dd HH:mm') ($([Environment]::OSVersion.VersionString)).
Best wall time of $Repeats runs per language. Times in milliseconds.

| Benchmark | Best (ms) | Output |
|-----------|----------:|--------|
"@ | Set-Content $report

foreach ($r in $results) {
    "| $($r.Benchmark) | $($r.Best_ms) | $($r.Output) |" | Add-Content $report
}

@"

## Notes

- ``loopsum``: tight ``while`` loop summing 0..9_999_999 (10M iterations).
- ``fib``: classic recursive Fibonacci(35) — function-call / branch heavy.
- Tulpar AOT for ``fib`` uses the typed AOT path (explicit ``: int`` return type)
  for native LLVM i64 codegen.
- Native compilers used optimization flags: ``gcc -O2``, ``rustc -C opt-level=3``.
- Java timing includes JVM startup; consider it pessimistic for short runs.
- Go binaries are statically optimized via ``go build`` defaults.
"@ | Add-Content $report

Write-Host "`nReport written to $report"
