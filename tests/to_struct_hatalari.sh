#!/usr/bin/env bash
# `to_struct(json, "Ad")` DERLEME ZAMANI hata yollari (K133, 2026-09-28):
# hedef tip derleme zamaninda bilinmeli — ikinci arguman bir struct'i adlayan
# dizgi SABITI. Her hata `tulpar build` cikis kodu != 0 ve beklenen mesaj
# ister. Pozitif kontrol: gecerli kaynak derlenir ve dogru sonucu basar.
# (Calisma zamani hatalari — eksik/yanlis tipli alan — tests/to_struct.test.tpr.)
#
#   tests/to_struct_hatalari.sh [tulpar_yolu]
set -uo pipefail
TUL="${1:-./tulpar}"
[ -x "$TUL" ] || { echo "HATA: '$TUL' calistirilabilir degil" >&2; exit 1; }
TUL="$(cd "$(dirname "$TUL")" && pwd)/$(basename "$TUL")"
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
fail=0
n_gecti=0
gecti() { n_gecti=$((n_gecti + 1)); printf "  \033[0;32mgecti\033[0m  %s\n" "$1"; }
dustu() { printf "  \033[0;31mDUSTU\033[0m  %s\n" "$1"; fail=1; }
reddedilmeli() {
    local ad="$1" bekle="$2" kaynak="$3"
    printf '%s\n' "$kaynak" > "$TMP/$ad.tpr"
    local out rc
    out=$(cd "$TMP" && "$TUL" build "$ad.tpr" "$ad.out" 2>&1); rc=$?
    if [ "$rc" -eq 0 ]; then
        dustu "$ad: derleme BASARILI oldu, hata bekleniyordu"
    elif ! echo "$out" | grep -qF -- "$bekle"; then
        dustu "$ad: reddedildi ama beklenen mesaj yok: '$bekle'"
        echo "$out" | sed 's/^/         /'
    else
        gecti "$ad"
    fi
}

reddedilmeli bilinmeyen_struct "'Yok' adinda bir struct yok" \
'struct Nokta { int x; int y; }
json j = fromJson("{}");
Nokta p = to_struct(j, "Yok");
print(p.x);'

reddedilmeli ad_sabit_degil "dizgi SABITI olmali" \
'struct Nokta { int x; int y; }
json j = fromJson("{}");
str ad = "Nokta";
Nokta p = to_struct(j, ad);
print(p.x);'

reddedilmeli tek_arguman "dizgi SABITI olmali" \
'struct Nokta { int x; int y; }
json j = fromJson("{}");
Nokta p = to_struct(j);
print(p.x);'

printf '%s\n' 'struct Nokta { int x; int y; }
Nokta p = to_struct(fromJson("{\"x\": 3, \"y\": 4}"), "Nokta");
print(toString(p.x + p.y));' > "$TMP/gecerli.tpr"
out=$(cd "$TMP" && "$TUL" build gecerli.tpr gecerli.out 2>&1 && ./gecerli.out 2>&1); rc=$?
if [ "$rc" -eq 0 ] && echo "$out" | grep -q "^7$"; then gecti "gecerli to_struct derlenir ve calisir (pozitif kontrol)"
else dustu "gecerli to_struct: rc=$rc cikti='$out'"; fi

if [ "$fail" -eq 0 ]; then echo "to_struct_hatalari: $n_gecti/$n_gecti"; fi
exit $fail
