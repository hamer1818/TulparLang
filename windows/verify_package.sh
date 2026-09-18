#!/usr/bin/env bash
# dist-windows/ paketini YALITILMIS ortamda sinar: paket gercekten kendi kendine
# yetiyor mu?
#
# Yalitim sart, cunku gelistirme makinesinde sysroot zaten PATH'te: paket eksik
# olsa bile calisiyormus gibi gorunur (bu, "bende calisiyordu" sinifinin ta
# kendisi). Burada paket AYRI bir dizine kopyalanir ve Wine'a YALNIZ o dizin
# gosterilir — sysroot, build-windows/ ve TULPAR_CC ortamdan CIKARILIR.
set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PKG="${1:-$ROOT/dist-windows}"
GREEN='\033[0;32m'; RED='\033[0;31m'; NC='\033[0m'
[ -d "$PKG" ] || { echo -e "${RED}HATA:${NC} paket yok: $PKG (once windows/package.sh)"; exit 1; }
command -v wine >/dev/null || { echo -e "${RED}HATA:${NC} wine yok"; exit 1; }

TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
cp -r "$PKG" "$TMP/Tulpar"
cd "$TMP/Tulpar"

# YALITIM: sysroot'a giden her sey temizlenir.
export WINEPREFIX="${WINEPREFIX:-$HOME/.tulpar-wine}"
export WINEDEBUG=-all
unset WINEPATH TULPAR_CC TULPAR_ENGINE_ASSETS TULPAR_AOT_LINK_FLAGS

FAIL=0
# IKI KOSUL: cikis kodu 0 VE beklenen metin. Yalniz metne bakmak yetmiyordu —
# olculdu (2026-09-18): editor kapisinin olcutu "varlik" idi ve BASARISIZLIK
# mesaji yolu icerdigi icin ("...\motor\varliklar/editor.sahne") ayni kelime
# orada da geciyordu; kapi, sahne SILINMISKEN bile yesil veriyordu. Pozitif
# kontrol (varliklari kasten kaldir) bunu yakaladi. Ders: olcut, hedeflenen
# hatanin ciktisinda BULUNMAMALI — ve cikis kodu bedava bir ikinci taniktir.
kontrol() { # ad, beklenen-metin, komut...
    local ad="$1" bekle="$2"; shift 2
    local out rc
    out=$(DISPLAY= timeout 600 "$@" 2>&1); rc=$?
    out=$(grep -vE "MESA|pci id|^wine:" <<< "$out")
    if [ "$rc" = "0" ] && grep -q "$bekle" <<< "$out"; then
        printf "  ${GREEN}TAMAM${NC}  %s\n" "$ad"
    else
        printf "  ${RED}DUSTU${NC}  %s (beklenen: '%s', cikis %d)\n" "$ad" "$bekle" "$rc"
        sed 's/^/         /' <<< "$out" | tail -5
        FAIL=1
    fi
}

echo "paket yalitilmis kopyada sinaniyor: $TMP/Tulpar"
echo "(PATH'te sysroot YOK, TULPAR_CC ayarli DEGIL — paket kendi basina)"
echo

kontrol "surum"            "TulparLang"        wine cmd /c tulpar.cmd version
kontrol "derle + calistir" "HOS GELDINIZ"     wine cmd /c tulpar.cmd "ornekler\\01_hello_world.tpr"
kontrol "tek dosya uret"   "Successfully"      wine cmd /c tulpar.cmd build "ornekler\\02_basics.tpr" cikti
if [ -f cikti.exe ]; then
    kontrol "uretilen ikili"  "Basics"         wine ./cikti.exe
else
    printf "  ${RED}DUSTU${NC}  uretilen ikili (cikti.exe olusmadi)\n"; FAIL=1
fi
kontrol "bicimlendirici"   "print"             wine cmd /c tulpar.cmd fmt "ornekler\\01_hello_world.tpr"
kontrol "tip denetimi"     "typecheck: ok"     wine cmd /c tulpar.cmd typecheck "ornekler\\02_basics.tpr"

# MOTOR: "kare uretti" YETMEZ. Model/yazi tipi bulunamazsa motor sessizce duz
# kutulara ve yazi tipsiz HUD'a duser, yine de goruntu yazar — ilk paket surumu
# tam bu yuzden YANLIS YESIL verdi (varliklar ic ice dizinlerdeydi, hicbiri
# bulunmuyordu; hatayi kullanici gordu). O yuzden burada YUKLENDI kaniti
# araniyor: glTF satiri ve sahnenin varlik sayisi.
if [ -f motor/engine_demo.exe ]; then
    kontrol "motor: kare uretti"   "goruntu"   wine cmd /c "motor\\demo.cmd" --headless 3 --out kare.ppm
    kontrol "motor: glTF yuklendi" "glTF: "    wine cmd /c "motor\\demo.cmd" --headless 3 --out kare2.ppm
    kontrol "motor: yazi tipi"     "font: "    wine cmd /c "motor\\demo.cmd" --headless 3 --out kare3.ppm
fi
if [ -f motor/engine_editor.exe ]; then
    # Editor sahneyi BULAMAZSA cikis kodu 1 ve "sahne ...: acilamadi" der.
    kontrol "editor: sahne acildi" "kaynak"    wine cmd /c "motor\\editor.cmd" --headless 3 --out ed.ppm
fi

echo
if [ "$FAIL" = "0" ]; then
    echo -e "${GREEN}PAKET KENDI KENDINE YETIYOR.${NC} Zip'leyip gonderebilirsiniz."
else
    echo -e "${RED}PAKET EKSIK.${NC} Yukaridaki dusen adimlar hedef makinede de dusecek."
fi
exit $FAIL
