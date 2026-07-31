#!/bin/bash
# ============================================================
# Tame Web — WASM arşivlerini üretir (Tulpar oyunları tarayıcıda)
#
# Çıktılar (wasm/dist/):
#   libtulpar_runtime_web.a — Tulpar runtime'ının Emscripten derlemesi
#   libtulpar_tame_web.a    — raylib PLATFORM_WEB + aot_tm_* binding'leri
#
# Bunlar `tulpar build --target=web oyun.tpr` link adımında kullanılır
# (bkz. src/aot/aot_pipeline.cpp web yolu). Ana CMake build'ine dokunmaz.
#
# Notlar:
# - tulpar_async.cpp derlenmez (ucontext — Emscripten'de yok). Async
#   kullanan program web hedefinde link hatası alır; oyunlar kullanmaz.
# - TLS (OpenSSL) kapalı; http_fetch düz-soket yolu derlenir ama tarayıcıda
#   ham soket çalışmaz (Emscripten stub). Oyunlar için sorun değil.
# - raylib web'de rglfw.c DERLENMEZ: Emscripten'in kendi GLFW JS
#   implementasyonu link'te -sUSE_GLFW=3 ile gelir.
# ============================================================
set -e
cd "$(dirname "$0")"

# emsdk aktivasyonu (vendored)
if ! command -v emcc &>/dev/null; then
    if [ -f "./emsdk/emsdk_env.sh" ]; then
        # shellcheck disable=SC1091
        source "./emsdk/emsdk_env.sh" >/dev/null 2>&1
    fi
fi
if ! command -v emcc &>/dev/null; then
    echo "HATA: emcc bulunamadı. 'source wasm/emsdk/emsdk_env.sh' çalıştırın."
    exit 1
fi
echo "emcc: $(emcc --version | head -1)"

ROOT=".."
OUT="dist"
OBJ="$OUT/obj"
rm -rf "$OUT"
mkdir -p "$OBJ"

CXXFLAGS="-O2 -std=c++17 -I$ROOT/src -I$ROOT/runtime -DTULPAR_RUNTIME_ONLY -DPLATFORM_LINUX -Wno-deprecated-declarations"
CFLAGS="-O2 -I$ROOT/src -DTULPAR_RUNTIME_ONLY"

echo ""
echo "[1/3] Tulpar runtime (web) derleniyor..."
RUNTIME_CPP=(
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
    "web_stubs.cpp"
)
for f in "${RUNTIME_CPP[@]}"; do
    o="$OBJ/$(basename "${f%.*}").o"
    echo "  CXX $(basename "$f")"
    em++ $CXXFLAGS -c "$f" -o "$o"
done
echo "  CC  cJSON.c"
emcc $CFLAGS -c "$ROOT/runtime/cJSON.c" -o "$OBJ/cJSON.o"
echo "  CC  sqlite3.c"
emcc -O2 -DSQLITE_OMIT_LOAD_EXTENSION -DSQLITE_THREADSAFE=0 -Wno-unused-but-set-variable \
    -c "$ROOT/lib/sqlite3/sqlite3.c" -o "$OBJ/sqlite3.o"

emar rcs "$OUT/libtulpar_runtime_web.a" "$OBJ"/*.o
echo "  -> $OUT/libtulpar_runtime_web.a"

echo ""
echo "[2/3] raylib (PLATFORM_WEB) + tame binding'leri derleniyor..."
RAYLIB_DIR="$ROOT/lib/raylib"
RAYFLAGS="-O2 -DPLATFORM_WEB -DGRAPHICS_API_OPENGL_ES2 -I$RAYLIB_DIR -w"
mkdir -p "$OBJ/tame"
for m in rcore rshapes rtextures rtext rmodels raudio utils; do
    echo "  CC  $m.c"
    emcc $RAYFLAGS -c "$RAYLIB_DIR/$m.c" -o "$OBJ/tame/$m.o"
done
echo "  CC  tame_impl.c"
# RAYFLAGS şart (android/build_tame_android.sh'in yaptığı gibi): bayraksız
# derlenince tame_impl.c platform koşullarını göremez — ışık shader'ı sessizce
# MASAÜSTÜ (GLSL 330) varyantını seçip WebGL'de derlenmiyordu.
emcc $RAYFLAGS -c "$ROOT/runtime/tame_impl.c" -o "$OBJ/tame/tame_impl.o"
echo "  CXX tame_bindings.cpp"
em++ $CXXFLAGS -c "$ROOT/runtime/tame_bindings.cpp" -o "$OBJ/tame/tame_bindings.o"

emar rcs "$OUT/libtulpar_tame_web.a" "$OBJ/tame"/*.o
echo "  -> $OUT/libtulpar_tame_web.a"

echo ""
echo "[3/3] Özet"
ls -lh "$OUT"/*.a
echo ""
echo "Tamam. 'tulpar build --target=web oyun.tpr' bu arşivleri wasm/dist/ altında arar."
