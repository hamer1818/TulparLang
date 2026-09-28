#!/bin/bash
# `tulpar build` BAYRAK KAPISI — `build`'den sonra yazilan bayraklar gercekten
# etki ediyor mu, ve onbellek derleme kipini karistiriyor mu?
#
# NEDEN (olculdu 2026-09-27):
#   * `tulpar build --strict x.tpr out`  -> cikis 0, ikili uretildi. Plan
#     03'un kendi ornegi (`aborting build`) bu yazimla HIC calismiyordu;
#     yalniz `tulpar --strict build ...` calisiyordu.
#   * `tulpar build --debug x.tpr out`   -> SIFIR .debug_* bolumu. Plan 07'nin
#     kendi yazimi (`tulpar build --debug`) DWARF'siz ikili birakiyordu.
#   Sebep: src/main.cpp'deki bayrak dongusu `build`'de duruyor, konumsal
#   dongu de '-' ile baslayani SESSIZCE atliyordu.
#   * Onbellek yalniz mtime'a bakiyordu: debug'siz ikiliden sonra `--debug`
#     "Cache hit" alip DWARF'siz ikiliyi, debug ikilisinden sonra duz derleme
#     de OPTIMIZASYONSUZ ikiliyi birakiyordu.
#
# Kapi uc platformda da kosuyor (bash + suruc ciktisi). DWARF'in VARLIGI
# platform aracina bagli: ELF'te readelf (.debug_info), Mach-O'da nm'in hata
# ayiklama haritasi (OSO), PE'de ise BIZIM DW_AT_producer dizgimiz
# ("Tulpar AOT") — MinGW'de duz derleme de CRT'den gelen .debug_info
# tasiyor, yani PE'de bolum varligi hicbir sey soylemiyor (olculdu
# 2026-09-27, Windows CI). Arac yoksa o alt denetim GORUNUR atlanir;
# kip/onbellek denetimleri araca bagli degil.
#
# POZITIF KONTROLLER: (1) ayni dosya `--strict`'siz DERLENIYOR ve `[typecheck]`
# uyarisi basiyor — yani kirmizi, strict'ten geliyor; (2) ayni kipte ikinci
# derleme "Cache hit" ALIYOR — yani kip degisiminde isabet olmamasi kuralin
# eseri, onbellegin bozuk olmasi degil.
set -u
cd "$(dirname "$0")/.."
TULPAR="${1:-./tulpar}"
[ -x "$TULPAR" ] || [ -x "$TULPAR.exe" ] || { echo "build bayrak kapisi: $TULPAR yok"; exit 1; }

TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
FAIL=0
OK=0

gecti() { echo "  gecti  $1"; OK=$((OK + 1)); }
dustu() { echo "  DUSTU  $1"; FAIL=1; }

# ikili yolu (Windows'ta .exe eki)
exe() { if [ -f "$1.exe" ]; then echo "$1.exe"; else echo "$1"; fi; }

printf 'int n = 5;\nprint(len(n));\n' > "$TMP/uyari.tpr"
printf 'func topla(int a, int b): int {\n    int c = a + b;\n    return c;\n}\nprint(topla(2, 3));\n' > "$TMP/temiz.tpr"

# --- STRICT --------------------------------------------------------------
# Pozitif kontrol: strict'siz DERLENIYOR ve uyari basiyor.
OUT=$("$TULPAR" build "$TMP/uyari.tpr" "$TMP/kontrol" 2>&1); RC=$?
if [ $RC -eq 0 ] && echo "$OUT" | grep -q '\[typecheck\]' && [ -f "$(exe "$TMP/kontrol")" ]; then
    gecti "kontrol: strict'siz derleniyor ve [typecheck] uyarisi basiyor"
else
    dustu "kontrol: strict'siz derleme beklenmedik (rc=$RC) — asagidaki strict denetimleri hicbir sey olcmez"
    echo "$OUT" | sed -n '1,5p' | sed 's/^/      /'
fi

strict_red() {  # $1 = aciklama, geri kalan = tulpar argumanlari (cikti $TMP/s_out)
    local what="$1"; shift
    rm -f "$TMP/s_out" "$TMP/s_out.exe"
    local out rc
    out=$("$TULPAR" "$@" 2>&1); rc=$?
    if [ $rc -ne 0 ] && echo "$out" | grep -q 'strict mode); aborting build' &&
       [ ! -f "$TMP/s_out" ] && [ ! -f "$TMP/s_out.exe" ]; then
        gecti "$what -> cikis $rc, ikili yok"
    else
        dustu "$what -> cikis $rc (strict yok sayildi?)"
        echo "$out" | sed -n '1,4p' | sed 's/^/      /'
    fi
}
strict_red "tulpar --strict build"      --strict build "$TMP/uyari.tpr" "$TMP/s_out"
strict_red "tulpar build --strict"      build --strict "$TMP/uyari.tpr" "$TMP/s_out"
strict_red "tulpar build <f> <o> --strict" build "$TMP/uyari.tpr" "$TMP/s_out" --strict

# --no-typecheck build'den sonra da susturuyor.
OUT=$("$TULPAR" build --no-typecheck "$TMP/uyari.tpr" "$TMP/nt" 2>&1)
if echo "$OUT" | grep -q '\[typecheck\]'; then
    dustu "tulpar build --no-typecheck -> [typecheck] hala basiliyor"
else
    gecti "tulpar build --no-typecheck -> uyari yok"
fi

# Taninmayan bayrak SESSIZ degil.
OUT=$("$TULPAR" build --boyle-bir-bayrak-yok "$TMP/temiz.tpr" "$TMP/bb" 2>&1)
if echo "$OUT" | grep -q -- '--boyle-bir-bayrak-yok'; then
    gecti "taninmayan bayrak uyari basiyor"
else
    dustu "taninmayan bayrak SESSIZCE yutuldu"
fi

# --- DEBUG + ONBELLEK ------------------------------------------------------
# dwarf <ikili>: 1 = hata ayiklama bilgisi var, 0 = yok, 2 = olculemedi
dwarf() {
    local f; f=$(exe "$1")
    [ -f "$f" ] || { echo 2; return; }
    if head -c 4 "$f" | grep -q 'ELF' && command -v readelf >/dev/null 2>&1; then
        readelf -S "$f" 2>/dev/null | grep -q '\.debug_info' && echo 1 || echo 0
    elif [ "$(uname -s)" = "Darwin" ] && command -v nm >/dev/null 2>&1; then
        nm -ap "$f" 2>/dev/null | grep -q ' OSO ' && echo 1 || echo 0
    elif head -c 2 "$f" | grep -q 'MZ'; then
        grep -c -a 'Tulpar AOT' "$f" >/dev/null 2>&1 && echo 1 || echo 0
    else
        echo 2
    fi
}

cache_hit() { echo "$1" | grep -q 'Cache hit'; }

# 1) duz derleme
OUT=$("$TULPAR" build "$TMP/temiz.tpr" "$TMP/d" 2>&1)
D0=$(dwarf "$TMP/d")
# 2) ayni ciktiya --debug (build'den SONRA): onbellege isabet ETMEMELI
OUT=$("$TULPAR" build --debug "$TMP/temiz.tpr" "$TMP/d" 2>&1)
if cache_hit "$OUT"; then
    dustu "debug'siz ikiliden sonra 'build --debug' onbellege isabet etti (DWARF'siz ikili kaldi)"
else
    gecti "'build --debug' debug'siz ikilinin onbellegine takilmiyor"
fi
D1=$(dwarf "$TMP/d")
# 3) ardindan duz derleme: debug ikilisini BIRAKMAMALI
OUT=$("$TULPAR" build "$TMP/temiz.tpr" "$TMP/d" 2>&1)
if cache_hit "$OUT"; then
    dustu "debug ikilisinden sonra duz 'build' onbellege isabet etti (optimizasyonsuz ikili kaldi)"
else
    gecti "duz 'build' debug ikilisinin onbellegine takilmiyor"
fi
D2=$(dwarf "$TMP/d")
# 4) POZITIF KONTROL: ayni kipte ikinci derleme isabet ALMALI
OUT=$("$TULPAR" build "$TMP/temiz.tpr" "$TMP/d" 2>&1)
if cache_hit "$OUT"; then
    gecti "kontrol: ayni kipte ikinci derleme onbellege isabet ediyor"
else
    dustu "kontrol: ayni kipte ikinci derleme isabet ETMEDI — 2./3. adimlar hicbir sey kanitlamiyor"
fi
# 5) -g yazimi, ciktidan sonra
"$TULPAR" build "$TMP/temiz.tpr" "$TMP/g" -g >/dev/null 2>&1
D3=$(dwarf "$TMP/g")
# Program dogru calisiyor mu (debug ikilisi de)?
RUN=$("$(exe "$TMP/g")" 2>&1 | tr -d '\r')
[ "$RUN" = "5" ] && gecti "-g ikilisi dogru calisiyor (5)" || dustu "-g ikilisi yanlis cikti: '$RUN'"

if echo "$D0$D1$D2$D3" | grep -q 2; then
    echo "  ATLANDI DWARF varligi — bu platformda olcum araci yok (readelf / nm / objdump)"
else
    [ "$D0" = 0 ] && gecti "duz derleme: hata ayiklama bilgisi yok" || dustu "duz derleme DWARF tasiyor"
    [ "$D1" = 1 ] && gecti "'build --debug': hata ayiklama bilgisi var" || dustu "'build --debug' DWARF uretmedi (bayrak dustu)"
    [ "$D2" = 0 ] && gecti "debug'dan sonra duz derleme: DWARF yok" || dustu "debug'dan sonra duz derleme hala DWARF'li"
    [ "$D3" = 1 ] && gecti "'build <f> <o> -g': hata ayiklama bilgisi var" || dustu "'-g' DWARF uretmedi"
fi

if [ $FAIL -ne 0 ]; then
    echo "build bayrak kapisi DUSTU"
    exit 1
fi
echo "build bayrak kapisi temiz ($OK denetim: strict/no-typecheck/debug build'den sonra, kip degisiminde onbellek)"
exit 0
