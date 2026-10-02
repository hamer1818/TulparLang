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
#   2. BELLEK: 100k ve 1M parcali split'in tepe RSS farkindan parca basina bayt
#      (metin + parca nesnesi + 16 B dizi elemani). Esik 64 B: 5 haneli parca
#      yeni duzende ~54 B (6 metin + 32 nesne + 16 eleman), eski duzende
#      (Obj 32 B, ObjString 48 B) ~78 B — eski derleyicide KIRMIZI.
#      Pozitif kontrol: 13 baytlik parcalar 8 bayt daha uzun metin + 8 bayt
#      daha buyuk nesne demek; olculen parca basina fark >= 12 B olmali —
#      olmazsa kapi parca boyunu olcmuyordur.
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

# sizeof(ObjString): 64-bit'te 24 (2026-10-02'ye kadar 48 — Obj basligi 32 ->
# 8; 2026-10-01'e kadar 56 — `capacity` alani). Kapi 64-bit hedeflerde
# kosuyor (CI: Linux x86_64, macOS arm64, Windows x86_64); wasm32'de 20.
S=24
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

# Kontrol: anahtar kapaliyken sessiz.
SESSIZ=$("$TMP/prog" 2>&1 >/dev/null | grep -c "split-tani" || true)
[ "$SESSIZ" -eq 0 ] || { echo "  anahtar KAPALIYKEN $SESSIZ tani satiri basildi"; HATA=1; }

# --- 2. BELLEK -------------------------------------------------------------
cat > "$TMP/bellek.tpr" <<'TPREOF'
int n = toInt(env("SB_N"));
int uzun = toInt(env("SB_UZUN"));
var sb = StringBuilder(1024);
for (int i = 0; i < n; i = i + 1) {
    if (i > 0) { sb_append(sb, ","); }
    if (uzun > 0) { sb_append(sb, "ABCDEFGH"); }
    sb_append(sb, 10000 + i % 90000);
}
str s = sb_tostring(sb);
sb_free(sb);
array parts = split(s, ",");
int t = 0;
for (int i = 0; i < len(parts); i = i + 1) { t = t + len(parts[i]); }
print(toString(len(parts)) + " " + toString(t));
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
      v=$(SB_N=$1 SB_UZUN=$2 "$TMP/rsswrap" "$TMP/bellek" 2>&1 >/dev/null | tr -d '\r' | sed -n 's/^RSS_KB=//p')
      [ "$(uname -s)" = "Darwin" ] && [ -n "$v" ] && v=$((v / 1024))
      echo "$v"
    }
    K1=$(rss 100000 0); K2=$(rss 1000000 0)
    U1=$(rss 100000 1); U2=$(rss 1000000 1)
    for v in "$K1" "$K2" "$U1" "$U2"; do
      [ -n "$v" ] || { echo "  RSS okunamadi"; echo "split toplu kapisi DUSTU"; exit 1; }
    done
    # Cikti dogrulamasi (kapi yanlis programi olcmesin).
    KC=$(SB_N=1000000 SB_UZUN=0 "$TMP/bellek" | tr -d '\r')
    UC=$(SB_N=1000000 SB_UZUN=1 "$TMP/bellek" | tr -d '\r')
    [ "$KC" = "1000000 5000000" ] || { echo "  bellek sondasi ciktisi '$KC'"; HATA=1; }
    [ "$UC" = "1000000 13000000" ] || { echo "  bellek sondasi (uzun) ciktisi '$UC'"; HATA=1; }
    KB=$(( (K2 - K1) * 1024 / 900000 ))
    UB=$(( (U2 - U1) * 1024 / 900000 ))
    echo "  bellek: 5 baytlik parca basina ${KB} B (esik 64; 100k ${K1} KB, 1M ${K2} KB)"
    echo "  pozitif kontrol: 13 baytlik parca basina ${UB} B (fark $((UB - KB)) B, en az 12 olmali)"
    [ "$KB" -lt 64 ] || { echo "  PARCA BASINA ${KB} B — temsil buyumus (esik 64)"; HATA=1; }
    [ $((UB - KB)) -ge 12 ] || { echo "  POZITIF KONTROL: uzun parca farki $((UB - KB)) B — kapi parca boyunu olcmuyor"; HATA=1; } ;;
esac

if [ "$HATA" -ne 0 ]; then
  echo "split toplu kapisi DUSTU — gelen tani:"; echo "$TANI" | sed 's/^/    /'
  exit 1
fi
echo "split toplu kapisi: GECTI (3 split, her biri tek arena ayirmasi: $BEK_A/$BEK_B/$BEK_C bayt, kuyruk iade; anahtar kapaliyken sessiz)"
