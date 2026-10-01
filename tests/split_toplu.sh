#!/bin/bash
# split() TOPLU YOL KAPISI — parcalar gercekten TEK arena ayirmasinda mi?
#
# 2026-10-01: split() parca basina ayirmadan (strstr + gecici malloc/strncpy/
# free + parca basina arena ayirmasi + ikiye katlanarak buyuyen dizi) iki
# gecisli toplu yola gecti. Olculdu (benchmarks/fair/parse, 5M parca, Ryzen 7
# 9800X3D): split 112 -> 57 ms; kucuk sayfa hatasi 85 251 -> 4 618 (tek
# surekli bolge, THP 2 MB sayfalarla dolduruyor).
#
# Anlambilim tests/split_toplu.test.tpr'de; bu kapi yalniz MEKANIZMAYI olcer:
# TULPAR_SPLIT_TANI=1 iken runtime her split icin parca sayisini ve tek arena
# ayirmasinin boyunu stderr'e basar. Kapi beklenen boyu KENDISI hesaplar
# (parca basina 8'e yuvarlanmis sizeof(ObjString) + uzunluk + NUL) ve
# karsilastirir. Parca basina ayirmaya donulurse satir kaybolur ya da boy
# tutmaz -> KIRMIZI.
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

# sizeof(ObjString): 64-bit'te 56. Kapi 64-bit hedeflerde kosuyor (CI: Linux
# x86_64, macOS arm64, Windows x86_64); baska bir genislikte boy farkli olur.
S=56
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

# Kontrol: anahtar kapaliyken sessiz.
SESSIZ=$("$TMP/prog" 2>&1 >/dev/null | grep -c "split-tani" || true)
[ "$SESSIZ" -eq 0 ] || { echo "  anahtar KAPALIYKEN $SESSIZ tani satiri basildi"; HATA=1; }

if [ "$HATA" -ne 0 ]; then
  echo "split toplu kapisi DUSTU — gelen tani:"; echo "$TANI" | sed 's/^/    /'
  exit 1
fi
echo "split toplu kapisi: GECTI (3 split, her biri tek arena ayirmasi: $BEK_A/$BEK_B/$BEK_C bayt; anahtar kapaliyken sessiz)"
