#!/bin/bash

# Tulpar WebAssembly Build Script
# Emscripten SDK gereklidir

set -e

echo "=========================================="
echo "Tulpar WebAssembly Build"
echo "=========================================="

# Eğer repo içinde emsdk varsa otomatik yükle
if [ -f "./emsdk/emsdk_env.sh" ]; then
    # shellcheck disable=SC1091
    source "./emsdk/emsdk_env.sh"
fi

# Emscripten kontrolü
if ! command -v emcc &> /dev/null; then
    echo "HATA: Emscripten SDK bulunamadı!"
    echo "Lütfen Emscripten SDK'yı kurun: https://emscripten.org/docs/getting_started/downloads.html"
    exit 1
fi

echo "Emscripten versiyonu:"
emcc --version

# Build dizini oluştur
BUILD_DIR="build_wasm"
if [ -d "$BUILD_DIR" ]; then
    echo "Mevcut build dizini temizleniyor..."
    rm -rf "$BUILD_DIR"
fi

mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

echo ""
echo "CMake konfigürasyonu yapılıyor..."
emcmake cmake ..

echo ""
echo "Derleme başlatılıyor..."
emmake make

echo ""
echo "=========================================="
echo "Build tamamlandı!"
echo "=========================================="
echo ""
echo "Çıktı dosyaları:"
echo "  - tulpar_wasm.js"
echo "  - tulpar_wasm.wasm"
echo ""
echo "Bu dosyaları tulpar-lang-web/public/ klasörüne kopyalayın."

cd ..
