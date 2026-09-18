#!/usr/bin/env bash
# tulpar.exe'nin ICE AKTARMA TABLOSUNU denetler: her DLL ya stok Windows'ta
# bulunur ya da bizim paketledigimiz listededir.
#
# Neden: uretilen ikili, derleyen makinede calisir ama TEMIZ bir Windows'ta
# STATUS_DLL_NOT_FOUND ile acilmayabilir — eksik DLL'i kullanici gorur, biz
# gormeyiz. Bu denetim 3.13.0 oncesi CI'da vardi (objdump + beyaz liste);
# burasi onun yerel ikizi. Yeni bir gecisli bagimlilik geldiginde YUKSEK SESLE
# duser, sessizce bozuk paket uretmez.
#
# Kullanim: windows/check_imports.sh [exe]   (varsayilan build-windows/tulpar.exe)
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
EXE="${1:-$ROOT/build-windows/tulpar.exe}"
RED='\033[0;31m'; GREEN='\033[0;32m'; NC='\033[0m'

[ -f "$EXE" ] || { echo -e "${RED}HATA:${NC} yok: $EXE"; exit 1; }

# Stok Windows 10+ ile gelen DLL'ler (muhafazakar liste).
SYSTEM_DLLS="kernel32.dll kernelbase.dll ntdll.dll msvcrt.dll advapi32.dll
user32.dll shell32.dll ole32.dll oleaut32.dll ws2_32.dll wsock32.dll
crypt32.dll secur32.dll gdi32.dll rpcrt4.dll shlwapi.dll bcrypt.dll
iphlpapi.dll userenv.dll version.dll winmm.dll powrprof.dll dbghelp.dll
imm32.dll opengl32.dll setupapi.dll cfgmgr32.dll dwmapi.dll"

# Kurulumla birlikte gonderilen MinGW/OpenSSL DLL'leri.
BUNDLED_DLLS="libwinpthread-1.dll zlib1.dll libzstd.dll libssl-3-x64.dll
libcrypto-3-x64.dll libgcc_s_seh-1.dll libstdc++-6.dll"

# objdump: once capraz binutils, yoksa llvm-objdump (ikisi de ayni tabloyu okur).
if command -v x86_64-w64-mingw32-objdump >/dev/null 2>&1; then
    DUMP="x86_64-w64-mingw32-objdump -p"
elif command -v llvm-objdump >/dev/null 2>&1; then
    DUMP="llvm-objdump -p"
else
    echo -e "${RED}HATA:${NC} objdump yok (mingw-w64-binutils ya da llvm)"; exit 1
fi

# "DLL Name:" BUYUK N ile ICE aktarma tablosudur; llvm-objdump ayrica
# " DLL name: <kendi adi>" (kucuk n) satiriyla DISA aktarma dizinini yazar —
# onu iceri aktarma sanmak ikiliyi kendi bagimliligi gibi gosterirdi.
IMPORTS=$($DUMP "$EXE" | grep -E "^[[:space:]]*DLL Name:" \
          | sed -E 's/^[[:space:]]*DLL Name:[[:space:]]*//' \
          | tr 'A-Z' 'a-z' | sort -u)
[ -n "$IMPORTS" ] || { echo -e "${RED}HATA:${NC} import tablosu okunamadi"; exit 1; }

ALLOWED=$(printf '%s\n%s\n' "$SYSTEM_DLLS" "$BUNDLED_DLLS" | tr ' ' '\n' | tr 'A-Z' 'a-z' | grep -v '^$' | sort -u)
MISSING=""
for dll in $IMPORTS; do
    grep -qx "$dll" <<< "$ALLOWED" || MISSING="$MISSING $dll"
done

echo "$(basename "$EXE"): $(wc -w <<< "$IMPORTS") DLL iceri aktariyor"
for dll in $IMPORTS; do echo "  $dll"; done

if [ -n "$MISSING" ]; then
    echo -e "${RED}DUSTU:${NC} ne stok Windows ne paketlenen listede:$MISSING"
    echo "  Ya paketleme listesine ekle ya da bagimliligi kaldir."
    exit 1
fi
echo -e "${GREEN}TAMAM:${NC} butun import'lar stok Windows ya da paketlenen DLL."
