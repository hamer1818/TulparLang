#!/usr/bin/env bash
# Runtime'i ASAN ile derleyip C kosum takimini kosar.
#
# Paketler gorunur davranisi olcuyor; tampon tasmasi genelde malloc'un bos
# payina dusup SESSIZ kaliyor. Bu betik o sinifi deterministik yakaliyor.
# CI'da degil (ASAN derlemesi ~2 dk); elle kosuluyor ya da runtime'in
# bellege dokunan bir yerini degistirdiginde.
set -e
cd "$(dirname "$0")/.."
BUILD=build-asan
cmake -S . -B "$BUILD" -DCMAKE_BUILD_TYPE=RelWithDebInfo \
      -DCMAKE_C_FLAGS="-fsanitize=address -fno-omit-frame-pointer" \
      -DCMAKE_CXX_FLAGS="-fsanitize=address -fno-omit-frame-pointer" >/dev/null
cmake --build "$BUILD" -j"$(nproc)" --target tulpar_runtime >/dev/null
c++ -fsanitize=address -fno-omit-frame-pointer -g -O1 \
    tests/runtime_asan.c "$BUILD/libtulpar_runtime.a" \
    -o "$BUILD/runtime_asan" -lpthread -ldl -lm $(pkg-config --libs openssl 2>/dev/null || echo "-lssl -lcrypto")
ASAN_OPTIONS=detect_leaks=0 "$BUILD/runtime_asan"
