#!/usr/bin/env bash
# SOGUK YOL INDEKSI + DIZI MAIN YERELI KAPISI (2026-10-02).
#
# Sekil onbellekli dizi erisiminin genel (soguk) yolundaki 8/16 bayt adimli
# adresler indeksi volatile bir yuvadan geciriyor: LLVM'in LSR'i o adresler
# icin sicak donguye sayac kurmasin (llvm_backend.cpp cold_index_opaque).
# Bunun sayesinde ust duzey (yalniz main'de gorulen) DIZI bildirimleri de
# main yereline terfi ediliyor. Anlam: tests/soguk_indeks.test.tpr (gecisler
# soguk yolu GERCEKTEN kosturuyor — indeksi bozan sabotajda kirmizi). Bu kapi:
#
#   1. KARAR (IR): onbellekli dongunun genel yolunda `load volatile` var ve
#      TULPAR_NO_COLD_IX=1 ile YOK (pozitif kontrol, iki yon).
#   2. TERFI: yalniz main'de gorulen `int[]` / `float[]` / `array` global
#      DEGIL; fonksiyonda gecen dizi global; TULPAR_NO_ML_ARR=1 ile hepsi
#      global (pozitif kontrol); `int` hala global (olculmus kural).
#   3. SONUC: dort derleme (varsayilan / iki anahtar ayri ayri / ikisi
#      birden) ayni ciktiyi veriyor; sinir disi hala yakalaniyor.
#
# Etki (LSR'in sicak donguye koydugu sayac sayisi) IR'da gorunmuyor — llc
# icinde. Olcum: docs/mindmap/Performance.md "Soguk yol indeksi".
#
#   tests/soguk_indeks.sh [tulpar_yolu]
set -uo pipefail
TUL="${1:-./tulpar}"
[ -x "$TUL" ] || { echo "HATA: '$TUL' calistirilabilir degil" >&2; exit 1; }
TUL="$(cd "$(dirname "$TUL")" && pwd)/$(basename "$TUL")"
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
export LC_ALL=C
fail=0
n_gecti=0
gecti() { n_gecti=$((n_gecti + 1)); printf "  \033[0;32mgecti\033[0m  %s\n" "$1"; }
dustu() { printf "  \033[0;31mDUSTU\033[0m  %s\n" "$1"; fail=1; }

cat > "$TMP/e.tpr" <<'TPREOF'
func elek(int n): int {
    array f = array_fill(n + 1, 0);
    int c = 0;
    int i = 2;
    while (i <= n) {
        if (f[i] == 0) {
            c = c + 1;
            int k = i * i;
            while (k <= n) { f[k] = 1; k = k + i; }
        }
        i = i + 1;
    }
    return c;
}
int[] yalniz_int = array_fill(8, 1);
float[] yalniz_flt = array_fill(8, 0.5);
array yalniz_dz = [1, 2, 3];
int[] paylasilan = array_fill(4, 2);
int sayi = 3;
func oku(): int { return paylasilan[1]; }
int t = 0;
for (int i = 0; i < 8; i = i + 1) { t = t + yalniz_int[i] + toInt(yalniz_flt[i] * 2.0); }
t = t + yalniz_dz[2] + oku() + sayi;
print(elek(100000), t);
TPREOF
BEK="9592 24"

derle() {   # derle <cikti> [ortam...]
    local out="$1"; shift
    (cd "$TMP" && env "$@" TULPAR_AOT_NOCACHE=1 TULPAR_AOT_EMIT_LL=1 TULPAR_AOT_EMIT_LL_PRE=1 \
        "$TUL" build e.tpr "$out" >"$out.log" 2>&1)
}
has_global() { grep -q "^@tpr_g_$2 = " "$1"; }

for c in v:X=1 ci:TULPAR_NO_COLD_IX=1 ma:TULPAR_NO_ML_ARR=1 iki:TULPAR_NO_COLD_IX=1,TULPAR_NO_ML_ARR=1; do
    ad=${c%%:*}; ort=${c#*:}
    if ! derle "$ad" ${ort//,/ }; then dustu "$ad derlenemedi"; sed -n '1,10p' "$TMP/$ad.log"; fi
done

# 1. KARAR (IR)
if grep -q 'load volatile i64' "$TMP/v.ll" 2>/dev/null; then gecti "IR: soguk yol indeksi volatile yuvadan geciyor"
else dustu "IR: 'load volatile i64' YOK — soguk yol indeksi devrede degil"; fi
if grep -q 'load volatile i64' "$TMP/ci.ll" 2>/dev/null; then dustu "pozitif kontrol: TULPAR_NO_COLD_IX=1 iken de volatile var — kapi kor"
else gecti "pozitif kontrol: TULPAR_NO_COLD_IX=1 iken volatile yok"; fi

# 2. TERFI (optimizasyon ONCESI IR)
PL="$TMP/v.pre.ll"
for g in yalniz_int yalniz_flt yalniz_dz; do
    if has_global "$PL" "$g"; then dustu "yalniz main'de gorulen dizi '$g' hala global"
    else gecti "yalniz main'de gorulen dizi '$g' main yereli"; fi
done
if has_global "$PL" paylasilan; then gecti "fonksiyonda okunan dizi global kaliyor"
else dustu "fonksiyonda okunan 'paylasilan' global DEGIL"; fi
if has_global "$PL" sayi; then gecti "int ust duzey global kaliyor (olculmus kural)"
else dustu "int ust duzey terfi edildi — elek LSR gerilemesi geri gelir"; fi
if has_global "$TMP/ma.pre.ll" yalniz_int && has_global "$TMP/ma.pre.ll" yalniz_dz; then
    gecti "pozitif kontrol: TULPAR_NO_ML_ARR=1 iken diziler global"
else dustu "pozitif kontrol: TULPAR_NO_ML_ARR=1 iken de global yok — kapi kor"; fi

# 3. SONUC
for ad in v ci ma iki; do
    o=$(cd "$TMP" && ./"$ad" 2>&1 | tr -d '\r')
    if [ "$o" = "$BEK" ]; then gecti "sonuc ($ad): $o"
    else dustu "sonuc ($ad): '$o' (beklenen '$BEK')"; fi
done

# Onbellekli dongude sinir disi (genel yola duser, hata orada).
cat > "$TMP/s.tpr" <<'TPREOF'
func f(int n): int {
    int[] a = array_fill(8, 1);
    int t = 0;
    int k = 0;
    while (k <= n) { a[k] = 2; t = t + a[k]; k = k + 3; }
    return t;
}
print(f(9));
TPREOF
if (cd "$TMP" && TULPAR_AOT_NOCACHE=1 "$TUL" build s.tpr s >s.log 2>&1); then
    out=$(cd "$TMP" && ./s 2>&1 | tr -d '\r'); rc=$?   # pipefail: tr degil programin cikisi
    if [ "$rc" -ne 0 ] && grep -qiE 'out of bounds|sinir disinda' <<< "$out"; then
        gecti "sinir disi: calisma zamani hatasi (rc=$rc)"
    else dustu "sinir disi: rc=$rc cikti='$out'"; fi
else
    dustu "sinir disi programi derlenemedi"
fi

if [ "$fail" -eq 0 ]; then echo "soguk_indeks: $n_gecti/$n_gecti"; fi
exit $fail
