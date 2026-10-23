#!/bin/bash
# TULPAR_ANDROID_LAN=1 KAPISI — yerel ağ (aynı Wi-Fi'da UDP yayınıyla oyuncu bulma)
# kullanan oyunun APK'sına android.permission.CHANGE_WIFI_MULTICAST_STATE girmeli;
# seçenek verilmeyen oyuna GİRMEMELİ.
#
# NEDEN (2026-10-23, tulpar-engine N2a, ağ 1v1): çoğu telefonun Wi-Fi sürücüsü
# yayın/çoklu yayın paketlerini WifiManager.MulticastLock yokken süzer — rakibi
# bulan yayın hiç gelmez, hata da vermez. Kilidi almak bu izni ister; izin yoksa
# acquire SecurityException atar. Ayrıca INTERNET'siz (TULPAR_ANDROID_INTERNET=0)
# LAN istemek DURDURUR: izinsiz soket EACCES ile açılmaz.
#
# ÖLÇER: staging manifesti (varsayılan / LAN=1 / LAN=0) + imzalı APK'nın
# `aapt2 dump permissions` çıktısı (paketin KENDİSİ).
# POZİTİF KONTROLLER: (a) varsayılanda INTERNET satırı GÖRÜLÜYOR (kapı izin satırını
# okuyabiliyor) ve LAN izni YOK; (b) LAN=evet ve LAN=1+INTERNET=0 derlemeyi DURDURUYOR;
# (c) ikinci argüman eski bir ikili ise (seçenekten önce) KIRMIZI görmeli.
# Araç yoksa GÖRÜNÜR atlanır ("android lan kapisi: ATLANDI: <sebep>", çıkış 0).
#
# Kullanım: tests/android_lan_izni.sh [tulpar] [eski_tulpar]
set -u
cd "$(dirname "$0")/.."
TULPAR="${1:-./tulpar}"
ESKI="${2:-}"
[ -x "$TULPAR" ] || { echo "android lan kapisi: $TULPAR yok"; exit 1; }
TULPAR="$(cd "$(dirname "$TULPAR")" && pwd)/$(basename "$TULPAR")"
export TULPAR_AOT_NOCACHE=1
unset TULPAR_ANDROID_INTERNET TULPAR_ANDROID_LAN

atla() { echo "android lan kapisi: ATLANDI: $1"; exit 0; }
SDK="${TULPAR_ANDROID_SDK:-$HOME/Android/Sdk}"
BT=$(ls -d "$SDK"/build-tools/* 2>/dev/null | sort -V | tail -1)
[ -n "$BT" ] && [ -x "$BT/aapt2" ] || atla "build-tools/aapt2 yok ($SDK)"

TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
IZIN='android.permission.CHANGE_WIFI_MULTICAST_STATE'

kos() {  # kos <tulpar> -> FAIL/OK
  local T="$1" P="$TMP/p" OUT RC MAN
  FAIL=0; OK=0
  gecti() { echo "  gecti  $1"; OK=$((OK + 1)); }
  dustu() { echo "  DUSTU  $1"; FAIL=1; }
  rm -rf "$P"; mkdir -p "$P"
  printf 'print("lan izni");\n' > "$P/t.tpr"
  printf 'name = "lantest"\n\n[android]\npackage = "dev.tulparlang.lantest"\n' > "$P/tulpar.toml"
  stage() {  # stage <cikti> [env...]
    local ad="$1"; shift
    OUT=$(cd "$P" && env "$@" DISPLAY= "$T" build --target=android t.tpr "$ad" 2>&1); RC=$?
    MAN="$P/${ad}_apk/AndroidManifest.xml"
  }
  izin_var() { grep -q "$IZIN" "$MAN" 2>/dev/null; }

  stage varsayilan
  if echo "$OUT" | grep -qiE "NDK (bulunamadi|not found|gerekir)|needs the NDK|android/dist ars|android/dist archives"; then
    atla "NDK ya da android/dist arsivleri yok"
  fi
  if [ $RC -eq 0 ] && grep -q 'android.permission.INTERNET' "$MAN" && ! izin_var; then
    gecti "KONTROL: varsayilan manifestte INTERNET var, $IZIN yok"
  else dustu "varsayilan manifest beklenen degil (kod $RC)"; echo "$OUT" | tail -4; fi

  stage lan TULPAR_ANDROID_LAN=1
  if [ $RC -eq 0 ] && izin_var && grep -q 'android.permission.INTERNET' "$MAN"; then
    gecti "TULPAR_ANDROID_LAN=1 -> $IZIN + INTERNET var"
  else dustu "TULPAR_ANDROID_LAN=1 manifeste yansimadi (kod $RC)"; echo "$OUT" | tail -4; fi

  stage lan0 TULPAR_ANDROID_LAN=0
  if [ $RC -eq 0 ] && [ -f "$MAN" ] && ! izin_var; then gecti "TULPAR_ANDROID_LAN=0 -> izin yok"
  else dustu "TULPAR_ANDROID_LAN=0 beklenen degil (kod $RC)"; fi

  stage bozuk TULPAR_ANDROID_LAN=evet
  if [ $RC -ne 0 ] && echo "$OUT" | grep -q "TULPAR_ANDROID_LAN"; then gecti "KONTROL: TULPAR_ANDROID_LAN=evet derlemeyi durdurdu (kod $RC)"
  else dustu "TULPAR_ANDROID_LAN=evet kabul edildi (kod $RC)"; fi

  stage celiski TULPAR_ANDROID_LAN=1 TULPAR_ANDROID_INTERNET=0
  if [ $RC -ne 0 ] && echo "$OUT" | grep -q "INTERNET"; then gecti "KONTROL: LAN=1 + INTERNET=0 derlemeyi durdurdu (kod $RC)"
  else dustu "LAN=1 + INTERNET=0 kabul edildi (kod $RC)"; fi

  local PK=""
  for d in "$(dirname "$T")/android" ./android; do
    [ -x "$d/package_apk.sh" ] && { PK="$d/package_apk.sh"; break; }
  done
  if [ -z "$PK" ] || [ ! -f "$HOME/.android/debug.keystore" ]; then
    echo "  ATLANDI: package_apk.sh ya da debug.keystore yok — APK izin dokumu olculmedi"
    return
  fi
  for ad in lan varsayilan; do
    (cd "$P" && bash "$PK" "${ad}_apk" "$ad.apk" >"$TMP/pk_$ad.log" 2>&1)
  done
  local DL DV
  DL=$("$BT/aapt2" dump permissions "$P/lan.apk" 2>&1)
  DV=$("$BT/aapt2" dump permissions "$P/varsayilan.apk" 2>&1)
  if echo "$DL" | grep -q "$IZIN"; then gecti "aapt2 dump permissions (LAN=1 APK): $IZIN var"
  else dustu "LAN=1 APK'sinda izin yok"; echo "$DL" | head -5; tail -3 "$TMP/pk_lan.log"; fi
  if echo "$DV" | grep -q "android.permission.INTERNET" && ! echo "$DV" | grep -q "$IZIN"; then
    gecti "KONTROL: aapt2 dump permissions (varsayilan APK): INTERNET var, $IZIN yok"
  else dustu "varsayilan APK izin dokumu beklenen degil"; echo "$DV" | head -5; fi
}

kos "$TULPAR"
YFAIL=$FAIL; YOK=$OK
if [ -n "$ESKI" ]; then
  ESKI="$(cd "$(dirname "$ESKI")" && pwd)/$(basename "$ESKI")"
  echo "-- pozitif kontrol: eski ikili ($ESKI) KIRMIZI gormeli --"
  kos "$ESKI" >"$TMP/eski.log" 2>&1
  if [ $FAIL -ne 0 ]; then
    echo "  gecti  KONTROL: eski ikili $(grep -c DUSTU "$TMP/eski.log") maddede KIRMIZI"
    YOK=$((YOK + 1))
  else
    echo "  DUSTU  eski ikili de YESIL — kapi secenegi olcmuyor"; YFAIL=1
  fi
fi
if [ $YFAIL -eq 0 ]; then echo "android lan kapisi: $YOK gecti"; exit 0; fi
echo "android lan kapisi: $YOK gecti, KIRMIZI"; exit 1
