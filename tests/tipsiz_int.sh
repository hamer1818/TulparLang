#!/usr/bin/env bash
# TIPSIZ FONKSIYONUN INT-OZEL KLONU (K216, 2026-09-28).
#
# `func fib(n)` kutulu derleniyordu (her islem bir etiket dagitimi). Artik
# govde "parametreler int ise donus int" diye kanitlanabiliyorsa `<ad>$i`
# native klonu uretiliyor ve kutulu girisin ilk isi argumanlarin etiketine
# bakip klona gitmek. Olculdu (bu makine, 2026-09-28): tipsiz fib(32)
# 7,5 ms -> 0,21 ms (tipli surumle ayni).
#
# Kapi dort seyi olcer:
#   1. YAPI: uygun fonksiyonun klonu var (`fib$i`), uygun olmayaninki yok
#      (karsilastirma donduren `pos`, dizgi donduren yol iceren `karisik`);
#   2. ANLAM: karisik tipli cagrilardan olusan program klonlu ve klonsuz
#      (TULPAR_NO_INTSPEC=1) derlemede BIREBIR ayni ciktiyi veriyor — sifira
#      bolme hatasi dahil;
#   3. KUTULU YOL KORUNUYOR: tests/boxed_value_abi.test.tpr klonlar KAPALIYKEN
#      de geciyor (yoksa int argumanli testleri artik klondan geciyordu);
#   4. HIZ (yapisal degil, olcum): klonlu tipsiz fib klonsuzdan hizli.
#
#   tests/tipsiz_int.sh [tulpar_yolu]
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

cat > "$TMP/sp.tpr" <<'EOF'
func fib_u(n) { if (n < 2) { return n; } return fib_u(n - 1) + fib_u(n - 2); }
func topla(n) { var s = 0; for (int i = 0; i < n; i++) { s = s + i; } return s; }
func pos(x) { return x > 0; }
func karisik(x) { if (x > 100) { return "buyuk"; } return x * 2; }
func kare(x) { return x * x; }
func bol(a, b) { return a / b; }
func mod3(a) { return a % 3; }
func ack(m, n) { if (m == 0) { return n + 1; } if (n == 0) { return ack(m - 1, 1); } return ack(m - 1, ack(m, n - 1)); }
print(fib_u(20), fib_u(5.5), fib_u(1));
print(topla(10), topla(2.5));
print(pos(3), pos(-1));
print(karisik(3), karisik(500));
print(kare(3037000500), kare(1.5));
print(bol(7, 2), bol(7.0, 2), mod3(-7), mod3(7.5));
print(call("fib_u", 15), call("topla", 5));
print(ack(2, 3), fib_u(true));
print(bol(1, 0));
EOF

# 1. yapi
(cd "$TMP" && TULPAR_AOT_NOCACHE=1 TULPAR_AOT_EMIT_LL_PRE=1 "$TUL" build sp.tpr sp_on >/dev/null 2>&1)
LL=$(ls "$TMP"/*.pre.ll 2>/dev/null | head -1)
if [ -n "$LL" ] && grep -q 'fib_u\$i' "$LL" && grep -q 'mod3\$i' "$LL" && grep -q 'ack\$i' "$LL"; then
    gecti "uygun fonksiyonlarin klonu var (fib_u\$i, mod3\$i, ack\$i; optimizasyon oncesi IR)"
else dustu "klon bulunamadi (IR: ${LL:-yok})"; fi
if [ -n "$LL" ] && ! grep -q 'pos\$i' "$LL" && ! grep -q 'karisik\$i' "$LL"; then
    gecti "uygun olmayanin klonu yok (pos: bool donus, karisik: dizgi yolu)"
else dustu "uygun olmayan fonksiyon klonlandi"; fi

# 2. anlam: klonlu == klonsuz
(cd "$TMP" && TULPAR_AOT_NOCACHE=1 TULPAR_NO_INTSPEC=1 "$TUL" build sp.tpr sp_off >/dev/null 2>&1)
on=$(cd "$TMP" && ./sp_on 2>&1); rc_on=$?
off=$(cd "$TMP" && ./sp_off 2>&1); rc_off=$?
if [ "$on" = "$off" ] && [ "$rc_on" = "$rc_off" ] && grep -q "^6765 9 1$" <<<"$on"; then
    gecti "klonlu ve klonsuz cikti birebir ayni (rc=$rc_on, sifira bolme dahil)"
else dustu "cikti farkli: klonlu rc=$rc_on '$on' / klonsuz rc=$rc_off '$off'"; fi

# 3. kutulu yol, klonlar kapaliyken
out=$(cd "$ROOT" && TULPAR_AOT_NOCACHE=1 TULPAR_NO_INTSPEC=1 "$TUL" tests/boxed_value_abi.test.tpr 2>&1); rc=$?
if [ "$rc" -eq 0 ] && grep -q "Fail: 0" <<<"$out"; then gecti "boxed_value_abi klonlar kapaliyken geciyor"
else dustu "boxed_value_abi (TULPAR_NO_INTSPEC=1) rc=$rc: $(echo "$out" | tail -2)"; fi

# 4. hiz
cat > "$TMP/hiz.tpr" <<'EOF'
func fib_u(n) { if (n < 2) { return n; } return fib_u(n - 1) + fib_u(n - 2); }
int t0 = time_ms();
var r = fib_u(30);
print(toString(time_ms() - t0) + " " + toString(r));
EOF
(cd "$TMP" && TULPAR_AOT_NOCACHE=1 "$TUL" build hiz.tpr h_on >/dev/null 2>&1 &&
 TULPAR_AOT_NOCACHE=1 TULPAR_NO_INTSPEC=1 "$TUL" build hiz.tpr h_off >/dev/null 2>&1)
a=$(cd "$TMP" && ./h_on); b=$(cd "$TMP" && ./h_off)
ta=${a%% *}; tb=${b%% *}
if [ "${a##* }" = "832040" ] && [ "${b##* }" = "832040" ] && [ "${ta:-99999}" -le "${tb:-0}" ]; then
    gecti "tipsiz fib(30): klonlu ${ta} ms <= klonsuz ${tb} ms"
else dustu "hiz: klonlu '$a' / klonsuz '$b'"; fi

if [ "$fail" -eq 0 ]; then echo "tipsiz_int: $n_gecti/$n_gecti"; fi
exit $fail
