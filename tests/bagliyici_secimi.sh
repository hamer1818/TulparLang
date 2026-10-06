#!/bin/bash
# BAGLAYICI SECIMI KAPISI (2026-10-06) — Linux'ta `ld.lld` varsa varsayilan
# link onunla, uretilen KOD ld.bfd ile ayni.
#
# Neden: her programin odedigi sabit link bedeli ld.bfd'de ~42 ms, lld'de ~15
# ms (hello world, Ryzen 7 9800X3D). aot_link_driver() (aot_pipeline.cpp)
# PATH'te ld.lld bulursa `-fuse-ld=lld -Wl,-z,keep-text-section-prefix`
# ekliyor; TULPAR_LD=bfd eski yola, =lld zorlar; TULPAR_CC verilmisse
# dokunmaz. Calisma hizi iddiasi DEGIL bu kapinin isi (o olcum
# Performance.md "Link tabani"); bu kapi MEKANIZMAYI ve KOD AYNILIGINI olcer:
#   1. varsayilan link gercekten lld (`.comment`ta "Linker: LLD"), program kosar;
#   2. TULPAR_LD=bfd ile lld YOK (anahtar okunuyor — pozitif kontrol);
#   3. call("ad") — -rdynamic + dlsym lld ile de calisiyor;
#   4. iki ikilinin KULLANICI fonksiyonlari komut komut ayni (adresler
#      maskeli, adres yukleme gevsetmesi `mov r,imm` ~ `lea r,[rip+d]`).
# Eski derleyicide 1. adim kirmizi (bfd).
# lld yoksa: yerelde GORUNUR ATLANDI; CI'da (CI=true) KIRMIZI — Linux CI
# `lld` paketini kuruyor, yani varsayilan yol orada da olculuyor.
set -u
cd "$(dirname "$0")/.."
TULPAR="${1:-./tulpar}"
[ -x "$TULPAR" ] || { echo "baglayici kapisi: $TULPAR yok — atlandi"; exit 0; }
case "$(uname -s)" in
  Linux*) ;;
  *) echo "baglayici kapisi: ATLANDI — Linux disi (ld64 / MinGW ld degismedi)"; exit 0 ;;
esac
if [ -n "${TULPAR_CC:-}" ] || [ -n "${TULPAR_LD:-}" ]; then
  echo "baglayici kapisi: ATLANDI — TULPAR_CC/TULPAR_LD ortamda verilmis (varsayilan olculemez)"; exit 0
fi
if ! command -v ld.lld >/dev/null 2>&1; then
  if [ "${CI:-}" = "true" ]; then
    echo "baglayici kapisi DUSTU: CI'da ld.lld yok (build.yml Linux paketleri 'lld' icermeli)"; exit 1
  fi
  echo "baglayici kapisi: ATLANDI — ld.lld PATH'te yok (varsayilan bfd kaliyor)"; exit 0
fi
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
HATA=0
cat > "$TMP/p.tpr" <<'TPREOF'
func kare(int x): int { return x * x; }
func selam(str ad): str { return "merhaba " + ad; }
int t = 0;
for (int i = 0; i < 10; i = i + 1) { t = t + kare(i); }
print(t);
print(call("selam", "lld"));
TPREOF
BEK=$'285\nmerhaba lld'
derle() { env TULPAR_AOT_NOCACHE=1 "$@" "$TULPAR" build "$TMP/p.tpr" "$TMP/$OUT" >"$TMP/$OUT.log" 2>&1; }
OUT=lld derle
OUT=bfd derle TULPAR_LD=bfd
for v in lld bfd; do
  [ -x "$TMP/$v" ] || { echo "  $v ikilisi kurulmadi:"; sed 's/^/    /' "$TMP/$v.log" | head -5; HATA=1; continue; }
  O=$("$TMP/$v" 2>&1 | tr -d '\r')
  [ "$O" = "$BEK" ] || { echo "  $v ciktisi '$O' (beklenen 285 / merhaba lld)"; HATA=1; }
done
if [ "$HATA" -eq 0 ]; then
  C_L=$(readelf -p .comment "$TMP/lld" 2>/dev/null)
  C_B=$(readelf -p .comment "$TMP/bfd" 2>/dev/null)
  if [[ "$C_L" != *"Linker: LLD"* ]]; then
    echo "  varsayilan link lld DEGIL (.comment'ta 'Linker: LLD' yok)"; HATA=1
    OUT=zorla derle TULPAR_LD=lld
    echo "    tani: ld.lld=$(command -v ld.lld) -> $(readlink -f "$(command -v ld.lld)"); PATH=$PATH"
    echo "    tani: varsayilan .comment: $(tr '\n' ' ' <<< "$C_L")"
    echo "    tani: TULPAR_LD=lld .comment: $(readelf -p .comment "$TMP/zorla" 2>/dev/null | tr '\n' ' ')"
  fi
  [[ "$C_B" != *"Linker: LLD"* ]] || { echo "  TULPAR_LD=bfd ile de lld — anahtar okunmuyor"; HATA=1; }
  if command -v objdump >/dev/null 2>&1; then
    FARK=$(python3 - "$TMP/bfd" "$TMP/lld" <<'PYEOF'
import re, subprocess, sys
def f(p):
    out = subprocess.run(['objdump', '-d', '--no-show-raw-insn', '-M', 'intel', p],
                         capture_output=True, text=True).stdout
    F = {}; cur = None
    for ln in out.split('\n'):
        m = re.match(r'^[0-9a-f]+ <(.+)>:$', ln)
        if m:
            cur = m.group(1); F[cur] = []; continue
        m = re.match(r'^\s+[0-9a-f]+:\s+(.*)$', ln)
        if not m or cur is None: continue
        x = re.sub(r'<[^>]*>', '', m.group(1))
        x = re.sub(r'0x[0-9a-f]+|\b[0-9a-f]{5,}\b', 'X', x).strip()
        if re.match(r'^(nop|int3|xchg\s+ax,ax|cs nop|data16)', x): continue
        x = re.sub(r'^mov\s+(\w+),X$', r'ADR \1', x)
        x = re.sub(r'^lea\s+(\w+),\[rip\+X\].*$', r'ADR \1', x)
        F[cur].append(x)
    return F
a, b = f(sys.argv[1]), f(sys.argv[2])
u = [k for k in a if (k == 'main' or k.startswith('t_') or k.startswith('tulpar.')) and k in b]
bad = [k for k in u if a[k] != b[k]]
print(len(u), len(bad), ' '.join(bad[:5]))
PYEOF
)
    read -r NU NB REST <<< "$FARK"
    if [ "${NU:-0}" -lt 2 ]; then echo "  kullanici fonksiyonu bulunamadi ($FARK)"; HATA=1
    elif [ "$NB" -ne 0 ]; then echo "  bfd/lld kullanici kodu FARKLI: $REST"; HATA=1
    else ADIM4="kullanici kodu ayni ($NU fonksiyon)"; fi
  else
    ADIM4="kod karsilastirmasi ATLANDI (objdump yok)"
  fi
fi
if [ "$HATA" -ne 0 ]; then echo "baglayici kapisi DUSTU"; exit 1; fi
echo "baglayici kapisi: GECTI (varsayilan lld, TULPAR_LD=bfd ile bfd, call() calisiyor, ${ADIM4})"
