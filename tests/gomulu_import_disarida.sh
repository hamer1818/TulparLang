#!/usr/bin/env bash
# GOMULU MODULLER DEPO DISINDA DA IMPORT EDILEBILIR MI KAPISI.
#
# NIYE: gomulu `router` kendi icinde `import "lib/http_utils.tpr"` yaziyordu;
# bu yol CALISMA DIZININE gore diskten cozuluyor. Depo kokunden kosan butun
# testler bunu goremedi (lib/ orada var), ama kurulu tulpar ile baska bir
# dizinde `import "router"` derlenmiyordu (olculdu 2026-10-05, v3.39.6:
# "degisken adini yanlis yazmis olabilirsiniz" ile dustu). `tulpar_api`
# da ayni sekilde `import "lib/router.tpr"` yaziyordu.
#
#   1. STATIK: gomulu kitapliklarin (cmake/EmbedLibraries.cmake listesi)
#      hicbiri `import "lib/..."` / `import "....tpr"` yazmamali — gomulu
#      modul yalniz gomulu ADLA import eder. Pozitif kontrol: yapay ihlalli
#      bir dosya yakalanmali.
#   2. DINAMIK: lib/ OLMAYAN gecici bir dizinde her gomulu modul icin
#      `import "<ad>"` eden program derlenir (+ grafik gerektirmeyenler
#      calistirilir). Onbellek kapali: sonuc bayat olamaz.
#
#   tests/gomulu_import_disarida.sh [tulpar]
set -u
TUL="${1:-./tulpar}"
[ -x "$TUL" ] || [ -x "$TUL.exe" ] || { echo "HATA: '$TUL' yok" >&2; exit 1; }
TUL="$(cd "$(dirname "$TUL")" && pwd)/$(basename "$TUL")"
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
fail=0
gecti() { printf "  \033[0;32mgecti\033[0m  %s\n" "$1"; }
dustu() { printf "  \033[0;31mDUSTU\033[0m  %s\n" "$1"; fail=1; }

# Gomulu ad -> dosya (lib/ altinda), CMake listesinden: tek kaynak.
mapfile -t GOMULU < <(sed -n 's/^embed_library("\([^"]*\)" "\([^"]*\)".*/\1 \2/p' "$ROOT/cmake/EmbedLibraries.cmake" | tr -d '\r')
[ "${#GOMULU[@]}" -ge 10 ] || { dustu "EmbedLibraries.cmake'ten gomulu liste okunamadi (${#GOMULU[@]} satir)"; exit 1; }

# --- 1. STATIK ---------------------------------------------------------------
disk_importu() {  # $1 = dosya; disk yolu import eden satirlari basar
  grep -nE '^[[:space:]]*import[[:space:]]+"([^"]*/[^"]*|[^"]*\.tpr)"' "$1" | tr -d '\r'
}
mkdir -p "$TMP/pk"
printf '// yapay ihlal\nimport "lib/http_utils.tpr";\nimport "tame";\n' > "$TMP/pk/ihlal.tpr"
pk=$(disk_importu "$TMP/pk/ihlal.tpr")
if [ -n "$pk" ] && ! grep -q '"tame"' <<<"$pk"; then
  gecti "pozitif kontrol: yapay disk importu yakalandi, gomulu ad serbest"
else
  dustu "pozitif kontrol: statik tarama yapay ihlali yakalamadi ya da gomulu adi yanlislikla yakaladi ('$pk')"
fi
statik=0
for satir in "${GOMULU[@]}"; do
  ad="${satir%% *}"; dosya="${satir#* }"
  bul=$(disk_importu "$ROOT/lib/$dosya")
  if [ -n "$bul" ]; then dustu "lib/$dosya ($ad) diskten import ediyor: $bul"; statik=1; fi
done
[ $statik -eq 0 ] && gecti "statik: ${#GOMULU[@]} gomulu kitapligin hicbiri disk yolu import etmiyor"

# --- 2. DINAMIK (lib/ olmayan dizin) -----------------------------------------
# Grafik kitapliklari (tame/arcade/scene3d) yalniz DERLENIR: Windows'ta
# ekransiz runner'da pencere kitapligi calistirilamaz (build.sh test ile ayni
# kural); import + print programi pencere acmaz ama kosmak gereksiz risk.
cd "$TMP"
for satir in "${GOMULU[@]}"; do
  ad="${satir%% *}"
  # http_utils ve middleware router'in YARDIMCILARI: router'in global'lerini
  # (`_request`, `_router_port`) kullaniyorlar, tek baslarina import edilmek
  # icin yazilmadilar — gercek kullanimlari router'la birlikte.
  case "$ad" in
    http_utils|middleware) printf 'import "router";\nimport "%s";\nprint("gomulu %s tamam");\n' "$ad" "$ad" > "g_$ad.tpr" ;;
    *) printf 'import "%s";\nprint("gomulu %s tamam");\n' "$ad" "$ad" > "g_$ad.tpr" ;;
  esac
  if ! TULPAR_AOT_NOCACHE=1 "$TUL" build "g_$ad.tpr" "g_$ad" > "g_$ad.log" 2>&1; then
    dustu "lib/ olmayan dizinde import \"$ad\" derlenmedi: $(tr -d '\r' < "g_$ad.log" | grep -m2 -iE 'hata|error|bulun' | tr '\n' ' ')"
    continue
  fi
  case "$ad" in
    tame|arcade|scene3d) gecti "import \"$ad\" derlendi (grafik: calistirilmadi)"; continue ;;
  esac
  cikti=$(DISPLAY= WAYLAND_DISPLAY= "./g_$ad" 2>&1 | tr -d '\r' | tail -1)
  if [ "$cikti" = "gomulu $ad tamam" ]; then gecti "import \"$ad\" derlendi ve kostu"
  else dustu "import \"$ad\" kostu ama cikti '$cikti'"; fi
done

[ $fail -eq 0 ] && echo "gomulu import kapisi: GECTI" || echo "gomulu import kapisi: DUSTU"
exit $fail
