#!/bin/bash
# ============================================================
# Tame Android — imzalı APK'yı çalışan cihaza/emülatöre kurar + başlatır
#
# Kullanım:  android/install_run.sh <out>.apk [--screenshot <cikti.png>]
#
# WSL notu: adb Windows tarafında (Android Studio). adb.exe WSL /tmp yolunu
# okuyamaz, o yüzden APK önce Windows temp'e kopyalanır. --screenshot ile
# başlattıktan ~4 sn sonra screencap alınır (headless doğrulama; ekrana
# pencere açılmaz — emülatör zaten kendi penceresinde).
# ============================================================
set -e

APK="${1:?kullanim: install_run.sh <out>.apk [--screenshot cikti.png]}"
SHOT=""
[ "${2:-}" = "--screenshot" ] && SHOT="${3:?--screenshot icin cikti yolu verin}"

PKG="dev.tulparlang.game"
ACT="$PKG/android.app.NativeActivity"

# adb: PATH'ten ya da Windows SDK platform-tools
ADB=""
if command -v adb >/dev/null 2>&1; then
    ADB=adb
else
    for c in "$HOME/Android/Sdk/platform-tools/adb" \
             /mnt/c/Users/*/AppData/Local/Android/Sdk/platform-tools/adb.exe; do
        [ -f "$c" ] && ADB="$c" && break
    done
fi
[ -z "$ADB" ] && { echo "HATA: adb bulunamadi."; exit 1; }

EXE=""; case "$ADB" in *.exe) EXE=".exe";; esac
winpath() { if [ -n "$EXE" ]; then wslpath -w "$1"; else echo "$1"; fi; }

if ! "$ADB" get-state >/dev/null 2>&1; then
    echo "HATA: bagli cihaz/emulator yok. Android Studio'da bir AVD baslatin."
    exit 1
fi

# APK'yı adb'nin okuyabileceği yere koy (interop ise Windows temp).
APK_FOR_ADB="$APK"
if [ -n "$EXE" ]; then
    WINTMP=$(cmd.exe /c "echo %LOCALAPPDATA%\\Temp" 2>/dev/null | tr -d '\r')
    dst="$(wslpath -u "$WINTMP")/$(basename "$APK")"
    cp "$APK" "$dst"
    APK_FOR_ADB="$dst"
fi

echo "[1/3] adb install -r"
"$ADB" install -r "$(winpath "$APK_FOR_ADB")"

echo "[2/3] baslatiliyor: $ACT"
"$ADB" shell am start -n "$ACT" >/dev/null

if [ -n "$SHOT" ]; then
    echo "[3/3] 4 sn sonra ekran goruntusu"
    sleep 4
    "$ADB" exec-out screencap -p > "$SHOT"
    echo "  -> $SHOT ($(du -h "$SHOT" | cut -f1))"
    # canli mi? logcat'ten aktivite/native kontrolu (son 40 satir)
    echo "  --- son logcat (tulpar/tame) ---"
    "$ADB" logcat -d -t 200 2>/dev/null | grep -iE 'tulpar|tame|raylib|NativeActivity|libtulpargame|FATAL|AndroidRuntime' | tail -12 || true
fi
echo "Tamam."
