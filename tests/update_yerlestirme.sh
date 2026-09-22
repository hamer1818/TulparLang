#!/bin/bash
# `tulpar update` YERLESTIRME kapisi — indirileni yerine koyma yolu.
#
# Bu komutun en kirilgan adimi indirme degil, indirileni YERINE KOYMAK.
# Hazirlik dizini `$TMPDIR` (varsayilan `/tmp`), hedef ise kurulum dizini
# (genellikle `~/.local/bin`) ve ikisi AYRI dosya sistemlerinde olabiliyor.
# `rename(2)` dosya sistemi sinirini gecemez, EXDEV (18) doner.
#
# Olculdu 2026-09-22 (CachyOS): `/tmp` tmpfs, `/home` ayri aygit. Yayinlanmis
# `tulpar update`, 68 MB'lik indirmeyi ve SHA-256 dogrulamasini BASARIYLA
# bitirdikten sonra son adimda dusuyordu — yani komut o makinede hic
# calismiyordu. Hicbir kapi bu yolu olcmuyordu cunku gercek bir guncelleme
# AG ister; bu kapi tam da o yuzden agdan bagimsiz (`--test-install`).
#
# IKI kol var ve ikincisi olmadan birincisi hicbir sey soylemezdi: tek
# dosya sistemi uzerinde kosan bir deneme, EXDEV yolunu HIC gormez.
set -u
cd "$(dirname "$0")/.."
TULPAR="${1:-./tulpar}"
[ -x "$TULPAR" ] || { echo "yerlestirme kapisi: $TULPAR yok — atlandi"; exit 0; }

aygit() { stat -c %d "$1" 2>/dev/null || stat -f %d "$1" 2>/dev/null; }

HEDEF=$(mktemp -d "$PWD/.tulpar-yerlestirme-XXXXXX") || exit 1
trap 'rm -rf "$HEDEF"' EXIT
HEDEF_DEV=$(aygit "$HEDEF")

# 1) AYNI dosya sistemi — `rename` dogrudan calismali.
if ! out=$(TMPDIR="$HEDEF" "$TULPAR" update --test-install="$HEDEF" 2>&1); then
    echo "yerlestirme kapisi DUSTU (ayni dosya sistemi):"
    printf '%s\n' "$out" | sed 's/^/    /'
    exit 1
fi
echo "  gecti  ayni dosya sistemi"

# 2) FARKLI dosya sistemi — asil olculmek istenen yol. Yazilabilir ve
#    hedeften BASKA bir aygitta olan ilk aday kullaniliyor.
CAPRAZ=""
for aday in /tmp /dev/shm "${XDG_RUNTIME_DIR:-}"; do
    [ -n "$aday" ] && [ -d "$aday" ] && [ -w "$aday" ] || continue
    d=$(aygit "$aday") || continue
    [ -n "$d" ] && [ "$d" != "$HEDEF_DEV" ] && { CAPRAZ="$aday"; break; }
done
if [ -z "$CAPRAZ" ]; then
    echo "  ATLANDI capraz dosya sistemi — bu makinede baska aygitta yazilabilir dizin yok"
    echo "yerlestirme kapisi: yalniz ayni dosya sistemi olculdu"
    exit 0
fi

if ! out=$(TMPDIR="$CAPRAZ" "$TULPAR" update --test-install="$HEDEF" 2>&1); then
    echo "yerlestirme kapisi DUSTU (capraz dosya sistemi, TMPDIR=$CAPRAZ):"
    printf '%s\n' "$out" | sed 's/^/    /'
    exit 1
fi
# Deneme "sinir gecisi OLCULMEDI" diyorsa kol BOSA DONMUS demektir; bunu
# yesil saymak kapiyi sahte yapar.
case "$out" in
    *OLCULMEDI*|*"NOT exercised"*)
        echo "yerlestirme kapisi DUSTU: capraz kol sinir gecisini OLCMEDI"
        printf '%s\n' "$out" | sed 's/^/    /'
        exit 1 ;;
esac
echo "  gecti  capraz dosya sistemi (TMPDIR=$CAPRAZ)"
echo "yerlestirme kapisi temiz (ayni + capraz)"
