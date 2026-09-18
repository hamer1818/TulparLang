#!/usr/bin/env bash
# KENDI KENDINE YETEN Windows paketi uretir: zip'le, gonder, ac, calistir.
#
# NEDEN VAR: `build-windows/` klasorunu oldugu gibi zip'lemek YETMEZ — uc sey
# disarida kalir ve ucu de hedef makinede "calismiyor" olarak gorunur:
#   1) DLL'ler. tulpar.exe bes, motor ikilileri uc DLL istiyor (libstdc++-6 ve
#      libgcc_s_seh-1 yalniz motorda: tulpar.exe onlari statik linkliyor).
#   2) LINKLEYICI. AOT ikinci adimda `g++` cagirir; hedef makinede MinGW yoksa
#      "Baglama basarisiz" der. Burada CURATED bir linkleyici paketleniyor
#      (~37 MB): g++ + collect2 + ld + CRT/startup + link zincirinin gercekten
#      dokundugu arsivler. Derleyici (cc1plus, 41 MB) YOK — C++ derlemiyoruz,
#      yalnizca LLVM'in urettigi .o'yu baglıyoruz. Dosya listesi tahmin degil:
#      `-Wl,--trace` ile gercek link kapanisindan cikarildi.
#   3) VARLIKLAR. engine_demo/editor, TULPAR_ENGINE_ASSETS verilmezse DERLEME
#      ZAMANINDA gomulu Linux yoluna duser (ENGINE_SOURCE_DIR) — hedef makinede
#      o yol yoktur. Paketteki .cmd sarmalayicilari degiskeni kuruyor.
#
# Kullanim:
#   windows/package.sh                 # dist-windows/ uretir
#   windows/package.sh --zip           # ayrica dist-windows.zip
#   windows/package.sh --no-toolchain  # linkleyiciyi ATLA (kucuk paket; derleme
#                                      # icin hedefte MSYS2/w64devkit gerekir)
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD="$ROOT/build-windows"
SYS="$ROOT/windows/dist/mingw64"
OUT="$ROOT/dist-windows"
GCCVER=16.2.0
TRIPLE=x86_64-w64-mingw32
WANT_ZIP=0
WANT_TOOLCHAIN=1
for a in "$@"; do
    case "$a" in
        --zip) WANT_ZIP=1 ;;
        --no-toolchain) WANT_TOOLCHAIN=0 ;;
        *) echo "bilinmeyen secenek: $a"; exit 1 ;;
    esac
done

GREEN='\033[0;32m'; RED='\033[0;31m'; YEL='\033[1;33m'; NC='\033[0m'

# GECISLI DLL KAPANISI. Elle tutulan bir DLL listesi bir gun eksik kalir ve
# hata hedef makinede "program aciliyor ama hicbir sey olmuyor" diye gorunur
# (olculdu: linkleyicinin `ld.exe`si libintl-8.dll istiyordu, listede yoktu ve
# ld HIC BASLAMIYORDU — collect2 "ld returned 53" diyordu, sebebi soylemeden).
# Import tablosundan ozyinelemeli cikariliyor; sistem DLL'leri atlanir.
dll_kapanisi() { # <hedef-dizin> <exe...>
    local dest="$1"; shift
    local kuyruk=("$@") gorulen=() n dll src
    while [ ${#kuyruk[@]} -gt 0 ]; do
        n="${kuyruk[0]}"; kuyruk=("${kuyruk[@]:1}")
        [ -f "$n" ] || continue
        for dll in $(llvm-objdump -p "$n" 2>/dev/null | sed -n 's/^[[:space:]]*DLL Name:[[:space:]]*//p' | tr 'A-Z' 'a-z' | sort -u); do
            src="$SYS/bin/$dll"
            [ -f "$src" ] || continue          # sistem DLL'i: Windows'ta zaten var
            case " ${gorulen[*]:-} " in *" $dll "*) continue ;; esac
            gorulen+=("$dll")
            cp -n "$src" "$dest/" 2>/dev/null || true
            kuyruk+=("$src")                   # onun bagimliliklari da gelsin
        done
    done
}
[ -f "$BUILD/tulpar.exe" ] || { echo -e "${RED}HATA:${NC} $BUILD/tulpar.exe yok — once windows/build.sh"; exit 1; }
[ -d "$SYS" ] || { echo -e "${RED}HATA:${NC} sysroot yok — once python3 windows/setup_sysroot.py"; exit 1; }

rm -rf "$OUT"
mkdir -p "$OUT" "$OUT/motor" "$OUT/ornekler"

# --- 1) Derleyici + AOT'un linkte kullandigi arsivler ------------------------
# Arsivler tulpar.exe'nin YANINA konuyor: build_link_search_dirs() once
# calistirilabilirin dizinine bakiyor (src/aot/aot_pipeline.cpp).
cp "$BUILD/tulpar.exe" "$OUT/"
for a in libtulpar_runtime.a libtulpar_tame.a libtulpar_engine.a; do
    [ -f "$BUILD/$a" ] && cp "$BUILD/$a" "$OUT/"
done
if [ -d "$BUILD/engine" ]; then
    cp "$BUILD"/engine/libengine_*.a "$OUT/" 2>/dev/null || true
fi

# --- 2) DLL'ler -------------------------------------------------------------
# Ilk bes tulpar.exe icin, son ikisi motor ikilileri icin (tulpar.exe
# libstdc++/libgcc'yi statik linkliyor, motor linklemiyor).
dll_kapanisi "$OUT" "$OUT/tulpar.exe"

# --- 3) Motor ikilileri + varliklari ----------------------------------------
if [ -d "$BUILD/engine" ]; then
    for e in engine_demo engine_editor engine_sahnec engine_texpack; do
        [ -f "$BUILD/engine/$e.exe" ] && cp "$BUILD/engine/$e.exe" "$OUT/motor/"
    done
    dll_kapanisi "$OUT/motor" "$OUT"/motor/*.exe
    mkdir -p "$OUT/motor/varliklar/assets/fonts" "$OUT/motor/varliklar/tests/assets"
    cp -r "$ROOT/engine/assets/fonts/." "$OUT/motor/varliklar/assets/fonts/" 2>/dev/null || true
    cp -r "$ROOT/engine/tests/assets/." "$OUT/motor/varliklar/tests/assets/" 2>/dev/null || true
fi

# --- 4) Linkleyici (curated) ------------------------------------------------
# YAPI KORUNUR: g++ kendi kutuphanelerini KONUMUNA GORE bulur
# (bin/../lib/gcc/<triple>/<ver>), yani duzlestirmek calismaz.
if [ "$WANT_TOOLCHAIN" = "1" ]; then
    TC="$OUT/linkleyici"
    mkdir -p "$TC/bin" "$TC/lib/gcc/$TRIPLE/$GCCVER" "$TC/$TRIPLE/bin"
    cp "$SYS/bin/g++.exe" "$SYS/bin/ld.exe" "$SYS/bin/ar.exe" "$TC/bin/"
    cp "$SYS/$TRIPLE/bin/ld.exe" "$TC/$TRIPLE/bin/" 2>/dev/null || true
    cp "$SYS/lib/gcc/$TRIPLE/$GCCVER/collect2.exe" "$TC/lib/gcc/$TRIPLE/$GCCVER/"
    # LTO EKLENTISI: GCC surucusu link ederken `-fuse-linker-plugin` gecirir ve
    # eklenti yoksa "liblto_plugin.dll not found" ile DURUR — nesnelerimizde LTO
    # olmasa bile. Olculdu (windows/verify_package.sh, yalitilmis kosum): paketin
    # ilk surumu tam burada dustu. lto1.exe (38 MB) GEREKMEZ: gercek LTO
    # derlemesi yapmiyoruz, yalnizca eklentinin var olmasi isteniyor.
    cp "$SYS/lib/gcc/$TRIPLE/$GCCVER/liblto_plugin.dll" "$TC/lib/gcc/$TRIPLE/$GCCVER/"
    cp "$SYS/lib/gcc/$TRIPLE/$GCCVER/lto-wrapper.exe" "$TC/lib/gcc/$TRIPLE/$GCCVER/"
    mkdir -p "$TC/lib/bfd-plugins"
    cp "$SYS/lib/bfd-plugins/liblto_plugin.dll" "$TC/lib/bfd-plugins/" 2>/dev/null || true
    cp "$SYS/lib/gcc/$TRIPLE/$GCCVER"/{crtbegin.o,crtend.o,libgcc.a,libgcc_eh.a} \
       "$TC/lib/gcc/$TRIPLE/$GCCVER/"
    cp "$SYS/lib"/{crt2.o,default-manifest.o} "$TC/lib/"
    # Link kapanisi: `-Wl,--trace` ile olculdu; tame (opengl32/gdi32/winmm) ve
    # motor (ole32/user32/dbghelp) yollarinin istedikleri de burada.
    for l in advapi32 crypt32 crypto kernel32 mingw32 mingwex msvcrt pthread winpthread \
             shell32 ssl stdc++ user32 ws2_32 wsock32 opengl32 gdi32 winmm ole32 dbghelp \
             version imm32 setupapi cfgmgr32 ; do
        [ -f "$SYS/lib/lib$l.a" ] && cp "$SYS/lib/lib$l.a" "$TC/lib/"
    done
    # Linkleyici ikililerinin KENDI DLL'leri: Windows once ikilinin yanina
    # bakar, bu yuzden paketin kokundekiler yetmez.
    dll_kapanisi "$TC/bin" "$TC/bin"/*.exe "$TC/lib/gcc/$TRIPLE/$GCCVER"/*.exe
fi

# --- 5) Ornekler ------------------------------------------------------------
for e in 01_hello_world.tpr 02_basics.tpr 05_strings.tpr 06_functions.tpr; do
    [ -f "$ROOT/examples/$e" ] && cp "$ROOT/examples/$e" "$OUT/ornekler/"
done
[ -f "$ROOT/examples/engine_ilk_oyun.tpr" ] && cp "$ROOT/examples/engine_ilk_oyun.tpr" "$OUT/ornekler/"

# --- 6) Sarmalayicilar ------------------------------------------------------
# CRLF: Windows'ta Not Defteri ve cmd.exe icin.
w() { printf '%s\r\n' "$@"; }

{
w "@echo off"
w "rem Tulpar surucusu. Linkleyiciyi PATH'e koyar ve TULPAR_CC'yi kurar;"
w "rem boylece paket kendi kendine yeter (hedef makinede MinGW gerekmez)."
w "setlocal"
w "set \"TULPAR_KOK=%~dp0\""
w "if exist \"%TULPAR_KOK%linkleyici\\bin\\g++.exe\" ("
w "  set \"PATH=%TULPAR_KOK%linkleyici\\bin;%PATH%\""
w "  set \"TULPAR_CC=g++\""
w ")"
w "\"%TULPAR_KOK%tulpar.exe\" %*"
} > "$OUT/tulpar.cmd"

{
w "@echo off"
w "rem Motor demosu. Varlik yolu ZORUNLU: verilmezse ikili derleme zamaninda"
w "rem gomulu Linux yoluna duser ve yazi tipi/modeller bulunamaz."
w "setlocal"
w "set \"TULPAR_ENGINE_ASSETS=%~dp0varliklar\""
w "\"%~dp0engine_demo.exe\" %*"
} > "$OUT/motor/demo.cmd"

{
w "@echo off"
w "setlocal"
w "set \"TULPAR_ENGINE_ASSETS=%~dp0varliklar\""
w "\"%~dp0engine_editor.exe\" %*"
} > "$OUT/motor/editor.cmd"

{
w "TULPAR — Windows paketi"
w "======================="
w ""
w "Kurulum YOK: klasoru istediginiz yere acin ve calistirin."
w ""
w "HIZLI BASLANGIC"
w "  tulpar.cmd version                     surum"
w "  tulpar.cmd ornekler\\01_hello_world.tpr  bir program derle ve calistir"
w "  tulpar.cmd build ornekler\\02_basics.tpr oyun.exe   tek dosyalik ikili uret"
w "  tulpar.cmd fmt <dosya.tpr>             bicimlendir"
w "  tulpar.cmd typecheck <dosya.tpr>       tip denetimi"
w ""
w "MOTOR (3B)"
w "  motor\\demo.cmd                         Vulkan demosu (surucu gerekir)"
w "  motor\\demo.cmd --headless 60 --out kare.ppm    pencere acmadan kare uret"
w "  motor\\editor.cmd                       sahne editoru"
w ""
w "NEDEN tulpar.cmd, dogrudan tulpar.exe DEGIL?"
w "  Tulpar programlari NATIF derler: ikinci adimda bir linkleyici cagirir."
w "  tulpar.cmd paketteki linkleyiciyi PATH'e koyar. tulpar.exe'yi dogrudan"
w "  calistirirsaniz derleme adimi 'Baglama basarisiz' der (makinenizde MinGW"
w "  varsa o da calisir)."
w ""
w "GEREKSINIMLER"
w "  * Windows 10 ya da 11, 64 bit."
w "  * Motor icin Vulkan destekli ekran karti surucusu."
w "  * Baska hicbir sey yok: DLL'ler ve linkleyici pakette."
w ""
w "BILINEN SINIRLAR"
w "  * 'tulpar update' curl.exe ister (Windows 10+ ile birlikte gelir)."
w "  * Web/Android hedefleri Windows'tan desteklenmiyor (Linux/macOS'ta calisir)."
} > "$OUT/BENIOKU.txt"

echo
echo -e "${GREEN}paket hazir:${NC} $(realpath --relative-to="$ROOT" "$OUT")"
du -sh "$OUT" | awk '{print "  toplam boyut: " $1}'
[ "$WANT_TOOLCHAIN" = "1" ] && du -sh "$OUT/linkleyici" | awk '{print "  linkleyici  : " $1}'
[ -d "$OUT/motor" ] && du -sh "$OUT/motor" | awk '{print "  motor       : " $1}'

if [ "$WANT_ZIP" = "1" ]; then
    (cd "$ROOT" && rm -f dist-windows.zip && zip -qr dist-windows.zip "$(basename "$OUT")")
    echo -e "${GREEN}zip:${NC} $(du -h "$ROOT/dist-windows.zip" | cut -f1) — dist-windows.zip"
fi
echo
echo -e "${YEL}DOGRULAMA:${NC} windows/verify_package.sh  (paketi YALITILMIS ortamda Wine ile sinar)"
