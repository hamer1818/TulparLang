#!/bin/bash
# SURUM TUTARLILIK DENETIMI — `tulpar version` git etiketiyle ayni mi?
#
# Kullanim: tools/surum_denetle.sh <tulpar>
#   TULPAR_VERSION=<etiket> ortamdaysa (etiket derlemesi, build.yml) beklenen
#   o etiket; degilse `git describe --tags --match 'v[0-9]*' --dirty`.
#
# Neden var: surum `project(VERSION 3.13.1)` satirindan geliyordu; otomatik
# surum her birlesmede etiket kesmeye basladiktan sonra (2026-09-21) dal ve
# yerel derlemeler v3.37.x cikmisken "3.13.1-dev" diyordu ve hicbir kapi
# bunu olcmuyordu. Artik dizgi cmake/TulparVersion.cmake'te etiketten
# uretiliyor; bu betik (CI'da derlemenin hemen ardindan) ikilinin gercekten
# onu tasidigini olcer.
#
# Oz-denetim (pozitif kontrol — betigin kendisi bir sey olcuyor mu): surum
# betiginin uc yolu ayrica kosulur ve beklenen sonuclari vermek ZORUNDADIR:
#   * override -> aynen (etiket derlemesi yolu);
#   * git deposu olmayan dizin -> "0.0.0-dev" (sayi uydurmaz);
#   * bu depo -> git describe ile ayni.
# Ve ikilinin dizgisi, kasitli yanlis bir beklentiyle KARSILASTIRILINCA
# eslesmemeli (karsilastirma her seyi "esit" saymiyor).
set -u
cd "$(dirname "$0")/.."
ROOT="$(pwd)"
TULPAR="${1:-./tulpar}"
[ -x "$TULPAR" ] || { echo "surum denetimi DUSTU: $TULPAR yok"; exit 1; }
command -v git >/dev/null 2>&1 || { echo "surum denetimi DUSTU: git yok"; exit 1; }
command -v cmake >/dev/null 2>&1 || { echo "surum denetimi DUSTU: cmake yok"; exit 1; }

hata=0
script_surum() {   # $1 = kaynak dizini, $2 = override
  cmake -DTULPAR_SOURCE_DIR="$1" -DTULPAR_VERSION_OVERRIDE="$2" -DTULPAR_VERSION_PRINT=1 \
        -P "$ROOT/cmake/TulparVersion.cmake" 2>&1 | tr -d '\r' | tail -1
}

# --- oz-denetim --------------------------------------------------------------
v=$(script_surum "$ROOT" "v9.9.9-oz")
[ "$v" = "v9.9.9-oz" ] && echo "  gecti  override yolu aynen: $v" \
  || { echo "  DUSTU  override yolu: '$v' (beklenen v9.9.9-oz)"; hata=1; }
TMP=$(mktemp -d); trap 'rm -rf "$TMP"' EXIT
mkdir -p "$TMP/yok"   # var olan ama git deposu olmayan dizin: git GERCEKTEN cagrilip dusmeli
v=$(GIT_CEILING_DIRECTORIES="$TMP" script_surum "$TMP/yok" "")
[ "$v" = "0.0.0-dev" ] && echo "  gecti  git'siz dizin: $v (sayi uydurmuyor)" \
  || { echo "  DUSTU  git'siz dizin: '$v' (beklenen 0.0.0-dev)"; hata=1; }
desc=$(git -c safe.directory='*' -C "$ROOT" describe --tags --match 'v[0-9]*' --dirty 2>/dev/null || true)
v=$(script_surum "$ROOT" "")
if [ -n "$desc" ]; then
  [ "$v" = "$desc" ] && echo "  gecti  depo: git describe ile ayni: $v" \
    || { echo "  DUSTU  depo: betik '$v', git describe '$desc'"; hata=1; }
else
  echo "  bilgi  ulasilabilir v* etiketi yok (sig klon?) — betik: $v"
fi

# --- ikili ---------------------------------------------------------------
if [ -n "${TULPAR_VERSION:-}" ]; then
  beklenen="$TULPAR_VERSION"; kaynak="etiket derlemesi (TULPAR_VERSION)"
else
  beklenen="$desc"; kaynak="git describe"
  if [ -z "$beklenen" ]; then
    echo "  DUSTU  beklenen surum hesaplanamadi: etiket yok (checkout fetch-depth: 0 mi?)"
    exit 1
  fi
fi
ikili=$("$TULPAR" version 2>&1 | tr -d '\r' | head -1)
if [ "$ikili" = "TulparLang $beklenen" ]; then
  echo "  gecti  ikili: '$ikili' == $kaynak"
else
  echo "  DUSTU  ikili: '$ikili', beklenen 'TulparLang $beklenen' ($kaynak)"
  hata=1
fi
[ "$ikili" != "TulparLang ${beklenen}-yanlis" ] && echo "  gecti  pozitif kontrol: kasitli yanlis beklenti eslesmiyor" \
  || { echo "  DUSTU  pozitif kontrol: karsilastirma yanlis beklentiyle de eslesti"; hata=1; }

if [ $hata -ne 0 ]; then echo "surum denetimi DUSTU"; exit 1; fi
echo "surum denetimi temiz: $ikili"
