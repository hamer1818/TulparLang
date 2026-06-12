# ============================================
# TulparLang Windows Build Script (PowerShell)
# ============================================
# Thin wrapper around build.bat so we keep a single source of truth for
# Windows build logic (toolchain detection, build-windows wipe, linking).

param(
    [string]$Action = ""
)

$ErrorActionPreference = "Stop"
$batPath = Join-Path $PSScriptRoot "build.bat"

if (-not (Test-Path $batPath)) {
    Write-Host "ERROR: build.bat not found beside build.ps1" -ForegroundColor Red
    exit 1
}

if ([string]::IsNullOrWhiteSpace($Action)) {
    & cmd /c $batPath
} else {
    & cmd /c $batPath $Action
}

exit $LASTEXITCODE
