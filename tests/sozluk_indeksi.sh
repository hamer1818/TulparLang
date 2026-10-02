#!/bin/bash
# SOZLUK INDEKSI KAPISI — json nesnesinin hash indeksi buyurken ne kadar
# bellek tutuyor ve anahtarlari yeniden hash'liyor mu?
#
# 2026-10-02: benchmarks/fair hashmap (1M "k<i>" anahtari: ekleme + arama)
# Tulpar 106 ms / 88 MB, C 64 ms / 64 MB. Indeks her buyumede kapasiteyi
# anahtar sayisinin DORT katina cikariyordu (1M anahtarda 4M yuva = 32 MB,
# doluluk 0,24) ve butun anahtarlari bastan hash'liyordu (anahtar basina
# keys[p] -> ObjString -> chars isaretci kovalamasi). Simdi buyuk tabloda
# (1M yuva ustu) iki kat, kucukte eskisi gibi dort kat; buyumede eski
# yuvalar hash'leriyle TASINIYOR. Ayrinti: runtime_bindings.cpp "BUYUME
# (2026-10-02)", docs/mindmap/Performance.md.
#
# Ayaklar:
#   1. TANI (her platform): TULPAR_OBJ_TANI=1 surec sonunda
#      "obj-tani: buyume=B tasinan=T yeniden_hash=H enbuyuk_yuva=Y" basar.
#        * 1M anahtar -> Y = 2 097 152 (>= 2N'nin en kucuk 2 kuvveti; eski
#          kural 4 194 304), H < 64 (yalniz ilk kurulus: 16 anahtar; eski
#          yol her buyumede butun anahtarlari hash'lerdi), T > 0.
#        * 300k anahtar -> Y = 1 048 576 (kucuk tabloda x4 korunuyor: x2
#          buyume 300k'de eklemeyi %26 yavaslatmisti).
#      Eski derleyicide satir YOK -> kirmizi.
#   2. BELLEK (Linux'ta iddia; macOS'ta yalniz basilir — malloc/realloc
#      davranisi platform verisi): 100k ile 1M anahtar arasindaki tepe RSS
#      farki anahtar basina < 73 B. Hesap: dizgi 24 B (16 baslik + "k123456"
#      + NUL; karakterler nesnenin icinde, #454) + keys/values dizisi ~24,5 B
#      (kapasite 131072 -> 1048576, 8 + 16 B) + indeks (2M - 256k yuva) * 8 B
#      / 900k ~ 16 B = ~64 B (olculen 64). Eski kural indekste ~33 B -> ~82 B
#      (#454'ten once, 24 B dizgi basligiyla 90 B). Esik ikisinin ortasinda.
#   3. POZITIF KONTROL: ayni sondalar TULPAR_OBJ_INDEKS_X4=1 ile (eski x4
#      buyume) Y = 4 194 304 basmali ve Linux'ta bellek esigini ASMALI —
#      asmiyorsa kapi bir sey olcmuyor demektir.
# Cikti dogrulugu her sondada denetlenir. Fork/wait4 gerektiren bellek ayagi
# Windows'ta GORUNUR atlanir; tani ayagi orada da kosar.
set -u
cd "$(dirname "$0")/.."
TULPAR="${1:-./tulpar}"
[ -x "$TULPAR" ] || { echo "sozluk indeksi kapisi: $TULPAR yok — atlandi"; exit 0; }

TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
HATA=0

cat > "$TMP/ekle.tpr" <<'TPREOF'
int n = toInt(env("SI_N"));
json m = {};
for (int i = 0; i < n; i = i + 1) { m["k" + toString(i)] = i; }
int s = 0;
for (int i = 0; i < n; i = i + 1) { s = s + m["k" + toString((i * 7) % n)]; }
print(toString(len(m)) + " " + toString(s));
TPREOF

if ! "$TULPAR" build "$TMP/ekle.tpr" "$TMP/ekle" >"$TMP/b.log" 2>&1; then
  echo "sozluk indeksi kapisi DUSTU: sonda derlenmedi"; cat "$TMP/b.log"; exit 1
fi

# --- 1/3. TANI -------------------------------------------------------------
# tani <N> [X4] -> "Y H T" (bulunamazsa bos); cikti dogrulugu da burada.
tani() {
  local out
  out=$(SI_N=$1 TULPAR_OBJ_TANI=1 TULPAR_OBJ_INDEKS_X4=${2:-0} "$TMP/ekle" 2>"$TMP/tani.err" | tr -d '\r')
  local n=$1
  local beklenen="$n $((n * (n - 1) / 2))"
  if [ "$out" != "$beklenen" ]; then
    echo "  N=$n cikti '$out' (beklenen '$beklenen')" >&2
    HATA=1
  fi
  local satir
  satir=$(tr -d '\r' < "$TMP/tani.err" | grep '^obj-tani:' | tail -1)
  [ -n "$satir" ] || { echo ""; return; }
  local y h t
  y=$(echo "$satir" | sed -n 's/.*enbuyuk_yuva=\([0-9]*\).*/\1/p')
  h=$(echo "$satir" | sed -n 's/.*yeniden_hash=\([0-9]*\).*/\1/p')
  t=$(echo "$satir" | sed -n 's/.*tasinan=\([0-9]*\).*/\1/p')
  echo "$y $h $t"
}

R1=$(tani 1000000)
R2=$(tani 300000)
R3=$(tani 1000000 1)
if [ -z "$R1" ] || [ -z "$R2" ] || [ -z "$R3" ]; then
  echo "  obj-tani satiri yok (TULPAR_OBJ_TANI calismiyor / eski runtime)"
  HATA=1
else
  read -r Y1 H1 T1 <<<"$R1"
  read -r Y2 H2 T2 <<<"$R2"
  read -r Y3 H3 T3 <<<"$R3"
  echo "  tani 1M: enbuyuk_yuva=$Y1 yeniden_hash=$H1 tasinan=$T1 (beklenen 2097152, < 64, > 0)"
  echo "  tani 300k: enbuyuk_yuva=$Y2 (beklenen 1048576)"
  echo "  pozitif kontrol (x4): enbuyuk_yuva=$Y3 (beklenen 4194304)"
  [ "$Y1" = "2097152" ] || { echo "  1M anahtarda indeks $Y1 yuva (x2 buyume tutmadi)"; HATA=1; }
  [ "$H1" -lt 64 ] || { echo "  buyumede anahtarlar yeniden hash'leniyor: $H1"; HATA=1; }
  [ "$T1" -gt 0 ] || { echo "  buyumede yuva tasinmadi: $T1"; HATA=1; }
  [ "$Y2" = "1048576" ] || { echo "  300k anahtarda indeks $Y2 yuva (kucuk tabloda x4 tutmadi)"; HATA=1; }
  [ "$Y3" = "4194304" ] || { echo "  POZITIF KONTROL: x4 kuralinda indeks $Y3 yuva — anahtar calismiyor"; HATA=1; }
fi

# --- 2/3. BELLEK -----------------------------------------------------------
case "$(uname -s)" in
  MINGW*|MSYS*|CYGWIN*)
    echo "  [bellek ayagi ATLANDI: Windows'ta fork/wait4 yok — rsswrap kosamiyor]" ;;
  *)
    CC_BIN="${CC:-}"
    if [ -z "$CC_BIN" ]; then
      for c in cc gcc clang; do command -v "$c" >/dev/null 2>&1 && { CC_BIN="$c"; break; }; done
    fi
    if [ -z "$CC_BIN" ] || ! "$CC_BIN" -O2 benchmarks/fair/rsswrap.c -o "$TMP/rsswrap" 2>/dev/null; then
      echo "sozluk indeksi kapisi DUSTU: rsswrap derlenemedi (C derleyicisi?)"; exit 1
    fi
    # KB; macOS ru_maxrss BAYT.
    rss() {
      local v
      v=$(SI_N=$1 TULPAR_OBJ_INDEKS_X4=${2:-0} "$TMP/rsswrap" "$TMP/ekle" 2>&1 >/dev/null | sed -n 's/^RSS_KB=//p')
      [ "$(uname -s)" = "Darwin" ] && [ -n "$v" ] && v=$((v / 1024))
      echo "$v"
    }
    A1=$(rss 100000); A2=$(rss 1000000)
    K1=$(rss 100000 1); K2=$(rss 1000000 1)
    for v in "$A1" "$A2" "$K1" "$K2"; do
      [ -n "$v" ] || { echo "  RSS okunamadi"; echo "sozluk indeksi kapisi DUSTU"; exit 1; }
    done
    AB=$(( (A2 - A1) * 1024 / 900000 ))
    KB=$(( (K2 - K1) * 1024 / 900000 ))
    echo "  bellek: N=100k ${A1} KB, N=1M ${A2} KB, anahtar basina ${AB} B (esik 73)"
    echo "  pozitif kontrol (x4): anahtar basina ${KB} B (esigi ASMALI)"
    if [ "$(uname -s)" = "Linux" ]; then
      [ "$AB" -lt 73 ] || { echo "  INDEKS anahtar basina ${AB} B"; HATA=1; }
      [ "$KB" -ge 73 ] || { echo "  POZITIF KONTROL: x4 kuralinda da ${KB} B — kapi olcmuyor"; HATA=1; }
    else
      echo "  [bellek esigi yalniz Linux'ta iddia; burada basildi]"
    fi ;;
esac

if [ "$HATA" -ne 0 ]; then
  echo "sozluk indeksi kapisi DUSTU"
  exit 1
fi
echo "sozluk indeksi kapisi: GECTI (1M anahtarda 2M yuva, buyumede yeniden hash yok, kucukte x4, pozitif kontrol kirmizi)"
