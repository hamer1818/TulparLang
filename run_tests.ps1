param(
  [int]$CompileTimeoutSec = 30,
  [int]$RunTimeoutSec = 30
)

$ErrorActionPreference = 'Continue'
# Compile-only smoke tests: these examples call listen()/api_run() and would
# block the suite waiting for HTTP requests, but verifying that they still
# compile catches regressions in the embedded server/router/api stdlib path
# that would otherwise go unnoticed (a regression here used to be silent).
$compileOnly = @(
  '09_socket_simple.tpr','09_socket_server.tpr','09_socket_client.tpr',
  '11_router_app.tpr','12_threaded_server.tpr','14_api_server.tpr',
  'api_wings.tpr','api_wings_crud.tpr','api_router_crud.tpr','tulpar_api_demo.tpr',
  # utils.tpr is a module designed to be imported by 07_modules.tpr; running
  # it standalone is meaningless (no top-level program), but it should still
  # parse + lower cleanly so we at least catch compile-side regressions.
  'utils.tpr'
)
$skip = @()
$total = 0; $pass = 0; $fail = 0; $skipped = 0; $timedOut = 0; $compileOnlyPass = 0
$failed = @()
$timeouts = @()

function Invoke-WithTimeout {
  param(
    [string]$FilePath,
    [string[]]$ArgList,
    [string]$StdinFile,
    [int]$TimeoutSec
  )
  $psi = [Diagnostics.ProcessStartInfo]::new()
  $psi.FileName = $FilePath
  foreach ($a in $ArgList) { $psi.ArgumentList.Add($a) }
  $psi.RedirectStandardOutput = $true
  $psi.RedirectStandardError  = $true
  $psi.RedirectStandardInput  = $true
  $psi.UseShellExecute = $false
  $psi.CreateNoWindow  = $true
  $p = [Diagnostics.Process]::Start($psi)
  if ($StdinFile -and (Test-Path $StdinFile)) {
    $p.StandardInput.Write([IO.File]::ReadAllText($StdinFile))
  }
  $p.StandardInput.Close()
  if (-not $p.WaitForExit($TimeoutSec * 1000)) {
    try { $p.Kill($true) } catch {}
    $p.WaitForExit(2000) | Out-Null
    return @{ TimedOut = $true; ExitCode = -1 }
  }
  return @{ TimedOut = $false; ExitCode = $p.ExitCode }
}

foreach ($f in (Get-ChildItem examples\*.tpr | Sort-Object Name)) {
  $total++
  if ($skip -contains $f.Name) {
    Write-Host ("SKIP {0}" -f $f.Name)
    $skipped++; continue
  }
  $base = [IO.Path]::GetFileNameWithoutExtension($f.Name)
  $bin = "$base.exe"
  Remove-Item -ErrorAction SilentlyContinue -Force "$base.exe","$base.ll","$base.o"

  $sw = [Diagnostics.Stopwatch]::StartNew()
  $cRes = Invoke-WithTimeout -FilePath '.\tulpar.exe' -ArgList @('build', $f.FullName, $base) -TimeoutSec $CompileTimeoutSec
  $compileTime = $sw.ElapsedMilliseconds

  if ($cRes.TimedOut) {
    Write-Host ("TIMEOUT(compile {0}s) {1}" -f $CompileTimeoutSec, $f.Name)
    $timedOut++; $timeouts += "$($f.Name) (compile)"
    Remove-Item -ErrorAction SilentlyContinue -Force "$base.exe","$base.ll","$base.o"
    continue
  }
  if (-not (Test-Path $bin)) {
    Write-Host ("FAIL(compile) {0} [{1}ms]" -f $f.Name, $compileTime)
    $fail++; $failed += "$($f.Name) (compile)"
    continue
  }

  # Compile-only suite: server/listener examples that block on listen()/api_run().
  # We verify the build succeeds and the binary is produced; we don't run it.
  if ($compileOnly -contains $f.Name) {
    Write-Host ("PASS(compile-only) {0} [compile {1}ms]" -f $f.Name, $compileTime)
    $compileOnlyPass++
    Remove-Item -ErrorAction SilentlyContinue -Force "$base.exe","$base.ll","$base.o"
    continue
  }

  $input = "examples\inputs\$base.txt"
  $sw2 = [Diagnostics.Stopwatch]::StartNew()
  $rRes = Invoke-WithTimeout -FilePath ".\$bin" -ArgList @() -StdinFile $input -TimeoutSec $RunTimeoutSec
  $runTime = $sw2.ElapsedMilliseconds

  if ($rRes.TimedOut) {
    Write-Host ("TIMEOUT(run {0}s) {1} [compile {2}ms]" -f $RunTimeoutSec, $f.Name, $compileTime)
    $timedOut++; $timeouts += "$($f.Name) (run)"
  } elseif ($rRes.ExitCode -eq 0) {
    Write-Host ("PASS {0} [compile {1}ms, run {2}ms]" -f $f.Name, $compileTime, $runTime)
    $pass++
  } else {
    Write-Host ("FAIL(run rc={0}) {1} [compile {2}ms, run {3}ms]" -f $rRes.ExitCode, $f.Name, $compileTime, $runTime)
    $fail++; $failed += "$($f.Name) (run rc=$($rRes.ExitCode))"
  }

  Remove-Item -ErrorAction SilentlyContinue -Force "$base.exe","$base.ll","$base.o"
}

# ----- Unit tests under tests/*.test.tpr -----------------------------------
# These are written against the lib/test.tpr framework: each file imports
# "test", defines test handler functions, and calls test()/test_summary().
# A FAIL in any file makes the runner exit non-zero.
$unitPass = 0
$unitFail = 0
$unitFiles = @()
if (Test-Path 'tests') {
  $unitFiles = Get-ChildItem tests\*.test.tpr -ErrorAction SilentlyContinue | Sort-Object Name
}
foreach ($t in $unitFiles) {
  $base = [IO.Path]::GetFileNameWithoutExtension($t.Name)
  Remove-Item -ErrorAction SilentlyContinue -Force "tests\$base.exe","tests\$base.ll","tests\$base.o"
  $sw = [Diagnostics.Stopwatch]::StartNew()
  $cRes = Invoke-WithTimeout -FilePath '.\tulpar.exe' -ArgList @('build', $t.FullName, "tests\$base") -TimeoutSec $CompileTimeoutSec
  $compileTime = $sw.ElapsedMilliseconds
  if ($cRes.TimedOut -or -not (Test-Path "tests\$base.exe")) {
    Write-Host ("FAIL(unit-compile) {0}" -f $t.Name)
    $unitFail++
    continue
  }
  $sw2 = [Diagnostics.Stopwatch]::StartNew()
  $rRes = Invoke-WithTimeout -FilePath ".\tests\$base.exe" -ArgList @() -TimeoutSec $RunTimeoutSec
  $runTime = $sw2.ElapsedMilliseconds
  if ($rRes.TimedOut) {
    Write-Host ("TIMEOUT(unit-run) {0}" -f $t.Name)
    $unitFail++
  } elseif ($rRes.ExitCode -eq 0) {
    Write-Host ("PASS(unit) {0} [compile {1}ms, run {2}ms]" -f $t.Name, $compileTime, $runTime)
    $unitPass++
  } else {
    Write-Host ("FAIL(unit rc={0}) {1}" -f $rRes.ExitCode, $t.Name)
    $unitFail++
  }
  Remove-Item -ErrorAction SilentlyContinue -Force "tests\$base.exe","tests\$base.ll","tests\$base.o"
}

Write-Host ""
Write-Host "========================================"
Write-Host ("Examples Total:      {0}" -f $total)
Write-Host ("  Passed:            {0}" -f $pass)
Write-Host ("  Compile-only PASS: {0}" -f $compileOnlyPass)
Write-Host ("  Failed:            {0}" -f $fail)
Write-Host ("  Timeout:           {0}" -f $timedOut)
Write-Host ("  Skipped:           {0}" -f $skipped)
Write-Host ("Unit tests Total:    {0}" -f $unitFiles.Count)
Write-Host ("  Passed:            {0}" -f $unitPass)
Write-Host ("  Failed:            {0}" -f $unitFail)
Write-Host "========================================"
if ($failed.Count -gt 0) {
  Write-Host "Failed tests:"
  foreach ($x in $failed) { Write-Host "  - $x" }
}
if ($timeouts.Count -gt 0) {
  Write-Host "Timed-out tests:"
  foreach ($x in $timeouts) { Write-Host "  - $x" }
}
if (($fail + $timedOut + $unitFail) -gt 0) { exit 1 }
