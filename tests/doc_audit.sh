#!/bin/bash
# `tulpar doc` denetimi — belge üreteci de denetimsiz yüzeylerdendi.
#
# Ölçüldü: üç stdlib modülü (`router`, `middleware`, `http_utils`) HİÇ
# belgelenemiyordu. Sebep, `doc`un kodgen hatasında her şeyi atmasıydı; oysa
# o üç modül KARDEŞ modüllerin sembollerine bakıyor (`_router_port`,
# `_request`, `json_response`) ve tek başlarına derlenmiyor — birlikte import
# edildiklerinde tamamen geçerliler. Belge çıkarmak BİLDİRİMLERE bakar,
# derlemenin başarısına değil.
#
# Üç şey ölçülüyor:
#   1. her dosya için `doc` çıkış 0 veriyor,
#   2. çıktı BOŞ DEĞİL (başlık satırı var),
#   3. AYRIŞMAYAN dosya hâlâ HATA veriyor — "her zaman 0 dön" düzeltmesi
#      denetimi işe yaramaz hâle getirirdi.
set -u
cd "$(dirname "$0")/.."
TULPAR=./tulpar
[ -x "$TULPAR" ] || { echo "doc denetimi: ./tulpar yok — atlandi"; exit 0; }
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
fail=0
n=0
for f in examples/*.tpr lib/*.tpr; do
    [ -f "$f" ] || continue
    n=$((n + 1))
    if ! "$TULPAR" doc "$f" > "$TMP/out.md" 2>"$TMP/err"; then
        echo "  DOC HATA: $f"; sed -n '1,2p' "$TMP/err" | sed 's/^/     /'
        fail=$((fail + 1)); continue
    fi
    if ! grep -q '^# ' "$TMP/out.md"; then
        echo "  BOS/BASLIKSIZ CIKTI: $f"; fail=$((fail + 1))
    fi
done
# Ayrışmayan dosya HATA vermeli.
printf 'int x = ;\nprint(x)\n' > "$TMP/bozuk.tpr"
if "$TULPAR" doc "$TMP/bozuk.tpr" > /dev/null 2>&1; then
    echo "  AYRISMAYAN dosyaya 0 donuyor (denetim ise yaramaz olurdu)"
    fail=$((fail + 1))
fi
if [ "$fail" -gt 0 ]; then
    echo "doc denetimi BASARISIZ: $fail sorun"
    exit 1
fi
echo "doc denetimi temiz ($n dosya belgeleniyor)"
