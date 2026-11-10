#!/bin/bash
# android_lib KAPISI — package_apk.sh staging'deki android_lib/'i (kutuphane kaynaklari res/*.zip, paketler.txt,
# R'siz kutuphane dex'i) gorunce: kaynaklari -R ile baglamali, kutuphane paketinin R sinifini SON kaynak
# kimlikleriyle uretip dex'e katmali. Neden (2026-11-09, tulpar-engine KOPRU §16, Warlords of Era): Google hesap
# secici (Credential Manager) NativeActivity oyununa Gradle'siz girer; R kimlikleri ancak son link'te belli.
# OLCER: uretilen APK — aapt2 dump resources'ta kutuphane dizgisi, classes.dex'te kutuphane sinifi + R$string,
# ve R alani ile kaynak tablosundaki kimlik AYNI (dexdump). POZITIF KONTROL: android_lib'siz staging'de dex yok.
# AAB (package_aab.sh) da ayni olculerle (bundletool varsa).
# Kullanim: tests/android_kutuphane.sh [tulpar]   (PAKETLE=<eski package_apk.sh> / PAKETLE_AAB=<eski package_aab.sh>:
# pozitif kontrol, KIRMIZI gormeli)
set -u
cd "$(dirname "$0")/.."
TULPAR="${1:-./tulpar}"
[ -x "$TULPAR" ] || { echo "android kutuphane kapisi: $TULPAR yok"; exit 1; }
TULPAR="$(cd "$(dirname "$TULPAR")" && pwd)/$(basename "$TULPAR")"
export TULPAR_AOT_NOCACHE=1
atla() { echo "android kutuphane kapisi: ATLANDI: $1"; exit 0; }
SDK="${TULPAR_ANDROID_SDK:-${ANDROID_HOME:-$HOME/Android/Sdk}}"
BT=$(ls -d "$SDK"/build-tools/* 2>/dev/null | sort -V | tail -1)
AJ=$(ls "$SDK"/platforms/*/android.jar 2>/dev/null | sort -V | tail -1)
[ -n "$BT" ] && [ -n "$AJ" ] || atla "Android SDK yok"
command -v javac >/dev/null || atla "javac yok"
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
FAIL=0; OK=0
gecti() { echo "  gecti  $1"; OK=$((OK + 1)); }
dustu() { echo "  DUSTU  $1"; FAIL=1; }
P="$TMP/p"; mkdir -p "$P"
printf 'print("kutuphane");\n' > "$P/t.tpr"
printf 'name = "kuttest"\n\n[android]\npackage = "dev.tulparlang.kuttest"\n' > "$P/tulpar.toml"
OUT=$(cd "$P" && DISPLAY= "$TULPAR" build --target=android t.tpr k 2>&1); RC=$?
if echo "$OUT" | grep -qiE "NDK (bulunamadi|not found|gerekir)|needs the NDK|android/dist ars|android/dist archives"; then atla "NDK ya da android/dist arsivleri yok"; fi
STAGE="$P/k_apk"
[ $RC -eq 0 ] && [ -f "$STAGE/AndroidManifest.xml" ] || { echo "$OUT" | tail -4; echo "android kutuphane kapisi: staging uretilemedi"; exit 1; }
# POZITIF KONTROL: android_lib yokken dex yok
bash "${PAKETLE:-android/package_apk.sh}" "$STAGE" "$TMP/yok.apk" >/dev/null 2>&1
if [ -f "$TMP/yok.apk" ] && ! unzip -l "$TMP/yok.apk" | grep -q 'classes.dex'; then gecti "KONTROL: android_lib yok -> classes.dex yok"; else dustu "android_lib'siz APK beklenen degil"; fi
# Sahte kutuphane: paket org.ornek.kut, dizgi kut_selam; sinif org.ornek.kut.Selam R.string.kut_selam'i okur
L="$TMP/lib"; mkdir -p "$L/res/values" "$L/src/org/ornek/kut" "$L/rgen/org/ornek/kut"
printf '<resources><string name="kut_selam">merhaba kutuphane</string></resources>\n' > "$L/res/values/v.xml"
printf 'package org.ornek.kut;\npublic final class R { public static final class string { public static int kut_selam = 0; } }\n' > "$L/rgen/org/ornek/kut/R.java"
printf 'package org.ornek.kut;\npublic final class Selam { public static int kimlik() { return R.string.kut_selam; } }\n' > "$L/src/org/ornek/kut/Selam.java"
mkdir -p "$L/rcls" "$L/cls" "$STAGE/android_lib/res"
javac --release 11 -nowarn -d "$L/rcls" "$L/rgen/org/ornek/kut/R.java" &&
javac --release 11 -nowarn -cp "$L/rcls" -d "$L/cls" "$L/src/org/ornek/kut/Selam.java" &&
java -cp "$BT/lib/d8.jar" com.android.tools.r8.D8 --release --min-api 26 --lib "$AJ" --classpath "$L/rcls" --output "$STAGE/android_lib" "$L/cls/org/ornek/kut/Selam.class" &&
"$BT/aapt2" compile --dir "$L/res" -o "$STAGE/android_lib/res/00_org.ornek.kut.zip" || { echo "android kutuphane kapisi: sahte kutuphane kurulamadi"; exit 1; }
echo "org.ornek.kut" > "$STAGE/android_lib/paketler.txt"
sed -i 's/android:hasCode="false"/android:hasCode="true"/' "$STAGE/AndroidManifest.xml"
LOG=$(bash "${PAKETLE:-android/package_apk.sh}" "$STAGE" "$TMP/var.apk" 2>&1)
[ -f "$TMP/var.apk" ] || { echo "$LOG" | tail -6; dustu "android_lib'li APK uretilemedi"; }
if [ -f "$TMP/var.apk" ]; then
  RES=$("$BT/aapt2" dump resources "$TMP/var.apk" 2>/dev/null)
  KIM=$(sed -n 's/^ *resource \(0x[0-9a-f]*\) string\/kut_selam.*/\1/p' <<< "$RES" | head -1)
  if [ -n "$KIM" ] && grep -q 'merhaba kutuphane' <<< "$RES"; then gecti "kutuphane kaynagi APK'da ($KIM)"; else dustu "kutuphane kaynagi APK'da yok"; fi
  unzip -o -q "$TMP/var.apk" classes.dex -d "$TMP/d" 2>/dev/null
  DD=$("$BT/dexdump" -d "$TMP/d/classes.dex" 2>/dev/null)
  if grep -q "Lorg/ornek/kut/Selam;" <<< "$DD" && grep -q 'Lorg/ornek/kut/R\$string;' <<< "$DD"; then gecti "classes.dex: kutuphane sinifi + R\$string"; else dustu "classes.dex'te Selam ya da R\$string yok"; fi
  # R alaninin ilk degeri (static init) kaynak tablosundaki kimlikle ayni olmali
  RV=$(grep -A6 "name          : 'kut_selam'" <<< "$DD" | sed -n 's/^ *value *: \(-\?[0-9]*\).*/\1/p' | head -1)
  if [ -n "$KIM" ] && [ -n "$RV" ] && [ "$(printf '%d' "$KIM")" = "$RV" ]; then gecti "R.string.kut_selam = son kimlik ($RV)"; else dustu "R kimligi tutmuyor (R $RV, tablo $KIM)"; fi
fi
# AAB (package_aab.sh, 2026-11-10: Play paketinde kutuphane kaynagi + dex SESSIZCE duserdi): ayni staging'den
# base/dex/classes.dex'te sinif + R$string, R alani = APK'daki kimlik (ayni link girdisi -> ayni kimlik),
# base/resources.pb'de kutuphane dizgisi. bundletool yoksa (TULPAR_BUNDLETOOL_ALL) bu bolum ATLANIR, gorunur.
AABP="${PAKETLE_AAB:-android/package_aab.sh}"
if [ -z "${TULPAR_BUNDLETOOL_ALL:-}${TULPAR_BUNDLETOOL:-}" ] && ! ls android/bundletool-all*.jar >/dev/null 2>&1 && ! command -v bundletool >/dev/null; then
  echo "  ATLANDI AAB: bundletool yok (TULPAR_BUNDLETOOL_ALL)"
else
  LOG=$(bash "$AABP" "$STAGE" "$TMP/var.aab" 2>&1)
  if [ -f "$TMP/var.aab" ]; then
    rm -rf "$TMP/a"; unzip -o -q "$TMP/var.aab" 'base/dex/*' base/resources.pb -d "$TMP/a" 2>/dev/null
    ADD=$([ -f "$TMP/a/base/dex/classes.dex" ] && "$BT/dexdump" -d "$TMP/a/base/dex/classes.dex" 2>/dev/null)
    if grep -q "Lorg/ornek/kut/Selam;" <<< "$ADD" && grep -q 'Lorg/ornek/kut/R\$string;' <<< "$ADD"; then gecti "AAB base/dex: kutuphane sinifi + R\$string"; else dustu "AAB base/dex'te Selam ya da R\$string yok"; fi
    ARV=$(grep -A6 "name          : 'kut_selam'" <<< "$ADD" | sed -n 's/^ *value *: \(-\?[0-9]*\).*/\1/p' | head -1)
    if [ -n "$KIM" ] && [ -n "$ARV" ] && [ "$(printf '%d' "$KIM")" = "$ARV" ]; then gecti "AAB R.string.kut_selam = son kimlik ($ARV)"; else dustu "AAB R kimligi tutmuyor (R $ARV, APK tablosu $KIM)"; fi
    if [ -f "$TMP/a/base/resources.pb" ] && grep -aq 'merhaba kutuphane' "$TMP/a/base/resources.pb"; then gecti "AAB resources.pb: kutuphane dizgisi"; else dustu "AAB resources.pb'de kutuphane dizgisi yok"; fi
  else
    echo "$LOG" | tail -6; dustu "android_lib'li AAB uretilemedi"
  fi
fi
if [ $FAIL -eq 0 ]; then echo "android kutuphane kapisi: $OK gecti"; exit 0; fi
echo "android kutuphane kapisi: $OK gecti, KIRMIZI"; exit 1
