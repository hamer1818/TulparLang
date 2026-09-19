@echo off
REM ===========================================================================
REM Tulpar Engine baslatici (Windows).
REM
REM NEDEN VAR: motor ikilileri MSYS2 MINGW64 ile derleniyor ve uc DLL'e bagli
REM   (libgcc_s_seh-1.dll, libstdc++-6.dll, libwinpthread-1.dll). Ayrica
REM   masaustu penceresi GLFW ile aciliyor ve glfw3.dll CALISMA ANINDA
REM   dlopen ediliyor (engine/platform/window.cpp). Bunlarin hicbiri
REM   C:\Windows'ta yok; PATH'te mingw64\bin olmadan Explorer'dan cift
REM   tiklayinca STATUS_DLL_NOT_FOUND ile hic acilmaz, ya da acilir ama
REM   "GLFW yok (glfw3.dll): masaustu pencere acilamaz" der.
REM
REM   Calisma dizini de onemli: varlik yollari depo kokune GORELI cozuluyor.
REM
REM KULLANIM:
REM   motor.cmd                 -> editor (varsayilan)
REM   motor.cmd editor          -> sahne editoru
REM   motor.cmd demo            -> 3B demo
REM   motor.cmd test            -> engine_tests (pencere acmaz)
REM   motor.cmd demo --headless 240 --out kare.ppm   -> ek argumanlar gecer
REM ===========================================================================
setlocal

set "MINGW=C:\msys64\mingw64\bin"
if not exist "%MINGW%\glfw3.dll" (
    echo HATA: %MINGW%\glfw3.dll yok.
    echo   Kur:  C:\msys64\usr\bin\bash.exe -lc "pacman -S --needed --noconfirm mingw-w64-x86_64-glfw"
    exit /b 1
)
set "PATH=%MINGW%;%PATH%"

REM Depo kokune gec: varlik yollari buraya goreli.
cd /d "%~dp0"

set "BIN=build-windows\engine"
set "HEDEF=%~1"
if "%HEDEF%"=="" set "HEDEF=editor"
shift

if /i "%HEDEF%"=="editor"   set "EXE=%BIN%\engine_editor.exe"
if /i "%HEDEF%"=="demo"     set "EXE=%BIN%\engine_demo.exe"
if /i "%HEDEF%"=="test"     set "EXE=%BIN%\engine_tests.exe"
if /i "%HEDEF%"=="sahnec"   set "EXE=%BIN%\engine_sahnec.exe"
if /i "%HEDEF%"=="texpack"  set "EXE=%BIN%\engine_texpack.exe"
if /i "%HEDEF%"=="clodbake" set "EXE=%BIN%\engine_clodbake.exe"

if not defined EXE (
    echo Bilinmeyen hedef: %HEDEF%
    echo   Gecerli: editor ^| demo ^| test ^| sahnec ^| texpack ^| clodbake
    exit /b 1
)
if not exist "%EXE%" (
    echo HATA: %EXE% yok — once derleyin:
    echo   C:\msys64\usr\bin\bash.exe -lc "export PATH=/mingw64/bin:$PATH; cd '%CD%' ^&^& cmake --build build-windows -j12"
    exit /b 1
)

REM Kalan argumanlari oldugu gibi ilet (shift sonrasi %*  guvenilir degil).
set "ARGS=%1 %2 %3 %4 %5 %6 %7 %8 %9"
"%EXE%" %ARGS%
endlocal
