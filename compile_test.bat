@echo off
setlocal

echo Finding LLVM configuration...
for /f "tokens=*" %%i in ('llvm-config --cflags') do set LLVM_CFLAGS=%%i
for /f "tokens=*" %%i in ('llvm-config --ldflags') do set LLVM_LDFLAGS=%%i
for /f "tokens=*" %%i in ('llvm-config --libs core native analysis executionengine') do set LLVM_LIBS=%%i
for /f "tokens=*" %%i in ('llvm-config --system-libs') do set LLVM_SYS_LIBS=%%i

if "%LLVM_CFLAGS%"=="" (
    echo Error: llvm-config not found or failed.
    echo Please ensure LLVM is installed and llvm-config is in your PATH.
    echo Skipping compilation test.
    exit /b 1
)

echo Compiling...
gcc -g -I src src/aot/llvm_test.c src/aot/llvm_backend.c src/aot/llvm_types.c %LLVM_CFLAGS% %LLVM_LDFLAGS% %LLVM_LIBS% %LLVM_SYS_LIBS% -o llvm_test.exe

if %errorlevel% neq 0 (
    echo Compilation failed!
    exit /b 1
)

echo Compilation successful. Running test...
llvm_test.exe
