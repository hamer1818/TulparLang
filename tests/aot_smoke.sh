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

# 2b) Dogrudan calistirmada programin CIKIS KODU aynen gecmeli. Eskiden
#     surucu sifir olmayan her kodu 1'e duzluyordu (`exit(3)` -> 1, olculdu
#     2026-09-28): betik `exit(2)` ile yakalanmamis istisnayi ayirt
#     edemiyordu. Pozitif kontrol: basarili program 0, yakalanmamis istisna
#     1 — yani 3'u gormek "her sey 3" degil.
printf 'print("x");\nexit(3);\n' > "$TMP/x3.tpr"
printf 'int z = 0;\nprint(10 / z);\n' > "$TMP/xe.tpr"
"$TUL" "$TMP/x3.tpr" > /dev/null 2>&1; rc3=$?
"$TUL" "$TMP/xe.tpr" > /dev/null 2>&1; rce=$?
"$TUL" "$TMP/b.tpr" > /dev/null 2>&1; rc0=$?
if [ "$rc3" = "3" ] && [ "$rce" = "1" ] && [ "$rc0" = "0" ]; then
    gecti "cikis kodu iletiliyor (exit(3) -> 3, istisna -> 1, basari -> 0)"
else
    dustu "cikis kodu: exit(3) -> $rc3, istisna -> $rce, basari -> $rc0 (beklenen 3 / 1 / 0)"
fi

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

# 4) OLMAYAN cikti dizini (K161). Eskiden ayristirma + kod uretiminden SONRA
#    obje yaziminda ciplak "Error emitting object file: No such file or
#    directory" ile dusuyordu — hangi yolun eksik oldugunu soylemeden. Artik
#    ust dizin kuruluyor; ust yol bir DOSYA ise yol adiyla hata veriyor.
#    Pozitif kontrol: dizin GERCEKTEN yoktu (once `[ ! -e ]`).
[ ! -e "$TMP/yok" ] || dustu "kurulum: $TMP/yok zaten var — dizin olusturma sinanmaz"
if "$TUL" build "$TMP/a.tpr" "$TMP/yok/alt/a" > "$TMP/d.log" 2>&1 && [ -x "$TMP/yok/alt/a" ]; then
    out=$("$TMP/yok/alt/a" 2>&1)
    if [ "$out" = "tulpar:42" ]; then gecti "olmayan cikti dizini olusturuluyor"
    else dustu "olmayan dizine derlenen ikili yanlis cikti: '$out'"; fi
else
    dustu "olmayan cikti dizinine derleme basarisiz"; tail -5 "$TMP/d.log" | sed 's/^/         /'
fi
: > "$TMP/dosya"
# GORELI yol, $TMP icinden. MSYS2 (Windows CI) POSIX argumani yerel yola
# cevirirken bir bileseni DOSYA olan yolu CEVIREMIYOR ve `/tmp/...dosya/a`yi
# oldugu gibi geciriyor; yerel tulpar.exe onu `C:\tmp\...` sanip orada dizin
# kurdu ve derleme "basarili" oldu (olculdu 2026-09-27, teshis ciktisiyla).
# Olculmek istenen surucunun davranisi, kabugun yol cevirisi degil.
if (cd "$TMP" && LC_ALL=C "$TUL" build a.tpr dosya/a) > "$TMP/e.log" 2>&1; then
    dustu "ust yolu DOSYA olan cikti basarili sayildi"
    sed 's/^/         log: /' "$TMP/e.log" | tail -4
elif grep -q "not a directory: .*dosya" "$TMP/e.log"; then
    gecti "ust yolu dosya olan cikti: yolu adiyla hata"
else
    dustu "ust yolu dosya olan cikti: anlamli tani yok"; tail -3 "$TMP/e.log" | sed 's/^/         /'
fi

# 5) Surum satiri: yayinlanan ikilinin kendini dogru tanitmasi.
v=$("$TUL" version 2>&1 | head -1)
case "$v" in
    TulparLang\ *) gecti "surum satiri ($v)" ;;
    *) dustu "surum satiri beklenmedik: '$v'" ;;
esac

# TULPAR_CC (K225): linkleyici surucusu secilebilir. Iki ayak, ikisi de her
# platformda (Windows'ta system() cmd.exe'den gecer, sarmalayici betik
# kullanilamaz): sacma bir surucu adiyla link DUSMELI (degisken GERCEKTEN
# okunuyor — pozitif kontrol), acikca `clang++` ile GECMELI.
if TULPAR_AOT_NOCACHE=1 TULPAR_CC=tulpar_olmayan_surucu_xyz "$TUL" build "$TMP/a.tpr" "$TMP/cc_bad" > "$TMP/cc1.log" 2>&1; then
    dustu "TULPAR_CC okunmuyor: olmayan surucuyla link basarili sayildi"
elif TULPAR_AOT_NOCACHE=1 TULPAR_CC=clang++ "$TUL" build "$TMP/a.tpr" "$TMP/cc_ok" > "$TMP/cc2.log" 2>&1 \
        && [ "$("$TMP/cc_ok" 2>&1)" = "tulpar:42" ]; then
    gecti "TULPAR_CC: olmayan surucu dusuruyor, clang++ ile calisiyor"
else
    dustu "TULPAR_CC=clang++ ile derleme basarisiz"; tail -5 "$TMP/cc2.log" | sed 's/^/         /'
fi

if [ "$fail" -eq 0 ]; then
    echo -e "\033[0;32maot dumani temiz\033[0m (derle+linkle+calistir, dogrudan kosum, cikis kodu, stdlib, cikti dizini, surum, TULPAR_CC)"
else
    echo -e "\033[0;31mAOT DUMANI BASARISIZ!\033[0m" >&2
fi
exit "$fail"
