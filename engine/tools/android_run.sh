#!/bin/bash
# ============================================================
# Tulpar Engine — telefonda kosum (adb). NDK ile derler, NativeActivity APK'sini
# paketler (android/package_apk.sh), kurar, secilen kipte baslatir ve logu toplar.
#
#   engine/tools/android_run.sh tests [filtre]   # engine_tests APK surecinde
#   engine/tools/android_run.sh demo [kare] [--screenshot cikti.png]
#
# Neden APK: adb shell'deki ikili GPU'yu goremiyor (vkEnumeratePhysicalDevices 0,
# SELinux/HAL, Huawei P20 Pro ile olculdu 2026-09-14). Uygulama sureci gorur.
# Cikti: logcat "tulpar" etiketi + files/engine_log.txt (run-as ile cekilir).
# ============================================================
set -e
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
MODE="${1:-tests}"; shift || true
NDK="${TULPAR_ANDROID_NDK:-}"
if [ -z "$NDK" ]; then
    NDK=$(ls -d "$HOME"/Android/Sdk/ndk/* "$HOME"/Android/android-ndk-* 2>/dev/null | sort -V | head -1)
fi
[ -z "$NDK" ] && { echo "HATA: NDK yok (TULPAR_ANDROID_NDK)"; exit 1; }
ABI="${TULPAR_ANDROID_ABI:-arm64-v8a}"
# ABI basina ayri build dizini (emulator x86_64, telefon arm64-v8a).
if [ "$ABI" = "arm64-v8a" ]; then BUILD="$ROOT/build-android"; else BUILD="$ROOT/build-android-$ABI"; fi
PKG="dev.tulparlang.engine"
ACT="$PKG/android.app.NativeActivity"

echo "[1/5] derleme (NDK: $(basename "$NDK"), $ABI)"
cmake -S "$ROOT/engine" -B "$BUILD" -DCMAKE_TOOLCHAIN_FILE="$NDK/build/cmake/android.toolchain.cmake" \
      -DANDROID_ABI="$ABI" -DANDROID_PLATFORM=android-26 -DCMAKE_BUILD_TYPE=Release -DENGINE_TRACY="${TULPAR_TRACY:-OFF}" -DENGINE_SWAPPY="${TULPAR_SWAPPY:-OFF}" >/dev/null 2>&1
cmake --build "$BUILD" -j --target tulparengine engine_tests 2>&1 | grep -E "error|warning: unused" | grep -v third_party || true
[ -f "$BUILD/libtulparengine.so" ] || { echo "HATA: libtulparengine.so uretilmedi"; exit 1; }

echo "[2/5] paketleme"
STAGE="$BUILD/tulparengine_apk"
rm -rf "$STAGE"; mkdir -p "$STAGE/lib/$ABI"
cp "$BUILD/libtulparengine.so" "$STAGE/lib/$ABI/"
# Dogrulama katmani (Khronos, BestPractices+Arm = Mali linter): tests kipinde ve
# TULPAR_VALIDATION=1 ile APK'ya girer; yukleyici debuggable uygulamanin lib
# dizininden katman alir (Android 9+). Yoksa test ATLANDI der (sessiz yesil yok).
VVL="$ROOT/engine/third_party/vvl-android/$ABI/libVkLayer_khronos_validation.so"
if [ "$MODE" = tests ] || [ "${TULPAR_VALIDATION:-0}" = 1 ]; then
    if [ -f "$VVL" ]; then cp "$VVL" "$STAGE/lib/$ABI/"; echo "  dogrulama katmani APK'da ($(du -h "$VVL" | cut -f1))"
    else echo "  UYARI: dogrulama katmani yok -> engine/tools/fetch_vvl_android.sh (BestPractices testi ATLANDI olacak)"; fi
fi
cp "$ROOT/engine/platform/android/AndroidManifest.xml" "$STAGE/"
mkdir -p "$STAGE/assets" && cp "$ROOT"/engine/tests/assets/* "$ROOT"/engine/assets/fonts/*.ttf "$STAGE/assets/" # APK icine (host cikarir)
"$ROOT/android/package_apk.sh" "$STAGE" "$BUILD/tulparengine.apk" | grep -E "^\s+\+|HATA|apk" || true

echo "[3/5] kurulum"
adb install -r "$BUILD/tulparengine.apk" | tail -1
adb shell am force-stop "$PKG" || true

FILTER=""; FRAMES="600"; SHOT=""
while [ $# -gt 0 ]; do
    case "$1" in
        --screenshot) SHOT="$2"; shift 2;;
        *) if [ "$MODE" = tests ]; then FILTER="$1"; else FRAMES="$1"; fi; shift;;
    esac
done
# Bos deger adb shell'de kaybolur: tirnakla.
adb shell "setprop debug.tulpar.mode '$MODE'; setprop debug.tulpar.filter '$FILTER'; setprop debug.tulpar.frames '$FRAMES'; setprop debug.tulpar.present '${TULPAR_PRESENT:-fifo}'; setprop debug.tulpar.prerotate '${TULPAR_PREROTATE:-1}'; setprop debug.tulpar.size '${TULPAR_SIZE:-2159x1080}'; setprop debug.tulpar.validation '${TULPAR_VALIDATION:-0}'; setprop debug.tulpar.audio '${TULPAR_AUDIO:-0}'; setprop debug.tulpar.swappy '$([ "${TULPAR_SWAPPY:-OFF}" = ON ] && echo 1 || echo 0)'"

if [ "${TULPAR_TRACY:-OFF}" = ON ]; then adb forward tcp:8086 tcp:8086 >/dev/null && echo "  tracy: adb forward 8086 (masaustunde tracy-capture -a 127.0.0.1)"; fi
echo "[4/5] baslat: $MODE"
# Cikti dosyasi: uygulamanin harici dizini (adb pull ile okunur; logcat halkasi
# Huawei'de dakikalar icinde tasiyor — dosya asil kaynak, logcat yedek).
EXT="/sdcard/Android/data/$PKG/files"
LOGF="$EXT/engine_log.txt"
adb shell "rm -f '$LOGF'" 2>/dev/null || true
adb logcat -G 8M 2>/dev/null || true
adb logcat -c 2>/dev/null || true
adb shell am start -W -n "$ACT" >/dev/null
START=$(date +%s)
if [ -n "$SHOT" ]; then
    sleep 4
    adb exec-out screencap -p > "$SHOT"
    echo "  ekran goruntusu -> $SHOT"
fi
# Bitisi bekle: dosyada "[android] bitti" ya da cokme (en cok 300 s).
while true; do
    if adb shell "cat '$LOGF' 2>/dev/null" | grep -q "\[android\] bitti"; then break; fi
    if adb logcat -d -b crash 2>/dev/null | grep -q "Fatal signal"; then
        echo "  COKTU (Fatal signal):"; adb logcat -d -b crash 2>/dev/null | grep -E "Fatal signal|backtrace|#[0-9]+ pc" | head -24; break
    fi
    if [ $(( $(date +%s) - START )) -gt 300 ]; then echo "  ZAMAN ASIMI (300 s)"; break; fi
    sleep 2
done

echo "[5/5] log"
OUT="$BUILD/engine_log_${MODE}.txt"
if ! adb pull "$LOGF" "$OUT" >/dev/null 2>&1; then
    echo "  (dosya cekilemedi, logcat yedegi)"
    adb logcat -d -s tulpar:I 2>/dev/null | sed -E 's/^.*tulpar *: //' > "$OUT"
fi
echo "  -> $OUT ($(wc -l < "$OUT") satir); cihaz dizini: $EXT"
grep -E "^engine tests|FAIL|ATLANDI|\[bilgi\]|\[engine_demo\]|\[android\]|\[fatal\]" "$OUT" | head -80
