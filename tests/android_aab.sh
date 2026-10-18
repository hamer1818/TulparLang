#!/bin/bash
# `tulpar build --aab` KAPISI — Play'e yüklenebilir Android App Bundle gerçekten
# geçerli mi, sürüm kimliği ve hedef API doğru mu?
#
# NEDEN (ölçüldü 2026-10-13, tulpar-engine oyunu Play hazırlığı):
#   * package_aab.sh vardı ama hiç koşturulmamıştı: çalıştırılabilir değildi,
#     bundletool'u yalnız gradle önbelleğindeki kütüphane jar'ından
#     classpath kurarak arıyordu (GitHub'daki tek jar'ı tanımıyordu).
#   * targetSdk sabit 34'tü (manifestte VE aapt2 bayrağında). Google Play
#     31 Ağustos 2026'dan beri yeni uygulama/güncellemede 36 istiyor: 34
#     hedefli paket Play Console'da reddedilir — yerelde hiçbir şey uyarmazdı.
#   * versionCode yalnız toml'dan geliyordu; her yükleme bir öncekinden büyük
#     olmalı, betikten (env) artırılamıyordu. Geçersiz değer sessizce "1"e
#     düşseydi Play "bu sürüm kodu kullanıldı" derdi.
#
# ÖLÇER: (1) --aab çıktısında modül düzeni (BundleConfig.pb, base/manifest,
# resources.pb, iki ABI'nin .so'su, assets) + jarsigner imzası; (2) bundletool
# validate; (3) build-apks --mode=universal'dan çıkan APK'nın aapt2 badging'i:
# paket, env'den versionCode/versionName (toml'daki 7/0.7'yi EZMELİ),
# targetSdk 36, native-code iki ABI, assets APK'da.
#
# POZİTİF KONTROLLER: (a) TULPAR_ANDROID_VERSION_CODE=abc derlemeyi DURDURUYOR
# (doğrulama gerçekten ölçüyor); (b) BundleConfig.pb'si silinmiş kopyayı
# bundletool validate REDDEDİYOR (validate adımı gerçekten bir şey denetliyor);
# (c) env'siz staging manifesti toml'daki 7'yi taşıyor (env önceliği ölçülüyor,
# rastlantı değil).
#
# Araç yoksa (NDK / Android SDK / java / bundletool) GÖRÜNÜR atlanır:
# "android aab kapisi: ATLANDI: <sebep>" basar, çıkış 0 — TULPAR_AAB_ZORUNLU=1
# iken (CI) aynı durum KIRMIZI.
#
# Kullanım: tests/android_aab.sh [tulpar]   (bundletool: TULPAR_BUNDLETOOL_ALL)
set -u
cd "$(dirname "$0")/.."
TULPAR="${1:-./tulpar}"
TULPAR="$(cd "$(dirname "$TULPAR")" && pwd)/$(basename "$TULPAR")"
[ -x "$TULPAR" ] || { echo "android aab kapisi: $TULPAR yok"; exit 1; }

# TULPAR_AAB_ZORUNLU=1 (CI Linux: bundletool + SDK o iste kuruldu): atlama KUSUR,
# kapi KIRMIZI doner — yoksa arac eksikligi CI'da sessizce "olcmuyor" olurdu.
atla() {
    if [ "${TULPAR_AAB_ZORUNLU:-}" = "1" ]; then
        echo "android aab kapisi: DUSTU: $1 (TULPAR_AAB_ZORUNLU=1 — atlama kusur)"; exit 1
    fi
    echo "android aab kapisi: ATLANDI: $1"; exit 0
}

SDK="${TULPAR_ANDROID_SDK:-$HOME/Android/Sdk}"
[ -d "$SDK" ] || atla "Android SDK yok ($SDK)"
BT=$(ls -d "$SDK"/build-tools/* 2>/dev/null | sort -V | tail -1)
[ -n "$BT" ] && [ -x "$BT/aapt2" ] || atla "build-tools/aapt2 yok"
command -v java >/dev/null 2>&1 || atla "java yok"
command -v jarsigner >/dev/null 2>&1 || atla "jarsigner yok"
BTJ="${TULPAR_BUNDLETOOL_ALL:-}"
if [ -z "$BTJ" ]; then
    BTJ=$(ls -1 "$(dirname "$TULPAR")"/android/bundletool-all*.jar ./android/bundletool-all*.jar 2>/dev/null | sort -V | tail -1)
fi
[ -n "$BTJ" ] && [ -f "$BTJ" ] || atla "bundletool-all jar yok (TULPAR_BUNDLETOOL_ALL)"
export TULPAR_BUNDLETOOL_ALL="$BTJ"
bt() { java -jar "$BTJ" "$@"; }

TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
FAIL=0
OK=0
gecti() { echo "  gecti  $1"; OK=$((OK + 1)); }
dustu() { echo "  DUSTU  $1"; FAIL=1; }

mkdir -p "$TMP/p/assets"
printf 'print("merhaba aab");\n' > "$TMP/p/t.tpr"
printf 'ses verisi yerine\n' > "$TMP/p/assets/ornek.txt"
cat > "$TMP/p/tulpar.toml" <<'EOF'
name = "aabtest"

[android]
package = "dev.tulparlang.aabtest"
name = "AAB Test"
version_code = "7"
version_name = "0.7"
assets = "assets"
EOF

# --- (c) env'siz staging: toml değeri manifestte ---------------------------
OUT=$(cd "$TMP/p" && DISPLAY= "$TULPAR" build --target=android t.tpr kontrol 2>&1)
if echo "$OUT" | grep -qiE "NDK (bulunamadi|not found)|android/dist ars|android/dist archives"; then
    atla "NDK ya da android/dist arsivleri yok"
fi
KMAN="$TMP/p/kontrol_apk/AndroidManifest.xml"
if grep -q 'android:versionCode="7"' "$KMAN" 2>/dev/null && grep -q 'targetSdkVersion="36"' "$KMAN"; then
    gecti "KONTROL: env'siz manifest toml'daki versionCode 7'yi ve targetSdk 36'yi tasiyor"
else
    dustu "env'siz staging manifesti beklenen degil"; echo "$OUT" | tail -6
fi

# --- (a) geçersiz versionCode derlemeyi durdurur --------------------------
OUT=$(cd "$TMP/p" && TULPAR_ANDROID_VERSION_CODE=abc DISPLAY= "$TULPAR" build --target=android t.tpr bozuk 2>&1)
RC=$?
if [ $RC -ne 0 ] && echo "$OUT" | grep -q "versionCode"; then
    gecti "KONTROL: versionCode=abc derlemeyi durdurdu (kod $RC)"
else
    dustu "versionCode=abc kabul edildi (kod $RC)"; echo "$OUT" | tail -4
fi

# --- (1) --aab: env versionCode/versionName ile ---------------------------
OUT=$(cd "$TMP/p" && TULPAR_ANDROID_VERSION_CODE=42 TULPAR_ANDROID_VERSION_NAME=4.2-test \
      DISPLAY= "$TULPAR" build --aab t.tpr aabtest 2>&1)
RC=$?
AAB="$TMP/p/aabtest.aab"
if [ $RC -ne 0 ] || [ ! -f "$AAB" ]; then
    dustu "tulpar build --aab basarisiz (kod $RC)"; echo "$OUT" | tail -12
    echo "android aab kapisi: $OK gecti, KIRMIZI"; exit 1
fi
gecti "tulpar build --aab -> $(du -h "$AAB" | cut -f1)"
LIST=$(python3 -c 'import sys,zipfile; print("\n".join(zipfile.ZipFile(sys.argv[1]).namelist()))' "$AAB")
for e in BundleConfig.pb base/manifest/AndroidManifest.xml base/resources.pb \
         base/lib/arm64-v8a/libtulpargame.so base/lib/x86_64/libtulpargame.so \
         base/assets/assets/ornek.txt; do
    if echo "$LIST" | grep -qx "$e"; then gecti "aab icinde $e"; else dustu "aab icinde $e YOK"; fi
done
if echo "$LIST" | grep -q '^base/symbols/'; then dustu "striplenmemis symbols/ aab'a girmis"; fi
if jarsigner -verify "$AAB" 2>&1 | grep -q "jar verified"; then gecti "jarsigner -verify"; else dustu "aab imzasi dogrulanamadi"; fi
if bt validate --bundle="$AAB" >/dev/null 2>&1; then gecti "bundletool validate"; else dustu "bundletool validate reddetti"; fi

# --- (b) bozuk kopya validate'ten geçmemeli -------------------------------
python3 - "$AAB" "$TMP/bozuk.aab" <<'PY'
import sys, zipfile
src, dst = sys.argv[1], sys.argv[2]
with zipfile.ZipFile(src) as zi, zipfile.ZipFile(dst, "w", zipfile.ZIP_DEFLATED) as zo:
    for n in zi.namelist():
        if n != "BundleConfig.pb":
            zo.writestr(n, zi.read(n))
PY
if bt validate --bundle="$TMP/bozuk.aab" >/dev/null 2>&1; then
    dustu "BundleConfig.pb'siz kopyayi validate KABUL ETTI (validate bir sey olcmuyor)"
else
    gecti "KONTROL: BundleConfig.pb'siz kopyayi validate reddetti"
fi

# --- (2) universal APK + badging -----------------------------------------
if bt build-apks --bundle="$AAB" --output="$TMP/u.apks" --mode=universal \
       --ks="$HOME/.android/debug.keystore" --ks-pass=pass:android \
       --ks-key-alias=androiddebugkey --key-pass=pass:android >/dev/null 2>&1; then
    python3 -c 'import sys,zipfile; open(sys.argv[2],"wb").write(zipfile.ZipFile(sys.argv[1]).read("universal.apk"))' \
        "$TMP/u.apks" "$TMP/universal.apk"
    gecti "build-apks --mode=universal"
    BADGE=$("$BT/aapt2" dump badging "$TMP/universal.apk" 2>&1)
    chk() { if echo "$BADGE" | grep -q "$1"; then gecti "badging: $2"; else dustu "badging: $2 YOK"; fi; }
    chk "package: name='dev.tulparlang.aabtest' versionCode='42' versionName='4.2-test'" "paket + env versionCode 42 / versionName 4.2-test"
    chk "targetSdkVersion:'36'" "targetSdk 36"
    chk "minSdkVersion:'26'" "minSdk 26"
    chk "native-code: 'arm64-v8a' 'x86_64'" "native-code iki ABI"
    if python3 -c 'import sys,zipfile; sys.exit(0 if "assets/assets/ornek.txt" in zipfile.ZipFile(sys.argv[1]).namelist() else 1)' "$TMP/universal.apk"; then
        gecti "universal APK'da assets/assets/ornek.txt"
    else
        dustu "universal APK'da asset yok"
    fi
else
    dustu "build-apks --mode=universal basarisiz"
fi

# --- (3) yükleme anahtarıyla imza: parola hiçbir sürecin argv'sinde görünmez ---
# NEDEN (2026-10-18, tulpar-engine A8): package_aab.sh/package_apk.sh parolayı
# `-storepass "$P"` / `--ks-pass pass:$P` ile veriyordu; imza sürdükçe parola
# /proc/<pid>/cmdline'da (ps) okunuyordu. Artık `-storepass:env` / `env:AD`.
# ÖLÇER: atılık anahtarla --aab; imzacı sertifikası anahtarınkiyle aynı SHA-256;
# derleme boyunca /proc/*/cmdline taraması parolayı HİÇ görmüyor; çıktıda yok.
# POZİTİF KONTROL: parolayı bilerek argv'ye koyan bir süreci tarayıcı GÖRÜYOR.
if command -v keytool >/dev/null 2>&1; then
    PASS="tulpar-test-$$-${RANDOM}${RANDOM}-gizli"
    YKS="$TMP/yukleme.jks"
    TULPAR_ANDROID_KS_PASS="$PASS" keytool -genkeypair -keystore "$YKS" -storetype PKCS12 \
        -alias yukleme -keyalg RSA -keysize 2048 -validity 9200 -dname "CN=Tulpar AAB Test" \
        -storepass:env TULPAR_ANDROID_KS_PASS >/dev/null 2>&1
    # Tarayıcı parolayı ortamdan alır (kendi argv'sinde taşımaz); bulduğu
    # süreçlerin argv'sini (parola maskeli) dosyaya yazar, durdurma dosyası silinene dek.
    tara() {
        : > "$TMP/goruldu"; touch "$TMP/tara_$1"
        TARA_PASS="$PASS" python3 - "$TMP/tara_$1" "$TMP/goruldu" <<'PY2' &
import os, sys, time
dur, out, p = sys.argv[1], sys.argv[2], os.environ["TARA_PASS"].encode()
ben = str(os.getpid()); hits = set()
while os.path.exists(dur):
    for d in os.listdir("/proc"):
        if not d.isdigit() or d == ben: continue
        try: c = open("/proc/%s/cmdline" % d, "rb").read()
        except OSError: continue
        if p in c: hits.add(c.replace(p, b"<PAROLA>").replace(b"\0", b" ")[:160])
    time.sleep(0.02)
open(out, "wb").write(b"".join(h + b"\n" for h in sorted(hits)))
PY2
        TARA_PID=$!
        sleep 0.3
    }
    tara_bitir() { rm -f "$TMP/tara_$1"; wait "$TARA_PID" 2>/dev/null; }
    tara kontrol
    # `sh -c 'sleep ..'` DEĞİL: kabuk son komutu exec eder, argv parolayı ilk
    # milisaniyede kaybeder ve kontrol rastlantıya kalır (ilk koşuda öyle oldu).
    python3 -c 'import time; time.sleep(0.5)' "$PASS"
    tara_bitir kontrol
    if [ -s "$TMP/goruldu" ]; then
        gecti "KONTROL: argv'sinde parola olan sureci tarayici gordu"
    else
        dustu "tarayici argv'deki parolayi GORMEDI (tarama bir sey olcmuyor)"
    fi
    tara imza
    OUT=$(cd "$TMP/p" && TULPAR_ANDROID_KEYSTORE="$YKS" TULPAR_ANDROID_KS_PASS="$PASS" \
          TULPAR_ANDROID_KEY_ALIAS=yukleme DISPLAY= "$TULPAR" build --aab t.tpr imzali 2>&1)
    RC=$?
    tara_bitir imza
    IAAB="$TMP/p/imzali.aab"
    if [ $RC -eq 0 ] && [ -f "$IAAB" ] && jarsigner -verify "$IAAB" 2>&1 | grep -q "jar verified"; then
        gecti "yukleme anahtariyla --aab + jarsigner -verify"
        A=$(keytool -printcert -jarfile "$IAAB" 2>/dev/null | sed -n 's/^[[:space:]]*SHA256: //p' | head -1)
        B=$(TULPAR_ANDROID_KS_PASS="$PASS" keytool -list -v -keystore "$YKS" -alias yukleme \
              -storepass:env TULPAR_ANDROID_KS_PASS 2>/dev/null | sed -n 's/^[[:space:]]*SHA256: //p' | head -1)
        if [ -n "$A" ] && [ "$A" = "$B" ]; then gecti "imzaci sertifikasi = yukleme anahtari (SHA-256)"
        else dustu "imzaci sertifikasi anahtarinki degil ('$A' / '$B')"; fi
        if bt validate --bundle="$IAAB" >/dev/null 2>&1; then gecti "imzali aab bundletool validate"
        else dustu "imzali aab validate reddetti"; fi
    else
        dustu "yukleme anahtariyla --aab basarisiz (kod $RC)"; printf '%s\n' "${OUT//"$PASS"/<PAROLA>}" | tail -8
    fi
    if [ -s "$TMP/goruldu" ]; then
        dustu "parola imza sirasinda argv'de goruldu:"; sed 's/^/      /' "$TMP/goruldu"
    else
        gecti "parola imza boyunca hicbir surecin argv'sinde gorulmedi"
    fi
    case "$OUT" in *"$PASS"*) dustu "parola derleme ciktisinda basildi" ;; *) gecti "parola derleme ciktisinda yok" ;; esac
else
    echo "  ATLANDI: keytool yok — yukleme anahtari imzasi olculmedi"
fi

if [ $FAIL -eq 0 ]; then
    echo "android aab kapisi: $OK gecti"
    exit 0
fi
echo "android aab kapisi: $OK gecti, KIRMIZI"
exit 1
