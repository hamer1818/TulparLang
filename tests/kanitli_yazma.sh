#!/usr/bin/env bash
# KANITLI YAZMA KAPISI (K215, 2026-09-28): hizli (surumlenmis, dalsiz) yol
# GERCEKTEN kuruluyor mu, ve i32'ye sigmasi kanitlanamayan yazmada KURULMUYOR
# mu? Dogruluk tests/kanitli_yazma.test.tpr'de; bu kapi kararin kendisini
# derleyicinin `TULPAR_DBG_VER` cikisindan ("[ver] a[i]") okur. Iki yon de
# olculuyor: kanit kurulmali (pozitif) ve kurulmamali (negatif) — yalniz
# birini olcen kapi "hep kur" ya da "hic kurma" bozulmasini gormez.
#
#   tests/kanitli_yazma.sh [tulpar_yolu]
set -uo pipefail
TUL="${1:-./tulpar}"
[ -x "$TUL" ] || { echo "HATA: '$TUL' calistirilabilir degil" >&2; exit 1; }
TUL="$(cd "$(dirname "$TUL")" && pwd)/$(basename "$TUL")"
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
fail=0
n_gecti=0
gecti() { n_gecti=$((n_gecti + 1)); printf "  \033[0;32mgecti\033[0m  %s\n" "$1"; }
dustu() { printf "  \033[0;31mDUSTU\033[0m  %s\n" "$1"; fail=1; }

# kanit <ad> <beklenen: evet|hayir> <govde>
kanit() {
    local ad="$1" bekle="$2" govde="$3"
    printf 'func f(int[] a, var k) {\n    for (int i = 0; i < len(a); i++) { %s }\n}\nint[] a = array_fill(4, 0);\nf(a, 3);\nprint(a[3]);\n' \
        "$govde" > "$TMP/$ad.tpr"
    local out
    out=$(cd "$TMP" && TULPAR_DBG_VER=1 TULPAR_AOT_NOCACHE=1 "$TUL" build "$ad.tpr" "$ad.out" 2>&1)
    local var=hayir
    echo "$out" | grep -qF '[ver] a[i]' && var=evet
    if [ "$var" = "$bekle" ]; then gecti "$ad: kanit $bekle"
    else dustu "$ad: kanit '$var', beklenen '$bekle' — $govde"; echo "$out" | sed 's/^/         /'; fi
}

kanit sabit          evet  'a[i] = 7;'
kanit dongu_degiskeni evet 'a[i] = i;'
kanit degismez_ad    evet  'a[i] = k;'
kanit carpim_sinirli evet  'a[i] = i * 2;'
kanit maske          evet  'a[i] = (i * 3) & 4095;'
kanit tasan_sabit    hayir 'a[i] = 5000000000;'
kanit tasan_ofset    hayir 'a[i] = i + 2147483647;'
kanit bilesik_arti   hayir 'a[i] += 1;'
kanit artirma        hayir 'a[i]++;'
kanit degisen_ad     hayir 'k = k + 1; a[i] = k;'
kanit bilesik_ve     evet  'a[i] &= 255;'

if [ "$fail" -eq 0 ]; then echo "kanitli_yazma: $n_gecti/$n_gecti"; fi
exit $fail
