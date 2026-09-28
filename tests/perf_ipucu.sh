#!/usr/bin/env bash
# PERFORMANS IPUCU (K167, 2026-09-28): `TULPAR_PERF_HINTS=1` ile kanitli
# (sinir denetimsiz, dalsiz) dizi erisimi KURULAMAYAN en dis donguler icin
# derleyici nedenini ve cozumunu soyler. Eskiden erisim sessizce bekcili yola
# dusuyordu; `i < n` ile `i < len(a)` farki olculdu: 20M int, 5 tur toplama
# 256 ms -> 11 ms (bu makine, 2026-09-28).
#
# Kapi uc seyi olcer: (1) her neden sinifi dogru adlandiriliyor, (2) kanitli
# dongu icin ipucu YOK (pozitif kontrol: kapi "her donguye ipucu bas"a
# donusmesin), (3) degisken yokken hicbir ipucu basilmiyor (varsayilan kapali).
#
#   tests/perf_ipucu.sh [tulpar_yolu]
set -uo pipefail
export LC_ALL=C   # ipucu metinleri tr_en; beklenenler Ingilizce bicim
TUL="${1:-./tulpar}"
[ -x "$TUL" ] || { echo "HATA: '$TUL' calistirilabilir degil" >&2; exit 1; }
TUL="$(cd "$(dirname "$TUL")" && pwd)/$(basename "$TUL")"
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
fail=0
n_gecti=0
gecti() { n_gecti=$((n_gecti + 1)); printf "  \033[0;32mgecti\033[0m  %s\n" "$1"; }
dustu() { printf "  \033[0;31mDUSTU\033[0m  %s\n" "$1"; fail=1; }

cat > "$TMP/ipucu.tpr" <<'EOF'
int[] g = [1, 2, 3, 4];
func sinir_ad(int[] a, int n): int {
    int s = 0;
    for (int i = 0; i < n; i++) { s = s + a[i]; }
    return s;
}
func kanitli(int[] a): int {
    int s = 0;
    for (int i = 0; i < len(a); i++) { s = s + a[i]; }
    return s;
}
func kucuk_esit(int[] a): int {
    int s = 0;
    int n = len(a) - 1;
    for (int i = 0; i <= n; i++) { s = s + a[i]; }
    return s;
}
func while_len(int[] a): int {
    int s = 0;
    int i = 0;
    while (i < len(a)) { s = s + a[i]; i = i + 1; }
    return s;
}
func float_yazma(int[] a) {
    for (int i = 0; i < len(a); i++) { a[i] = 2.5; }
}
func tum_int(int n): int {
    int s = 0;
    for (int i = 0; i < n; i++) { s = s + g[i]; }
    return s;
}
int[] a = [1, 2, 3, 4];
print(sinir_ad(a, 4) + kanitli(a) + kucuk_esit(a) + while_len(a) + tum_int(4));
EOF

out=$(cd "$TMP" && TULPAR_PERF_HINTS=1 TULPAR_AOT_NOCACHE=1 "$TUL" build ipucu.tpr ipucu.out 2>&1); rc=$?
[ "$rc" -eq 0 ] && gecti "ipucu acikken derleme basarili (ipucu hata degil)" || dustu "derleme rc=$rc: $out"

bekle() {   # bekle <ad> <satir> <metin>
    if echo "$out" | grep -A1 -F -- "$3" | grep -qF -- "ipucu.tpr:$2"; then gecti "$1"
    else dustu "$1: satir $2 icin '$3' yok"; echo "$out" | sed 's/^/         /'; fi
}
bekle "sinir bir AD (i < n)" 4 'the loop bound is `n`, not `len(a)`'
bekle "kosul <= " 15 'the condition is not of the form `i < ...`'
bekle "while siniri len(a)" 21 "the while condition's bound is not a NAME"
bekle "int olmayan eleman yazmasi" 25 'the body writes a non-int element'

if echo "$out" | grep -qF "ipucu.tpr:9"; then dustu "kanitli dongu (satir 9) icin ipucu basildi"
else gecti "kanitli dongu icin ipucu YOK (pozitif kontrol)"; fi

n29=$(echo "$out" | grep -cF "ipucu.tpr:29")
if [ "$n29" -eq 1 ]; then gecti "tumu-int fonksiyonda global dizi (i < n): ipucu bir kez"
else dustu "satir 29 ipucu sayisi $n29 (beklenen 1)"; fi

sessiz=$(cd "$TMP" && TULPAR_AOT_NOCACHE=1 "$TUL" build ipucu.tpr ipucu2.out 2>&1)
if echo "$sessiz" | grep -qiF "performance hint"; then dustu "degisken yokken ipucu basildi"
else gecti "varsayilan kapali: degisken yokken ipucu yok"; fi

sonuc=$(cd "$TMP" && ./ipucu.out 2>&1)
[ "$sonuc" = "50" ] && gecti "program dogru sonucu veriyor (50)" || dustu "sonuc '$sonuc' (beklenen 50)"

if [ "$fail" -eq 0 ]; then echo "perf_ipucu: $n_gecti/$n_gecti"; fi
exit $fail
