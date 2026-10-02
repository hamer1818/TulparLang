#!/bin/bash
# OBJ BASLIGI KAPISI — codegen nesne turunu runtime'in genisliginde mi okuyor?
#
# 2026-10-02: Obj basligi 32 -> 8 bayt, `Obj::type` 4 -> 1 bayt
# (src/vm/obj_layout.h tek kaynak). Codegen'in satir ici dizi / struct dizisi
# yollari nesne turunu dogrudan yukleyip OBJ_ARRAY / OBJ_STRUCT_ARRAY ile
# karsilastiriyor. Bir yer hala `load i32` yaparsa komsu baytlari
# (arena_allocated, is_moved, ilklenmemis dolgu) da okur: sinav COGU ZAMAN
# tutmaz ve satir ici yol SESSIZCE devreden cikar — sonuc dogru, yalniz yavas.
# Hicbir anlambilim testi bunu goremez; bu kapi uretilen IR'ye bakar.
#
#   1. %struct.ObjArray / %struct.ObjStructArray IR tipi basligi
#      `{ i8, [7 x i8], ... }` diye modelliyor (obj_layout.h'den).
#   2. Tur yuklemeleri (adlari ot / otype) en az bir tane ve HEPSI `load i8`.
#   3. Program dogru sonucu basiyor.
# Pozitif kontrol: eski derleyici (Obj 32 B, i32 tur) bu kapida KIRMIZI —
# 2026-10-02'de 9dddaf38 ile izole dizinde kosturuldu.
set -u
cd "$(dirname "$0")/.."
TULPAR="${1:-./tulpar}"
[ -x "$TULPAR" ] || { echo "obj baslik kapisi: $TULPAR yok — atlandi"; exit 0; }

TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT

cat > "$TMP/prog.tpr" <<'TPREOF'
struct Nokta { int x; int y; }
func nokta(int x, int y): Nokta {
    Nokta p;
    p.x = x;
    p.y = y;
    return p;
}
func topla(int[] a): int {
    int t = 0;
    for (int i = 0; i < len(a); i = i + 1) { t = t + a[i]; }
    return t;
}
func is_yap(): int {
    int[] a = array_fill(100, 1);
    for (int i = 0; i < 100; i = i + 1) { a[i] = i; }
    Nokta[] ps = [];
    push(ps, nokta(1, 2));
    push(ps, nokta(3, 4));
    int s = 0;
    for (int i = 0; i < len(ps); i = i + 1) { s = s + ps[i].x + ps[i].y; }
    return topla(a) + s;
}
print(is_yap());
TPREOF

HATA=0
if ! TULPAR_AOT_EMIT_LL_PRE=1 "$TULPAR" build "$TMP/prog.tpr" "$TMP/prog" >"$TMP/derle.log" 2>&1; then
  echo "obj baslik kapisi DUSTU: sonda derlenmedi"; cat "$TMP/derle.log"; exit 1
fi
LL="$TMP/prog.pre.ll"
[ -f "$LL" ] || { echo "obj baslik kapisi DUSTU: IR dokumu yok ($LL)"; exit 1; }

OUT=$("$TMP/prog" | tr -d '\r')
[ "$OUT" = "4960" ] || { echo "  sonda ciktisi '$OUT' (beklenen 4960)"; HATA=1; }

# 1. IR tipleri.
for T in ObjArray ObjStructArray; do
  L=$(grep "^%struct.$T = type" "$LL" | tr -d '\r')
  case "$L" in
    *"{ i8, [7 x i8],"*) ;;
    *) echo "  %struct.$T basligi { i8, [7 x i8], ... } degil: $L"; HATA=1 ;;
  esac
done

# 2. Tur yuklemeleri.
YUK=$(grep -E "%([a-z]+\.)?ot(ype)?[0-9]* = load " "$LL" | tr -d '\r')
N=$(printf '%s\n' "$YUK" | grep -c "load" || true)
N8=$(printf '%s\n' "$YUK" | grep -c "= load i8," || true)
echo "  tur yuklemesi: $N (i8: $N8)"
[ "$N" -ge 2 ] || { echo "  tur yuklemesi bulunamadi (satir ici dizi yolu uretilmedi mi?)"; HATA=1; }
[ "$N" -eq "$N8" ] || { echo "  i8 OLMAYAN tur yuklemesi var:"; printf '%s\n' "$YUK" | grep -v "= load i8," | sed 's/^/    /'; HATA=1; }

if [ "$HATA" -ne 0 ]; then
  echo "obj baslik kapisi DUSTU"
  exit 1
fi
echo "obj baslik kapisi: GECTI (ObjArray/ObjStructArray basligi i8 + [7 x i8], $N tur yuklemesinin hepsi i8, sonuc 4960)"
