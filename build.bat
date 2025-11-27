@echo off
setlocal EnableDelayedExpansion
REM ============================================
REM TulparLang Build Script for Windows
REM ============================================

REM Check for MinGW
gcc --version >nul 2>nul
if %errorlevel% neq 0 (
    echo ERROR: MinGW ^(gcc^) is required. Please install it.
    echo For example, using Chocolatey: choco install mingw
    exit /b 1
)

REM Parse arguments
set "ACTION=%1"
set "TARGET=%2"

if "%ACTION%"=="clean" goto :clean
if "%ACTION%"=="test" goto :test_only
goto :build

:clean
echo Cleaning build artifacts...
if exist build (
    rmdir /s /q build 2>nul
)
if exist tulpar.exe (
    del tulpar.exe 2>nul
)
echo Clean complete.
exit /b 0

:build
echo ========================================
echo Building TulparLang...
echo ========================================
echo.

if not exist build mkdir build

REM Compile with UTF-8 support
REM Note: This is not a true incremental build (make is better for that), 
REM but we avoid cleaning everything first.
gcc -Wall -Wextra -g -Isrc -c src/lexer/lexer.c -o build/lexer_lexer.o
if %errorlevel% neq 0 goto :build_error
gcc -Wall -Wextra -g -Isrc -c src/parser/parser.c -o build/parser_parser.o
if %errorlevel% neq 0 goto :build_error
gcc -Wall -Wextra -g -Isrc -c src/interpreter/interpreter.c -o build/interpreter_interpreter.o
if %errorlevel% neq 0 goto :build_error
gcc -Wall -Wextra -g -Isrc -c src/main.c -o build/main.o
if %errorlevel% neq 0 goto :build_error
gcc -c lib/sqlite3/sqlite3.c -o build/sqlite3.o
if %errorlevel% neq 0 goto :build_error

REM Link
gcc build/lexer_lexer.o build/parser_parser.o build/interpreter_interpreter.o build/main.o build/sqlite3.o -o tulpar.exe -lws2_32
if %errorlevel% neq 0 goto :build_error

echo.
echo ========================================
echo BUILD SUCCESSFUL!
echo ========================================
echo.
echo Executable: tulpar.exe
echo.
echo Usage:
echo   build.bat           - Build only
echo   build.bat clean     - Clean build artifacts
echo   build.bat test      - Run all tests
echo   build.bat test file - Run specific test file
echo.

REM If just building, exit here.
exit /b 0

:test_only
REM If tulpar.exe doesn't exist, build it first
if not exist tulpar.exe call :build
if %errorlevel% neq 0 exit /b 1

echo.
echo ========================================
echo Running tests...
echo ========================================
echo.

set "TEST_FAILED=0"
set "INPUT_DIR=examples\inputs"

REM Hata üreten test dosyaları (kasıtlı olarak hata üretirler, build'de atlanır)
set "SKIP_TESTS=26_error_handling.tpr 26b_error_handling_mod.tpr 32_socket_server.tpr 32_socket_client.tpr 33_socket_wrapper_server.tpr 35_chat_server.tpr 36_async_chat_server.tpr"

REM If a specific target is provided, check if it exists
if not "%TARGET%"=="" (
    if not exist "%TARGET%" (
        echo ERROR: Test file '%TARGET%' not found.
        exit /b 1
    )
    REM Run single test
    call :run_test "%TARGET%"
) else (
    REM Run all tests
    for %%F in (examples\*.tpr) do (
        set "SKIP=0"
        for %%S in (!SKIP_TESTS!) do (
            if "%%~nxF"=="%%S" (
                set "SKIP=1"
                echo SKIP: %%F ^(intentional error test^)
            )
        )
        
        if !SKIP!==0 (
            call :run_test "%%F"
        )
    )
)

if !TEST_FAILED! neq 0 goto :test_error

echo.
echo ========================================
echo ALL TESTS PASSED!
echo ========================================
exit /b 0

:run_test
set "EXAMPLE=%~1"
set "NAME=%~n1"

echo Running %EXAMPLE%...
set "TEMP_OUT=build\temp_!NAME!.out"
set "TEMP_ERR=build\temp_!NAME!.err"

REM Run test with timeout using PowerShell
if exist "!INPUT_DIR!\!NAME!.txt" (
    powershell -Command "$proc = Start-Process -FilePath 'tulpar.exe' -ArgumentList '%EXAMPLE%' -PassThru -WindowStyle Hidden -RedirectStandardOutput '!TEMP_OUT!' -RedirectStandardError '!TEMP_ERR!' -RedirectStandardInput '!INPUT_DIR!\!NAME!.txt'; if ($proc) { $proc | Wait-Process -Timeout 15 -ErrorAction SilentlyContinue; if (-not $proc.HasExited) { Stop-Process -Id $proc.Id -Force; exit 1 } else { exit $proc.ExitCode } } else { exit 1 }"
) else (
    powershell -Command "$proc = Start-Process -FilePath 'tulpar.exe' -ArgumentList '%EXAMPLE%' -PassThru -WindowStyle Hidden -RedirectStandardOutput '!TEMP_OUT!' -RedirectStandardError '!TEMP_ERR!'; if ($proc) { $proc | Wait-Process -Timeout 15 -ErrorAction SilentlyContinue; if (-not $proc.HasExited) { Stop-Process -Id $proc.Id -Force; exit 1 } else { exit $proc.ExitCode } } else { exit 1 }"
)
set "TEST_EXITCODE=!errorlevel!"

REM Check if tulpar.exe is still running (timeout occurred)
tasklist /FI "IMAGENAME eq tulpar.exe" 2>nul | find /I "tulpar.exe" >nul
if !errorlevel!==0 (
    echo ERROR: %EXAMPLE% timed out after 15 seconds ^(infinite loop detected^)
    taskkill /F /IM tulpar.exe >nul 2>&1
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
        echo ERROR: %EXAMPLE% failed.
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
        echo OK: %EXAMPLE%
    )
)

REM Cleanup
if exist "!TEMP_OUT!" del "!TEMP_OUT!" 2>nul
if exist "!TEMP_ERR!" del "!TEMP_ERR!" 2>nul
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
