#!/bin/bash
# ============================================================
# Tulpar iOS — runtime statik arsivi (macOS + Xcode komut satiri araclari).
#
# Cikti: ios/dist/<sdk>/libtulpar_runtime_ios.a, sdk = iphoneos (cihaz) ve
# iphonesimulator (Apple silicon simulatoru). Ikisi de yalniz arm64.
# `tulpar build --target=ios oyun.tpr` link adiminda bunlari arar
# (bkz. src/aot/aot_pipeline.cpp iOS yolu; TULPAR_IOS_LIB_DIR ezer).
#
# android/build_tame_android.sh'in kardesi; ayni kaynak listesi. Fark:
# - Apple SDK'si yalniz macOS'ta: bu betik Linux'ta KOSMAZ (nesne uretimi
#   ise her yerde olur — derleyici Mach-O nesnesini SDK'siz yazar).
# - iOS'ta system()/popen()/fork yok (SDK "unavailable" der): runtime bunlari
#   TARGET_OS_IPHONE altinda -1 dondurur (sys_run, http_fetch'in curl yolu).
# - tame (raylib) DERLENMEZ: iOS'ta grafik yerel eklentiden (tulpar-engine).
# ============================================================
set -euo pipefail
cd "$(dirname "$0")"
command -v xcrun >/dev/null || { echo "HATA: xcrun yok (macOS + Xcode gerekir)"; exit 1; }
ROOT=".."
MIN="${TULPAR_IOS_MIN:-15.0}"
SDKS="${TULPAR_IOS_SDKS:-iphoneos iphonesimulator}"
for SDK in $SDKS; do
  case "$SDK" in
    iphoneos) TRIPLE="arm64-apple-ios$MIN" ;;
    iphonesimulator) TRIPLE="arm64-apple-ios$MIN-simulator" ;;
    *) echo "HATA: bilinmeyen sdk $SDK"; exit 1 ;;
  esac
  SYSROOT="$(xcrun --sdk "$SDK" --show-sdk-path)"
  CC="xcrun -sdk $SDK clang -target $TRIPLE -isysroot $SYSROOT"
  CXX="xcrun -sdk $SDK clang++ -target $TRIPLE -isysroot $SYSROOT"
  OUT="dist/$SDK"; OBJ="$OUT/obj"
  rm -rf "$OBJ"; rm -f "$OUT/libtulpar_runtime_ios.a"; mkdir -p "$OBJ"
  CXXFLAGS="-O2 -std=c++17 -fPIC -I$ROOT/src -I$ROOT/runtime -DTULPAR_RUNTIME_ONLY -DTULPAR_TARGET_IOS -Wno-deprecated-declarations"
  CFLAGS="-O2 -fPIC -I$ROOT/src -DTULPAR_RUNTIME_ONLY -DTULPAR_TARGET_IOS"
  echo "=== $SDK ($TRIPLE)"
  RUNTIME_CPP=(
    "$ROOT/src/common/localization.cpp"
    "$ROOT/src/common/diagnostics.cpp"
    "$ROOT/src/common/http_fetch.cpp"
    "$ROOT/src/vm/runtime_bindings.cpp"
    "$ROOT/src/vm/runtime_net.cpp"
    "$ROOT/src/vm/runtime_db.cpp"
    "$ROOT/src/vm/vm.cpp"
    "$ROOT/src/vm/bytecode.cpp"
    "$ROOT/runtime/tulpar_arc.cpp"
    "$ROOT/runtime/tulpar_native.cpp"
    "$ROOT/runtime/tulpar_gzip.cpp"
    "$ROOT/src/lexer/lexer.cpp"
    "$ROOT/src/parser/parser.cpp"
    "$ROOT/src/parser/import_alias.cpp"
    "$ROOT/src/pkg/sha256.cpp"
    "$ROOT/runtime/tulpar_async.cpp"
  )
  for f in "${RUNTIME_CPP[@]}"; do
    echo "  CXX $(basename "$f")"
    $CXX $CXXFLAGS -c "$f" -o "$OBJ/$(basename "${f%.*}").o"
  done
  echo "  CC  cJSON.c"
  $CC $CFLAGS -c "$ROOT/runtime/cJSON.c" -o "$OBJ/cJSON.o"
  echo "  CC  sqlite3.c"
  $CC -O2 -fPIC -DSQLITE_OMIT_LOAD_EXTENSION -DSQLITE_THREADSAFE=1 -w -c "$ROOT/lib/sqlite3/sqlite3.c" -o "$OBJ/sqlite3.o"
  xcrun -sdk "$SDK" ar rcs "$OUT/libtulpar_runtime_ios.a" "$OBJ"/*.o
  rm -rf "$OBJ"
  echo "  -> $OUT/libtulpar_runtime_ios.a ($(du -h "$OUT/libtulpar_runtime_ios.a" | cut -f1))"
done
echo "Tamam. 'tulpar build --target=ios oyun.tpr' bu arsivleri ios/dist/<sdk>/ altinda arar."
