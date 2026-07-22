#!/bin/bash
# ============================================================
# Tame Android — NDK statik arşivlerini üretir (Tulpar oyunları Android'de)
#
# Çıktılar (android/dist/<abi>/):
#   libtulpar_runtime_android.a — Tulpar runtime'ının bionic derlemesi
#   libtulpar_tame_android.a    — raylib PLATFORM_ANDROID + native_app_glue
#                                 + aot_tm_* binding'leri
#
# ABI'ler: arm64-v8a (gerçek cihazlar) + x86_64 (Android Studio emülatörü).
# Bunlar `tulpar build --target=android oyun.tpr` link adımında kullanılır
# (bkz. src/aot/aot_pipeline.cpp android yolu). Ana CMake build'ine dokunmaz.
#
# NDK: TULPAR_ANDROID_NDK env'i, yoksa ~/Android/android-ndk-* aranır.
# wasm/build_tame_web.sh'nin birebir kardeşi. Notlar:
# - tulpar_async.cpp derlenmez (bionic'te makecontext/swapcontext yok —
#   Emscripten'dekiyle aynı sınırlama). android_stubs.cpp koşulsuz
#   aot_event_loop_run() referansını karşılar; async KULLANAN program
#   link hatası alır (doğru davranış).
# - raylib'de rglfw.c DERLENMEZ (masaüstü); pencere/girdi
#   platforms/rcore_android.c + NDK native_app_glue üzerinden gelir.
# - Android 15+ 16KB sayfa imajları için .so linkinde
#   -Wl,-z,max-page-size=16384 gerekir — o bayrak Faz 2'nin link
#   satırında (buradaki arşivler etkilenmez).
# ============================================================
set -e
cd "$(dirname "$0")"

API=34

NDK="${TULPAR_ANDROID_NDK:-}"
if [ -z "$NDK" ]; then
    NDK=$(ls -d "$HOME"/Android/android-ndk-* 2>/dev/null | sort | tail -1)
fi
TC="$NDK/toolchains/llvm/prebuilt/linux-x86_64/bin"
if [ ! -x "$TC/llvm-ar" ]; then
    echo "HATA: NDK bulunamadı (TULPAR_ANDROID_NDK ayarla ya da ~/Android/android-ndk-* kur)."
    exit 1
fi
echo "NDK: $NDK (API $API)"

ROOT=".."
GLUE="$NDK/sources/android/native_app_glue"

build_abi() {
    local abi="$1" triple="$2"
    local CC="$TC/${triple}${API}-clang"
    local CXX="$TC/${triple}${API}-clang++"
    local AR="$TC/llvm-ar"
    local OUT="dist/$abi"
    local OBJ="$OUT/obj"
    rm -rf "$OUT"
    mkdir -p "$OBJ/tame"

    local CXXFLAGS="-O2 -std=c++17 -fPIC -I$ROOT/src -I$ROOT/runtime -DTULPAR_RUNTIME_ONLY -DPLATFORM_LINUX -Wno-deprecated-declarations"
    local CFLAGS="-O2 -fPIC -I$ROOT/src -DTULPAR_RUNTIME_ONLY"

    echo ""
    echo "=== ABI: $abi ($triple) ==="
    echo "[1/3] Tulpar runtime (android) derleniyor..."
    local RUNTIME_CPP=(
        "$ROOT/src/common/localization.cpp"
        "$ROOT/src/common/diagnostics.cpp"
        "$ROOT/src/common/http_fetch.cpp"
        "$ROOT/src/vm/runtime_bindings.cpp"
        "$ROOT/src/vm/vm.cpp"
        "$ROOT/src/vm/bytecode.cpp"
        "$ROOT/runtime/tulpar_arc.cpp"
        "$ROOT/runtime/tulpar_native.cpp"
        "$ROOT/runtime/tulpar_gzip.cpp"
        "$ROOT/src/lexer/lexer.cpp"
        "$ROOT/src/parser/parser.cpp"
        "$ROOT/src/parser/import_alias.cpp"
        "$ROOT/src/pkg/sha256.cpp"
        "android_stubs.cpp"
    )
    for f in "${RUNTIME_CPP[@]}"; do
        local o="$OBJ/$(basename "${f%.*}").o"
        echo "  CXX $(basename "$f")"
        "$CXX" $CXXFLAGS -c "$f" -o "$o"
    done
    echo "  CC  cJSON.c"
    "$CC" $CFLAGS -c "$ROOT/runtime/cJSON.c" -o "$OBJ/cJSON.o"
    echo "  CC  sqlite3.c"
    "$CC" -O2 -fPIC -DSQLITE_OMIT_LOAD_EXTENSION -DSQLITE_THREADSAFE=1 \
        -Wno-unused-but-set-variable \
        -c "$ROOT/lib/sqlite3/sqlite3.c" -o "$OBJ/sqlite3.o"

    "$AR" rcs "$OUT/libtulpar_runtime_android.a" "$OBJ"/*.o
    echo "  -> $OUT/libtulpar_runtime_android.a"

    echo "[2/3] raylib (PLATFORM_ANDROID) + native_app_glue + tame binding'leri..."
    local RAYLIB_DIR="$ROOT/lib/raylib"
    local RAYFLAGS="-O2 -fPIC -DPLATFORM_ANDROID -DGRAPHICS_API_OPENGL_ES2 -I$RAYLIB_DIR -I$GLUE -w"
    for m in rcore rshapes rtextures rtext rmodels raudio utils; do
        echo "  CC  $m.c"
        "$CC" $RAYFLAGS -c "$RAYLIB_DIR/$m.c" -o "$OBJ/tame/$m.o"
    done
    echo "  CC  android_native_app_glue.c"
    "$CC" -O2 -fPIC -I"$GLUE" -c "$GLUE/android_native_app_glue.c" \
        -o "$OBJ/tame/android_native_app_glue.o"
    echo "  CC  tame_impl.c"
    "$CC" -O2 -fPIC -I"$RAYLIB_DIR" -c "$ROOT/runtime/tame_impl.c" \
        -o "$OBJ/tame/tame_impl.o"
    echo "  CXX tame_bindings.cpp"
    "$CXX" $CXXFLAGS -c "$ROOT/runtime/tame_bindings.cpp" -o "$OBJ/tame/tame_bindings.o"

    "$AR" rcs "$OUT/libtulpar_tame_android.a" "$OBJ/tame"/*.o
    echo "  -> $OUT/libtulpar_tame_android.a"
}

build_abi "arm64-v8a" "aarch64-linux-android"
build_abi "x86_64"    "x86_64-linux-android"

echo ""
echo "[3/3] Özet"
ls -lh dist/*/*.a
echo ""
echo "Tamam. 'tulpar build --target=android oyun.tpr' bu arşivleri android/dist/<abi>/ altında arar."
