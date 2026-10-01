#!/bin/bash
# FLOAT DIZI IC ICE SURUM — YAPI KAPISI (2026-10-01).
#
# nbody'nin `advance`'inde i dongusu icindeki 0-4 turluk j dongusu, en ic
# dongu surumunun (fv_try_version) sinavini HER girisinde yapiyordu: yedi
# dizinin deposu + ~20 erisimin araligi, ustune int sekil onbelleginin dort
# baslik okumasi. Artik (1) sinav dis dongu basinda bir kez (fvn_try_version,
# uc nokta sinavi) ve hizli dis govdede ic donguler sinavsiz/genel kopyasiz;
# (2) int sekil onbellegi float surumlu donguede yalniz GENEL govdede
# kuruluyor. nbody 187,6 -> 114,5 ms (C 115,0; Ryzen 7 9800X3D, 2026-10-01).
#
# Anlam paketi tests/float_ic_ice.test.tpr (sinir disi hala hata, uc nokta
# tutmazsa genel yol). Bu kapi KARARI on-optimizasyon IR'indan okuyor:
#   1) nbody bicimli dongude `fvn_fast` ve sinavsiz ic dongu (`fvn_ic_done`)
#      var; sonuc kapali derlemeyle AYNI.
#   2) int sekil onbellegi (`shape.ty`) surum dallanmasindan ONCE yok —
#      yalniz genel govdede.
#   3) dis govdesinde ic dongu disinda dizi erisimi olan dongu (matmul'un k
#      dongusu bicimi) ic ice surumlenMIYOR.
# POZITIF KONTROL: TULPAR_NO_FVNEST=1 -> 1)'in iki blogu da yok.
#
# Sabotajla dogrulandi (2026-10-01):
#   * j'li erisimin alt sinir sinavi (0 <= B + J0(I0)) kaldirilinca anlam
#     paketinin "J0 = i - 1" testi kirmizi (a[-1] sessizce okundu/yazildi).
#   * j'siz `i + c` erisimin ust siniri I0 ile (UBo - 1 yerine) sinaninca
#     "i + 1 tasiyor" testi kirmizi.
#   * int sekil onbellegi dongu basina geri alininca 2) kirmizi.
set -u
cd "$(dirname "$0")/.."
TULPAR="${1:-./tulpar}"
[ -x "$TULPAR" ] || { echo "float ic ice kapisi: $TULPAR yok — atlandi"; exit 0; }
TULPAR="$(cd "$(dirname "$TULPAR")" && pwd)/$(basename "$TULPAR")"

TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
PASS=0
FAIL=0
ok()  { echo "  gecti  $1"; PASS=$((PASS + 1)); }
bad() { echo "  DUSTU  $1"; FAIL=$((FAIL + 1)); }

cat > "$TMP/n.tpr" <<'EOF'
float[] x = array_fill(5, 0.0);
float[] v = array_fill(5, 0.0);
float[] m = array_fill(5, 0.0);
func adim(float dt) {
    for (int i = 0; i < 5; i = i + 1) {
        for (int j = i + 1; j < 5; j = j + 1) {
            float d = x[i] - x[j];
            v[i] = v[i] - d * m[j] * dt;
            v[j] = v[j] + d * m[i] * dt;
        }
    }
    for (int i = 0; i < 5; i = i + 1) { x[i] = x[i] + dt * v[i]; }
}
func kdongu(float[] a, float[] b, int n) {
    for (int k = 0; k < n; k = k + 1) {
        float av = a[k];
        for (int j = 0; j < n; j = j + 1) { b[j] = b[j] + av; }
    }
}
for (int k = 0; k < 5; k = k + 1) { x[k] = toFloat(k * 3 % 7); m[k] = toFloat(k + 1); }
for (int s = 0; s < 1000; s = s + 1) { adim(0.001); }
float[] p = [1.0, 2.0, 3.0];
float[] q = array_fill(3, 0.0);
kdongu(p, q, 3);
float t = q[0] + q[1] + q[2];
for (int k = 0; k < 5; k = k + 1) { t = t + x[k] * 1000.0 + v[k]; }
print(toInt(round(t * 1000.0)));
EOF

derle() {  # derle <cikti_adi> [ortam...]
  local out="$1"; shift
  (cd "$TMP" && env "$@" TULPAR_AOT_NOCACHE=1 TULPAR_AOT_EMIT_LL_PRE=1 "$TULPAR" build n.tpr "$out" >"$out.log" 2>&1)
}
fn_body() {  # <ll> <ad>: kutulu t_<ad>.f ya da yerel <ad> govdesi
  sed -n "/^define .*@\(t_\)\{0,1\}$2\(\.f\)\{0,1\}(/,/^}/p" "$1"
}

if ! derle yeni; then
  echo "float ic ice kapisi DUSTU: sonda derlenmedi"; sed -n '1,15p' "$TMP/yeni.log"; exit 1
fi
out=$("$TMP/yeni" | tr -d '\r')
A=$(fn_body "$TMP/yeni.pre.ll" adim)
K=$(fn_body "$TMP/yeni.pre.ll" kdongu)
[ -n "$A" ] && [ -n "$K" ] || bad "fonksiyon govdeleri IR'da bulunamadi (fn_body kor)"

# 1) ic ice surum kuruldu
echo "$A" | grep -q '^fvn_fast:' && ok "nbody bicimi: dis dongu basinda tek sinav (fvn_fast)" \
  || bad "fvn_fast yok — ic ice surum kurulmadi"
echo "$A" | grep -q '^fvn_ic_done' && ok "hizli dis govdede ic dongu sinavsiz (fvn_ic_done)" \
  || bad "fvn_ic_done yok — ic dongu hizli dis govdede yine sinaniyor"

# 2) int sekil onbellegi surum dallanmasindan once yok
on=$(echo "$A" | sed -n '1,/^fvn_fast:/p' | grep -c '^shape\.ty' || true)
[ "$on" -eq 0 ] && ok "int sekil onbellegi dallanmadan once kurulmuyor" \
  || bad "dallanmadan once $on shape.ty blogu — her giriste baslik yeniden okunuyor"

# 3) matmul bicimli k dongusu ic ice surumlenmiyor
echo "$K" | grep -q '^fvn_fast' && bad "dis govdesinde erisim olan k dongusu ic ice surumlendi" \
  || ok "dis govdesinde erisim olan dongu ic ice surumlenmiyor (muhafazakar)"

# Pozitif kontrol
if derle kapali TULPAR_NO_FVNEST=1; then
  KA=$(fn_body "$TMP/kapali.pre.ll" adim)
  echo "$KA" | grep -qE '^fvn_(fast|ic_done)' && bad "pozitif kontrol: kapaliyken de fvn bloklari var — denetim kor" \
    || ok "pozitif kontrol: TULPAR_NO_FVNEST=1 ile fvn bloklari yok"
  ko=$("$TMP/kapali" | tr -d '\r')
  [ -n "$out" ] && [ "$ko" = "$out" ] && ok "iki derleme ayni sonuc ($out)" \
    || bad "sonuclar farkli: acik '$out', kapali '$ko'"
else
  bad "pozitif kontrol derlemesi basarisiz"
fi

echo "float_ic_ice: $PASS/$((PASS + FAIL))"
[ $FAIL -eq 0 ] || exit 1
exit 0
