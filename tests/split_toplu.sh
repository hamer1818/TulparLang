#!/bin/bash
# split() TOPLU YOL KAPISI — parcalar gercekten TEK arena ayirmasinda mi, ve
# parca basina ne kadar bellek tutuyor?
#
# 2026-10-01: split() parca basina ayirmadan (strstr + gecici malloc/strncpy/
# free + parca basina arena ayirmasi + ikiye katlanarak buyuyen dizi) iki
# gecisli toplu yola gecti. Olculdu (benchmarks/fair/parse, 5M parca, Ryzen 7
# 9800X3D): split 112 -> 57 ms; kucuk sayfa hatasi 85 251 -> 4 618 (tek
# surekli bolge, THP 2 MB sayfalarla dolduruyor).
#
# 2026-10-02: Obj basligi 32 -> 8 bayt (ObjString 48 -> 24) ve tek baytlik
# ayiricida parca boylari toplanmadan UST SINIRLA ayrilip artan kuyruk iade
# ediliyor (aot_arena_trim_last). parse 374 -> 260 MB.
#
# Anlambilim tests/split_toplu.test.tpr'de; bu kapi MEKANIZMAYI ve BELLEGI
# olcer:
#   1. TULPAR_SPLIT_TANI=1 iken runtime her split icin parca sayisini, tek
#      arena ayirmasinin (iadeden sonraki) boyunu ve kuyrugun gercekten iade
#      edilip edilmedigini stderr'e basar. Kapi beklenen boyu KENDISI hesaplar
#      (parca basina 8'e yuvarlanmis sizeof(ObjString) + uzunluk + NUL) ve
#      karsilastirir. Parca basina ayirmaya donulurse satir kaybolur ya da boy
#      tutmaz; iade bozulursa "kuyruk iade hayir" -> KIRMIZI.
#   2. BELLEK: ayni 1M parcalik metin, split'li ve split'siz iki kosum; tepe
#      RSS farki / parca = split'in parca basina maliyeti (nesne + 16 B dizi
#      elemani; metin iki kosumda ortak ve duser). Metin `repeat` ile TEK
#      seferde kurulur (arena, serbest birakilmaz): StringBuilder'in buyuyen
#      ve birakilan tamponu macOS'ta split'e yeniden verilip olcumu
#      bozuyordu (ilk surum: 5 bayt 61 B, 13 bayt 69 B — fark 8, beklenen 16).
#      Esik 45 B: 5 baytlik parca 40 B (24 nesne + 16; 2026-10-02 ikinci
#      adim — karakterler nesnenin icinde, ObjString 24 -> 16 B), onceki
#      duzende 48 B (32 + 16), daha oncesinde 72 B — ikisi de KIRMIZI.
#      Pozitif kontrol: 21 baytlik parca nesneyi 16 B buyutur (8'e
#      yuvarlanmis 16+22 = 40); olculen fark >= 10 B olmali — olmazsa kapi
#      parca boyunu olcmuyordur.
#
# 2026-10-06: parca TABLOSU kutusuz dizgi deposu (ARR_ELEM_STR): eleman
# basina 16 B VMValue yerine 8 B `ObjString *`. 5 baytlik parca 40 -> 32 B
# (24 nesne + 8). Tani satiri `depo dizgi` der; TULPAR_NO_STRARR=1 iken
# `depo kutulu` (anahtar okunuyor) ve bellek ayagi ayni kosumda iki depoyu
# olcer: fark parca basina >= 6 B olmali (beklenen 8) — olmazsa kapi depoyu
# olcmuyordur. Esik 37 B: eski kutulu tablo (40 B) KIRMIZI.
#
# Kapinin kendi kontrolu: anahtar KAPALIYKEN hicbir satir basilmamali (anahtar
# gercekten okunuyor, tani varsayilan olarak ciktiyi kirletmiyor).
set -u
cd "$(dirname "$0")/.."
TULPAR="${1:-./tulpar}"
[ -x "$TULPAR" ] || { echo "split toplu kapisi: $TULPAR yok — atlandi"; exit 0; }

TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT

cat > "$TMP/prog.tpr" <<'TPREOF'
array a = split("a,bb,ccc,,dddd", ",");
array b = split("0123456789abcdef::x", "::");
array c = split("xyz", "");
print(len(a) + len(b) + len(c));
TPREOF

if ! "$TULPAR" build "$TMP/prog.tpr" "$TMP/prog" >"$TMP/derle.log" 2>&1; then
  echo "split toplu kapisi DUSTU: sonda derlenmedi"; cat "$TMP/derle.log"; exit 1
fi

# sizeof(ObjString): 16, her hedefte (2026-10-02 ikinci adim: karakterler
# nesnenin icinde, `chars` isaretcisi yok; ondan once 24 — Obj basligi 32 ->
# 8; 2026-10-02'ye kadar 48; 2026-10-01'e kadar 56 — `capacity` alani).
S=16
hiz() { echo $(( (S + $1 + 1 + 7) / 8 * 8 )); }
BEK_A=$(( $(hiz 1) + $(hiz 2) + $(hiz 3) + $(hiz 0) + $(hiz 4) ))
BEK_B=$(( $(hiz 16) + $(hiz 1) ))
BEK_C=$(( 3 * $(hiz 1) ))

OUT=$(TULPAR_SPLIT_TANI=1 "$TMP/prog" 2>"$TMP/tani.err")
KOD=$?
TANI=$(tr -d '\r' < "$TMP/tani.err")
HATA=0
[ "$KOD" -eq 0 ] || { echo "  sonda cikis kodu $KOD"; HATA=1; }
[ "$(echo "$OUT" | tr -d '\r')" = "10" ] || { echo "  sonda ciktisi '$OUT' (beklenen 10)"; HATA=1; }
for BEK in "5 parca, tek arena ayirmasi $BEK_A bayt" \
           "2 parca, tek arena ayirmasi $BEK_B bayt" \
           "3 parca, tek arena ayirmasi $BEK_C bayt"; do
  if ! echo "$TANI" | grep -qF "split-tani: $BEK"; then
    echo "  beklenen tani satiri yok: split-tani: $BEK"; HATA=1
  fi
done
N=$(echo "$TANI" | grep -c "^split-tani:")
[ "$N" -eq 3 ] || { echo "  3 tani satiri bekleniyordu, $N geldi"; HATA=1; }
# Kuyruk iadesi: tek baytlik ayiricili split (a) ust sinirla ayirdi; artan
# geri verilmis olmali. Oteki ikisi tam boyla ayiriyor (yine "evet").
NI=$(echo "$TANI" | grep -c "kuyruk iade evet")
[ "$NI" -eq 3 ] || { echo "  kuyruk iadesi: 3 'evet' bekleniyordu, $NI geldi"; HATA=1; }
# Ust sinir gercekten kullaniliyor mu (a'nin satirinda ust sinir > boy).
UA=$(echo "$TANI" | sed -n "s/^split-tani: 5 parca, tek arena ayirmasi $BEK_A bayt (ust sinir \([0-9]*\),.*/\1/p")
[ -n "$UA" ] && [ "$UA" -gt "$BEK_A" ] || { echo "  tek baytlik ayiricida ust sinir ($UA) kullanilmamis"; HATA=1; }

# Depo: uc split de kutusuz dizgi deposunda; TULPAR_NO_STRARR=1 kutuluya cevirir.
ND=$(grep -c "depo dizgi)" <<< "$TANI")
[ "$ND" -eq 3 ] || { echo "  depo: 3 'depo dizgi' bekleniyordu, $ND geldi (kutusuz dizgi deposu devre disi?)"; HATA=1; }
TANI_K=$(TULPAR_SPLIT_TANI=1 TULPAR_NO_STRARR=1 "$TMP/prog" 2>&1 >/dev/null | tr -d '\r')
NK=$(grep -c "depo kutulu)" <<< "$TANI_K")
[ "$NK" -eq 3 ] || { echo "  TULPAR_NO_STRARR=1: 3 'depo kutulu' bekleniyordu, $NK geldi (anahtar okunmuyor)"; HATA=1; }

# Kontrol: anahtar kapaliyken sessiz.
SESSIZ=$("$TMP/prog" 2>&1 >/dev/null | grep -c "split-tani" || true)
[ "$SESSIZ" -eq 0 ] || { echo "  anahtar KAPALIYKEN $SESSIZ tani satiri basildi"; HATA=1; }

# --- 2. BELLEK -------------------------------------------------------------
cat > "$TMP/bellek.tpr" <<'TPREOF'
int n = toInt(env("SB_N"));
int uzun = toInt(env("SB_UZUN"));
int bol = toInt(env("SB_BOL"));
str birim = "12345,";
if (uzun > 0) { birim = "ABCDEFGHIJKLMNOP12345,"; }
str s = repeat(birim, n);
if (bol > 0) {
    array parts = split(s, ",");
    int t = 0;
    for (int i = 0; i < len(parts); i = i + 1) { t = t + len(parts[i]); }
    print(toString(len(parts)) + " " + toString(t));
} else {
    print(len(s));
}
TPREOF
case "$(uname -s)" in
  MINGW*|MSYS*|CYGWIN*)
    echo "  [bellek ayagi ATLANDI: Windows'ta fork/wait4 yok — rsswrap kosamiyor]" ;;
  *)
    CC_BIN="${CC:-}"
    if [ -z "$CC_BIN" ]; then
      for c in cc gcc clang; do command -v "$c" >/dev/null 2>&1 && { CC_BIN="$c"; break; }; done
    fi
    if [ -z "$CC_BIN" ] || ! "$CC_BIN" -O2 benchmarks/fair/rsswrap.c -o "$TMP/rsswrap" 2>/dev/null; then
      echo "split toplu kapisi DUSTU: rsswrap derlenemedi (C derleyicisi?)"; exit 1
    fi
    if ! "$TULPAR" build "$TMP/bellek.tpr" "$TMP/bellek" >"$TMP/derle2.log" 2>&1; then
      echo "split toplu kapisi DUSTU: bellek sondasi derlenmedi"; cat "$TMP/derle2.log"; exit 1
    fi
    # KB; macOS ru_maxrss BAYT.
    rss() {
      local v
      v=$(SB_N=1000000 SB_UZUN=$1 SB_BOL=$2 TULPAR_NO_STRARR=${3:-0} "$TMP/rsswrap" "$TMP/bellek" 2>&1 >/dev/null | tr -d '\r' | sed -n 's/^RSS_KB=//p')
      [ "$(uname -s)" = "Darwin" ] && [ -n "$v" ] && v=$((v / 1024))
      echo "$v"
    }
    K0=$(rss 0 0); K1=$(rss 0 1); KN=$(rss 0 1 1)
    U0=$(rss 1 0); U1=$(rss 1 1)
    for v in "$K0" "$K1" "$KN" "$U0" "$U1"; do
      [ -n "$v" ] || { echo "  RSS okunamadi"; echo "split toplu kapisi DUSTU"; exit 1; }
    done
    # Cikti dogrulamasi (kapi yanlis programi olcmesin). Sondaki ayirici
    # bos bir son parca verir: 1M + 1 parca.
    KC=$(SB_N=1000000 SB_UZUN=0 SB_BOL=1 "$TMP/bellek" | tr -d '\r')
    UC=$(SB_N=1000000 SB_UZUN=1 SB_BOL=1 "$TMP/bellek" | tr -d '\r')
    [ "$KC" = "1000001 5000000" ] || { echo "  bellek sondasi ciktisi '$KC'"; HATA=1; }
    [ "$UC" = "1000001 21000000" ] || { echo "  bellek sondasi (uzun) ciktisi '$UC'"; HATA=1; }
    KB=$(( (K1 - K0) * 1024 / 1000000 ))
    UB=$(( (U1 - U0) * 1024 / 1000000 ))
    NB=$(( (KN - K0) * 1024 / 1000000 ))
    echo "  bellek: 5 baytlik parca basina ${KB} B (esik 37; split'siz ${K0} KB, split'li ${K1} KB)"
    echo "  pozitif kontrol: 21 baytlik parca basina ${UB} B (fark $((UB - KB)) B, en az 10 olmali)"
    echo "  pozitif kontrol: kutulu tablo (TULPAR_NO_STRARR=1) parca basina ${NB} B (fark $((NB - KB)) B, en az 6 olmali)"
    [ "$KB" -lt 37 ] || { echo "  PARCA BASINA ${KB} B — temsil buyumus (esik 37)"; HATA=1; }
    [ $((NB - KB)) -ge 6 ] || { echo "  POZITIF KONTROL: kutulu/kutusuz tablo farki $((NB - KB)) B — kapi depoyu olcmuyor"; HATA=1; }
    [ $((UB - KB)) -ge 10 ] || { echo "  POZITIF KONTROL: uzun parca farki $((UB - KB)) B — kapi parca boyunu olcmuyor"; HATA=1; } ;;
esac

if [ "$HATA" -ne 0 ]; then
  echo "split toplu kapisi DUSTU — gelen tani:"; echo "$TANI" | sed 's/^/    /'
  exit 1
fi
echo "split toplu kapisi: GECTI (3 split, her biri tek arena ayirmasi: $BEK_A/$BEK_B/$BEK_C bayt, kuyruk iade; anahtar kapaliyken sessiz)"
