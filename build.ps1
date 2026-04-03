# ============================================
# TulparLang Windows Build Script (PowerShell)
# Version 2.1.0
# ============================================
# Requires: Visual Studio 2019/2022, CMake, LLVM 18+

param(
    [string]$Action = "build"
)

$ErrorActionPreference = "Stop"

Write-Host "========================================" -ForegroundColor Blue
Write-Host "TulparLang Windows Build Script" -ForegroundColor Blue
Write-Host "========================================" -ForegroundColor Blue
Write-Host ""

$BuildDir = "build-windows"

# ============================================
# Clean Action
# ============================================
if ($Action -eq "clean") {
    Write-Host "Cleaning build artifacts..."
    Remove-Item -Recurse -Force $BuildDir -ErrorAction SilentlyContinue
    Remove-Item -Recurse -Force build -ErrorAction SilentlyContinue
    Remove-Item -Force tulpar.exe -ErrorAction SilentlyContinue
    Write-Host "Clean complete." -ForegroundColor Green
    exit 0
}

# ============================================
# Check Dependencies
# ============================================
Write-Host "Checking dependencies..."

# Check CMake
if (-not (Get-Command cmake -ErrorAction SilentlyContinue)) {
    Write-Host "ERROR: CMake not found." -ForegroundColor Red
    Write-Host "Install from: https://cmake.org/" -ForegroundColor Yellow
    exit 1
}
Write-Host "  CMake: OK" -ForegroundColor Green

# Check Visual Studio (MSBuild)
if (-not (Get-Command msbuild -ErrorAction SilentlyContinue)) {
    Write-Host "ERROR: Visual Studio not found." -ForegroundColor Red
    Write-Host "Install Visual Studio 2019/2022 with C++ Desktop Development workload." -ForegroundColor Yellow
    exit 1
}
Write-Host "  Visual Studio: OK" -ForegroundColor Green

# Check LLVM
if (-not (Get-Command llvm-config -ErrorAction SilentlyContinue)) {
    Write-Host "  WARNING: llvm-config not found in PATH." -ForegroundColor Yellow
    Write-Host "  Make sure LLVM is installed and CMAKE can find it." -ForegroundColor Yellow
} else {
    Write-Host "  LLVM: OK" -ForegroundColor Green
}

Write-Host ""

# ============================================
# Build
# ============================================
Write-Host "Building TulparLang for Windows..."
New-Item -ItemType Directory -Force -Path $BuildDir | Out-Null
Set-Location $BuildDir

Write-Host "Running CMake configuration..." -ForegroundColor Cyan
try {
    cmake .. -G "Visual Studio 17 2022" -A x64 -DCMAKE_BUILD_TYPE=Release
    if ($LASTEXITCODE -ne 0) { throw "CMake configuration failed" }
} catch {
    Write-Host ""
    Write-Host "ERROR: CMake configuration failed!" -ForegroundColor Red
    Write-Host ""
    Write-Host "Try installing LLVM 18+ from:" -ForegroundColor Yellow
    Write-Host "  https://github.com/llvm/llvm-project/releases" -ForegroundColor Yellow
    Set-Location ..
    exit 1
}

Write-Host ""
Write-Host "Building project..." -ForegroundColor Cyan
try {
    cmake --build . --config Release -j
    if ($LASTEXITCODE -ne 0) { throw "Build failed" }
} catch {
    Write-Host ""
    Write-Host "ERROR: Build failed!" -ForegroundColor Red
    Set-Location ..
    exit 1
}

# Copy executable
Write-Host ""
Write-Host "Copying executable..." -ForegroundColor Cyan
Copy-Item Release\tulpar.exe ..\tulpar.exe

Set-Location ..

Write-Host ""
Write-Host "========================================" -ForegroundColor Green
Write-Host "BUILD SUCCESSFUL!" -ForegroundColor Green
Write-Host "========================================" -ForegroundColor Green
Write-Host ""
Write-Host "Executable: tulpar.exe"
Write-Host ""
Write-Host "Usage:"
Write-Host "  tulpar.exe script.tpr              - Run script (AOT mode, native speed)"
Write-Host "  tulpar.exe --vm script.tpr         - Run in VM mode"
Write-Host "  tulpar.exe build script.tpr        - Build standalone executable"
Write-Host "  tulpar.exe --repl                  - Interactive REPL"
Write-Host ""
Write-Host "  .\build.ps1 clean                  - Clean build artifacts"
