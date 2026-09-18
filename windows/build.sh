#!/usr/bin/env bash
# Tulpar'i Linux hosttan Windows'a (x86_64-w64-mingw32) capraz derler.
#
#   windows/build.sh              # yapilandir + derle (artimlidir)
#   windows/build.sh clean        # build-windows/ silinir, sifirdan
#   windows/build.sh <hedef>      # tek hedef (orn. tulpar, tulpar_runtime)
#
# Gereksinimler:
#   1) Host capraz GCC:  sudo pacman -S mingw-w64-gcc mingw-w64-binutils \
#                            mingw-w64-crt mingw-w64-headers mingw-w64-winpthreads
#   2) Hedef sysroot:    python3 windows/setup_sysroot.py
#
# Dizin adi BILEREK build-windows: AOT boru hatti Windows'ta calisirken
# libtulpar_runtime.a'yi ararken tam bu adi (ve build-windows/Release'i)
# deniyor — bkz. build_link_search_dirs(), src/aot/aot_pipeline.cpp.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="$ROOT/build-windows"
SYSROOT="$ROOT/windows/dist/mingw64"
TRIPLE=x86_64-w64-mingw32

RED='\033[0;31m'; GREEN='\033[0;32m'; YEL='\033[1;33m'; NC='\033[0m'

# Derleyici: capraz GCC varsa o (MSYS2'nin LLVM'ini derleyen GCC ile ayni
# surum), yoksa host clang + sysroot (sudo istemez). Secimi toolchain dosyasi
# yapiyor; burasi yalniz kullaniciya ne kullanilacagini soyluyor.
HOST_GXX=""
if command -v "$TRIPLE-g++" >/dev/null 2>&1; then
    HOST_GXX="$(command -v "$TRIPLE-g++")"
elif [ -x "$ROOT/windows/dist/host/usr/bin/$TRIPLE-g++" ]; then
    HOST_GXX="$ROOT/windows/dist/host/usr/bin/$TRIPLE-g++"
fi
if [ -n "$HOST_GXX" ]; then
    echo -e "${GREEN}derleyici:${NC} $HOST_GXX ($("$HOST_GXX" -dumpversion))"
elif command -v clang++ >/dev/null 2>&1; then
    # UYARI, tercih degil: clang ile derlenen nesneler GCC'nin libstdc++'iyla
    # karisiyor ve OLCULEN bir hata uretiyor (catch icinden atilan hata
    # `call()` sinirini gecerken surec cokuyor — Tuzaklar 9f).
    echo -e "${RED}UYARI:${NC} capraz GCC yok, host clang++ kullanilacak — BILINEN ABI SORUNU var."
    echo "  Kur:  python3 windows/setup_sysroot.py --host-gcc      (sudo gerekmez)"
    echo "  ya da sudo pacman -S --needed mingw-w64-gcc mingw-w64-binutils mingw-w64-crt mingw-w64-headers mingw-w64-winpthreads"
else
    echo -e "${RED}HATA:${NC} ne $TRIPLE-g++ ne clang++ var."
    exit 1
fi
if [ ! -f "$SYSROOT/lib/cmake/llvm/LLVMConfig.cmake" ]; then
    echo -e "${RED}HATA:${NC} hedef sysroot eksik ($SYSROOT)."
    echo "  python3 windows/setup_sysroot.py"
    exit 1
fi

if [ "${1:-}" = "clean" ]; then
    echo "build-windows/ siliniyor..."
    rm -rf "$BUILD_DIR"
    shift || true
fi

TARGET="${1:-}"
JOBS="$(nproc 2>/dev/null || echo 4)"

cmake -S "$ROOT" -B "$BUILD_DIR" \
    -DCMAKE_TOOLCHAIN_FILE="$ROOT/cmake/toolchain-mingw64.cmake" \
    -DCMAKE_BUILD_TYPE=Release

if [ -n "$TARGET" ]; then
    cmake --build "$BUILD_DIR" --target "$TARGET" -j "$JOBS"
else
    cmake --build "$BUILD_DIR" -j "$JOBS"
fi

echo
for f in tulpar.exe libtulpar_runtime.a libtulpar_tame.a; do
    if [ -f "$BUILD_DIR/$f" ]; then
        printf "${GREEN}%-24s${NC} %s\n" "$f" "$(du -h "$BUILD_DIR/$f" | cut -f1)"
    else
        printf "${YEL}%-24s${NC} yok\n" "$f"
    fi
done
echo
echo "Calistir:  source windows/wine_env.sh && wine build-windows/tulpar.exe version"
