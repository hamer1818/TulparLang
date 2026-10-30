#!/usr/bin/env bash
# CI YENIDEN KULLANIM — derleme isinin basinda: PR'in test edilmis
# artefaktlari bu koşumda yayinlanabilir mi? (build.yml, uc platform)
#
#   tools/ci_yeniden_kullan.sh <linux-x64|macos-arm64|windows-x64>
#   -> GITHUB_OUTPUT'a reused=true|false; true ise dosyalar dist/ altinda,
#      upload adimlarinin bekledigi adlarla.
#
# Iki yol:
#   * main push (squash birlesmesi): PR ucunun yesil koşumundan indir — eskiden
#     Linux/macOS'ta satir ici olan desen; Windows'ta hic yoktu, her birlesmede
#     6-9 dk sifirdan derleniyordu (2026-10-30).
#   * `v*` etiketi: KATI yol. Karar tools/yeniden_kullanim_karar.sh'te (agac,
#     surum, PR durumu, koşum sonucu); "yeniden" ise indirilen ikili AYRICA
#     calistirilip tools/surum_denetle.sh ile etiketle olculur. Herhangi bir
#     adim tutmazsa reused=false ve is sifirdan derler (eski yol).
# Hicbir durumda sessiz degil: karar ve sebep is ozetine yazilir. Indirme /
# kopyalama hatasi isi kirmizi yapmaz, sifirdan derlemeye dusurur.
set -uo pipefail
cd "$(dirname "$0")/.."
P="${1:?platform}"
OUT="${GITHUB_OUTPUT:-/dev/null}"
OZET="${GITHUB_STEP_SUMMARY:-/dev/null}"
case "$P" in
  linux-x64|macos-arm64)
    ADLAR="tulpar-$P libtulpar_runtime-$P TameEngine-$P"
    DOSYALAR="tulpar-$P libtulpar_runtime-$P.a TameEngine-$P.tar.gz"
    IKILI="dist/tulpar-$P" ;;
  windows-x64)
    ADLAR="tulpar-windows-x64 tulpar-windows-x64-dosyalar tulpar-windows-installer libtulpar_runtime-windows-x64"
    DOSYALAR="tulpar-windows-x64.zip tulpar-windows-x64.exe libwinpthread-1.dll zlib1.dll libzstd.dll libssl-3-x64.dll libcrypto-3-x64.dll tulpar-setup-windows-x64.exe libtulpar_runtime-windows-x64.a"
    IKILI="dist/tulpar-windows-x64.exe" ;;
  *) echo "bilinmeyen platform: $P" >&2; exit 2 ;;
esac

hayir() {
  echo "Sifirdan derleniyor: $1"
  echo "- **$P: sifirdan derlendi** — $1" >> "$OZET"
  rm -rf dist _pr-artifacts
  echo "reused=false" >> "$OUT"; exit 0
}

pr_no=$(git log -1 --format='%s' | grep -oE '\(#[0-9]+\)' | tail -1 | tr -d '(#)' || true)
etiket=""
case "${GITHUB_REF:-}" in refs/tags/v*) etiket="${GITHUB_REF_NAME}" ;; esac

[ -n "$pr_no" ] || hayir "commit basliginda (#N) yok"
# (jq yerine gh --jq: Windows Git Bash'te ayri arac gerekmesin.)
pr_bilgi=$(gh pr view "$pr_no" --json headRefOid,state,mergeCommit \
             --jq '[.headRefOid // "", .state // "", (.mergeCommit.oid // "")] | join(" ")' 2>/dev/null) \
  || hayir "PR #$pr_no okunamadi"
read -r head_sha pr_durum pr_birlesme <<< "$pr_bilgi"
[ -n "$head_sha" ] || hayir "PR #$pr_no ucu okunamadi"
# Yesil koşum: PR ucunda, pull_request olayiyla. (Etiket yolunda olay sarti
# ozellikle onemli: tahmini surumu yalniz PR koşumu gomer.)
kosum=$(gh run list --workflow='Build (LLVM)' --status=success --limit=40 \
          --json databaseId,headSha,event \
          --jq ".[] | select(.headSha==\"$head_sha\" and .event==\"pull_request\") | .databaseId" | head -1)

if [ -n "$etiket" ]; then
  bilgi_dir=$(mktemp -d)
  pr_agac=""; pr_surum=""
  if [ -n "$kosum" ] && gh run download "$kosum" -n "derleme-bilgisi-$P" -D "$bilgi_dir" >/dev/null 2>&1; then
    f=$(find "$bilgi_dir" -name 'derleme-bilgisi.txt' | head -1)
    if [ -n "$f" ]; then
      pr_agac=$(sed -n 's/^agac=//p' "$f"); pr_surum=$(sed -n 's/^surum=//p' "$f")
    fi
  fi
  kosum_sonuc=""
  [ -n "$kosum" ] && kosum_sonuc=$(gh run view "$kosum" --json conclusion --jq .conclusion 2>/dev/null || true)
  k=$(bash tools/yeniden_kullanim_karar.sh "$etiket" "$(git rev-parse HEAD)" "$(git rev-parse 'HEAD^{tree}')" \
        "$pr_no" "${pr_durum:-}" "${pr_birlesme:-}" \
        "$kosum" "$kosum_sonuc" "$pr_agac" "$pr_surum")
  echo "$k"
  sebep=$(sed -n 's/^sebep=//p' <<< "$k")
  grep -q '^karar=yeniden$' <<< "$k" || hayir "$sebep"
else
  [ -n "$kosum" ] || hayir "PR #$pr_no ucu ($head_sha) icin yesil Build (LLVM) koşumu yok"
  sebep="PR #$pr_no koşumu $kosum (main push)"
fi

echo "Yeniden kullaniliyor: $sebep"
rm -rf dist _pr-artifacts; mkdir -p dist _pr-artifacts
for a in $ADLAR; do
  gh run download "$kosum" -n "$a" -D "_pr-artifacts/$a" || hayir "artefakt $a indirilemedi (koşum $kosum)"
done
# Artefakt duzeni: dosyalar dogrudan ya da bir alt dizinde — ikisi de duzlestirilir.
find _pr-artifacts -type f -exec cp {} dist/ \;
for f in $DOSYALAR; do
  [ -s "dist/$f" ] || hayir "indirilen artefaktta $f yok ya da bos (koşum $kosum)"
done
chmod +x "$IKILI" 2>/dev/null || true

if [ -n "$etiket" ]; then
  # Kaydin otesinde: ikiliyi CALISTIR ve surumunu etiketle olc.
  if ! TULPAR_VERSION="$etiket" bash tools/surum_denetle.sh "$IKILI"; then
    hayir "indirilen ikilinin surumu etiketle tutmadi (tools/surum_denetle.sh)"
  fi
fi
ls -la dist/
echo "- **$P: yeniden kullanildi** — $sebep" >> "$OZET"
echo "reused=true" >> "$OUT"
