#!/bin/bash
# Khronos Vulkan dogrulama katmani (Android ikilileri, Apache-2.0) -> gitignore'lu
# engine/third_party/vvl-android/<abi>/. android_run.sh tests kipinde APK'ya koyar;
# BestPractices + Arm kurallari (PerfDoc'un ardili) telefonda kosar.
set -e
VER="${1:-1.4.357.0}"
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
DST="$ROOT/engine/third_party/vvl-android"
URL="https://github.com/KhronosGroup/Vulkan-ValidationLayers/releases/download/vulkan-sdk-$VER/android-binaries-$VER.tar.gz"
TMP="$(mktemp -d)"
echo "indiriliyor: $URL"
curl -sL --fail -o "$TMP/vvl.tar.gz" "$URL"
tar xzf "$TMP/vvl.tar.gz" -C "$TMP"
mkdir -p "$DST"
for abi in arm64-v8a x86_64; do
    mkdir -p "$DST/$abi"
    cp "$TMP/android-binaries-$VER/$abi/libVkLayer_khronos_validation.so" "$DST/$abi/"
done
echo "$VER" > "$DST/VERSION"
rm -rf "$TMP"
ls -la "$DST"/*/
