@echo off
setlocal EnableDelayedExpansion
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
    rmdir /s /q build 2>nul
)
if exist tulpar.exe (
    del tulpar.exe 2>nul
    REM If file is still in use, stop build
    if exist tulpar.exe (
        echo.
        echo ERROR: tulpar.exe is in use by another process.
        echo Please close all programs using tulpar.exe and try again.
        echo.
        echo You can check which process is using it with:
        echo   tasklist /FI "IMAGENAME eq tulpar.exe"
        echo.
        exit /b 1
    )
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
echo Running examples test suite...
echo ========================================

set "TEST_FAILED=0"
set "INPUT_DIR=examples\inputs"

REM Hata üreten test dosyaları (kasıtlı olarak hata üretirler, build'de atlanır)
set "SKIP_TESTS=26_error_handling.tpr 26b_error_handling_mod.tpr"

for %%F in (examples\*.tpr) do (
    set "EXAMPLE=%%F"
    set "NAME=%%~nF"
    
    REM Hata ureten testleri atla
    set "SKIP=0"
    for %%S in (!SKIP_TESTS!) do (
        if "%%~nxF"=="%%S" (
            set "SKIP=1"
            echo SKIP: %%F ^(intentional error test^)
        )
    )
    
    if !SKIP!==0 (
        echo Running %%F...
        set "TEMP_OUT=build\temp_!NAME!.out"
        set "TEMP_ERR=build\temp_!NAME!.err"
        
        REM Run test with timeout using PowerShell
        REM Use simple approach: run process and wait, then read files after process ends
        if exist "!INPUT_DIR!\!NAME!.txt" (
            powershell -Command "$proc = Start-Process -FilePath 'tulpar.exe' -ArgumentList '%%F' -PassThru -WindowStyle Hidden -RedirectStandardOutput '!TEMP_OUT!' -RedirectStandardError '!TEMP_ERR!' -RedirectStandardInput '!INPUT_DIR!\!NAME!.txt'; if ($proc) { $proc | Wait-Process -Timeout 15 -ErrorAction SilentlyContinue; if (-not $proc.HasExited) { Stop-Process -Id $proc.Id -Force; Start-Sleep -Seconds 2; exit 1 } else { Start-Sleep -Seconds 1; exit $proc.ExitCode } } else { exit 1 }"
        ) else (
            powershell -Command "$proc = Start-Process -FilePath 'tulpar.exe' -ArgumentList '%%F' -PassThru -WindowStyle Hidden -RedirectStandardOutput '!TEMP_OUT!' -RedirectStandardError '!TEMP_ERR!'; if ($proc) { $proc | Wait-Process -Timeout 15 -ErrorAction SilentlyContinue; if (-not $proc.HasExited) { Stop-Process -Id $proc.Id -Force; Start-Sleep -Seconds 2; exit 1 } else { Start-Sleep -Seconds 1; exit $proc.ExitCode } } else { exit 1 }"
        )
        set "TEST_EXITCODE=!errorlevel!"
        
        REM Check if tulpar.exe is still running (timeout occurred)
        tasklist /FI "IMAGENAME eq tulpar.exe" 2>nul | find /I "tulpar.exe" >nul
        if !errorlevel!==0 (
            echo ERROR: %%F timed out after 15 seconds ^(infinite loop detected^)
            taskkill /F /IM tulpar.exe >nul 2>&1
            timeout /t 1 /nobreak >nul 2>&1
            echo.
            echo Error output:
            if exist "!TEMP_ERR!" type "!TEMP_ERR!"
            echo.
            echo Output ^(partial^):
            if exist "!TEMP_OUT!" (
                powershell -Command "Get-Content '!TEMP_OUT!' -TotalCount 50"
            )
            echo.
            echo ----------------------------------------
            set "TEST_FAILED=1"
        ) else (
            if !TEST_EXITCODE! neq 0 (
                echo ERROR: %%F failed.
                echo.
                echo Error output:
                if exist "!TEMP_ERR!" type "!TEMP_ERR!"
                echo.
                echo Output:
                if exist "!TEMP_OUT!" type "!TEMP_OUT!"
                echo.
                echo ----------------------------------------
                set "TEST_FAILED=1"
            ) else (
                echo OK: %%F
            )
        )
        
        REM Cleanup
        timeout /t 1 /nobreak >nul 2>&1
        if exist "!TEMP_OUT!" del "!TEMP_OUT!" 2>nul
        if exist "!TEMP_ERR!" del "!TEMP_ERR!" 2>nul
    )
)

if !TEST_FAILED! neq 0 goto :test_error

echo.
echo ========================================
echo BUILD SUCCESSFUL!
echo ========================================
echo.
echo Executable: tulpar.exe
echo To run examples:
echo   tulpar.exe examples\01_hello_world.tpr
exit /b 0

:test_error
echo.
echo ========================================
echo ERROR: Example tests failed!
echo ========================================
exit /b 1

:build_error
echo.
echo ========================================
echo ERROR: Build failed!
echo ========================================
exit /b 1
