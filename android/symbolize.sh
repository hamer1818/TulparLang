#!/usr/bin/env bash
# Android yigin izindeki adresi fonksiyon adina cevirir.
#
# NEDEN VAR: APK'ya giren .so STRIPLENIR (36 MB -> 5.4 MB olculdu). Strip,
# cihazdaki izde fonksiyon ADLARINI oldurur — ve bu depoda o adlar bir ise
# yaradi: `t_menu_ciz.f+576` satiri Android x86_64 kod uretimindeki hizalama
# hatasini tam yerinden gosterdi (Tuzaklar 8ap). Striplenmemis kopya bu yuzden
# atilmiyor, `<stage>/symbols/<abi>/libtulpargame.so` altinda duruyor; bu betik
# adresi oradan cozer. Yani boyut kazanci alinir, teshis yetenegi kalir.
#
# KULLANIM
#   android/symbolize.sh <stage_dizini> <abi> <adres...>
#   adb logcat -d | android/symbolize.sh <stage_dizini> <abi>     # borudan
#
# Adres bicimleri kabul edilir: 0x29771b | 29771b | "libtulpargame.so+0x29771b"
# Borudan okurken satirlardaki `libtulpargame.so+0xADRES` ve `pc ADRES` kaliplari
# taranir; eslesen her satirin altina cozum yazilir.
set -u

if [ $# -lt 2 ]; then
  echo "kullanim: $0 <stage_dizini> <abi> [adres...]" >&2
  echo "  ornek : $0 out/aksiyon_apk x86_64 0x29771b" >&2
  echo "  ornek : adb logcat -d | $0 out/aksiyon_apk x86_64" >&2
  exit 2
fi
STAGE="$1"; ABI="$2"; shift 2

SYM="$STAGE/symbols/$ABI/libtulpargame.so"
if [ ! -f "$SYM" ]; then
  echo "HATA: sembol kopyasi yok: $SYM" >&2
  echo "  Strip TULPAR_ANDROID_NO_STRIP=1 ile kapatilmis olabilir; o durumda" >&2
  echo "  APK'daki .so zaten striplenmemistir: $STAGE/lib/$ABI/libtulpargame.so" >&2
  exit 1
fi

# llvm-symbolizer: NDK'da ya da PATH'te. NDK arama sirasi driver ile ayni.
SYMBOLIZER="$(command -v llvm-symbolizer || true)"
if [ -z "$SYMBOLIZER" ]; then
  NDK="${TULPAR_ANDROID_NDK:-}"
  if [ -z "$NDK" ]; then NDK="$(ls -d "$HOME"/Android/Sdk/ndk/* "$HOME"/Android/android-ndk-* 2>/dev/null | head -1)"; fi
  if [ -n "$NDK" ]; then
    CAND="$NDK/toolchains/llvm/prebuilt/linux-x86_64/bin/llvm-symbolizer"
    [ -x "$CAND" ] && SYMBOLIZER="$CAND"
  fi
fi
if [ -z "$SYMBOLIZER" ]; then
  echo "HATA: llvm-symbolizer bulunamadi (PATH ya da NDK)." >&2
  exit 1
fi

coz() { # $1 = ham adres
  local a="$1"
  a="${a##*+}"                       # "libtulpargame.so+0x123" -> "0x123"
  case "$a" in 0x*|0X*) ;; *) a="0x$a";; esac
  local out
  out="$("$SYMBOLIZER" --obj="$SYM" --demangle --functions=linkage --inlines "$a" 2>/dev/null \
        | sed '/^$/d' | paste -sd' <- ' -)"
  if [ -z "$out" ] || [ "$out" = "?? ??:0" ]; then
    echo "  $a -> COZULEMEDI (adres bu .so'ya ait mi? ABI dogru mu?)"
  else
    echo "  $a -> $out"
  fi
}

if [ $# -gt 0 ]; then
  for a in "$@"; do coz "$a"; done
  exit 0
fi

# Borudan: eslesen satiri oldugu gibi bas, altina cozumu yaz.
bulundu=0
while IFS= read -r line; do
  printf '%s\n' "$line"
  adr="$(printf '%s' "$line" | grep -oE 'libtulpargame\.so\+0x[0-9a-fA-F]+' | head -1)"
  if [ -z "$adr" ]; then
    adr="$(printf '%s' "$line" | grep -oE 'pc +[0-9a-fA-F]{6,16}' | head -1 | awk '{print $2}')"
  fi
  if [ -n "$adr" ]; then coz "$adr"; bulundu=1; fi
done
if [ "$bulundu" = "0" ]; then
  echo "(cozulecek adres iceren satir bulunamadi — girdi gercekten bir yigin izi mi?)" >&2
fi
