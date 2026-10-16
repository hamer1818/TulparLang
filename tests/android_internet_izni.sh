#!/bin/bash
# `[android] internet = false` KAPISI — interneti kullanmayan bir oyunun APK'sına
# android.permission.INTERNET girmemeli; seçenek verilmeyen eski oyun izni
# KORUMALI (http_get / çevrimiçi skor sessizce kırılmasın).
#
# NEDEN (2026-10-16, tulpar-engine "Küpler ile Küreler" yayın hazırlığı):
# derleyici manifeste İNTERNET iznini koşulsuz yazıyordu. Oyun ağ kullanmıyor,
# ama mağaza sayfası izni gösteriyor ve Play "Veri güvenliği" formu ağ
# erişimini sorguluyordu. İzin artık toml `[android] internet = false` ya da
# TULPAR_ANDROID_INTERNET=0 ile kalkıyor (env toml'u ezer; eski bir tulpar'ın
# toml'u okuyabilmesi için anahtarı env'den veren betikler var).
#
# ÖLÇER: (1) staging manifesti — varsayılan / toml false / toml true + env 0 /
# toml false + env 1 / tırnaklı "false"; (2) imzalı APK'nın `aapt2 dump
# permissions` çıktısı (manifest metni değil, paketin KENDİSİ): false → İNTERNET
# yok, VIBRATE var; varsayılan → İNTERNET var.
#
# POZİTİF KONTROLLER: (a) varsayılan derlemede izin VAR — kapının "izin yok"
# maddeleri boş geçmiyor, izin satırını gerçekten görüyor; (b) `internet = "evet"`
# ve TULPAR_ANDROID_INTERNET=hayir derlemeyi DURDURUYOR (yazım hatası sessizce
# izni bırakmıyor); (c) ikinci argüman eski bir ikili ise (ör. b72260d8-dirty, seçenekten
# önce) aynı senaryo ona karşı KIRMIZI görmeli.
#
# Araç yoksa (NDK / android/dist / aapt2) GÖRÜNÜR atlanır: "android internet
# kapisi: ATLANDI: <sebep>", çıkış 0.
#
# Kullanım: tests/android_internet_izni.sh [tulpar] [eski_tulpar]
set -u
cd "$(dirname "$0")/.."
TULPAR="${1:-./tulpar}"
ESKI="${2:-}"
[ -x "$TULPAR" ] || { echo "android internet kapisi: $TULPAR yok"; exit 1; }
TULPAR="$(cd "$(dirname "$TULPAR")" && pwd)/$(basename "$TULPAR")"
export TULPAR_AOT_NOCACHE=1
unset TULPAR_ANDROID_INTERNET

atla() { echo "android internet kapisi: ATLANDI: $1"; exit 0; }
SDK="${TULPAR_ANDROID_SDK:-$HOME/Android/Sdk}"
BT=$(ls -d "$SDK"/build-tools/* 2>/dev/null | sort -V | tail -1)
[ -n "$BT" ] && [ -x "$BT/aapt2" ] || atla "build-tools/aapt2 yok ($SDK)"

TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
IZIN='android.permission.INTERNET'

kos() {  # kos <tulpar> -> FAIL/OK sayaçları
  local T="$1" P="$TMP/p" OUT RC MAN
  FAIL=0; OK=0
  gecti() { echo "  gecti  $1"; OK=$((OK + 1)); }
  dustu() { echo "  DUSTU  $1"; FAIL=1; }
  rm -rf "$P"; mkdir -p "$P"
  printf 'print("internet izni");\n' > "$P/t.tpr"
  toml() {  # toml <internet satiri ya da bos>
    printf 'name = "izintest"\n\n[android]\npackage = "dev.tulparlang.izintest"\n%s\n' "$1" > "$P/tulpar.toml"
  }
  stage() {  # stage <cikti> [env...] -> MAN yolu, RC, OUT
    local ad="$1"; shift
    OUT=$(cd "$P" && env "$@" DISPLAY= "$T" build --target=android t.tpr "$ad" 2>&1); RC=$?
    MAN="$P/${ad}_apk/AndroidManifest.xml"
  }
  izin_var() { grep -q "$IZIN" "$MAN" 2>/dev/null; }

  # (a) varsayılan: izin VAR (eski davranış korunuyor + kapı izni görüyor)
  toml ""
  stage varsayilan
  if echo "$OUT" | grep -qiE "NDK (bulunamadi|not found|gerekir)|needs the NDK|android/dist ars|android/dist archives"; then
    atla "NDK ya da android/dist arsivleri yok"
  fi
  if [ $RC -eq 0 ] && izin_var; then gecti "KONTROL: secenek yokken manifestte INTERNET var"
  else dustu "varsayilan manifestte INTERNET yok (kod $RC)"; echo "$OUT" | tail -4; fi

  # (1) toml internet = false -> yok; VIBRATE etkilenmiyor
  toml "internet = false"
  stage kapali
  if [ $RC -eq 0 ] && [ -f "$MAN" ] && ! izin_var && grep -q 'android.permission.VIBRATE' "$MAN"; then
    gecti "toml internet = false -> INTERNET yok, VIBRATE duruyor"
  else dustu "toml internet = false manifeste yansimadi (kod $RC)"; echo "$OUT" | tail -4; fi

  # tırnaklı değer de kabul
  toml 'internet = "false"'
  stage tirnakli
  if [ $RC -eq 0 ] && [ -f "$MAN" ] && ! izin_var; then gecti 'toml internet = "false" -> INTERNET yok'
  else dustu 'toml internet = "false" yansimadi'; fi

  # env toml'u ezer (iki yönde)
  toml "internet = true"
  stage env0 TULPAR_ANDROID_INTERNET=0
  if [ $RC -eq 0 ] && [ -f "$MAN" ] && ! izin_var; then gecti "toml true + TULPAR_ANDROID_INTERNET=0 -> INTERNET yok"
  else dustu "TULPAR_ANDROID_INTERNET=0 toml'u ezmedi"; fi
  toml "internet = false"
  stage env1 TULPAR_ANDROID_INTERNET=1
  if [ $RC -eq 0 ] && izin_var; then gecti "toml false + TULPAR_ANDROID_INTERNET=1 -> INTERNET var"
  else dustu "TULPAR_ANDROID_INTERNET=1 toml'u ezmedi"; fi

  # (b) yazım hataları DURDURUR
  toml 'internet = "evet"'
  stage bozuk1
  # (toml'daki biçim hatası her anahtarda olduğu gibi dosyanın [android]
  # ayarlarını düşürür ve bunu satır numarasıyla SÖYLER — sessiz değil.)
  if echo "$OUT" | grep -q "'internet' must be"; then gecti "KONTROL: internet = \"evet\" hata olarak raporlandi"
  else dustu "internet = \"evet\" sessizce gecti (kod $RC)"; echo "$OUT" | tail -3; fi
  toml ""
  stage bozuk2 TULPAR_ANDROID_INTERNET=hayir
  if [ $RC -ne 0 ] && echo "$OUT" | grep -q "TULPAR_ANDROID_INTERNET"; then
    gecti "KONTROL: TULPAR_ANDROID_INTERNET=hayir derlemeyi durdurdu (kod $RC)"
  else dustu "TULPAR_ANDROID_INTERNET=hayir kabul edildi (kod $RC)"; fi

  # (2) imzalı APK'nın kendisi (aapt2 dump permissions)
  local PK=""
  for d in "$(dirname "$T")/android" ./android; do
    [ -x "$d/package_apk.sh" ] && { PK="$d/package_apk.sh"; break; }
  done
  if [ -z "$PK" ] || [ ! -f "$HOME/.android/debug.keystore" ]; then
    echo "  ATLANDI: package_apk.sh ya da debug.keystore yok — APK izin dokumu olculmedi"
    ATLANAN=1
    return
  fi
  for ad in kapali varsayilan; do
    (cd "$P" && bash "$PK" "${ad}_apk" "$ad.apk" >"$TMP/pk_$ad.log" 2>&1)
  done
  local DK DV
  DK=$("$BT/aapt2" dump permissions "$P/kapali.apk" 2>&1)
  DV=$("$BT/aapt2" dump permissions "$P/varsayilan.apk" 2>&1)
  if echo "$DK" | grep -q "android.permission.VIBRATE" && ! echo "$DK" | grep -q "$IZIN"; then
    gecti "aapt2 dump permissions (internet = false APK): INTERNET yok"
  else dustu "internet = false APK'sinda izin dokumu beklenen degil"; echo "$DK" | head -5; tail -3 "$TMP/pk_kapali.log"; fi
  if echo "$DV" | grep -q "$IZIN"; then gecti "KONTROL: aapt2 dump permissions (varsayilan APK): INTERNET var"
  else dustu "varsayilan APK'da INTERNET yok"; echo "$DV" | head -5; fi
}

ATLANAN=0
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
if [ $YFAIL -eq 0 ]; then echo "android internet kapisi: $YOK gecti"; exit 0; fi
echo "android internet kapisi: $YOK gecti, KIRMIZI"; exit 1
