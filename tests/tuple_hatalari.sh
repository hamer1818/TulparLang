#!/usr/bin/env bash
# Coklu donus / tuple HATA YOLLARI (P0.1, 2026-09-21). Neden .test.tpr degil:
# hepsi AYRISTIRMA hatasi (bkz. tests/enum_hatalari.sh basligi). Her fixture
# `tulpar typecheck` ile kosulur: cikis 2 + beklenen mesaj parcasi; pozitif
# kontrol gecerli kaynagin 0 verdigini kanitlar.
#
#   tests/tuple_hatalari.sh [tulpar_yolu]
set -uo pipefail
TUL="${1:-./tulpar}"
[ -x "$TUL" ] || { echo "HATA: '$TUL' calistirilabilir degil" >&2; exit 1; }
TUL="$(cd "$(dirname "$TUL")" && pwd)/$(basename "$TUL")"
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
fail=0
gecti() { printf "  \033[0;32mgecti\033[0m  %s\n" "$1"; }
dustu() { printf "  \033[0;31mDUSTU\033[0m  %s\n" "$1"; fail=1; }
reddedilmeli() {
    local ad="$1" bekle="$2" kaynak="$3"
    printf '%s\n' "$kaynak" > "$TMP/$ad.tpr"
    local out rc
    out=$(cd "$TMP" && "$TUL" typecheck "$ad.tpr" 2>&1); rc=$?
    if [ "$rc" -ne 2 ]; then
        dustu "$ad: cikis 2 (ayristirma hatasi) bekleniyordu, $rc geldi"
        echo "$out" | sed 's/^/         /'
    elif ! echo "$out" | grep -qF -- "$bekle"; then
        dustu "$ad: reddedildi ama beklenen mesaj yok: '$bekle'"
        echo "$out" | sed 's/^/         /'
    else
        gecti "$ad"
    fi
}

reddedilmeli tuple_olmayan_fonksiyonda_coklu_return "coklu 'return' yalnizca" \
'func f(): float { return 1.0, 2.0; }'

reddedilmeli return_deger_sayisi "3 deger veriyor, fonksiyon 2 tip" \
'func f(): (float, float) { return 1.0, 2.0, 3.0; }'

reddedilmeli bildirim_ad_sayisi "3 ad, 2 deger" \
'func f(): (float, float) { return 1.0, 2.0; }
float a, b, c = f();'

reddedilmeli suslu_parantezsiz_govde "suslu parantezli govde" \
'func f(): (float, float) { return 1.0, 2.0; }
int x = 1;
if (x > 0) float a, b = f();'

reddedilmeli tek_degiskene_baglama "tek degiskene baglanamaz" \
'func f(): (float, float) { return 1.0, 2.0; }
var t = f();'

reddedilmeli sag_taraf_cagri_degil "dogrudan cagrisi olmali" \
'float a, b = 5;'

reddedilmeli tek_tipli_liste "en az iki tip" \
'func f(): (float) { return 1.0; }'

reddedilmeli lambda_icinde_coklu_return "coklu 'return' yalnizca" \
'func f(): (float, float) {
    var g = (x) => { return x, x; };
    return 1.0, 2.0;
}'

# Pozitif kontrol.
printf '%s\n' 'float a, b = f();
func f(): (float, float) { return 1.0, 2.0; }
a, b = f();
int n, float h = g(3);
func g(int k): (int, float) { return k, k * 0.5; }' > "$TMP/gecerli.tpr"
out=$(cd "$TMP" && "$TUL" typecheck gecerli.tpr 2>&1); rc=$?
if [ "$rc" -eq 0 ]; then gecti "gecerli tuple kabul (pozitif kontrol)"
else dustu "gecerli tuple reddedildi (rc=$rc)"; echo "$out" | sed 's/^/         /'; fi

if [ "$fail" -eq 0 ]; then echo "tuple_hatalari: 9/9"; fi
exit $fail
