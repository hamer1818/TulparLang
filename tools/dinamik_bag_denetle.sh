#!/usr/bin/env bash
# DINAMIK BAG KAPISI — yayinlanan `tulpar` ikilisi yalniz hedef sistemin
# KENDI kitapliklarina mi bagli?
#
# NIYE VAR (olculdu 2026-10-02): yayinlanmis v3.38.0 `tulpar-macos-universal`
# dort Homebrew dylib'ine bagliydi (llvm@18 libunwind, zstd, openssl@3 ssl +
# crypto). Hangisi eksikse dyld acilista abort ediyordu; motor CI'i bunu
# `brew install llvm@18` ile gecici olarak orttu. Derleme isinde HICBIR SEY
# bunu gormedi, cunku koscuda dordu de kuruluydu — "ikili kendi makinesinde
# aciliyor" ile "kullanicinin makinesinde aciliyor" ayri iddialar.
#
#   Mach-O (macOS): her LC_LOAD_DYLIB /usr/lib/ ya da /System/ altinda olmali.
#                   /opt/homebrew, /usr/local, @rpath, ... -> KIRMIZI.
#   ELF (Linux):    NEEDED listesi asagidaki izinli kumede olmali (glibc,
#                   libstdc++/libgcc_s ve her dagitimin temel paketlerindeki
#                   zlib/zstd/tinfo/openssl3). RUNPATH/RPATH girdisi /usr ya da
#                   /lib disinda ise KIRMIZI. v3.38.0 olcumu (llvm-readelf -d):
#                   NEEDED = libssl.so.3 libcrypto.so.3 libm.so.6 libz.so.1
#                   libzstd.so.1 libtinfo.so.6 libstdc++.so.6 libgcc_s.so.1
#                   libc.so.6 ld-linux-x86-64.so.2; RUNPATH /usr/lib/llvm-17/lib
#                   (CMake'in derleme rpath'i; zararsiz, hicbir sey oradan yuklenmiyor).
#                   2026-10-05'ten beri CI LLVM_DIR'i llvm-18'e kilitliyor (o
#                   RUNPATH, apt 18 kurarken find_package'in koscudaki 17'yi
#                   sectigini gosteriyordu) — RUNPATH artik /usr/lib/llvm-18/lib.
#                   LLVM statik bagli (68 MB) — libLLVM*.so gelirse KIRMIZI.
#
#   KULLANICI IKILISI: bu betik verilen HER ikiliye bakar; `tulpar build`
#                   ciktisi icin tests/kullanici_ikili_bag.sh onu TLS'ye dokunan
#                   bir programla cagirir (Linux + macOS CI).
#   PE (Windows):   bu betik bakmaz; build-windows isinin kendi DLL kapisi var.
#
# POZITIF KONTROL (--oz-sinama): gecici bir dizine bir paylasimli kitaplik
# derleyip ona bagli bir program uretir — kapi onu KIRMIZI gormeli; ayni
# derleyiciyle uretilmis sade bir program YESIL gormeli (kapi her seyi
# kirmiziya cevirmiyor).
#
#   tools/dinamik_bag_denetle.sh <ikili>
#   tools/dinamik_bag_denetle.sh --oz-sinama
set -uo pipefail

ELF_IZINLI="libc.so.6 libm.so.6 libpthread.so.0 libdl.so.2 librt.so.1
ld-linux-x86-64.so.2 ld-linux-aarch64.so.1 libstdc++.so.6 libgcc_s.so.1
libz.so.1 libzstd.so.1 libtinfo.so.6 libssl.so.3 libcrypto.so.3"

bicim() {  # bicim <dosya> -> macho | elf | pe | ?
  local m
  m=$(od -An -tx1 -N4 "$1" 2>/dev/null | tr -d ' \n')
  case "$m" in
    cffaedfe|cefaedfe|feedfacf|feedface|cafebabe|bebafeca) echo macho ;;
    7f454c46) echo elf ;;
    4d5a*) echo pe ;;
    *) echo "?" ;;
  esac
}

macho_bagimliliklar() {  # LC_LOAD_DYLIB yollari, satir basina bir
  local cikti
  if command -v otool >/dev/null 2>&1; then cikti=$(otool -L "$1")
  elif command -v llvm-otool >/dev/null 2>&1; then cikti=$(llvm-otool -L "$1")
  elif command -v llvm-objdump >/dev/null 2>&1; then cikti=$(llvm-objdump --macho --dylibs-used "$1")
  else echo "HATA: otool / llvm-otool / llvm-objdump yok" >&2; return 2; fi
  # Ilk satir (ve evrensel ikilide mimari basliklari) dosya adidir; bagimliliklar
  # girintili ve "(compatibility version ...)" ile biter.
  printf '%s\n' "$cikti" | grep -E '^[[:space:]]+[^[:space:]].*\(compatibility version' \
    | sed -E 's/^[[:space:]]+//; s/ \(compatibility version.*$//' | sort -u
}

elf_dinamik() {  # readelf -d ciktisi
  if command -v readelf >/dev/null 2>&1; then readelf -d "$1"
  elif command -v llvm-readelf >/dev/null 2>&1; then llvm-readelf -d "$1"
  else echo "HATA: readelf / llvm-readelf yok" >&2; return 2; fi
}

denetle() {  # denetle <ikili> -> 0 temiz, 1 kirmizi, 2 arac/bicim hatasi
  local ikili="$1" kotu=0 b
  [ -f "$ikili" ] || { echo "HATA: '$ikili' yok" >&2; return 2; }
  b=$(bicim "$ikili")
  case "$b" in
    macho)
      local deps
      deps=$(macho_bagimliliklar "$ikili") || return 2
      [ -n "$deps" ] || { echo "HATA: '$ikili' icin bagimlilik listesi bos — arac ciktisi okunamadi" >&2; return 2; }
      echo "dinamik bag ($ikili, Mach-O):"
      while IFS= read -r d; do
        case "$d" in
          /usr/lib/*|/System/*) echo "  sistem   $d" ;;
          *) echo "  SISTEM DISI  $d"; kotu=1 ;;
        esac
      done <<< "$deps"
      ;;
    elf)
      local dyn needed rp
      dyn=$(elf_dinamik "$ikili") || return 2
      needed=$(printf '%s\n' "$dyn" | sed -nE 's/.*\(NEEDED\).*\[(.*)\].*/\1/p')
      rp=$(printf '%s\n' "$dyn" | sed -nE 's/.*\((RUNPATH|RPATH)\).*\[(.*)\].*/\2/p')
      [ -n "$needed" ] || { echo "HATA: '$ikili' icin NEEDED listesi bos (statik mi, arac mi?)" >&2; return 2; }
      echo "dinamik bag ($ikili, ELF):"
      while IFS= read -r d; do
        if printf '%s\n' $ELF_IZINLI | grep -qxF -- "$d"; then echo "  izinli   $d"
        else echo "  IZINSIZ  $d"; kotu=1; fi
      done <<< "$needed"
      if [ -n "$rp" ]; then
        local IFS_ESKI="$IFS" g
        IFS=':'
        for g in $rp; do
          [ -z "$g" ] && continue
          case "$g" in
            /usr/*|/lib/*|/lib64/*) echo "  runpath  $g (sistem altinda; zararsiz)" ;;
            *) echo "  RUNPATH SISTEM DISI  $g"; kotu=1 ;;
          esac
        done
        IFS="$IFS_ESKI"
      fi
      ;;
    pe) echo "HATA: PE ikili — Windows'un kendi DLL kapisi var (build-windows)" >&2; return 2 ;;
    *) echo "HATA: '$ikili' taninmayan bicim" >&2; return 2 ;;
  esac
  if [ "$kotu" -ne 0 ]; then
    echo "DINAMIK BAG KAPISI KIRMIZI: ikili sistem disi bir kitapliga bagli — kullanici makinesinde acilmayabilir"
    return 1
  fi
  echo "dinamik bag kapisi: temiz"
  return 0
}

oz_sinama() {
  local cc="${CC:-}" rc fail=0  # T global: EXIT tuzagi fonksiyondan sonra kosar
  if [ -z "$cc" ]; then
    for c in cc clang gcc; do command -v "$c" >/dev/null 2>&1 && { cc="$c"; break; }; done
  fi
  [ -n "$cc" ] || { echo "HATA: C derleyicisi yok" >&2; return 2; }
  T=$(mktemp -d)
  trap 'rm -rf "$T"' EXIT
  printf 'int deneme_f(void) { return 7; }\n' > "$T/deneme.c"
  printf 'int deneme_f(void);\nint main(void) { return deneme_f() == 7 ? 0 : 1; }\n' > "$T/kullan.c"
  printf 'int main(void) { return 0; }\n' > "$T/sade.c"
  case "$(uname -s)" in
    Darwin)
      "$cc" -dynamiclib -o "$T/libdeneme.dylib" -install_name "$T/libdeneme.dylib" "$T/deneme.c" || return 2
      "$cc" -o "$T/kullan" "$T/kullan.c" -L"$T" -ldeneme || return 2 ;;
    *)
      "$cc" -shared -fPIC -o "$T/libdeneme.so" "$T/deneme.c" || return 2
      "$cc" -o "$T/kullan" "$T/kullan.c" -L"$T" -ldeneme -Wl,-rpath,"$T" || return 2 ;;
  esac
  "$cc" -o "$T/sade" "$T/sade.c" || return 2
  "$T/kullan" || { echo "HATA: deneme programi kosmadi" >&2; return 2; }

  denetle "$T/kullan" >"$T/k.log" 2>&1; rc=$?
  if [ $rc -eq 1 ] && grep -q "deneme" "$T/k.log"; then
    echo "  gecti  pozitif kontrol: gecici dizindeki kitapliga bagli ikili KIRMIZI"
  else
    echo "  DUSTU  pozitif kontrol: sistem disi bag yakalanmadi (rc=$rc)"; sed 's/^/         /' "$T/k.log"; fail=1
  fi
  denetle "$T/sade" >"$T/s.log" 2>&1; rc=$?
  if [ $rc -eq 0 ]; then
    echo "  gecti  sade program YESIL (kapi her seyi kirmiziya cevirmiyor)"
  else
    echo "  DUSTU  sade program kirmizi (rc=$rc)"; sed 's/^/         /' "$T/s.log"; fail=1
  fi
  return $fail
}

case "${1:-}" in
  --oz-sinama) oz_sinama; exit $? ;;
  ""|-h|--help) sed -n '2,32p' "$0"; exit 2 ;;
  *) denetle "$1"; exit $? ;;
esac
