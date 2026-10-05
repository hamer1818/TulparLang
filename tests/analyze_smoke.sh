#!/usr/bin/env bash
# `tulpar analyze` KAPISI (K157, 2026-09-29).
#
# Rapor iki mevcut kuralin cikisi: typeinfer'in `@no_alloc` denetimi (K041)
# ve backend'in hizli yol ipuclari (K167). Kapi, raporun o kurallarla AYNI
# seyi soyledigini olcuyor — yoksa rapor ayri bir "kopya" gibi sessizce
# ayrisabilirdi:
#   1. rapordaki her fonksiyon icin `@no_alloc` isareti konup `tulpar
#      typecheck` kosuluyor: "ayirmasiz" dediklerinde typecheck TEMIZ,
#      "AYIRIYOR" dediklerinde typecheck o fonksiyonu REDDEDIYOR. (Pozitif
#      kontrol ikisini birden istiyor: yalniz birini olcmek "hepsine ayni
#      cevap" hatasini goremezdi.)
#   2. gecisli neden: cagiranin nedeni cagrilani adiyla soyluyor.
#   3. hizli yol: sinir `n - 1` iken ipucu VAR, `len(a)` iken YOK (kontrol).
#   4. cikis kodlari: temiz 0, @no_alloc ihlali 1, kullanim/ayristirma 2.
#
#   tests/analyze_smoke.sh [tulpar_yolu]
set -uo pipefail
export LC_ALL=C TULPAR_LANG=en
TUL="${1:-./tulpar}"
[ -x "$TUL" ] || { echo "HATA: '$TUL' calistirilabilir degil" >&2; exit 1; }
TUL="$(cd "$(dirname "$TUL")" && pwd)/$(basename "$TUL")"
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
fail=0
n=0
gecti() { n=$((n + 1)); printf "  \033[0;32mgecti\033[0m  %s\n" "$1"; }
dustu() { printf "  \033[0;31mDUSTU\033[0m  %s\n" "$1"; fail=1; }

cat > "$TMP/p.tpr" <<'EOF'
func topla(int a, int b): int { return a + b; }
func selam(str ad): str { return "selam " + ad; }
func sar(str ad): str { return selam(ad); }
func liste(): int { int[] b = [1, 2]; return len(b); }
func toplam(int[] a, int n): int {
    int s = 0;
    for (int i = 0; i < n - 1; i++) { s = s + a[i]; }
    return s;
}
int[] xs = [1, 2, 3];
print(toString(topla(1, 2)) + selam("x") + sar("y") + toString(liste() + toplam(xs, 3)));
EOF

out=$("$TUL" analyze "$TMP/p.tpr" 2>&1); rc=$?
[ $rc -eq 0 ] && gecti "temiz dosya: cikis 0" || { dustu "temiz dosya rc=$rc"; echo "$out" | sed 's/^/      /'; }

# 1) Rapor == @no_alloc denetimi, fonksiyon fonksiyon.
satirlar=$(echo "$out" | grep -E '^  [a-z_]+ +line [0-9]+ +(no-alloc|ALLOCATES)')
[ "$(echo "$satirlar" | wc -l)" -eq 5 ] && gecti "rapor bes fonksiyonu da listeliyor" \
    || { dustu "rapor satirlari beklenmedik"; echo "$out" | sed 's/^/      /'; }
temiz_n=0; kirli_n=0
while read -r ad _ _ durum _; do
    [ -n "$ad" ] || continue
    sed "s/^func $ad(/@no_alloc\nfunc $ad(/" "$TMP/p.tpr" > "$TMP/isaret.tpr"
    grep -q "^@no_alloc" "$TMP/isaret.tpr" || { dustu "isaret konamadi: $ad"; continue; }
    "$TUL" typecheck "$TMP/isaret.tpr" > "$TMP/tc.log" 2>&1; trc=$?
    if [ "$durum" = "no-alloc" ]; then
        temiz_n=$((temiz_n + 1))
        [ $trc -eq 0 ] || { dustu "$ad: rapor 'no-alloc' ama @no_alloc typecheck'i reddediyor"; sed 's/^/      /' "$TMP/tc.log" | tail -3; }
    else
        kirli_n=$((kirli_n + 1))
        if [ $trc -ne 0 ] && grep -q "'$ad' is @no_alloc but allocates" "$TMP/tc.log"; then :; else
            dustu "$ad: rapor 'ALLOCATES' ama @no_alloc typecheck'ten geciyor (rc=$trc)"
        fi
    fi
done <<< "$satirlar"
[ $temiz_n -eq 2 ] && [ $kirli_n -eq 3 ] \
    && gecti "rapor == @no_alloc denetimi (2 ayirmasiz typecheck'ten gecti, 3 ayiran reddedildi)" \
    || dustu "beklenen 2 ayirmasiz / 3 ayiran, gorulen $temiz_n / $kirli_n"

# 2) Gecisli neden cagrilani adlandiriyor.
echo "$out" | grep -E "^  sar .*ALLOCATES — call to 'selam' allocates" > /dev/null \
    && gecti "gecisli neden: sar -> 'selam' cagrisi" || dustu "sar icin gecisli neden yok"

# 3) Hizli yol: `n - 1` ipucu var; `len(a)` ile yok (kontrol).
grep -q 'performance hint: `a\[i\]` stays bounds-checked' <<<"$out" \
    && grep -q '1 fast-path hints' <<<"$out" \
    && gecti "hizli yol: 'n - 1' siniri icin ipucu" || dustu "hizli yol ipucu yok"
sed 's/i < n - 1; i++/i < len(a); i++/' "$TMP/p.tpr" > "$TMP/kanitli.tpr"
out2=$("$TUL" analyze "$TMP/kanitli.tpr" 2>&1)
grep -q '0 fast-path hints' <<<"$out2" && ! grep -q 'performance hint' <<<"$out2" \
    && gecti "kontrol: 'len(a)' ile ipucu yok" || dustu "kanitli dongude ipucu cikti"

# 4) Cikis kodlari.
sed 's/^func selam(/@no_alloc\nfunc selam(/' "$TMP/p.tpr" > "$TMP/ihlal.tpr"
"$TUL" analyze "$TMP/ihlal.tpr" > "$TMP/ihlal.log" 2>&1; rc=$?
[ $rc -eq 1 ] && grep -q 'selam .*ALLOCATES.*@no_alloc' "$TMP/ihlal.log" \
    && gecti "@no_alloc ihlali: cikis 1, satir isaretli" || dustu "@no_alloc ihlali rc=$rc"
printf 'int x = ;\n' > "$TMP/bozuk.tpr"
"$TUL" analyze "$TMP/bozuk.tpr" > /dev/null 2>&1; rc=$?
[ $rc -eq 2 ] && gecti "ayristirma hatasi: cikis 2" || dustu "ayristirma hatasi rc=$rc"
"$TUL" analyze > /dev/null 2>&1; rc=$?
[ $rc -eq 2 ] && gecti "dosyasiz: cikis 2" || dustu "dosyasiz rc=$rc"

if [ $fail -ne 0 ]; then echo "analyze kapisi DUSTU"; exit 1; fi
echo "analyze kapisi temiz ($n denetim: rapor == @no_alloc denetimi, gecisli neden, hizli yol + kontrol, cikis kodlari)"
