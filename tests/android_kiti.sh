#!/bin/bash
# Android kiti kurulum kapisi — `tulpar update --android`in yerlestirme yolu, AGSIZ.
#
# Surumler tulpar-android-kit.tar.gz yayinliyor (build.yml "Android kiti"); update onu
# ikilinin yanina android/ olarak acar. Iddia: (1) tam kit kurulur, eskisinin yerine
# gecer; (2) eksik dosyali ya da baska surumlu kit REDDEDILIR ve eski kit YERINDE kalir
# (yarim kit APK'yi derlemede degil cihazda bozardi). (2) olmadan (1) bir sey olcmezdi:
# her seyi kuran bir yol da (1)'den gecer.
set -u
cd "$(dirname "$0")/.."
TULPAR="${1:-./tulpar}"
[ -x "$TULPAR" ] || { echo "android kiti kapisi: $TULPAR yok — atlandi"; exit 0; }
case "$(uname -s)" in MINGW*|MSYS*|CYGWIN*) echo "android kiti kapisi: Windows host Android hedefini derlemiyor — atlandi"; exit 0;; esac

T=$(mktemp -d "$PWD/.tulpar-android-kiti-XXXXXX") || exit 1
trap 'rm -rf "$T"' EXIT
kirmizi=0
dus() { echo "  DUSTU  $1"; kirmizi=1; }
gec() { echo "  gecti  $1"; }

kit() {  # kit <ad> <surum> [atlanacak dosya]
    local d="$T/k_$1/android"
    mkdir -p "$d/dist/arm64-v8a" "$d/dist/x86_64"
    for f in package_apk.sh package_aab.sh install_run.sh symbolize.sh build_tame_android.sh \
             dist/arm64-v8a/libtulpar_runtime_android.a dist/arm64-v8a/libtulpar_tame_android.a \
             dist/x86_64/libtulpar_runtime_android.a dist/x86_64/libtulpar_tame_android.a; do
        [ "$f" = "${3:-}" ] && continue
        echo "$1 $f" > "$d/$f"
    done
    echo "TulparLang $2" > "$d/SURUM"
    tar -C "$T/k_$1" -czf "$T/$1.tar.gz" android
}
HEDEF="$T/kurulum"; mkdir -p "$HEDEF"

kit bir v9.0.0
"$TULPAR" update --test-android-kit="$T/bir.tar.gz,$HEDEF,v9.0.0" >/dev/null 2>&1 \
    && grep -qx 'TulparLang v9.0.0' "$HEDEF/android/SURUM" && [ -f "$HEDEF/android/dist/x86_64/libtulpar_tame_android.a" ] \
    && gec "(1) tam kit kuruldu" || dus "(1) tam kit kurulmadi"

kit iki v9.0.1
"$TULPAR" update --test-android-kit="$T/iki.tar.gz,$HEDEF,v9.0.1" >/dev/null 2>&1 \
    && grep -qx 'TulparLang v9.0.1' "$HEDEF/android/SURUM" && grep -q '^iki ' "$HEDEF/android/package_apk.sh" \
    && [ -z "$(ls -A "$HEDEF" | grep -v '^android$')" ] \
    && gec "(1) yeni kit eskisinin yerine gecti, artik dizin kalmadi" || dus "(1) yenileme: $(ls -A "$HEDEF" | tr '\n' ' ')"

kit eksik v9.0.2 dist/arm64-v8a/libtulpar_runtime_android.a
if "$TULPAR" update --test-android-kit="$T/eksik.tar.gz,$HEDEF,v9.0.2" >"$T/eksik.out" 2>&1; then
    dus "(2) eksik dosyali kit KURULDU"
else
    grep -q 'libtulpar_runtime_android.a' "$T/eksik.out" && grep -qx 'TulparLang v9.0.1' "$HEDEF/android/SURUM" \
        && gec "(2) eksik dosyali kit reddedildi, eski kit yerinde" || dus "(2) eksik: $(cat "$T/eksik.out")"
fi

kit baska v9.0.3
if "$TULPAR" update --test-android-kit="$T/baska.tar.gz,$HEDEF,v9.0.4" >"$T/baska.out" 2>&1; then
    dus "(2) baska surumlu kit KURULDU"
else
    grep -q 'v9.0.3' "$T/baska.out" && grep -qx 'TulparLang v9.0.1' "$HEDEF/android/SURUM" \
        && gec "(2) baska surumlu kit reddedildi, eski kit yerinde" || dus "(2) surum: $(cat "$T/baska.out")"
fi

[ "$kirmizi" = 0 ] && echo "android kiti kapisi: gecti" || { echo "android kiti kapisi: KIRMIZI"; exit 1; }
