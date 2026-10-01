#!/bin/bash
# GECICI DIZGI KAPISI — sozluk anahtari ve toString birlestirmesi bellek
# biriktiriyor mu?
#
# 2026-10-01: benchmarks/fair hashmap (1M ekleme + 1M arama, anahtar
# "k" + toString(i)) 439 MB tepe bellek olcuyordu (C 65 MB). Uc kaynak:
#   * `"k" + toString(i)` iki arena dizgisi (toString + birlestirme);
#   * anahtar dizgisi aramadan sonra hic geri alinmiyordu (1M arama = 64 MB);
#   * vm_object_set anahtari once arenaya, kalici kapta bir de malloc'a
#     kopyaliyordu — anahtar ZATEN VARKEN de.
# Ayrica ust duzey `str s = sb_tostring(sb)` metni iki kez tutuyordu (arena +
# kalici kopya; parse cekirdeginde 29 MB).
#
# Ayaklar (her biri tepe RSS FARKI: ayni sonda kucuk ve buyuk N ile; taban
# surec bellegi farkta duser):
#   1. ARAMA: 1000 anahtarli sozlukte N arama. 2,9M ek arama < 16 MB
#      (eski yol ~354 MB: arama basina iki 64 B arena dizgisi).
#   2. EKLEME: N anahtar iki kez yazilir (ikinci tur MEVCUT anahtar). Anahtar
#      basina < 192 B (eski yol ~330 B: toString + birlestirme + anahtarin
#      arena kopyasi + malloc kopyasi + tablo).
#   3. METIN: ust duzey `str s = sb_tostring(sb)`, 30 MB metin. N=100k ile
#      3M arasindaki fark metnin 2,5 katindan az olmali (eski yol: sb tamponu
#      + arena kopyasi + kalici kopya ~3x).
#   4. IR: arama/ekleme gecici anahtar yardimcilarindan geciyor
#      (aot_get_element_tmpkey / aot_set_element_tmpkey), `"k" + toString(i)`
#      icin aot_to_string_ptr cagrisi YOK.
#   5. POZITIF KONTROL: ayni ARAMA sondasi TULPAR_NO_TMPKEY=1
#      TULPAR_NO_TOSTR_FUSE=1 ile (iki mekanizma kapali) derlenince 1. ayagin
#      esigini ASMALI — asmiyorsa kapi bir sey olcmuyor demektir.
# Fork/wait4 gerektiren ayaklar Windows'ta GORUNUR atlanir.
set -u
cd "$(dirname "$0")/.."
TULPAR="${1:-./tulpar}"
[ -x "$TULPAR" ] || { echo "gecici dizgi kapisi: $TULPAR yok — atlandi"; exit 0; }

TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
HATA=0

cat > "$TMP/ara.tpr" <<'TPREOF'
json m = {};
for (int i = 0; i < 1000; i = i + 1) { m["k" + toString(i)] = i; }
int n = toInt(env("GD_N"));
int s = 0;
for (int i = 0; i < n; i = i + 1) { s = s + m["k" + toString(i % 1000)]; }
print(s);
TPREOF

cat > "$TMP/ekle.tpr" <<'TPREOF'
func doldur(json m, int n) {
    for (int i = 0; i < n; i = i + 1) { m["k" + toString(i)] = i; }
}
json m = {};
int n = toInt(env("GD_N"));
doldur(m, n);
doldur(m, n);
print(len(m));
TPREOF

cat > "$TMP/metin.tpr" <<'TPREOF'
int n = toInt(env("GD_N"));
var sb = StringBuilder(1024);
for (int i = 0; i < n; i = i + 1) { sb_append(sb, "abcdefghij"); }
str s = sb_tostring(sb);
sb_free(sb);
print(len(s));
TPREOF

for p in ara ekle metin; do
  if ! TULPAR_AOT_EMIT_LL=1 "$TULPAR" build "$TMP/$p.tpr" "$TMP/$p" >"$TMP/$p.log" 2>&1; then
    echo "gecici dizgi kapisi DUSTU: $p sondasi derlenmedi"; cat "$TMP/$p.log"; exit 1
  fi
done
if ! TULPAR_NO_TMPKEY=1 TULPAR_NO_TOSTR_FUSE=1 "$TULPAR" build "$TMP/ara.tpr" "$TMP/ara_kapali" >"$TMP/k.log" 2>&1; then
  echo "gecici dizgi kapisi DUSTU: kapali sonda derlenmedi"; cat "$TMP/k.log"; exit 1
fi

# Cikti dogrulugu (bellek olcumu yanlis programi olcmesin).
OUT=$(GD_N=5000 "$TMP/ara" | tr -d '\r')
[ "$OUT" = "2497500" ] || { echo "  ara ciktisi '$OUT' (beklenen 2497500)"; HATA=1; }
OUT=$(GD_N=5000 "$TMP/ekle" | tr -d '\r')
[ "$OUT" = "5000" ] || { echo "  ekle ciktisi '$OUT' (beklenen 5000)"; HATA=1; }
OUT=$(GD_N=1000 "$TMP/metin" | tr -d '\r')
[ "$OUT" = "10000" ] || { echo "  metin ciktisi '$OUT' (beklenen 10000)"; HATA=1; }

# Kullanici `toString` tanimlarsa yerlesik golgelenir: birlestirme kisa yolu
# DEVREYE GIRMEMELI (kullanicinin fonksiyonu cagrilmali).
cat > "$TMP/golge.tpr" <<'TPREOF'
func toString(x): str { return "U"; }
json m = {};
m["a" + toString(5)] = 1;
print("a" + toString(5) + " " + toString(m["aU"]));
TPREOF
if ! "$TULPAR" build "$TMP/golge.tpr" "$TMP/golge" >"$TMP/g.log" 2>&1; then
  echo "gecici dizgi kapisi DUSTU: golge sondasi derlenmedi"; cat "$TMP/g.log"; exit 1
fi
OUT=$("$TMP/golge" | tr -d '\r')
[ "$OUT" = "aU U" ] || { echo "  kullanici toString'i golgelenmedi: '$OUT' (beklenen 'aU U')"; HATA=1; }

# --- 4. IR ---------------------------------------------------------------
for p in ara ekle; do
  LL="$TMP/$p.ll"
  if [ ! -f "$LL" ]; then echo "  IR dokumu yok ($LL)"; HATA=1; continue; fi
  NTS=$(grep -c "call .*@aot_to_string_ptr(" "$LL" || true)
  [ "$NTS" -eq 0 ] || { echo "  $p: '\"k\" + toString(i)' hala aot_to_string_ptr cagiriyor ($NTS)"; HATA=1; }
done
NG=$(grep -c "call .*@aot_get_element_tmpkey(" "$TMP/ara.ll" 2>/dev/null || true)
NS=$(grep -c "call .*@aot_set_element_tmpkey(" "$TMP/ekle.ll" 2>/dev/null || true)
[ "${NG:-0}" -ge 1 ] || { echo "  ara: gecici anahtarli okuma uretilmedi"; HATA=1; }
[ "${NS:-0}" -ge 1 ] || { echo "  ekle: gecici anahtarli yazma uretilmedi"; HATA=1; }

# --- 1/2/3/5. BELLEK -----------------------------------------------------
case "$(uname -s)" in
  MINGW*|MSYS*|CYGWIN*)
    echo "  [bellek ayaklari ATLANDI: Windows'ta fork/wait4 yok — rsswrap kosamiyor]" ;;
  *)
    CC_BIN="${CC:-}"
    if [ -z "$CC_BIN" ]; then
      for c in cc gcc clang; do command -v "$c" >/dev/null 2>&1 && { CC_BIN="$c"; break; }; done
    fi
    if [ -z "$CC_BIN" ] || ! "$CC_BIN" -O2 benchmarks/fair/rsswrap.c -o "$TMP/rsswrap" 2>/dev/null; then
      echo "gecici dizgi kapisi DUSTU: rsswrap derlenemedi (C derleyicisi?)"; exit 1
    fi
    # KB; macOS ru_maxrss BAYT.
    rss() {
      local v
      v=$(GD_N=$2 "$TMP/rsswrap" "$TMP/$1" 2>&1 >/dev/null | sed -n 's/^RSS_KB=//p')
      [ "$(uname -s)" = "Darwin" ] && [ -n "$v" ] && v=$((v / 1024))
      echo "$v"
    }
    A1=$(rss ara 100000); A2=$(rss ara 3000000)
    E1=$(rss ekle 100000); E2=$(rss ekle 1000000)
    M1=$(rss metin 100000); M2=$(rss metin 3000000)
    K1=$(rss ara_kapali 100000); K2=$(rss ara_kapali 3000000)
    for v in "$A1" "$A2" "$E1" "$E2" "$M1" "$M2" "$K1" "$K2"; do
      [ -n "$v" ] || { echo "  RSS okunamadi"; echo "gecici dizgi kapisi DUSTU"; exit 1; }
    done
    AF=$(( (A2 - A1) / 1024 ))
    EB=$(( (E2 - E1) * 1024 / 900000 ))
    MF=$(( (M2 - M1) / 1024 ))
    KF=$(( (K2 - K1) / 1024 ))
    echo "  arama: N=100k ${A1} KB, N=3M ${A2} KB, fark ${AF} MB (esik 16)"
    echo "  ekleme: N=100k ${E1} KB, N=1M ${E2} KB, anahtar basina ${EB} B (esik 192)"
    echo "  metin: 1 MB ${M1} KB, 30 MB ${M2} KB, fark ${MF} MB (esik 72 = 29 MB metnin 2,5 kati)"
    echo "  pozitif kontrol (iki mekanizma kapali): arama farki ${KF} MB (esigi ASMALI)"
    [ "$AF" -lt 16 ] || { echo "  ARAMA bellek biriktiriyor: ${AF} MB"; HATA=1; }
    [ "$EB" -lt 192 ] || { echo "  EKLEME anahtar basina ${EB} B"; HATA=1; }
    [ "$MF" -lt 72 ] || { echo "  METIN iki kez tutuluyor: ${MF} MB"; HATA=1; }
    [ "$KF" -ge 16 ] || { echo "  POZITIF KONTROL: mekanizmalar kapaliyken de fark ${KF} MB — kapi olcmuyor"; HATA=1; } ;;
esac

if [ "$HATA" -ne 0 ]; then
  echo "gecici dizgi kapisi DUSTU"
  exit 1
fi
echo "gecici dizgi kapisi: GECTI (arama ayirmasiz, ekleme tek kopya, metin tek kopya, IR yardimcilari var, pozitif kontrol kirmizi)"
