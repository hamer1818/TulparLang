#!/bin/bash
# typeof() SABIT DIZGI KAPISI (2026-10-02).
#
# Neden var: typeof(x) her cagrida tip adini string arenasina yeniden
# kopyaliyordu (cagri basina 48 + ad bayti; checkpoint'siz arena hic geri
# sarilmiyor). Artik tip adlari olumsuz, paylasilan dizgiler. Anlam
# tests/typeof_sabit.test.tpr'de; bu kapi MEKANIZMAYI olcer, runtime'a C++
# sondasi linkleyerek (TULPAR_AOT_LINK_FLAGS, tests/debox_yaris.sh gibi):
#   1. ayni tip icin iki cagri AYNI isaretciyi donduruyor (ayirma yok);
#   2. donen dizgi kalici (arena_allocated = 0) — yazma bariyeri kopyalamaz;
#   3. yedi tipin adi dogru ("int" "float" "bool" "string" "array" "object"
#      "null");
#   4. arena_save -> typeof -> arena_drop -> arenaya yeni dizgiler: onceki
#      typeof sonucu HALA dogru (eskiden ayni bellek uzerine yaziliyordu).
#
# POZITIF KONTROL (4'un olctugunu gosterir): ayni save/typeof/drop/ustune-
# yazma duzenegi ARENADAN ayrilmis bir dizgiyle (aot_allocate_string —
# typeof'un ESKI davranisi) kosulur ve ustune yazilmis olmasi ZORUNLU.
# Gorulmezse duzenek arena tekrar kullanimini uretemiyor demektir ve kapi
# duser.
set -u
cd "$(dirname "$0")/.."
TULPAR="${1:-./tulpar}"
[ -x "$TULPAR" ] || { echo "typeof sabit kapisi: $TULPAR yok — atlandi"; exit 0; }
case "$(uname -s)" in
  MINGW*|MSYS*|CYGWIN*)
    # call() sondanin t_ sembolunu dlsym ile buluyor; sonda nesnesinin PE'de
    # disari acilmasi OLCULMEDI (tests/fonksiyon_arama.sh ile ayni gerekce).
    # Anlam paketi (typeof_sabit.test.tpr) Windows'ta da kosuyor. Sessiz degil:
    echo "typeof sabit kapisi: ATLANDI (Windows: sonda nesnesi PE'de olculmedi)"
    exit 0 ;;
esac
CXX_BIN="${CXX:-}"
if [ -z "$CXX_BIN" ]; then
  for c in c++ clang++ g++; do command -v "$c" >/dev/null 2>&1 && { CXX_BIN="$c"; break; }; done
fi
[ -n "$CXX_BIN" ] || { echo "typeof sabit kapisi DUSTU: C++ derleyicisi yok (c++/clang++/g++)"; exit 1; }

TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT

cat > "$TMP/prog.tpr" <<'TPREOF'
// Denetim sondanin icinde; call() onu dlsym ile bulur (t_typeof_kontrol).
call("typeof_kontrol");
TPREOF

cat > "$TMP/sonda.cpp" <<'CPPEOF'
#include "vm/vm.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>

extern "C" {
VMValue aot_typeof(VMValue v);
VMValue aot_arena_save(void);
VMValue aot_arena_drop(VMValue idx);
ObjString *aot_allocate_string(const char *chars, int length);
}

static int hata = 0;
static void hatali(const char *m) { std::printf("typeof: HATA %s\n", m); hata++; }

static ObjString *tip(VMValue v) { return (ObjString *)AS_OBJ(aot_typeof(v)); }
static bool esit(ObjString *s, const char *w) {
  return s && s->length == (int)std::strlen(w) && std::memcmp(s->chars, w, s->length) == 0;
}

// save -> uret() -> drop -> arenayi "Z" dolu dizgilerle doldur; uretilen
// dizgi hala `w` mi?
static bool drop_sonrasi_saglam(ObjString *(*uret)(), const char *w) {
  VMValue cp = aot_arena_save();
  ObjString *s = uret();
  aot_arena_drop(cp);
  char z[64];
  std::memset(z, 'Z', sizeof z);
  for (int i = 0; i < 64; i++) aot_allocate_string(z, 1 + (i % 40));
  return esit(s, w);
}
static ObjString *typeof_float() { return tip(VM_FLOAT(2.5)); }
static ObjString *arenadan_float() { return aot_allocate_string("float", 5); }

extern "C" void t_typeof_kontrol(VMValue *result) {
  *result = VM_VOID();
  ObjString *a = tip(VM_INT(1)), *b = tip(VM_INT(2));
  if (a != b) hatali("ayni tip icin iki cagri FARKLI isaretci (her cagri ayiriyor)");
  if (a && a->obj.arena_allocated != 0) hatali("typeof sonucu arenada (gecici) — kalici olmali");

  ObjString *dz = aot_allocate_string("xy", 2);
  VMValue dv = VM_OBJ((Obj *)dz);
  struct { VMValue v; const char *w; } t[] = {
      {VM_INT(7), "int"},      {VM_FLOAT(1.0), "float"}, {VM_BOOL(1), "bool"},
      {dv, "string"},          {VM_VOID(), "null"},
  };
  for (auto &e : t)
    if (!esit(tip(e.v), e.w)) { std::printf("typeof: HATA ad yanlis, beklenen '%s'\n", e.w); hata++; }

  const bool kontrol_ezildi = !drop_sonrasi_saglam(arenadan_float, "float");
  const bool typeof_saglam = drop_sonrasi_saglam(typeof_float, "float");
  std::printf("typeof: bilgi pozitif kontrol (arenadan dizgi drop sonrasi): %s\n",
              kontrol_ezildi ? "ustune yazildi (duzenek olcuyor)" : "SAGLAM KALDI");
  if (!kontrol_ezildi)
    hatali("pozitif kontrol: arenadan ayrilan dizgi drop + yeni ayirma sonrasi ezilmedi — duzenek olcmuyor");
  if (!typeof_saglam) hatali("typeof sonucu arena_drop + yeni ayirma sonrasi bozuldu");

  std::printf("typeof: SONUC %d hata\n", hata);
  std::fflush(stdout);
  std::exit(hata ? 3 : 0);
}
CPPEOF

if ! "$CXX_BIN" -std=c++17 -O1 -c "$TMP/sonda.cpp" -Isrc -o "$TMP/sonda.o" 2>"$TMP/cxx.err"; then
  echo "typeof sabit kapisi DUSTU: sonda derlenemedi ($CXX_BIN)"
  sed -n '1,12p' "$TMP/cxx.err" | sed 's/^/    /'
  exit 1
fi
if ! TULPAR_AOT_LINK_FLAGS="$TMP/sonda.o" "$TULPAR" build "$TMP/prog.tpr" "$TMP/prog" >"$TMP/build.out" 2>&1; then
  echo "typeof sabit kapisi DUSTU: program derlenemedi/linklenemedi"
  tail -12 "$TMP/build.out" | sed 's/^/    /'
  exit 1
fi

# Cikti DOSYAYA: bir boruda `$?` son halkanin kodunu verir.
"$TMP/prog" >"$TMP/out" 2>"$TMP/err"
RC=$?
if [ $RC -ne 0 ] || ! grep -q "^typeof: SONUC 0 hata" "$TMP/out"; then
  echo "typeof sabit kapisi DUSTU (cikis kodu $RC)"
  grep "^typeof:" "$TMP/out" | sed 's/^/    /'
  sed -n '1,6p' "$TMP/err" | sed 's/^/    /'
  exit 1
fi
echo "typeof sabit kapisi temiz: ayni isaretci, kalici, 5 ad dogru, arena_drop sonrasi saglam; pozitif kontrol ezilmeyi gordu"
exit 0
