#!/usr/bin/env bash
# FOR BASLIGINDA BILESIK ARTIM — DONGU PLANLARI KAPISI (2026-10-02).
#
# `for (...; ...; i += K)` 2026-10-02'ye kadar ayristirma hatasiydi. Parser
# artik ad hedefinde `i op= e`'yi `i = i op e`'ye seker aciyor; bu kapi o
# seker acmanin DONGU PLANLARINA ulastigini olcer — `i += K` yazan dongu
# `i = i + K` yazanla AYNI surumu almali:
#
#   [sver] i   struct dizisi dongu surumu (#453)
#   [fver] j   float dizi surumu (#432)
#   [iaver] j  int dizi surumu (#438)
#
# Her plan icin uc derleme: `i = i + 1` (pozitif kontrol: kapi surumu
# goruyor), `i += 1` (asil iddia), `i += -1` (negatif kontrol: K > 0 degil,
# surum KURULMAMALI — kapi "her seye evet" demiyor). Anlam (cikti dogru mu)
# tests/for_bilesik_artim.test.tpr'de.
#
#   tests/for_bilesik_artim.sh [tulpar_yolu]
set -uo pipefail
TUL="${1:-./tulpar}"
[ -x "$TUL" ] || { echo "HATA: '$TUL' calistirilabilir degil" >&2; exit 1; }
TUL="$(cd "$(dirname "$TUL")" && pwd)/$(basename "$TUL")"
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
export LC_ALL=C
fail=0
gecti() { printf "  \033[0;32mgecti\033[0m  %s\n" "$1"; }
dustu() { printf "  \033[0;31mDUSTU\033[0m  %s\n" "$1"; fail=1; }

derle() {   # derle <ad> -> derleyici cikisi (TULPAR_DBG_VER)
    (cd "$TMP" && TULPAR_DBG_VER=1 TULPAR_AOT_NOCACHE=1 "$TUL" build "$1.tpr" "$1.out" 2>&1 | tr -d '\r')
}
# `grep -q` boruyu erken kapatir; pipefail altinda yazan taraf SIGPIPE ile
# duser. Cikti degiskende, here-string ile.
var_mi() { grep -qF -- "$2" <<< "$1"; }

# karar <ad> <isaret> <beklenen: evet|hayir> -> $TMP/<ad>.tpr hazir olmali
karar() {
    local ad="$1" isaret="$2" bekle="$3" out var=hayir
    out=$(derle "$ad")
    var_mi "$out" "$isaret" && var=evet
    if [ "$var" = "$bekle" ]; then gecti "$ad: '$isaret' $bekle"
    else dustu "$ad: '$isaret' '$var', beklenen '$bekle'"; echo "$out" | sed 's/^/         /'; fi
}

for ARTIM in 'i = i + 1' 'i += 1' 'i += -1'; do
    case "$ARTIM" in
        'i = i + 1') ek=topla; bekle=evet ;;
        'i += 1')    ek=bilesik; bekle=evet ;;
        *)           ek=negatif; bekle=hayir ;;
    esac
    JART="${ARTIM//i/j}"

    # struct dizisi surumu
    printf 'struct P { float x; float y; }\nfunc f(P[] a, int n) {\n    for (int i = 0; i < n; %s) { a[i].x = a[i].x + a[i].y; }\n}\nP[] a = [];\nfor (int k = 0; k < 16; k = k + 1) { push(a, { x: 1.0, y: 2.0 }); }\nf(a, 8);\nprint(a[3].x);\n' \
        "$ARTIM" > "$TMP/s_$ek.tpr"
    karar "s_$ek" '[sver] i' "$bekle"

    # float dizi surumu
    printf 'func f(float[] a, float[] b, int n, int m) {\n    for (int i = 0; i < m; i++) {\n        for (int j = 0; j < n; %s) { a[i * n + j] = a[i * n + j] + b[j] * 2.0; }\n    }\n}\nfloat[] a = array_fill(64, 1.0);\nfloat[] b = array_fill(64, 1.0);\nf(a, b, 8, 8);\nprint(a[63]);\n' \
        "$JART" > "$TMP/f_$ek.tpr"
    karar "f_$ek" '[fver] j' "$bekle"

    # int dizi surumu
    printf 'func f(int[] a, int[] b, int n, int m) {\n    for (int i = 0; i < m; i++) {\n        for (int j = 0; j < n; %s) { a[i * n + j] = a[i * n + j] + b[j] * 2; }\n    }\n}\nint[] a = array_fill(64, 1);\nint[] b = array_fill(64, 1);\nf(a, b, 8, 8);\nprint(a[63]);\n' \
        "$JART" > "$TMP/n_$ek.tpr"
    karar "n_$ek" '[iaver] j' "$bekle"
done

# Uc bicimin ikilisi AYNI ciktiyi vermeli (negatif bicim burada dongu
# sonsuz olurdu — o yalniz derleniyor, kosulmuyor).
for p in s f n; do
    a=$("$TMP/${p}_topla.out" 2>&1 | tr -d '\r')
    b=$("$TMP/${p}_bilesik.out" 2>&1 | tr -d '\r')
    if [ -n "$a" ] && [ "$a" = "$b" ]; then gecti "$p: '+= 1' ile '= + 1' ayni cikti ($a)"
    else dustu "$p: cikti farkli: '$a' / '$b'"; fi
done

if [ "$fail" -ne 0 ]; then echo "for_bilesik_artim: DUSTU"; exit 1; fi
echo "for_bilesik_artim: gecti"
