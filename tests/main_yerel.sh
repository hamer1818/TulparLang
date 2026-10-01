#!/bin/bash
# UST DUZEY MAIN-YERELI + STRUCT DIZISI SEKIL ONBELLEGI — YAPI KAPISI
# (2026-10-01).
#
# Anlam testleri tests/main_yerel.test.tpr'de. Bu betik KARARLARIN kendisini
# optimizasyon ONCESI IR'dan okuyor — ikisi de yalniz hiz degistirir, sonucu
# degil; bozulduklarinda anlam paketi YESIL kalir:
#   1) yalniz main'de gorulen ust duzey ad LLVM global'i OLMAMALI
#      (`@tpr_g_<ad>` yok); fonksiyonda / lambdada / try govdesinde gecen
#      ad global KALMALI. `int` olculmus kuralla global kalir (elek LSR
#      gerilemesi), TULPAR_MAIN_LOCALS_ALL=1 onu da terfi ettirir.
#   2) sekli degismeyen dongude struct dizisi basligi dongu basinda okunmali
#      (`sarrc.` bloklari); govdesi push eden dongude OKUNMAMALI.
#   3) struct dizisi alan saklamasi TBAA etiketli olmali.
#   4) onbellekli dongude sinir disi erisim hala calisma zamani hatasi.
#
# POZITIF KONTROL: TULPAR_NO_MAIN_LOCALS=1 / TULPAR_NO_SARR_CACHE=1 iki
# optimizasyonu kapatir; o derlemelerde 1) ve 2) tersine donmeli — donmuyorsa
# denetim kordur.
#
# Enjeksiyonla dogrulandi (2026-10-01):
#   * tarama (ml_scan_main) AST_LAMBDA'yi atlayacak bicimde bozulunca lambda
#     terfi edilmis yuvaya eristi; guvenlik agi (main_local_foreign_use)
#     "ic hata: ... baska bir fonksiyondan erisiliyor" ile derlemeyi DURDURDU
#     — bu kapi ve tests/main_yerel.test.tpr kirmizi (gecersiz IR yok).
#   * SarrCacheScope'tan yeniden baglama denetimi kaldirilinca anlam
#     paketinin "dizi dongude yeniden baglanir" testi kirmizi (19 yerine 3).
# Eski derleyiciyle (iki optimizasyon da yok): 12/17 — 1), 2) ve 3)'un
# olumlu yonu (ve TULPAR_MAIN_LOCALS_ALL) dusuyor, pozitif kontroller ve
# anlam satirlari geciyor.
set -u
cd "$(dirname "$0")/.."
TULPAR="${1:-./tulpar}"
[ -x "$TULPAR" ] || { echo "main yerel kapisi: $TULPAR yok — atlandi"; exit 0; }
TULPAR="$(cd "$(dirname "$TULPAR")" && pwd)/$(basename "$TULPAR")"

TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
PASS=0
FAIL=0
ok()  { echo "  gecti  $1"; PASS=$((PASS + 1)); }
bad() { echo "  DUSTU  $1"; FAIL=$((FAIL + 1)); }

cat > "$TMP/m.tpr" <<'EOF'
struct P { float x; float vx; }
float yalniz = 0.5;
int yalniz_int = 2;
float paylasilan = 1.0;
int lam_ad = 0;
int try_ad = 0;
P[] ps = [];
func oku(): float { return paylasilan; }
func buyut(int n): float {
    P[] q = [{ x: 1.0, vx: 0.0 }];
    float t = 0.0;
    for (int i = 0; i < n; i = i + 1) { push(q, { x: 2.0, vx: 0.0 }); t = t + q[i].x; }
    return t;
}
var f = () => lam_ad + 1;
try { try_ad = 1; } catch (e) { }
for (int i = 0; i < 100; i = i + 1) { push(ps, { x: toFloat(i), vx: 1.0 }); }
for (int s = 0; s < 4; s = s + 1) {
    for (int i = 0; i < len(ps); i = i + 1) { ps[i].x = ps[i].x + ps[i].vx * yalniz; }
}
float t = 0.0;
for (int i = 0; i < len(ps); i = i + 1) { t = t + ps[i].x; }
for (int i = 0; i < 3; i = i + 1) { yalniz_int = yalniz_int + i; }
print(toInt(t), toInt(oku()), f(), try_ad, toInt(buyut(5)), yalniz_int);
EOF

derle() {  # derle <cikti_adi> [ortam...]
  local out="$1"; shift
  (cd "$TMP" && env "$@" TULPAR_AOT_NOCACHE=1 TULPAR_AOT_EMIT_LL_PRE=1 "$TULPAR" build m.tpr "$out" >"$out.log" 2>&1)
}
# `<ad>` fonksiyonunun on-optimizasyon govdesi (main ya da kutulu t_<ad>).
fn_body() {
  awk -v f="$2" '
    /^define/ { on = ($0 ~ ("@(t_)?" f "(\\.f)?\\(")) }
    on { print }
    on && /^}/ { on = 0 }' "$1"
}
has_global() { grep -q "^@tpr_g_$2 = " "$1"; }

if ! derle yeni; then
  echo "main yerel kapisi DUSTU: program derlenemedi"; sed -n '1,15p' "$TMP/yeni.log"; exit 1
fi
out=$("$TMP/yeni")
[ "$out" = "5150 1 1 1 9 5" ] && ok "program dogru sonuc veriyor (5150 1 1 1 9 5)" \
  || bad "program sonucu: '$out' (5150 1 1 1 9 5 bekleniyordu)"
LL="$TMP/yeni.pre.ll"

# 1) terfi karari
has_global "$LL" yalniz && bad "yalniz main'de gorulen 'yalniz' hala global" \
  || ok "yalniz main'de gorulen float main yereli (global yok)"
has_global "$LL" ps && bad "yalniz main'de gorulen 'ps' (P[]) hala global" \
  || ok "yalniz main'de gorulen struct dizisi main yereli"
# `int` bilerek global (elek olcumu, llvm_backend.cpp main_local_declare);
# TULPAR_MAIN_LOCALS_ALL=1 onu da terfi ettirir — kural gercekten tipten mi?
has_global "$LL" yalniz_int && ok "int ust duzey global kaliyor (olculmus kural)" \
  || bad "int ust duzey terfi edildi — elek LSR gerilemesi (%22) geri gelir"
if derle hepsi TULPAR_MAIN_LOCALS_ALL=1; then
  has_global "$TMP/hepsi.pre.ll" yalniz_int && bad "TULPAR_MAIN_LOCALS_ALL=1 ile de int global" \
    || ok "TULPAR_MAIN_LOCALS_ALL=1: int de terfi ediliyor"
  [ "$("$TMP/hepsi")" = "5150 1 1 1 9 5" ] && ok "TULPAR_MAIN_LOCALS_ALL=1 ayni sonuc" \
    || bad "TULPAR_MAIN_LOCALS_ALL=1 farkli sonuc: '$("$TMP/hepsi")'"
else
  bad "TULPAR_MAIN_LOCALS_ALL=1 derlemesi basarisiz"
fi
has_global "$LL" paylasilan && ok "fonksiyonda okunan ad global kaliyor" \
  || bad "fonksiyonda okunan 'paylasilan' global DEGIL (fonksiyon neyi okuyor?)"
has_global "$LL" lam_ad && ok "lambda adi global kaliyor" \
  || bad "lambdada okunan 'lam_ad' global DEGIL"
has_global "$LL" try_ad && ok "try govdesindeki ad global kaliyor (setjmp)" \
  || bad "try govdesinde yazilan 'try_ad' global DEGIL — longjmp sonrasi bayat deger riski"

# 2) struct dizisi sekil onbellegi
n_main=$(fn_body "$LL" main | grep -c '^sarrc\.ty')
n_buyut=$(fn_body "$LL" buyut | grep -c '^sarrc\.ty')
[ "$n_main" -ge 1 ] && ok "sekli degismeyen dongude baslik dongu basinda okunuyor ($n_main)" \
  || bad "sekli degismeyen dongude sarrc. blogu yok — onbellek kurulmadi"
[ "$n_buyut" -eq 0 ] && ok "push eden dongude onbellek yok" \
  || bad "push eden dongude onbellek KURULDU ($n_buyut) — bayat count/data"

# 3) TBAA: alan saklamasi etiketli
if grep -Eq 'store double .*%sarr\.set\.field[0-9]*, align 8, !tbaa' "$LL"; then
  ok "struct dizisi alan saklamasi TBAA etiketli"
else
  bad "struct dizisi alan saklamasinda !tbaa yok — baslik her erisimde yeniden okunur"
fi

# Pozitif kontroller
if derle kapali TULPAR_NO_MAIN_LOCALS=1 TULPAR_NO_SARR_CACHE=1; then
  KL="$TMP/kapali.pre.ll"
  has_global "$KL" yalniz && ok "pozitif kontrol: terfi kapaliyken 'yalniz' global" \
    || bad "pozitif kontrol: terfi kapaliyken de global yok — denetim kor"
  n_k=$(grep -c '^sarrc\.ty' "$KL")
  [ "$n_k" -eq 0 ] && ok "pozitif kontrol: onbellek kapaliyken sarrc. yok" \
    || bad "pozitif kontrol: onbellek kapaliyken sarrc. var ($n_k) — denetim kor"
  [ "$("$TMP/kapali")" = "$out" ] && ok "kapali derleme ayni sonucu veriyor" \
    || bad "kapali derleme farkli sonuc: '$("$TMP/kapali")'"
else
  bad "pozitif kontrol derlemesi basarisiz"
fi

# 4) onbellekli (ust duzey, terfi edilmis) dongude sinir disi
cat > "$TMP/s.tpr" <<'EOF'
struct P { float x; float vx; }
P[] ps = [{ x: 0.0, vx: 0.0 }, { x: 0.0, vx: 0.0 }];
for (int i = 0; i <= len(ps); i = i + 1) { ps[i].x = 1.0; }
print("sonra", toInt(ps[0].x + ps[1].x));
EOF
if (cd "$TMP" && TULPAR_AOT_NOCACHE=1 "$TULPAR" build s.tpr s >s.log 2>&1); then
  serr=$("$TMP/s" 2>&1); src=$?
  if [ $src -ne 0 ] && echo "$serr" | grep -qiE 'sinir disinda|out of bounds'; then
    ok "sinir disi: calisma zamani hatasi (cikis $src)"
  else
    bad "sinir disi: hata yok (cikis $src): $serr"
  fi
  sout=$(TULPAR_SOFT_RUNTIME=1 "$TMP/s" 2>/dev/null); s2=$?
  [ $s2 -eq 0 ] && [ "$sout" = "sonra 2" ] && ok "yumusak kip: tani + devam (sonra 2)" \
    || bad "yumusak kip: cikis $s2, cikti '$sout' (sonra 2 bekleniyordu)"
else
  bad "sinir disi programi derlenemedi"
fi

echo "main_yerel: $PASS/$((PASS + FAIL))"
[ $FAIL -eq 0 ] || exit 1
exit 0
