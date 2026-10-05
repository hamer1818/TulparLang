#!/usr/bin/env bash
# macOS: OpenSSL'i KAYNAKTAN, yalniz kullanilan parcalarla derler (yayin CI'i).
#
# NIYE: macOS'ta OpenSSL statik arsiv olarak libtulpar_runtime.a'nin icine
# katiliyor (TULPAR_TLS_IN_RUNTIME, #463) ve TLS kullanan her `tulpar build`
# ciktisi onu tasiyor. Homebrew'un openssl@3'u her seyi aciyor (legacy
# saglayici, engine, GOST, SM2/3/4, QUIC, CMS, ...); TLS istemci/sunucu icin
# gereken cok daha az. Olculdu (2026-10-05, macOS CI arm64, PR #465,
# tools/tls_boyut_olc.sh), TLS programi, -dead_strip + gizli runtime ile:
#   Homebrew openssl@3 3.6.4        4 451 776 bayt
#   bu betik (ayni surum, no-*)    3 190 488 bayt  (-%28; eski satira gore -%43)
# Bedel (macos-latest, -j3): olcum turunda Configure + make build_libs 49 s,
# bu betikle (install_dev dahil) 55 s; indirme + dogrulama 2 s. CI'da surum +
# betik ozeti anahtariyla onbelleklenir, sicak kosu ~0.
#
# GUVENLIK: surum ELLE SABITLENMEZ — her koşumda Homebrew'un openssl@3
# surumu okunur (`brew list --versions`), yani yama Homebrew'a dustugu gun
# buraya da duser. Tarball'in SHA-256'si OpenSSL'in kendi yayinindaki
# .sha256 dosyasiyla dogrulanir.
#
# SERTIFIKA YOLU: --openssldir Homebrew'unkiyle AYNI ($(brew --prefix)/etc/
# openssl@3); SSL_CTX_set_default_verify_paths eskisiyle ayni yere bakar.
#
#   tools/openssl_kucuk_derle.sh <surum> <kurulum-dizini>
set -euo pipefail

V=${1:?surum}
HEDEF=${2:?kurulum dizini}
ISDIR=$(mktemp -d)
trap 'rm -rf "$ISDIR"' EXIT
OPENSSLDIR="$(brew --prefix)/etc/openssl@3"

# Kapatilanlar: TLS istemci/sunucu (runtime_net.cpp, http_fetch.cpp) bunlarin
# hicbirine dokunmuyor. `no-deprecated` icin kaynak OPENSSL_init_ssl'e
# gecirildi (SSL_library_init & co. 1.1.0'dan beri yalniz makro).
SECENEK=(no-shared no-tests no-docs no-legacy no-deprecated no-engine no-dso
  no-comp no-zlib no-ssl3 no-dtls no-srp no-gost no-idea no-cast no-seed
  no-rc2 no-rc4 no-rc5 no-md4 no-mdc2 no-whirlpool no-bf no-camellia no-aria
  no-sm2 no-sm3 no-sm4 no-siphash no-ocb no-cms no-ts no-srtp no-sctp no-ct
  no-ec2m no-scrypt no-cmp no-quic no-async no-uplink no-module no-ui-console)

t0=$(date +%s)
URL="https://github.com/openssl/openssl/releases/download/openssl-$V/openssl-$V.tar.gz"
curl -fsSL --retry 3 -o "$ISDIR/o.tgz" "$URL"
curl -fsSL --retry 3 -o "$ISDIR/o.tgz.sha256" "$URL.sha256"
beklenen=$(grep -oE '[0-9a-f]{64}' "$ISDIR/o.tgz.sha256" || true)
bulunan=$(shasum -a 256 "$ISDIR/o.tgz" | awk '{print $1}')
if [ "$beklenen" != "$bulunan" ]; then
    echo "HATA: openssl-$V.tar.gz SHA-256 uyusmuyor (beklenen $beklenen, bulunan $bulunan)" >&2
    exit 1
fi
tar xzf "$ISDIR/o.tgz" -C "$ISDIR"
cd "$ISDIR/openssl-$V"
t1=$(date +%s)
./Configure darwin64-arm64-cc "${SECENEK[@]}" -Os \
    --prefix="$HEDEF" --openssldir="$OPENSSLDIR" --libdir=lib > "$ISDIR/cfg.log" 2>&1 \
    || { tail -20 "$ISDIR/cfg.log" >&2; exit 1; }
make -j"$(sysctl -n hw.ncpu)" build_libs > "$ISDIR/make.log" 2>&1 \
    || { tail -30 "$ISDIR/make.log" >&2; exit 1; }
make install_dev > "$ISDIR/install.log" 2>&1 || { tail -20 "$ISDIR/install.log" >&2; exit 1; }
t2=$(date +%s)
echo "openssl $V (kucuk): indirme+dogrulama $((t1 - t0)) s, Configure+make+install $((t2 - t1)) s"
ls -l "$HEDEF/lib/libssl.a" "$HEDEF/lib/libcrypto.a"
