#!/usr/bin/env bash
# MODUL, KENDISINI ICE AKTARANIN FONKSIYONUNU GORUR (oyun geri bildirimi #5,
# 2026-10-08).
#
# Ilk gercek cok dosyali oyun (motor deposu) bir modulden ana dosyanin bir
# fonksiyonunu cagirdi: "'ana_yardimci' adinda bir fonksiyon bulunamadi".
# Ayni modul ana dosyanin GLOBAL'ini sorunsuz okuyordu. Kok neden kodgen
# sirasi: ana dosyanin globalleri import'lardan ONCE bildiriliyordu (Pass
# 0.1), fonksiyon imzalari SONRA (Pass 1a); modulun govdeleri ikisinin
# arasinda uretiliyordu. Ic ice modullerde ebeveynin imzalari zaten once
# bildiriliyordu (Pass 0.15) — ana dosya tek istisnaydi.
#
# Kural (docs/mindmap/Imports and Modules.md): bir modul kendi import'larini
# ve kendisini (dogrudan ya da dolayli) ice aktaran her dosyanin ust duzey
# adlarini — global VE fonksiyon — metin sirasindan bagimsiz gorur. SONRA ice
# aktarilan kardes modulu gormez (globalini de): ipuculu net hata.
#
#   tests/modul_ice_aktaran.sh [tulpar_yolu]
set -uo pipefail
TUL="${1:-./tulpar}"
[ -x "$TUL" ] || { echo "HATA: '$TUL' calistirilabilir degil" >&2; exit 1; }
TUL="$(cd "$(dirname "$TUL")" && pwd)/$(basename "$TUL")"
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
export LC_ALL=C TULPAR_AOT_NOCACHE=1
fail=0
n=0
gecti() { printf "  \033[0;32mgecti\033[0m  %s\n" "$1"; n=$((n + 1)); }
dustu() { printf "  \033[0;31mDUSTU\033[0m  %s\n" "$1"; fail=1; n=$((n + 1)); }
icerir() { grep -qF -- "$2" <<<"$1"; }
goster() { sed 's/^/         /' <<<"$1"; }
# kos <dosya> <beklenen son satir> <etiket>
kos() {
    local out rc
    out=$(cd "$TMP" && "$TUL" "$1" 2>&1); rc=$?
    if [ $rc -eq 0 ] && [ "$(tail -1 <<<"$out" | tr -d '\r')" = "$2" ]; then gecti "$3"
    else dustu "$3 (rc=$rc)"; goster "$out"; fi
}

# 1) Ice aktaranin fonksiyonu, import'tan SONRA tanimli (oyunun durumu).
printf '%s\n' 'func modul_f(): int {' '    return ana_yardimci() + 1;' '}' > "$TMP/modul.tpr"
printf '%s\n' 'import "modul.tpr";' 'func ana_yardimci(): int { return 41; }' 'print(modul_f());' > "$TMP/ana.tpr"
kos ana.tpr 42 "modul ice aktaranin sonra tanimli fonksiyonunu cagirir"

# 2) Import'tan ONCE tanimli; kutulu (str) imza da.
printf '%s\n' 'func modul_s(): str { return ana_ad("x") + "!"; }' > "$TMP/modul_s.tpr"
printf '%s\n' 'func ana_ad(str s): str { return s + s; }' 'import "modul_s.tpr";' 'print(modul_s());' > "$TMP/ana_s.tpr"
kos ana_s.tpr 'xx!' "modul ice aktaranin once tanimli kutulu fonksiyonunu cagirir"

# 3) Global zaten gorunuyordu (gerileme korumasi): metin sirasindan bagimsiz.
printf '%s\n' 'func modul_g(): int { return g + 1; }' > "$TMP/modul_g.tpr"
printf '%s\n' 'import "modul_g.tpr";' 'int g = 5;' 'print(modul_g());' > "$TMP/ana_g.tpr"
kos ana_g.tpr 6 "modul ice aktaranin global'ini okur (degismedi)"

# 4) Dolayli: ana -> ara -> derin; derin ANA dosyanin ve ARA'nin fonksiyonunu
#    cagirir.
printf '%s\n' 'import "derin.tpr";' 'func ara_f(): int { return 100; }' 'func ara_giris(): int { return derin_f(); }' > "$TMP/ara.tpr"
printf '%s\n' 'func derin_f(): int { return ana_f() + ara_f(); }' > "$TMP/derin.tpr"
printf '%s\n' 'import "ara.tpr";' 'func ana_f(): int { return 23; }' 'print(ara_giris());' > "$TMP/ana_d.tpr"
kos ana_d.tpr 123 "dolayli: en derin modul anayi ve arayi gorur"

# 5) Yerel tanim kazanir (K043) — fonksiyonlarin erken bildirimi bunu bozmadi:
#    modulun kendi cagrisi da ana dosyanin ayni adli fonksiyonuna gider.
printf '%s\n' 'func yardim(): int { return 7; }' 'func modul_y(): int { return yardim(); }' > "$TMP/modul_y.tpr"
printf '%s\n' 'import "modul_y.tpr";' 'func yardim(): int { return 9; }' 'print(modul_y());' > "$TMP/ana_y.tpr"
kos ana_y.tpr 9 "ayni ad: yerel tanim kazanir (K043 degismedi)"

# 6) SONRA ice aktarilan kardes: gorunmez, ipucu kardesin adini ve cozumu verir.
printf '%s\n' 'func a_f(): int { return b_f() * 2; }' > "$TMP/a.tpr"
printf '%s\n' 'func b_f(): int { return 21; }' > "$TMP/b.tpr"
printf '%s\n' 'import "a.tpr";' 'import "b.tpr";' 'print(a_f());' > "$TMP/kardes.tpr"
out=$(cd "$TMP" && "$TUL" kardes.tpr 2>&1); rc=$?
if [ $rc -ne 0 ] && icerir "$out" "'b_f'" && icerir "$out" "--> a.tpr:1" \
   && icerir "$out" "defined in module 'b.tpr', which is imported AFTER" && icerir "$out" 'import "b.tpr";'; then
    gecti "sonraki kardes: net hata + ipucu (b.tpr)"
else dustu "sonraki kardes (rc=$rc)"; goster "$out"; fi

# 7) Ipucunun dedigini yapmak duzeltir: modul kardesi KENDISI ice aktarir
#    (ikinci import tekillestirilir).
printf '%s\n' 'import "b.tpr";' 'func a_f(): int { return b_f() * 2; }' > "$TMP/a.tpr"
kos kardes.tpr 42 "modul kardesi kendisi ice aktarinca calisir"

# Pozitif kontrol: gercekten tanimsiz ad hala hata (kural her adi
# gorunur yapmiyor).
printf '%s\n' 'func modul_t(): int { return hic_yok(); }' > "$TMP/modul_t.tpr"
printf '%s\n' 'import "modul_t.tpr";' 'print(modul_t());' > "$TMP/ana_t.tpr"
out=$(cd "$TMP" && "$TUL" ana_t.tpr 2>&1); rc=$?
if [ $rc -ne 0 ] && icerir "$out" "'hic_yok'"; then gecti "tanimsiz ad hala hata (pozitif kontrol)"
else dustu "tanimsiz ad (rc=$rc)"; goster "$out"; fi

[ "$fail" -eq 0 ] && echo "modul_ice_aktaran: $n/$n"
exit $fail
