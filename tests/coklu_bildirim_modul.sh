#!/usr/bin/env bash
# COKLU BILDIRIM: MODULLER ARASI + TIP TANISI (oyun geri bildirimi #7,
# 2026-10-08).
#
# Ilk gercek cok dosyali oyunda iki sey:
#   1. `float sx, sy = ekrana(...)`: `ekrana` ana dosyanin ice aktardigi
#      BASKA bir modulde (kamera.tpr) ise bu dosya onu KENDISI import
#      etmedikce "coklu bildirimin sag tarafi `: (T, T)` bildiren bir
#      fonksiyonun dogrudan cagrisi olmali" — normal cagri `ekrana(...)` ayni
#      fonksiyonu buluyordu. Kok neden: coklu bildirim ayristirici sekeri,
#      callee'nin tuple tipini ayristirma aninda bilmesi gerekiyor ve modulun
#      ayristiricisi yalniz KENDI import'larinin imzalarini goruyordu. Simdi
#      ana dosyanin ayristirmasi programin butun tuple imzalarini yakaliyor,
#      modul ayristirmalari onu en dusuk oncelikle miras aliyor; gorunurluk
#      karari kodgende (oyun geri bildirimi #5: sonraki kardes -> ipuculu
#      "bulunamadi").
#   2. `bool bas, px, py = isaretci()` (`isaretci(): (bool, float, float)`)
#      ucunu de bool yapiyor (tipi yazilmayan ad ilk adin tipini alir — C
#      gibi; kural DEGISMEDI, degistirmek `float a, b = f()`yi (float, int)
#      icin sessizce int yapardi). Tani ise bildirimde degil sonraki ATAMA
#      satirlarinda cikiyordu. Simdi bildirim satirinda, cozumuyle.
#
#   tests/coklu_bildirim_modul.sh [tulpar_yolu]
set -uo pipefail
TUL="${1:-./tulpar}"
[ -x "$TUL" ] || [ -x "$TUL.exe" ] || { echo "HATA: '$TUL' calistirilabilir degil" >&2; exit 1; }
TUL="$(cd "$(dirname "$TUL")" && pwd)/$(basename "$TUL")"
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
export LC_ALL=C TULPAR_AOT_NOCACHE=1
fail=0
n=0
gecti() { printf "  \033[0;32mgecti\033[0m  %s\n" "$1"; n=$((n + 1)); }
dustu() { printf "  \033[0;31mDUSTU\033[0m  %s\n" "$1"; fail=1; n=$((n + 1)); }
icerir() { grep -qF -- "$2" <<<"$1"; }
goster() { sed 's/^/         /' <<<"$1"; }
kos() {  # kos <beklenen son satir> <etiket> <tulpar argumanlari...>
    local bek=$1 et=$2; shift 2
    OUT=$(cd "$TMP" && "$TUL" "$@" 2>&1); RC=$?
    OUT=$(tr -d '\r' <<<"$OUT")
    if [ $RC -eq 0 ] && [ "$(tail -1 <<<"$OUT")" = "$bek" ]; then gecti "$et"
    else dustu "$et (rc=$RC)"; goster "$OUT"; fi
}

# 1) Belgedeki yeniden uretim: kardes modulun tuple fonksiyonu (once ice
#    aktarilan), bu modul onu import etmiyor.
printf '%s\n' 'func ekrana(float x): (float, float) { return x * 2.0, x * 3.0; }' > "$TMP/kamera.tpr"
printf '%s\n' 'func savas_f(): float {' '    float sx, sy = ekrana(1.0);' '    return sx + sy;' '}' > "$TMP/savas.tpr"
printf '%s\n' 'import "kamera.tpr";' 'import "savas.tpr";' 'print(savas_f());' > "$TMP/oyun.tpr"
kos 5 "kardes modulun tuple fonksiyonu coklu bildirimle (belgedeki yeniden uretim)" oyun.tpr

# 2) Ice aktaranin (ana dosyanin) tuple fonksiyonu + coklu ATAMA bicimi.
printf '%s\n' 'func m_f(): int {' '    int a = 0;' '    int b = 0;' '    a, b = bol(17, 5);' '    int q, r = bol(9, 4);' '    return a * 100 + b * 10 + q + r;' '}' > "$TMP/m.tpr"
printf '%s\n' 'import "m.tpr";' 'func bol(int x, int y): (int, int) { return x / y, x % y; }' 'print(m_f());' > "$TMP/ana.tpr"
kos 323 "ice aktaranin tuple fonksiyonu: coklu atama ve bildirim" ana.tpr

# 3) Modulun KENDI tuple fonksiyonu (gerileme korumasi): miras tablo onu
#    bozmaz.
printf '%s\n' 'func cift(): (int, int, int) { return 1, 2, 3; }' 'func y_f(): int { int a, b, c = cift(); return a + b + c; }' > "$TMP/y.tpr"
printf '%s\n' 'import "y.tpr";' 'print(y_f());' > "$TMP/ana_y.tpr"
kos 6 "modulun kendi tuple fonksiyonu (degismedi)" ana_y.tpr

# 4) Sonra ice aktarilan kardes: ayristirici kabul eder, kodgen normal
#    cagridaki ipuculu "bulunamadi"yi verir (oyun geri bildirimi #5 kurali).
printf '%s\n' 'import "savas.tpr";' 'import "kamera.tpr";' 'print(savas_f());' > "$TMP/ters.tpr"
OUT=$(cd "$TMP" && "$TUL" ters.tpr 2>&1); RC=$?
if [ $RC -ne 0 ] && icerir "$OUT" "'ekrana'" && icerir "$OUT" "imported AFTER" \
   && ! icerir "$OUT" "must directly call"; then
    gecti "sonraki kardes: ipuculu 'bulunamadi' (ayristirma hatasi degil)"
else dustu "sonraki kardes (rc=$RC)"; goster "$OUT"; fi

# 5) `tulpar typecheck` da modulu ayristirabiliyor (eskiden "imported module
#    failed to parse").
OUT=$(cd "$TMP" && "$TUL" typecheck oyun.tpr 2>&1); RC=$?
if [ $RC -eq 0 ] && ! icerir "$OUT" "failed to parse"; then gecti "typecheck: kardes modulun coklu bildirimi ayristiriliyor"
else dustu "typecheck (rc=$RC)"; goster "$OUT"; fi

# 6) Tip tanisi BILDIRIM satirinda ve cozumuyle.
printf '%s\n' 'func isaretci(): (bool, float, float) { return true, 1.5, 2.5; }' \
    'bool bas, px, py = isaretci();' 'print(bas);' > "$TMP/tip.tpr"
OUT=$(cd "$TMP" && "$TUL" tip.tpr 2>&1); RC=$?
if icerir "$OUT" "declaration of 'px': expected bool, got float at line 2" \
   && icerir "$OUT" "takes the FIRST name's type" && icerir "$OUT" "float px = ..." \
   && icerir "$OUT" "declaration of 'py'"; then
    gecti "tek tip coklu bildirim: tani bildirim satirinda + cozum"
else dustu "tek tip tanisi (rc=$RC)"; goster "$OUT"; fi

# 7) Dogru yazimlar TANISIZ: her ada kendi tipi / `var` / genisleyen donusum
#    (int -> float, kural degismedi: `float a, b = f()` (float, int) icin b float).
printf '%s\n' 'func isaretci(): (bool, float, float) { return true, 1.5, 2.5; }' \
    'func karma(): (float, int) { return 0.5, 7; }' \
    'bool bas, float px, float py = isaretci();' 'var b2, x2, y2 = isaretci();' \
    'float a, b = karma();' 'print(px + py + x2 + y2 + a + b);' > "$TMP/dogru.tpr"
OUT=$(cd "$TMP" && "$TUL" dogru.tpr 2>&1); RC=$?
OUT=$(tr -d '\r' <<<"$OUT")
if [ $RC -eq 0 ] && ! icerir "$OUT" "[typecheck]" && [ "$(tail -1 <<<"$OUT")" = "15.5" ]; then
    gecti "tipli / var / genisleyen coklu bildirim tanisiz (15.5)"
else dustu "dogru yazimlar (rc=$RC)"; goster "$OUT"; fi

# Pozitif kontrol: hicbir yerde tanimli olmayan fonksiyon hala ayristirma hatasi.
printf '%s\n' 'func z_f(): float { float a, b = yok_boyle(1.0); return a; }' > "$TMP/z.tpr"
printf '%s\n' 'import "z.tpr";' 'print(z_f());' > "$TMP/ana_z.tpr"
OUT=$(cd "$TMP" && "$TUL" ana_z.tpr 2>&1); RC=$?
if [ $RC -ne 0 ] && icerir "$OUT" "must directly call"; then gecti "tanimsiz tuple fonksiyonu hala hata (pozitif kontrol)"
else dustu "tanimsiz tuple fonksiyonu (rc=$RC)"; goster "$OUT"; fi

[ "$fail" -eq 0 ] && echo "coklu_bildirim_modul: $n/$n"
exit $fail
