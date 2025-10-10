@echo off
REM ============================================
REM OLang Build Script for Windows
REM ============================================

echo ========================================
echo OLang Build Script for Windows
echo ========================================
echo.

REM Check if build directory exists
if exist build (
    echo Cleaning old build...
    rmdir /s /q build
)

REM Create build directory
mkdir build
cd build

REM Configure with CMake
echo.
echo Configuring with CMake...
cmake .. -G "MinGW Makefiles"
if %errorlevel% neq 0 (
    echo ERROR: CMake configuration failed!
    cd ..
    exit /b 1
)

REM Build
echo.
echo Building OLang...
cmake --build . --config Release
if %errorlevel% neq 0 (
    echo ERROR: Build failed!
    cd ..
    exit /b 1
)

REM Copy executable to root
echo.
echo Copying executable...
copy olang.exe ..\olang.exe

cd ..

echo.
echo ========================================
echo BUILD SUCCESSFUL!
echo ========================================
echo.
echo Executable: olang.exe
echo.
echo To run examples:
echo   olang.exe examples\01_hello_world.olang
echo.

pause
