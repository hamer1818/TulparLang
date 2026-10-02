#!/usr/bin/env bash
# STRUCT DIZISI DONGU SURUMU KAPISI (2026-10-02).
#
# `for (i = E; i < UB; i = i + K)` govdesindeki `A[i]` (A tipli struct dizisi)
# erisimleri SURUMLENIR: dongu basinda i >= 0 ve UB' <= count(A) bir kez
# sinanir, hizli govdede erisim tek GEP (sinav da runtime yavas yolu da yok),
# kosul dongu basindaki UB ile. Ayni PR'da `push(d, e)` (yer varsa) ve
# `toFloat` (INT/FLOAT) satir ici. Dogruluk tests/struct_dizi_surum.test.tpr'de;
# bu kapi uc seyi olcer:
#
#   1. KARAR: surum kuruluyor mu / kurulmamasi gereken yerde kurulmuyor mu
#      (TULPAR_DBG_VER cikisi: "[sver] i"). Iki yon de.
#   2. SINIR: hizli surumu OLAN dongude sinir disi erisim hala YAKALANIYOR
#      (cikis != 0 + "out of bounds"). Dongu basindaki sinav tutmayinca genel
#      (bekcili) govdeye dusulmeli — dusulmezse a[len] sessizce okunur.
#   3. ETKI: hizli yol IR'de gercekten var (`sv.ep`, `spush.fast`, `tof.i2f`)
#      ve kapatma anahtarlariyla (TULPAR_NO_SVER / TULPAR_NO_SPUSH_INLINE) YOK
#      — kapinin kendisinin olctugunun pozitif kontrolu; uc derlemenin ciktisi
#      ayni.
#
#   tests/struct_dizi_surum.sh [tulpar_yolu]
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
    (cd "$TMP" && TULPAR_DBG_VER=1 TULPAR_AOT_NOCACHE=1 "$TUL" build "$1.tpr" "$1.out" 2>&1 | tr -d '\r')
}
# `grep -q` boruyu erken kapatir; pipefail altinda yazan taraf SIGPIPE ile
# duser ve "bulundu" "bulunamadi"ya donerdi. Cikti degiskende, here-string ile.
var_mi() { grep -qF -- "$2" <<< "$1"; }

STRUCT='struct P { float x; float y; float vx; float vy; }'

# ---- 1. KARAR ---------------------------------------------------------------
# karar <ad> <beklenen: evet|hayir> <ic govde>
karar() {
    local ad="$1" bekle="$2" govde="$3"
    printf '%s\nfunc g(int v): int { return v + 1; }\nfunc f(P[] a, P[] b, int n) {\n    for (int i = 0; i < n; i = i + 1) { %s }\n}\nP[] a = [];\nP[] b = [];\nfor (int k = 0; k < 16; k = k + 1) { push(a, { x: 1.0, y: 2.0, vx: 0.5, vy: 0.25 }); push(b, { x: 3.0, y: 4.0, vx: 0.5, vy: 0.25 }); }\nf(a, b, 8);\nprint(a[3].x);\n' \
        "$STRUCT" "$govde" > "$TMP/$ad.tpr"
    local out var=hayir
    out=$(derle "$ad")
    var_mi "$out" '[sver] i' && var=evet
    if [ "$var" = "$bekle" ]; then gecti "$ad: surum $bekle"
    else dustu "$ad: surum '$var', beklenen '$bekle' — $govde"; echo "$out" | sed 's/^/         /'; fi
}

karar alan_yaz      evet  'a[i].x = a[i].x + a[i].vx;'
karar iki_dizi      evet  'a[i].x = b[i].y * 2.0;'
karar bilesik       evet  'a[i].x += 1.0; a[i].y *= 2.0;'
karar kosullu       evet  'if (a[i].x > 0.5) { a[i].vx = 0.0 - a[i].vx; }'
# Sekli degistiren cagri (push) / kullanici fonksiyonu: surum YOK.
karar push_var      hayir 'a[i].x = 1.0; push(b, { x: 1.0, y: 1.0, vx: 1.0, vy: 1.0 });'
karar kullanici     hayir 'a[i].x = toFloat(g(i));'
# Dongu degiskeni govdede ataniyor / artiriliyor (`i++` de sayilir).
karar ivar_atama    hayir 'a[i].x = 1.0; i = i + 1;'
karar ivar_artim    hayir 'i++; a[i].x = 1.0;'
# Sinir govdede degisiyor.
karar sinir_atama   hayir 'a[i].x = 1.0; n = n - 1;'
# `A[i]` bicimi yok (indeks bir kopya): plan aramaz.
karar kopya_indeks  hayir 'int k = i; a[k].x = 1.0;'
# Afin indeks (`i + 1`) bu surumun kapsaminda degil: surum acilir ama o
# erisim bekcili kalir — burada yalniz `a[i + 1]` var, surum YOK.
karar ofsetli       hayir 'a[i + 1].x = 1.0;'

# ---- 2. SINIR ---------------------------------------------------------------
# sinir <ad> <kosul> <baslangic> <n> <beklenen cikis: 0|hata>
sinir() {
    local ad="$1" kosul="$2" bas="$3" n="$4" cikis="$5"
    printf '%s\nfunc f(P[] a, int bas, int n): float {\n    float t = 0.0;\n    for (int i = bas; %s; i = i + 1) { t = t + a[i].x; a[i].y = t; }\n    return t;\n}\nP[] a = [];\nfor (int k = 0; k < 8; k = k + 1) { push(a, { x: 1.0, y: 0.0, vx: 0.0, vy: 0.0 }); }\nprint(f(a, %s, %s));\n' \
        "$STRUCT" "$kosul" "$bas" "$n" > "$TMP/$ad.tpr"
    local out rc var=hayir
    out=$(derle "$ad")
    var_mi "$out" '[sver] i' && var=evet
    # Sinir sinavinin anlami ancak hizli surum VARKEN var: once o.
    if [ "$var" != "evet" ]; then dustu "$ad: hizli surum kurulmadi — sinir sinavi olculemez"; return; fi
    out=$(cd "$TMP" && ./"$ad.out" 2>&1 | tr -d '\r'); rc=$?   # pipefail: tr degil programin cikisi
    if [ "$cikis" = "0" ] && [ "$rc" -eq 0 ]; then gecti "$ad: calisti ($out)"
    elif [ "$cikis" = "hata" ] && [ "$rc" -ne 0 ] && grep -qi "out of bounds\|sinir disinda" <<< "$out"; then
        gecti "$ad: sinir disi YAKALANDI (rc=$rc)"
    else dustu "$ad: rc=$rc cikti='$out' (beklenen $cikis)"; fi
}
sinir tam          'i < n'   0 8 0
sinir ofsetli_tam  'i < n'   3 8 0
sinir ust_tasan    'i < n'   0 9 hata
sinir esit_tasan   'i <= n'  0 8 hata
sinir alt_tasan    'i < n'  -1 4 hata

# Ust duzey: sinir global `n`, dizi bir eleman KISA; ic dongu dis donguyle
# ic ice (benchmark bicimi).
cat > "$TMP/ust.tpr" <<TPREOF
$STRUCT
int n = 6;
P[] ps = [];
for (int k = 0; k < n - 1; k = k + 1) { push(ps, { x: 1.0, y: 0.0, vx: 0.0, vy: 0.0 }); }
for (int s = 0; s < 2; s = s + 1) {
    for (int i = 0; i < n; i = i + 1) { ps[i].x = ps[i].x + 1.0; }
}
print(ps[0].x);
TPREOF
out=$(derle ust)
if var_mi "$out" '[sver] i'; then
    out=$(cd "$TMP" && ./ust.out 2>&1 | tr -d '\r'); rc=$?   # pipefail: tr degil programin cikisi
    if [ "$rc" -ne 0 ] && grep -qi "out of bounds\|sinir disinda" <<< "$out"; then
        gecti "ust duzey: global sinirli ic dongude sinir disi YAKALANDI (rc=$rc)"
    else dustu "ust duzey: rc=$rc cikti='$out' (sinir disi yakalanmadi)"; fi
else
    dustu "ust duzey: hizli surum kurulmadi"
fi

# ---- 3. ETKI (IR) -----------------------------------------------------------
cat > "$TMP/ir.tpr" <<TPREOF
$STRUCT
int n = toInt(env("SD_N"));
if (n <= 0) { n = 1000; }
P[] ps = [];
for (int i = 0; i < n; i = i + 1) {
    push(ps, { x: toFloat(i % 100) * 0.5, y: toFloat(i % 77) * 0.25, vx: toFloat((i % 13) - 6) * 0.1, vy: 0.5 });
}
for (int s = 0; s < 20; s = s + 1) {
    for (int i = 0; i < n; i = i + 1) {
        ps[i].vy = ps[i].vy - 0.0981;
        ps[i].x = ps[i].x + ps[i].vx * 0.01;
        ps[i].y = ps[i].y + ps[i].vy * 0.01;
        if (ps[i].y < 0.0) { ps[i].y = 0.0 - ps[i].y; ps[i].vy = (0.0 - ps[i].vy) * 0.9; }
    }
}
float sum = 0.0;
for (int i = 0; i < n; i = i + 1) { sum = sum + ps[i].x + ps[i].y; }
print(toInt(round(sum * 1000.0)));
TPREOF
ir_derle() {   # ir_derle <cikti adi> <ek ortam>
    rm -f "$TMP/$1.ll"
    (cd "$TMP" && env $2 TULPAR_AOT_NOCACHE=1 TULPAR_AOT_EMIT_LL=1 "$TUL" build ir.tpr "$1" >/dev/null 2>&1)
}
ir_var() { grep -qE -- "$2" "$TMP/$1.ll" 2>/dev/null; }   # ir_var <ad> <desen>
ir_derle ir.out "SD_X=1"
ir_derle ir_yok.out "TULPAR_NO_SVER=1 TULPAR_NO_SPUSH_INLINE=1"
if ir_var ir_yok.out 'sv\.ep|sv\.hic'; then dustu "pozitif kontrol: TULPAR_NO_SVER=1 iken de sv.ep var — kapi bir sey olcmuyor"
else gecti "pozitif kontrol: TULPAR_NO_SVER=1 iken kanitli erisim yok"; fi
if ir_var ir_yok.out 'spush\.fast'; then dustu "pozitif kontrol: TULPAR_NO_SPUSH_INLINE=1 iken de spush.fast var"
else gecti "pozitif kontrol: TULPAR_NO_SPUSH_INLINE=1 iken satir ici push yok"; fi
if ir_var ir.out 'sv\.ep'; then gecti "IR: struct dizisi surumunun kanitli erisimi (sv.ep) var"
else dustu "IR: sv.ep YOK — struct dizisi surumu devrede degil"; fi
if ir_var ir.out 'spush\.fast'; then gecti "IR: satir ici push (spush.fast) var"
else dustu "IR: spush.fast YOK — satir ici push devrede degil"; fi
if ir_var ir.out 'tof\.i2f'; then gecti "IR: satir ici toFloat (tof.i2f) var"
else dustu "IR: tof.i2f YOK — satir ici toFloat devrede degil"; fi
# Sonuc uc yolda da ayni: varsayilan, her sey kapali, yalniz surum kapali.
ir_derle ir_sv0.out "TULPAR_NO_SVER=1"
r1=$(cd "$TMP" && ./ir.out | tr -d '\r'); r2=$(cd "$TMP" && ./ir_yok.out | tr -d '\r')
r3=$(cd "$TMP" && ./ir_sv0.out | tr -d '\r')
if [ -n "$r1" ] && [ "$r1" = "$r2" ] && [ "$r1" = "$r3" ]; then
    gecti "sonuc: varsayilan, hepsi kapali ve surum kapali derleme ayni ($r1)"
else dustu "sonuc: varsayilan '$r1', hepsi kapali '$r2', surum kapali '$r3'"; fi

if [ "$fail" -eq 0 ]; then echo "struct_dizi_surum: $n_gecti/$n_gecti"; fi
exit $fail
