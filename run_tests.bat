@echo off
REM Cross-platform test runner for Windows.
REM Thin wrapper around run_tests.ps1 (real implementation; supports per-test
REM timeouts so a hanging example does not block the suite).
where pwsh >nul 2>&1
if %ERRORLEVEL%==0 (
    pwsh -NoProfile -ExecutionPolicy Bypass -File "%~dp0run_tests.ps1" %*
) else (
    powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0run_tests.ps1" %*
)
exit /b %ERRORLEVEL%
