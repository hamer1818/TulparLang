#!/usr/bin/env bash
# `enum` HATA YOLLARI (P0.2, 2026-09-21): yanlis kullanim SESSIZCE gecmemeli.
#
# Neden .test.tpr degil: bunlar AYRISTIRMA hatalari. `tests/typeinfer/fail`
# fixture'lari `[typecheck]` satiri ister (tip denetimi), .test.tpr paketleri
# ise derlenemeyen kaynagi kosturamaz. Tek yol derleyiciyi disaridan
# cagirmak: her fixture `tulpar typecheck` ile kosulur, cikis kodu 2
# (ayristirma hatasi) VE beklenen mesaj parcasi aranir. Pozitif kontrol
# (gecerli dosya -> 0) harness'in "her seyi reddediyor" olmadigini kanitlar.
#
#   tests/enum_hatalari.sh [tulpar_yolu]
set -uo pipefail
TUL="${1:-./tulpar}"
[ -x "$TUL" ] || { echo "HATA: '$TUL' calistirilabilir degil" >&2; exit 1; }
TUL="$(cd "$(dirname "$TUL")" && pwd)/$(basename "$TUL")"
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
fail=0
gecti() { printf "  \033[0;32mgecti\033[0m  %s\n" "$1"; }
dustu() { printf "  \033[0;31mDUSTU\033[0m  %s\n" "$1"; fail=1; }

# reddedilmeli <ad> <beklenen-mesaj-parcasi> <kaynak>
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

reddedilmeli bilinmeyen_uye "adli uye yok" \
'enum Ekran { MENU, OYUN }
int x = Ekran.MENUU;'

reddedilmeli yeniden_tanim "yeniden tanimlandi" \
'enum Ekran { MENU }
enum Ekran { OYUN }'

reddedilmeli fonksiyon_icinde "ust duzeyde" \
'func f() {
    enum Ekran { MENU }
    return 0;
}'

reddedilmeli tamsayi_olmayan_deger "TAMSAYI sabiti" \
'enum Ekran { MENU = "menu" }'

reddedilmeli yinelenen_uye "uyesi yinelendi" \
'enum Ekran { MENU, OYUN, MENU }'

# Pozitif kontrol: gecerli enum + bildirimden once kullanim -> 0.
printf '%s\n' 'func f(Ekran e): int { return e + 1; }
enum Ekran { MENU, OYUN = 5, AYAR }
int x = f(Ekran.AYAR);' > "$TMP/gecerli.tpr"
out=$(cd "$TMP" && "$TUL" typecheck gecerli.tpr 2>&1); rc=$?
if [ "$rc" -eq 0 ]; then gecti "gecerli enum kabul (pozitif kontrol)"
else dustu "gecerli enum reddedildi (rc=$rc)"; echo "$out" | sed 's/^/         /'; fi

if [ "$fail" -eq 0 ]; then echo "enum_hatalari: 6/6"; fi
exit $fail
