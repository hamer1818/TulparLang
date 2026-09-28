#!/usr/bin/env bash
# FOR-IN SAYACLI DONGUYLE AYNI YOLDAN MI? (K209, 2026-09-27) — YAPISAL kapi.
#
# `for (x in a)` bir sayacli donguye aciliyor. Kosul `length(it)` iken
# sayacli dongunun kanitli yolu (sekil onbellegi + surumleme + vektorlestirme)
# kosuldaki uzunlugu onbellekten okuyamiyor, her turda aot_len cagriliyordu.
# Olculdu (20M int[], tek gecis, Ryzen 7 9800X3D, LLVM 22): for-in 19 ms,
# ayni isi yapan `for (j = 0; j < len(a); ...)` 2 ms. Acilim `len` kullaninca
# for-in de 2 ms.
#
# Neden zamanli degil yapisal: duvar saati esigi yuk altinda koşucuyu olcer
# (Tuzaklar 1l) ve vektorlestirme LLVM surumune bagli (CI 18, yerel 22). Bu
# kapi optimizasyon ONCESI IR'a (bizim kodgenimizin ciktisi, LLVM'den
# bagimsiz) bakiyor: ayni isi yapan for-in ve sayacli program, `aot_len`
# cagrisi ve `length` yolu (`len_result`) sayisinda AYNI olmali.
#
# Pozitif kontrol: onceki derleyici (acilimda `length`) bu kapida KIRMIZI —
# for-in 5 aot_len + 4 len_result, sayacli 3 + 0 (olculdu 2026-09-27).
#
#   tests/forin_sekil.sh [tulpar_yolu]
set -uo pipefail
TUL="${1:-./tulpar}"
[ -x "$TUL" ] || { echo "HATA: '$TUL' calistirilabilir degil" >&2; exit 1; }
TUL="$(cd "$(dirname "$TUL")" && pwd)/$(basename "$TUL")"
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT

cat > "$TMP/fi.tpr" <<'TPREOF'
int n = toInt(env("FI_N"));
if (n <= 0) { n = 64; }
int[] a = array_fill(n, 3);
int s = 0;
for (x in a) { s = s + x; }
print(s);
TPREOF
cat > "$TMP/fc.tpr" <<'TPREOF'
int n = toInt(env("FI_N"));
if (n <= 0) { n = 64; }
int[] a = array_fill(n, 3);
int s = 0;
for (int j = 0; j < len(a); j = j + 1) { s = s + a[j]; }
print(s);
TPREOF

for p in fi fc; do
    if ! (cd "$TMP" && TULPAR_AOT_EMIT_LL_PRE=1 TULPAR_AOT_NOCACHE=1 "$TUL" build "$p.tpr" "$p" >/dev/null 2>&1); then
        echo "forin_sekil DUSTU: $p.tpr derlenemedi"; exit 1
    fi
    [ -f "$TMP/$p.pre.ll" ] || { echo "forin_sekil DUSTU: $p.pre.ll yok (TULPAR_AOT_EMIT_LL_PRE)"; exit 1; }
done
say() { grep -c "$1" "$2" || true; }
fi_len=$(say 'call.*@aot_len' "$TMP/fi.pre.ll"); fc_len=$(say 'call.*@aot_len' "$TMP/fc.pre.ll")
fi_lr=$(say 'len_result' "$TMP/fi.pre.ll");     fc_lr=$(say 'len_result' "$TMP/fc.pre.ll")
out_fi=$(FI_N=1000 "$TMP/fi"); out_fc=$(FI_N=1000 "$TMP/fc")
if [ "$out_fi" != "3000" ] || [ "$out_fc" != "3000" ]; then
    echo "forin_sekil DUSTU: yanlis sonuc (for-in '$out_fi', sayacli '$out_fc', 3000 olmali)"; exit 1
fi
if [ "$fi_len" != "$fc_len" ] || [ "$fi_lr" != "$fc_lr" ]; then
    echo "forin_sekil DUSTU: for-in sayacli donguden FARKLI yoldan geciyor" \
         "(aot_len $fi_len/$fc_len, len_result $fi_lr/$fc_lr) — kanitli hizli yol kayboldu"
    exit 1
fi
echo "forin_sekil: for-in sayacli dongunun yolunda (aot_len $fi_len, len_result $fi_lr; sonuc 3000)"
exit 0
