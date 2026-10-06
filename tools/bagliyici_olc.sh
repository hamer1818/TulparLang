#!/usr/bin/env bash
# GECICI OLCUM (birlesmeden once silinir): hello world'un link suresi,
# varsayilan bagliyici vs lld (varsa), en iyi 5 — macOS/Windows karari icin.
T=${1:-./tulpar}
d=$(mktemp -d)
printf 'print("merhaba");\n' > "$d/h.tpr"
olc() {
  local ad=$1; shift
  local best=999999 t
  for i in 1 2 3 4 5; do
    t=$(env TULPAR_AOT_NOCACHE=1 TULPAR_AOT_TIME=1 "$@" "$T" build "$d/h.tpr" "$d/h_$ad" 2>&1 | tr -d '\r' | sed -n 's/.*link *\([0-9]*\)ms.*/\1/p')
    [ -n "$t" ] && [ "$t" -lt "$best" ] && best=$t
  done
  echo "[baglayici olcumu] $ad: link en iyi ${best} ms ($(uname -s))"
}
olc varsayilan
if command -v ld.lld >/dev/null 2>&1 || command -v ld.lld.exe >/dev/null 2>&1; then
  olc lld TULPAR_CC="clang++ -fuse-ld=lld"
else
  echo "[baglayici olcumu] ld.lld yok"
fi
command -v ld64.lld >/dev/null 2>&1 && olc ld64.lld TULPAR_CC="clang++ -fuse-ld=lld"
rm -rf "$d"
exit 0
