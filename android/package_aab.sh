#!/bin/bash
# ============================================================
# Tame Android — AAB paketleyici (staging dizini -> imzalı .aab)
#
# Play Store'a YÜKLENEBİLİR Android App Bundle üretir. APK'nın kardeşi
# (package_apk.sh) ama çıktı bundletool ile .aab. Play, AAB'den cihaza özel
# APK'lar üretir (ABI/dpi split) → indirme boyutu küçülür; Play artık yeni
# uygulamalarda AAB zorunlu.
#
# Girdi:  `tulpar build --target=android` staging dizini (<out>_apk/).
# Çıktı:  <out>.aab (bundletool build-bundle + jarsigner ile imzalı).
#
# Kullanım:
#   android/package_aab.sh <out>_apk [<out>.aab]
#
# WSL notu: package_apk.sh ile aynı — aapt2/java Windows tarafında (interop),
# bundletool jar'ı gradle önbelleğinden bulunur, iş Windows temp'inde yürür.
# ============================================================
set -e

STAGE="${1:?kullanim: package_aab.sh <out>_apk [<out>.aab]}"
STAGE="${STAGE%/}"
OUT_AAB="${2:-${STAGE%_apk}.aab}"

if [ ! -f "$STAGE/AndroidManifest.xml" ]; then
    echo "HATA: $STAGE/AndroidManifest.xml yok — once 'tulpar build --target=android' calistirin."
    exit 1
fi

# --- SDK / araç konumları (package_apk.sh ile aynı) ------------------------
SDK="${TULPAR_ANDROID_SDK:-}"
if [ -z "$SDK" ]; then
    for c in "$HOME/Android/Sdk" /mnt/c/Users/*/AppData/Local/Android/Sdk; do
        [ -d "$c" ] && SDK="$c" && break
    done
fi
[ -z "$SDK" ] && { echo "HATA: Android SDK bulunamadi (TULPAR_ANDROID_SDK ayarla)."; exit 1; }

BT=$(ls -d "$SDK"/build-tools/* 2>/dev/null | sort -V | tail -1)
[ -z "$BT" ] && { echo "HATA: build-tools yok ($SDK/build-tools)."; exit 1; }
ANDROID_JAR=$(ls "$SDK"/platforms/*/android.jar 2>/dev/null | sort -V | tail -1)
[ -z "$ANDROID_JAR" ] && { echo "HATA: android.jar yok ($SDK/platforms/*/)."; exit 1; }

EXE=""
if [ -f "$BT/aapt2.exe" ]; then EXE=".exe"; fi
AAPT2="$BT/aapt2$EXE"

# Java + jarsigner (Linux PATH ya da Android Studio jbr).
JAVA=""; JARSIGNER=""; KEYTOOL=""
if command -v java >/dev/null 2>&1; then
    JAVA=java; JARSIGNER=jarsigner; KEYTOOL=keytool
else
    for j in "/mnt/c/Program Files/Android/Android Studio/jbr/bin" \
             "/mnt/c/Program Files/Java"/*/bin; do
        [ -f "$j/java.exe" ] && JAVA="$j/java.exe" && JARSIGNER="$j/jarsigner.exe" && KEYTOOL="$j/keytool.exe" && break
    done
fi
[ -z "$JAVA" ] && { echo "HATA: java bulunamadi."; exit 1; }

# bundletool jar: TULPAR_BUNDLETOOL env → gradle önbelleği (en yüksek sürüm).
# bundletool jar seç: gradle önbelleği (Linux HOME + Windows Users) içindeki
# TÜM sürümleri topla, sürüm sırasına göre EN YÜKSEĞİ al (eski 1.9.0 jar'ında
# çalıştırılabilir manifest yok → newest şart). TULPAR_BUNDLETOOL bunu ezer.
BUNDLETOOL="${TULPAR_BUNDLETOOL:-}"
if [ -z "$BUNDLETOOL" ]; then
    BUNDLETOOL=$(ls -1 \
        "$HOME"/.gradle/caches/modules-2/files-2.1/com.android.tools.build/bundletool/*/*/bundletool-*.jar \
        /mnt/c/Users/*/.gradle/caches/modules-2/files-2.1/com.android.tools.build/bundletool/*/*/bundletool-*.jar \
        2>/dev/null | sed 's#.*/bundletool-\([0-9.]*\)\.jar#\1 &#' | sort -V | tail -1 | cut -d' ' -f2-)
fi
[ -z "$BUNDLETOOL" ] && { echo "HATA: bundletool.jar bulunamadi. TULPAR_BUNDLETOOL ile yolunu goster (https://github.com/google/bundletool/releases)."; exit 1; }

echo "SDK:         $SDK"
echo "build-tools: $BT"
echo "bundletool:  $(basename "$BUNDLETOOL")"
echo "java:        $JAVA"

winpath() { if [ -n "$EXE" ]; then wslpath -w "$1"; else echo "$1"; fi; }

if [ -n "$EXE" ]; then
    WINTMP=$(cmd.exe /c "echo %LOCALAPPDATA%\\Temp" 2>/dev/null | tr -d '\r')
    WORK="$(wslpath -u "$WINTMP")/tulpar_aab_$$"
else
    WORK=$(mktemp -d)
fi
rm -rf "$WORK"; mkdir -p "$WORK"
trap 'rm -rf "$WORK"' EXIT

# --- 1) aapt2 link --proto-format: bundletool proto APK'sı -----------------
# AAB, protobuf kodlu manifest + resources.pb ister (ikili XML değil). aapt2
# --proto-format bunu üretir; APK yolundan tek fark bu bayrak.
PROTO="$WORK/proto.apk"
RES_FLAT=()
if [ -d "$STAGE/res" ]; then
    echo ""
    echo "[1/5] aapt2 compile (res/) + link --proto-format"
    COMPILED="$WORK/res_flat"; mkdir -p "$COMPILED"
    "$AAPT2" compile --dir "$(winpath "$STAGE/res")" -o "$(winpath "$COMPILED")"
    for fl in "$COMPILED"/*.flat; do
        [ -f "$fl" ] && RES_FLAT+=("$(winpath "$fl")")
    done
else
    echo ""
    echo "[1/5] aapt2 link --proto-format (manifest)"
fi
"$AAPT2" link --proto-format -o "$(winpath "$PROTO")" \
    --manifest "$(winpath "$STAGE/AndroidManifest.xml")" \
    -I "$(winpath "$ANDROID_JAR")" \
    --min-sdk-version 26 --target-sdk-version 34 \
    ${RES_FLAT[@]+"${RES_FLAT[@]}"}

# --- 2) bundletool modül düzeni: base/ ------------------------------------
# proto APK içinden AndroidManifest.xml (proto) + resources.pb + res/ alınır;
# bundletool "base" modülü şu düzeni ister:
#   base/manifest/AndroidManifest.xml, base/resources.pb, base/res/**,
#   base/lib/**, base/assets/**
echo "[2/5] bundletool modul duzeni (base/) hazirlaniyor"
MOD="$WORK/mod"; mkdir -p "$MOD"
python3 - "$PROTO" "$MOD" <<'PY'
import sys, zipfile
proto, mod = sys.argv[1], sys.argv[2]
with zipfile.ZipFile(proto) as z:
    z.extractall(mod)
PY
BASE="$WORK/base"; mkdir -p "$BASE/manifest"
mv "$MOD/AndroidManifest.xml" "$BASE/manifest/AndroidManifest.xml"
[ -f "$MOD/resources.pb" ] && mv "$MOD/resources.pb" "$BASE/resources.pb"
[ -d "$MOD/res" ] && mv "$MOD/res" "$BASE/res"
# native kütüphaneler + oyun asset'leri (STORED/DEFLATED python ile — apk yolu)
python3 - "$BASE" "$STAGE" <<'PY'
import os, sys, shutil
base, stage = sys.argv[1], sys.argv[2]
libroot = os.path.join(stage, "lib")
if os.path.isdir(libroot):
    for abi in sorted(os.listdir(libroot)):
        so = os.path.join(libroot, abi, "libtulpargame.so")
        if os.path.isfile(so):
            d = os.path.join(base, "lib", abi); os.makedirs(d, exist_ok=True)
            shutil.copy2(so, os.path.join(d, "libtulpargame.so"))
            print("  +", "lib/%s/libtulpargame.so" % abi)
aroot = os.path.join(stage, "assets")
if os.path.isdir(aroot):
    dst = os.path.join(base, "assets")
    shutil.copytree(aroot, dst, dirs_exist_ok=True)
    print("  + assets/")
PY

# base.zip: base/ içeriğini KÖKTEN zip'le (manifest/, res/, lib/, resources.pb)
echo "[3/5] base.zip"
BASEZIP="$WORK/base.zip"
python3 - "$BASE" "$BASEZIP" <<'PY'
import os, sys, zipfile
base, out = sys.argv[1], sys.argv[2]
with zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED) as z:
    for dirpath, _dirs, files in os.walk(base):
        for fn in files:
            full = os.path.join(dirpath, fn)
            arc = os.path.relpath(full, base).replace(os.sep, "/")
            z.write(full, arc)
PY

# --- 4) bundletool build-bundle -------------------------------------------
echo "[4/5] bundletool build-bundle -> .aab"
RAW_AAB="$WORK/raw.aab"
# Gradle önbelleğindeki bundletool jar'ı kütüphane jarıdır (Main-Class + gömülü
# bağımlılık YOK). Fat "bundletool-all" jar'ı yoksa classpath'i önbellekten
# kur: bundletool + transitif bağımlılıkları. CPSEP: Windows java ';', Linux ':'.
if [ -n "${TULPAR_BUNDLETOOL_ALL:-}" ]; then
    # Kullanıcı fat "bundletool-all-*.jar" verdiyse doğrudan onu kullan (en temiz).
    "$JAVA" -jar "$(winpath "$TULPAR_BUNDLETOOL_ALL")" build-bundle \
        --modules="$(winpath "$BASEZIP")" --output="$(winpath "$RAW_AAB")"
else
    # Fat jar yok → classpath'i gradle önbelleğinden kur. TÜM bağımlılık
    # jar'larını tek dizine kopyalayıp `-cp "dir/*"` joker'i ile ver: komut
    # satırı uzunluk sınırına takılmaz, eksik bir jar kalırsa NoClassDefFound
    # verir (o zaman aşağıdaki listeye ekle). Bundletool jar'ından geriye doğru
    # gradle modules-2 kökü bulunur.
    GRADLE_ROOT="${BUNDLETOOL%%/com.android.tools.build/bundletool/*}"
    CPDIR="$WORK/cp"; mkdir -p "$CPDIR"
    cp "$BUNDLETOOL" "$CPDIR/"
    add_dep() {  # $1 = grup/artifact göreli yolu; en yüksek sürüm jar'ı kopyala
        local j
        j=$(ls -1 "$GRADLE_ROOT/$1"/*/*/*.jar 2>/dev/null | sort -V | tail -1)
        [ -n "$j" ] && cp "$j" "$CPDIR/"
    }
    add_dep com.google.protobuf/protobuf-java
    add_dep com.google.guava/guava
    add_dep com.google.guava/failureaccess
    add_dep net.sf.jopt-simple/jopt-simple
    add_dep com.android.tools.build/aapt2-proto
    add_dep com.google.dagger/dagger
    add_dep javax.inject/javax.inject
    add_dep com.android/zipflinger
    add_dep com.android.tools.build/apksig
    add_dep com.google.errorprone/error_prone_annotations
    add_dep com.google.code.gson/gson
    add_dep org.bouncycastle/bcprov-jdk18on
    add_dep org.bouncycastle/bcpkix-jdk18on
    add_dep org.bitbucket.b_c/jose4j
    add_dep com.google.auto.value/auto-value-annotations
    add_dep com.google.code.findbugs/jsr305
    add_dep org.slf4j/slf4j-api
    WILD="$(winpath "$CPDIR")"
    [ -n "$EXE" ] && WILD="$WILD\\*" || WILD="$WILD/*"
    "$JAVA" -cp "$WILD" \
        com.android.tools.build.bundletool.BundleToolMain build-bundle \
        --modules="$(winpath "$BASEZIP")" \
        --output="$(winpath "$RAW_AAB")"
fi

# --- 5) imzala: jarsigner (AAB, apksigner değil) --------------------------
# Play upload key ile imza: TULPAR_ANDROID_KEYSTORE + _KS_PASS + _KEY_ALIAS
# (+ ops. _KEY_PASS). Verilmezse debug keystore ile imzalanır (yerel doğrulama;
# Play'e yüklemeden önce kendi upload anahtarınla imzala).
if [ -n "${TULPAR_ANDROID_KEYSTORE:-}" ]; then
    KS="$TULPAR_ANDROID_KEYSTORE"
    [ -f "$KS" ] || { echo "HATA: keystore yok: $KS"; exit 1; }
    KS_PASS="${TULPAR_ANDROID_KS_PASS:?HATA: TULPAR_ANDROID_KS_PASS gerekli}"
    KEY_ALIAS="${TULPAR_ANDROID_KEY_ALIAS:?HATA: TULPAR_ANDROID_KEY_ALIAS gerekli}"
    KEY_PASS="${TULPAR_ANDROID_KEY_PASS:-$KS_PASS}"
    echo "[5/5] jarsigner (RELEASE: $(basename "$KS"), alias: $KEY_ALIAS)"
    cp "$RAW_AAB" "$OUT_AAB"
    "$JARSIGNER" -keystore "$(winpath "$KS")" \
        -storepass "$KS_PASS" -keypass "$KEY_PASS" \
        -sigalg SHA256withRSA -digestalg SHA-256 \
        "$(winpath "$OUT_AAB")" "$KEY_ALIAS"
else
    KS_DIR="$HOME/.android"; mkdir -p "$KS_DIR"
    KS="$KS_DIR/debug.keystore"
    if [ ! -f "$KS" ]; then
        echo "[5/5] debug.keystore uretiliyor"
        "$KEYTOOL" -genkeypair -v -keystore "$(winpath "$KS")" \
            -alias androiddebugkey -keyalg RSA -keysize 2048 -validity 10000 \
            -storepass android -keypass android \
            -dname "CN=Android Debug,O=Android,C=US" >/dev/null 2>&1 || true
    fi
    echo "[5/5] jarsigner (debug)"
    cp "$RAW_AAB" "$OUT_AAB"
    "$JARSIGNER" -keystore "$(winpath "$KS")" \
        -storepass android -keypass android \
        -sigalg SHA256withRSA -digestalg SHA-256 \
        "$(winpath "$OUT_AAB")" androiddebugkey
fi

echo ""
echo "Tamam: $OUT_AAB"
ls -lh "$OUT_AAB"
echo "Dogrula:  java -jar bundletool validate --bundle=$OUT_AAB"
echo "Not: Play Console'a yuklemeden once kendi upload anahtarinla imzala."
