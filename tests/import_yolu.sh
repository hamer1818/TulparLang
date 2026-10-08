#!/usr/bin/env bash
# IMPORT YOLU ICE AKTARAN DOSYAYA GORE (oyun geri bildirimi #6, 2026-10-08).
#
# `import "..."` calisma dizinine gore cozuluyordu: `a/b/m1.tpr` icindeki
# `import "d/c.tpr"` (dosya `a/b/d/c.tpr`) `a/` dizininden `tulpar b/m1.tpr`
# ile kosunca "Import dosyasi acilamadi". Motor ornekleri bu yuzden
# `import "examples/davranis/x.tpr"` yaziyor ve yalniz tulpar/ dizininden
# calisiyor; oyun yalniz kendi dizininden.
#
# Kural (src/common/import_resolve.hpp — kodgen, onbellek, typeinfer ve
# ayristiricinin on taramasi AYNI fonksiyonu cagiriyor): once ice aktaran
# dosyanin dizini (`<dizin>/<ad>.tpr`, `<dizin>/<ad>`), bulunamazsa ESKI kural
# (calisma dizini: `<ad>`, `<ad>.tpr`), sonra `tulpar_modules/`. Iki yerde de
# ayni adli FARKLI dosya varsa ice aktaranin dizini kazanir ve uyari basilir.
# Tekillestirme dosya KIMLIGIYLE (yazimla degil). Onbellek anahtari tarafi:
# tests/onbellek.sh ("ice aktaranin dizini").
#
#   tests/import_yolu.sh [tulpar_yolu]
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
# kos <dizin> <beklenen son satir> <etiket> <tulpar argumanlari...>
kos() {
    local d=$1 bek=$2 et=$3; shift 3
    OUT=$(cd "$d" && "$TUL" "$@" 2>&1); RC=$?
    OUT=$(tr -d '\r' <<<"$OUT")
    if [ $RC -eq 0 ] && [ "$(tail -1 <<<"$OUT")" = "$bek" ]; then gecti "$et"
    else dustu "$et (rc=$RC)"; goster "$OUT"; fi
}

# 1) Belgedeki yeniden uretim: a/b/m1.tpr -> "d/c.tpr" = a/b/d/c.tpr.
mkdir -p "$TMP/a/b/d"
printf '%s\n' 'func c_f(): int { return 7; }' > "$TMP/a/b/d/c.tpr"
printf '%s\n' 'import "d/c.tpr";' 'print(c_f());' > "$TMP/a/b/m1.tpr"
kos "$TMP/a" 7 "a/ dizininden 'tulpar b/m1.tpr' (belgedeki yeniden uretim)" b/m1.tpr
kos "$TMP" 7 "baska dizinden 'tulpar a/b/m1.tpr'" a/b/m1.tpr
kos "$TMP/a/b" 7 "kendi dizininden (eski kural, degismedi)" m1.tpr
kos "$TMP" 7 "mutlak yolla" "$TMP/a/b/m1.tpr"

# 2) Ic ice: modulun import'u MODULUN dizinine gore (alt/x.tpr -> "y/z.tpr"
#    = alt/y/z.tpr), program hangi dizinden baslatilirsa baslatilsin.
mkdir -p "$TMP/p/alt/y"
printf '%s\n' 'func z_f(): int { return 5; }' > "$TMP/p/alt/y/z.tpr"
printf '%s\n' 'import "y/z.tpr";' 'func x_f(): int { return z_f() * 2; }' > "$TMP/p/alt/x.tpr"
printf '%s\n' 'import "alt/x.tpr";' 'print(x_f());' > "$TMP/p/ana.tpr"
kos "$TMP" 10 "ic ice modul kendi dizininden cozer (baska dizinden kosu)" p/ana.tpr

# 3) Eski kural geri donus: calisma dizinine gore yazilmis import aynen calisir.
mkdir -p "$TMP/e/ortak" "$TMP/e/oyun"
printf '%s\n' 'func ortak_f(): int { return 11; }' > "$TMP/e/ortak/o.tpr"
printf '%s\n' 'import "ortak/o.tpr";' 'print(ortak_f());' > "$TMP/e/oyun/ana.tpr"
kos "$TMP/e" 11 "calisma dizinine gore yazilmis import (eski kural) calisir" oyun/ana.tpr

# 4) Belirsizlik: iki yerde ayni adli FARKLI dosya -> ice aktaranin dizini + uyari.
mkdir -p "$TMP/b/oyun"
printf '%s\n' 'func m_f(): int { return 1; }' > "$TMP/b/oyun/m.tpr"
printf '%s\n' 'func m_f(): int { return 2; }' > "$TMP/b/m.tpr"
printf '%s\n' 'import "m.tpr";' 'print(m_f());' > "$TMP/b/oyun/ana.tpr"
kos "$TMP/b" 1 "belirsizlik: ice aktaranin dizini kazanir" oyun/ana.tpr
if icerir "$OUT" "resolves in two places" && icerir "$OUT" "oyun/m.tpr" && icerir "$OUT" "--> oyun/ana.tpr:1"; then
    gecti "belirsizlik uyarisi iki yolu ve import satirini soyler"
else dustu "belirsizlik uyarisi"; goster "$OUT"; fi
kos "$TMP/b/oyun" 1 "ayni dosya (kendi dizininden): uyari yok" ana.tpr
if icerir "$OUT" "resolves in two places"; then dustu "tek dosyada yanlis belirsizlik uyarisi"; goster "$OUT"
else gecti "tek dosyada uyari yok"; fi

# 5) Tekillestirme KIMLIKLE: ayni dosyanin iki yazimi bir kez yuklenir.
mkdir -p "$TMP/t/alt"
printf '%s\n' 'print("x yuklendi");' 'func tx(): int { return 3; }' > "$TMP/t/alt/x.tpr"
printf '%s\n' 'import "x.tpr";' 'func ty(): int { return tx() + 1; }' > "$TMP/t/alt/y.tpr"
printf '%s\n' 'import "alt/x.tpr";' 'import "alt/y.tpr";' 'print(ty());' > "$TMP/t/ana.tpr"
kos "$TMP" 4 "ayni dosya iki yazimla: derlenir" t/ana.tpr
k=$(grep -c "x yuklendi" <<<"$OUT")
[ "$k" = "1" ] && gecti "ayni dosya iki yazimla: BIR kez yuklenir" || { dustu "ayni dosya $k kez yuklendi"; goster "$OUT"; }

# 6) Ayni yazim, FARKLI iki dosya: ikisi de yuklenir (eskiden ikincisi
#    dizgi tekillestirmesiyle SESSIZCE atlaniyordu).
mkdir -p "$TMP/q/alt"
printf '%s\n' 'func u1(): int { return 100; }' > "$TMP/q/util.tpr"
printf '%s\n' 'func u2(): int { return 20; }' > "$TMP/q/alt/util.tpr"
printf '%s\n' 'import "util";' 'func m_g(): int { return u2(); }' > "$TMP/q/alt/m.tpr"
printf '%s\n' 'import "util";' 'import "alt/m.tpr";' 'print(u1() + m_g());' > "$TMP/q/ana.tpr"
kos "$TMP/q" 120 "ayni yazim iki farkli dosya: ikisi de yuklenir" ana.tpr

# 7) Dongu: modul ana dosyayi geri ice aktariyor -> no-op (ana dosyanin ust
#    duzey kodu BIR kez kosar).
printf '%s\n' 'import "dongu_ana.tpr";' 'func m_f(): int { return ana_f() + 1; }' > "$TMP/dongu_m.tpr"
printf '%s\n' 'import "dongu_m.tpr";' 'func ana_f(): int { return 41; }' 'print(m_f());' > "$TMP/dongu_ana.tpr"
kos "$TMP" 42 "dongu: ana dosyayi geri ice aktarma derlenir" dongu_ana.tpr
k=$(grep -c "^42" <<<"$OUT")
[ "$k" = "1" ] && gecti "dongu: ana dosyanin ust duzey kodu bir kez" || { dustu "dongu: $k kez kostu"; goster "$OUT"; }

# 8) Ayni adli DIZIN modulu golgelemez: `import "davranis"` (davranis/ +
#    davranis.tpr) dosyayi yukler.
mkdir -p "$TMP/g/davranis"
printf '%s\n' 'func dv(): int { return 9; }' > "$TMP/g/davranis.tpr"
printf '%s\n' 'import "davranis";' 'print(dv());' > "$TMP/g/ana.tpr"
kos "$TMP/g" 9 "ayni adli dizin modulu golgelemez" ana.tpr

# 9) typecheck de ayni kurali izler: baska dizinden modulun imzasini gorur
#    (arguman sayisi yanlis -> hata; modul bulunamasaydi sessiz gecerdi).
printf '%s\n' 'import "d/c.tpr";' 'print(c_f(1, 2));' > "$TMP/a/b/m2.tpr"
OUT=$(cd "$TMP" && "$TUL" typecheck a/b/m2.tpr 2>&1); RC=$?
if [ $RC -ne 0 ] && icerir "$OUT" "c_f"; then gecti "typecheck baska dizinden modul imzasini gorur"
else dustu "typecheck (rc=$RC)"; goster "$OUT"; fi

# 10) Uzun dizin yolu (> 256 karakter): ic ice import'un dizini kesilmeden
#     tasinir (dizin tamponlari 256'dan 1024'e cikti; snprintf SESSIZCE
#     keserdi). Windows'ta MAX_PATH (260) yuzunden atlanir.
case "$(uname -s)" in
MINGW*|MSYS*|CYGWIN*) printf "  \033[0;33mATLANDI\033[0m  uzun yol (Windows MAX_PATH)\n" ;;
*)
    uzun="$TMP/u/$(printf 'd%.0s' $(seq 1 90))/$(printf 'e%.0s' $(seq 1 90))/$(printf 'f%.0s' $(seq 1 90))"
    mkdir -p "$uzun"
    printf '%s\n' 'func n_f(): int { return 4; }' > "$uzun/n.tpr"
    printf '%s\n' 'import "n.tpr";' 'func m_f(): int { return n_f() + 1; }' > "$uzun/m.tpr"
    printf '%s\n' "import \"$uzun/m.tpr\";" 'print(m_f());' > "$TMP/u/ana.tpr"
    kos "$TMP/u" 5 "uzun dizin yolu (${#uzun} karakter) kesilmez" ana.tpr ;;
esac

# Pozitif kontrol: hicbir yerde olmayan import hala "acilamadi" der (kural her
# yolu cozulur yapmiyor). Not: kullanilmayan bir import'un bulunamamasi bugun
# de derlemeyi DURDURMUYOR (cikis 0) — ayri konu, burada olculmuyor.
printf '%s\n' 'import "yok/boyle.tpr";' 'print(1);' > "$TMP/a/b/yok.tpr"
OUT=$(cd "$TMP" && "$TUL" a/b/yok.tpr 2>&1); RC=$?
if icerir "$OUT" "Could not import file 'yok/boyle.tpr'"; then gecti "bulunamayan import hala hata basar (pozitif kontrol)"
else dustu "bulunamayan import (rc=$RC)"; goster "$OUT"; fi

[ "$fail" -eq 0 ] && echo "import_yolu: $n/$n"
exit $fail
