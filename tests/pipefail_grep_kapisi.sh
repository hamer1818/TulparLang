#!/usr/bin/env bash
# PIPEFAIL + ERKEN CIKAN GREP KAPISI (Tuzaklar 2, "altinci yalan bicimi").
#
# `set -o pipefail` altinda `echo "$out" | grep -q desen` BELIRLENIMSIZDIR:
# grep eslesmeyi bulunca okumayi birakip cikar; echo hala yaziyorsa SIGPIPE
# alir (141) ve pipefail borunun durumunu 141 yapar. Yani eslesme VARKEN
# kosul yanlis doner -> sahte kirmizi; `! echo | grep -q` biciminde ise
# sahte YESIL. Olculdu (2026-10-05, Linux x86_64, bash 5.3, 200 tekrar):
# 39 KB cikti 1/200, 49 KB 12/200, 64 KB 190/200, 73 KB+ 200/200 sahte
# kirmizi; <<< ve [[ ]] bicimleri her boyutta 0/200. Pipe tamponu macOS'ta
# 16 KB'tan basliyor, yani orada daha kucuk ciktilar da tetikliyor.
#
# Kural: pipefail'li bir betikte `| grep -q` (ve ayni mekanizmayla erken
# cikan `-m N`/`--max-count`) YOK. Yerine `grep -q desen <<<"$out"` ya da
# `[[ $out == *metin* ]]`. Bu kapi o kalibi arar; yorum satirlari sayilmaz.
#
# Tarananlar: tests/*.sh tests/*/*.sh tools/*.sh android/*.sh benchmarks/*.sh
# wasm/*.sh build.sh — yalniz `set -...o pipefail` iceren dosyalar — ve
# .github/workflows/*.yml (Actions `shell: bash` adimlari `bash -eo pipefail`
# ile kosar; YAML'de hangi adimin hangi kabukta oldugunu ayirmak yerine hepsi
# taranir).
#
# Pozitif kontrol HER KOSUMDA: kasitli kalipli ornek dosya KIRMIZI, duzeltilmis
# hali / yorum satiri / pipefail'siz dosya YESIL cikmazsa kapi kendisi kirmizi.
#
# Kullanim: tests/pipefail_grep_kapisi.sh [dosya...]  (dosya yoksa depo)
set -u

KOK=$(cd "$(dirname "$0")/.." && pwd)

# `| grep ... -q|-m|--quiet|--silent|--max-count`: borudan beslenen ve erken
# cikabilen grep. Arguman araligi `|;&` ile biter; tirnak icindeki `|` bir
# desenin parcasi olabilir ama o durumda bayrak genelde desenden ONCE gelir.
DESEN='\|[[:space:]]*grep([[:space:]]+[^|;&]*)?[[:space:]](-[a-zA-Z]*[qm][a-zA-Z0-9]*|--quiet|--silent|--max-count)([[:space:]=]|$)'
PIPEFAIL_DESEN='^[[:space:]]*set[[:space:]]+(-[a-zA-Z]*o[[:space:]]+pipefail|.*-o[[:space:]]+pipefail)|^#!.*pipefail'

# denetle <dosya> <zorunlu:0|1> -> ihlal satirlarini basar, ihlal varsa 1
denetle() {
    local f=$1 hep=$2 bulgu
    [ -f "$f" ] || return 0
    if [ "$hep" -ne 1 ] && ! grep -qE "$PIPEFAIL_DESEN" "$f"; then
        return 0
    fi
    bulgu=$(grep -nE "$DESEN" "$f" | grep -vE '^[0-9]+:[[:space:]]*#')
    [ -z "$bulgu" ] && return 0
    while IFS= read -r s; do
        printf '  %s:%s\n' "$f" "$s"
    done <<<"$bulgu"
    return 1
}

# --- pozitif kontrol (her kosumda) ---------------------------------------
GEC=$(mktemp -d 2>/dev/null || mktemp -d -t pfgrep)
trap 'rm -rf "$GEC"' EXIT
B='|'   # kalip bu dosyanin kendi metninde gecmesin diye parcali
{
    echo '#!/usr/bin/env bash'; echo 'set -uo pipefail'
    echo "out=\$(seq 1 9)"
    echo "if echo \"\$out\" $B grep -qF 5; then echo var; fi"
} >"$GEC/kotu.sh"
{
    echo '#!/usr/bin/env bash'; echo 'set -o pipefail'
    echo "printf '%s\n' a b $B grep -m1 a"
} >"$GEC/kotu_m.sh"
{
    echo '#!/usr/bin/env bash'; echo 'set -uo pipefail'
    echo "out=\$(seq 1 9)"
    echo "if grep -qF 5 <<<\"\$out\"; then echo var; fi"
    echo "# yorum: echo \"\$out\" $B grep -q 5 burada sayilmaz"
} >"$GEC/iyi.sh"
{
    echo '#!/usr/bin/env bash'
    echo "echo \"\$x\" $B grep -q 5   # pipefail YOK: borunun durumu grep'in"
} >"$GEC/pipefailsiz.sh"
kontrol_hata=0
denetle "$GEC/kotu.sh" 0 >/dev/null && { echo "POZITIF KONTROL DUSTU: kasitli 'echo | grep -q' yakalanmadi"; kontrol_hata=1; }
denetle "$GEC/kotu_m.sh" 0 >/dev/null && { echo "POZITIF KONTROL DUSTU: kasitli '| grep -m1' yakalanmadi"; kontrol_hata=1; }
denetle "$GEC/iyi.sh" 0 >/dev/null || { echo "POZITIF KONTROL DUSTU: duzeltilmis bicim / yorum satiri kirmizi"; kontrol_hata=1; }
denetle "$GEC/pipefailsiz.sh" 0 >/dev/null || { echo "POZITIF KONTROL DUSTU: pipefail'siz dosya kirmizi"; kontrol_hata=1; }
if [ $kontrol_hata -ne 0 ]; then
    echo "pipefail grep kapisi: KAPININ KENDISI OLCMUYOR"
    exit 1
fi

# --- tarama ----------------------------------------------------------------
ihlal=0
n=0
if [ $# -gt 0 ]; then
    for f in "$@"; do n=$((n + 1)); denetle "$f" 0 || ihlal=1; done
else
    cd "$KOK" || exit 1
    for f in tests/*.sh tests/*/*.sh tools/*.sh android/*.sh benchmarks/*.sh wasm/*.sh build.sh; do
        [ -f "$f" ] || continue
        n=$((n + 1)); denetle "$f" 0 || ihlal=1
    done
    for f in .github/workflows/*.yml; do
        [ -f "$f" ] || continue
        n=$((n + 1)); denetle "$f" 1 || ihlal=1
    done
fi
if [ $ihlal -ne 0 ]; then
    echo "pipefail grep kapisi: KIRMIZI — yukaridaki satirlar pipefail altinda"
    echo "  erken cikan grep'e boru ile besleniyor (eslesme varken 141 doner)."
    echo "  Duzeltme: grep -q DESEN <<<\"\$degisken\"  ya da  [[ \$degisken == *metin* ]]"
    exit 1
fi
echo "pipefail grep kapisi: $n dosya temiz (pozitif kontrol: 2 kasitli kalip yakalandi, 2 temiz ornek gecti)"
exit 0
