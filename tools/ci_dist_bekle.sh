#!/usr/bin/env bash
# DIST ARSIVLERI — build-linux'un suites adimindan once web + android arsivlerini
# hazirlar (build.yml).
#
# NIYE (2026-10-30): arsivleri uretmek (emscripten + iki ABI NDK) Linux isinin
# kritik yolunda ~190 s idi (PR #490: emsdk 25 s + arsivler 166 s) ve derleme +
# ornek testlerinden tamamen BAGIMSIZ. Artik ayri `dist-arsivleri` isi onlari
# PARALEL uretiyor; bu betik o isin bitmesini bekler. Kapi gevsemedi: suites
# yine TULPAR_DIST_ZORUNLU=1 ile, gercek arsivlerle kosar; paralel is duserse
# ya da iptal olursa build-linux KIRMIZI olur (zorunlu check adi build-linux;
# dist isinin sonucunu da o tasir).
#
#   tools/ci_dist_bekle.sh   -> GITHUB_OUTPUT: kaynak=paralel | yerinde
#     * main push: paralel is bu olayda kosmuyor (main normalde PR artefaktini
#       yeniden kullanir; buraya ancak yeniden kullanim duserse gelinir) ->
#       arsivler YERINDE uretilir (eski yol).
#     * diger: `dist-arsivleri` isi tamamlanana kadar bekle (en cok
#       CI_DIST_BEKLE saniye, 1200); basariliysa kaynak=paralel (indirme bir
#       sonraki adimda, actions/download-artifact), degilse cikis 1.
set -uo pipefail
cd "$(dirname "$0")/.."
OUT="${GITHUB_OUTPUT:-/dev/null}"
BEKLE="${CI_DIST_BEKLE:-1200}"
ARALIK="${CI_DIST_ARALIK:-10}"

if [ "${GITHUB_EVENT_NAME:-}" = "push" ] && [ "${GITHUB_REF:-}" = "refs/heads/main" ]; then
  echo "main push: dist arsivleri yerinde uretiliyor (paralel is bu olayda kosmuyor)"
  export TULPAR_ANDROID_NDK="${ANDROID_NDK_LATEST_HOME:-${ANDROID_NDK_HOME:-}}"
  test -x "$TULPAR_ANDROID_NDK/toolchains/llvm/prebuilt/linux-x86_64/bin/llvm-ar" || {
    echo "::error::NDK yok ($TULPAR_ANDROID_NDK) — android dist kapisi olcemez"; exit 1; }
  ./wasm/build_tame_web.sh && ./android/build_tame_android.sh || exit 1
  echo "kaynak=yerinde" >> "$OUT"; exit 0
fi

t0=$(date +%s)
while :; do
  d=$(gh api "repos/${GITHUB_REPOSITORY}/actions/runs/${GITHUB_RUN_ID}/jobs?filter=latest&per_page=100" \
        --jq '.jobs[] | select(.name=="dist-arsivleri") | .status + " " + (.conclusion // "")' 2>/dev/null | head -1)
  case "$d" in
    "completed success")
      echo "dist-arsivleri isi basarili ($(( $(date +%s) - t0 )) s beklendi)"
      echo "kaynak=paralel" >> "$OUT"; exit 0 ;;
    completed*)
      echo "::error::dist-arsivleri isi '${d#completed }' ile bitti — web/android kapilari olcemez; build-linux bu sonucu tasir"
      exit 1 ;;
  esac
  if [ $(( $(date +%s) - t0 )) -ge "$BEKLE" ]; then
    echo "::error::dist-arsivleri isi ${BEKLE} s'de bitmedi (durum '${d:-bulunamadi}')"; exit 1
  fi
  sleep "$ARALIK"
done
