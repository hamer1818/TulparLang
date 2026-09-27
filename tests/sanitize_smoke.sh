#!/bin/bash
# `tulpar build --sanitize=address` KAPISI (K166).
#
# NEDEN: `TULPAR_AOT_LINK_FLAGS=-fsanitize=address` yalniz ASan RUNTIME'ini
# baglıyordu; URETILEN kod enstrumante edilmedigi icin Tulpar'in kendi
# yigin/yigit erisimleri hic denetlenmiyordu (Tuzaklar 6o sinifi). Bayrak
# artik IR'i da enstrumante ediyor (`sanitize_address` + LLVM `asan`).
#
# NE OLCULUYOR
#   1. IR: sanitize'li derlemede `sanitize_address` + `__asan_report`
#      cagrilari VAR; duz derlemede YOK (pozitif/negatif cift).
#   2. Calisma: sanitize'li ikili dogru ciktiyi verip 0 ile cikiyor (sizinti
#      denetimi varsayilan KAPALI — arena + olumsuz kalicilar LSan'a sizinti
#      gorunur) ve ASan runtime'i GERCEKTEN yuklu: `ASAN_OPTIONS=help=1`
#      bayrak listesini basiyor.
#   3. Onbellek: kip degisince isabet YOK; ayni kipte ikinci derleme isabet
#      ALIYOR (pozitif kontrol).
#   4. Hata yollari: desteklenmeyen tur (rc 2), `build` disinda kullanim.
# Windows (MinGW) GORUNUR atlanir: ASan orada desteklenmiyor.
set -u
cd "$(dirname "$0")/.."
TULPAR="${1:-./tulpar}"
TULPAR="$(cd "$(dirname "$TULPAR")" && pwd)/$(basename "$TULPAR")"
case "$(uname -s)" in
    MINGW*|MSYS*|CYGWIN*) echo "sanitize kapisi ATLANDI: Windows/MinGW'de ASan yok"; exit 0 ;;
esac
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
FAIL=0; OK=0
gecti() { echo "  gecti  $1"; OK=$((OK + 1)); }
dustu() { echo "  DUSTU  $1"; FAIL=1; }

cat > "$TMP/p.tpr" <<'T'
func topla(array a): int {
    int s = 0;
    for (int i = 0; i < len(a); i = i + 1) { s = s + a[i]; }
    return s;
}
array xs = [3, 4, 5];
json j = {"ad": "tulpar"};
print(toString(topla(xs)) + ":" + toString(j["ad"]));
T

# 1) IR
( cd "$TMP" && TULPAR_AOT_EMIT_LL=1 "$TULPAR" build p.tpr duz > duz.log 2>&1 )
mv "$TMP/duz.ll" "$TMP/duz_ir.ll" 2>/dev/null
( cd "$TMP" && TULPAR_AOT_EMIT_LL=1 "$TULPAR" build --sanitize=address p.tpr asan > asan.log 2>&1 )
if [ ! -f "$TMP/asan.ll" ] || [ ! -f "$TMP/duz_ir.ll" ]; then
    dustu "IR dosyalari uretilmedi"; sed -n '1,5p' "$TMP/asan.log" | sed 's/^/      /'
else
    if grep -q 'sanitize_address' "$TMP/asan.ll" && grep -q '__asan_report' "$TMP/asan.ll"; then
        gecti "sanitize'li IR enstrumante (sanitize_address + __asan_report)"
    else
        dustu "sanitize'li IR'da enstrumantasyon YOK"
    fi
    if grep -q '__asan_report\|sanitize_address' "$TMP/duz_ir.ll"; then
        dustu "duz derlemede ASan izi var (negatif kontrol)"
    else
        gecti "duz derlemede enstrumantasyon yok (negatif kontrol)"
    fi
fi

# 2) Calisma
OUT=$("$TMP/asan" 2>"$TMP/asan_run.err"); RC=$?
if [ $RC -eq 0 ] && [ "$OUT" = "12:tulpar" ]; then
    gecti "sanitize'li ikili dogru cikti, cikis 0 (sizinti denetimi varsayilan kapali)"
else
    dustu "sanitize'li ikili: rc=$RC cikti='$OUT'"; tail -5 "$TMP/asan_run.err" | sed 's/^/      /'
fi
if ASAN_OPTIONS=help=1 "$TMP/asan" 2>&1 | grep -q 'AddressSanitizer'; then
    gecti "ASan runtime'i yuklu (ASAN_OPTIONS=help=1 bayrak listesi)"
else
    dustu "ASan runtime'i YUKLU DEGIL — IR enstrumante ama baglanmamis?"
fi

# 3) Onbellek
cp "$TMP/p.tpr" "$TMP/q.tpr"
( cd "$TMP" && "$TULPAR" build q.tpr q > /dev/null 2>&1 )
O=$(cd "$TMP" && "$TULPAR" build --sanitize=address q.tpr q 2>&1)
echo "$O" | grep -q 'Cache hit' && dustu "duz ikiliden sonra --sanitize onbellege isabet etti" \
    || gecti "--sanitize duz ikilinin onbellegine takilmiyor"
O=$(cd "$TMP" && "$TULPAR" build q.tpr q 2>&1)
echo "$O" | grep -q 'Cache hit' && dustu "ASan ikilisinden sonra duz derleme isabet etti (ASan'li ikili kaldi)" \
    || gecti "duz derleme ASan ikilisinin onbellegine takilmiyor"
O=$(cd "$TMP" && "$TULPAR" build q.tpr q 2>&1)
echo "$O" | grep -q 'Cache hit' && gecti "kontrol: ayni kipte ikinci derleme isabet ediyor" \
    || dustu "kontrol: ayni kipte ikinci derleme isabet ETMEDI — onceki iki adim bir sey kanitlamiyor"

# 4) Hata yollari
( cd "$TMP" && LC_ALL=C "$TULPAR" build --sanitize=thread p.tpr t > t.log 2>&1 ); RC=$?
[ $RC -eq 2 ] && grep -q 'only --sanitize=address' "$TMP/t.log" \
    && gecti "desteklenmeyen sanitizer: rc 2 + mesaj" || dustu "desteklenmeyen sanitizer rc=$RC"
( cd "$TMP" && LC_ALL=C "$TULPAR" --sanitize p.tpr > r.log 2>&1 ); RC=$?
[ $RC -ne 0 ] && grep -q "only works with 'tulpar build'" "$TMP/r.log" \
    && gecti "build disinda --sanitize: net hata" || dustu "build disinda --sanitize rc=$RC"

if [ $FAIL -ne 0 ]; then echo "sanitize kapisi DUSTU"; exit 1; fi
echo "sanitize kapisi temiz ($OK denetim: IR enstrumantasyonu, ASan runtime, onbellek, hata yollari)"
