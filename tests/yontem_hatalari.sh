#!/usr/bin/env bash
# YONTEM ve YINELENEN TANIM hata yollari (K003 + K002, 2026-09-28).
#
# Eskiden ayni dosyada ayni adli ikinci fonksiyon SESSIZCE yutuluyordu (ilk
# tanim kazaniyor): `func f(): int {return 1;}` + `func f(): int {return 2;}`
# "1" basiyordu; `func area(Rect)` + `func area(Circle)` ile `c.area()` 0.
# Her hata `tulpar build` cikis kodu != 0 ve beklenen mesaj ister. Pozitif
# kontrol: gecerli yontemli kaynak derlenir ve dogru sonucu basar.
#
#   tests/yontem_hatalari.sh [tulpar_yolu]
set -uo pipefail
# DILI SABITLE: ayristirici tanilari tr_en ile yerel ayara gore dil seciyor;
# CI Ingilizce, gelistirici makinesi Turkce olabilir (tests/typeinfer/run.sh
# ile ayni sebep). Beklenen metinler Ingilizce bicim.
export LC_ALL=C
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

reddedilmeli yinelenen_fonksiyon "is defined twice (first at line 1)" \
'func f(): int { return 1; }
func f(): int { return 2; }
print(f());'

reddedilmeli tipe_gore_asiri_yukleme "is defined twice" \
'struct Rect { int w; int h; }
struct Circle { int r; }
func area(Rect x): int { return x.w * x.h; }
func area(Circle c): int { return c.r * c.r * 3; }
Circle c = { r: 2 };
print(c.area());'

reddedilmeli yinelenen_yontem "is defined twice" \
'struct P { int x; }
func P.v(): int { return self.x; }
func P.v(): int { return 0; }
P p; print(p.v());'

reddedilmeli struct_olmayan_tip "bir struct tipi degil" \
'func Foo.bar(): int { return 1; }
print(1);'

reddedilmeli enum_yontemi "methods can only be defined on struct types" \
'enum Renk { KIRMIZI, MAVI }
func Renk.ad(): str { return "x"; }
print(1);'

reddedilmeli self_yeniden "implicit first parameter of a method" \
'struct P { int x; }
func P.f(int self): int { return 1; }
print(1);'

printf '%s\n' 'struct Rect { int w; int h; }
struct Circle { int r; }
func Rect.area(): int { return self.w * self.h; }
func Circle.area(): int { return self.r * self.r * 3; }
Rect a = { w: 2, h: 3 };
Circle c = { r: 2 };
print(toString(a.area()) + " " + toString(c.area()));' > "$TMP/gecerli.tpr"
out=$(cd "$TMP" && "$TUL" build gecerli.tpr gecerli.out 2>&1 && ./gecerli.out 2>&1); rc=$?
if [ "$rc" -eq 0 ] && echo "$out" | grep -q "^6 12$"; then gecti "gecerli yontemler derlenir ve tipe gore dagilir (pozitif kontrol)"
else dustu "gecerli yontemler: rc=$rc cikti='$out'"; fi

if [ "$fail" -eq 0 ]; then echo "yontem_hatalari: $n_gecti/$n_gecti"; fi
exit $fail
