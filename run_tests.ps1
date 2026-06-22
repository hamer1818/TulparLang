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
  'api_wings.tpr','api_wings_crud.tpr','api_wings_tls.tpr','api_wings_sse.tpr','api_router_crud.tpr','demo_users_api.tpr','wings_simple_test.tpr','wings_middleware_test.tpr','wings_groups_test.tpr','wings_query_test.tpr','wings_response_model_test.tpr','wings_upload_test.tpr','wings_di_test.tpr','tulpar_api_demo.tpr',
  # utils.tpr is a module designed to be imported by 07_modules.tpr; running
  # it standalone is meaningless (no top-level program), but it should still
  # parse + lower cleanly so we at least catch compile-side regressions.
  'utils.tpr'
)
$skip = @()

# HTTP smoke probes — see build.sh for rationale. The 2s alive check below
# only verifies startup didn't crash, but wings/router examples block in
# accept() so the handler-level bug class (Issue #86 / PR #76 cookies) sails
# right through. Each entry below = "after alive check, HTTP GET this URL
# with a 5s timeout; require any response code, otherwise FAIL". The body
# and status are intentionally ignored — the bug fires before either is
# produced, we just need the roundtrip.
$smokeProbes = @{
  'api_wings.tpr'         = 'http://127.0.0.1:3000/'
  'api_wings_crud.tpr'    = 'http://127.0.0.1:3000/'
  'api_wings_tls.tpr'     = 'https://127.0.0.1:8443/'
  'api_router_crud.tpr'   = 'http://127.0.0.1:8080/'
  '11_router_app.tpr'     = 'http://127.0.0.1:8080/'
  '12_threaded_server.tpr'= 'http://127.0.0.1:8089/'
}

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
  # We verify the build succeeds AND run a 2-second runtime smoke test —
  # spawn the binary, give it time to either start serving (wings/router
  # block on accept) or crash early, then check liveness. PR #64 was
  # exactly this regression: every wings example built clean on Linux
  # but segfaulted at socket_server() — would have caught it here at
  # CI time instead of silently shipping in a release.
  if ($compileOnly -contains $f.Name) {
    $smokeProc = $null
    $smokeFailed = $false
    $smokeMsg = ""
    try {
      $smokeProc = Start-Process -FilePath ".\$bin" -PassThru -NoNewWindow `
        -RedirectStandardOutput ([IO.Path]::Combine($env:TEMP, "$base.smoke.out")) `
        -RedirectStandardError  ([IO.Path]::Combine($env:TEMP, "$base.smoke.err"))
      $smokeProc | Wait-Process -Timeout 2 -ErrorAction SilentlyContinue
      if ($smokeProc.HasExited) {
        if ($smokeProc.ExitCode -ne 0) {
          $smokeFailed = $true
          $smokeMsg = "exit $($smokeProc.ExitCode)"
        }
      } else {
        # Still alive after 2s. If we have an HTTP probe for this
        # example, hit it now — the bare alive check otherwise lets
        # handler-level crashes (Issue #86) pass undetected.
        $probeUrl = $smokeProbes[$f.Name]
        if ($probeUrl) {
          try {
            # -UseBasicParsing avoids IE engine init; TimeoutSec bounds the
            # call so a hung handler doesn't wedge the suite.
            # -SkipCertificateCheck trusts the self-signed fixture cert in
            # tests/fixtures/ for api_wings_tls.tpr (no CA chain); no-op
            # for plain HTTP probes since the flag only kicks in on https://.
            $resp = Invoke-WebRequest -Uri $probeUrl -UseBasicParsing -TimeoutSec 5 -SkipCertificateCheck -ErrorAction Stop
            # Any HTTP response is success — we don't care about status.
            if (-not $smokeProc.HasExited -and $resp.StatusCode -gt 0) {
              # Probe round-tripped, server still alive: real pass.
            } else {
              $smokeFailed = $true
              $smokeMsg = "server died after probe"
            }
          } catch {
            # Connect refused, timeout, or 5xx-with-throw all land here.
            # Differentiate "server crashed" vs "probe couldn't reach":
            # if the process exited during the probe, it crashed on the
            # request — exactly the regression we want to catch.
            if ($smokeProc.HasExited) {
              $smokeFailed = $true
              $smokeMsg = "server crashed on probe (exit $($smokeProc.ExitCode))"
            } else {
              # Server still alive but probe couldn't reach. Could be a
              # genuine probe misconfiguration (wrong port). Still flag
              # it so we notice rather than silently hide bugs.
              $smokeFailed = $true
              $smokeMsg = "probe failed: $($_.Exception.Message)"
            }
          }
        }
        Stop-Process -Id $smokeProc.Id -Force -ErrorAction SilentlyContinue
        $smokeProc | Wait-Process -ErrorAction SilentlyContinue
      }
    } catch {
      $smokeFailed = $true
      $smokeMsg = "exception: $_"
    }
    Remove-Item -ErrorAction SilentlyContinue -Force -LiteralPath @(
      [IO.Path]::Combine($env:TEMP, "$base.smoke.out"),
      [IO.Path]::Combine($env:TEMP, "$base.smoke.err")
    )
    if ($smokeFailed) {
      Write-Host ("FAIL(smoke {0}) {1} [compile {2}ms]" -f $smokeMsg, $f.Name, $compileTime)
      $fail++; $failed += "$($f.Name) (smoke $smokeMsg)"
    } else {
      $probeNote = if ($smokeProbes[$f.Name]) { ' +probe' } else { '' }
      Write-Host ("PASS(compile-only +smoke{0}) {1} [compile {2}ms]" -f $probeNote, $f.Name, $compileTime)
      $compileOnlyPass++
    }
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
