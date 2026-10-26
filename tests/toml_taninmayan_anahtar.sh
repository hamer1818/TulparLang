#!/usr/bin/env bash
# TANINMAYAN tulpar.toml ANAHTARI DOSYANIN TAMAMINI DUSURMEMELI
# (tulpar-engine oyun geri bildirimi #21, 2026-10-08).
#
# Oyun `[android] cutout = "short_edges"` ekleyince (o anahtari tanimayan
# kurulu v3.40.10'da) manifest parse'i dustu ve surucu DOSYANIN TAMAMINI yok
# saydi: `[ext] paths` gitti, `import "engine"` "su an hicbir eklenti yuklu
# degil" diye yaniltici patladi; Android derlemesinde paket/ad/ikon/assets de
# varsayilana duserdi (oyun varliksiz, baska paket kimligiyle). Asil sebep tek
# satirlik bir "(ignoring)" idi.
#
# Sozlesme: taninmayan BOLUM ya da ANAHTAR -> `tulpar.toml:<satir>: warning:`
# (anahtar adiyla), o satir atlanir, GERISI GECERLI. Bicimi bozuk satir
# (taninan anahtarin yanlis tipli degeri) hala hata — o yazim hatasi.
#
# POZITIF KONTROL: ayni senaryo eski koda (e1bf5a44, #21 oncesi) karsi
# koşturuldugunda 1-3 KIRMIZI (toml yok sayilir: eklenti yuklenmez, [build]
# varsayilanlari uygulanmaz). Ikinci arguman verilirse o ikiliyle de kosulur
# ve KIRMIZI gormesi beklenir:
#   tests/toml_taninmayan_anahtar.sh ./tulpar [eski_tulpar]
set -uo pipefail
cd "$(dirname "$0")/.."
TUL="${1:-./tulpar}"
ESKI="${2:-}"
[ -x "$TUL" ] || { echo "HATA: '$TUL' calistirilabilir degil" >&2; exit 1; }
TUL="$(cd "$(dirname "$TUL")" && pwd)/$(basename "$TUL")"
SRC="$(pwd)/tests/yerel_eklenti"
export LC_ALL=C TULPAR_AOT_NOCACHE=1
unset TULPAR_EXT_PATH

CC_BIN="${CC:-}"
if [ -z "$CC_BIN" ]; then
  for c in cc clang gcc; do command -v "$c" >/dev/null 2>&1 && { CC_BIN="$c"; break; }; done
fi
AR_BIN=""
for a in ar llvm-ar; do command -v "$a" >/dev/null 2>&1 && { AR_BIN="$a"; break; }; done
[ -n "$CC_BIN" ] && [ -n "$AR_BIN" ] || { echo "toml kapisi DUSTU: C derleyicisi/ar yok"; exit 1; }

TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT

# Ornek yerel eklenti (tests/yerel_eklenti.sh ile ayni kurulum).
mkdir -p "$TMP/ext/lib"
cp "$SRC/tulpar-ext.json" "$SRC/ornek.tpr" "$TMP/ext/"
"$CC_BIN" -std=c99 -O2 -fPIC -c "$SRC/ornek_eklenti.c" -o "$TMP/ext/lib/ornek_eklenti.o" 2>"$TMP/cc.err" \
  && "$AR_BIN" rcs "$TMP/ext/lib/libornek.a" "$TMP/ext/lib/ornek_eklenti.o" \
  || { echo "toml kapisi DUSTU: ornek eklenti derlenemedi"; sed -n 1,10p "$TMP/cc.err"; exit 1; }

kos() {  # kos <tulpar> -> 0 hepsi gecti, 1 en az biri dustu
  local T="$1" fail=0 P="$TMP/proje" rc out err
  gecti() { printf "  \033[0;32mgecti\033[0m  %s\n" "$1"; }
  dustu() { printf "  \033[0;31mDUSTU\033[0m  %s\n" "$1"; fail=1; }
  rm -rf "$P"; mkdir -p "$P"
  cp "$SRC/kullan.tpr" "$P/"
  printf 'print("giris");\n' > "$P/giris.tpr"
  # Satir numaralari asagida dogrulaniyor — sirayi bozma.
  cat > "$P/tulpar.toml" <<'EOF'
name = "deneme"
gelecek_ust = "x"
sayi_anahtari = 3

[ext]
paths = ["../ext"]
baska = "y"

[build]
entry = "giris.tpr"
output = "giris_ikili"

[android]
package = "dev.deneme.oyun"
gelecek_anahtar = "short_edges"

[gelecek_bolum]
ic = "z"
ic_sayi = 5
EOF
  cp "$P/tulpar.toml" "$TMP/toml.ilk"

  # 1) #21'in birebiri: [ext] paths, taninmayan anahtarlarin yaninda gecerli.
  (cd "$P" && "$T" kullan.tpr) >"$TMP/o1" 2>"$TMP/e1"; rc=$?
  if [ $rc -eq 0 ] && diff -q "$SRC/kullan.beklenen" "$TMP/o1" >/dev/null 2>&1; then
    gecti "[ext] paths taninmayan anahtarlarla birlikte uygulaniyor (import \"ornek\" calisti)"
  else
    dustu "[ext] paths uygulanmadi (rc=$rc)"; sed 's/^/         /' "$TMP/e1" | head -6
  fi

  # 2) Uyari: dosya:satir + anahtar adi; bolum bir kez; atlanan satir bir kez.
  err=$(cat "$TMP/e1")
  local ok=1
  for beklenen in \
      "tulpar.toml:2: warning: top-level key 'gelecek_ust'" \
      "tulpar.toml:3: warning: top-level key 'sayi_anahtari'" \
      "tulpar.toml:7: warning: [ext] key 'baska'" \
      "tulpar.toml:15: warning: [android] key 'gelecek_anahtar'" \
      "tulpar.toml:17: warning: unknown section [gelecek_bolum]"; do
    if [ "$(grep -cF -- "$beklenen" <<<"$err")" != 1 ]; then ok=0; echo "         eksik/tekrar: $beklenen"; fi
  done
  if grep -qE "'ic'|'ic_sayi'|ignoring" <<<"$err"; then ok=0; echo "         bolum ici satir ayrica uyarildi ya da dosya yok sayildi"; fi
  if [ $ok -eq 1 ]; then gecti "uyarilar: 5 satir, her biri bir kez, dosya:satir + anahtar adiyla"
  else dustu "uyari metni beklenen gibi degil"; sed 's/^/         /' <<<"$err" | head -8; fi

  # 3) [build] varsayilanlari da uygulaniyor (arguman verilmeyen `tulpar build`).
  (cd "$P" && "$T" build) >"$TMP/b3" 2>&1; rc=$?
  if [ $rc -eq 0 ] && [ -x "$P/giris_ikili" -o -x "$P/giris_ikili.exe" ] &&
     [ "$(cd "$P" && ./giris_ikili 2>/dev/null)" = "giris" ]; then
    gecti "[build] entry/output taninmayan anahtarlarla birlikte uygulaniyor"
  else
    dustu "[build] varsayilanlari uygulanmadi (rc=$rc)"; tail -4 "$TMP/b3" | sed 's/^/         /'
  fi

  # 4) pkg add: geri yazmak atlanan anahtarlari silerdi -> reddeder, dosya ayni.
  (cd "$P" && "$T" pkg add bir_paket@1.0.0) >"$TMP/o4" 2>&1; rc=$?
  if [ $rc -ne 0 ] && cmp -s "$P/tulpar.toml" "$TMP/toml.ilk" && grep -q "not saving" "$TMP/o4"; then
    gecti "pkg add: taninmayan anahtar varken kaydetmiyor (dosya bayt bayt ayni)"
  else
    dustu "pkg add taninmayan anahtarli dosyayi yeniden yazdi ya da sebep demedi (rc=$rc)"
    sed 's/^/         /' "$TMP/o4" | head -4
  fi

  # 5) KONTROL: taninan anahtarin bozuk degeri HALA hata ve bunu acik soyler.
  printf 'name = 3\n\n[ext]\npaths = ["../ext"]\n' > "$P/tulpar.toml"
  (cd "$P" && "$T" giris.tpr) >"$TMP/o5" 2>"$TMP/e5"
  if grep -q "tulpar.toml: line 1: only string values are supported" "$TMP/e5" &&
     grep -q "ignoring the whole file" "$TMP/e5"; then
    gecti "bozuk deger (taninan anahtar) hala hata, 'dosyanin tamami' diye soyluyor"
  else
    dustu "bozuk deger hatasi beklenen gibi degil"; sed 's/^/         /' "$TMP/e5" | head -4
  fi
  return $fail
}

echo "toml taninmayan anahtar kapisi: $TUL"
kos "$TUL"; fail=$?

if [ -n "$ESKI" ]; then
  ESKI="$(cd "$(dirname "$ESKI")" && pwd)/$(basename "$ESKI")"
  echo "POZITIF KONTROL (eski ikili, KIRMIZI beklenir): $ESKI"
  if kos "$ESKI" >"$TMP/eski.log" 2>&1; then
    sed 's/^/    /' "$TMP/eski.log"
    echo "  POZITIF KONTROL DUSTU: eski ikili de gecti — kapi bir sey olcmuyor"; fail=1
  else
    sed 's/^/    /' "$TMP/eski.log"
    echo "  pozitif kontrol: eski ikili kirmizi ($(grep -c DUSTU "$TMP/eski.log") dusus)"
  fi
fi

[ $fail -eq 0 ] && echo "toml taninmayan anahtar kapisi: TAMAM" || echo "toml taninmayan anahtar kapisi: DUSTU"
exit $fail
