#!/bin/bash
# AGDK games-frame-pacing (Swappy) AAR'indan prefab statik kutuphaneleri ve basliklari
# -> engine/third_party/swappy/{include,libs/<abi>} (libs gitignore'lu). Apache-2.0.
# Gradle gerekmez: Google Maven'dan dogrudan AAR (zip).
set -e
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
DST="$ROOT/engine/third_party/swappy"
META="https://dl.google.com/android/maven2/androidx/games/games-frame-pacing/maven-metadata.xml"
VER="${1:-$(curl -s --fail "$META" | grep -o '<release>[^<]*' | sed 's/<release>//')}"
[ -n "$VER" ] || { echo "HATA: surum bulunamadi"; exit 1; }
TMP="$(mktemp -d)"
echo "indiriliyor: games-frame-pacing $VER"
curl -sL --fail -o "$TMP/a.aar" "https://dl.google.com/android/maven2/androidx/games/games-frame-pacing/$VER/games-frame-pacing-$VER.aar"
(cd "$TMP" && unzip -q a.aar)
mkdir -p "$DST/include/swappy" "$DST/include/common"
cp "$TMP"/prefab/modules/swappy_static/include/swappy/*.h "$DST/include/swappy/"
cp "$TMP"/prefab/modules/swappy_static/include/common/*.h "$DST/include/common/"
for abi in arm64-v8a x86_64; do
    mkdir -p "$DST/libs/$abi"
    cp "$TMP/prefab/modules/swappy_static/libs/android.$abi/libswappy_static.a" "$DST/libs/$abi/"
done
echo "$VER" > "$DST/VERSION"
rm -rf "$TMP"
ls -la "$DST"/libs/*/
