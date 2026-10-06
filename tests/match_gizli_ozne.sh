#!/bin/bash
# MATCH'IN GIZLI OZNESI KULLANICI KODUNDAN ERISILEMEZ (2026-10-06).
#
# match oznesi tumu skaler bir struct ise heap'e (OBJ_STRUCT) kutulanip gizli
# bir yerele yaziliyor. OBJ_STRUCT alan adi/tipi TASIMIYOR (yalniz tip adi +
# 8 baytlik yuvalar); disari cikarsa konumsal ve ham yazilir. Gizli yerelin
# adi `__match_N` idi — gecerli bir tanimlayici: kolda `__match_0` yazmak onu
# disari veriyordu. Olculdu (taban derleyici): `R { 4612811918334230528, 0,
# 3 }` (2.5'in bit deseni, false 0, bool true 1), toJson `null`. Ad artik
# nokta iceriyor (`match.N`) — tanimlayici olamaz.
#
# Kapi: (1) kolda `__match_0` DERLENMEMELI ve hata o adi soylemeli; (2)
# pozitif kontrol: ayni program gecerli bir adla derlenip `R { f: 2.5, ok:
# false, n: 3 }` basmali (yani dusus addan, sozdiziminden degil). Taban
# derleyicide (1) derlenir ve ham metni basar -> KIRMIZI.
# Desenlerin her temsilde ayni sonucu: tests/match_struct_ozne.test.tpr.
set -u
cd "$(dirname "$0")/.."
TULPAR="${1:-./tulpar}"
[ -x "$TULPAR" ] || { echo "match gizli ozne kapisi: $TULPAR yok — atlandi"; exit 0; }
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
HATA=0

cat > "$TMP/sizinti.tpr" <<'TPREOF'
struct R { float f; bool ok; int n; }
R r = { f: 2.5, ok: false, n: 3 };
var g = match r { _ => __match_0 };
print(g);
TPREOF
cat > "$TMP/kontrol.tpr" <<'TPREOF'
struct R { float f; bool ok; int n; }
R r = { f: 2.5, ok: false, n: 3 };
var g = match r { _ => r };
print(g);
TPREOF

if LC_ALL=C TULPAR_AOT_NOCACHE=1 "$TULPAR" build "$TMP/sizinti.tpr" "$TMP/sizinti" >"$TMP/s.log" 2>&1; then
  echo "  gizli ozne ERISILEBILIR: kolda __match_0 derlendi; cikti: $("$TMP/sizinti" 2>&1 | tr -d '\r' | head -n 1)"
  HATA=1
else
  S_LOG=$(tr -d '\r' < "$TMP/s.log")
  if [[ "$S_LOG" != *"__match_0"* ]]; then
    echo "  derleme dustu ama hata __match_0'i soylemiyor:"; sed 's/^/    /' "$TMP/s.log" | head -5; HATA=1
  fi
fi
if ! LC_ALL=C TULPAR_AOT_NOCACHE=1 "$TULPAR" build "$TMP/kontrol.tpr" "$TMP/kontrol" >"$TMP/k.log" 2>&1; then
  echo "  POZITIF KONTROL: gecerli program derlenmedi"; sed 's/^/    /' "$TMP/k.log" | head -5; HATA=1
else
  K=$("$TMP/kontrol" 2>&1 | tr -d '\r')
  [ "$K" = "R { f: 2.5, ok: false, n: 3 }" ] || { echo "  POZITIF KONTROL: cikti '$K'"; HATA=1; }
fi

if [ "$HATA" -ne 0 ]; then echo "match gizli ozne kapisi DUSTU"; exit 1; fi
echo "match gizli ozne kapisi: GECTI (kolda __match_0 derlenmiyor; gecerli adla R { f: 2.5, ok: false, n: 3 })"
