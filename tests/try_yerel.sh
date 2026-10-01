#!/bin/bash
# TRY GOVDESINDE YAZILAN YEREL — YAPI KAPISI (2026-10-01, Tuzaklar 7i).
#
# Anlam testleri tests/try_yerel.test.tpr'de. Bu betik KARARIN kendisini
# optimizasyon ONCESI IR'dan okuyor:
#   1) try govdesinde yazilip catch/sonrasinda okunan yerelin (`t`) BUTUN
#      erisimleri volatile — yoksa SROA onu yazmaca alir, throw'dan onceki
#      saklama olu sayilir ve catch setjmp anindaki eski degeri gorur.
#   2) YALNIZ gerekli yereller: govdede bildirilip yalniz govdede kullanilan
#      yerel (`ic`, dongu sayaci `sayac`) volatile DEGIL — try icindeki sicak
#      dongu yazmacta kalmali. try'siz fonksiyonda hic volatile yok.
#   3) sonuc dogru (46: govdede t = 45 + 1, sonra throw).
#
# POZITIF KONTROL: TULPAR_NO_TRY_VOLATILE=1 isaretlemeyi kapatir; o derlemede
# modulde hic volatile olmamali VE sonuc eski hataya donmeli (0) — donmuyorsa
# ya kapi kor ya da hata baska bir yoldan duzelmis (o zaman bu duzeltme olu).
#
# Sabotajla dogrulandi (2026-10-01):
#   * govde bolgesi yalniz `try` giris blogu olacak bicimde bozulunca
#     (govdenin ekledigi bloklar kayda girmeyince) 1) ve tests/try_yerel.test.tpr
#     kirmizi (13 testin 11'i dustu).
#   * "govde disinda da kullaniliyor" kosulu kaldirilinca 2) kirmizi (`ic`
#     ve `sayac` volatile oldu).
# Eski derleyiciyle: tests/try_yerel.test.tpr 0/13, bu kapi 1) ve 3)'te dusuyor.
set -u
cd "$(dirname "$0")/.."
TULPAR="${1:-./tulpar}"
[ -x "$TULPAR" ] || { echo "try yerel kapisi: $TULPAR yok — atlandi"; exit 0; }
TULPAR="$(cd "$(dirname "$TULPAR")" && pwd)/$(basename "$TULPAR")"

TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
PASS=0
FAIL=0
ok()  { echo "  gecti  $1"; PASS=$((PASS + 1)); }
bad() { echo "  DUSTU  $1"; FAIL=$((FAIL + 1)); }

cat > "$TMP/s.tpr" <<'EOF'
func sicak(int n): int {
    int t = 0;
    try {
        int ic = 0;
        for (int sayac = 0; sayac < n; sayac = sayac + 1) { ic = ic + sayac; }
        t = ic;
        if (n < 0) { throw "neg"; }
        t = t + 1;
        throw "son";
    } catch (e) { }
    return t;
}
func tryyok(int n): int {
    int t = 0;
    for (int i = 0; i < n; i = i + 1) { t = t + i; }
    return t;
}
print(sicak(10), tryyok(10));
EOF

derle() {  # derle <cikti_adi> [ortam...]
  local out="$1"; shift
  (cd "$TMP" && env "$@" TULPAR_AOT_NOCACHE=1 TULPAR_AOT_EMIT_LL_PRE=1 "$TULPAR" build s.tpr "$out" >"$out.log" 2>&1)
}
# `<ad>` fonksiyonunun on-optimizasyon govdesi (kutulu t_<ad>.f ya da yerel <ad>).
fn_body() {
  sed -n "/^define .*@\(t_\)\{0,1\}$2\(\.f\)\{0,1\}(/,/^}/p" "$1"
}

if ! derle yeni; then
  echo "try yerel kapisi DUSTU: program derlenemedi"; sed -n '1,15p' "$TMP/yeni.log"; exit 1
fi
out=$("$TMP/yeni")
[ "$out" = "46 45" ] && ok "sonuc dogru (46 45)" \
  || bad "sonuc: '$out' (46 45 bekleniyordu — try yereli eski degerine dondu)"
LL="$TMP/yeni.pre.ll"
SB=$(fn_body "$LL" sicak)
[ -n "$SB" ] || bad "sicak fonksiyonunun govdesi IR'da bulunamadi (fn_body kor)"

# 1) catch'te okunan yerel volatile
if echo "$SB" | grep -Eq 'store volatile .*, ptr %t,' && \
   echo "$SB" | grep -Eq 'load volatile .*, ptr %t,'; then
  ok "try govdesinde yazilip catch sonrasi okunan 't' volatile (load+store)"
else
  bad "'t' volatile DEGIL — longjmp sonrasi bayat deger"
fi
# Butun erisimler: volatile olmayan t erisimi kalmamali.
nv=$(echo "$SB" | grep -E '(load|store) .*, ptr %t,' | grep -vc volatile)
[ "$nv" -eq 0 ] && ok "'t'nin volatile olmayan erisimi yok" \
  || bad "'t'nin $nv erisimi volatile degil"

# 2) yalniz gerekli yereller
if echo "$SB" | grep -Eq 'volatile .*, ptr %(ic|sayac)[0-9]*,'; then
  bad "govdede bildirilen 'ic'/'sayac' volatile — sicak dongu bellege dustu"
else
  ok "govdede dogup olen yerel ('ic', dongu sayaci) volatile degil"
fi
TB=$(fn_body "$LL" tryyok)
[ -n "$TB" ] || bad "tryyok govdesi IR'da bulunamadi"
echo "$TB" | grep -q volatile && bad "try'siz fonksiyonda volatile var" \
  || ok "try'siz fonksiyonda volatile yok"

# Pozitif kontrol
if derle kapali TULPAR_NO_TRY_VOLATILE=1; then
  grep -q volatile "$TMP/kapali.pre.ll" && bad "pozitif kontrol: kapaliyken de volatile var — denetim kor" \
    || ok "pozitif kontrol: TULPAR_NO_TRY_VOLATILE=1 ile volatile yok"
  kout=$("$TMP/kapali")
  [ "$kout" = "0 45" ] && ok "pozitif kontrol: kapaliyken eski hata geri geliyor (0 45)" \
    || bad "pozitif kontrol: kapaliyken sonuc '$kout' (0 45 bekleniyordu) — hata baska yoldan mi duzeldi?"
else
  bad "pozitif kontrol derlemesi basarisiz"
fi

echo "try_yerel: $PASS/$((PASS + FAIL))"
[ $FAIL -eq 0 ] || exit 1
exit 0
