#!/bin/bash
# WEB HEDEFINDE try/catch KAPISI (2026-10-02).
#
# Neden var: try/catch modulde dogrudan `setjmp` cagrisina iner. wasm'da
# gercek bir `setjmp` sembolu yok — emcc kendi clang'iyla derlerken LLVM'in
# SjLj alcaltma gecisini acar; biz wasm objesini KENDI LLVM'imizle uretiyoruz
# ve gecis kapaliydi. Sonuc: try iceren (ya da `import "test"` eden) HER
# program `tulpar build --target=web` ile `wasm-ld: undefined symbol: setjmp`
# diye linkte dusuyordu. Hicbir kapi web'e gercekten link etmiyordu
# (dist_archive_audit.py yalniz arsivdeki sembolleri sayar).
#
# Neyi olcuyor: try/catch'li bir programi web'e derleyip LINKLER ve node ile
# KOSAR; ciktiyi satir satir karsilastirir (fonksiyondan firlatma, dongude
# yakalama, try icinde degisen yerelin korunmasi, ic ice try, yeniden
# firlatma).
#
# POZITIF KONTROL: ayni program `TULPAR_WEB_SJLJ=0` ile (gecis kapali —
# duzeltmeden onceki durum) derlenir ve link `undefined symbol: setjmp` ile
# DUSMEK ZORUNDADIR. Dusmezse kapi bir sey olcmuyor demektir.
#
# Emscripten (em++) ya da wasm/dist arsivleri yoksa GORUNUR atlanir;
# `TULPAR_DIST_ZORUNLU=1` (CI Linux: emsdk kurulu, arsivler o iste uretiliyor)
# iken atlama KIRMIZI.
set -u
cd "$(dirname "$0")/.."
ROOT="$(pwd)"
TULPAR="${1:-./tulpar}"
case "$TULPAR" in /*) ;; *) TULPAR="$ROOT/${TULPAR#./}" ;; esac
[ -x "$TULPAR" ] || { echo "web try/catch kapisi: $TULPAR yok — atlandi"; exit 0; }

atla() {
  if [ "${TULPAR_DIST_ZORUNLU:-}" = "1" ]; then
    echo "web try/catch kapisi DUSTU: $1 (TULPAR_DIST_ZORUNLU=1 — atlama kusur)"
    exit 1
  fi
  echo "web try/catch kapisi: ATLANDI ($1)"
  exit 0
}

if ! command -v em++ >/dev/null 2>&1 && [ -f "$ROOT/wasm/emsdk/emsdk_env.sh" ]; then
  # shellcheck disable=SC1091
  source "$ROOT/wasm/emsdk/emsdk_env.sh" >/dev/null 2>&1
fi
command -v em++ >/dev/null 2>&1 || atla "em++ yok (source wasm/emsdk/emsdk_env.sh)"
[ -f "$ROOT/wasm/dist/libtulpar_runtime_web.a" ] || atla "wasm/dist arsivi yok (wasm/build_tame_web.sh)"
NODE="${EMSDK_NODE:-}"
[ -n "$NODE" ] && [ -x "$NODE" ] || NODE="$(command -v node || true)"
[ -n "$NODE" ] || atla "node yok"
export TULPAR_WEB_LIB_DIR="$ROOT/wasm/dist"

TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT

cat > "$TMP/tc.tpr" <<'TPREOF'
func bol(int a, int b): int {
  if (b == 0) { throw "sifira bolme"; }
  return a / b;
}
func derin(int n): int {
  if (n == 0) { throw "dipte"; }
  return derin(n - 1) + 1;
}
try {
  print(bol(10, 2));
  print(bol(1, 0));
  print("buraya gelmemeli");
} catch (e) {
  print("yakalandi: " + toString(e));
}
int n = 0;
for (int i = 0; i < 3; i++) {
  try {
    if (i == 1) { throw "bir"; }
    n = n + 10;
  } catch (e) {
    n = n + 1;
  }
}
print(n);
int yerel = 1;
try {
  yerel = 42;
  derin(5);
} catch (e) {
  print("derin: " + toString(e) + " yerel=" + toString(yerel));
}
try {
  try {
    throw "ic";
  } catch (e) {
    throw "dis<-" + toString(e);
  }
} catch (e) {
  print("ic ice: " + toString(e));
}
print("bitti");
TPREOF

BEKLENEN='5
yakalandi: sifira bolme
21
derin: dipte yerel=42
ic ice: dis<-ic
bitti'

cd "$TMP"
fail=0

# --- 1) gercek yol: link + node ---------------------------------------------
if ! "$TULPAR" build --target=web tc.tpr -o out/tc > build.log 2>&1; then
  echo "  DUSTU  web derleme/link (try/catch):"
  grep -iE "error|hata" build.log | head -5 | sed 's/^/          /'
  fail=1
else
  cikti=$("$NODE" out/tc.js 2>&1 | tr -d '\r')
  if [ "$cikti" = "$BEKLENEN" ]; then
    echo "  gecti  web: try/catch linkleniyor ve node'da dogru calisiyor (6 satir)"
  else
    echo "  DUSTU  web: node ciktisi beklenenden farkli:"
    echo "$cikti" | head -10 | sed 's/^/          /'
    fail=1
  fi
  # Bilgi: hangi SjLj ABI'si uretildi (LLVM <= 18 eski, >= 19 yeni). Eski
  # ABI runtime/web_sjlj_uyum.c ile karsilaniyor; CI (LLVM 18) onu olcuyor.
  NM=""
  for c in llvm-nm llvm-nm-18 "${EMSDK:-/nonexistent}/upstream/bin/llvm-nm"; do
    command -v "$c" >/dev/null 2>&1 && { NM="$c"; break; }
  done
  if [ -n "$NM" ] && [ -f out/tc.o ]; then
    if "$NM" out/tc.o 2>/dev/null | grep -qw saveSetjmp; then
      echo "  bilgi: SjLj ABI eski (saveSetjmp/testSetjmp — runtime/web_sjlj_uyum.c)"
    elif "$NM" out/tc.o 2>/dev/null | grep -qw __wasm_setjmp; then
      echo "  bilgi: SjLj ABI yeni (__wasm_setjmp)"
    fi
  fi
fi

# --- 2) pozitif kontrol: gecis kapali -> setjmp tanimsiz --------------------
if TULPAR_WEB_SJLJ=0 "$TULPAR" build --target=web tc.tpr -o kontrol/tc > kontrol.log 2>&1; then
  echo "  DUSTU  pozitif kontrol: SjLj gecisi kapaliyken link TUTTU — kapi bir sey olcmuyor"
  fail=1
elif grep -q "undefined symbol: setjmp" kontrol.log; then
  echo "  gecti  pozitif kontrol: gecis kapali -> 'undefined symbol: setjmp' (duzeltme oncesi hali)"
else
  echo "  DUSTU  pozitif kontrol: link dustu ama BASKA sebeple:"
  grep -iE "error|hata" kontrol.log | head -3 | sed 's/^/          /'
  fail=1
fi

if [ $fail -ne 0 ]; then
  echo "web try/catch kapisi DUSTU"
  exit 1
fi
echo "web try/catch kapisi temiz (link + node + pozitif kontrol)"
