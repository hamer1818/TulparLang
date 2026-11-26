#!/usr/bin/env bash
# JENERIK + `?` HATA YOLLARI (2026-11-26) — ayristirma hatalari adiyla.
set -uo pipefail
export LC_ALL=C
TUL="${1:-./tulpar}"
TUL="$(cd "$(dirname "$TUL")" && pwd)/$(basename "$TUL")"
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
fail=0; n=0
gecti() { n=$((n + 1)); printf "  \033[0;32mgecti\033[0m  %s\n" "$1"; }
dustu() { printf "  \033[0;31mDUSTU\033[0m  %s\n" "$1"; fail=1; }
reddedilmeli() {
    printf '%s\n' "$3" > "$TMP/$1.tpr"
    local out rc
    out=$(cd "$TMP" && "$TUL" build "$1.tpr" "$1.out" 2>&1); rc=$?
    if [ "$rc" -eq 0 ]; then dustu "$1: derleme BASARILI oldu, hata bekleniyordu"
    elif ! grep -qF -- "$2" <<<"$out"; then dustu "$1: beklenen mesaj yok: '$2'"; echo "$out" | head -4 | sed 's/^/         /'
    else gecti "$1"; fi
}
reddedilmeli soru_ust_duzey "'?' can only be used inside a function body" \
'import "sonuc";
var x = tamam(1)?;'
reddedilmeli soru_ifade "'?' must follow a call, a variable or an access" \
'import "sonuc";
func f(): json { int x = 1 + 2?; return tamam(x); }'
reddedilmeli soru_parantezsiz "needs a braced block here" \
'import "sonuc";
func f(bool k): json { if (k) tamam(1)?; return tamam(0); }'
reddedilmeli tip_param_yinelendi "duplicate type parameter: 'T'" \
'func f<T, T>(T a): T { return a; }'
reddedilmeli tip_arg_sayisi "has the wrong number of type arguments" \
'struct Kutu<T> { T deger; }
Kutu<int, str> k;'
reddedilmeli tip_arg_sayisi_fn "generic function 'ilk' has the wrong number of type arguments" \
'func ilk<T>(T[] xs): T { return xs[0]; }
print(ilk<int, str>([1]));'
# Pozitif kontrol: gecerli kaynak derlenir ve dogru basar.
printf 'import "sonuc";\nfunc b(int x): json { if (x == 0) { return hata("s"); } return tamam(10 / x); }\nfunc f(int x): json { int y = b(x)?; return tamam(y + 1); }\nfunc ilk<T>(T[] xs): T { return xs[0]; }\nprint(ac(f(2)));\nprint(hatasi(f(0)));\nprint(ilk(["k"]));\n' > "$TMP/ok.tpr"
out=$(cd "$TMP" && "$TUL" ok.tpr 2>&1)
[ "$out" = "6
s
k" ] && gecti "gecerli kaynak derlenir ve dogru basar (pozitif kontrol)" || { dustu "pozitif kontrol: '$out'"; }
[ $fail -eq 0 ] && echo "jenerik_hatalari: $n/$n" || { echo "jenerik_hatalari DUSTU"; exit 1; }
