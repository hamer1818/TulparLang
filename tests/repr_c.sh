#!/bin/bash
# `@repr(C)` — tanilar ve C YERLESIMI KAPISI (K036, 2026-09-29).
#
# `@repr(C) type T { ... }` struct'a C yerlesimi SOZU verir: alanlar bildirim
# sirasinda, hedefin hizalama/dolgu kuraliyla; `bool` C'deki gibi 1 bayt
# (varsayilan yerlesimde 8 baytlik yuva). Yalniz skaler alan (int/float/bool/
# f32/i32); yalniz `C` (std140/std430 GLSL'e ozgu, dilde vektor tipi yok).
#
#   A. Tanilar: str alan, `@repr(std140)`, fonksiyona `@repr`, modul/ana dosya
#      yerlesim sozu catismasi; gecerli kullanim (pozitif kontrol).
#   B. C YERLESIMI: C++ sondasi struct dizisinin deposunu `struct Bayrak *` /
#      `struct Kay *` diye okur (sizeof, 1 baytlik bool, dolgu, her alan) ve
#      C tarafindan yazar — Tulpar yazilani okumali.
#
# KAPININ POZITIF KONTROLU: ABI_BOZ=1 ile sonda bool'u 8 baytlik yuva
# (varsayilan yerlesim) varsayar — KIRMIZI olmak ZORUNDA.
# Eski derleyiciyle (`@` yok, olculdu 2026-09-29): 0/6 — hepsi kirmizi.
set -u
cd "$(dirname "$0")/.."
TULPAR="${1:-./tulpar}"
[ -x "$TULPAR" ] || { echo "repr(C) kapisi: $TULPAR yok — atlandi"; exit 0; }
TULPAR="$(cd "$(dirname "$TULPAR")" && pwd)/$(basename "$TULPAR")"
export LC_ALL=C

TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
PASS=0
FAIL=0
ok()  { echo "  gecti  $1"; PASS=$((PASS + 1)); }
bad() { echo "  DUSTU  $1"; FAIL=$((FAIL + 1)); [ -n "${2:-}" ] && echo "$2" | sed -n '1,8p' | sed 's/^/      /'; }
kos() { (cd "$TMP" && TULPAR_AOT_NOCACHE=1 "$TULPAR" "$1" 2>&1); }

# ---- A. Tanilar ---------------------------------------------------------
printf '@repr(C)\ntype K { int a; str s; }\nprint(1);\n' > "$TMP/a1.tpr"
out=$(kos a1.tpr); rc=$?
if [ $rc -ne 0 ] && echo "$out" | grep -q "a @repr(C) struct may only hold scalar fields ('s' is not)"; then
  ok "@repr(C) struct'ta str alan: ayristirma hatasi"
else bad "@repr(C) struct'ta str alan (rc=$rc)" "$out"; fi

printf '@repr(std140)\ntype K { int a; }\nprint(1);\n' > "$TMP/a2.tpr"
out=$(kos a2.tpr); rc=$?
if [ $rc -ne 0 ] && echo "$out" | grep -q "unsupported layout '@repr(std140)' (only @repr(C))"; then
  ok "@repr(std140): desteklenmiyor (sessizce C'ye dusmuyor)"
else bad "@repr(std140) (rc=$rc)" "$out"; fi

printf '@repr(C)\nfunc f() { return 1; }\nprint(f());\n' > "$TMP/a3.tpr"
out=$(kos a3.tpr); rc=$?
if [ $rc -ne 0 ] && echo "$out" | grep -q "@repr(C) applies only to a struct (type) declaration"; then
  ok "fonksiyona @repr(C): ayristirma hatasi"
else bad "fonksiyona @repr(C) (rc=$rc)" "$out"; fi

mkdir -p "$TMP/m"
printf '@repr(C)\ntype Vx { bool b; int n; }\nfunc mkv(): Vx { Vx r = { b: true, n: 3 }; return r; }\n' > "$TMP/m/mod.tpr"
printf 'type Vx { bool b; int n; }\nimport "mod.tpr";\nVx v = mkv();\nprint(v.n);\n' > "$TMP/m/ana.tpr"
out=$(cd "$TMP/m" && TULPAR_AOT_NOCACHE=1 "$TULPAR" ana.tpr 2>&1); rc=$?
if [ $rc -ne 0 ] && echo "$out" | grep -q '{ bool b; int n; }.*@repr(C) { bool b; int n; }'; then
  ok "modul @repr(C) / ana dosya varsayilan: yerlesim catismasi derleme hatasi"
else bad "modul @repr(C) / ana dosya varsayilan (rc=$rc)" "$out"; fi

cat > "$TMP/a5.tpr" <<'EOF'
@repr(C)
type B { bool a; f32 x; bool b; i32 n; }
B v = { a: true, x: 1.5, b: false, n: -3 };
v.b = true;
B[] d = [v];
print(toJson(d), d[0].a, d[0].b);
EOF
out=$(kos a5.tpr); rc=$?
if [ $rc -eq 0 ] && [ "$out" = '[{"a":true,"x":1.5,"b":true,"n":-3}] true true' ]; then
  ok "gecerli @repr(C) struct derlenir ve dogru calisir (pozitif kontrol)"
else bad "gecerli @repr(C) struct (rc=$rc)" "$out"; fi

# ---- B. C yerlesimi -----------------------------------------------------
case "$(uname -s)" in
  MINGW*|MSYS*|CYGWIN*)
    echo "  ATLANDI  C yerlesimi (Windows: sonda nesnesi PE'de olculmedi; tests/fonksiyon_arama.sh ile ayni gerekce)"
    echo "repr_c: $PASS/$((PASS + FAIL)) (C yerlesimi atlandi)"
    [ $FAIL -eq 0 ] || exit 1
    exit 0 ;;
esac
CXX_BIN="${CXX:-}"
if [ -z "$CXX_BIN" ]; then
  for c in c++ clang++ g++; do command -v "$c" >/dev/null 2>&1 && { CXX_BIN="$c"; break; }; done
fi
[ -n "$CXX_BIN" ] || { echo "repr(C) kapisi DUSTU: C++ derleyicisi yok (c++/clang++/g++)"; exit 1; }

cat > "$TMP/abi.tpr" <<'EOF'
@repr(C)
type Bayrak { bool a; f32 x; bool b; i32 n; }
@repr(C)
type Kay { int a; float b; bool c; }
Bayrak[] t = [];
push(t, { a: true, x: 1.5, b: false, n: -3 });
push(t, { a: false, x: 0.1, b: true, n: 70000 });
Kay[] k = [];
push(k, { a: 5000000000, b: -0.5, c: true });
call("abi_kontrol", t, k);
print(t[0].b, t[1].n, k[0].c);
EOF

cat > "$TMP/sonda.cpp" <<'CPPEOF'
#include "vm/vm.hpp"
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>

// Tulpar: @repr(C) type Bayrak { bool a; f32 x; bool b; i32 n; }
struct Bayrak { bool a; float x; bool b; int32_t n; };
// Tulpar: @repr(C) type Kay { int a; float b; bool c; }
struct Kay { int64_t a; double b; bool c; };
// ABI_BOZ: bool'u varsayilan yerlesimdeki gibi 8 baytlik yuva sanan sonda.
struct BayrakYuva { int64_t a; float x; int64_t b; int32_t n; };

static int g_hata = 0;
static void denet(bool ok, const char *ne) {
  std::printf("abi: %s %s\n", ok ? "TAMAM" : "HATA", ne);
  if (!ok) g_hata++;
}

extern "C" void t_abi_kontrol(VMValue *r, VMValue *pt, VMValue *pk) {
  *r = VM_VOID();
  const bool boz = std::getenv("ABI_BOZ") != nullptr;
  denet(pt && IS_STRUCT_ARRAY(*pt) && pk && IS_STRUCT_ARRAY(*pk), "iki arguman da struct dizisi");
  if (!pt || !IS_STRUCT_ARRAY(*pt) || !pk || !IS_STRUCT_ARRAY(*pk)) goto son;
  {
    ObjStructArray *a = AS_STRUCT_ARRAY(*pt);
    if (boz) {
      denet(a->elem_size == (int)sizeof(BayrakYuva), "Bayrak eleman boyu == sizeof(BayrakYuva) (bool 8 bayt)");
    } else {
      Bayrak *p = reinterpret_cast<Bayrak *>(a->data);
      denet(a->elem_size == (int)sizeof(Bayrak), "Bayrak eleman boyu == C sizeof(Bayrak) (16: 1 baytlik bool + dolgu)");
      denet(p[0].a == true && p[0].x == 1.5f && p[0].b == false && p[0].n == -3,
            "Bayrak[0] C gorunumu: bool @0, float @4, bool @8, int32 @12");
      denet(p[1].a == false && p[1].x == 0.1f && p[1].b == true && p[1].n == 70000, "Bayrak[1] C gorunumu");
      p[0].b = true;   // C yazar, Tulpar okur
      p[1].n = 42;
    }
    ObjStructArray *b = AS_STRUCT_ARRAY(*pk);
    Kay *q = reinterpret_cast<Kay *>(b->data);
    denet(b->elem_size == (int)sizeof(Kay), "Kay eleman boyu == C sizeof(Kay) (24: sondaki bool + kuyruk dolgusu)");
    denet(q[0].a == 5000000000LL && q[0].b == -0.5 && q[0].c == true, "Kay[0] C gorunumu: int64 @0, double @8, bool @16");
    if (!boz) q[0].c = false;
    std::printf("abi: bilgi sizeof(Bayrak)=%zu offsetof(Bayrak,b)=%zu sizeof(Kay)=%zu offsetof(Kay,c)=%zu\n",
                sizeof(Bayrak), offsetof(Bayrak, b), sizeof(Kay), offsetof(Kay, c));
  }
son:
  std::printf("abi: SONUC %d hata\n", g_hata);
  std::fflush(stdout);
  if (g_hata) std::exit(3);
}
CPPEOF

if ! "$CXX_BIN" -std=c++17 -O1 -c "$TMP/sonda.cpp" -Isrc -o "$TMP/sonda.o" 2>"$TMP/cxx.err"; then
  echo "repr(C) kapisi DUSTU: sonda derlenemedi ($CXX_BIN)"
  sed -n '1,12p' "$TMP/cxx.err" | sed 's/^/    /'
  exit 1
fi
if ! TULPAR_AOT_NOCACHE=1 TULPAR_AOT_LINK_FLAGS="$TMP/sonda.o" "$TULPAR" build "$TMP/abi.tpr" "$TMP/abi" >"$TMP/build.out" 2>&1; then
  bad "C yerlesimi: program derlenemedi/linklenemedi" "$(tail -12 "$TMP/build.out")"
else
  "$TMP/abi" >"$TMP/out" 2>"$TMP/err"; rc=$?
  son=$(grep -v '^abi:' "$TMP/out" | tail -1)
  if [ $rc -eq 0 ] && grep -q "^abi: SONUC 0 hata" "$TMP/out" && ! grep -q "^abi: HATA" "$TMP/out" &&
     [ "$son" = "true 42 false" ]; then
    ok "@repr(C) struct dizisi C dizisiyle bayt bayt ayni, iki yon ($(grep -c '^abi: TAMAM' "$TMP/out") denetim; C'nin yazdigi: $son)"
  else bad "@repr(C) struct dizisi C dizisiyle ayni (rc=$rc, cikti '$son')" "$(grep '^abi:' "$TMP/out"; sed -n '1,4p' "$TMP/err")"; fi
  grep "^abi: bilgi" "$TMP/out" | sed 's/^abi: bilgi /  bilgi: /'
  ABI_BOZ=1 "$TMP/abi" >"$TMP/out_boz" 2>&1; rc_boz=$?
  if [ $rc_boz -ne 0 ] && grep -q "^abi: HATA Bayrak eleman boyu" "$TMP/out_boz"; then
    ok "pozitif kontrol: bool'u 8 bayt sanan sonda kirmizi (cikis $rc_boz)"
  else bad "pozitif kontrol (ABI_BOZ=1) yesil gecti — denetimler bir sey olcmuyor" "$(cat "$TMP/out_boz")"; fi
fi

echo "repr_c: $PASS/$((PASS + FAIL))"
[ $FAIL -eq 0 ] || exit 1
exit 0
