#!/bin/bash
# f32 / i32 STRUCT ALANLARI — tanilar ve C YERLESIMI KAPISI (K037/K035).
#
# Anlam testleri tests/f32_alan.test.tpr'de. Bu betik iki seyi olcuyor:
#
#   A. Tanilar: f32 alanli struct'ta skaler olmayan alan (ayristirma hatasi),
#      yerel `f32 x` (typecheck hatasi, cozum onerisiyle), `f32`in degisken
#      adi olarak gecerli kalmasi, ayni adli kullanici tipinin baglamsal adi
#      golgelemesi, modulde `f32 x` / ana dosyada `float x` yerlesim catismasi.
#
#   B. C YERLESIMI (asil iddia): struct dizisinin eleman deposu, ayni alanlari
#      tasiyan C struct'inin dizisiyle BAYT BAYT ayni. Sonda (C++; vm.hpp'yi
#      icerir) programa baglanir, program `call("abi_kontrol", t, k)` ile
#      diziyi verir; sonda ObjStructArray::data'yi `struct Tepe *` / `struct
#      Karma *` diye okur (sizeof, hizalama/dolgu, her alanin degeri), sonra
#      C tarafindan YAZAR — Tulpar yazilani okumali (iki yon).
#
# KAPININ POZITIF KONTROLU: ABI_BOZ=1 ile sonda eski yerlesimi (alan basina 8
# bayt: double/int64) varsayar. Kapi o kosumu KIRMIZI gormek ZORUNDA; yesil
# gorurse denetimler hicbir sey olcmuyor demektir ve kapi duser.
# Eski derleyiciyle (f32'siz, olculdu 2026-09-29): 2/7 — A'nin 6 denetiminden 4'u
# (degisken adi ve golgeleme zaten gecerliydi) ve B kirmizi.
set -u
cd "$(dirname "$0")/.."
TULPAR="${1:-./tulpar}"
[ -x "$TULPAR" ] || { echo "f32 yerlesim kapisi: $TULPAR yok — atlandi"; exit 0; }
TULPAR="$(cd "$(dirname "$TULPAR")" && pwd)/$(basename "$TULPAR")"   # modul testleri baska dizinden kosuyor
export LC_ALL=C   # tanilar Ingilizce beklenir (tr_en yerel ayara bakar)

TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
PASS=0
FAIL=0
ok()  { echo "  gecti  $1"; PASS=$((PASS + 1)); }
bad() { echo "  DUSTU  $1"; FAIL=$((FAIL + 1)); [ -n "${2:-}" ] && echo "$2" | sed -n '1,8p' | sed 's/^/      /'; }

# ---- A. Tanilar ---------------------------------------------------------
cat > "$TMP/a1.tpr" <<'EOF'
type Kotu { f32 x; str ad; }
print(1);
EOF
out=$(TULPAR_AOT_NOCACHE=1 "$TULPAR" "$TMP/a1.tpr" 2>&1); rc=$?
if [ $rc -ne 0 ] && echo "$out" | grep -q "may only hold scalar fields ('ad' is not)"; then
  ok "f32 alanli struct'ta str alan: ayristirma hatasi"
else bad "f32 alanli struct'ta str alan: ayristirma hatasi (rc=$rc)" "$out"; fi

cat > "$TMP/a2.tpr" <<'EOF'
f32 x = 1.5;
print(x);
EOF
out=$("$TULPAR" typecheck "$TMP/a2.tpr" 2>&1); rc=$?
if [ $rc -ne 0 ] && echo "$out" | grep -q "'f32' is only a struct field type (4-byte storage); local 'x' should use 'float'"; then
  ok "yerel f32: typecheck hatasi + 'float' onerisi"
else bad "yerel f32: typecheck hatasi + 'float' onerisi (rc=$rc)" "$out"; fi

cat > "$TMP/a3.tpr" <<'EOF'
float f32 = 2.5;
int i32 = 7;
print(f32 + i32);
EOF
out=$(TULPAR_AOT_NOCACHE=1 "$TULPAR" "$TMP/a3.tpr" 2>&1); rc=$?
if [ $rc -eq 0 ] && [ "$out" = "9.5" ]; then
  ok "f32 / i32 degisken adi olarak gecerli (baglamsal ad, anahtar sozcuk degil)"
else bad "f32 / i32 degisken adi olarak gecerli (rc=$rc)" "$out"; fi

cat > "$TMP/a4.tpr" <<'EOF'
type f32 { int a; }
type W { f32 v; int n; }
W w = { v: { a: 3 }, n: 1 };
print(w.v.a + w.n);
EOF
out=$(TULPAR_AOT_NOCACHE=1 "$TULPAR" "$TMP/a4.tpr" 2>&1); rc=$?
if [ $rc -eq 0 ] && [ "$out" = "4" ]; then
  ok "ayni adli kullanici tipi baglamsal 'f32'yi golgeler"
else bad "ayni adli kullanici tipi baglamsal 'f32'yi golgeler (rc=$rc)" "$out"; fi

mkdir -p "$TMP/m"
cat > "$TMP/m/mod.tpr" <<'EOF'
type Vx { f32 x; i32 n; }
func mkv(float a): Vx { Vx r = { x: a, n: 3 }; return r; }
EOF
cat > "$TMP/m/ana.tpr" <<'EOF'
type Vx { float x; i32 n; }
import "mod.tpr";
Vx v = mkv(0.5);
print(v.x);
EOF
out=$(cd "$TMP/m" && TULPAR_AOT_NOCACHE=1 "$TULPAR" ana.tpr 2>&1); rc=$?
if [ $rc -ne 0 ] && echo "$out" | grep -q '{ float x; i32 n; }.*{ f32 x; i32 n; }'; then
  ok "modul 'f32 x' / ana dosya 'float x': yerlesim catismasi derleme hatasi"
else bad "modul 'f32 x' / ana dosya 'float x': yerlesim catismasi (rc=$rc)" "$out"; fi

cat > "$TMP/m/iyi.tpr" <<'EOF'
import "mod.tpr";
Vx v = mkv(0.1);
Vx[] d = [];
push(d, v);
print(d[0].x, d[0].n);
EOF
out=$(cd "$TMP/m" && TULPAR_AOT_NOCACHE=1 "$TULPAR" iyi.tpr 2>&1); rc=$?
if [ $rc -eq 0 ] && [ "$out" = "0.10000000149011612 3" ]; then
  ok "modulun f32 struct'i ana dosyada (pozitif kontrol: gecerli kullanim derlenir)"
else bad "modulun f32 struct'i ana dosyada (rc=$rc)" "$out"; fi

# ---- B. C yerlesimi -----------------------------------------------------
case "$(uname -s)" in
  MINGW*|MSYS*|CYGWIN*)
    # Sonda `t_abi_kontrol`u dlsym yedegiyle buluyor; PE'de disari acilma
    # fonksiyon_arama.sh'deki gibi olculmedi. Sessiz degil:
    echo "  ATLANDI  C yerlesimi (Windows: sonda nesnesi PE'de olculmedi; tests/fonksiyon_arama.sh ile ayni gerekce)"
    echo "f32_yerlesim: $PASS/$((PASS + FAIL)) (C yerlesimi atlandi)"
    [ $FAIL -eq 0 ] || exit 1
    exit 0 ;;
esac
CXX_BIN="${CXX:-}"
if [ -z "$CXX_BIN" ]; then
  for c in c++ clang++ g++; do command -v "$c" >/dev/null 2>&1 && { CXX_BIN="$c"; break; }; done
fi
[ -n "$CXX_BIN" ] || { echo "f32 yerlesim kapisi DUSTU: C++ derleyicisi yok (c++/clang++/g++)"; exit 1; }

cat > "$TMP/abi.tpr" <<'EOF'
type Tepe { f32 x; f32 y; f32 z; i32 renk; }
type Karma { i32 a; float b; f32 c; int d; bool e; }
Tepe[] t = [];
push(t, { x: 1.5, y: -2.25, z: 0.1, renk: 7 });
push(t, { x: 3.0, y: 4.0, z: 5.0, renk: -1 });
Karma[] k = [];
push(k, { a: -3, b: 0.1, c: 2.5, d: 5000000000, e: true });
push(k, { a: 9, b: -1.5, c: 0.1, d: -2, e: false });
call("abi_kontrol", t, k);
print(t[1].y, t[1].renk, k[1].c);
EOF

cat > "$TMP/sonda.cpp" <<'CPPEOF'
#include "vm/vm.hpp"
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>

// Tulpar: type Tepe { f32 x; f32 y; f32 z; i32 renk; }
struct Tepe { float x, y, z; int32_t renk; };
// Tulpar: type Karma { i32 a; float b; f32 c; int d; bool e; }
// (float = double, int = int64, bool alan varsayilan yerlesimde 8 bayt)
struct Karma { int32_t a; double b; float c; int64_t d; int64_t e; };
// ABI_BOZ: ESKI yerlesim — alan basina 8 bayt.
struct TepeYuva { double x, y, z; int64_t renk; };

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
    denet(a->count == 2, "Tepe[] uzunluk 2");
    if (boz) {
      TepeYuva *p = reinterpret_cast<TepeYuva *>(a->data);
      denet(a->elem_size == (int)sizeof(TepeYuva), "Tepe eleman boyu == sizeof(TepeYuva) (eski yerlesim)");
      denet(p[0].x == 1.5 && p[1].renk == -1, "Tepe alanlari eski yerlesimle okunuyor");
    } else {
      Tepe *p = reinterpret_cast<Tepe *>(a->data);
      denet(a->elem_size == (int)sizeof(Tepe), "Tepe eleman boyu == C sizeof(Tepe) (16)");
      denet(p[0].x == 1.5f && p[0].y == -2.25f && p[0].z == 0.1f && p[0].renk == 7,
            "Tepe[0] C gorunumu: 1.5, -2.25, 0.1f, 7");
      denet(p[1].x == 3.0f && p[1].y == 4.0f && p[1].z == 5.0f && p[1].renk == -1,
            "Tepe[1] C gorunumu (adim sizeof(Tepe)): 3, 4, 5, -1");
      p[1].y = 99.5f;   // C tarafindan yaz: Tulpar okumali
      p[1].renk = 42;
    }
    ObjStructArray *b = AS_STRUCT_ARRAY(*pk);
    Karma *q = reinterpret_cast<Karma *>(b->data);
    denet(b->elem_size == (int)sizeof(Karma), "Karma eleman boyu == C sizeof(Karma) (40: hizalama dolgusu dahil)");
    denet(q[0].a == -3 && q[0].b == 0.1 && q[0].c == 2.5f && q[0].d == 5000000000LL && q[0].e == 1,
          "Karma[0] C gorunumu: i32 @0, double @8, float @16, int64 @24, bool @32");
    denet(q[1].a == 9 && q[1].b == -1.5 && q[1].c == 0.1f && q[1].d == -2 && q[1].e == 0,
          "Karma[1] C gorunumu");
    if (!boz) q[1].c = 0.75f;
    std::printf("abi: bilgi sizeof(Tepe)=%zu sizeof(Karma)=%zu offsetof(Karma,c)=%zu offsetof(Karma,d)=%zu\n",
                sizeof(Tepe), sizeof(Karma), offsetof(Karma, c), offsetof(Karma, d));
  }
son:
  std::printf("abi: SONUC %d hata\n", g_hata);
  std::fflush(stdout);
  if (g_hata) std::exit(3);
}
CPPEOF

if ! "$CXX_BIN" -std=c++17 -O1 -c "$TMP/sonda.cpp" -Isrc -o "$TMP/sonda.o" 2>"$TMP/cxx.err"; then
  echo "f32 yerlesim kapisi DUSTU: sonda derlenemedi ($CXX_BIN)"
  sed -n '1,12p' "$TMP/cxx.err" | sed 's/^/    /'
  exit 1
fi
if ! TULPAR_AOT_NOCACHE=1 TULPAR_AOT_LINK_FLAGS="$TMP/sonda.o" "$TULPAR" build "$TMP/abi.tpr" "$TMP/abi" >"$TMP/build.out" 2>&1; then
  bad "C yerlesimi: program derlenemedi/linklenemedi" "$(tail -12 "$TMP/build.out")"
else
  "$TMP/abi" >"$TMP/out" 2>"$TMP/err"; rc=$?
  son=$(grep -v '^abi:' "$TMP/out" | tail -1)
  if [ $rc -eq 0 ] && grep -q "^abi: SONUC 0 hata" "$TMP/out" && ! grep -q "^abi: HATA" "$TMP/out"; then
    ok "struct dizisi deposu C dizisiyle bayt bayt ayni ($(grep -c '^abi: TAMAM' "$TMP/out") denetim)"
  else bad "struct dizisi deposu C dizisiyle ayni (rc=$rc)" "$(grep '^abi:' "$TMP/out"; sed -n '1,4p' "$TMP/err")"; fi
  if [ "$son" = "99.5 42 0.75" ]; then
    ok "C'nin yazdigi degerleri Tulpar okuyor (99.5 42 0.75)"
  else bad "C'nin yazdigi degerleri Tulpar okuyor" "cikti: $son"; fi
  grep "^abi: bilgi" "$TMP/out" | sed 's/^abi: bilgi /  bilgi: /'
  # Pozitif kontrol: eski (8 bayt/alan) yerlesimi varsayan sonda KIRMIZI olmali.
  ABI_BOZ=1 "$TMP/abi" >"$TMP/out_boz" 2>&1; rc_boz=$?
  if [ $rc_boz -ne 0 ] && grep -q "^abi: HATA Tepe eleman boyu" "$TMP/out_boz"; then
    ok "pozitif kontrol: eski yerlesimi varsayan sonda kirmizi (cikis $rc_boz)"
  else bad "pozitif kontrol (ABI_BOZ=1) yesil gecti — denetimler bir sey olcmuyor" "$(cat "$TMP/out_boz")"; fi
fi

echo "f32_yerlesim: $PASS/$((PASS + FAIL))"
[ $FAIL -eq 0 ] || exit 1
exit 0
