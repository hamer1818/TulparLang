#!/usr/bin/env bash
# YEREL GIRIS NOKTASI `t_<ad>.n` (2026-10-06) — yapi + anlam + pozitif kontrol.
#
# `func qs(int[] a, int lo, int hi)` gibi tipli ama native ABI'ye sigmayan
# fonksiyonun `int` parametreleri ham i64 geciyor (bkz. llvm_backend.cpp
# "YEREL GIRIS NOKTASI"). Olculdu (benchmarks/fair qsort, Ryzen 7 9800X3D,
# 2026-10-06, taskset -c 2,3, en iyi 7): 70,5 -> 65,3 ms.
#
# Kapi:
#   1. YAPI (optimizasyon oncesi IR): `t_qs.n` var ve main ONU dogrudan
#      cagiriyor; `t_qs.f` girisinde `.n`ye dagitim var; parametresi yeniden
#      baglanan `azalt`ta ve esikten buyuk govdede `.n` yok;
#   2. KAPATMA ANAHTARI: TULPAR_NO_YEREL_GIRIS=1 ile hicbir `.n` yok (iki yon);
#   3. ANLAM: tests/yerel_giris.test.tpr acik ve kapali derlemede geciyor,
#      ciktilar birebir ayni;
#   4. POZITIF KONTROL: TULPAR_YEREL_GIRIS_SINAMA=etiket (kutulu argumanin
#      etiket sinavi atlanir — dizgi `.n`ye ham yuk olarak gider) anlam testini
#      KIRMIZIYA ceviriyor.
#
#   tests/yerel_giris.sh [tulpar_yolu]
set -uo pipefail
TUL="${1:-./tulpar}"
[ -x "$TUL" ] || { echo "HATA: '$TUL' calistirilabilir degil" >&2; exit 1; }
TUL="$(cd "$(dirname "$TUL")" && pwd)/$(basename "$TUL")"
ROOT="$(pwd)"
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
fail=0
n_gecti=0
gecti() { n_gecti=$((n_gecti + 1)); printf "  \033[0;32mgecti\033[0m  %s\n" "$1"; }
dustu() { printf "  \033[0;31mDUSTU\033[0m  %s\n" "$1"; fail=1; }

cp "$ROOT/benchmarks/fair/qsort.tpr" "$TMP/qs.tpr"
cat >> "$TMP/qs.tpr" <<'EOF'
func azalt(int[] d, int n) {
    int s = 0;
    while (n > 0) { s = s + n; n = n - 1; }
    return s;
}
print(azalt(a, 4));
EOF
# Esikten (100 AST dugumu) buyuk govde: 60 deyim.
{
  echo 'func buyuk(int[] d, int k) {'
  echo '    int s = 0;'
  for i in $(seq 1 60); do echo "    s = s + k * $i;"; done
  echo '    return s;'
  echo '}'
  echo 'print(buyuk(a, 2));'
} >> "$TMP/qs.tpr"

# 1. yapi
(cd "$TMP" && TULPAR_AOT_NOCACHE=1 TULPAR_AOT_EMIT_LL_PRE=1 BENCH_N=1000 "$TUL" build qs.tpr on >/dev/null 2>&1)
LL="$TMP/on.pre.ll"
main_calls=0
if [ -f "$LL" ]; then
    main_calls=$(awk '/^define .*@main\(/,/^}/' "$LL" | grep -c 'call .*@t_qs\.n(')
fi
if [ "$main_calls" -ge 1 ] && grep -q '^define internal .*@t_qs\.n(' "$LL"; then
    gecti "t_qs.n tanimli, main dogrudan cagiriyor ($main_calls cagri)"
else dustu "t_qs.n yok ya da main cagirmiyor (IR: $LL, cagri=$main_calls)"; fi
fdisp=0
[ -f "$LL" ] && fdisp=$(awk '/^define .*@t_qs\.f\(/,/^}/' "$LL" | grep -c 'call .*@t_qs\.n(')
if [ "$fdisp" -ge 1 ]; then gecti "t_qs.f girisi etiketler INT ise t_qs.n'ye dagitiyor"
else dustu "t_qs.f'de .n dagitimi yok"; fi
if [ -f "$LL" ] && ! grep -q '@t_azalt\.n(' "$LL" && ! grep -q '@t_buyuk\.n(' "$LL" &&
   grep -q '@t_azalt\.f(' "$LL" && grep -q '@t_buyuk\.f(' "$LL"; then
    gecti "yeniden baglanan parametre (azalt) ve esikten buyuk govde (buyuk): .n yok"
else dustu "uygun olmayan fonksiyona .n uretildi (ya da .f yok)"; fi

# 2. kapatma anahtari
(cd "$TMP" && TULPAR_AOT_NOCACHE=1 TULPAR_AOT_EMIT_LL_PRE=1 TULPAR_NO_YEREL_GIRIS=1 BENCH_N=1000 \
    "$TUL" build qs.tpr off >/dev/null 2>&1)
if [ -f "$TMP/off.pre.ll" ] && ! grep -q '\.n(' "$TMP/off.pre.ll" && grep -q '@t_qs\.f(' "$TMP/off.pre.ll"; then
    gecti "TULPAR_NO_YEREL_GIRIS=1: hicbir .n yok, t_qs.f duruyor"
else dustu "kapatma anahtari calismiyor"; fi
# `tr -d '\r'`: Windows'ta ikili satirlari CRLF ile bitiriyor (CI'da olculdu).
o_on=$(cd "$TMP" && BENCH_N=20000 ./on 2>&1 | tr -d '\r')
o_off=$(cd "$TMP" && BENCH_N=20000 ./off 2>&1 | tr -d '\r')
if [ -n "$o_on" ] && [ "$o_on" = "$o_off" ] && [[ "$o_on" == *" 0"$'\n'"10"$'\n'"3660" ]]; then
    gecti "qsort (20 000) + azalt + buyuk: acik/kapali cikti ayni, bozuk sira 0"
else dustu "cikti: acik '$o_on' / kapali '$o_off'"; fi

# 3. anlam
run_suite() {
    (cd "$ROOT" && env TULPAR_AOT_NOCACHE=1 "$@" "$TUL" tests/yerel_giris.test.tpr 2>&1)
}
s_on=$(run_suite); rc_on=$?
s_off=$(run_suite TULPAR_NO_YEREL_GIRIS=1); rc_off=$?
if [ "$rc_on" -eq 0 ] && [ "$rc_off" -eq 0 ] && grep -q "Fail: 0" <<<"$s_on" &&
   [ "$(grep -v '^\[typecheck\]' <<<"$s_on")" = "$(grep -v '^\[typecheck\]' <<<"$s_off")" ]; then
    gecti "yerel_giris.test.tpr acik ve kapali geciyor, cikti ayni ($(grep -o 'Tests: [0-9]*' <<<"$s_on"))"
else dustu "anlam testi: acik rc=$rc_on / kapali rc=$rc_off: $(tail -3 <<<"$s_on")"; fi

# 4. pozitif kontrol
s_sab=$(run_suite TULPAR_YEREL_GIRIS_SINAMA=etiket); rc_sab=$?
if [ "$rc_sab" -ne 0 ] && ! grep -q "Fail: 0" <<<"$s_sab"; then
    gecti "sabotaj (etiket sinavi atlandi) anlam testini kirmiziya ceviriyor (rc=$rc_sab)"
else dustu "sabotaj YAKALANMADI — anlam testi kutulu arguman yolunu olcmuyor"; fi

if [ "$fail" -eq 0 ]; then echo "yerel_giris: $n_gecti/$n_gecti"; fi
exit $fail
