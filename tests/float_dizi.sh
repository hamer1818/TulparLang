#!/usr/bin/env bash
# FLOAT DIZI DONGU SURUMU KAPISI (2026-10-01).
#
# En icteki `for` dongusu, float dizi erisimleri `X[B + j]` biciminde ve her
# eleman yazmasi kesin float ise SURUMLENIR: dongu basinda sinir + double
# depo + etiket bir kez sinanir, hizli govdede erisim tek GEP+load/store.
# Dogruluk tests/float_dizi.test.tpr'de; bu kapi uc seyi olcer:
#
#   1. KARAR: surum kuruluyor mu / kurulmamasi gereken yerde kurulmuyor mu
#      (derleyicinin TULPAR_DBG_VER cikisi: "[fver] j" / "[fver-yok] j").
#      Iki yon de: yalniz birini olcen kapi "hep kur" / "hic kurma"
#      bozulmasini gormez.
#   2. SINIR: hizli surumu OLAN dongude sinir disi erisim hala YAKALANIYOR
#      (cikis != 0 + "out of bounds"). Dongu basindaki sinav tutmayinca
#      genel (bekcili) surume dusulmeli — dusulmezse a[len] sessizce okunur.
#   3. ETKI: hizli surum IR'de gercekten var (`fv.el` yuklemesi) ve
#      TULPAR_NO_FVER=1 ile YOK — kapinin kendisinin olctugunun pozitif
#      kontrolu.
#
#   tests/float_dizi.sh [tulpar_yolu]
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

derle() {   # derle <ad> -> derleyici cikisi
    (cd "$TMP" && TULPAR_DBG_VER=1 TULPAR_AOT_NOCACHE=1 "$TUL" build "$1.tpr" "$1.out" 2>&1)
}

# ---- 1. KARAR ---------------------------------------------------------------
# karar <ad> <beklenen: evet|hayir> <dizi tipi> <ic govde>
karar() {
    local ad="$1" bekle="$2" tip="$3" govde="$4" dolgu="1.0"
    [ "$tip" = "int" ] && dolgu="1"
    printf 'func f(%s[] a, %s[] b, int n, int m) {\n    for (int i = 0; i < m; i++) {\n        for (int j = 0; j < n; j++) { %s }\n    }\n}\n%s[] a = array_fill(64, %s);\n%s[] b = array_fill(64, %s);\nf(a, b, 8, 8);\nprint(a[63]);\n' \
        "$tip" "$tip" "$govde" "$tip" "$dolgu" "$tip" "$dolgu" > "$TMP/$ad.tpr"
    local out var=hayir
    out=$(derle "$ad")
    grep -qF '[fver] j' <<<"$out" && var=evet
    if [ "$var" = "$bekle" ]; then gecti "$ad: surum $bekle"
    else dustu "$ad: surum '$var', beklenen '$bekle' — $govde"; echo "$out" | sed 's/^/         /'; fi
}

karar matmul_bicimi  evet  float 'a[i * n + j] = a[i * n + j] + b[j] * 2.0;'
karar yerel_float    evet  float 'float d = a[j] - b[j]; a[j] = d * d;'
karar sqrt_ile       evet  float 'b[i * n + j] = sqrt(a[j] + 1.0);'
# Kesin float olmayan yazma diziyi kutuya cevirebilir: surum YOK.
karar int_yazma      hayir float 'a[j] = 1;'
karar int_ifade      hayir float 'a[j] = a[j] + 1;'
# Afin olmayan indeks (j * 2): sinir dongu basinda sinanamaz.
karar afin_degil     hayir float 'a[j * 2] = 1.0;'
# Sekli degistiren cagri (push) ve bilesik yazma.
karar push_var       hayir float 'a[j] = 1.0; push(b, 1.0);'
karar bilesik        hayir float 'a[j] += 1.0;'
# Dongu degiskeni govdede ataniyor.
karar ivar_atama     hayir float 'a[j] = 1.0; j = j + 1;'
# Tamsayi dizileri bu surume HIC girmez (ipucu: `float[]` bildirimi) —
# int dongulerinin kodu ve olculmus dengesi degismesin.
karar int_dizi       hayir int   'a[i * n + j] = a[i * n + j] + b[j];'

# ---- 2. SINIR ---------------------------------------------------------------
# sinir <ad> <kosul> <ofs> <n> <beklenen cikis: 0|hata>
sinir() {
    local ad="$1" kosul="$2" ofs="$3" n="$4" cikis="$5"
    printf 'func f(float[] a, int ofs, int n): float {\n    float t = 0.0;\n    for (int j = 0; %s; j++) { t = t + a[ofs + j]; }\n    return t;\n}\nfloat[] a = array_fill(8, 1.0);\nprint(f(a, %s, %s));\n' \
        "$kosul" "$ofs" "$n" > "$TMP/$ad.tpr"
    local out rc var=hayir
    out=$(derle "$ad")
    grep -qF '[fver] j' <<<"$out" && var=evet
    # Sinir sinavinin anlami ancak hizli surum VARKEN var: once o.
    if [ "$var" != "evet" ]; then dustu "$ad: hizli surum kurulmadi — sinir sinavi olculemez"; return; fi
    out=$(cd "$TMP" && ./"$ad.out" 2>&1); rc=$?
    if [ "$cikis" = "0" ] && [ "$rc" -eq 0 ]; then gecti "$ad: calisti ($out)"
    elif [ "$cikis" = "hata" ] && [ "$rc" -ne 0 ] && grep -qi "out of bounds" <<<"$out"; then
        gecti "$ad: sinir disi YAKALANDI (rc=$rc)"
    else dustu "$ad: rc=$rc cikti='$out' (beklenen $cikis)"; fi
}
sinir tam           'j < n'   0 8 0
sinir ofsetli_tam   'j < n'   3 5 0
sinir ust_tasan     'j < n'   1 8 hata
sinir esit_tasan    'j <= n'  0 8 hata
sinir alt_tasan     'j < n'  -1 4 hata

# Ic ice: sicak dongu ucuncu seviyede, dizi bir satir KISA (son satirda
# i * n + j sinirin disina tasar).
cat > "$TMP/icice.tpr" <<'TPREOF'
func f(float[] c, float[] b, int n) {
    for (int i = 0; i < n; i++) {
        for (int k = 0; k < n; k++) {
            for (int j = 0; j < n; j++) { c[i * n + j] = c[i * n + j] + b[k * n + j]; }
        }
    }
}
int n = 6;
float[] c = array_fill(n * n - 1, 0.0);
float[] b = array_fill(n * n, 1.0);
f(c, b, n);
print(c[0]);
TPREOF
out=$(derle icice)
if grep -qF '[fver] j' <<<"$out"; then
    out=$(cd "$TMP" && ./icice.out 2>&1); rc=$?
    if [ "$rc" -ne 0 ] && grep -qi "out of bounds" <<<"$out"; then
        gecti "icice: ucuncu seviyede sinir disi YAKALANDI (rc=$rc)"
    else dustu "icice: rc=$rc cikti='$out' (sinir disi yakalanmadi)"; fi
else
    dustu "icice: hizli surum kurulmadi"
fi

# ---- 3. ETKI (IR) -----------------------------------------------------------
cat > "$TMP/ir.tpr" <<'TPREOF'
func f(float[] c, float[] b, int n, float s) {
    for (int k = 0; k < n; k++) {
        for (int j = 0; j < n; j++) { c[k * n + j] = c[k * n + j] + s * b[j]; }
    }
}
int n = toInt(env("FD_N"));
if (n <= 0) { n = 16; }
float[] c = array_fill(n * n, 0.0);
float[] b = array_fill(n, 1.5);
f(c, b, n, 2.0);
print(toInt(c[n * n - 1]));
TPREOF
ir_var() {   # ir_var <cikti adi> <ek ortam> -> 0: hizli yukleme IR'de var
    rm -f "$TMP/$1.ll"
    (cd "$TMP" && env $2 TULPAR_AOT_NOCACHE=1 TULPAR_AOT_EMIT_LL=1 "$TUL" build ir.tpr "$1" >/dev/null 2>&1)
    grep -qE 'fv\.(el|ep)|wide\.load' "$TMP/$1.ll" 2>/dev/null
}
if ir_var ir_yok.out "TULPAR_NO_FVER=1"; then dustu "pozitif kontrol: TULPAR_NO_FVER=1 iken de fv.el var — kapi bir sey olcmuyor"
else gecti "pozitif kontrol: TULPAR_NO_FVER=1 iken hizli yukleme yok"; fi
if ir_var ir.out "FD_X=1"; then gecti "IR: hizli surumun double yuklemesi var"
else dustu "IR: hizli surum yuklemesi (fv.el) YOK — optimizasyon devrede degil"; fi
# Sonuc uc yolda da ayni: son satir c[k*n+j] tek kez s * b[j] = 2 * 1.5 = 3
# aliyor. Hizli surum / double depo kapali (genel surum) / surum kapali.
r1=$(cd "$TMP" && ./ir.out); r2=$(cd "$TMP" && TULPAR_NO_F64=1 ./ir.out)
r3=$(cd "$TMP" && ./ir_yok.out)
if [ "$r1" = "3" ] && [ "$r2" = "3" ] && [ "$r3" = "3" ]; then
    gecti "sonuc: hizli surum, kutulu depo ve surumsuz derleme ayni (3)"
else dustu "sonuc: hizli '$r1', kutulu depo '$r2', surumsuz '$r3' (beklenen 3)"; fi
# Bilgi: vektorlestirme (LLVM surumune/hedefe bagli — kapi DEGIL).
if grep -qE '<[0-9]+ x double>' "$TMP"/ir.out.ll 2>/dev/null; then echo "  bilgi  ic dongu vektorlesti"
else echo "  bilgi  ic dongu vektorlesmedi (LLVM/hedef)"; fi

if [ "$fail" -eq 0 ]; then echo "float_dizi: $n_gecti/$n_gecti"; fi
exit $fail
