#!/usr/bin/env bash
# CI SURUM KAPISI — tools/ci_surum.sh PR'da dogru surumu tahmin ediyor ve
# yarista TAHMIN ETMIYOR mu? Gecici depoda gercek bir "refs/pull/N/merge"
# benzeri birlesme commit'i kurulur (HEAD^1 = main, HEAD^2 = PR ucu).
#   1. taban etiketli, PR govdesinde `Surum: minor` -> son etiketin minor artisi
#   2. commit mesajinda `Surum: major`              -> major (govde patch olsa da)
#   3. KONTROL: taban etiketsiz kod degisikligi (otomatik-surum henuz kosmadi),
#      bekleme 0 s -> tahmin YOK (bos; etikette sifirdan)
#   4. KONTROL: taban yalniz README degistirmis (etiket gelmeyecek) -> beklemeden tahmin
#   5. etiket push'u -> etiketin kendisi; main push -> bos
set -uo pipefail
KOK="$(cd "$(dirname "$0")/.." && pwd)"
T=$(mktemp -d); trap 'rm -rf "$T"' EXIT
h=0; ok=0
esit() { if [ "$2" = "$3" ]; then echo "  gecti  $1: '${2}'"; ok=$((ok+1)); else echo "  DUSTU  $1: '$2' (beklenen '$3')"; h=1; fi; }

R="$T/depo"; mkdir -p "$R/tools"
cp "$KOK/tools/ci_surum.sh" "$KOK/tools/sonraki_surum.sh" "$R/tools/"
cd "$R" && git init -q -b main && git config user.email t@t && git config user.name t
echo a > kod.c && git add . && git commit -qm "taban" && git tag v2.3.4
git checkout -qb pr && echo b > yeni.c && git add . && git commit -qm "ozellik" && git checkout -q main

birlestir() {   # $1 commit mesaji eki -> HEAD = birlesme commit'i (ayrik)
  git checkout -q --detach main
  git merge -q --no-ff --no-edit pr -m "Merge pr into main" >/dev/null
}
kos() {   # env... -> TULPAR_VERSION degeri
  env GITHUB_ENV="$T/env" GITHUB_STEP_SUMMARY=/dev/null CI_SURUM_BEKLE=0 "$@" bash tools/ci_surum.sh >/dev/null 2>&1
  sed -n 's/^TULPAR_VERSION=//p' "$T/env" | tail -1; rm -f "$T/env"
}
PR="GITHUB_EVENT_NAME=pull_request GITHUB_BASE_REF=main GITHUB_REF=refs/pull/9/merge"

birlestir
esit "PR govdesi Surum: minor" "$(kos $PR PR_GOVDE=$'aciklama\n\nSurum: minor')" v2.4.0
git checkout -q pr && git commit -q --allow-empty -m $'ek\n\nSurum: major' && git checkout -q main
birlestir
esit "commit mesajinda Surum: major" "$(kos $PR PR_GOVDE='govde')" v3.0.0
# Taban etiketsiz kod degisikligi: otomatik-surum henuz etiket atmadi.
git branch -f pr pr~1   # major commit'ini geri al: artis yine patch
git checkout -q main && echo c > kod.c && git commit -qam "baska PR birlesti"
birlestir
esit "KONTROL: taban etiket bekliyor -> tahmin yok" "$(kos $PR PR_GOVDE='')" ""
git tag v2.3.5 main
birlestir
esit "taban etiketi gelince tahmin (v2.3.5 + patch)" "$(kos $PR PR_GOVDE='')" v2.3.6
git checkout -q main && echo d > README.md && git add README.md && git commit -qm "belge"
birlestir
esit "KONTROL: taban yalniz README -> beklemeden tahmin" "$(kos $PR PR_GOVDE='')" v2.3.6
esit "etiket push'u -> etiketin kendisi" "$(kos GITHUB_REF=refs/tags/v9.0.0 GITHUB_REF_NAME=v9.0.0)" v9.0.0
esit "main push -> bos (git describe)" "$(kos GITHUB_EVENT_NAME=push GITHUB_REF=refs/heads/main)" ""

if [ $h -ne 0 ]; then echo "ci surum kapisi DUSTU"; exit 1; fi
echo "ci surum kapisi: $ok gecti"
