@echo off
REM ============================================
REM TulparLang Build Script for Windows
REM ============================================

echo ========================================
echo TulparLang Build Script for Windows
echo ========================================
echo.

REM Check for MinGW
gcc --version >nul 2>nul
if %errorlevel% neq 0 (
    echo ERROR: MinGW ^(gcc^) is required. Please install it.
    echo For example, using Chocolatey: choco install mingw
    exit /b 1
)

echo Building with Makefile...
echo.

REM Clean old build
if exist build (
    rmdir /s /q build
)
if exist tulpar.exe (
    del tulpar.exe
)

REM Create build directory
mkdir build

REM Compile with UTF-8 support
gcc -Wall -Wextra -g -Isrc -c src/lexer/lexer.c -o build/lexer_lexer.o
if %errorlevel% neq 0 goto :build_error
gcc -Wall -Wextra -g -Isrc -c src/parser/parser.c -o build/parser_parser.o
if %errorlevel% neq 0 goto :build_error
gcc -Wall -Wextra -g -Isrc -c src/interpreter/interpreter.c -o build/interpreter_interpreter.o
if %errorlevel% neq 0 goto :build_error
gcc -Wall -Wextra -g -Isrc -c src/main.c -o build/main.o
if %errorlevel% neq 0 goto :build_error

REM Link
gcc build/lexer_lexer.o build/parser_parser.o build/interpreter_interpreter.o build/main.o -o tulpar.exe
if %errorlevel% neq 0 goto :build_error

echo.
echo ========================================
echo BUILD SUCCESSFUL!
echo ========================================
echo.
echo Executable: tulpar.exe
echo To run examples:
echo   tulpar.exe examples\01_hello_world.tpr
exit /b 0

:build_error
echo.
echo ========================================
echo ERROR: Build failed!
echo ========================================
exit /b 1
