#!/usr/bin/env bash
# INT YEREL GOLGE SURUMU + INT DIZI DONGU SURUMU KAPISI (2026-10-01).
#
# Fonksiyon icindeki kutulu `int` yereller en dis uygun dongude native i64
# golgeye iniyor (dongu basinda etiket + `int[]` depo sinavi, tutmazsa bugunku
# kod); ic ice dongunun en icteki `for`u `X[B + j]` erisimleriyle 32-bit depoya
# dogrudan iniyor (i32'ye sigmayan yazma genel surume geciyor). Dogruluk
# tests/int_golge.test.tpr'de; bu kapi dort seyi olcer:
#
#   1. KARAR: surum kuruluyor mu / kurulmamasi gereken yerde kurulmuyor mu
#      (TULPAR_DBG_VER: "[iver]" / "[iver-yok]" / "[iaver] j" / "[iaver-yok]").
#   2. SINIR: hizli kopyasi OLAN dongude sinir disi erisim hala YAKALANIYOR;
#      yumusak kipte (TULPAR_SOFT_RUNTIME=1) cikti surumsuz derlemeyle ayni.
#   3. ETKI (IR): hizli kopya IR'de gercekten var (`iver_fast`, `iav.el`) ve
#      TULPAR_NO_IVER=1 / TULPAR_NO_IAVER=1 ile YOK — kapinin kendisinin
#      olctugunun pozitif kontrolu.
#   4. FARK: ayni program uc kipte (varsayilan / golgesiz / int dizi surumsuz)
#      ayni ciktiyi veriyor.
#
#   tests/int_golge.sh [tulpar_yolu]
set -uo pipefail
TUL="${1:-./tulpar}"
[ -x "$TUL" ] || { echo "HATA: '$TUL' calistirilabilir degil" >&2; exit 1; }
TUL="$(cd "$(dirname "$TUL")" && pwd)/$(basename "$TUL")"
ROOT="$(pwd)"
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
export LC_ALL=C
fail=0
n_gecti=0
gecti() { n_gecti=$((n_gecti + 1)); printf "  \033[0;32mgecti\033[0m  %s\n" "$1"; }
dustu() { printf "  \033[0;31mDUSTU\033[0m  %s\n" "$1"; fail=1; }

derle() {   # derle <ad> [ek ortam] -> derleyici cikisi (TULPAR_DBG_VER)
    (cd "$TMP" && env ${2:-} TULPAR_DBG_VER=1 TULPAR_AOT_NOCACHE=1 "$TUL" build "$1.tpr" "$1.out" 2>&1)
}

# ---- 1. KARAR ---------------------------------------------------------------
# karar <ad> <grep -E deseni> <beklenen: var|yok> <aciklama>; kaynak $TMP/<ad>.tpr
# KARAR_ENV: derlemeye eklenen ortam (bos = varsayilan).
karar() {
    local ad="$1" desen="$2" bekle="$3" ne="$4" out var=yok
    out=$(derle "$ad" "${KARAR_ENV:-}")
    grep -qE "$desen" <<<"$out" && var=var
    if [ "$var" = "$bekle" ]; then gecti "$ad: $ne"
    else dustu "$ad: '$desen' $var, beklenen $bekle — $ne"; echo "$out" | sed 's/^/         /'; fi
}

cat > "$TMP/qs.tpr" <<'TPREOF'
func qs(int[] a, int lo, int hi) {
    int i = lo;
    int j = hi;
    int p = a[(lo + hi) / 2];
    while (i <= j) {
        while (a[i] < p) { i = i + 1; }
        while (a[j] > p) { j = j - 1; }
        if (i <= j) { int t = a[i]; a[i] = a[j]; a[j] = t; i = i + 1; j = j - 1; }
    }
    if (lo < j) { qs(a, lo, j); }
    if (i < hi) { qs(a, i, hi); }
}
int n = toInt(env("IG_N"));
if (n <= 0) { n = 200; }
int[] a = array_fill(n, 0);
int seed = 42;
for (int i = 0; i < n; i = i + 1) { seed = (seed * 48271) % 2147483647; a[i] = seed % 1000; }
qs(a, 0, n - 1);
int bad = 0;
for (int i = 1; i < n; i = i + 1) { if (a[i - 1] > a[i]) { bad = bad + 1; } }
print(toString(a[0]) + " " + toString(a[n - 1]) + " " + toString(bad));
TPREOF
karar qs '^\[iver\] satir 5: 3 golge, 1 bildirim, [0-9]+ okuma, 1 dizi' var \
    "qsort'un bolme dongusu golgeleniyor (i, j, p + t, a okumalari)"

cat > "$TMP/mm.tpr" <<'TPREOF'
func carp(int[] a, int[] b, int[] c, int n) {
    for (int i = 0; i < n; i = i + 1) {
        for (int k = 0; k < n; k = k + 1) {
            int av = a[i * n + k];
            for (int j = 0; j < n; j = j + 1) { c[i * n + j] = c[i * n + j] + av * b[k * n + j]; }
        }
    }
}
int n = toInt(env("IG_N"));
if (n <= 0) { n = 12; }
int m = toInt(env("IG_M"));
if (m <= 0) { m = 1; }
int[] a = array_fill(n * n, 0);
int[] b = array_fill(n * n, 0);
int[] c = array_fill(n * n - toInt(env("IG_KISA")), 0);
for (int i = 0; i < n * n; i = i + 1) { a[i] = mod(i * 7, 11) * m; b[i] = mod(i * 3, 13) * m - 5; }
carp(a, b, c, n);
int t = 0;
for (int i = 0; i < len(c); i = i + 1) { t = t + c[i] * mod(i, 7); }
print(t);
TPREOF
karar mm '^\[iaver\] j: 2 dizi, 3 erisim' var "matmul'un ic dongusu int dizi surumunde"
karar mm '^\[iavb\] j: aralik kaniti' var "matmul'un ic dongusu aralik kanitli (sinavsiz) govdeyi aliyor"
# `n` kutulu `int` parametre: golge YEREL GIRIS KAPALIYKEN olculur. Acikken
# (2026-10-06) `carp`in govdesi `t_carp.n`de, `n` orada zaten native — golge
# gereksiz, `.f` (yalniz INT olmayan argumanda kosan) surumsuz.
KARAR_ENV=TULPAR_NO_YEREL_GIRIS=1 karar mm '^\[iver\] satir 2: 1 golge, 4 bildirim' var \
    "matmul'un dis dongusu golgeleniyor (n + i, k, av, j; yerel giris kapali)"
karar mm '^\[iver\] satir 2: 0 golge, 4 bildirim' var \
    "yerel giriste n native: dis dongu golgesiz (i, k, av, j)"

# Kapanis iceren dongu: kanit reddediyor.
cat > "$TMP/kapanis.tpr" <<'TPREOF'
func f(int[] a, int n): int {
    int s = 0;
    for (int j = 0; j < n; j++) { var g = (x) => x + 1; s = s + g(a[j]); }
    return s;
}
int[] a = array_fill(4, 1);
print(f(a, 4));
TPREOF
karar kapanis '^\[iver\]' yok "kapanisli dongu golgelenmiyor"

# Int dizi surumu: iki eleman yazmasi / yazma ilk deyim degil -> surum yok.
cat > "$TMP/iki_yazma.tpr" <<'TPREOF'
func f(int[] a, int[] b, int n) {
    for (int i = 0; i < 2; i++) {
        for (int j = 0; j < n; j++) { a[j] = b[j] + 1; b[j] = a[j] * 2; }
    }
}
int[] a = array_fill(8, 1);
int[] b = array_fill(8, 2);
f(a, b, 8);
print(a[7] + b[7]);
TPREOF
karar iki_yazma '^\[iaver-yok\] j: birden cok eleman yazmasi' var "iki eleman yazmasi: int dizi surumu yok"
cat > "$TMP/ilk_degil.tpr" <<'TPREOF'
func f(int[] a, int[] b, int n): int {
    int s = 0;
    for (int i = 0; i < 2; i++) {
        for (int j = 0; j < n; j++) { if (j > n) { s = 1; } a[j] = b[j] + 1; }
    }
    return s;
}
int[] a = array_fill(8, 1);
int[] b = array_fill(8, 2);
print(f(a, b, 8) + a[7]);
TPREOF
karar ilk_degil '^\[iaver-yok\] j: eleman yazmasi govdenin ilk' var "yazma ilk deyim degil: int dizi surumu yok"
# En dis seviyedeki tek tamsayi dongusu `for` surumlemesinde (K201/K215) kalir.
cat > "$TMP/dis.tpr" <<'TPREOF'
func f(int[] a, int[] b, int n) {
    for (int j = 0; j < n; j++) { a[j] = a[j] + b[j]; }
}
int[] a = array_fill(8, 1);
int[] b = array_fill(8, 2);
f(a, b, 8);
print(a[7]);
TPREOF
karar dis '^\[iaver\]' yok "en dis seviyede int dizi surumu yok"

# ---- 2. SINIR ---------------------------------------------------------------
# Golge kopyasi OLAN dongude son tur a[len] okuyor.
cat > "$TMP/sinir.tpr" <<'TPREOF'
func topla(int[] a, int k): int {
    int i = 0;
    int s = 0;
    while (i <= k) { s = s + a[i]; i = i + 1; }
    return s;
}
int[] a = array_fill(6, 2);
print(topla(a, toInt(env("IG_K"))));
print("sonra");
TPREOF
out=$(derle sinir)
if grep -qE '^\[iver\] satir 4: [0-9]+ golge, 0 bildirim, 1 okuma' <<<"$out"; then
    # Windows ciktisi CRLF: satir sonu karsilastirmadan once ayiklaniyor.
    r=$(cd "$TMP" && IG_K=5 ./sinir.out 2>&1); rc=$?
    r=$(printf '%s' "$r" | tr -d '\r')
    if [ "$rc" -eq 0 ] && [ "$r" = "$(printf '12\nsonra')" ]; then gecti "sinir: tam sinirda calisti (12)"
    else dustu "sinir: IG_K=5 rc=$rc cikti='$r' (beklenen 12)"; fi
    r=$(cd "$TMP" && IG_K=6 ./sinir.out 2>&1); rc=$?
    if [ "$rc" -ne 0 ] && grep -qi "out of bounds" <<<"$r"; then
        gecti "sinir: golge kopyasinda sinir disi YAKALANDI (rc=$rc)"
    else dustu "sinir: IG_K=6 rc=$rc cikti='$r' (sinir disi yakalanmadi)"; fi
    # Yumusak kip: tani + 0 ikame + devam — surumsuz derlemeyle AYNI cikti.
    cp "$TMP/sinir.tpr" "$TMP/sinir_yok.tpr"
    (cd "$TMP" && TULPAR_NO_IVER=1 TULPAR_AOT_NOCACHE=1 "$TUL" build sinir_yok.tpr sinir_yok.out >/dev/null 2>&1)
    r1=$(cd "$TMP" && IG_K=6 TULPAR_SOFT_RUNTIME=1 ./sinir.out 2>/dev/null)
    r2=$(cd "$TMP" && IG_K=6 TULPAR_SOFT_RUNTIME=1 ./sinir_yok.out 2>/dev/null)
    if [ -n "$r1" ] && [ "$r1" = "$r2" ]; then gecti "yumusak kip: golgeli ve golgesiz ayni ('$(echo $r1)')"
    else dustu "yumusak kip: golgeli '$r1' / golgesiz '$r2'"; fi
else
    dustu "sinir: golge kurulmadi — sinir sinavi olculemez"; echo "$out" | sed 's/^/         /'
fi

# Int dizi surumu: c bir eleman KISA -> son satirin son yazmasi sinir disi.
# Dongu basindaki sinir sinavi tutmuyor, genel surum hatayi veriyor.
r=$(cd "$TMP" && IG_KISA=1 ./mm.out 2>&1); rc=$?
if [ "$rc" -ne 0 ] && grep -qi "out of bounds" <<<"$r"; then
    gecti "int dizi surumu: ic dongude sinir disi YAKALANDI (rc=$rc)"
else dustu "int dizi surumu: IG_KISA=1 rc=$rc cikti='$r' (sinir disi yakalanmadi)"; fi

# ---- 3. ETKI (IR) -----------------------------------------------------------
ir_var() {   # ir_var <kaynak> <cikti> <desen> <ek ortam> -> 0: desen IR'de var
    # Iyilestirme ONCESI (.pre.ll: blok adlari duruyor) ve SONRASI (.ll: deger
    # adlari duruyor) IR birlikte aranir.
    rm -f "$TMP/$2.ll" "$TMP/$2.pre.ll"
    (cd "$TMP" && env $4 TULPAR_AOT_NOCACHE=1 TULPAR_AOT_EMIT_LL=1 TULPAR_AOT_EMIT_LL_PRE=1 \
        "$TUL" build "$1.tpr" "$2" >/dev/null 2>&1)
    # Boru YOK: `cat a b | grep -q` pipefail altinda, grep erken cikip cat
    # SIGPIPE (141) alinca eslesme varken de basarisiz donuyordu — CI'da
    # uc platformda da (yerelde zamanlama sansiyla yesil).
    grep -qE "$3" "$TMP/$2.ll" "$TMP/$2.pre.ll" 2>/dev/null
}
if ir_var qs qs_yok.out 'iver_fast' "TULPAR_NO_IVER=1"; then
    dustu "pozitif kontrol: TULPAR_NO_IVER=1 iken de iver_fast var — kapi bir sey olcmuyor"
else gecti "pozitif kontrol: TULPAR_NO_IVER=1 iken golge kopyasi yok"; fi
if ir_var qs qs_ir.out 'iver_fast' "IG_X=1"; then gecti "IR: golge kopyasi (iver_fast) var"
else dustu "IR: iver_fast YOK — golge surumu devrede degil"; fi
if ir_var mm mm_yok.out 'iav\.el' "TULPAR_NO_IAVER=1"; then
    dustu "pozitif kontrol: TULPAR_NO_IAVER=1 iken de iav.el var — kapi bir sey olcmuyor"
else gecti "pozitif kontrol: TULPAR_NO_IAVER=1 iken int dizi yuklemesi yok"; fi
if ir_var mm mm_ir.out 'iav\.el' "IG_X=1"; then gecti "IR: int dizi surumunun 32-bit yuklemesi (iav.el) var"
else dustu "IR: iav.el YOK — int dizi surumu devrede degil"; fi

# ARALIK KANITI (iavb, 2026-10-06): sinavsiz govde IR'de var / kapaliyken yok.
if ir_var mm mm_b_yok.out 'iavb_fast' "TULPAR_NO_IAVB=1"; then
    dustu "pozitif kontrol: TULPAR_NO_IAVB=1 iken de iavb_fast var — kapi bir sey olcmuyor"
else gecti "pozitif kontrol: TULPAR_NO_IAVB=1 iken aralik kanitli govde yok"; fi
if ir_var mm mm_b_ir.out 'iavb_fast' "IG_X=1" && ir_var mm mm_b_ir.out 'iavb\.tara' "IG_X=1"; then
    gecti "IR: aralik kanitli govde (iavb_fast) + OR taramasi (iavb.tara) var"
else dustu "IR: iavb_fast / iavb.tara YOK — aralik kaniti devrede degil"; fi

# ---- 4. FARK ----------------------------------------------------------------
# Uc kip ayni cikti: varsayilan / golgesiz / int dizi surumsuz. matmul ayrica
# i32'yi asan degerlerle (IG_M): hizli govdedeki yazma sigmiyor -> deopt.
for prog in qs mm; do
    (cd "$TMP" && TULPAR_NO_IVER=1 TULPAR_AOT_NOCACHE=1 "$TUL" build $prog.tpr ${prog}_g.out >/dev/null 2>&1)
    (cd "$TMP" && TULPAR_NO_IAVER=1 TULPAR_AOT_NOCACHE=1 "$TUL" build $prog.tpr ${prog}_d.out >/dev/null 2>&1)
    (cd "$TMP" && TULPAR_NO_IAVB=1 TULPAR_AOT_NOCACHE=1 "$TUL" build $prog.tpr ${prog}_b.out >/dev/null 2>&1)
done
for cfg in "qs IG_N=200" "qs IG_N=5000" "mm IG_N=12" "mm IG_N=9 IG_M=100000"; do
    set -- $cfg; prog=$1; shift
    r0=$(cd "$TMP" && env "$@" ./$prog.out 2>&1)
    r1=$(cd "$TMP" && env "$@" ./${prog}_g.out 2>&1)
    r2=$(cd "$TMP" && env "$@" ./${prog}_d.out 2>&1)
    r3=$(cd "$TMP" && env "$@" ./${prog}_b.out 2>&1)
    if [ -n "$r0" ] && [ "$r0" = "$r1" ] && [ "$r0" = "$r2" ] && [ "$r0" = "$r3" ]; then
        gecti "fark: $cfg -> '$r0' (dort kip ayni)"
    else dustu "fark: $cfg -> varsayilan '$r0' / golgesiz '$r1' / int dizi surumsuz '$r2' / aralik kanitsiz '$r3'"; fi
done

# ---- 5. ARALIK KANITI: anlam + sabotaj --------------------------------------
# tests/int_aralik.test.tpr her senaryoyu surumsuz ikizle eleman eleman
# karsilastiriyor; TULPAR_IAVB_SINAMA=sinir esigi 62'ye cikarir (sigmayan deger
# sinavsiz yazilir) — paket KIRMIZIYA donmeli.
ia() { (cd "$ROOT" && env TULPAR_AOT_NOCACHE=1 "$@" "$TUL" tests/int_aralik.test.tpr 2>&1); }
o=$(ia); rc=$?
if [ "$rc" -eq 0 ] && grep -q "Fail: 0" <<<"$o"; then gecti "int_aralik.test.tpr gecti ($(grep -o 'Tests: [0-9]*' <<<"$o"))"
else dustu "int_aralik.test.tpr rc=$rc: $(tail -3 <<<"$o")"; fi
o=$(ia TULPAR_NO_IAVB=1); rc=$?
if [ "$rc" -eq 0 ] && grep -q "Fail: 0" <<<"$o"; then gecti "int_aralik.test.tpr aralik kaniti KAPALIYKEN de geciyor"
else dustu "int_aralik.test.tpr (TULPAR_NO_IAVB=1) rc=$rc: $(tail -3 <<<"$o")"; fi
o=$(ia TULPAR_IAVB_SINAMA=sinir); rc=$?
if [ "$rc" -ne 0 ] && ! grep -q "Fail: 0" <<<"$o"; then
    gecti "sabotaj (esik 62) int_aralik.test.tpr'yi kirmiziya ceviriyor ($(grep -c 'FAIL' <<<"$o") test)"
else dustu "sabotaj YAKALANMADI — anlam testi aralik kanitini olcmuyor"; fi

if [ "$fail" -eq 0 ]; then echo "int_golge: $n_gecti/$n_gecti"; fi
exit $fail
