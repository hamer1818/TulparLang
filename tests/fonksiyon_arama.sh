#!/bin/bash
# aot_func_lookup KAPISI — bir Tulpar fonksiyonu ADIYLA, cagirmadan ve
# AYIRMADAN cozulebiliyor mu?
#
# Tuketici: tulpar-engine. Motor her betik kancasini (`<taban>_guncelle`, ...)
# eskiden `call()` yoluyla (aot_call_dynamic_n) cagiriyordu ve o yol HER
# CAGRIDA adi bir ObjString'e kopyalamayi istiyor. Dizgi AOT arenasina gider,
# arena hic sifirlanmaz: olculdu (2026-09-25, RTX 5080 masaustu, motorda 200
# bos kanca) kare basina +14.4 KB kalici buyume, cagri basina 72 bayt.
# aot_func_lookup kancayi YUKLEMEDE bir kez cozer; motor isaretciyi ve
# aritesini saklar.
#
# Neyi olcuyor (hepsi uretilen ikilinin ICINDEN, gercek runtime ile):
#   1. kayitli fonksiyon -> onbellekteki isaretci + KAYITLI arite (2 / 0 / 1)
#   2. donen isaretci CAGRILABILIR: ikili(2, 40) == 42, sifir() == 42,
#      tipli(7.0) == 21 (tipli `int` parametresine float gecer — motor her
#      argumani VM_FLOAT verir, donusumu cagrilan yapar)
#   3. kayitsiz ama disari acik `t_<ad>` -> dlsym yedegi, arite -1
#   4. bilinmeyen ad / bos ad / sigmayan ad -> nullptr, arite -1
#   5. TUMU-int `func yerel(int x): int` yerel ABI'li: ciplak `yerel` sembolu
#      VAR ama arama onu DONDURMEZ (call()'in ikinci dlsym'i dondururdu ve
#      kutulu imzayla cagirirdi — isaretci ABI'si karisirdi)
#   6. AYIRMA YOK (glibc: mallinfo2): 400 000 arama sonrasi yigin degismez.
#      Olcegin POZITIF KONTROLU: ayni sayac, eski `call()` yolunun (cagri
#      basina yeni dizgi) buyumesini GORMELI — goremiyorsa olcek kordur.
#
# KAPININ KENDI POZITIF KONTROLU: sonda ARAMA_BOZ=1 ile "ikili" yerine
# "sifir"i arar. Kapi o kosumu KIRMIZI gormek ZORUNDA; yesil gorurse
# denetimler hicbir sey olcmuyor demektir ve kapi duser.
set -u
cd "$(dirname "$0")/.."
TULPAR="${1:-./tulpar}"
[ -x "$TULPAR" ] || { echo "fonksiyon arama kapisi: $TULPAR yok — atlandi"; exit 0; }
case "$(uname -s)" in
  MINGW*|MSYS*|CYGWIN*)
    # Tek tuketici (tulpar-engine motor_derleyici.sh) Windows'ta kurulmuyor ve
    # sonda nesnesinin PE'de disari acilmasi burada OLCULMEDI. Sessiz degil:
    echo "fonksiyon arama kapisi: ATLANDI (Windows: sonda nesnesi PE'de olculmedi; tuketici motor koprusu Windows'ta kurulmuyor)"
    exit 0 ;;
esac
CXX_BIN="${CXX:-}"
if [ -z "$CXX_BIN" ]; then
  for c in c++ clang++ g++; do command -v "$c" >/dev/null 2>&1 && { CXX_BIN="$c"; break; }; done
fi
[ -n "$CXX_BIN" ] || { echo "fonksiyon arama kapisi DUSTU: C++ derleyicisi yok (c++/clang++/g++)"; exit 1; }

TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT

cat > "$TMP/prog.tpr" <<'TPREOF'
// Uc kutulu sekil: iki parametre, parametresiz, tipli parametre (motorun
// `func kanca(int id)` bicimi). `yerel` TUMU-int: kutulu giris noktasi YOK,
// yalniz yerel ABI'li ciplak `yerel` sembolu var (cagri hedefi degil).
func ikili(a, b) { return a + b; }
func sifir() { return 42; }
func tipli(int x) { return x * 3; }
func yerel(int x): int { return x * 3; }
// Denetim sondanin icinde; call() onu dlsym ile bulur (t_arama_kontrol).
call("arama_kontrol");
TPREOF

cat > "$TMP/sonda.cpp" <<'CPPEOF'
#include "vm/vm.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dlfcn.h>
#if defined(__GLIBC__) && (__GLIBC__ > 2 || (__GLIBC__ == 2 && __GLIBC_MINOR__ >= 33))
#include <malloc.h>
#define SONDA_MALLINFO 1
#endif

extern "C" void *aot_func_lookup(const char *name, int *arity);
extern "C" VMValue aot_call_dynamic_n(VMValue func_name, VMValue *args, int argc);
extern "C" ObjString *vm_alloc_string_aot(void *vm, const char *chars, int length);

typedef void (*F0)(VMValue *);
typedef void (*F1)(VMValue *, VMValue *);
typedef void (*F2)(VMValue *, VMValue *, VMValue *);

static int g_hata = 0;
static void denet(bool ok, const char *ne) {
  std::printf("arama: %s %s\n", ok ? "TAMAM" : "HATA", ne);
  if (!ok) g_hata++;
}
static double sayi(const VMValue &v) {
  return IS_INT(v) ? (double)AS_INT(v) : IS_FLOAT(v) ? AS_FLOAT(v) : -1e300;
}
#ifdef SONDA_MALLINFO
static long long yigin(void) { struct mallinfo2 m = mallinfo2(); return (long long)(m.uordblks + m.hblkhd); }
#endif

// Kayitsiz ama disari acik: dlsym yedegi bunu bulmali (-rdynamic).
extern "C" __attribute__((used)) void t_disari(VMValue *r) { *r = VM_INT(7); }

extern "C" void t_arama_kontrol(VMValue *result) {
  *result = VM_VOID();
  const bool boz = std::getenv("ARAMA_BOZ") != nullptr; // kapinin pozitif kontrolu
  int ar = 99;
  void *p = aot_func_lookup(boz ? "sifir" : "ikili", &ar);
  denet(p != nullptr && ar == 2, "ikili: onbellekten, arite 2");
  if (p && ar == 2) {
    VMValue r = VM_VOID(), a = VM_INT(2), b = VM_INT(40);
    ((F2)p)(&r, &a, &b);
    denet(sayi(r) == 42.0, "ikili(2, 40) == 42");
  }
  ar = 99;
  void *p0 = aot_func_lookup("sifir", &ar);
  denet(p0 != nullptr && ar == 0 && p0 != p, "sifir: onbellekten, arite 0, ikiliden farkli isaretci");
  if (p0 && ar == 0) {
    VMValue r = VM_VOID();
    ((F0)p0)(&r);
    denet(sayi(r) == 42.0, "sifir() == 42");
  }
  ar = 99;
  void *pt = aot_func_lookup("tipli", &ar);
  denet(pt != nullptr && ar == 1, "tipli: onbellekten, arite 1");
  if (pt && ar == 1) {
    VMValue r = VM_VOID(), a = VM_FLOAT(7.0);
    ((F1)pt)(&r, &a);
    denet(sayi(r) == 21.0, "tipli(7.0) == 21 (float -> int donusumu cagrilanda)");
  }
  // Ciplak ad yedegi YOK: `yerel` sembolu var ama VMValue ABI'si degil;
  // call()'in ikinci dlsym'i onu bulur ve kutulu imzayla cagirirdi.
  ar = 99;
  denet(dlsym(RTLD_DEFAULT, "yerel") != nullptr && aot_func_lookup("yerel", &ar) == nullptr && ar == -1,
        "yerel ABI'li `yerel`: ciplak sembol var, arama nullptr (kutulu imzayla cagrilamaz)");
  ar = 99;
  void *pd = aot_func_lookup("disari", &ar);
  denet(pd == (void *)&t_disari && ar == -1, "disari: kayitsiz, dlsym yedegi, arite -1");
  ar = 99;
  denet(aot_func_lookup("yok_boyle_bir_fonksiyon", &ar) == nullptr && ar == -1, "bilinmeyen ad: nullptr, arite -1");
  ar = 99;
  denet(aot_func_lookup("", &ar) == nullptr && ar == -1 && aot_func_lookup(nullptr, nullptr) == nullptr, "bos / null ad: nullptr");
  char uzun[400];
  std::memset(uzun, 'x', sizeof uzun - 1);
  uzun[sizeof uzun - 1] = 0;
  denet(aot_func_lookup(uzun, nullptr) == nullptr, "sigmayan ad: nullptr (kirpilip baska sembol bulunmaz)");
#ifdef SONDA_MALLINFO
  {
    const int N = 100000;
    const long long y0 = yigin();
    uintptr_t iz = 0;
    for (int i = 0; i < N; i++) {
      int a1 = 0;
      iz += (uintptr_t)aot_func_lookup("ikili", &a1) + (uintptr_t)aot_func_lookup("sifir", &a1) +
            (uintptr_t)aot_func_lookup("disari", &a1) + (uintptr_t)aot_func_lookup("yok_boyle", &a1) + (uintptr_t)a1;
    }
    const long long y1 = yigin();
    // Eski yol: call() adi ObjString olarak ister -> cagri basina yeni dizgi.
    VMValue args[2] = {VM_INT(1), VM_INT(2)};
    for (int i = 0; i < N; i++) {
      ObjString *s = vm_alloc_string_aot(nullptr, "ikili", 5);
      aot_call_dynamic_n(VM_OBJ((Obj *)s), args, 2);
    }
    const long long y2 = yigin();
    std::printf("arama: bilgi %d x 4 arama: yigin %+lld bayt; %d call(): yigin %+lld bayt (iz %llu)\n", N, y1 - y0, N, y2 - y1,
                (unsigned long long)(iz & 1));
    denet(y1 - y0 == 0, "arama AYIRMIYOR (400 000 aramada yigin degismedi)");
    denet(y2 - y1 > 0, "olcek kor degil: call() yolunun cagri basina dizgisini goruyor");
  }
#else
  std::printf("arama: ATLANDI ayirma olcumu (mallinfo2 yok: glibc >= 2.33 degil)\n");
#endif
  std::printf("arama: SONUC %d hata\n", g_hata);
  std::fflush(stdout);
  if (g_hata) std::exit(3);
}
CPPEOF

if ! "$CXX_BIN" -std=c++17 -O1 -c "$TMP/sonda.cpp" -Isrc -o "$TMP/sonda.o" 2>"$TMP/cxx.err"; then
  echo "fonksiyon arama kapisi DUSTU: sonda derlenemedi ($CXX_BIN)"
  sed -n '1,12p' "$TMP/cxx.err" | sed 's/^/    /'
  exit 1
fi
if ! TULPAR_AOT_LINK_FLAGS="$TMP/sonda.o" "$TULPAR" build "$TMP/prog.tpr" "$TMP/prog" >"$TMP/build.out" 2>&1; then
  echo "fonksiyon arama kapisi DUSTU: program derlenemedi/linklenemedi"
  tail -12 "$TMP/build.out" | sed 's/^/    /'
  exit 1
fi

# Cikti DOSYAYA: bir boruda `$?` son halkanin kodunu verir (bkz. args_gecis.sh).
"$TMP/prog" >"$TMP/out" 2>"$TMP/err"
RC=$?
if [ $RC -ne 0 ] || ! grep -q "^arama: SONUC 0 hata" "$TMP/out" || grep -q "^arama: HATA" "$TMP/out"; then
  echo "fonksiyon arama kapisi DUSTU (cikis kodu $RC)"
  grep "^arama:" "$TMP/out" | sed 's/^/    /'
  sed -n '1,6p' "$TMP/err" | sed 's/^/    /'
  exit 1
fi

# Pozitif kontrol: bozulmus sonda KIRMIZI olmali.
ARAMA_BOZ=1 "$TMP/prog" >"$TMP/out_boz" 2>&1
RC_BOZ=$?
if [ $RC_BOZ -eq 0 ] || ! grep -q "^arama: HATA ikili" "$TMP/out_boz"; then
  echo "fonksiyon arama kapisi DUSTU: pozitif kontrol (ARAMA_BOZ=1) yesil gecti — denetimler bir sey olcmuyor"
  sed -n '1,8p' "$TMP/out_boz" | sed 's/^/    /'
  exit 1
fi

TAMAM=$(grep -c "^arama: TAMAM" "$TMP/out")
BILGI=$(grep "^arama: bilgi\|^arama: ATLANDI" "$TMP/out" | sed 's/^arama: //')
echo "fonksiyon arama kapisi: $TAMAM denetim temiz; pozitif kontrol (bozuk ad) kirmizi gordu (cikis $RC_BOZ)"
[ -n "$BILGI" ] && echo "  $BILGI"
exit 0
