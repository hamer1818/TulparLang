#!/usr/bin/env bash
# IMPORT EDILEN struct'larin HATA YOLU (2026-09-25): ayni ad, FARKLI yerlesim.
#
# Modulun struct'lari artik kaydediliyor (prescan_import_types). Ayni adi
# iki yerlesimle bildirmek (ana program + modul ya da iki modul) derleme
# HATASI: sessizce ilkini kullanmak ikinci modulun fonksiyonlarini YANLIS
# yerlesimle derlerdi (alan indeksi/tipi kayar, cop okunur). Ayni ad + AYNI
# yerlesim ise tek tiptir ve moduller arasinda gecer — pozitif kontrol.
#
# typecheck bunu goremez (yerel tanim modulunkini sessizce golgeler); kaynak
# derlenmeye calisilir ve `tulpar build` cikis kodu + mesaj olculur.
#
#   tests/struct_import_hatalari.sh [tulpar_yolu]
set -uo pipefail
TUL="${1:-./tulpar}"
[ -x "$TUL" ] || { echo "HATA: '$TUL' calistirilabilir degil" >&2; exit 1; }
TUL="$(cd "$(dirname "$TUL")" && pwd)/$(basename "$TUL")"
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
fail=0
gecti() { printf "  \033[0;32mgecti\033[0m  %s\n" "$1"; }
dustu() { printf "  \033[0;31mDUSTU\033[0m  %s\n" "$1"; fail=1; }
# reddedilmeli <ad> <beklenen mesaj> — $TMP/<ad>/ana.tpr derlenemez olmali.
reddedilmeli() {
    local ad="$1" bekle="$2"
    local out rc
    out=$(cd "$TMP/$ad" && LC_ALL=C "$TUL" build ana.tpr ana.out 2>&1); rc=$?
    if [ "$rc" -eq 0 ]; then
        dustu "$ad: derleme BASARILI oldu, hata bekleniyordu"
    elif ! echo "$out" | grep -qF -- "$bekle"; then
        dustu "$ad: reddedildi ama beklenen mesaj yok: '$bekle'"
        echo "$out" | sed 's/^/         /'
    else
        gecti "$ad"
    fi
}

# 1. Ana program ile modul: ayni ad, farkli alan tipi.
mkdir -p "$TMP/ana_modul"
printf '%s\n' 'struct V { int x; int y; }
func v_yap(): V { return { x: 1, y: 2 }; }' > "$TMP/ana_modul/m.tpr"
printf '%s\n' 'import "m";
struct V { float x; float y; }
V a = v_yap();
print(a.x);' > "$TMP/ana_modul/ana.tpr"
reddedilmeli ana_modul "struct 'V' is declared with two different layouts"

# 2. Iki modul (biri digerini ic ice import ediyor): ayni ad, farkli alan
#    SAYISI. Hata ana programin import satirini gostermeli.
mkdir -p "$TMP/iki_modul"
printf '%s\n' 'struct P { int a; }
func p_b(): P { return { a: 1 }; }' > "$TMP/iki_modul/b.tpr"
printf '%s\n' 'import "b";
struct P { int a; int b; }
func p_a(): P { return { a: 1, b: 2 }; }' > "$TMP/iki_modul/a.tpr"
printf '%s\n' 'import "a";
P p = p_a();
print(p.a);' > "$TMP/iki_modul/ana.tpr"
reddedilmeli iki_modul "struct 'P' is declared with two different layouts"

# 3. Ayni ad, ayni alanlar, FARKLI SIRA: yerlesim farkli (alan indeksi kayar).
mkdir -p "$TMP/sira"
printf '%s\n' 'struct Q { int a; float b; }
func q_yap(): Q { return { a: 1, b: 2.0 }; }' > "$TMP/sira/m.tpr"
printf '%s\n' 'import "m";
struct Q { float b; int a; }
Q q = q_yap();
print(q.a);' > "$TMP/sira/ana.tpr"
reddedilmeli sira "struct 'Q' is declared with two different layouts"

# POZITIF KONTROL: ayni ad + AYNI yerlesim ana programda ve iki modulde ->
# tek tip; modul A'nin dondurdugu deger modul B'nin parametresine gecer.
mkdir -p "$TMP/gecerli"
printf '%s\n' 'struct V { float x; float y; }
func v_uzun2(V v): float { return v.x * v.x + v.y * v.y; }' > "$TMP/gecerli/b.tpr"
printf '%s\n' 'struct V { float x; float y; }
func v_yap(float x, float y): V { return { x: x, y: y }; }' > "$TMP/gecerli/a.tpr"
printf '%s\n' 'import "a";
import "b";
struct V { float x; float y; }
V k = v_yap(3.0, 4.0);
V[] vs = [];
push(vs, k);
vs[0].x += 1.0;
print(toString(v_uzun2(k)) + " " + toString(vs[0].x) + " " + typeof(v_yap(1.0, 1.0)));' > "$TMP/gecerli/ana.tpr"
out=$(cd "$TMP/gecerli" && "$TUL" build ana.tpr ana.out 2>&1 && ./ana.out 2>&1); rc=$?
if [ "$rc" -eq 0 ] && echo "$out" | grep -q "^25 4 int$"; then
    gecti "ayni yerlesim ana program + iki modulde tek tip (pozitif kontrol)"
else
    dustu "ayni yerlesim: rc=$rc cikti='$out'"
fi

if [ "$fail" -eq 0 ]; then echo "struct_import_hatalari: 4/4"; fi
exit $fail
