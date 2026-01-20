# Tulpar AOT - Build Script (Windows)

$env:Path += ";C:\Program Files\LLVM\bin"

Write-Host "=== Building Tulpar LLVM Test ===" -ForegroundColor Green

# Use local headers (no admin needed!)
$LLVM_INCLUDE = "-I../../include"
$LLVM_LIB = "-L`"C:\Program Files\LLVM\lib`""

Write-Host "Compiling llvm_test.c..." -ForegroundColor Yellow
Write-Host "Using local LLVM headers from: ../../include" -ForegroundColor Cyan

# Compile only (linking is complex on Windows, will use CMake later)
clang -c $LLVM_INCLUDE llvm_test.c -o llvm_test.obj

if ($LASTEXITCODE -eq 0) {
    Write-Host "`n✓ Compilation successful!" -ForegroundColor Green
    Write-Host "LLVM C API headers working!" -ForegroundColor Green
    Write-Host "`nNext: Full build with CMake for linking" -ForegroundColor Yellow
} else {
    Write-Host "`n✗ Compilation failed!" -ForegroundColor Red
    exit 1
}
