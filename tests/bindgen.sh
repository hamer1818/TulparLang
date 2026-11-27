#!/usr/bin/env bash
# BINDGEN KAPISI (2026-11-26) — `tulpar bindgen` C basligindan dogru bildirim
# uretiyor mu, ve uretilen bildirimle GERCEK C kodu dogru cagriliyor mu?
#
#   1. Fikstur (tests/bindgen/bg_ornek.h): gurultu makrolari, extern "C",
#      typedef zincirleri (virgullu), opak struct tutamaci, enum, cagri kurali
#      makrolu fonksiyon isaretcisi typedef'i, satir ici isaretci parametresi,
#      C _Bool (cbool), `T dizi[]`, nesne-bicimli tip makrosu, BASKA dosyada
#      tanimli tip (#include takibi). Uretilen bildirim beklenenle BAYT BAYT;
#      atlananlar gerekceleriyle (variadic, struct deger, long double,
#      fonksiyon donduren); icerilen basligin fonksiyonu BAGLANMAZ.
#   2. Uretilen bildirimle fikstur kitapligi derlenip cagrilir (qsort + baglamli
#      geri cagri + cbool iki yonde) — cikti beklenenle bayt bayt.
#   3. Gercek kitapliklar (varsa; yoksa GORUNUR atlanir): zlib — zlibVersion()
#      Python'un gordugu surumle ayni; SDL3 — SDL'in KENDI zamanlayici
#      thread'i Tulpar geri cagrisini cagiriyor, cbool donusleri dogru.
# Kontroller: --no-includes ile ayni baslik `bg_kimlik`i "bilinmeyen tip
# bg_u32" diye atlar (include takibi yuk tasiyor); fikstur basliginda bir
# imza degisirse bildirim farki kirmizi.
set -uo pipefail
cd "$(dirname "$0")/.."
TUL="$(pwd)/${1:-tulpar}"
[ -x "$TUL" ] || TUL="$(pwd)/tulpar"
SRC="$(pwd)/tests/bindgen"
export LC_ALL=C
unset TULPAR_EXT_PATH
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
fail=0
gecti() { printf "  \033[0;32mgecti\033[0m  %s\n" "$1"; }
dustu() { printf "  \033[0;31mDUSTU\033[0m  %s\n" "$1"; fail=1; }
CC_BIN=""; for c in cc clang gcc; do command -v "$c" >/dev/null 2>&1 && { CC_BIN="$c"; break; }; done
AR_BIN=""; for a in ar llvm-ar; do command -v "$a" >/dev/null 2>&1 && { AR_BIN="$a"; break; }; done

# 1) bildirim
mkdir -p "$TMP/ext/lib"
(cd "$SRC/../.." && "$TUL" bindgen tests/bindgen/bg_ornek.h --lib bg_ornek --lib-dir lib -o "$TMP/ext/tulpar-ext.json") 2>"$TMP/rapor"; rc=$?
if [ $rc -eq 0 ] && diff -u --strip-trailing-cr "$SRC/bg_ornek.beklenen.json" "$TMP/ext/tulpar-ext.json" >"$TMP/fark"; then
  gecti "bg_ornek.h -> bildirim bayt bayt beklenen (20 fonksiyon)"
else
  dustu "bildirim farkli (rc=$rc)"; head -20 "$TMP/fark" | sed 's/^/         /'
fi
if diff -u --strip-trailing-cr "$SRC/bg_ornek.beklenen.tpr" "$TMP/ext/bg_ornek.tpr" >"$TMP/fark_m"; then
  gecti "K304: degerle gecen struct'lar icin @repr(C) modulu bayt bayt (BgNokta, etiketli BgEtiket)"
else
  dustu "struct modulu farkli"; head -12 "$TMP/fark_m" | sed 's/^/         /'
fi
for g in "bg_yazdir: variadic" "bg_kisa: parametre 1: struct BgKisa degerle gecemez: alan 'a'" \
         "bg_hassas: donus: long double" "bg_isaretci_donduren: fonksiyon isaretcisi"; do
  grep -qF "atlandi  $g" "$TMP/rapor" && gecti "gerekceli atlandi: ${g%%:*}" || dustu "rapor satiri yok: $g"
done
grep -q "bg_tipler_icindeki_fonksiyon" "$TMP/ext/tulpar-ext.json" \
  && dustu "icerilen basligin fonksiyonu baglandi" || gecti "icerilen basligin fonksiyonu baglanmadi (yalniz tipleri)"
(cd "$SRC/../.." && "$TUL" bindgen tests/bindgen/bg_ornek.h --no-includes) >/dev/null 2>"$TMP/rapor2"
grep -q "atlandi  bg_kimlik: .*bilinmeyen tip 'bg_u32'" "$TMP/rapor2" \
  && gecti "kontrol: --no-includes ile bg_kimlik bilinmeyen tip (include takibi yuk tasiyor)" \
  || { dustu "kontrol: --no-includes farki gorunmedi"; tail -4 "$TMP/rapor2" | sed 's/^/         /'; }

# 2) uretilen bildirimle cagri
if [ -n "$CC_BIN" ] && [ -n "$AR_BIN" ]; then
  "$CC_BIN" -std=c99 -O2 -I "$SRC" -c "$SRC/bg_ornek.c" -o "$TMP/ext/lib/bg.o" && "$AR_BIN" rcs "$TMP/ext/lib/libbg_ornek.a" "$TMP/ext/lib/bg.o"
  "$TUL" --ext "$TMP/ext" "$SRC/bg_kullan.tpr" >"$TMP/o" 2>"$TMP/e"; rc=$?
  if [ $rc -eq 0 ] && diff -u --strip-trailing-cr "$SRC/bg_kullan.beklenen" "$TMP/o" >"$TMP/fark2"; then
    gecti "uretilen bildirimle cagrilar dogru (qsort, baglamli cb, cbool, enum, typedef zinciri, struct deger)"
  else
    dustu "cagri ciktisi farkli (rc=$rc)"; head -12 "$TMP/fark2" "$TMP/e" | sed 's/^/         /'
  fi
else
  dustu "C derleyicisi/ar yok"
fi

# 3) gercek kitapliklar
if [ -f /usr/include/zlib.h ] && command -v python3 >/dev/null && python3 -c "import zlib" 2>/dev/null; then
  mkdir -p "$TMP/z"
  "$TUL" bindgen /usr/include/zlib.h --name zlib --lib z -o "$TMP/z/tulpar-ext.json" 2>"$TMP/zr"
  printf 'print(zlibVersion());\nprint(adler32(1, 0, 0));\nprint(crc32(0, 0, 0));\n' > "$TMP/z.tpr"
  out=$("$TUL" --ext "$TMP/z" "$TMP/z.tpr" 2>&1)
  bek="$(python3 -c 'import zlib;print(zlib.ZLIB_RUNTIME_VERSION)')
1
0"
  [ "$out" = "$bek" ] && gecti "gercek zlib ($(grep -o '[0-9]* fonksiyon baglandi' "$TMP/zr")): surum Python ile ayni, adler32/crc32 dogru" \
    || { dustu "gercek zlib"; echo "$out" | head -4 | sed 's/^/         /'; }
else
  echo "  ATLANDI zlib (baslik ya da python3 zlib yok)"
fi
sdl_lib=""
for f in /usr/lib/libSDL3.so /usr/lib64/libSDL3.so /usr/lib/*/libSDL3.so /usr/local/lib/libSDL3.so /opt/homebrew/lib/libSDL3.dylib; do
  [ -e "$f" ] && { sdl_lib="$f"; break; }
done
# Gercek libc: div/ldiv/lldiv struct DEGER dondurur (K304).
if [ -f /usr/include/stdlib.h ] && [ "$(uname -s)" = "Linux" ]; then
  mkdir -p "$TMP/cstd"
  "$TUL" bindgen /usr/include/stdlib.h --name cstd --prefix div --prefix ldiv --prefix lldiv \
    -o "$TMP/cstd/tulpar-ext.json" 2>"$TMP/cr"
  python3 - "$TMP/cstd/tulpar-ext.json" <<'PYEOF'
import json, sys
p = sys.argv[1]; m = json.load(open(p))
m["link"] = {"linux": {"libs": []}, "macos": {"libs": []}, "windows": {"libs": []}}
json.dump(m, open(p, "w"))
PYEOF
  printf 'import "cstd";\ndiv_t a = div(17, 5);\nldiv_t b = ldiv(-5000000007, 1000);\nlldiv_t c = lldiv(9000000000000000007, 10);\nprint(toString(a.quot) + " " + toString(a.rem) + " " + toString(b.quot) + " " + toString(b.rem) + " " + toString(c.quot) + " " + toString(c.rem));\n' > "$TMP/cstd.tpr"
  out=$("$TUL" --ext "$TMP/cstd" "$TMP/cstd.tpr" 2>&1)
  [ "$out" = "3 2 -5000000 -7 900000000000000000 7" ] \
    && gecti "gercek libc: div/ldiv/lldiv struct DEGER donusu dogru" \
    || { dustu "gercek libc div: '$out'"; tail -3 "$TMP/cr" | sed 's/^/         /'; }
fi
if [ -f /usr/include/SDL3/SDL_timer.h ] && [ -n "$sdl_lib" ]; then
  mkdir -p "$TMP/sdl"
  "$TUL" bindgen /usr/include/SDL3/SDL_timer.h --name sdl_timer --prefix SDL_ --lib SDL3 -o "$TMP/sdl/tulpar-ext.json" 2>/dev/null
  cat > "$TMP/sdl.tpr" <<'T'
int g_tik = 0;
func tik(ud, id, aralik) { atomic_add(g_tik, 1); return aralik; }
print(SDL_GetPerformanceFrequency() > 0);
print(SDL_RemoveTimer(424242));
int id = SDL_AddTimer(10, tik, 0);
SDL_Delay(200);
print(atomic_load(g_tik) >= 3);
print(SDL_RemoveTimer(id));
T
  out=$(DISPLAY= WAYLAND_DISPLAY= timeout 30 "$TUL" --ext "$TMP/sdl" "$TMP/sdl.tpr" 2>&1)
  [ "$out" = "true
false
true
true" ] && gecti "gercek SDL3: zamanlayici thread'i Tulpar geri cagrisini cagiriyor, cbool donusleri dogru" \
    || { dustu "gercek SDL3"; echo "$out" | head -6 | sed 's/^/         /'; }
else
  echo "  ATLANDI SDL3 (baslik ya da kitaplik yok)"
fi

[ $fail -eq 0 ] && echo "bindgen kapisi: temiz" || { echo "bindgen kapisi DUSTU"; exit 1; }
