@echo off
REM ============================================
REM TulparLang Windows Build Script (LLVM Backend)
REM Version 2.2.0
REM ============================================
REM Tek bir build-windows klasoru kullanir; her calistirildiginda
REM klasorun ICERIGI silinir, build aynii klasorun icine yapilir.

setlocal enableextensions

echo ========================================
echo TulparLang Windows Build Script
echo ========================================
echo.

set "ACTION=%1"
set "BUILD_DIR=build-windows"

if /I "%ACTION%"=="clean" goto :do_clean
goto :check_deps

:do_clean
echo Cleaning build artifacts...
if exist "%BUILD_DIR%" rmdir /s /q "%BUILD_DIR%" 2>nul
if exist tulpar.exe del /q tulpar.exe 2>nul
if exist a.out del /q a.out 2>nul
if exist a.exe del /q a.exe 2>nul
echo Clean complete.
exit /b 0

:check_deps
echo Checking dependencies...
where cmake >nul 2>&1
if errorlevel 1 (
    echo ERROR: CMake not found. Install from https://cmake.org/
    exit /b 1
)
echo   CMake: OK

set "USE_MINGW=0"
set "LLVM_DIR_USED="
set "VSDEVCMD="

REM Check for MSVC-compatible LLVM
if exist "C:\Program Files\LLVM\lib\cmake\llvm\LLVMConfig.cmake" goto :have_msvc_llvm
goto :try_mingw

:have_msvc_llvm
REM Check if cl.exe is already in PATH
where cl >nul 2>&1
if errorlevel 1 goto :find_vsdevcmd
set "LLVM_DIR_USED=C:\Program Files\LLVM\lib\cmake\llvm"
echo   Toolchain: MSVC + Windows LLVM
goto :do_build

:find_vsdevcmd
if exist "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\Common7\Tools\VsDevCmd.bat" set "VSDEVCMD=C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\Common7\Tools\VsDevCmd.bat"
if exist "C:\Program Files\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat" set "VSDEVCMD=C:\Program Files\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat"
if exist "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat" set "VSDEVCMD=C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat"
if exist "C:\Program Files\Microsoft Visual Studio\2022\Professional\Common7\Tools\VsDevCmd.bat" set "VSDEVCMD=C:\Program Files\Microsoft Visual Studio\2022\Professional\Common7\Tools\VsDevCmd.bat"
if exist "C:\Program Files\Microsoft Visual Studio\2022\Enterprise\Common7\Tools\VsDevCmd.bat" set "VSDEVCMD=C:\Program Files\Microsoft Visual Studio\2022\Enterprise\Common7\Tools\VsDevCmd.bat"

if "%VSDEVCMD%"=="" goto :try_mingw

echo   Toolchain: MSVC (via VsDevCmd) + Windows LLVM
set "LLVM_DIR_USED=C:\Program Files\LLVM\lib\cmake\llvm"
call "%VSDEVCMD%" -arch=x64 -host_arch=x64 >nul
goto :do_build

:try_mingw
if not exist "C:\msys64\mingw64\lib\cmake\llvm\LLVMConfig.cmake" goto :no_toolchain
if not exist "C:\msys64\mingw64\bin\g++.exe" goto :no_toolchain
set "USE_MINGW=1"
set "LLVM_DIR_USED=C:\msys64\mingw64\lib\cmake\llvm"
set "PATH=C:\msys64\mingw64\bin;%PATH%"
echo   Toolchain: MinGW-w64 (MSYS2) + LLVM
goto :do_build

:no_toolchain
echo ERROR: No usable toolchain found.
echo   Need MSVC + Windows LLVM, or MSYS2 mingw64 with LLVM installed.
exit /b 1

:do_build
echo   LLVM_DIR: %LLVM_DIR_USED%
echo.

echo Preparing %BUILD_DIR% (wiping contents)...
if exist "%BUILD_DIR%" rmdir /s /q "%BUILD_DIR%"
mkdir "%BUILD_DIR%"
cd "%BUILD_DIR%"

if "%USE_MINGW%"=="1" goto :cmake_mingw
goto :cmake_msvc

:cmake_msvc
echo Configuring with Visual Studio 17 2022...
cmake .. -G "Visual Studio 17 2022" -A x64 -DCMAKE_BUILD_TYPE=Release -DLLVM_DIR="%LLVM_DIR_USED%"
if errorlevel 1 goto :cmake_failed
echo.
echo Building project...
cmake --build . --config Release -j
if errorlevel 1 goto :build_failed
goto :copy_exe

:cmake_mingw
echo Configuring with MinGW Makefiles...
cmake .. -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release -DLLVM_DIR="%LLVM_DIR_USED%" -DCMAKE_C_COMPILER=gcc -DCMAKE_CXX_COMPILER=g++ -DCMAKE_PREFIX_PATH=C:/msys64/mingw64 -DZLIB_ROOT=C:/msys64/mingw64 -DLibXml2_ROOT=C:/msys64/mingw64
if errorlevel 1 goto :cmake_failed
echo.
echo Building project...
cmake --build . -j
if errorlevel 1 goto :build_failed
goto :copy_exe

:cmake_failed
echo ERROR: CMake configuration failed!
cd ..
exit /b 1

:build_failed
echo ERROR: Build failed!
cd ..
exit /b 1

:copy_exe
echo.
echo Copying executable...
if exist tulpar.exe copy /Y tulpar.exe ..\tulpar.exe >nul
if exist Release\tulpar.exe copy /Y Release\tulpar.exe ..\tulpar.exe >nul
cd ..

if not exist tulpar.exe (
    echo ERROR: tulpar.exe was not produced.
    exit /b 1
)

echo.
echo ========================================
echo BUILD SUCCESSFUL!
echo ========================================
echo Executable: tulpar.exe
exit /b 0
