#!/usr/bin/env bash
# ICE AKTARILAN MODULDEKI HATANIN KONUMU (oyun geri bildirimi #4, 2026-10-08).
#
# Ilk gercek cok dosyali oyun ("Kupler ile Kurelerin Savasi", motor deposu)
# `savas.tpr` icindeki tanimsiz bir cagriyi `oyun.tpr:311` diye ve OYUN.TPR'nin
# 311. satirinin metniyle raporladi: satir numarasi modulun, dosya adi ve
# gosterilen alinti ice aktaranin. Hatayi arayan masum bir satira bakiyordu.
# Kok neden: kodgen tanisi (report_codegen_error) her zaman backend'in KOK
# kaynak metnini/adini kullaniyordu; modulun govdeleri uretilirken baglam
# degismiyordu. K056 (2026-09-27) yalniz AYRISTIRMA hatalarini duzeltmisti.
# Ayni sinifin iki kardesi de burada olculuyor:
#   * sozcukleyici hatasi dosya adi tasimiyordu (`at line 3, col 12`) ve
#     typeinfer'in sessiz on gecisi ikinci bir kopya basiyordu;
#   * modulden donunce baglam GERI gelmeli (ana dosyanin kendi hatasi ana
#     dosyanin adiyla).
# LSP tarafi (tani kok belgede import satirina baglanir, relatedInformation
# modulun konumunu verir) tests/lsp_audit.py'de.
#
#   tests/modul_tani_konumu.sh [tulpar_yolu]
set -uo pipefail
TUL="${1:-./tulpar}"
[ -x "$TUL" ] || { echo "HATA: '$TUL' calistirilabilir degil" >&2; exit 1; }
TUL="$(cd "$(dirname "$TUL")" && pwd)/$(basename "$TUL")"
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
export LC_ALL=C TULPAR_AOT_NOCACHE=1
fail=0
n=0
gecti() { printf "  \033[0;32mgecti\033[0m  %s\n" "$1"; n=$((n + 1)); }
dustu() { printf "  \033[0;31mDUSTU\033[0m  %s\n" "$1"; fail=1; n=$((n + 1)); }
icerir() { grep -qF -- "$2" <<<"$1"; }
say() { grep -cF -- "$2" <<<"$1"; }
goster() { sed 's/^/         /' <<<"$1"; }

# Modulun 6. satiri hatali; ana dosyanin 6. satiri MASUM ve farkli metinli —
# ikisi karisirsa alinti ele verir.
printf '%s\n' '// m1' '// m2' '// m3' 'func modul_f(): int {' '    int x = 1;' \
    '    return tanimsiz_cagri() + x;' '}' > "$TMP/modul.tpr"
printf '%s\n' 'import "modul.tpr";' 'print(modul_f());' '// a3' '// a4' '// a5' \
    '// ana dosyanin masum altinci satiri' > "$TMP/ana.tpr"

# 1) Kodgen hatasi modulde: modulun adi + modulun satiri + modulun alintisi.
out=$(cd "$TMP" && "$TUL" ana.tpr 2>&1); rc=$?
if [ $rc -ne 0 ] && icerir "$out" "'tanimsiz_cagri'" && icerir "$out" "--> modul.tpr:6" \
   && icerir "$out" "return tanimsiz_cagri() + x;" && ! icerir "$out" "ana.tpr:6" \
   && ! icerir "$out" "masum altinci"; then
    gecti "kodgen hatasi modulde: 'modul.tpr:6' + modulun satiri"
else dustu "kodgen hatasi modulde (rc=$rc)"; goster "$out"; fi

# 2) Ic ice: ana -> ara -> derin. Hata en derindeki dosyanin adiyla.
printf '%s\n' 'import "derin.tpr";' 'func ara_f(): int { return derin_f(); }' > "$TMP/ara.tpr"
printf '%s\n' '// d1' 'func derin_f(): int {' '    return yok_boyle_bir_f(2);' '}' > "$TMP/derin.tpr"
printf '%s\n' 'import "ara.tpr";' 'print(ara_f());' '// a3 masum' > "$TMP/ana2.tpr"
out=$(cd "$TMP" && "$TUL" ana2.tpr 2>&1); rc=$?
if [ $rc -ne 0 ] && icerir "$out" "--> derin.tpr:3" && icerir "$out" "return yok_boyle_bir_f(2);" \
   && ! icerir "$out" "ana2.tpr:3" && ! icerir "$out" "ara.tpr:3"; then
    gecti "ic ice modul: 'derin.tpr:3'"
else dustu "ic ice modul (rc=$rc)"; goster "$out"; fi

# 3) Modulden DONUNCE baglam geri gelir: ana dosyanin kendi hatasi ana
#    dosyanin adiyla (modul temiz).
printf '%s\n' 'func temiz_f(): int { return 1; }' > "$TMP/temiz.tpr"
printf '%s\n' 'import "temiz.tpr";' 'print(temiz_f());' 'print(ana_tanimsiz(3));' > "$TMP/ana3.tpr"
out=$(cd "$TMP" && "$TUL" ana3.tpr 2>&1); rc=$?
if [ $rc -ne 0 ] && icerir "$out" "--> ana3.tpr:3" && icerir "$out" "print(ana_tanimsiz(3));" \
   && ! icerir "$out" "temiz.tpr:3"; then
    gecti "modulden sonra ana dosyanin hatasi: 'ana3.tpr:3'"
else dustu "modulden sonra baglam (rc=$rc)"; goster "$out"; fi

# 4) Sozcukleyici hatasi modulde: dosya adi + satir:sutun, TEK kopya
#    (typeinfer'in sessiz on gecisi artik basmiyor).
printf '%s\n' 'func f(): int {' '  int a = 1;' '  return a $ 2;' '}' > "$TMP/mlex.tpr"
printf '%s\n' 'import "mlex.tpr";' 'print(f());' > "$TMP/alex.tpr"
out=$(cd "$TMP" && "$TUL" alex.tpr 2>&1); rc=$?
k=$(say "$out" "Unknown character '\$'")
if [ $rc -ne 0 ] && icerir "$out" "--> mlex.tpr:3:12" && [ "$k" = "1" ]; then
    gecti "sozcukleyici hatasi modulde: 'mlex.tpr:3:12', tek kopya"
else dustu "sozcukleyici hatasi modulde (rc=$rc, kopya=$k)"; goster "$out"; fi

# 5) Sozcukleyici hatasi ana dosyada: ayni, ana dosyanin adiyla.
printf '%s\n' 'int a = 1;' 'print(a $ 2);' > "$TMP/lexana.tpr"
out=$(cd "$TMP" && "$TUL" lexana.tpr 2>&1); rc=$?
k=$(say "$out" "Unknown character '\$'")
if [ $rc -ne 0 ] && icerir "$out" "--> lexana.tpr:2:9" && [ "$k" = "1" ]; then
    gecti "sozcukleyici hatasi ana dosyada: 'lexana.tpr:2:9', tek kopya"
else dustu "sozcukleyici hatasi ana dosyada (rc=$rc, kopya=$k)"; goster "$out"; fi

# 6) `tulpar typecheck` da sozcukleyici hatasinda modulun adini basar.
out=$(cd "$TMP" && "$TUL" typecheck alex.tpr 2>&1); rc=$?
if [ $rc -ne 0 ] && icerir "$out" "--> mlex.tpr:3:12"; then
    gecti "typecheck: 'mlex.tpr:3:12'"
else dustu "typecheck sozcukleyici (rc=$rc)"; goster "$out"; fi

# Pozitif kontrol: ayni duzen, hatasiz modul derlenir ve calisir — yukaridaki
# kirmizilar duzenin kendisinden degil, hatanin kendisinden.
printf '%s\n' '// m1' '// m2' '// m3' 'func modul_f(): int {' '    int x = 1;' \
    '    return 41 + x;' '}' > "$TMP/modul.tpr"
out=$(cd "$TMP" && "$TUL" ana.tpr 2>&1); rc=$?
if [ $rc -eq 0 ] && [ "$(tail -1 <<<"$out")" = "42" ]; then gecti "hatasiz modul calisir (pozitif kontrol)"
else dustu "hatasiz modul (rc=$rc)"; goster "$out"; fi

[ "$fail" -eq 0 ] && echo "modul_tani_konumu: $n/$n"
exit $fail
