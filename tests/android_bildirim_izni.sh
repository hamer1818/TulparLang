#!/bin/bash
# TULPAR_ANDROID_BILDIRIM=1 KAPISI — bildirim gosteren oyunun APK'sina android.permission.POST_NOTIFICATIONS
# girmeli; secenek verilmeyen oyuna GIRMEMELI (Android 13+ bu izin olmadan bildirimi sessizce gostermez).
# Neden (2026-11-09, tulpar-engine eng_notify, Warlords of Era): arka planda biten savas/insaat bildirimi.
# OLCER: staging manifesti (varsayilan / BILDIRIM=1 / BILDIRIM=0 / BILDIRIM=evet / toml `[android] bildirim`).
# POZITIF KONTROLLER: varsayilanda INTERNET satiri GORULUYOR (kapi izin satirini okuyabiliyor) ve bildirim
# izni YOK; yazim hatasi derlemeyi DURDURUYOR; ikinci arguman eski ikili ise KIRMIZI gormeli.
# Kullanim: tests/android_bildirim_izni.sh [tulpar] [eski_tulpar]
set -u
cd "$(dirname "$0")/.."
TULPAR="${1:-./tulpar}"
ESKI="${2:-}"
[ -x "$TULPAR" ] || { echo "android bildirim kapisi: $TULPAR yok"; exit 1; }
TULPAR="$(cd "$(dirname "$TULPAR")" && pwd)/$(basename "$TULPAR")"
export TULPAR_AOT_NOCACHE=1
unset TULPAR_ANDROID_INTERNET TULPAR_ANDROID_LAN TULPAR_ANDROID_BILDIRIM
atla() { echo "android bildirim kapisi: ATLANDI: $1"; exit 0; }
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
IZIN='android.permission.POST_NOTIFICATIONS'
kos() {
  local T="$1" P="$TMP/p" OUT RC MAN
  FAIL=0; OK=0
  gecti() { echo "  gecti  $1"; OK=$((OK + 1)); }
  dustu() { echo "  DUSTU  $1"; FAIL=1; }
  rm -rf "$P"; mkdir -p "$P"
  printf 'print("bildirim");\n' > "$P/t.tpr"
  printf 'name = "bildtest"\n\n[android]\npackage = "dev.tulparlang.bildtest"\n' > "$P/tulpar.toml"
  stage() { local ad="$1"; shift; OUT=$(cd "$P" && env "$@" DISPLAY= "$T" build --target=android t.tpr "$ad" 2>&1); RC=$?; MAN="$P/${ad}_apk/AndroidManifest.xml"; }
  izin_var() { grep -q "$IZIN" "$MAN" 2>/dev/null; }
  stage varsayilan
  if echo "$OUT" | grep -qiE "NDK (bulunamadi|not found|gerekir)|needs the NDK|android/dist ars|android/dist archives"; then atla "NDK ya da android/dist arsivleri yok"; fi
  if [ $RC -eq 0 ] && grep -q 'android.permission.INTERNET' "$MAN" && ! izin_var; then gecti "KONTROL: varsayilanda INTERNET var, $IZIN yok"
  else dustu "varsayilan manifest beklenen degil (kod $RC)"; echo "$OUT" | tail -4; fi
  stage b1 TULPAR_ANDROID_BILDIRIM=1
  if [ $RC -eq 0 ] && izin_var; then gecti "TULPAR_ANDROID_BILDIRIM=1 -> $IZIN var"; else dustu "BILDIRIM=1 manifeste yansimadi (kod $RC)"; echo "$OUT" | tail -4; fi
  stage b0 TULPAR_ANDROID_BILDIRIM=0
  if [ $RC -eq 0 ] && [ -f "$MAN" ] && ! izin_var; then gecti "TULPAR_ANDROID_BILDIRIM=0 -> izin yok"; else dustu "BILDIRIM=0 beklenen degil (kod $RC)"; fi
  stage bozuk TULPAR_ANDROID_BILDIRIM=evet
  if [ $RC -ne 0 ] && echo "$OUT" | grep -q "TULPAR_ANDROID_BILDIRIM"; then gecti "KONTROL: BILDIRIM=evet derlemeyi durdurdu"; else dustu "BILDIRIM=evet kabul edildi (kod $RC)"; fi
  printf 'name = "bildtest"\n\n[android]\npackage = "dev.tulparlang.bildtest"\nbildirim = true\n' > "$P/tulpar.toml"
  stage tb
  if [ $RC -eq 0 ] && izin_var; then gecti "toml [android] bildirim = true -> $IZIN var"; else dustu "toml bildirim = true yansimadi (kod $RC)"; fi
  stage tbenv TULPAR_ANDROID_BILDIRIM=0
  if [ $RC -eq 0 ] && [ -f "$MAN" ] && ! izin_var; then gecti "env toml'u ezdi (BILDIRIM=0)"; else dustu "env toml'u ezmedi (kod $RC)"; fi
  printf 'name = "bildtest"\n\n[android]\npackage = "dev.tulparlang.bildtest"\nbildirim = "evet"\n' > "$P/tulpar.toml"
  stage tbozuk
  if echo "$OUT" | grep -q "line [0-9]*: \[android\] 'bildirim' must be" && ! izin_var; then gecti "KONTROL: toml bildirim = \"evet\" satir numarasiyla hata"; else dustu "toml bildirim = \"evet\" sessizce kabul edildi (kod $RC)"; echo "$OUT" | tail -3; fi
}
kos "$TULPAR"
YFAIL=$FAIL; YOK=$OK
if [ -n "$ESKI" ]; then
  ESKI="$(cd "$(dirname "$ESKI")" && pwd)/$(basename "$ESKI")"
  echo "-- pozitif kontrol: eski ikili ($ESKI) KIRMIZI gormeli --"
  kos "$ESKI" >"$TMP/eski.log" 2>&1
  if [ $FAIL -ne 0 ]; then echo "  gecti  KONTROL: eski ikili $(grep -c DUSTU "$TMP/eski.log") maddede KIRMIZI"; YOK=$((YOK + 1))
  else echo "  DUSTU  eski ikili de YESIL — kapi secenegi olcmuyor"; YFAIL=1; fi
fi
if [ $YFAIL -eq 0 ]; then echo "android bildirim kapisi: $YOK gecti"; exit 0; fi
echo "android bildirim kapisi: $YOK gecti, KIRMIZI"; exit 1
