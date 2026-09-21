#!/usr/bin/env bash
# Tipli struct dizisi HATA YOLLARI (P1.1, 2026-09-21): derleme (codegen)
# hatalari — `tulpar build` cikis kodu != 0 ve beklenen mesaj. Ayristirma
# hatasi olmadigi icin `typecheck` bunlari goremez; kaynak derlenmeye
# calisilir. Pozitif kontrol gecerli kaynagin derlendigini kanitlar.
#
#   tests/struct_dizisi_hatalari.sh [tulpar_yolu]
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

reddedilmeli str_alanli_eleman "yalnizca int/bool/float alanli" \
'struct Oyuncu { str ad; int can; }
Oyuncu[] o = [];'

reddedilmeli bilinmeyen_alan "alani yok" \
'struct V { float x; float y; }
V[] vs = [];
V a; a.x = 1.0; push(vs, a);
float q = vs[0].z;'

reddedilmeli yanlis_tip_push "yalnizca V degeri kabul eder" \
'struct V { float x; float y; }
V[] vs = [];
push(vs, 42);'

reddedilmeli yanlis_tip_eleman_atama "yalnizca V degeri atanabilir" \
'struct V { float x; float y; }
struct W { int a; }
V[] vs = [];
W w; w.a = 1;
V v; v.x = 1.0; push(vs, v);
vs[0] = w;'

printf '%s\n' 'struct V { float x; float y; }
V[] vs = [];
V v; v.x = 1.5; v.y = 2.5;
push(vs, v);
vs[0].x = vs[0].x + 1.0;
V k = vs[0];
print(toString(len(vs)) + " " + toString(k.x));' > "$TMP/gecerli.tpr"
out=$(cd "$TMP" && "$TUL" build gecerli.tpr gecerli.out 2>&1 && ./gecerli.out 2>&1); rc=$?
if [ "$rc" -eq 0 ] && echo "$out" | grep -q "^1 2.5$"; then gecti "gecerli struct dizisi derlenir ve calisir (pozitif kontrol)"
else dustu "gecerli struct dizisi: rc=$rc cikti='$out'"; fi

if [ "$fail" -eq 0 ]; then echo "struct_dizisi_hatalari: 5/5"; fi
exit $fail
