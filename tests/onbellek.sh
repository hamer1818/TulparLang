#!/usr/bin/env bash
# DERLEME ONBELLEGI KAPISI — davranis ayagi (2026-10-05).
#
# NIYE: `tulpar build`'in onbellegi mtime'a bakiyordu ve kod uretimini
# degistiren ortam degiskenlerini, runtime arsivini, paket-yerel kardes
# import'lari, mtime'i geri alinmis icerigi GORMUYORDU. Olculdu (2026-10-05,
# v3.39.6): TULPAR_NO_FVER=1 ile ikinci build "Cache hit" deyip float surumlu
# ESKI ikiliyi birakti — o anahtari acip kapatarak A/B yapan her kapi
# (tests/*.sh) TULPAR_AOT_NOCACHE=1'i hatirlamak zorundaydi; unutan hicbir
# sey olcmuyordu. `tulpar dosya.tpr` ise hic onbellek kullanmiyordu.
# Anahtar artik icerik adresli (src/aot/aot_cache.hpp); bu kapi onu olcer:
#
#   1. ISABET: ayni girdiyle ikinci kosu derlemeyi atliyor ve OLCULEBILIR
#      hizli (sure once/sonra basilir).
#   2. ISKA + DOGRU YENI CIKTI: kaynakta tek karakter; yerel import;
#      paket-yerel kardes import (eski onbellegin gormedigi); mtime'i geri
#      alinmis icerik; TULPAR_NO_FVER (IR'de fv.el var/yok — kodgeni
#      gercekten degistiriyor); eklenti arsivi (mtime geri alinmis dahil);
#      runtime arsivi (mtime geri alinmis dahil); tulpar.toml; import
#      edilen bir modulun kendi disk importu (iki kademe "lib/..." yolu).
#   3. ORTAM: anahtar disi (TULPAR_ENGINE_*) isabeti bozmaz; bilinmeyen bir
#      TULPAR_* iska verir (guvenli yon: hepsi anahtarda).
#   4. GOZLEM: TULPAR_AOT_EMIT_LL / TULPAR_AOT_TIME onbellegi atlatir (.ll
#      yazilir, sure basilir).
#   5. TANI: derlemenin stderr'i (64'ten fazla struct uyarisi) isabette
#      AYNEN yeniden basilir.
#   6. OZ DENETIM POZITIF KONTROLU: TULPAR_CACHE_SINAMA=tarama-yok import
#      taramasini atlar -> kodgenin okudugu dosya anahtarda yok -> sonuc
#      YAYIMLANMAMALI (ikinci kosu da iska). Yayimlanirsa oz denetim olcmuyor.
#   7. PARALEL: ayni programi 8 surec ayni anda derleyip calistirinca hepsi
#      dogru cikti verir; depoda TEK girdi, artik gecici dosya yok.
#   8. TAVAN: TULPAR_CACHE_MAX_MB asilinca eski girdiler silinir (LRU).
#   9. TULPAR_AOT_NOCACHE=1 hicbir sey yazmaz; `tulpar cache clean` siler.
#  10. build yolu: isabet, ikili baska bir dosyayla degisince iska.
#
# Onbellek kok dizini gecici (TULPAR_CACHE_DIR): kullanicinin onbellegine
# dokunulmaz, kapi da onun icerigine bagli degildir.
#
#   tests/onbellek.sh [tulpar_yolu]
set -uo pipefail
cd "$(dirname "$0")/.."
ROOT=$(pwd)
TUL="${1:-./tulpar}"
[ -x "$TUL" ] || [ -x "$TUL.exe" ] || { echo "onbellek kapisi: $TUL yok"; exit 1; }
TUL="$(cd "$(dirname "$TUL")" && pwd)/$(basename "$TUL")"
export LC_ALL=C
for v in TULPAR_AOT_NOCACHE TULPAR_EXT_PATH TULPAR_AOT_TIME TULPAR_AOT_EMIT_LL \
         TULPAR_AOT_EMIT_LL_PRE TULPAR_AOT_VERBOSE TULPAR_AOT_STRIP_DEBUG TULPAR_DBG_VER \
         TULPAR_PERF_HINTS TULPAR_CACHE_RAPOR TULPAR_CACHE_SINAMA TULPAR_CACHE_MAX_MB \
         TULPAR_NO_FVER; do
    unset "$v"
done
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
EXE=""
WIN=0
case "$(uname -s)" in MINGW*|MSYS*|CYGWIN*) EXE=".exe"; WIN=1 ;; esac
yerel() { if [ $WIN -eq 1 ] && command -v cygpath >/dev/null 2>&1; then cygpath -m "$1"; else printf '%s' "$1"; fi; }
ONB="$TMP/onb"
export TULPAR_CACHE_DIR="$(yerel "$ONB")"

fail=0
gecti() { printf "  \033[0;32mgecti\033[0m  %s\n" "$1"; }
dustu() { printf "  \033[0;31mDUSTU\033[0m  %s\n" "$1"; fail=1; }
atla()  { printf "  \033[0;33mATLANDI\033[0m  %s\n" "$1"; }

simdi_ms() {
    if [ -n "${EPOCHREALTIME:-}" ]; then local t=${EPOCHREALTIME/[.,]/}; echo $((10#$t / 1000))
    elif command -v python3 >/dev/null 2>&1; then python3 -c 'import time; print(int(time.time()*1000))'
    else echo $(( $(date +%s) * 1000 )); fi
}

# kos <dizin> [AD=DEGER ...] -- <tulpar argumanlari>
#   -> OUT (stdout), ERR (stderr, [onbellek] satirlari dahil), RC
kos() {
    local dir=$1; shift
    local envs=()
    while [ $# -gt 0 ] && [ "$1" != "--" ]; do envs+=("$1"); shift; done
    shift
    OUT=$(cd "$dir" && env TULPAR_CACHE_RAPOR=1 ${envs[@]+"${envs[@]}"} "$TUL" "$@" 2>"$TMP/err.txt")
    RC=$?
    OUT=$(tr -d '\r' <<<"$OUT")
    ERR=$(tr -d '\r' < "$TMP/err.txt")
}
isabet() { grep -q '^\[onbellek\] isabet' <<<"$ERR"; }
iska()   { grep -q '^\[onbellek\] iska' <<<"$ERR"; }
kapali() { grep -q '^\[onbellek\] kapali' <<<"$ERR"; }
karar()  { if isabet; then echo isabet; elif iska; then echo iska; elif kapali; then echo kapali; else echo "?"; fi; }
girdi_sayisi() { ls "$ONB/run" 2>/dev/null | grep -cE '^[0-9a-f]{32}(\.exe)?$'; }

# beklenen <aciklama> <isabet|iska> <cikti>
beklenen() {
    local what=$1 k=$2 o=$3 got
    got=$(karar)
    if [ "$got" = "$k" ] && [ "$RC" -eq 0 ] && [ "$OUT" = "$o" ]; then
        gecti "$what -> $k, cikti '$o'"
    else
        dustu "$what -> karar '$got' (beklenen $k), rc=$RC, cikti '$OUT' (beklenen '$o')"
        sed -n '1,6p' <<<"$ERR" | sed 's/^/         /'
    fi
}

mkdir -p "$TMP/a"
A="$TMP/a"

# ---- 1. ISABET + HIZ ---------------------------------------------------------
cat > "$A/agir.tpr" <<'T'
import "wings";
func saglik(req) { return ok({"durum": "ayakta"}); }
get("/saglik", "saglik");
print("agir tamam");
T
t0=$(simdi_ms); kos "$A" -- agir.tpr; t1=$(simdi_ms)
beklenen "ilk kosu (wings importlu)" iska "agir tamam"
t2=$(simdi_ms); kos "$A" -- agir.tpr; t3=$(simdi_ms)
beklenen "ayni girdiyle ikinci kosu" isabet "agir tamam"
ilk=$((t1 - t0)); ikinci=$((t3 - t2))
if [ "$ilk" -le 0 ]; then
    atla "sure olculemedi (ms saati yok)"
elif [ $((ikinci * 2)) -lt "$ilk" ]; then
    gecti "isabet olculebilir hizli: ilk $ilk ms, isabet $ikinci ms"
else
    dustu "isabet hizli DEGIL: ilk $ilk ms, isabet $ikinci ms"
fi

# ---- 2. ISKA MATRISI -----------------------------------------------------------
printf 'print(1);\n' > "$A/tek.tpr"
kos "$A" -- tek.tpr;  beklenen "kaynak (ilk)" iska "1"
kos "$A" -- tek.tpr;  beklenen "kaynak (ayni)" isabet "1"
printf 'print(2);\n' > "$A/tek.tpr"
kos "$A" -- tek.tpr;  beklenen "kaynakta tek karakter degisti" iska "2"

printf 'import "yardimci";\nprint(deger());\n' > "$A/ana.tpr"
printf 'func deger(): int { return 10; }\n' > "$A/yardimci.tpr"
kos "$A" -- ana.tpr;  beklenen "yerel import (ilk)" iska "10"
kos "$A" -- ana.tpr;  beklenen "yerel import (ayni)" isabet "10"
cp -p "$A/yardimci.tpr" "$TMP/yardimci.eski"
printf 'func deger(): int { return 20; }\n' > "$A/yardimci.tpr"
kos "$A" -- ana.tpr;  beklenen "yerel import degisti" iska "20"
# mtime'i geri alinmis icerik: ayni boyut, eski mtime.
printf 'func deger(): int { return 30; }\n' > "$A/yardimci.tpr"
touch -r "$TMP/yardimci.eski" "$A/yardimci.tpr"
kos "$A" -- ana.tpr;  beklenen "icerik degisti, mtime GERI ALINDI" iska "30"

# Paket-yerel kardes: tulpar_modules/paket/paket.tpr icindeki `import "ic"`
# tulpar_modules/paket/ic.tpr'ye cozulur (eski mtime onbellegi bunu GORMUYORDU).
mkdir -p "$A/tulpar_modules/paket"
printf 'import "paket";\nprint(paket_deger());\n' > "$A/paketli.tpr"
printf 'import "ic";\nfunc paket_deger(): int { return ic_deger(); }\n' > "$A/tulpar_modules/paket/paket.tpr"
printf 'func ic_deger(): int { return 100; }\n' > "$A/tulpar_modules/paket/ic.tpr"
kos "$A" -- paketli.tpr;  beklenen "paket-yerel kardes import (ilk)" iska "100"
kos "$A" -- paketli.tpr;  beklenen "paket-yerel kardes import (ayni)" isabet "100"
printf 'func ic_deger(): int { return 200; }\n' > "$A/tulpar_modules/paket/ic.tpr"
kos "$A" -- paketli.tpr;  beklenen "paket-yerel kardes import degisti" iska "200"

# tulpar.toml
kos "$A" -- tek.tpr;  beklenen "toml yok" isabet "2"
printf '[package]\nname = "onb"\nversion = "0.1.0"\n' > "$A/tulpar.toml"
kos "$A" -- tek.tpr;  beklenen "tulpar.toml eklendi" iska "2"
rm -f "$A/tulpar.toml"

# Import edilen bir modulun KENDI disk importu (iki kademe, calisma dizinine
# gore cozulen "lib/..." yolu). Eskiden bu durum gomulu router'in
# `import "lib/http_utils.tpr"` satiriyla sinaniyordu; o satir 2026-10-05'te
# gomulu ada ("http_utils") cevrildi (depo disinda derlenmiyordu,
# tests/gomulu_import_disarida.sh). Ayni anahtar yolu yerel modulle sinaniyor.
mkdir -p "$TMP/gm/lib"
printf 'func gm_deger(): int { return 7; }\n' > "$TMP/gm/lib/gm_alt.tpr"
printf 'import "lib/gm_alt.tpr";\nfunc gm_ust(): int { return gm_deger(); }\n' > "$TMP/gm/gm_ust.tpr"
printf 'import "gm_ust.tpr";\nprint(gm_ust());\n' > "$TMP/gm/r.tpr"
kos "$TMP/gm" -- r.tpr;  beklenen "modulun disk importu (ilk)" iska "7"
kos "$TMP/gm" -- r.tpr;  beklenen "modulun disk importu (ayni)" isabet "7"
printf 'func gm_deger(): int { return 8; }\n' > "$TMP/gm/lib/gm_alt.tpr"
kos "$TMP/gm" -- r.tpr;  beklenen "modulun diskteki importu degisti" iska "8"

# Ice aktaranin DIZININE gore cozum (oyun geri bildirimi #6, 2026-10-08):
# program BASKA bir dizinden baslatiliyor, import ana dosyanin dizininden
# cozuluyor; calisma dizininde AYNI ADLI baska bir dosya da var. Anahtarin
# tarayicisi eski kurali (calisma dizini) izleseydi calisma dizinindekinin
# ozetini tasir, ana dosyanin yanindaki modul degisince ISABET verirdi —
# bayat ikili (Tuzaklar 7n). Golgelenenin VARLIGI da anahtarda: kodgenin
# belirsizlik uyarisi isabette yeniden basiliyor, silinince bayat kalmasin.
mkdir -p "$TMP/iy/oyun"
printf 'func iy_deger(): int { return 1; }\n' > "$TMP/iy/oyun/iy_m.tpr"
printf 'func iy_deger(): int { return 99; }\n' > "$TMP/iy/iy_m.tpr"
printf 'import "iy_m.tpr";\nprint(iy_deger());\n' > "$TMP/iy/oyun/ana.tpr"
kos "$TMP/iy" -- oyun/ana.tpr;  beklenen "ice aktaranin dizini (ilk)" iska "1"
grep -qF "resolves in two places" <<<"$ERR" && gecti "belirsizlik uyarisi basildi" \
    || dustu "belirsizlik uyarisi YOK"
kos "$TMP/iy" -- oyun/ana.tpr;  beklenen "ice aktaranin dizini (ayni)" isabet "1"
printf 'func iy_deger(): int { return 2; }\n' > "$TMP/iy/oyun/iy_m.tpr"
kos "$TMP/iy" -- oyun/ana.tpr;  beklenen "ana dosyanin yanindaki modul degisti" iska "2"
printf 'func iy_deger(): int { return 98; }\n' > "$TMP/iy/iy_m.tpr"
kos "$TMP/iy" -- oyun/ana.tpr;  beklenen "golgelenenin ICERIGI degisti (kullanilmiyor)" isabet "2"
rm -f "$TMP/iy/oyun/iy_m.tpr"
kos "$TMP/iy" -- oyun/ana.tpr;  beklenen "yanindaki silindi: eski kurala (calisma dizini) donus" iska "98"
printf 'func iy_deger(): int { return 3; }\n' > "$TMP/iy/oyun/iy_m.tpr"
kos "$TMP/iy" -- oyun/ana.tpr;  beklenen "yanindaki geri geldi" iska "3"
rm -f "$TMP/iy/iy_m.tpr"
kos "$TMP/iy" -- oyun/ana.tpr;  beklenen "golgelenen silindi (uyari bayat kalmaz)" iska "3"
grep -qF "resolves in two places" <<<"$ERR" && dustu "golgelenen silindi ama uyari hala basiliyor" \
    || gecti "golgelenen silinince uyari yok"

# ---- Kod uretimi anahtari: TULPAR_NO_FVER (build yolu, ayni cikti adi) -------
cat > "$A/ir.tpr" <<'T'
func f(float[] a, float[] b, int n, int m) {
    for (int i = 0; i < m; i++) {
        for (int j = 0; j < n; j++) { a[i * n + j] = a[i * n + j] + b[j] * 2.0; }
    }
}
float[] a = array_fill(64, 1.0);
float[] b = array_fill(64, 1.0);
f(a, b, 8, 8);
print(a[63]);
T
ozet() { if command -v sha256sum >/dev/null 2>&1; then sha256sum "$1" | cut -c1-16; else shasum -a 256 "$1" | cut -c1-16; fi; }
kos "$A" -- build ir.tpr irb
ac=$(ozet "$A/irb$EXE")
kos "$A" -- build ir.tpr irb
grep -q 'Cache hit' <<<"$OUT" && gecti "build: ayni girdiyle ikinci derleme isabet" \
    || dustu "build: ikinci derleme isabet ETMEDI ($(karar))"
kos "$A" TULPAR_NO_FVER=1 -- build ir.tpr irb
kapat=$(ozet "$A/irb$EXE")
if ! grep -q 'Cache hit' <<<"$OUT" && [ "$kapat" != "$ac" ]; then
    gecti "TULPAR_NO_FVER=1: iska, yeni ikili ($ac -> $kapat)"
else
    dustu "TULPAR_NO_FVER=1: ESKI ikili kaldi ($ac -> $kapat) — anahtar kodgen degiskenini gormuyor"
fi
if [ "$(uname -s)" = "Linux" ]; then
    (cd "$A" && TULPAR_AOT_NOCACHE=1 TULPAR_NO_FVER=1 "$TUL" build ir.tpr taze >/dev/null 2>&1)
    [ "$(ozet "$A/taze")" = "$kapat" ] && gecti "TULPAR_NO_FVER=1 ikilisi onbelleksiz taze derlemeyle bayt bayt ayni" \
        || dustu "TULPAR_NO_FVER=1 ikilisi taze derlemeden FARKLI"
else
    atla "bayt bayt karsilastirma yalniz Linux'ta (macOS imza kimligi / PE zaman damgasi cikti adina bagli)"
fi
(cd "$A" && TULPAR_AOT_EMIT_LL=1 "$TUL" build ir.tpr ir_acik >/dev/null 2>&1)
(cd "$A" && TULPAR_AOT_EMIT_LL=1 TULPAR_NO_FVER=1 "$TUL" build ir.tpr ir_kapali >/dev/null 2>&1)
n_ac=$(grep -c 'fv.el' "$A/ir_acik.ll" 2>/dev/null)
n_kapali=$(grep -c 'fv.el' "$A/ir_kapali.ll" 2>/dev/null)
if [ "${n_ac:-0}" -gt 0 ] && [ "${n_kapali:-1}" -eq 0 ]; then
    gecti "kontrol: TULPAR_NO_FVER kodgeni gercekten degistiriyor (IR'de fv.el: $n_ac -> 0)"
else
    dustu "kontrol: fv.el sayilari acik=${n_ac:-?} kapali=${n_kapali:-?} — ustteki iska bir sey kanitlamiyor"
fi
kos "$A" TULPAR_NO_FVER=1 -- build ir.tpr irb
grep -q 'Cache hit' <<<"$OUT" && gecti "TULPAR_NO_FVER=1 tekrar: isabet (yeni anahtarla)" \
    || dustu "TULPAR_NO_FVER=1 tekrar isabet etmedi"
kos "$A" -- build ir.tpr irb
if ! grep -q 'Cache hit' <<<"$OUT" && [ "$(ozet "$A/irb$EXE")" != "$kapat" ]; then
    gecti "TULPAR_NO_FVER kaldirilinca: iska, surumlu ikili geri geldi"
else
    dustu "TULPAR_NO_FVER kaldirilinca ikili degismedi"
fi
cp "$A/tek.tpr" "$A/baska.tpr"
(cd "$A" && TULPAR_AOT_NOCACHE=1 "$TUL" build baska.tpr baska >/dev/null 2>&1)
cp "$A/baska$EXE" "$A/irb$EXE"
kos "$A" -- build ir.tpr irb
if ! grep -q 'Cache hit' <<<"$OUT" && [ "$(cd "$A" && ./irb$EXE | tr -d '\r')" = "3" ]; then
    gecti "build: ikili baska bir dosyayla degistirilince iska (kimlik tutmadi), dogru ikili geri geldi"
else
    dustu "build: degistirilmis ikiliye isabet etti"
fi

# ---- Eklenti arsivi -------------------------------------------------------------
CC_BIN=""
for c in cc clang gcc; do command -v "$c" >/dev/null 2>&1 && { CC_BIN="$c"; break; }; done
AR_BIN=""
for c in ar llvm-ar; do command -v "$c" >/dev/null 2>&1 && { AR_BIN="$c"; break; }; done
if [ -n "$CC_BIN" ] && [ -n "$AR_BIN" ]; then
    E="$TMP/eklenti"
    mkdir -p "$E/lib" "$TMP/ek"
    cat > "$E/tulpar-ext.json" <<'J'
{
  "tulpar_ext": 1,
  "name": "onbsinama",
  "version": "1.0.0",
  "functions": [{"name": "onb_deger", "returns": "i64"}],
  "link": {
    "linux":   {"lib_dirs": ["lib"], "libs": ["onbsinama"]},
    "macos":   {"lib_dirs": ["lib"], "libs": ["onbsinama"]},
    "windows": {"lib_dirs": ["lib"], "libs": ["onbsinama"]}
  }
}
J
    arsiv() {  # arsiv <deger>
        printf 'long long onb_deger(void) { return %s; }\n' "$1" > "$E/onb.c"
        "$CC_BIN" -O2 -c "$E/onb.c" -o "$E/onb.o" && rm -f "$E/lib/libonbsinama.a" &&
            "$AR_BIN" rcs "$E/lib/libonbsinama.a" "$E/onb.o"
    }
    printf 'print(onb_deger());\n' > "$TMP/ek/e.tpr"
    EXT=$(yerel "$E")
    arsiv 41
    kos "$TMP/ek" -- --ext "$EXT" e.tpr;  beklenen "eklenti (ilk)" iska "41"
    kos "$TMP/ek" -- --ext "$EXT" e.tpr;  beklenen "eklenti (ayni)" isabet "41"
    cp -p "$E/lib/libonbsinama.a" "$TMP/onb_eski.a"
    arsiv 42
    kos "$TMP/ek" -- --ext "$EXT" e.tpr;  beklenen "eklenti arsivi yeniden derlendi" iska "42"
    arsiv 43
    touch -r "$TMP/onb_eski.a" "$E/lib/libonbsinama.a"
    kos "$TMP/ek" -- --ext "$EXT" e.tpr;  beklenen "eklenti arsivi degisti, mtime GERI ALINDI" iska "43"
else
    dustu "C derleyicisi / ar yok — eklenti ve runtime arsivi ayaklari KOSMADI"
fi

# ---- Runtime arsivi ---------------------------------------------------------------
# Surucunun bir kopyasi + arsivleri gecici bir dizinde: link once surucunun
# yanindaki libtulpar_runtime.a'yi kullanir (build_link_search_dirs).
RTA=""
for d in "$(dirname "$TUL")" "$ROOT/build-linux" "$ROOT/build-macos" "$ROOT/build-windows" "$ROOT/build"; do
    [ -f "$d/libtulpar_runtime.a" ] && { RTA="$d"; break; }
done
if [ -n "$RTA" ] && [ -n "$CC_BIN" ] && [ -n "$AR_BIN" ]; then
    S="$TMP/surucu"
    mkdir -p "$S" "$TMP/rt"
    # .exe yalniz Windows'ta (MSYS2): Linux'ta depo kokunde
    # kalmis bir tulpar.exe (capraz derleme artigi) kopyalanip "$S/tulpar"
    # hic olusmuyordu -> rc=127 (olculdu 2026-10-05, yerel).
    case "$(uname -s)" in
      MINGW*|MSYS*|CYGWIN*) cp "$TUL.exe" "$S/" ;;
      *) cp "$TUL" "$S/" ;;
    esac
    cp "$RTA/libtulpar_runtime.a" "$S/"
    [ -f "$RTA/libtulpar_tame.a" ] && cp "$RTA/libtulpar_tame.a" "$S/"
    TUL_ESKI=$TUL
    TUL="$S/$(basename "$TUL")"
    printf 'print("runtime");\n' > "$TMP/rt/p.tpr"
    kos "$TMP/rt" -- p.tpr;  beklenen "runtime arsivi (ilk, kopya surucu)" iska "runtime"
    kos "$TMP/rt" -- p.tpr;  beklenen "runtime arsivi (ayni)" isabet "runtime"
    cp -p "$S/libtulpar_runtime.a" "$TMP/rt_eski.a"
    printf 'int tulpar_onbellek_sinama_uyesi = 1;\n' > "$TMP/uye.c"
    "$CC_BIN" -c "$TMP/uye.c" -o "$TMP/onb_sinama_uyesi.o"
    "$AR_BIN" rcs "$S/libtulpar_runtime.a" "$TMP/onb_sinama_uyesi.o"
    kos "$TMP/rt" -- p.tpr;  beklenen "runtime arsivi degisti (yeni uye)" iska "runtime"
    cp -p "$TMP/rt_eski.a" "$TMP/rt_ikinci.a"
    printf 'int tulpar_onbellek_sinama_uyesi = 2;\n' > "$TMP/uye.c"
    "$CC_BIN" -c "$TMP/uye.c" -o "$TMP/onb_sinama_uyesi.o"
    "$AR_BIN" rcs "$S/libtulpar_runtime.a" "$TMP/onb_sinama_uyesi.o"
    touch -r "$TMP/rt_eski.a" "$S/libtulpar_runtime.a"
    kos "$TMP/rt" -- p.tpr;  beklenen "runtime arsivi degisti, mtime GERI ALINDI" iska "runtime"
    TUL=$TUL_ESKI
else
    dustu "runtime arsivi ayagi KOSMADI (arsiv: '${RTA:-yok}', cc/ar: '${CC_BIN:-yok}'/'${AR_BIN:-yok}')"
fi

# ---- 3. ORTAM ---------------------------------------------------------------------
kos "$A" -- tek.tpr;                               beklenen "taban" isabet "2"
kos "$A" TULPAR_ENGINE_HEADLESS=7 -- tek.tpr;     beklenen "anahtar disi TULPAR_ENGINE_HEADLESS" isabet "2"
kos "$A" TULPAR_CACHE_MAX_MB=999 -- tek.tpr;      beklenen "onbellek ayari TULPAR_CACHE_MAX_MB" isabet "2"
kos "$A" TULPAR_ONBELLEK_SINAMA_BILINMEYEN=1 -- tek.tpr
beklenen "bilinmeyen TULPAR_* (guvenli yon: anahtarda)" iska "2"

# ---- 4. GOZLEM --------------------------------------------------------------------
rm -f "$A/irb.ll"
kos "$A" TULPAR_AOT_EMIT_LL=1 -- build ir.tpr irb
if kapali && [ -f "$A/irb.ll" ]; then gecti "TULPAR_AOT_EMIT_LL: onbellek atlandi, .ll yazildi"
else dustu "TULPAR_AOT_EMIT_LL: onbellek atlanmadi ya da .ll yok ($(karar))"; fi
kos "$A" TULPAR_AOT_TIME=1 -- build ir.tpr irb
if kapali && grep -q 'AOT-TIME' <<<"$ERR"; then gecti "TULPAR_AOT_TIME: onbellek atlandi, faz sureleri basildi"
else dustu "TULPAR_AOT_TIME: onbellek atlanmadi ya da sure yok ($(karar))"; fi

# ---- 5. TANI YENIDEN BASIMI ---------------------------------------------------------
{
    for i in $(seq 1 66); do printf 'struct S%d { int a; }\n' "$i"; done
    printf 'print("yapilar");\n'
} > "$A/yapilar.tpr"
kos "$A" -- yapilar.tpr
uyari1=$(grep -c 'struct table full' <<<"$ERR")
beklenen "65+ struct (ilk)" iska "yapilar"
kos "$A" -- yapilar.tpr
uyari2=$(grep -c 'struct table full' <<<"$ERR")
beklenen "65+ struct (ayni)" isabet "yapilar"
if [ "${uyari1:-0}" -gt 0 ] && [ "$uyari1" = "$uyari2" ]; then
    gecti "derleme uyarisi isabette aynen basildi ($uyari1 satir)"
else
    dustu "derleme uyarisi: ilk $uyari1 satir, isabette $uyari2 satir"
fi

# ---- 6. OZ DENETIM POZITIF KONTROLU --------------------------------------------------
kos "$A" TULPAR_CACHE_SINAMA=tarama-yok -- ana.tpr
if iska && grep -q 'kapsanmayan girdi' <<<"$ERR" && [ "$OUT" = "30" ]; then
    gecti "tarama atlaninca oz denetim kapsanmayan girdiyi yakaladi (cikti dogru: 30)"
else
    dustu "oz denetim kapsanmayan girdiyi YAKALAMADI ($(karar), cikti '$OUT')"
fi
kos "$A" TULPAR_CACHE_SINAMA=tarama-yok -- ana.tpr
if iska; then gecti "kapsanmayan sonuc yayimlanmadi (ikinci kosu da iska)"
else dustu "kapsanmayan sonuc YAYIMLANDI ($(karar)) — oz denetim etkisiz"; fi

# ---- 7. PARALEL YARIS ------------------------------------------------------------------
P="$TMP/par"
mkdir -p "$P"
export TULPAR_CACHE_DIR="$(yerel "$TMP/onb_par")"
printf 'int t = 0;\nfor (int i = 0; i < 1000; i++) { t = t + i; }\nprint(t);\n' > "$P/par.tpr"
paralel() {
    local i
    for i in 1 2 3 4 5 6 7 8; do
        (cd "$P" && "$TUL" par.tpr > "$P/out.$i" 2>"$P/err.$i"; echo $? > "$P/rc.$i") &
    done
    wait
    local ok=0
    for i in 1 2 3 4 5 6 7 8; do
        [ "$(cat "$P/rc.$i")" = "0" ] && [ "$(tr -d '\r' < "$P/out.$i")" = "499500" ] && ok=$((ok + 1))
    done
    echo $ok
}
n=$(paralel)
[ "$n" = "8" ] && gecti "8 surec ayni anda (bos onbellek): hepsi dogru (499500)" \
    || dustu "8 paralel surecten yalniz $n tanesi dogru"
n=$(paralel)
[ "$n" = "8" ] && gecti "8 surec ayni anda (dolu onbellek): hepsi dogru" \
    || dustu "8 paralel isabetten yalniz $n tanesi dogru"
g=$(ls "$TMP/onb_par/run" 2>/dev/null | grep -cE '^[0-9a-f]{32}(\.exe)?$')
t=$(ls "$TMP/onb_par/run" 2>/dev/null | grep -c '^tmp-')
[ "$g" = "1" ] && [ "$t" = "0" ] && gecti "depoda tek girdi, gecici dosya kalmadi" \
    || dustu "depoda $g girdi, $t gecici dosya (beklenen 1 / 0)"
export TULPAR_CACHE_DIR="$(yerel "$ONB")"

# ---- 8. TAVAN (LRU) ----------------------------------------------------------------------
# Gercek ikililerin boyutu platforma bagli (Linux ~1,4 MB; macOS dead_strip ile
# ~50 KB — macOS CI'da 4 girdi 1 MB'a sigdi, tavan hic asilmadi): tavanin
# GERCEKTEN asildigi belli olsun diye depoya iki EN ESKI, 700 KB'lik sahte
# girdi de konuyor (adlari gercek girdi bicimi). Beklenen: sahteler silinir;
# tavan asildikca eski (korumasiz) girdiler gider; az once derlenen girdi
# (son 60 s korumasi) kalir.
export TULPAR_CACHE_DIR="$(yerel "$TMP/onb_tavan")"
TR="$TMP/onb_tavan/run"
for i in 1 2 3; do
    printf 'print(%d);\n' "$i" > "$A/t$i.tpr"
    (cd "$A" && "$TUL" "t$i.tpr" >/dev/null 2>&1)
done
for f in "$TR/"*; do touch -t 202001010000 "$f"; done
for s in aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa1 aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa2; do
    head -c 716800 /dev/zero > "$TR/$s$EXE"
    touch -t 201901010000 "$TR/$s$EXE"
done
printf 'print(4);\n' > "$A/t4.tpr"
kos "$A" TULPAR_CACHE_MAX_MB=1 -- t4.tpr
sahte=$(ls "$TR" | grep -c '^aaaaaaaa')
yeni=$(find "$TR" -type f -mmin -5 | grep -cE '/[0-9a-f]{32}(\.exe)?$')
eski=$(find "$TR" -type f -mmin +60 | grep -cE '/[0-9a-f]{32}(\.exe)?$')
toplam=$(cat "$TR"/* 2>/dev/null | wc -c | tr -d ' ')
if [ "$OUT" = "4" ] && [ "$sahte" = "0" ] && [ "$yeni" -ge 1 ] && grep -q 'tavan' <<<"$ERR" &&
   { [ "$eski" = "0" ] || [ "$toplam" -le 838861 ]; }; then
    gecti "tavan (1 MB) asildi: en eski girdiler silindi (kalan eski $eski, toplam $toplam bayt), yeni girdi kaldi"
else
    dustu "tavan: sahte $sahte (beklenen 0), yeni $yeni, eski $eski, toplam $toplam bayt, cikti '$OUT'"
    sed -n '1,4p' <<<"$ERR" | sed 's/^/         /'
fi
export TULPAR_CACHE_DIR="$(yerel "$ONB")"

# ---- 9. KAPATMA + TEMIZLIK -----------------------------------------------------------------
once=$(girdi_sayisi)
printf 'print(77);\n' > "$A/kapali.tpr"
kos "$A" TULPAR_AOT_NOCACHE=1 -- kapali.tpr
if kapali && [ "$OUT" = "77" ] && [ "$(girdi_sayisi)" = "$once" ]; then
    gecti "TULPAR_AOT_NOCACHE=1: calisti, depoya yazmadi"
else
    dustu "TULPAR_AOT_NOCACHE=1: karar '$(karar)', cikti '$OUT', girdi $once -> $(girdi_sayisi)"
fi
info=$(cd "$A" && "$TUL" cache info 2>&1 | tr -d '\r')
grep -qE '^(dizin|directory): ' <<<"$info" && gecti "tulpar cache info calisiyor" || dustu "tulpar cache info: '$info'"
(cd "$A" && "$TUL" cache clean >/dev/null 2>&1)
if [ "$(girdi_sayisi)" = "0" ] && [ "$once" -gt 0 ]; then
    gecti "tulpar cache clean: $once girdi silindi"
else
    dustu "tulpar cache clean sonrasi $(girdi_sayisi) girdi kaldi (once $once)"
fi
kos "$A" -- tek.tpr;  beklenen "temizlikten sonra" iska "2"

echo ""
if [ $fail -ne 0 ]; then echo "onbellek kapisi: DUSTU"; exit 1; fi
echo "onbellek kapisi: TAMAM"
