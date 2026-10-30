#!/usr/bin/env bash
# CI YENIDEN KULLANIM KAPISI — tools/ci_yeniden_kullan.sh'in etiket yolu gercekten
# olcuyor mu? Sahte `gh` (PATH'in onunde) GitHub'in yerine gecer: PR, koşum ve
# artefaktlar gecici dizinden gelir. Gercek git deposu (gecici) ve gercek
# tools/surum_denetle.sh kullanilir; "ikili" `version` alt komutuna cevap veren
# bir kabuk betigi.
#
# Durumlar (her biri ayri koşum):
#   1. hepsi tutuyor                      -> reused=true, dist/ dolu
#   2. KONTROL: agac farkli               -> reused=false
#   3. KONTROL: kaydedilen surum farkli   -> reused=false
#   4. KONTROL: kayit dogru, IKILI yanlis surum -> reused=false (ikili calistirilarak olculdu)
#   5. KONTROL: artefakt eksik            -> reused=false (isi kirmizi yapmaz)
#   6. main push (gevsek yol)             -> reused=true
# Kullanim: tests/ci_yeniden_kullan.sh   (git + cmake gerekir)
set -uo pipefail
KOK="$(cd "$(dirname "$0")/.." && pwd)"
command -v cmake >/dev/null 2>&1 || { echo "ci yeniden kullanim kapisi: ATLANDI: cmake yok"; exit 0; }
T=$(mktemp -d); trap 'rm -rf "$T"' EXIT
h=0; ok=0
gecti() { echo "  gecti  $1"; ok=$((ok+1)); }
dustu() { echo "  DUSTU  $1"; h=1; }

# --- gecici depo: araclar + bir "squash" commit'i + etiketi -------------------
R="$T/depo"; mkdir -p "$R/tools" "$R/cmake"
cp "$KOK/tools/ci_yeniden_kullan.sh" "$KOK/tools/yeniden_kullanim_karar.sh" "$KOK/tools/surum_denetle.sh" "$R/tools/"
cp "$KOK/cmake/TulparVersion.cmake" "$R/cmake/"
(
  cd "$R" && git init -q && git config user.email t@t && git config user.name t
  echo k > kod.c && git add . && git commit -qm "ilk" && git tag v1.0.0
  echo k2 > kod.c && git commit -qam "ozellik: deneme (#7)" && git tag v1.1.0
) >/dev/null
ETIKET_SHA=$(git -C "$R" rev-parse HEAD)
AGAC=$(git -C "$R" rev-parse 'HEAD^{tree}')

# --- sahte gh ------------------------------------------------------------------
mkdir -p "$T/bin" "$T/art"
cat > "$T/bin/gh" <<'GH'
#!/usr/bin/env bash
# Sahte gh: yalniz ci_yeniden_kullan.sh'in kullandigi cagrilar.
case "$1 $2" in
  "pr view")   cat "$SAHTE/pr.txt" ;;
  "run list")  cat "$SAHTE/kosum.txt" ;;
  "run view")  echo success ;;
  "run download")
    ad=""; hedef=""; shift 2; shift   # koşum kimligi
    while [ $# -gt 0 ]; do case "$1" in -n) ad="$2"; shift 2 ;; -D) hedef="$2"; shift 2 ;; *) shift ;; esac; done
    [ -d "$SAHTE/art/$ad" ] || { echo "artefakt yok: $ad" >&2; exit 1; }
    mkdir -p "$hedef" && cp -r "$SAHTE/art/$ad/." "$hedef/" ;;
  *) echo "sahte gh: bilinmeyen $*" >&2; exit 1 ;;
esac
GH
chmod +x "$T/bin/gh"
export SAHTE="$T"

ikili_yaz() {   # $1 surum -> art/tulpar-linux-x64/tulpar-linux-x64
  mkdir -p "$T/art/tulpar-linux-x64"
  printf '#!/bin/sh\n[ "$1" = version ] && echo "TulparLang %s"\n' "$1" > "$T/art/tulpar-linux-x64/tulpar-linux-x64"
  chmod +x "$T/art/tulpar-linux-x64/tulpar-linux-x64"
}
kur() {   # $1 kaydedilen agac, $2 kaydedilen surum, $3 ikili surum
  rm -rf "$T/art"; mkdir -p "$T/art/derleme-bilgisi-linux-x64" "$T/art/libtulpar_runtime-linux-x64" "$T/art/TameEngine-linux-x64"
  printf 'agac=%s\nsurum=%s\n' "$1" "$2" > "$T/art/derleme-bilgisi-linux-x64/derleme-bilgisi.txt"
  echo a > "$T/art/libtulpar_runtime-linux-x64/libtulpar_runtime-linux-x64.a"
  echo t > "$T/art/TameEngine-linux-x64/TameEngine-linux-x64.tar.gz"
  ikili_yaz "$3"
  echo "baspr123 MERGED $ETIKET_SHA" > "$T/pr.txt"
  echo 4242 > "$T/kosum.txt"
}
kos() {   # $1 GITHUB_REF -> GITHUB_OUTPUT icerigi
  rm -f "$T/out"; rm -rf "$R/dist"
  ( cd "$R" && PATH="$T/bin:$PATH" GITHUB_REF="$1" GITHUB_REF_NAME="${1##*/}" GITHUB_OUTPUT="$T/out" \
      GITHUB_STEP_SUMMARY=/dev/null bash tools/ci_yeniden_kullan.sh linux-x64 > "$T/log" 2>&1 )
  cat "$T/out" 2>/dev/null
}

kur "$AGAC" v1.1.0 v1.1.0
[ "$(kos refs/tags/v1.1.0)" = "reused=true" ] && [ -x "$R/dist/tulpar-linux-x64" ] \
  && gecti "hepsi tutuyor -> yeniden kullanildi, dist/ dolu" || { dustu "hepsi tutuyor ama yeniden kullanilmadi"; tail -5 "$T/log"; }
kur "0000000000000000000000000000000000000000" v1.1.0 v1.1.0
[ "$(kos refs/tags/v1.1.0)" = "reused=false" ] && grep -q "agac farkli" "$T/log" \
  && gecti "KONTROL: agac farkli -> sifirdan" || { dustu "agac farkliyken yeniden kullanildi"; tail -5 "$T/log"; }
kur "$AGAC" v1.0.9 v1.1.0
[ "$(kos refs/tags/v1.1.0)" = "reused=false" ] && grep -q "gomulu surum" "$T/log" \
  && gecti "KONTROL: kaydedilen surum farkli -> sifirdan" || { dustu "surum farkliyken yeniden kullanildi"; tail -5 "$T/log"; }
kur "$AGAC" v1.1.0 v1.0.9
[ "$(kos refs/tags/v1.1.0)" = "reused=false" ] && grep -q "surumu etiketle tutmadi" "$T/log" && [ ! -e "$R/dist" ] \
  && gecti "KONTROL: kayit dogru, ikili yanlis surum -> sifirdan (ikili calistirildi), dist/ temizlendi" \
  || { dustu "ikili yanlis surumken yeniden kullanildi"; tail -5 "$T/log"; }
kur "$AGAC" v1.1.0 v1.1.0; rm -rf "$T/art/TameEngine-linux-x64"
[ "$(kos refs/tags/v1.1.0)" = "reused=false" ] && grep -q "TameEngine-linux-x64 indirilemedi" "$T/log" \
  && gecti "KONTROL: artefakt eksik -> sifirdan (kirmizi degil)" || { dustu "artefakt eksikken"; tail -5 "$T/log"; }
kur "" "" v1.0.0-3-gabc
[ "$(kos refs/heads/main)" = "reused=true" ] \
  && gecti "main push: kayitsiz koşum da yeniden kullanilir (eski davranis)" || { dustu "main push"; tail -5 "$T/log"; }

if [ $h -ne 0 ]; then echo "ci yeniden kullanim kapisi DUSTU"; exit 1; fi
echo "ci yeniden kullanim kapisi: $ok gecti"
