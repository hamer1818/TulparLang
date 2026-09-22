#!/bin/bash
# args() GECIS KAPISI — argumanlar programa BOZULMADAN ulasiyor mu?
#
# tests/args.test.tpr argv'nin VAR oldugunu olcuyor ama HIC arguman
# gecirmiyor. Tam bu bosluk yuzunden Windows'ta su hata gorunmedi
# (olculdu 2026-09-21, CI):
#
#     sahne bulunamadi: 'examples/scenes/toplayici.scene.json'
#
# Tirnaklar kaynakta YOKTU. `tulpar` uretilen ikiliyi bir KABUK uzerinden
# calistiriyor (aot_pipeline `system()`) ve argumanlari POSIX sh yazimiyla
# TEK TIRNAKLA sariyordu; cmd.exe ise `'` karakterini siradan bir harf
# sayar, yani tirnaklar argumanin PARCASI oluyordu. Windows'ta arguman alan
# HER Tulpar programi etkileniyordu — bir CLI araci, sahne editoru, bu
# depodaki kod uretimi kapisi.
#
# Kapi platformdan bagimsiz: her yerde ayni alti degeri gecirip geri
# okuyor. Bir kabuk yaziminin digerine sizmasi burada hemen kirmizi olur.
set -u
cd "$(dirname "$0")/.."
TULPAR="${1:-./tulpar}"
[ -x "$TULPAR" ] || { echo "args gecis kapisi: $TULPAR yok — atlandi"; exit 0; }

TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT

cat > "$TMP/yaz.tpr" <<'TPREOF'
// Her argumani kose parantez icinde kendi satirina basar. Parantezler
// sinirlari gorunur kiliyor: bir kabuk tirnagi sizmissa cikti onu
// oldugu gibi gosterir.
array A = args();
int i = 1;                 // A[0] programin kendi yolu
while (i < length(A)) {
    print("[" + A[i] + "]");
    i = i + 1;
}
TPREOF

# Alti deger, her biri bir kabuk kuralini zorluyor. `%` BILEREK yok:
# cmd.exe onu cift tirnak icinde bile genisletir ve bu bilinen bir sinir
# (bkz. src/main.cpp'deki alintilama notu).
BEKLENEN="[sade]
[bosluk var]
[tek'tirnak]
[cift\"tirnak]
[ters\\bolu]
[--bayrak]"

ALINAN=$("$TULPAR" "$TMP/yaz.tpr" \
    "sade" "bosluk var" "tek'tirnak" 'cift"tirnak' 'ters\bolu' "--bayrak" \
    2>"$TMP/err")
RC=$?

if [ $RC -ne 0 ]; then
    echo "args gecis kapisi DUSTU: program cikis kodu $RC"
    sed -n '1,5p' "$TMP/err" | sed 's/^/    /'
    exit 1
fi

if [ "$ALINAN" != "$BEKLENEN" ]; then
    echo "args gecis kapisi DUSTU: argumanlar BOZULARAK ulasti"
    echo "  beklenen:"; printf '%s\n' "$BEKLENEN" | sed 's/^/      /'
    echo "  alinan:";   printf '%s\n' "$ALINAN"   | sed 's/^/      /'
    exit 1
fi

echo "args gecis kapisi temiz (6 arguman, kabuk yazimi sizmiyor)"
