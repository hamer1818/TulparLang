# Build Tulpac Driver
$env:Path += ";C:\Program Files\LLVM\bin"

$LLVM_INCLUDE = "-I../../include"
$LLVM_LIB_DIR = "C:\Program Files\LLVM\lib"
$LIBS = "-lLLVM-C"
$SYS_LIBS = "-lAdvapi32", "-lShell32", "-lOle32", "-lUuid"

# Include parser/lexer headers
$COMMON_INCLUDE = "-I../lexer -I../parser"

Write-Host "Compiling dependencies..."
# We need to compile lexer.c and parser.c from their source dirs
# Assuming lexer object creation is consistent with previous steps
clang -c ../lexer/lexer.c -o lexer.obj
clang -c ../parser/parser.c -o parser.obj
clang -c $LLVM_INCLUDE llvm_backend.c -o llvm_backend.obj

Write-Host "Compiling Tulpar AOT Driver..."
clang -c $LLVM_INCLUDE $COMMON_INCLUDE tulpar_aot.c -o tulpar_aot.obj

if (!(Test-Path llvm_backend.obj)) {
    Write-Host "FATAL: llvm_backend.obj missing!" -ForegroundColor Red
    exit 1
}

Write-Host "Linking tulpar_aot.exe..."
clang tulpar_aot.obj llvm_backend.obj lexer.obj parser.obj -o tulpar_aot.exe -L"$LLVM_LIB_DIR" $LIBS $SYS_LIBS

if ($LASTEXITCODE -eq 0) {
    Write-Host "`n✓ Tulpar AOT built successfully!" -ForegroundColor Green
    Write-Host "Running with math.tpr..."
    .\tulpar_aot.exe math.tpr
} else {
    Write-Host "Build failed." -ForegroundColor Red
}
