#!/bin/bash
# Biçimlendirici denetimi: `tulpar fmt` KULLANICININ KAYNAĞINI değiştiriyor
# (`--write`), yani buradaki bir hata doğrudan veri kaybı.
#
# Bu denetim yokken iki hata birden hayatta kaldı ve ölçüldü:
#   • `i++` → `i + +` yazılıyordu, yani biçimlendirici DERLENMEYEN kod
#     üretiyordu. Depoda 84 dosya `++`/`--` kullanıyor.
#   • `tulpar typecheck` ayrıştırma hatasını basıp yine de 0 dönüyordu, yani
#     bozulmayı yakalayabilecek tek kapı da körüdü.
#
# Üç şey ölçülüyor, her `examples/*.tpr` ve `lib/*.tpr` için:
#   1. fmt çalışıyor (çıkış 0),
#   2. IDEMPOTENT: fmt(fmt(x)) == fmt(x) — değilse kaydet-biçimlendir
#      döngüsünde dosya sonsuza kadar değişir,
#   3. çıktı hâlâ AYRIŞIYOR (`typecheck` çıkışı 2 değil). Tip uyarısı (1)
#      biçimlendiricinin suçu değil, yalnız ayrıştırma hatası sayılıyor.
set -u
cd "$(dirname "$0")/.."
TULPAR=./tulpar
[ -x "$TULPAR" ] || { echo "fmt denetimi: ./tulpar yok — atlandi"; exit 0; }
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
fail=0
n=0
for f in examples/*.tpr examples/en/*.tpr lib/*.tpr; do
    [ -f "$f" ] || continue
    n=$((n + 1))
    if ! "$TULPAR" fmt "$f" > "$TMP/a.tpr" 2>"$TMP/err"; then
        echo "  FMT HATA: $f"; sed -n '1,3p' "$TMP/err"; fail=$((fail + 1)); continue
    fi
    if ! "$TULPAR" fmt "$TMP/a.tpr" > "$TMP/b.tpr" 2>/dev/null; then
        echo "  FMT HATA (ikinci gecis): $f"; fail=$((fail + 1)); continue
    fi
    if ! diff -q "$TMP/a.tpr" "$TMP/b.tpr" > /dev/null; then
        echo "  IDEMPOTENT DEGIL: $f"
        diff "$TMP/a.tpr" "$TMP/b.tpr" | head -4 | sed 's/^/      /'
        fail=$((fail + 1)); continue
    fi
    "$TULPAR" typecheck "$TMP/a.tpr" > /dev/null 2>&1
    if [ $? -eq 2 ]; then
        echo "  AYRISMIYOR: $f (biçimlendirici bozdu)"
        "$TULPAR" typecheck "$TMP/a.tpr" 2>&1 | head -4 | sed 's/^/      /'
        fail=$((fail + 1))
    fi
done
if [ "$fail" -gt 0 ]; then
    echo "fmt denetimi BASARISIZ: $n dosyanin $fail tanesi"
    exit 1
fi
echo "fmt denetimi temiz ($n dosya: idempotent + ayrisiyor)"
