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
# Sigmayan yazma govdenin ILK deyimiyse (K201) hizli surum sigma sinavi +
# genel surume gecisle ACILIR; ilk deyim degilse acilmaz.
kanit tasan_sabit    evet  'a[i] = 5000000000;'
kanit tasan_sabit_2  hayir 'int t = 0; a[i] = 5000000000;'
kanit tasan_ofset    evet  'a[i] = i + 2147483647;'
kanit tasan_ofset_2  hayir 'int t = 0; a[i] = i + 2147483647;'
kanit bilesik_arti   hayir 'a[i] += 1;'
kanit artirma        hayir 'a[i]++;'
kanit degisen_ad     hayir 'k = k + 1; a[i] = k;'
kanit bilesik_ve     evet  'a[i] &= 255;'

# ---- sinir: `i < n`, `i <= n`, `i < len(b)` (2026-09-28) ----
# Kanit dongu basindaki `n <= count(a)` sinavina dayaniyor. Iki yon: kurulmali
# ([ver]) VE sinav tutmazsa (n > len) hizli yol ACILMAMALI — acilirsa a[len]'i
# sessizce okur. Genel yol sinir disi okumayi yakalar (strict: hata, cikis != 0).
sinir() {   # sinir <ad> <kosul> <n> <beklenen: evet|hayir> <cikis: 0|hata> [govde]
    local ad="$1" kosul="$2" n="$3" bekle="$4" cikis="$5" govde="${6:-s = s + a[i];}"
    printf 'func f(int[] a, int[] b, int n): int {\n    int s = 0;\n    for (int i = 0; %s; i++) { %s }\n    return s;\n}\nint[] a = array_fill(8, 1);\nint[] b = array_fill(6, 1);\nprint(f(a, b, %s));\n' \
        "$kosul" "$govde" "$n" > "$TMP/$ad.tpr"
    local out rc var=hayir
    out=$(cd "$TMP" && TULPAR_DBG_VER=1 TULPAR_AOT_NOCACHE=1 "$TUL" build "$ad.tpr" "$ad.out" 2>&1)
    echo "$out" | grep -qF '[ver] a[i]' && var=evet
    if [ "$var" = "$bekle" ]; then gecti "$ad: kanit $bekle"
    else dustu "$ad: kanit '$var', beklenen '$bekle' — $kosul"; fi
    out=$(cd "$TMP" && ./"$ad.out" 2>&1); rc=$?
    if [ "$cikis" = "0" ] && [ "$rc" -eq 0 ]; then gecti "$ad: calisti ($out)"
    elif [ "$cikis" = "hata" ] && [ "$rc" -ne 0 ]; then gecti "$ad: sinir disi YAKALANDI (rc=$rc)"
    else dustu "$ad: rc=$rc cikti='$out' (beklenen $cikis)"; fi
}
sinir ad_tam        'i < n'      8 evet 0
sinir ad_kismi      'i < n'      5 evet 0
sinir ad_esit       'i <= n'     7 evet 0
sinir ad_tasan      'i < n'      9 evet hata
sinir ad_esit_tasan 'i <= n'     8 evet hata
sinir baska_dizi    'i < len(b)' 0 evet 0 's = s + a[i] * b[i];'
# b dongude indekslenmiyorsa sekil onbelleginde yok: sinir sinanamaz, kanit yok.
sinir baska_dizi_yok 'i < len(b)' 0 hayir 0
# K201 oku-yaz: `a[i] = a[i] + b[i]` (ilk deyim, sigmazsa genel surume gecis).
sinir oku_yaz       'i < len(b)' 0 evet 0 'a[i] = a[i] + b[i];'
# ilk deyim DEGIL: onceki etki (s) turu bastan kosmayi yanlis yapar — kanit yok.
sinir oku_yaz_ikinci 'i < len(b)' 0 hayir 0 's = s + 1; a[i] = a[i] + b[i];'

if [ "$fail" -eq 0 ]; then echo "kanitli_yazma: $n_gecti/$n_gecti"; fi
exit $fail
