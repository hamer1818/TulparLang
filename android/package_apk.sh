#!/bin/bash
# ============================================================
# Tame Android — APK paketleyici (staging dizini -> imzalı .apk)
#
# Girdi:  `tulpar build --target=android oyun.tpr <out>`in ürettiği
#         <out>_apk/ staging dizini (lib/<abi>/libtulpargame.so +
#         AndroidManifest.xml).
# Çıktı:  <out>.apk (zipalign'lı + debug-imzalı, emülatör/cihaza kurulabilir).
#
# Kullanım:
#   android/package_apk.sh <out>_apk [<out>.apk]
#
# WSL notu: Android SDK build-tools (aapt2/zipalign/apksigner) ve Java bu
# makinede Windows tarafında (Android Studio). Script onları interop ile
# (.exe/.bat) çağırır ve paketlemeyi Windows-erişilebilir bir temp dizinde
# (%LOCALAPPDATA%\Temp) yürütür; Windows araçlarına `wslpath -w` ile Windows
# yolları verilir. Saf-Linux SDK varsa TULPAR_ANDROID_SDK ile onu göster,
# script .exe yerine düz ikilileri arar.
# ============================================================
set -e

STAGE="${1:?kullanim: package_apk.sh <out>_apk [<out>.apk]}"
STAGE="${STAGE%/}"
OUT_APK="${2:-${STAGE%_apk}.apk}"

if [ ! -f "$STAGE/AndroidManifest.xml" ]; then
    echo "HATA: $STAGE/AndroidManifest.xml yok — once 'tulpar build --target=android' calistirin."
    exit 1
fi

# --- SDK / araç konumları --------------------------------------------------
SDK="${TULPAR_ANDROID_SDK:-}"
if [ -z "$SDK" ]; then
    for c in "$HOME/Android/Sdk" /mnt/c/Users/*/AppData/Local/Android/Sdk; do
        [ -d "$c" ] && SDK="$c" && break
    done
fi
[ -z "$SDK" ] && { echo "HATA: Android SDK bulunamadi (TULPAR_ANDROID_SDK ayarla)."; exit 1; }

# En yüksek build-tools sürümü
BT=$(ls -d "$SDK"/build-tools/* 2>/dev/null | sort -V | tail -1)
[ -z "$BT" ] && { echo "HATA: build-tools yok ($SDK/build-tools)."; exit 1; }
# platform android.jar (herhangi biri iş görür; en yükseği)
ANDROID_JAR=$(ls "$SDK"/platforms/*/android.jar 2>/dev/null | sort -V | tail -1)
[ -z "$ANDROID_JAR" ] && { echo "HATA: android.jar yok ($SDK/platforms/*/)."; exit 1; }

# Windows interop mu, düz Linux mü?
EXE=""
if [ -f "$BT/aapt2.exe" ]; then EXE=".exe"; fi
AAPT2="$BT/aapt2$EXE"
ZIPALIGN="$BT/zipalign$EXE"

# Java (apksigner + keytool). Linux'ta PATH'ten, WSL'de Android Studio jbr.
JAVA=""
KEYTOOL=""
if command -v java >/dev/null 2>&1; then
    JAVA=java; KEYTOOL=keytool
else
    for j in "/mnt/c/Program Files/Android/Android Studio/jbr/bin" \
             "/mnt/c/Program Files/Java"/*/bin; do
        [ -f "$j/java.exe" ] && JAVA="$j/java.exe" && KEYTOOL="$j/keytool.exe" && break
    done
fi
[ -z "$JAVA" ] && { echo "HATA: java bulunamadi (apksigner icin gerekli)."; exit 1; }

echo "SDK:         $SDK"
echo "build-tools: $BT"
echo "android.jar: $(basename "$(dirname "$ANDROID_JAR")")"
echo "java:        $JAVA"

# Windows .exe'lerine yol vermek için yardımcı: interop ise wslpath -w.
winpath() { if [ -n "$EXE" ]; then wslpath -w "$1"; else echo "$1"; fi; }

# --- Çalışma dizini: interop'ta Windows temp, değilse mktemp ---------------
if [ -n "$EXE" ]; then
    WINTMP=$(cmd.exe /c "echo %LOCALAPPDATA%\\Temp" 2>/dev/null | tr -d '\r')
    WORK="$(wslpath -u "$WINTMP")/tulpar_apk_$$"
else
    WORK=$(mktemp -d)
fi
rm -rf "$WORK"; mkdir -p "$WORK"
trap 'rm -rf "$WORK"' EXIT

# --- 1) aapt2 link: manifest (+ varsa res/: uygulama ikonu) -> base APK ----
# tulpar.toml [android] icon=... verdiyse driver, staging'e res/mipmap/
# ic_launcher.png koyar ve manifest @mipmap/ic_launcher'a atifta bulunur —
# o durumda res/ once aapt2 compile ile .flat'lere derlenip link'e girer.
BASE="$WORK/base.apk"
echo ""
RES_FLAT=()
if [ -d "$STAGE/res" ]; then
    echo "[1/4] aapt2 compile (res/) + link (manifest -> APK)"
    COMPILED="$WORK/res_flat"
    mkdir -p "$COMPILED"
    "$AAPT2" compile --dir "$(winpath "$STAGE/res")" -o "$(winpath "$COMPILED")"
    for fl in "$COMPILED"/*.flat; do
        [ -f "$fl" ] && RES_FLAT+=("$(winpath "$fl")")
    done
else
    echo "[1/4] aapt2 link (manifest -> APK)"
fi
"$AAPT2" link -o "$(winpath "$BASE")" \
    --manifest "$(winpath "$STAGE/AndroidManifest.xml")" \
    -I "$(winpath "$ANDROID_JAR")" \
    --min-sdk-version 26 --target-sdk-version 34 \
    ${RES_FLAT[@]+"${RES_FLAT[@]}"}

# --- 2) native kütüphaneler (STORED) + oyun asset'leri ---------------------
# assets/: driver'ın stage'e koyduğu ses/sprite/font dosyaları. aapt2 -A
# KULLANILMAZ — Windows aapt2'si girdi adlarını ters bölüyle yazıp
# AAssetManager eşleşmesini bozuyor; python zipfile ile düz '/' garanti.
echo "[2/4] lib/<abi>/*.so + assets/ ekleniyor"
python3 - "$BASE" "$STAGE" <<'PY'
import os, sys, zipfile
base, stage = sys.argv[1], sys.argv[2]
libroot = os.path.join(stage, "lib")
with zipfile.ZipFile(base, "a", zipfile.ZIP_STORED) as z:
    for abi in sorted(os.listdir(libroot)):
        so = os.path.join(libroot, abi, "libtulpargame.so")
        if os.path.isfile(so):
            arc = "lib/%s/libtulpargame.so" % abi
            zi = zipfile.ZipInfo(arc)
            zi.compress_type = zipfile.ZIP_STORED
            zi.external_attr = 0o755 << 16
            with open(so, "rb") as f:
                z.writestr(zi, f.read())
            print("  +", arc, "(%d bytes)" % os.path.getsize(so))
    aroot = os.path.join(stage, "assets")
    if os.path.isdir(aroot):
        for dirpath, _dirs, files in os.walk(aroot):
            for fn in sorted(files):
                full = os.path.join(dirpath, fn)
                rel = os.path.relpath(full, aroot).replace(os.sep, "/")
                arc = "assets/" + rel
                z.write(full, arc, zipfile.ZIP_DEFLATED)
                print("  +", arc, "(%d bytes)" % os.path.getsize(full))
PY

# --- 3) zipalign (16KB sayfa hizası: -P 16; .so STORED zaten) --------------
ALIGNED="$WORK/aligned.apk"
echo "[3/4] zipalign -P 16"
"$ZIPALIGN" -P 16 -f 4 "$(winpath "$BASE")" "$(winpath "$ALIGNED")"

# --- 4) imzala: RELEASE keystore (env verilmisse) ya da debug ---------------
# Play Store'a yuklenebilir imza icin kendi keystore'unu goster:
#   TULPAR_ANDROID_KEYSTORE=<yol>       (zorunlu — release modunu acar)
#   TULPAR_ANDROID_KS_PASS=<parola>     (zorunlu)
#   TULPAR_ANDROID_KEY_ALIAS=<alias>    (zorunlu)
#   TULPAR_ANDROID_KEY_PASS=<parola>    (opsiyonel; varsayilan: KS_PASS)
# Keystore uretmek icin (bir kez):
#   keytool -genkeypair -keystore release.keystore -alias oyunum \
#           -keyalg RSA -keysize 2048 -validity 10000
if [ -n "${TULPAR_ANDROID_KEYSTORE:-}" ]; then
    KS="$TULPAR_ANDROID_KEYSTORE"
    [ -f "$KS" ] || { echo "HATA: keystore yok: $KS"; exit 1; }
    KS_PASS="${TULPAR_ANDROID_KS_PASS:?HATA: TULPAR_ANDROID_KS_PASS gerekli}"
    KEY_ALIAS="${TULPAR_ANDROID_KEY_ALIAS:?HATA: TULPAR_ANDROID_KEY_ALIAS gerekli}"
    KEY_PASS="${TULPAR_ANDROID_KEY_PASS:-$KS_PASS}"
    echo "[4/4] apksigner ile imzalaniyor (RELEASE: $(basename "$KS"), alias: $KEY_ALIAS)"
    "$JAVA" -jar "$(winpath "$BT/lib/apksigner.jar")" sign \
        --ks "$(winpath "$KS")" --ks-pass "pass:$KS_PASS" \
        --key-pass "pass:$KEY_PASS" --ks-key-alias "$KEY_ALIAS" \
        --out "$(winpath "$OUT_APK")" "$(winpath "$ALIGNED")"
else
    # ~/.android/debug.keystore (yoksa üret). Standart debug parametreleri.
    KS_DIR="$HOME/.android"; mkdir -p "$KS_DIR"
    KS="$KS_DIR/debug.keystore"
    if [ ! -f "$KS" ]; then
        echo "[4/4] debug.keystore uretiliyor"
        "$KEYTOOL" -genkeypair -v -keystore "$(winpath "$KS")" \
            -alias androiddebugkey -keyalg RSA -keysize 2048 -validity 10000 \
            -storepass android -keypass android \
            -dname "CN=Android Debug,O=Android,C=US" >/dev/null 2>&1 || true
    fi
    echo "[4/4] apksigner ile imzalaniyor (debug)"
    "$JAVA" -jar "$(winpath "$BT/lib/apksigner.jar")" sign \
        --ks "$(winpath "$KS")" --ks-pass pass:android --key-pass pass:android \
        --ks-key-alias androiddebugkey \
        --out "$(winpath "$OUT_APK")" "$(winpath "$ALIGNED")"
fi

echo ""
echo "Tamam: $OUT_APK"
ls -lh "$OUT_APK"
echo "Kurulum:  android/install_run.sh $OUT_APK   (ya da adb install -r $OUT_APK)"
