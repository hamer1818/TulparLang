#!/usr/bin/env bash
# KULLANILMAYAN GOMULU FONKSIYON AYIKLAMASI KAPISI (llvm_backend.cpp
# strip_unused_embedded).
#
# NIYE: `import "wings"` 28 satirlik programa ~160 kitaplik fonksiyonu
# getiriyordu ve hepsi her derlemede -O3 + nesne uretiminden geciyordu
# (olculdu 2026-10-05, Ryzen 7 9800X3D: wings_groups_test optimize 449 ms +
# emit 380 ms). Ayiklama kullanilmayanlari optimizasyondan ONCE atiyor —
# ama ADLA cagrilan bir kitaplik fonksiyonunu atarsa program calisma
# zamaninda "Function not found" ile duser. Bu kapi iki yonu de olcer:
#
#   1. AYIKLAMA ETKIN: wings programinda tanimli fonksiyon sayisi dusuyor
#      (TULPAR_AOT_STRIP_DEBUG satiri) — sessizce kapanirsa kirmizi.
#   2. ADLA CAGRI KORUNUYOR: `call("created", ...)` (ad yalniz ana
#      programin dizgisinde; wings icinde dogrudan cagrilmiyor) ve
#      birlestirilerek kurulan ad (`"crea" + ted`) calisiyor.
#   3. POZITIF KONTROL: TULPAR_AOT_STRIP_SINAMA=kok-yok kok adlarini yok
#      sayar -> ayni program DUSMELI. Dusmezse 2. ayak hicbir sey olcmuyordu.
#   4. KACIS: TULPAR_AOT_KEEP_ALL=1 ayiklamayi kapatir (satir basilmaz).
#
#   tests/gomulu_ayiklama.sh [tulpar]
set -u
TUL="${1:-./tulpar}"
[ -x "$TUL" ] || { echo "HATA: '$TUL' yok" >&2; exit 1; }
TUL="$(cd "$(dirname "$TUL")" && pwd)/$(basename "$TUL")"
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
fail=0
gecti() { printf "  \033[0;32mgecti\033[0m  %s\n" "$1"; }
dustu() { printf "  \033[0;31mDUSTU\033[0m  %s\n" "$1"; fail=1; }

cat > "$TMP/adla.tpr" <<'T'
import "wings";
json r = call("created", {"a": 1});
print(r["_status"]);
T
cat > "$TMP/kurulan.tpr" <<'T'
import "wings";
str son = "ted";
str ad = "crea" + son;
json r = call(ad, {"a": 2});
print(r["_status"]);
T

derle() {  # derle <kaynak> <cikti> [env...]
    local src=$1 out=$2; shift 2
    (cd "$TMP" && env TULPAR_AOT_NOCACHE=1 TULPAR_AOT_STRIP_DEBUG=1 "$@" "$TUL" build "$src" "$out") > "$TMP/$out.log" 2>&1
}

# 1 + 2: adla cagri
if derle adla.tpr adla; then
    satir=$(grep -m1 'AOT-STRIP' "$TMP/adla.log")
    once=$(sed -nE 's/.*tanimli fonksiyon ([0-9]+) -> ([0-9]+).*/\1/p' <<<"$satir")
    sonra=$(sed -nE 's/.*tanimli fonksiyon ([0-9]+) -> ([0-9]+).*/\2/p' <<<"$satir")
    if [ -n "$once" ] && [ -n "$sonra" ] && [ "$sonra" -lt "$once" ]; then
        gecti "ayiklama etkin: tanimli fonksiyon $once -> $sonra"
    else
        dustu "ayiklama satiri yok ya da sayi dusmedi: '${satir:-yok}'"
    fi
    o=$("$TMP/adla" 2>&1 | tail -1)
    [ "$o" = "201" ] && gecti "call(\"created\") ayiklamadan sonra calisiyor (201)" \
        || dustu "call(\"created\") ciktisi '$o' (beklenen 201)"
else
    dustu "adla.tpr derlenemedi"; tail -5 "$TMP/adla.log" | sed 's/^/         /'
fi

if derle kurulan.tpr kurulan; then
    o=$("$TMP/kurulan" 2>&1 | tail -1)
    [ "$o" = "201" ] && gecti "birlestirilerek kurulan ad (\"crea\" + son) korunuyor" \
        || dustu "kurulan ad ciktisi '$o' (beklenen 201)"
else
    dustu "kurulan.tpr derlenemedi"; tail -5 "$TMP/kurulan.log" | sed 's/^/         /'
fi

# 3: pozitif kontrol — kokler yok sayilinca ayni program dusmeli
if derle adla.tpr sabotaj TULPAR_AOT_STRIP_SINAMA=kok-yok; then
    o=$("$TMP/sabotaj" 2>&1); rc=$?
    if [ $rc -ne 0 ] || [ "$(tail -1 <<<"$o")" != "201" ]; then
        gecti "pozitif kontrol: kokler yok sayilinca call(\"created\") dustu (rc=$rc)"
    else
        dustu "pozitif kontrol: kokler yok sayildigi halde calisti — 2. ayak olcmuyor"
    fi
else
    gecti "pozitif kontrol: kokler yok sayilinca derleme dustu"
fi

# 4: kacis yolu
if derle adla.tpr hepsi TULPAR_AOT_KEEP_ALL=1; then
    if grep -q 'AOT-STRIP' "$TMP/hepsi.log"; then dustu "TULPAR_AOT_KEEP_ALL=1 iken ayiklama kosmus"
    else gecti "TULPAR_AOT_KEEP_ALL=1 ayiklamayi kapatiyor"; fi
    [ "$("$TMP/hepsi" 2>&1 | tail -1)" = "201" ] || dustu "KEEP_ALL ikilisi calismadi"
else
    dustu "KEEP_ALL derlemesi dustu"
fi

if [ $fail -ne 0 ]; then
    echo -e "\033[0;31mGOMULU AYIKLAMA KAPISI KIRMIZI\033[0m"; exit 1
fi
echo "gomulu ayiklama kapisi: temiz"
