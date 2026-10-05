#!/usr/bin/env bash
# macOS TLS PROGRAMI BOYUT OLCUMU — elle (macOS) kosulur, CI'da kosmaz.
# Docs: docs/mindmap/Build System.md "macOS TLS programi kucultme" tablosu
# (2026-10-05, PR #465'te CI'da bu betikle olculdu). Kipler surucuden
# bagimsizdir (sarici surucunun runtime/dead_strip bayraklarini kendi
# secimiyle degistirir):
#   v0   ESKI link satiri (runtime acik, dead_strip yok)
#   ds   + -Wl,-dead_strip           (yalniz: -rdynamic yuzunden kazanc yok)
#   g    runtime -load_hidden        (yalniz gizleme)
#   dsg  dead_strip + gizli runtime  (= bugunku surucu satiri)
#   dsgx dsg + -Wl,-x
#   oz*  ayni kipler, OpenSSL kaynaktan `no-*` ile derlenmis arsivlerle
#        (OLC_OPENSSL=0 ile atlanir; arsiv zaten kucuk OpenSSL'liyse anlamsiz)
# Her biri: boyut, `strip -x` sonrasi boyut, calisiyor mu (tls:var).
#   tools/tls_boyut_olc.sh <tulpar> <libtulpar_runtime.a>
set -u
T=$(cd "$(dirname "$1")" && pwd)/$(basename "$1")
A=$(cd "$(dirname "$2")" && pwd)/$(basename "$2")
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
cd "$TMP" || exit 1

openssl req -x509 -newkey rsa:2048 -nodes -days 1 -subj /CN=localhost \
    -keyout anahtar.pem -out sertifika.pem >/dev/null 2>&1 || { echo "openssl req yok"; exit 1; }
cat > tls.tpr <<T2
int ctx = tls_init("$TMP/sertifika.pem", "$TMP/anahtar.pem");
if (ctx != 0) { print("tls:var"); } else { print("tls:yok"); }
T2
printf 'print("sade");\n' > sade.tpr

# Surucu sarici: runtime arsivini (-ltulpar_runtime ya da
# -Wl,-hidden-ltulpar_runtime) OLC_ARSIV ile (gizli ya da acik) degistirir,
# surucunun -dead_strip'ini atar, OLC_EK bayraklarini ekler, komutu kaydeder.
# Yani v0 her surucude ESKI link satiri; kipler surucuden bagimsiz.
cat > sarici.sh <<'S'
#!/bin/bash
args=()
for a in "$@"; do
  if { [ "$a" = "-ltulpar_runtime" ] || [ "$a" = "-Wl,-hidden-ltulpar_runtime" ]; } && [ -n "${OLC_ARSIV:-}" ]; then
    if [ "${OLC_GIZLI:-0}" = 1 ]; then args+=("-Wl,-load_hidden,$OLC_ARSIV"); else args+=("$OLC_ARSIV"); fi
  elif [ "$a" = "-Wl,-dead_strip" ]; then
    :   # surucunun kendi dead_strip'i: kipler OLC_EK ile acikca seciyor
  else
    args+=("$a")
  fi
done
echo "/usr/bin/clang++ ${args[*]} ${OLC_EK:-}" >> "$OLC_KAYIT"
exec /usr/bin/clang++ "${args[@]}" ${OLC_EK:-}
S
chmod +x sarici.sh
export OLC_KAYIT="$TMP/komutlar.txt"

# olc <ad> <program> <arsiv> <gizli:0|1> <ek bayraklar>
olc() {
    local ad=$1 prg=$2 ars=$3 gz=$4 ek=$5 t0 t1 b s out
    rm -f "$ad"
    t0=$(date +%s)
    if ! OLC_ARSIV="$ars" OLC_GIZLI="$gz" OLC_EK="$ek" TULPAR_CC="$TMP/sarici.sh" \
            TULPAR_AOT_NOCACHE=1 "$T" build "$prg" "$ad" > "$ad.log" 2>&1 || [ ! -x "$ad" ]; then
        echo "OLCUM $ad: LINK DUSTU"; tail -8 "$ad.log" | sed 's/^/    /'; return
    fi
    t1=$(date +%s)
    b=$(wc -c < "$ad" | tr -d ' ')
    cp "$ad" "$ad.x"; strip -x "$ad.x" 2>/dev/null; s=$(wc -c < "$ad.x" | tr -d ' ')
    out=$("./$ad" 2>&1 | tail -1)
    echo "OLCUM $ad: $b bayt, strip -x: $s bayt, cikti '$out', build $((t1 - t0)) s"
    echo "| $ad | $b | $s | $out |" >> "${GITHUB_STEP_SUMMARY:-/dev/null}"
}

echo "| kip | bayt | strip -x | cikti |" >> "${GITHUB_STEP_SUMMARY:-/dev/null}"
echo "|---|---|---|---|" >> "${GITHUB_STEP_SUMMARY:-/dev/null}"
olc v0       tls.tpr  "$A" 0 ""
olc ds       tls.tpr  "$A" 0 "-Wl,-dead_strip"
olc dsg      tls.tpr  "$A" 1 "-Wl,-dead_strip"
olc g        tls.tpr  "$A" 1 ""
olc dsgx     tls.tpr  "$A" 1 "-Wl,-dead_strip -Wl,-x"
olc sade_v0  sade.tpr "$A" 0 ""
olc sade_dsg sade.tpr "$A" 1 "-Wl,-dead_strip"
# Surucunun KENDI link satiri (sarici yok): yeni varsayilan gercekten bu mu?
TULPAR_AOT_NOCACHE=1 "$T" build tls.tpr surucu > surucu.log 2>&1 && \
    echo "OLCUM surucu-varsayilan: $(wc -c < surucu | tr -d ' ') bayt, cikti '$(./surucu 2>&1 | tail -1)'"

# Bilesim: v0 ve dsg link haritasi, arsiv uyesi grubuna gore __TEXT+__DATA.
harita() {
    local ad=$1 gz=$2 ek=$3
    OLC_ARSIV="$A" OLC_GIZLI="$gz" OLC_EK="$ek -Wl,-map,$TMP/$ad.map" TULPAR_CC="$TMP/sarici.sh" \
        TULPAR_AOT_NOCACHE=1 "$T" build tls.tpr "$ad.h" > /dev/null 2>&1 || { echo "harita $ad: link dustu"; return; }
    python3 - "$TMP/$ad.map" "$ad" <<'P'
import re, sys, collections
yol, ad = sys.argv[1], sys.argv[2]
dos = {}; boy = collections.Counter(); bolum = None
for l in open(yol, errors='replace'):
    if l.startswith('# Object files:'): bolum = 'o'; continue
    if l.startswith('# Sections:'): bolum = 's'; continue
    if l.startswith('# Symbols:'): bolum = 'y'; continue
    if l.startswith('# Dead Stripped'): bolum = None; continue
    if bolum == 'o':
        m = re.match(r'\[\s*(\d+)\]\s+(.*)', l)
        if m: dos[int(m.group(1))] = m.group(2).strip()
    elif bolum == 'y':
        m = re.match(r'0x[0-9A-Fa-f]+\s+0x([0-9A-Fa-f]+)\s+\[\s*(\d+)\]', l)
        if m:
            f = dos.get(int(m.group(2)), '?')
            if 'libcrypto' in f: g = 'libcrypto'
            elif 'libssl' in f: g = 'libssl'
            elif 'libtulpar_runtime' in f or '.a(' in f: g = 'runtime (diger uye)'
            elif f.endswith('.o'): g = 'kullanici nesnesi'
            else: g = 'diger (' + f.split('/')[-1][:30] + ')'
            boy[g] += int(m.group(1), 16)
top = sum(boy.values())
print(f'HARITA {ad}: toplam {top} bayt (sembol boyutlari)')
for g, b in boy.most_common(8):
    print(f'    {g:28s} {b:>10d}  %{100*b/max(top,1):.1f}')
P
}
harita v0 0 ""
harita dsg 1 "-Wl,-dead_strip"

# OpenSSL kaynaktan, kucultulmus. Surumu Homebrew'unkiyle ayni (basliklar
# runtime'in derlendigi baslik setiyle uyusur).
[ "${OLC_OPENSSL:-1}" = 1 ] || exit 0
V=$(brew list --versions openssl@3 2>/dev/null | awk '{print $2}' | sed 's/_[0-9]*$//')
[ -n "$V" ] || { echo "openssl@3 surumu okunamadi"; exit 0; }
echo "OpenSSL kaynak surumu: $V"
t0=$(date +%s)
curl -fsSL -o ossl.tgz "https://github.com/openssl/openssl/releases/download/openssl-$V/openssl-$V.tar.gz" || { echo "OpenSSL $V indirilemedi"; exit 0; }
tar xzf ossl.tgz && cd "openssl-$V" || exit 0
t1=$(date +%s)
SECENEK="no-shared no-tests no-docs no-legacy no-deprecated no-engine no-dso no-comp no-zlib
 no-ssl3 no-dtls no-srp no-gost no-idea no-cast no-seed no-rc2 no-rc4 no-rc5 no-md4 no-mdc2
 no-whirlpool no-bf no-camellia no-aria no-sm2 no-sm3 no-sm4 no-siphash no-ocb no-cms no-ts
 no-srtp no-sctp no-ct no-ec2m no-scrypt no-cmp no-quic no-async no-uplink no-module
 no-ui-console"
# shellcheck disable=SC2086
if ! ./Configure darwin64-arm64-cc $SECENEK -Os > ../cfg.log 2>&1; then
    grep -i 'unsupported' ../cfg.log
    echo "Configure dustu:"; tail -15 ../cfg.log; exit 0
fi
NJ=$(sysctl -n hw.ncpu)
if ! make -j"$NJ" build_libs > ../make.log 2>&1; then
    echo "make dustu:"; tail -20 ../make.log; exit 0
fi
t2=$(date +%s)
echo "OLCUM openssl kaynak: indirme+acma $((t1 - t0)) s, Configure+make build_libs $((t2 - t1)) s (-j$NJ)"
ls -l libssl.a libcrypto.a
cd "$TMP" || exit 1
# Runtime'in kendi uyeleri (CMake adlari *.c.o / *.cpp.o) + yeni OpenSSL.
mkdir -p uye && cd uye || exit 1
ar -t "$A" | grep -E '\.(c|cc|cpp|m|mm)\.o$' > ../uyeler.txt
ar -x "$A" $(cat ../uyeler.txt)
cd "$TMP" || exit 1
echo "runtime uyesi: $(wc -l < uyeler.txt)"
libtool -static -no_warning_for_no_symbols -o ozel.a uye/*.o "openssl-$V/libssl.a" "openssl-$V/libcrypto.a" 2>&1 | tail -3
ls -l ozel.a "$A"
olc oz   tls.tpr "$TMP/ozel.a" 0 ""
olc ozdsg tls.tpr "$TMP/ozel.a" 1 "-Wl,-dead_strip"
olc ozdsgx tls.tpr "$TMP/ozel.a" 1 "-Wl,-dead_strip -Wl,-x"
olc oz_sade sade.tpr "$TMP/ozel.a" 1 "-Wl,-dead_strip"
exit 0
