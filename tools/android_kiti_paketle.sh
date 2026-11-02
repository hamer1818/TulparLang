#!/bin/bash
# Android kiti paketleyici: tulpar-android-kit.tar.gz (surum varligi, 2026-11-02).
#
#   tools/android_kiti_paketle.sh <android kaynak dizini> <dist dizini> <surum metni> <cikti.tar.gz>
#
# <dist dizini> altinda <abi>/libtulpar_{runtime,tame}_android.a beklenir
# (android/build_tame_android.sh'in ciktisi). <surum metni> `tulpar --version`
# bicimi: "TulparLang v3.44.0" — `tulpar update --android` ve Android derlemesi
# kitin SURUM'unu ikilinin surumuyle karsilastirir (src/cli/update_cmd.cpp).
#
# Iki yerde kosar: dist-arsivleri isinde HER PR'da (deneme paketi; yapi kirilirsa
# PR kirmizi — yayin etiketinde ilk kez kirilmasin) ve release isinde (yayinlanan).
# Zorunlu dosya listesi update_cmd.cpp install_android_kit'teki kZorunlu ile ayni.
set -euo pipefail
[ $# -eq 4 ] || { echo "kullanim: $0 <android> <dist> <surum> <cikti.tar.gz>" >&2; exit 2; }
KAYNAK="$1"; DIST="$2"; SURUM="$3"; CIKTI="$4"
case "$SURUM" in "TulparLang "*) ;; *) echo "HATA: surum metni 'TulparLang <etiket>' olmali: '$SURUM'" >&2; exit 2;; esac
G="$(mktemp -d)"; trap 'rm -rf "$G"' EXIT
mkdir -p "$G/android"
for f in package_apk.sh package_aab.sh install_run.sh symbolize.sh build_tame_android.sh; do
    test -s "$KAYNAK/$f" || { echo "HATA: kit: $KAYNAK/$f yok" >&2; exit 1; }
    cp "$KAYNAK/$f" "$G/android/"
done
for abi in arm64-v8a x86_64; do
    mkdir -p "$G/android/dist/$abi"
    for a in libtulpar_runtime_android.a libtulpar_tame_android.a; do
        test -s "$DIST/$abi/$a" || { echo "HATA: kit: $DIST/$abi/$a yok" >&2; exit 1; }
        cp "$DIST/$abi/$a" "$G/android/dist/$abi/"
    done
done
printf '%s\n' "$SURUM" > "$G/android/SURUM"
tar -C "$G" -czf "$CIKTI" android
LISTE="$(tar -tzf "$CIKTI")"
for f in android/package_apk.sh android/package_aab.sh android/SURUM \
         android/dist/arm64-v8a/libtulpar_runtime_android.a android/dist/arm64-v8a/libtulpar_tame_android.a \
         android/dist/x86_64/libtulpar_runtime_android.a android/dist/x86_64/libtulpar_tame_android.a; do
    grep -qx "$f" <<<"$LISTE" || { echo "HATA: kit: $f arsivde yok" >&2; exit 1; }
done
echo "android kiti: $CIKTI ($(wc -c < "$CIKTI") bayt, $(grep -c . <<<"$LISTE") girdi, $SURUM)"
