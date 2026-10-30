#!/usr/bin/env bash
# DIST BEKLEME KAPISI — tools/ci_dist_bekle.sh paralel isin sonucunu gercekten
# tasiyor mu? Sahte `gh` is durumunu sirayla verir.
#   1. in_progress -> in_progress -> completed success : kaynak=paralel, cikis 0
#   2. KONTROL: completed failure   : cikis 1 (build-linux kirmizi)
#   3. KONTROL: completed skipped   : cikis 1 (is kosmadiysa arsiv yok)
#   4. KONTROL: is hic gorunmuyor   : zaman asimi -> cikis 1
set -uo pipefail
KOK="$(cd "$(dirname "$0")/.." && pwd)"
T=$(mktemp -d); trap 'rm -rf "$T"' EXIT
h=0; ok=0
mkdir -p "$T/bin" "$T/r/tools"; cp "$KOK/tools/ci_dist_bekle.sh" "$T/r/tools/"
cat > "$T/bin/gh" <<'GH'
#!/usr/bin/env bash
# Her cagrida durum dosyasinin bir sonraki satirini ver (son satir tekrar eder).
n=$(cat "$SAHTE/n" 2>/dev/null || echo 0); n=$((n+1)); echo $n > "$SAHTE/n"
sed -n "${n}p" "$SAHTE/durumlar" | grep . || tail -1 "$SAHTE/durumlar"
GH
chmod +x "$T/bin/gh"; export SAHTE="$T"
kos() {   # $@ durum satirlari -> "rc kaynak"
  printf '%s\n' "$@" > "$T/durumlar"; rm -f "$T/n" "$T/out"
  ( cd "$T/r" && PATH="$T/bin:$PATH" GITHUB_EVENT_NAME=pull_request GITHUB_REF=refs/pull/1/merge \
      GITHUB_REPOSITORY=o/r GITHUB_RUN_ID=1 GITHUB_OUTPUT="$T/out" CI_DIST_BEKLE=3 CI_DIST_ARALIK=0 \
      bash tools/ci_dist_bekle.sh > "$T/log" 2>&1 ); local rc=$?
  echo "$rc $(sed -n 's/^kaynak=//p' "$T/out" 2>/dev/null)"
}
esit() { if [ "$2" = "$3" ]; then echo "  gecti  $1"; ok=$((ok+1)); else echo "  DUSTU  $1: '$2' (beklenen '$3')"; tail -3 "$T/log"; h=1; fi; }
esit "bekler, basarida kaynak=paralel" "$(kos 'in_progress ' 'in_progress ' 'completed success')" "0 paralel"
esit "KONTROL: paralel is dustu -> kirmizi" "$(kos 'in_progress ' 'completed failure')" "1 "
esit "KONTROL: paralel is atlandi -> kirmizi" "$(kos 'completed skipped')" "1 "
esit "KONTROL: is gorunmuyor -> zaman asimi, kirmizi" "$(kos '')" "1 "
if [ $h -ne 0 ]; then echo "ci dist bekleme kapisi DUSTU"; exit 1; fi
echo "ci dist bekleme kapisi: $ok gecti"
