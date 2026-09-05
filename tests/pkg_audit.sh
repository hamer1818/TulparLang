#!/bin/bash
# `tulpar pkg` denetimi — paket yöneticisi kullanıcının PROJE DİZİNİNE yazıyor
# (`tulpar.toml`, `tulpar_modules/`), yani buradaki hata kullanıcının
# dosyalarını etkiliyor.
#
# Ölçüm sonucu: pkg SAĞLAM. Bu denetim bulunan bir hatayı değil, çalışan bir
# yüzeyi koruyor — LSP'de olduğu gibi.
#
# Ölçülenler:
#   1. init -> add(path:) -> install zinciri,
#   2. vendor edilen paket GERÇEKTEN import edilebiliyor (asıl iddia bu:
#      dosyayı kopyalamak yetmez, çözümleme yolunda da görünmeli),
#   3. list/remove toml'u doğru düzenliyor,
#   4. OLMAYAN yol SIFIRDAN FARKLI dönüyor — hatayı basıp 0 dönmek bu depoda
#      görülmüş bir sınıf (`tulpar typecheck` aynısını yapıyordu) ve
#      `pkg install && build` zincirini sessizce bozardı,
#   5. ULAŞILAMAYAN registry asılmıyor, net konuşuyor ve 0 dönmüyor.
set -u
cd "$(dirname "$0")/.."
ROOT=$(pwd)
TULPAR="$ROOT/tulpar"
[ -x "$TULPAR" ] || { echo "pkg denetimi: ./tulpar yok — atlandi"; exit 0; }
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
fail=0
note() { echo "  $1"; fail=$((fail + 1)); }

mkdir -p "$TMP/dep/mathx" "$TMP/proje"
cat > "$TMP/dep/mathx/mathx.tpr" <<'EOF'
func kare(int n): int { return n * n; }
EOF
cd "$TMP/proje" || exit 1

"$TULPAR" pkg init deneme > /dev/null 2>&1 || note "pkg init basarisiz"
[ -f tulpar.toml ] || note "pkg init tulpar.toml yazmadi"
"$TULPAR" pkg add "mathx@path:../dep/mathx" > /dev/null 2>&1 || note "pkg add basarisiz"
grep -q 'mathx = "path:../dep/mathx"' tulpar.toml || note "pkg add bagimliligi yazmadi"
"$TULPAR" pkg install > /dev/null 2>&1 || note "pkg install basarisiz"
[ -f tulpar_modules/mathx/mathx.tpr ] || note "pkg install vendor etmedi"

# ASIL İDDİA: vendor edilen paket import edilebiliyor.
cat > kullan.tpr <<'EOF'
import "mathx";
print(kare(7));
EOF
out=$(DISPLAY= "$TULPAR" kullan.tpr 2>&1 | tail -1)
[ "$out" = "49" ] || note "vendor edilen paket import edilemedi (ciktı: $out)"

"$TULPAR" pkg list 2>&1 | grep -q "mathx" || note "pkg list bagimliligi gostermiyor"
"$TULPAR" pkg remove mathx > /dev/null 2>&1 || note "pkg remove basarisiz"
grep -q "mathx" tulpar.toml && note "pkg remove satiri birakti"

# OLMAYAN yol: hatayı basıp 0 dönmek `pkg install && build` zincirini
# sessizce bozardı.
"$TULPAR" pkg add "yok@path:../hicboyle" > /dev/null 2>&1
if "$TULPAR" pkg install > /dev/null 2>&1; then
    note "olmayan yolda pkg install 0 donuyor"
fi

# ULAŞILAMAYAN registry: asılmamalı, 0 dönmemeli.
#
# `timeout` GNU coreutils'te; macOS'ta YOK (orada brew ile `gtimeout`).
# Ciplak `timeout` bu satiri macOS'ta "command not found" ile duserirdi —
# fark edilmedi cunku bu denetim orada hic kosmuyordu. Hicbiri yoksa
# zaman siniri olmadan kosuyor: CI adim zaman asimi yedek.
TO=""
if command -v timeout >/dev/null 2>&1; then TO="timeout 30"
elif command -v gtimeout >/dev/null 2>&1; then TO="gtimeout 30"; fi
if $TO "$TULPAR" pkg search test --registry "http://127.0.0.1:9" \
        > "$TMP/s.txt" 2>&1; then
    note "olu registry'de pkg search 0 donuyor"
fi
[ $? -eq 124 ] && note "pkg search ASILDI (zaman asimi)"

cd "$ROOT" || exit 1
if [ "$fail" -gt 0 ]; then
    echo "pkg denetimi BASARISIZ: $fail sorun"
    exit 1
fi
echo "pkg denetimi temiz (init/add/install/import/list/remove + hata yollari)"
