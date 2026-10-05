#!/usr/bin/env bash
# MODUL AD CAKISMASI UYARILARI (K043, 2026-09-27). Calisma zamani anlami
# tests/modul_ad_cakismasi.test.tpr'de; burada TANI olculuyor:
#   * iki modul ayni adi tanimliyor -> "defined in two imported modules"
#     (ilki kazanir; eskiden SESSIZDI)
#   * ana dosya modulle ayni adi tanimliyor -> "shadows the function ... across
#     the WHOLE program"
# Uyari, hata DEGIL (sayilmaz): cakisma kullanicinin duzeltemeyecegi iki stdlib
# modulu arasinda olabiliyor (olculdu: tame/arcade `dokunuldu`). Pozitif
# kontrol: cakisma yokken uyari yok.
#
#   tests/modul_ad_cakismasi.sh [tulpar_yolu]
set -uo pipefail
TUL="${1:-./tulpar}"
[ -x "$TUL" ] || { echo "HATA: '$TUL' calistirilabilir degil" >&2; exit 1; }
TUL="$(cd "$(dirname "$TUL")" && pwd)/$(basename "$TUL")"
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
export LC_ALL=C
fail=0
n=0
gecti() { printf "  \033[0;32mgecti\033[0m  %s\n" "$1"; n=$((n + 1)); }
dustu() { printf "  \033[0;31mDUSTU\033[0m  %s\n" "$1"; fail=1; n=$((n + 1)); }

printf '%s\n' 'func ortak(): int { return 1; }' > "$TMP/ma.tpr"
printf '%s\n' 'func ortak(): int { return 2; }' 'func yardim(): int { return 7; }' > "$TMP/mb.tpr"
printf '%s\n' 'import "ma";' 'import "mb";' 'func yardim(): int { return 9; }' 'print(ortak() * 10 + yardim());' > "$TMP/ana.tpr"

out=$(cd "$TMP" && "$TUL" typecheck ana.tpr 2>&1); rc=$?
if grep -qF "'ortak' is defined in two imported modules: 'ma' (line 1) and 'mb' (line 1)" <<<"$out"; then
    gecti "iki modul: uyari, iki tanimin yeri"
else dustu "iki modul uyarisi yok"; echo "$out" | sed 's/^/         /'; fi
if grep -qF "'yardim' (line 3) shadows the function of the same name in imported module 'mb' (line 2)" <<<"$out"; then
    gecti "yerel golge: uyari, iki tanimin yeri"
else dustu "yerel golge uyarisi yok"; echo "$out" | sed 's/^/         /'; fi
if [ $rc -eq 0 ]; then gecti "uyari SAYILMIYOR (typecheck cikis 0)"
else dustu "typecheck cikis $rc (uyari sayilmamaliydi)"; fi
run=$(cd "$TMP" && "$TUL" ana.tpr 2>/dev/null | tail -1)
if [ "$run" = "19" ]; then gecti "calisma: ilk modul + yerel tanim (1*10 + 9)"
else dustu "calisma ciktisi '$run' (19 bekleniyordu)"; fi

# Pozitif kontrol: cakisma yok -> uyari yok.
printf '%s\n' 'func ortak2(): int { return 2; }' > "$TMP/mb.tpr"
printf '%s\n' 'import "ma";' 'import "mb";' 'print(ortak() + ortak2());' > "$TMP/ana.tpr"
out=$(cd "$TMP" && "$TUL" typecheck ana.tpr 2>&1)
if grep -qE "two imported modules|shadows the function" <<<"$out"; then
    dustu "cakisma yokken uyari basildi"; echo "$out" | sed 's/^/         /'
else gecti "cakisma yok -> uyari yok (pozitif kontrol)"; fi

[ "$fail" -eq 0 ] && echo "modul_ad_cakismasi: $n/$n"
exit $fail
