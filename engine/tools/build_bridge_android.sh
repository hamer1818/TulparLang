#!/usr/bin/env bash
# Tulpar Engine koprusunun Android arsivleri: `tulpar build --target=android oyun.tpr`
# (import "engine") android/dist/<abi>/ altinda libtulpar_engine_android.a +
# libengine_*.a arar. Bu betik iki ABI icin NDK'yla derler ve oraya kopyalar.
#   engine/tools/build_bridge_android.sh            # arm64-v8a + x86_64
#   TULPAR_ANDROID_ABI=x86_64 engine/tools/build_bridge_android.sh   # tek ABI
# NDK: TULPAR_ANDROID_NDK ya da ~/Android/Sdk/ndk/* (android_run.sh ile ayni).
# Runtime arsivi (libtulpar_runtime_android.a) android/build_tame_android.sh'tan gelir.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
NDK="${TULPAR_ANDROID_NDK:-}"
if [ -z "$NDK" ]; then
    NDK=$(ls -d "$HOME"/Android/Sdk/ndk/* "$HOME"/Android/android-ndk-* 2>/dev/null | sort -V | head -1 || true)
fi
[ -z "$NDK" ] && { echo "HATA: NDK yok (TULPAR_ANDROID_NDK)"; exit 1; }
ABIS="${TULPAR_ANDROID_ABI:-arm64-v8a x86_64}"
for ABI in $ABIS; do
    if [ "$ABI" = "arm64-v8a" ]; then BUILD="$ROOT/build-android"; else BUILD="$ROOT/build-android-$ABI"; fi
    echo "=== $ABI (NDK $(basename "$NDK")) -> $BUILD"
    cmake -S "$ROOT/engine" -B "$BUILD" -DCMAKE_TOOLCHAIN_FILE="$NDK/build/cmake/android.toolchain.cmake" \
          -DANDROID_ABI="$ABI" -DANDROID_PLATFORM=android-26 -DCMAKE_BUILD_TYPE=Release >/dev/null
    cmake --build "$BUILD" -j --target tulpar_engine_android 2>&1 | grep -E "error|Error" | grep -v third_party && { echo "HATA: derleme"; exit 1; } || true
    DIST="$ROOT/android/dist/$ABI"
    mkdir -p "$DIST"
    for a in tulpar_engine_android engine_content engine_renderer engine_sim engine_rhi engine_audio engine_core engine_platform engine_jolt engine_recast engine_meshopt engine_astcenc; do
        f="$BUILD/lib$a.a"
        [ -f "$f" ] || { echo "HATA: $f yok"; exit 1; }
        cp "$f" "$DIST/"
    done
    [ -f "$DIST/libtulpar_runtime_android.a" ] || echo "UYARI: $DIST/libtulpar_runtime_android.a yok — android/build_tame_android.sh kos"
    ls -la "$DIST"/libtulpar_engine_android.a "$DIST"/libengine_core.a
done
echo "Tamam. 'tulpar build --target=android oyun.tpr' (import \"engine\") bu arsivleri kullanir."
