#!/bin/bash
# [android] url_scheme KAPISI — semasi verilen oyunun manifestinde etkinlige VIEW/DEFAULT/BROWSABLE
# intent-filter'i (<data android:scheme=...>) ve launchMode="singleTask" girmeli; verilmeyende GIRMEMELI.
# Neden (2026-11-09, Warlords of Era Google girisi): tarayicidaki OAuth donus sayfasinin "oyuna don"
# baglantisi calisan oyunu one getirsin; singleTask olmadan Chrome'un gorevinde ikinci NativeActivity acilir.
# OLCER: staging manifesti (varsayilan / env / toml / env toml'u ezer / gecersiz sema DURDURUR).
# POZITIF KONTROLLER: varsayilanda LAUNCHER filtresi GORULUYOR (kapi etkinligi okuyabiliyor); http ve
# buyuk harfli sema derlemeyi DURDURUYOR; ikinci arguman eski ikili ise KIRMIZI gormeli.
# Kullanim: tests/android_url_semasi.sh [tulpar] [eski_tulpar]
set -u
cd "$(dirname "$0")/.."
TULPAR="${1:-./tulpar}"
ESKI="${2:-}"
[ -x "$TULPAR" ] || { echo "android url semasi kapisi: $TULPAR yok"; exit 1; }
TULPAR="$(cd "$(dirname "$TULPAR")" && pwd)/$(basename "$TULPAR")"
export TULPAR_AOT_NOCACHE=1
unset TULPAR_ANDROID_URL_SCHEME
atla() { echo "android url semasi kapisi: ATLANDI: $1"; exit 0; }
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
kos() {
  local T="$1" P="$TMP/p" OUT RC MAN
  FAIL=0; OK=0
  gecti() { echo "  gecti  $1"; OK=$((OK + 1)); }
  dustu() { echo "  DUSTU  $1"; FAIL=1; }
  rm -rf "$P"; mkdir -p "$P"
  printf 'print("sema");\n' > "$P/t.tpr"
  printf 'name = "sematest"\n\n[android]\npackage = "dev.tulparlang.sematest"\n' > "$P/tulpar.toml"
  stage() { local ad="$1"; shift; OUT=$(cd "$P" && env "$@" DISPLAY= "$T" build --target=android t.tpr "$ad" 2>&1); RC=$?; MAN="$P/${ad}_apk/AndroidManifest.xml"; }
  sema_var() { grep -q "android:scheme=\"$1\"" "$MAN" 2>/dev/null && grep -q 'android.intent.category.BROWSABLE' "$MAN" && grep -q 'android:launchMode="singleTask"' "$MAN"; }
  hic_yok() { [ -f "$MAN" ] && ! grep -q 'android:scheme=' "$MAN" && ! grep -q 'BROWSABLE' "$MAN" && ! grep -q 'launchMode' "$MAN"; }
  stage varsayilan
  if echo "$OUT" | grep -qiE "NDK (bulunamadi|not found|gerekir)|needs the NDK|android/dist ars|android/dist archives"; then atla "NDK ya da android/dist arsivleri yok"; fi
  if [ $RC -eq 0 ] && grep -q 'android.intent.category.LAUNCHER' "$MAN" && hic_yok; then gecti "KONTROL: varsayilanda LAUNCHER var, sema/BROWSABLE/launchMode yok"
  else dustu "varsayilan manifest beklenen degil (kod $RC)"; echo "$OUT" | tail -4; fi
  stage e1 TULPAR_ANDROID_URL_SCHEME=woe
  if [ $RC -eq 0 ] && sema_var woe; then gecti "TULPAR_ANDROID_URL_SCHEME=woe -> filtre + singleTask"; else dustu "env semasi manifeste yansimadi (kod $RC)"; echo "$OUT" | tail -4; fi
  stage e2 TULPAR_ANDROID_URL_SCHEME=http
  if [ $RC -ne 0 ] && echo "$OUT" | grep -q "url_scheme"; then gecti "KONTROL: sema http derlemeyi durdurdu"; else dustu "sema http kabul edildi (kod $RC)"; fi
  stage e3 TULPAR_ANDROID_URL_SCHEME=Woe
  if [ $RC -ne 0 ] && echo "$OUT" | grep -q "url_scheme"; then gecti "KONTROL: buyuk harfli sema derlemeyi durdurdu"; else dustu "sema Woe kabul edildi (kod $RC)"; fi
  printf 'name = "sematest"\n\n[android]\npackage = "dev.tulparlang.sematest"\nurl_scheme = "sematest"\n' > "$P/tulpar.toml"
  stage t1
  if [ $RC -eq 0 ] && sema_var sematest; then gecti "toml url_scheme = \"sematest\" -> filtre + singleTask"; else dustu "toml semasi yansimadi (kod $RC)"; echo "$OUT" | tail -4; fi
  stage t2 TULPAR_ANDROID_URL_SCHEME=
  if [ $RC -eq 0 ] && hic_yok; then gecti "env bos dizgi toml'u ezdi (sema yok)"; else dustu "env toml'u ezmedi (kod $RC)"; fi
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
if [ $YFAIL -eq 0 ]; then echo "android url semasi kapisi: $YOK gecti"; exit 0; fi
echo "android url semasi kapisi: $YOK gecti, KIRMIZI"; exit 1
