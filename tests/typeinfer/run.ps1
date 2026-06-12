# Typeinfer regression runner (Windows).
# Mirrors run.sh — drives `tulpar --strict <file>` over pass/ and fail/
# fixtures and asserts the expected exit code.

$ErrorActionPreference = "Stop"

$tulpar = ".\tulpar.exe"
if (-not (Test-Path $tulpar)) {
    Write-Error "tulpar.exe not found. Run build.bat or build.ps1 first."
    exit 1
}

$failures = 0
$passCount = 0
$failCount = 0

foreach ($f in Get-ChildItem -Path "tests\typeinfer\pass" -Filter *.tpr) {
    $out = & $tulpar --strict $f.FullName 2>&1 | Out-String
    if ($LASTEXITCODE -eq 0) {
        Write-Host ("PASS {0}" -f $f.Name) -ForegroundColor Green
        $passCount++
    } else {
        Write-Host ("FAIL {0} — expected exit 0, got {1}" -f $f.Name, $LASTEXITCODE) -ForegroundColor Red
        Write-Host ($out -split "`n" | ForEach-Object { "    $_" }) -join "`n"
        $failures++
    }
}

foreach ($f in Get-ChildItem -Path "tests\typeinfer\fail" -Filter *.tpr) {
    $out = & $tulpar --strict $f.FullName 2>&1 | Out-String
    if ($LASTEXITCODE -ne 0 -and $out -match '\[typecheck\]') {
        Write-Host ("PASS {0} (rejected as expected)" -f $f.Name) -ForegroundColor Green
        $failCount++
    } else {
        Write-Host ("FAIL {0} — expected exit !=0 with [typecheck], got {1}" -f $f.Name, $LASTEXITCODE) -ForegroundColor Red
        Write-Host ($out -split "`n" | ForEach-Object { "    $_" }) -join "`n"
        $failures++
    }
}

Write-Host ""
if ($failures -ne 0) {
    Write-Host ("{0} typeinfer test(s) failed." -f $failures) -ForegroundColor Red
    exit 1
}
Write-Host ("All typeinfer tests passed ({0} pass, {1} fail-fixtures rejected)" -f $passCount, $failCount) -ForegroundColor Green
# Explicit exit 0 — without this PowerShell propagates $LASTEXITCODE from
# the last `tulpar --strict <fail-fixture>` call (which is non-zero by design).
exit 0
