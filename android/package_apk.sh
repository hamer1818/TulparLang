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

# min/target SDK aapt2 bayragiyla VERILMEZ: surucunun yazdigi manifestteki
# <uses-sdk> tek kaynak (tulpar.toml target_sdk / TULPAR_ANDROID_TARGET_SDK).
# Eskiden burada sabit --target-sdk-version 34 vardi; manifest 36 dese bile
# hangisinin kazandigini aapt2 surumu belirliyordu.
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
# Android kutuphaneleri (STAGE/android_lib/, or. tulpar-engine tools/android_google.py: Credential Manager):
#   res/*.zip       aapt2 ile derlenmis kutuphane kaynaklari -> -R (ust uste; ayni ad sonrakinde kalir)
#   paketler.txt    kaynaklari olan kutuphane paketleri -> --extra-packages: R siniflari BU link'in son
#                   kimlikleriyle uretilir (kutuphane dex'i R'yi icermez; R8'e kutuphane olarak verildi)
#   classes*.dex    kutuphane + motor Java kodu (R'siz) -> R dex'iyle birlestirilip APK'ya
# Manifest (hasCode=true, bilesenler) staging'e kutuphaneyi ekleyen arac tarafindan yazilmistir.
LIB="$STAGE/android_lib"
LIB_ARGS=()
if [ -d "$LIB" ]; then
    for z in "$LIB"/res/*.zip; do [ -f "$z" ] && LIB_ARGS+=(-R "$(winpath "$z")"); done
    [ ${#LIB_ARGS[@]} -gt 0 ] && LIB_ARGS+=(--auto-add-overlay)
    if [ -s "$LIB/paketler.txt" ]; then
        mkdir -p "$WORK/gen"
        LIB_ARGS+=(--extra-packages "$(grep -v '^$' "$LIB/paketler.txt" | paste -sd: -)" --java "$(winpath "$WORK/gen")")
    fi
    echo "      android_lib: $(ls "$LIB"/res/*.zip 2>/dev/null | wc -l) kaynak paketi, $(ls "$LIB"/classes*.dex 2>/dev/null | wc -l) dex"
fi
"$AAPT2" link -o "$(winpath "$BASE")" \
    --manifest "$(winpath "$STAGE/AndroidManifest.xml")" \
    -I "$(winpath "$ANDROID_JAR")" \
    ${LIB_ARGS[@]+"${LIB_ARGS[@]}"} \
    ${RES_FLAT[@]+"${RES_FLAT[@]}"}

# Kutuphane R siniflari (son kimlikler) -> dex; kutuphane dex'iyle birlestir -> STAGE/classes.dex
DEX_OUT=""
if [ -d "$LIB" ] && ls "$LIB"/classes*.dex >/dev/null 2>&1; then
    JAVAC="$(dirname "$JAVA")/javac"; [ -x "$JAVAC" ] || JAVAC="$(command -v javac || true)"
    [ -n "$JAVAC" ] || { echo "HATA: javac yok (android_lib R siniflari icin JDK gerekir)"; exit 1; }
    D8JAR="$BT/lib/d8.jar"
    [ -f "$D8JAR" ] || { echo "HATA: d8.jar yok: $D8JAR"; exit 1; }
    DEX_IN=()
    for d in "$LIB"/classes*.dex; do DEX_IN+=("$(winpath "$d")"); done
    if [ -d "$WORK/gen" ] && [ -n "$(find "$WORK/gen" -name '*.java' -print -quit)" ]; then
        mkdir -p "$WORK/rcls"
        find "$WORK/gen" -name '*.java' > "$WORK/rliste.txt"
        "$JAVAC" --release 11 -nowarn -d "$WORK/rcls" @"$WORK/rliste.txt" || { echo "HATA: R siniflari derlenemedi"; exit 1; }
        (cd "$WORK/rcls" && find . -name '*.class') > "$WORK/rcls.txt"
        while read -r c; do DEX_IN+=("$(winpath "$WORK/rcls/${c#./}")"); done < "$WORK/rcls.txt"
    fi
    mkdir -p "$WORK/dex"
    "$JAVA" -cp "$(winpath "$D8JAR")" com.android.tools.r8.D8 --release --min-api 26 \
        --lib "$(winpath "$ANDROID_JAR")" --output "$(winpath "$WORK/dex")" "${DEX_IN[@]}" \
        || { echo "HATA: d8 (android_lib dex birlestirme)"; exit 1; }
    DEX_OUT="$WORK/dex"
    echo "      android_lib dex: $(ls "$WORK/dex" | tr '\n' ' ')"
fi

# --- 2) native kütüphaneler (STORED) + oyun asset'leri ---------------------
# assets/: driver'ın stage'e koyduğu ses/sprite/font dosyaları. aapt2 -A
# KULLANILMAZ — Windows aapt2'si girdi adlarını ters bölüyle yazıp
# AAssetManager eşleşmesini bozuyor; python zipfile ile düz '/' garanti.
echo "[2/4] lib/<abi>/*.so + assets/ ekleniyor"
python3 - "$BASE" "$STAGE" "$DEX_OUT" <<'PY'
import os, sys, zipfile
base, stage = sys.argv[1], sys.argv[2]
dex_out = sys.argv[3] if len(sys.argv) > 3 else ""
libroot = os.path.join(stage, "lib")
with zipfile.ZipFile(base, "a", zipfile.ZIP_STORED) as z:
    for abi in sorted(os.listdir(libroot)):
        # Tame: libtulpargame.so — dizindeki her .so (dongu genel)
        for name in sorted(os.listdir(os.path.join(libroot, abi))):
            if not name.endswith(".so"):
                continue
            so = os.path.join(libroot, abi, name)
            arc = "lib/%s/%s" % (abi, name)
            zi = zipfile.ZipInfo(arc)
            zi.compress_type = zipfile.ZIP_STORED
            zi.external_attr = 0o755 << 16
            with open(so, "rb") as f:
                z.writestr(zi, f.read())
            print("  +", arc, "(%d bytes)" % os.path.getsize(so))
    dex = os.path.join(stage, "classes.dex")
    if dex_out:  # android_lib: kutuphane + R dex'leri (classes.dex, classes2.dex ...)
        for fn in sorted(os.listdir(dex_out)):
            if fn.endswith(".dex"):
                z.write(os.path.join(dex_out, fn), fn, zipfile.ZIP_DEFLATED)
                print("  + %s (%d bytes)" % (fn, os.path.getsize(os.path.join(dex_out, fn))))
    elif os.path.isfile(dex):  # hasCode=true host (motor: Swappy Java simi icin gerekli)
        z.write(dex, "classes.dex", zipfile.ZIP_DEFLATED)
        print("  + classes.dex (%d bytes)" % os.path.getsize(dex))
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
    : "${TULPAR_ANDROID_KS_PASS:?HATA: TULPAR_ANDROID_KS_PASS gerekli}"
    KEY_ALIAS="${TULPAR_ANDROID_KEY_ALIAS:?HATA: TULPAR_ANDROID_KEY_ALIAS gerekli}"
    # Parola komut satırına girmez (`env:AD`, değer değil): `pass:$P` biçimi onu
    # imza boyunca /proc/<pid>/cmdline'da açık bırakıyordu (package_aab.sh ile aynı).
    KP_VAR=TULPAR_ANDROID_KEY_PASS
    [ -n "${TULPAR_ANDROID_KEY_PASS:-}" ] || KP_VAR=TULPAR_ANDROID_KS_PASS
    export TULPAR_ANDROID_KS_PASS "$KP_VAR"
    case "$JAVA" in *.exe) export WSLENV="${WSLENV:+$WSLENV:}TULPAR_ANDROID_KS_PASS:$KP_VAR" ;; esac
    echo "[4/4] apksigner ile imzalaniyor (RELEASE: $(basename "$KS"), alias: $KEY_ALIAS)"
    "$JAVA" -jar "$(winpath "$BT/lib/apksigner.jar")" sign \
        --ks "$(winpath "$KS")" --ks-pass "env:TULPAR_ANDROID_KS_PASS" \
        --key-pass "env:$KP_VAR" --ks-key-alias "$KEY_ALIAS" \
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
