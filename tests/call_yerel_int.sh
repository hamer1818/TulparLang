#!/bin/bash
# call(f, ...) YEREL INT YOLU KAPISI (2026-10-01).
#
# Tumu-int `func f(int x): int` yerel (i64) ABI'li; call() eskiden onu
# kutulu sarmalayici `tb_f` uzerinden cagiriyordu: arguman bellege yazilip
# geri okunuyor, float kirpma secimi (fptosi + select) iki kez bagimlilik
# zincirinde, ikinci bir cagri ve sonuc yuvasindan gecis. benchmarks/fair/
# callfn 174 ms (C gcc -O2 92). Artik havuz kaydi ciplak giris noktasini
# (`nfp`) da tasiyor ve argumanlarin hepsi INT ise satir ici yol onu dogrudan
# cagiriyor: 75 ms (Ryzen 7 9800X3D, 2026-10-01, taskset 10,11, en iyi 7).
#
# Anlam paketi tests/call_yerel_int.test.tpr (iki yolun ayni sonucu verdigi).
# Bu kapi yolun GERCEKTEN kullanildigini iki bagimsiz yerden okuyor:
#   1) IR: `call i64 %fnref.nfp(` var ve main ciplak giris noktalarini
#      `aot_register_func_native` ile kaydediyor (codegen tarafi).
#   2) runtime tanisi: TULPAR_CALL_TANI=1 -> `yerel=2` (iki hedefin havuz
#      kaydi nfp tasiyor; runtime tarafi — kayit ya da aot_fn_ref bozulursa
#      IR yesil kalir ama yol hic tutmaz, bunu yalniz bu ayak gorur).
# POZITIF KONTROL: TULPAR_NO_CALL_NATIVE=1 -> ikisi de kaybolmali (yerel=0),
# cikti AYNI kalmali.
#
# Sabotajla dogrulandi (2026-10-01):
#   * aot_fn_ref'te `e->nfp = ...` satiri kaldirilinca 2) kirmizi (yerel=0),
#     IR ayagi yesil — iki ayak gercekten ayri seyleri olcuyor.
#   * codegen'de yerel yol kapatilinca (call_native_enabled hep false) 1) ve
#     2) kirmizi.
set -u
cd "$(dirname "$0")/.."
TULPAR="${1:-./tulpar}"
[ -x "$TULPAR" ] || { echo "call yerel int kapisi: $TULPAR yok — atlandi"; exit 0; }
TULPAR="$(cd "$(dirname "$TULPAR")" && pwd)/$(basename "$TULPAR")"

TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
PASS=0
FAIL=0
ok()  { echo "  gecti  $1"; PASS=$((PASS + 1)); }
bad() { echo "  DUSTU  $1"; FAIL=$((FAIL + 1)); }

cat > "$TMP/c.tpr" <<'EOF'
func f(int x): int { return (x * 31 + 7) % 1000003; }
func g(int x): int { return (x * 17 + 3) % 1000003; }
var tab = [f, g];
int acc = 1;
for (int i = 0; i < 100000; i = i + 1) { acc = call(tab[acc & 1], acc); }
print(acc);
EOF

derle() {  # derle <cikti_adi> [ortam...]
  local out="$1"; shift
  (cd "$TMP" && env "$@" TULPAR_AOT_NOCACHE=1 TULPAR_AOT_EMIT_LL=1 "$TULPAR" build c.tpr "$out" >"$out.log" 2>&1)
}
tani() { TULPAR_CALL_TANI=1 "$1" 2>&1 >/dev/null | tr -d '\r' | sed -n 's/^call-tani: .* yerel=\([0-9]*\)$/\1/p'; }

if ! derle yeni; then
  echo "call yerel int kapisi DUSTU: sonda derlenmedi"; sed -n '1,15p' "$TMP/yeni.log"; exit 1
fi
out=$("$TMP/yeni" | tr -d '\r')
LL="$TMP/yeni.ll"
grep -q 'call i64 %fnref\.nfp' "$LL" && ok "IR: ciplak giris noktasina dogrudan cagri var" \
  || bad "IR: 'call i64 %fnref.nfp' yok — yerel int yolu uretilmedi"
NREG=$(grep -c 'call void @aot_register_func_native(' "$LL" || true)
[ "$NREG" -eq 2 ] && ok "IR: iki hedef aot_register_func_native ile kayitli" \
  || bad "IR: aot_register_func_native cagrisi $NREG (2 bekleniyordu)"
y=$(tani "$TMP/yeni")
[ "$y" = "2" ] && ok "runtime: havuzda 2 kayit yerel giris noktasi tasiyor" \
  || bad "runtime: yerel='$y' (2 bekleniyordu) — nfp dolmuyor, yol hic tutmaz"

if derle kapali TULPAR_NO_CALL_NATIVE=1; then
  KL="$TMP/kapali.ll"
  grep -q '%fnref\.nfp' "$KL" && bad "pozitif kontrol: kapaliyken de nfp yolu var — denetim kor" \
    || ok "pozitif kontrol: TULPAR_NO_CALL_NATIVE=1 ile nfp yolu yok"
  grep -q '@aot_register_func_native(' "$KL" && bad "pozitif kontrol: kapaliyken de native kayit var" \
    || ok "pozitif kontrol: kapaliyken native kayit yok"
  yk=$(tani "$TMP/kapali")
  [ "$yk" = "0" ] && ok "pozitif kontrol: kapaliyken yerel=0" \
    || bad "pozitif kontrol: kapaliyken yerel='$yk' (0 bekleniyordu)"
  ko=$("$TMP/kapali" | tr -d '\r')
  [ -n "$out" ] && [ "$ko" = "$out" ] && ok "iki yol ayni sonuc ($out)" \
    || bad "sonuclar farkli: yerel '$out', kutulu '$ko'"
else
  bad "pozitif kontrol derlemesi basarisiz"
fi

echo "call_yerel_int: $PASS/$((PASS + FAIL))"
[ $FAIL -eq 0 ] || exit 1
exit 0
