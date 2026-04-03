@echo off
REM ============================================
REM TulparLang Windows Build Script (LLVM Backend)
REM Version 2.1.0
REM ============================================
REM Requires: Visual Studio 2019/2022, CMake, LLVM 18+

setlocal enabledelayedexpansion

echo ========================================
echo TulparLang Windows Build Script
echo ========================================
echo.

REM Parse arguments
set ACTION=%1
set BUILD_DIR=build-windows

if "%ACTION%"=="clean" (
    echo Cleaning build artifacts...
    if exist %BUILD_DIR% rmdir /s /q %BUILD_DIR% 2>nul
    if exist build rmdir /s /q build 2>nul
    if exist tulpar.exe del /q tulpar.exe 2>nul
    echo Clean complete.
    exit /b 0
)

REM ============================================
REM Check Dependencies
REM ============================================
echo Checking dependencies...

REM Check for CMake
where cmake >nul 2>&1
if %errorlevel% neq 0 (
    echo ERROR: CMake not found. Install from https://cmake.org/
    exit /b 1
)
echo   CMake: OK

REM Check for Visual Studio (MSBuild)
where msbuild >nul 2>&1
if %errorlevel% neq 0 (
    echo ERROR: Visual Studio not found. Install Visual Studio 2019/2022 with C++ workload.
    exit /b 1
)
echo   Visual Studio: OK

REM Check for LLVM (optional, CMake will find it)
where llvm-config >nul 2>&1
if %errorlevel% neq 0 (
    echo WARNING: llvm-config not found in PATH.
    echo Make sure LLVM is installed and CMAKE can find it.
) else (
    echo   LLVM: OK
)

echo.

REM ============================================
REM Build
REM ============================================
echo Building TulparLang for Windows...
if not exist %BUILD_DIR% mkdir %BUILD_DIR%
cd %BUILD_DIR%

echo Running CMake configuration...
cmake .. -G "Visual Studio 17 2022" -A x64 -DCMAKE_BUILD_TYPE=Release
if %errorlevel% neq 0 (
    echo ERROR: CMake configuration failed!
    echo.
    echo Try installing LLVM 18+ from: https://github.com/llvm/llvm-project/releases
    cd ..
    exit /b 1
)

echo.
echo Building project...
cmake --build . --config Release -j
if %errorlevel% neq 0 (
    echo ERROR: Build failed!
    cd ..
    exit /b 1
)

REM Copy executable
echo.
echo Copying executable...
copy Release\tulpar.exe ..\tulpar.exe >nul

cd ..

echo.
echo ========================================
echo BUILD SUCCESSFUL!
echo ========================================
echo.
echo Executable: tulpar.exe
echo.
echo Usage:
echo   tulpar.exe script.tpr              - Run script (AOT mode, native speed)
echo   tulpar.exe --vm script.tpr         - Run in VM mode
echo   tulpar.exe build script.tpr        - Build standalone executable
echo   tulpar.exe --repl                  - Interactive REPL
echo.
echo   build.bat clean                    - Clean build artifacts
