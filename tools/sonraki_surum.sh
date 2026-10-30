#!/usr/bin/env bash
# SONRAKI SURUM — otomatik-surum.yml ile PR CI'in ORTAK hesabi.
#
# NIYE (2026-10-30): etiket isi uc platformu sifirdan derleyip testleri yeniden
# kosuyordu (~10-11 dk), cunku surum ikiliye derlemede gomuluyor. Artik PR CI
# birlesince cikacak surumu TAHMIN edip onunla derliyor; etiket isi PR'in
# test edilmis ikilisini yayinlayabiliyor (karar: tools/yeniden_kullanim_karar.sh).
# Tahmin ile etiket AYNI hesaptan gelmeli — iki kopya kayarsa her etikette
# "surum farkli -> sifirdan" denir ve kazanc sessizce kaybolur. Bu yuzden
# hesap burada, iki is akisi da bunu cagiriyor.
#
#   tools/sonraki_surum.sh seviye <dosya>...       -> major|minor|patch
#       (her dosyada `Surum: minor` / `Sürüm: major` / `Release: minor` satiri
#       aranir; en buyugu kazanir, hicbiri yoksa patch)
#   tools/sonraki_surum.sh sonraki <seviye> [son]  -> vX.Y.Z
#       (son verilmezse depodaki en buyuk v* etiketi; hic yoksa v0.0.0)
#   tools/sonraki_surum.sh son                     -> en buyuk v* etiketi (yoksa bos)
#   tools/sonraki_surum.sh belge_yalniz <sha>      -> cikis 0: yalniz belge/olcum
#       ciktisi degismis (otomatik-surum surum CIKARMAZ)
#   tools/sonraki_surum.sh etiket_bekleniyor <sha> -> cikis 0: bu commit'e
#       otomatik-surum etiket ATACAK ama henuz atmadi (kod degisikligi, etiketsiz)
#   tools/sonraki_surum.sh --oz-test               -> kendi pozitif kontrolleri
set -euo pipefail

seviye_metin() {   # stdin: metin -> seviye
  local m; m="$(cat)"
  if grep -Eqi '^[[:space:]]*(surum|sürüm|release)[[:space:]]*:[[:space:]]*major[[:space:]]*$' <<< "$m"; then
    echo major
  elif grep -Eqi '^[[:space:]]*(surum|sürüm|release)[[:space:]]*:[[:space:]]*minor[[:space:]]*$' <<< "$m"; then
    echo minor
  else
    echo patch
  fi
}

buyuk() {   # iki seviyeden buyugu
  case "$1$2" in
    *major*) echo major ;;
    *minor*) echo minor ;;
    *) echo patch ;;
  esac
}

son_etiket() { git tag --list 'v[0-9]*' --sort=-v:refname | head -1; }

sonraki() {   # $1 seviye, $2 son etiket (bos = v0.0.0)
  local son="${2:-v0.0.0}" c X Y Z
  c="${son#v}"; c="${c%%-*}"
  IFS=. read -r X Y Z <<< "$c"
  X="${X:-0}"; Y="${Y:-0}"; Z="${Z:-0}"
  case "$1" in
    major) X=$((X+1)); Y=0; Z=0 ;;
    minor) Y=$((Y+1)); Z=0 ;;
    *)     Z=$((Z+1)) ;;
  esac
  echo "v${X}.${Y}.${Z}"
}

# otomatik-surum.yml'nin "yalniz belge degismis -> surum yok" kurali ile AYNI.
belge_yalniz() {   # $1 sha -> cikis 0: yalniz belge/olcum ciktisi degismis
  local d k
  git rev-parse -q --verify "$1^" >/dev/null || return 1
  d="$(git diff --name-only "$1^..$1")"
  k="$(echo "$d" | grep -vE '^(README\.md|benchmarks/RESULTS\.(md|json))$' || true)"
  [ -n "$d" ] && [ -z "$k" ]
}

etiket_bekleniyor() {   # $1 sha
  [ -z "$(git tag --points-at "$1" | grep -E '^v[0-9]' || true)" ] || return 1
  ! belge_yalniz "$1"
}

oz_test() {
  local h=0 t
  sinan() { if [ "$2" = "$3" ]; then echo "  gecti  $1: $2"; else echo "  DUSTU  $1: '$2' (beklenen '$3')"; h=1; fi; }
  t=$(mktemp -d); trap 'rm -rf "$t"' RETURN
  printf 'baslik\n\nSurum: minor\n' > "$t/a"
  printf 'govde\n  sürüm:  MAJOR  \n' > "$t/b"
  printf 'Surum: minorx\nsurum: patch\n' > "$t/c"
  printf 'Release: minor\n' > "$t/d"
  sinan "Surum: minor satiri" "$(seviye_metin < "$t/a")" minor
  sinan "sürüm: MAJOR (buyuk/kucuk harf, bosluk)" "$(seviye_metin < "$t/b")" major
  sinan "KONTROL: 'minorx' minor sayilmaz" "$(seviye_metin < "$t/c")" patch
  sinan "Release: minor" "$(seviye_metin < "$t/d")" minor
  sinan "satir yok -> patch" "$(echo 'duz metin' | seviye_metin)" patch
  sinan "en buyuk kazanir (minor+major)" "$(buyuk minor major)" major
  sinan "patch+minor" "$(buyuk patch minor)" minor
  sinan "patch artisi" "$(sonraki patch v3.41.3)" v3.41.4
  sinan "minor artisi Z'yi sifirlar" "$(sonraki minor v3.41.3)" v3.42.0
  sinan "major artisi" "$(sonraki major v3.41.3)" v4.0.0
  sinan "etiketsiz depo -> v0.0.1" "$(sonraki patch "")" v0.0.1
  sinan "on-surum eki atilir" "$(sonraki patch v1.2.3-rc1)" v1.2.4
  # Gecici depoda etiket ve belge kurali (gercek git ile).
  (
    cd "$t" && git init -q r && cd r && git config user.email t@t && git config user.name t
    echo a > kod.c && git add . && git commit -qm k1 && git tag v1.0.0
    echo b > kod.c && git commit -qam k2
    echo c > README.md && git add . && git commit -qm belge
  ) >/dev/null
  local r="$t/r"
  sinan "son etiket (gecici depo)" "$(git -C "$r" tag --list 'v[0-9]*' --sort=-v:refname | head -1)" v1.0.0
  ( cd "$r"; etiket_bekleniyor "$(git rev-parse HEAD~1)" ) && x=evet || x=hayir
  sinan "etiketsiz kod commit'i -> etiket bekleniyor" "$x" evet
  ( cd "$r"; etiket_bekleniyor HEAD ) && x=evet || x=hayir
  sinan "KONTROL: yalniz README -> etiket beklenmez" "$x" hayir
  ( cd "$r"; etiket_bekleniyor "$(git rev-parse HEAD~2)" ) && x=evet || x=hayir
  sinan "KONTROL: zaten etiketli -> beklenmez" "$x" hayir
  if [ $h -ne 0 ]; then echo "sonraki_surum oz-test DUSTU"; return 1; fi
  echo "sonraki_surum oz-test temiz"
}

k="${1:-}"; shift || true
case "$k" in
  seviye)
    s=patch
    for f in "$@"; do s="$(buyuk "$s" "$(seviye_metin < "$f")")"; done
    echo "$s" ;;
  sonraki) sonraki "${1:?seviye}" "${2-$(son_etiket)}" ;;
  son) son_etiket ;;
  belge_yalniz) belge_yalniz "${1:?sha}" ;;
  etiket_bekleniyor) etiket_bekleniyor "${1:?sha}" ;;
  --oz-test) oz_test ;;
  *) echo "kullanim: $0 seviye|sonraki|son|etiket_bekleniyor|--oz-test" >&2; exit 2 ;;
esac
