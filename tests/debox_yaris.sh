#!/bin/bash
# arr_debox YARIS KAPISI — N thread ayni kutusuz int[]'i AYNI ANDA ilk kez
# kutulayan okuma yoluna (arr_items -> arr_debox) girdiginde dizi TEK KEZ mi
# cevriliyor?
#
# Neden var (FINDINGS T4, K130): `arr_debox` bir OKUMA yolundan tetikleniyor
# ve dizi basligina yaziyor (malloc, items_/idata/capacity). 2026-09-09'da
# cift denetimli kilit + eski tamponu serbest birakmama ile sertlestirildi;
# ama bunu TETIKLEYEN bir test yoktu (docs/mindmap/Concurrency.md "bunu
# tetikleyen bir test yok"). Kilit bir gun "gereksiz" diye sokulurse hicbir
# sey kirmizi olmazdi.
#
# Neyi olcuyor: R taze dizi (n eleman, 64-bit kutusuz). Her dizi icin T
# thread bir dondurma bariyerinde bekler, ayni anda `arr_items(a)` cagirir ve
# donen isaretciyi kaydeder. Tek cevrim = T thread'in hepsi AYNI tamponu
# gorur. Iki cevrim = en az iki farkli tampon (her cevirici kendi tamponunu
# yayinlar). Ayrica ornek elemanlar dogru mu (VM_INT(i*3+1)).
#
# POZITIF KONTROL (kapinin kendisi olcuyor mu): ayni duzenek, arr_debox'un
# KILITSIZ bir kopyasina (sertlestirmeden onceki algoritma) uygulanir ve
# CIFT CEVRIM GORMEK ZORUNDADIR. Goremezse duzenek yarisi uretemiyor demektir
# ve kapi duser — "gercek arr_debox temiz" sonucu o zaman hicbir sey olcmezdi.
# Tek cekirdekli makinede thread'ler ortusmez: kapi GORUNUR bicimde atlanir.
set -u
cd "$(dirname "$0")/.."
TULPAR="${1:-./tulpar}"
[ -x "$TULPAR" ] || { echo "debox yaris kapisi: $TULPAR yok — atlandi"; exit 0; }
case "$(uname -s)" in
  MINGW*|MSYS*|CYGWIN*)
    # MinGW'nin win32 thread modelinde std::thread yok (runtime da bu yuzden
    # platform_threads.h kullaniyor). Sessiz degil:
    echo "debox yaris kapisi: ATLANDI (Windows: sonda std::thread kullaniyor, MinGW win32 modelinde yok)"
    exit 0 ;;
esac
CXX_BIN="${CXX:-}"
if [ -z "$CXX_BIN" ]; then
  for c in c++ clang++ g++; do command -v "$c" >/dev/null 2>&1 && { CXX_BIN="$c"; break; }; done
fi
[ -n "$CXX_BIN" ] || { echo "debox yaris kapisi DUSTU: C++ derleyicisi yok (c++/clang++/g++)"; exit 1; }

TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT

cat > "$TMP/prog.tpr" <<'TPREOF'
// Denetim sondanin icinde; call() onu dlsym ile bulur (t_debox_kontrol).
call("debox_kontrol");
TPREOF

cat > "$TMP/sonda.cpp" <<'CPPEOF'
#include "vm/vm.hpp"
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>
#include <vector>

static const int R = 60;       // taze dizi sayisi
static const int N = 100000;   // eleman: cevrim ~100 us, ortusme penceresi genis
static int T = 8;              // thread

static ObjArray *yeni_dizi() {
  ObjArray *a = static_cast<ObjArray *>(calloc(1, sizeof(ObjArray)));
  a->obj.type = OBJ_ARRAY;
  a->obj.ref_count = 1;
  a->count = N;
  a->capacity = N;
  a->elem_bits = 64;
  a->idata = static_cast<long long *>(malloc(sizeof(long long) * N));
  for (int i = 0; i < N; i++) a->idata[i] = (long long)i * 3 + 1;
  return a;
}

// arr_debox'un 2026-09-09 sertlestirmesinden ONCEKI sekli: kilit yok.
// (Kaynak isaretcisi bir kez okunuyor ki kontrol kosumu null deref ile
// cokmesin; olculen sey cift cevrim, cokme degil.)
static void debox_kilitsiz(ObjArray *a) {
  if (!a->idata) return;
  const long long *src = a->idata;
  VMValue *boxed = static_cast<VMValue *>(malloc(sizeof(VMValue) * N));
  for (int i = 0; i < N; i++) boxed[i] = VM_INT(src[i]);
  a->items_ = boxed;
  a->idata = nullptr;
}

static VMValue *gercek(ObjArray *a) { return arr_items(a); }
static VMValue *kilitsiz(ObjArray *a) { debox_kilitsiz(a); return a->items_; }

// Donus: cift cevrim goren dizi sayisi; *yanlis: yanlis eleman goren thread.
static int kos(VMValue *(*fn)(ObjArray *), int *yanlis) {
  int cift = 0;
  *yanlis = 0;
  for (int r = 0; r < R; r++) {
    ObjArray *a = yeni_dizi();
    std::atomic<int> hazir{0};
    std::atomic<bool> basla{false};
    std::vector<VMValue *> gordu(T, nullptr);
    std::vector<int> hata(T, 0);
    std::vector<std::thread> th;
    for (int t = 0; t < T; t++) {
      th.emplace_back([&, t] {
        hazir.fetch_add(1);
        while (!basla.load(std::memory_order_acquire)) { }
        VMValue *it = fn(a);
        gordu[t] = it;
        for (int k = 0; k < N; k += 997)
          if (!it || !IS_INT(it[k]) || AS_INT(it[k]) != (long long)k * 3 + 1) { hata[t] = 1; break; }
      });
    }
    while (hazir.load() < T) { }
    basla.store(true, std::memory_order_release);
    for (auto &x : th) x.join();
    for (int t = 1; t < T; t++)
      if (gordu[t] != gordu[0]) { cift++; break; }
    for (int t = 0; t < T; t++) *yanlis += hata[t];
  }
  return cift;
}

extern "C" void t_debox_kontrol(VMValue *result) {
  *result = VM_VOID();
  unsigned hw = std::thread::hardware_concurrency();
  if (hw < 2) {
    std::printf("debox: ATLANDI tek cekirdek (hardware_concurrency=%u) — thread'ler ortusmez\n", hw);
    std::fflush(stdout);
    std::exit(0);
  }
  if ((int)hw < T) T = (int)hw < 2 ? 2 : (int)hw;
  int y_k = 0, y_g = 0;
  const int c_k = kos(kilitsiz, &y_k);
  const int c_g = kos(gercek, &y_g);
  std::printf("debox: bilgi %d thread x %d dizi x %d eleman: kilitsiz kopya %d/%d dizide cift cevrim, gercek arr_debox %d/%d\n",
              T, R, N, c_k, R, c_g, R);
  int hata = 0;
  if (c_k == 0) { std::printf("debox: HATA pozitif kontrol: kilitsiz kopyada cift cevrim GORULMEDI — duzenek yarisi uretmiyor\n"); hata++; }
  if (c_g != 0) { std::printf("debox: HATA gercek arr_debox %d dizide birden fazla kez cevirdi\n", c_g); hata++; }
  if (y_g != 0) { std::printf("debox: HATA gercek arr_debox'ta %d thread yanlis eleman gordu\n", y_g); hata++; }
  std::printf("debox: SONUC %d hata\n", hata);
  std::fflush(stdout);
  std::exit(hata ? 3 : 0);
}
CPPEOF

if ! "$CXX_BIN" -std=c++17 -O1 -pthread -c "$TMP/sonda.cpp" -Isrc -o "$TMP/sonda.o" 2>"$TMP/cxx.err"; then
  echo "debox yaris kapisi DUSTU: sonda derlenemedi ($CXX_BIN)"
  sed -n '1,12p' "$TMP/cxx.err" | sed 's/^/    /'
  exit 1
fi
if ! TULPAR_AOT_LINK_FLAGS="$TMP/sonda.o -pthread" "$TULPAR" build "$TMP/prog.tpr" "$TMP/prog" >"$TMP/build.out" 2>&1; then
  echo "debox yaris kapisi DUSTU: program derlenemedi/linklenemedi"
  tail -12 "$TMP/build.out" | sed 's/^/    /'
  exit 1
fi

# Cikti DOSYAYA: bir boruda `$?` son halkanin kodunu verir.
"$TMP/prog" >"$TMP/out" 2>"$TMP/err"
RC=$?
if grep -q "^debox: ATLANDI" "$TMP/out"; then
  echo "debox yaris kapisi: $(grep '^debox: ATLANDI' "$TMP/out" | sed 's/^debox: //')"
  exit 0
fi
if [ $RC -ne 0 ] || ! grep -q "^debox: SONUC 0 hata" "$TMP/out"; then
  echo "debox yaris kapisi DUSTU (cikis kodu $RC)"
  grep "^debox:" "$TMP/out" | sed 's/^/    /'
  sed -n '1,6p' "$TMP/err" | sed 's/^/    /'
  exit 1
fi
echo "debox yaris kapisi: gercek arr_debox tek cevrim; pozitif kontrol (kilitsiz kopya) cift cevrim gordu"
grep "^debox: bilgi" "$TMP/out" | sed 's/^debox: /  /'
exit 0
