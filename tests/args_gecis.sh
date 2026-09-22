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

# `tr -d '\r'` SART. Windows'ta `print()` satir sonunu CRLF olarak yaziyor;
# karsilastirdigimiz BEKLENEN ise kabuk tarafinda duz LF. Bu kapi ilk
# kosumunda tam olarak bunun yuzunden kirmizi dondu ve cikti GORSEL OLARAK
# birebir ayniydi (olculdu 2026-09-22, Windows CI) — yani fark gorunmuyordu
# ve kapi neyi olctugunu yanlis soyluyordu. Burada olculen sey ARGUMANIN
# KENDISI; satir sonu bicimi bu kapinin konusu degil.
# Cikti DOSYAYA aliniyor, boru hattina DEGIL: bir boruda `$?` son halkanin
# (yani `tr`'nin) kodunu verir ve programin gercek cikis kodu KAYBOLUR.
"$TULPAR" "$TMP/yaz.tpr" \
    "sade" "bosluk var" "tek'tirnak" 'cift"tirnak' 'ters\bolu' "--bayrak" \
    > "$TMP/out" 2>"$TMP/err"
RC=$?
ALINAN=$(tr -d '\r' < "$TMP/out")

if [ $RC -ne 0 ]; then
    echo "args gecis kapisi DUSTU: program cikis kodu $RC"
    sed -n '1,5p' "$TMP/err" | sed 's/^/    /'
    exit 1
fi

if [ "$ALINAN" != "$BEKLENEN" ]; then
    echo "args gecis kapisi DUSTU: argumanlar BOZULARAK ulasti"
    echo "  beklenen:"; printf '%s\n' "$BEKLENEN" | sed 's/^/      /'
    echo "  alinan:";   printf '%s\n' "$ALINAN"   | sed 's/^/      /'
    # GORUNMEYEN FARK. Bu kapi bir kez iki liste GORSEL OLARAK birebir
    # ayniyken dustu (CR'ler); yukaridaki iki blok bakan insana "ayni" diyor
    # ve yanlis yere baktiriyordu. `sed -n l` kacis bicimini basiyor, yani
    # \r / \t / UTF-8 baytlari gorunur oluyor.
    echo "  alinan (kacis bicimi):"
    printf '%s\n' "$ALINAN" | sed -n l | sed 's/^/      /'
    exit 1
fi

echo "args gecis kapisi temiz (6 arguman, kabuk yazimi sizmiyor)"
