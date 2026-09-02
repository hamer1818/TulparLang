#!/usr/bin/env bash
# UÇTAN UCA AOT dumanı: derleyici GERÇEKTEN program üretebiliyor mu?
#
# NİYE VAR: macOS işi uzun süre yalnız DERLEYİP artefakt yüklüyordu — yani
# "tulpar ikilisi oluştu" ile "tulpar çalışıyor" arasındaki fark orada hiç
# ölçülmüyordu. Sonuç (2026-09-02'de ölçüldü): yayınlanan
# `tulpar-macos-universal` ile HİÇBİR program derlenemiyordu
# (`ld: library 'ssl' not found`) ve bu aylarca fark edilmedi. Bu betik o
# boşluğu kapatıyor: yayınlanan her platformda en az bir gerçek
# derle→linkle→çalıştır zinciri koşuyor.
#
# Kasten HIZLI ve GRAFİKSİZ: pencere açan hiçbir şey yok, ~10 saniye.
#
#   tests/aot_smoke.sh [tulpar_yolu]
set -uo pipefail

TUL="${1:-./tulpar}"
[ -x "$TUL" ] || { echo "HATA: '$TUL' calistirilabilir degil" >&2; exit 1; }
TUL="$(cd "$(dirname "$TUL")" && pwd)/$(basename "$TUL")"

TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
fail=0

gecti() { printf "  \033[0;32mgecti\033[0m  %s\n" "$1"; }
dustu() { printf "  \033[0;31mDUSTU\033[0m  %s\n" "$1"; fail=1; }

# 1) `tulpar build` -> calistirilabilir ikili -> dogru cikti.
#    Kirilan sey TAM OLARAK buydu: link asamasi.
cat > "$TMP/a.tpr" <<'T'
func topla(a, b): int { return a + b; }
str s = "tul" + "par";
print(s + ":" + toString(topla(20, 22)));
T
if "$TUL" build "$TMP/a.tpr" "$TMP/a" > "$TMP/a.log" 2>&1 && [ -x "$TMP/a" ]; then
    out=$("$TMP/a" 2>&1)
    if [ "$out" = "tulpar:42" ]; then gecti "build -> calistir -> cikti"
    else dustu "build ciktisi yanlis: '$out' (beklenen 'tulpar:42')"; fi
else
    dustu "build basarisiz"; tail -15 "$TMP/a.log" | sed 's/^/         /'
fi

# 2) Dogrudan calistirma yolu (`tulpar dosya.tpr`) — build'den AYRI bir kod
#    yolu: gecici ikiliyi kendi yazip kosturup siliyor.
cat > "$TMP/b.tpr" <<'T'
array xs = [3, 1, 2];
int t = 0;
int i = 0;
while (i < length(xs)) { t = t + xs[i]; i = i + 1; }
print("toplam=" + toString(t));
T
out=$("$TUL" "$TMP/b.tpr" 2>&1 | tail -1)
if [ "$out" = "toplam=6" ]; then gecti "dogrudan calistirma"
else dustu "dogrudan calistirma ciktisi: '$out' (beklenen 'toplam=6')"; fi

# 3) Stdlib + dosya G/C: uretilen ikili CALISMA ZAMANI kutuphanesine de
#    baglaniyor mu. Yalniz link degil, arena/dizgi/JSON yollari da kosuyor.
cat > "$TMP/c.tpr" <<'T'
str yol = "smoke_out.txt";
write_file(yol, "merhaba");
str geri = read_file(yol);
json j = { "ad": "tulpar", "n": 7 };
json k = fromJson(toJson(j));
print(geri + "|" + k["ad"] + "|" + toString(k["n"]));
T
if "$TUL" build "$TMP/c.tpr" "$TMP/c" > "$TMP/c.log" 2>&1 && [ -x "$TMP/c" ]; then
    out=$(cd "$TMP" && ./c 2>&1 | tail -1)
    if [ "$out" = "merhaba|tulpar|7" ]; then gecti "stdlib (dosya + json)"
    else dustu "stdlib ciktisi: '$out' (beklenen 'merhaba|tulpar|7')"; fi
else
    dustu "stdlib ornegi derlenemedi"; tail -15 "$TMP/c.log" | sed 's/^/         /'
fi

# 4) Surum satiri: yayinlanan ikilinin kendini dogru tanitmasi.
v=$("$TUL" version 2>&1 | head -1)
case "$v" in
    TulparLang\ *) gecti "surum satiri ($v)" ;;
    *) dustu "surum satiri beklenmedik: '$v'" ;;
esac

if [ "$fail" -eq 0 ]; then
    echo -e "\033[0;32maot dumani temiz\033[0m (derle+linkle+calistir, dogrudan kosum, stdlib, surum)"
else
    echo -e "\033[0;31mAOT DUMANI BASARISIZ!\033[0m" >&2
fi
exit "$fail"
