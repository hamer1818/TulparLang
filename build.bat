@echo off
setlocal EnableDelayedExpansion
REM ============================================
REM TulparLang Build Script for Windows (LLVM Backend)
REM Version 2.1.0
REM ============================================

echo ========================================
echo TulparLang Build Script (LLVM Backend)
echo ========================================
echo.

REM Parse arguments
set "ACTION=%1"
set "TARGET=%2"

if "%ACTION%"=="clean" goto :clean
if "%ACTION%"=="test" goto :test_only
goto :build

:clean
echo Cleaning build artifacts...
if exist build rmdir /s /q build 2>nul
if exist tulpar.exe del tulpar.exe 2>nul
if exist a.out.exe del a.out.exe 2>nul
if exist a.out del a.out 2>nul
if exist a.out.ll del a.out.ll 2>nul
if exist a.out.o del a.out.o 2>nul
echo Clean complete.
exit /b 0

:build
echo Checking dependencies...
echo.

REM Check CMake
cmake --version >nul 2>nul
if %errorlevel% neq 0 (
    echo ERROR: CMake is required.
    echo Install from: https://cmake.org/download/
    echo Or use: winget install Kitware.CMake
    exit /b 1
)
echo   CMake: OK

REM Check for LLVM (via llvm-config or cmake find)
where llvm-config >nul 2>nul
if %errorlevel% neq 0 (
    REM Try common LLVM paths
    if exist "C:\Program Files\LLVM\bin\llvm-config.exe" (
        set "PATH=C:\Program Files\LLVM\bin;%PATH%"
        echo   LLVM: OK (found in Program Files)
    ) else (
        echo.
        echo WARNING: llvm-config not found in PATH.
        echo.
        echo Install LLVM from: https://releases.llvm.org/
        echo Or use MSYS2: pacman -S mingw-w64-x86_64-llvm
        echo.
        echo Attempting build anyway (CMake may find LLVM)...
    )
) else (
    echo   LLVM: OK
)

echo.
echo Building TulparLang...
echo.

if not exist build mkdir build
cd build

REM Configure with CMake
cmake .. -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release
if %errorlevel% neq 0 (
    echo.
    echo ========================================
    echo ERROR: CMake configuration failed!
    echo ========================================
    echo.
    echo Make sure LLVM is installed and findable by CMake.
    echo You may need to set LLVM_DIR environment variable:
    echo   set LLVM_DIR=C:\path\to\llvm\lib\cmake\llvm
    echo.
    cd ..
    exit /b 1
)

REM Build
cmake --build . --config Release
if %errorlevel% neq 0 (
    echo.
    echo ========================================
    echo ERROR: Build failed!
    echo ========================================
    cd ..
    exit /b 1
)

REM Copy executable
copy tulpar.exe ..\tulpar.exe >nul

cd ..

echo.
echo ========================================
echo BUILD SUCCESSFUL!
echo ========================================
echo.
echo Executable: tulpar.exe
echo.
echo Usage:
echo   tulpar.exe --aot file.tpr      - Compile to native
echo   tulpar.exe --aot file.tpr out  - Custom output name
echo   build.bat clean                - Clean artifacts
echo   build.bat test                 - Run all tests
echo.
exit /b 0

:test_only
REM If tulpar.exe doesn't exist, build it first
if not exist tulpar.exe call :build
if %errorlevel% neq 0 exit /b 1

echo.
echo ========================================
echo Running AOT tests...
echo ========================================
echo.

set "TEST_FAILED=0"
set "INPUT_DIR=examples\inputs"
set "SKIP_TESTS=utils.tpr 09_socket_server.tpr 09_socket_client.tpr 11_router_app.tpr 12_threaded_server.tpr"

if not "%TARGET%"=="" (
    if not exist "%TARGET%" (
        echo ERROR: Test file '%TARGET%' not found.
        exit /b 1
    )
    call :run_test "%TARGET%"
) else (
    for %%F in (examples\*.tpr) do (
        set "SKIP=0"
        for %%S in (!SKIP_TESTS!) do (
            if "%%~nxF"=="%%S" (
                set "SKIP=1"
                echo SKIP: %%F
            )
        )
        
        if !SKIP!==0 (
            call :run_test "%%F"
        )
    )
)

if !TEST_FAILED! neq 0 (
    echo.
    echo ========================================
    echo ERROR: Some tests failed!
    echo ========================================
    exit /b 1
)

echo.
echo ========================================
echo ALL TESTS PASSED!
echo ========================================
exit /b 0

:run_test
set "EXAMPLE=%~1"
set "NAME=%~n1"

echo Testing %EXAMPLE%...

REM AOT compile
tulpar.exe --aot "%EXAMPLE%" >nul 2>&1
if %errorlevel% neq 0 (
    echo   FAIL: %EXAMPLE% ^(compilation^)
    set "TEST_FAILED=1"
    exit /b 0
)

REM Run the compiled binary
if exist a.out (
    if exist "!INPUT_DIR!\!NAME!.txt" (
        a.out < "!INPUT_DIR!\!NAME!.txt" >nul 2>&1
    ) else (
        a.out >nul 2>&1
    )
    
    if !errorlevel! neq 0 (
        echo   FAIL: %EXAMPLE% ^(execution^)
        set "TEST_FAILED=1"
    ) else (
        echo   PASS: %EXAMPLE%
    )
) else (
    echo   FAIL: %EXAMPLE% ^(no output^)
    set "TEST_FAILED=1"
)

REM Cleanup
if exist a.out del a.out 2>nul
if exist a.out.ll del a.out.ll 2>nul
if exist a.out.o del a.out.o 2>nul

exit /b 0
