#!/usr/bin/env bash
# AYRILMIS SOZCUK ve IMPORT EDILEN MODULDE AYRISTIRMA HATASI (K056, 2026-09-27).
#
# `don` (= return) ve `move` token; ad olarak kullanilinca tani "ayrilmis
# kelime ad olarak kullanilamaz: 'don'" diyor. Uc kusur vardi (olculdu):
#   1. hata import edilen MODULDEYSE konum `(stdin):2` basiliyordu; calistir
#      yolunda ikinci kopya ANA dosyanin adi ve o satirin metniyle geliyordu
#      (`--> ana.tpr:2` + ana dosyanin 2. satiri);
#   2. `tulpar typecheck ana.tpr` modul hatalarini basip "ok" deyip 0
#      donuyordu (CI kapisi olarak kor);
#   3. calistir yolunda ilk "gercek" tani yaniltici "'f' adinda bir fonksiyon
#      bulunamadi" idi; `tulpar typecheck` ana dosyada da `(stdin)` basiyordu.
# Bu tanilarin hicbir regresyon testi yoktu.
#
#   tests/modul_ayristirma_hatalari.sh [tulpar_yolu]
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
icerir() { echo "$1" | grep -qF -- "$2"; }

printf '%s\n' 'func f(): int {' '    int don = 3;' '    return don;' '}' > "$TMP/modul.tpr"
printf '%s\n' 'import "modul";' 'print(f());' > "$TMP/ana.tpr"
printf '%s\n' 'print(1);' 'bool don = true;' > "$TMP/yerel.tpr"

# 1) Ana dosyada: dosya adi + satir, typecheck cikis 2.
out=$(cd "$TMP" && "$TUL" typecheck yerel.tpr 2>&1); rc=$?
if [ $rc -eq 2 ] && icerir "$out" "reserved word cannot be used as a name: 'don'" && icerir "$out" "yerel.tpr:2"; then
    gecti "ana dosya: tani + 'yerel.tpr:2' + cikis 2"
else dustu "ana dosya (rc=$rc)"; echo "$out" | sed 's/^/         /'; fi

# 2) Modulde, typecheck: modul adi + satir alintisi, "ok" DEGIL, cikis != 0.
out=$(cd "$TMP" && "$TUL" typecheck ana.tpr 2>&1); rc=$?
if [ $rc -ne 0 ] && icerir "$out" "modul.tpr:2" && icerir "$out" "int don = 3;" && ! icerir "$out" "(stdin)" && ! icerir "$out" ": ok"; then
    gecti "modul, typecheck: 'modul.tpr:2' + alinti + cikis $rc"
else dustu "modul, typecheck (rc=$rc)"; echo "$out" | sed 's/^/         /'; fi

# 3) Modulde, calistir: modul adi, TEK kopya, yaniltici 'bulunamadi' YOK.
out=$(cd "$TMP" && "$TUL" ana.tpr 2>&1); rc=$?
kopya=$(echo "$out" | grep -c "reserved word cannot be used as a name: 'don'")
if [ $rc -ne 0 ] && icerir "$out" "modul.tpr:2" && [ "$kopya" = "1" ] && ! icerir "$out" "(stdin)" \
   && ! icerir "$out" "ana.tpr:2" && icerir "$out" "failed to parse"; then
    gecti "modul, calistir: tek tani, modul adiyla, import satirinda neden"
else dustu "modul, calistir (rc=$rc, kopya=$kopya)"; echo "$out" | sed 's/^/         /'; fi

# Pozitif kontrol: gecerli modul derlenir ve calisir.
printf '%s\n' 'func f(): int {' '    int donus = 3;' '    return donus;' '}' > "$TMP/modul.tpr"
out=$(cd "$TMP" && "$TUL" ana.tpr 2>&1); rc=$?
if [ $rc -eq 0 ] && [ "$(echo "$out" | tail -1)" = "3" ]; then gecti "gecerli modul calisir (pozitif kontrol)"
else dustu "gecerli modul (rc=$rc)"; echo "$out" | sed 's/^/         /'; fi

[ "$fail" -eq 0 ] && echo "modul_ayristirma_hatalari: $n/$n"
exit $fail
