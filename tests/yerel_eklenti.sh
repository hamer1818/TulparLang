#!/usr/bin/env bash
# YEREL EKLENTI KAPISI (K303) — kurulu bir `tulpar` bir C kitapligini
# derleyici yeniden derlenmeden kullanabiliyor mu?
#
# tests/yerel_eklenti/ altindaki ornek eklenti (ornek_eklenti.c + tulpar-ext.json
# + ornek.tpr) burada statik arsive derlenir ve uc bulunma yolunun HER BIRIYLE
# (--ext, TULPAR_EXT_PATH, tulpar.toml [ext]) ayni program kosturulur; cikti
# kullan.beklenen ile BAYT BAYT karsilastirilir. Program her tip ailesini
# (i64/i32/f64/f32/bool/str/void, 10 parametre, NULL dizgi, statik tampon
# kopyasi, Tulpar'i geri cagirma) bir kez gecirir.
#
# POZITIF KONTROLLER (kapinin olctugunu gosteren yollar — her biri KIRMIZI
# gormek zorunda; yesil gorurse kapi duser):
#   * eklenti verilmeden `import "ornek"` -> anlamli hata + --ext ipucu
#   * bildirimde bir sembol bozulunca -> link o ADLA duser (run ve build)
#   * bildirimde bir parametre tipi bozulunca -> typecheck yakalar
#   * ornek_eklenti.c'nin ABI kilidi bozulunca -> C derlemesi duser
#   * bozuk JSON / olmayan dizin / yerlesik ad / eksik link bolumu -> adiyla hata
# Ayrica: eklentiyi kullanmayan program ona BAGLANMAZ; ayni adli eklentide
# --ext, TULPAR_EXT_PATH'i ezer; kullanici fonksiyonu eklentiyi golgeler;
# LSP (python3 varsa) hover/tamamlama/imza yardiminda eklentiyi gorur.
#
#   tests/yerel_eklenti.sh [tulpar_yolu]
set -uo pipefail
cd "$(dirname "$0")/.."
TUL="${1:-./tulpar}"
[ -x "$TUL" ] || { echo "yerel eklenti kapisi: $TUL yok — atlandi"; exit 0; }
TUL="$(cd "$(dirname "$TUL")" && pwd)/$(basename "$TUL")"
SRC="$(pwd)/tests/yerel_eklenti"
export LC_ALL=C   # tani metinleri Ingilizce kolundan okunur
unset TULPAR_EXT_PATH

CC_BIN="${CC:-}"
if [ -z "$CC_BIN" ]; then
  for c in cc clang gcc; do command -v "$c" >/dev/null 2>&1 && { CC_BIN="$c"; break; }; done
fi
AR_BIN=""
for a in ar llvm-ar; do command -v "$a" >/dev/null 2>&1 && { AR_BIN="$a"; break; }; done
[ -n "$CC_BIN" ] && [ -n "$AR_BIN" ] || { echo "yerel eklenti kapisi DUSTU: C derleyicisi/ar yok"; exit 1; }

TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
fail=0
gecti() { printf "  \033[0;32mgecti\033[0m  %s\n" "$1"; }
dustu() { printf "  \033[0;31mDUSTU\033[0m  %s\n" "$1"; fail=1; }

# --- Eklentiyi kur ------------------------------------------------------------
kur() {  # kur <hedef_dizin>
  mkdir -p "$1/lib"
  cp "$SRC/tulpar-ext.json" "$SRC/ornek.tpr" "$1/"
  "$CC_BIN" -std=c99 -O2 -fPIC -Werror=incompatible-pointer-types -c "$SRC/ornek_eklenti.c" -o "$1/lib/ornek_eklenti.o" \
    && "$AR_BIN" rcs "$1/lib/libornek.a" "$1/lib/ornek_eklenti.o"
}
if ! kur "$TMP/ext" 2>"$TMP/cc.err"; then
  echo "yerel eklenti kapisi DUSTU: ornek eklenti derlenemedi ($CC_BIN)"; sed -n '1,10p' "$TMP/cc.err"; exit 1
fi
# ABI kilidinin pozitif kontrolu: imza kayinca C derlemesi DUSMELI.
if "$CC_BIN" -std=c99 -O2 -DORNEK_ABI_BOZ -Werror=incompatible-pointer-types -c "$SRC/ornek_eklenti.c" -o "$TMP/boz.o" 2>/dev/null; then
  dustu "ABI kilidi: kasitli imza kaymasi (ORNEK_ABI_BOZ) C derlemesinden GECTI — kilit bir sey olcmuyor"
else
  gecti "ABI kilidi: kasitli imza kaymasi C derlemesinde duser"
fi
cp "$SRC/kullan.tpr" "$TMP/kullan.tpr"

beklenen="$SRC/kullan.beklenen"
karsilastir() {  # karsilastir <ad> <cikti> <rc> <log>
  if [ "$3" -eq 0 ] && diff -u "$beklenen" "$2" >"$TMP/fark" 2>&1; then gecti "$1: cikti bayt bayt beklenen"
  else dustu "$1 (rc=$3)"; head -20 "$TMP/fark" | sed 's/^/         /'; tail -12 "$4" | sed 's/^/         /'; fi
}

# 1) --ext ile calistir (sessiz derleme yolu)
(cd "$TMP" && "$TUL" --ext ext kullan.tpr) >"$TMP/o1" 2>"$TMP/e1"; rc=$?
karsilastir "--ext <dizin> (calistir)" "$TMP/o1" $rc "$TMP/e1"
if grep -q "\[typecheck\]" "$TMP/e1"; then dustu "ornek program [typecheck] uyarisi uretti"; grep "\[typecheck\]" "$TMP/e1" | head -5; fi

# 2) tulpar build --ext=<dizin>, sonra ikili
(cd "$TMP" && "$TUL" build --ext="$TMP/ext" kullan.tpr kullan_ikili) >"$TMP/b2" 2>&1; rc=$?
if [ $rc -eq 0 ] && [ -x "$TMP/kullan_ikili" -o -x "$TMP/kullan_ikili.exe" ]; then
  (cd "$TMP" && ./kullan_ikili) >"$TMP/o2" 2>&1; rc=$?
  karsilastir "build --ext=<yol> -> ikili" "$TMP/o2" $rc "$TMP/b2"
else
  dustu "build --ext basarisiz (rc=$rc)"; tail -12 "$TMP/b2" | sed 's/^/         /'
fi

# 3) TULPAR_EXT_PATH (yol listesi; olmayan bir yol listede DEGIL — ayri sinanir)
(cd "$TMP" && TULPAR_EXT_PATH="$TMP/ext" "$TUL" kullan.tpr) >"$TMP/o3" 2>"$TMP/e3"; rc=$?
karsilastir "TULPAR_EXT_PATH" "$TMP/o3" $rc "$TMP/e3"

# 4) tulpar.toml [ext] paths (goreli: toml'un dizinine gore)
mkdir -p "$TMP/proje"
cp "$SRC/kullan.tpr" "$TMP/proje/"
printf 'name = "deneme"\n\n[ext]\npaths = ["../ext"]  # ornek eklenti\n' > "$TMP/proje/tulpar.toml"
(cd "$TMP/proje" && "$TUL" kullan.tpr) >"$TMP/o4" 2>"$TMP/e4"; rc=$?
karsilastir "tulpar.toml [ext] paths" "$TMP/o4" $rc "$TMP/e4"

# 5) Oncelik: ayni ADLI iki eklenti — --ext (saglam) TULPAR_EXT_PATH'teki
#    bozuk kopyayi ezer (bozuk kopya yuklenseydi link duserdi).
kur "$TMP/eski" 2>/dev/null
sed -i.bak 's/"name": "ornek_topla",/"name": "ornek_topla", "symbol": "ornek_topla_ESKI",/' "$TMP/eski/tulpar-ext.json"
(cd "$TMP" && TULPAR_EXT_PATH="$TMP/eski" "$TUL" --ext ext kullan.tpr) >"$TMP/o5" 2>"$TMP/e5"; rc=$?
karsilastir "oncelik: --ext, TULPAR_EXT_PATH'teki ayni adli eklentiyi ezer" "$TMP/o5" $rc "$TMP/e5"

# 6) Kullanici fonksiyonu eklenti fonksiyonunu golgeler (yerlesik kurali)
cat > "$TMP/golge.tpr" <<'T'
import "ornek";
func ornek_topla(a, b) { return 99; }
print("golge " + toString(ornek_topla(1, 2)));
T
out=$(cd "$TMP" && "$TUL" --ext ext golge.tpr 2>&1 | tail -1)
[ "$out" = "golge 99" ] && gecti "kullanici tanimi eklenti fonksiyonunu golgeler" || dustu "golgeleme: '$out' (beklenen 'golge 99')"

# 7) Eklentiyi KULLANMAYAN program ona baglanmaz: link bolumu bozuk bir
#    eklenti yukluyken bile derlenir.
mkdir -p "$TMP/kullanilmayan"
printf '{"tulpar_ext":1,"name":"bos_eklenti","functions":[{"name":"bos_f","returns":"i64"}],"link":{"linux":{"libs":["yok_boyle_bir_kitaplik"]},"macos":{"libs":["yok_boyle_bir_kitaplik"]},"windows":{"libs":["yok_boyle_bir_kitaplik"]}}}' > "$TMP/kullanilmayan/tulpar-ext.json"
printf 'print("sade " + toString(1 + 1));\n' > "$TMP/sade.tpr"
out=$(cd "$TMP" && "$TUL" --ext kullanilmayan sade.tpr 2>&1 | tail -1)
[ "$out" = "sade 2" ] && gecti "eklentiyi kullanmayan program ona baglanmaz" || dustu "kullanilmayan eklenti linke girdi: '$out'"

# --- POZITIF KONTROLLER ---------------------------------------------------------
# P1) Eklenti verilmeden import -> hata + ipucu, cikis != 0
(cd "$TMP" && "$TUL" kullan.tpr) >"$TMP/p1" 2>&1; rc=$?
if [ $rc -ne 0 ] && grep -q "Could not import file 'ornek'" "$TMP/p1" && grep -q -- "--ext <dir>" "$TMP/p1"; then
  gecti "eklentisiz import \"ornek\": anlamli hata + --ext ipucu (cikis $rc)"
else
  dustu "eklentisiz import: beklenen hata/ipucu yok (rc=$rc)"; head -6 "$TMP/p1" | sed 's/^/         /'
fi

# P2) Bildirimde sembol bozuk -> link o ADLA duser (calistir + build)
kur "$TMP/bozuk_sembol" 2>/dev/null
sed -i.bak 's/"name": "ornek_kare32",/"name": "ornek_kare32", "symbol": "ornek_kare32_YOK",/' "$TMP/bozuk_sembol/tulpar-ext.json"
(cd "$TMP" && "$TUL" --ext bozuk_sembol kullan.tpr) >"$TMP/p2" 2>&1; rc=$?
if [ $rc -ne 0 ] && grep -q "ornek_kare32_YOK" "$TMP/p2" && grep -q "Native extension used: ornek" "$TMP/p2"; then
  gecti "bozuk sembol (calistir): link 'ornek_kare32_YOK' adiyla duser"
else
  dustu "bozuk sembol (calistir): sembol adi gorunmedi (rc=$rc)"; tail -8 "$TMP/p2" | sed 's/^/         /'
fi
(cd "$TMP" && "$TUL" build --ext bozuk_sembol kullan.tpr bozuk_ikili) >"$TMP/p2b" 2>&1; rc=$?
if [ $rc -ne 0 ] && grep -q "ornek_kare32_YOK" "$TMP/p2b"; then
  gecti "bozuk sembol (build): link 'ornek_kare32_YOK' adiyla duser"
else
  dustu "bozuk sembol (build): sembol adi gorunmedi (rc=$rc)"; tail -8 "$TMP/p2b" | sed 's/^/         /'
fi

# P3) Bildirimde parametre tipi bozuk -> typecheck yakalar
kur "$TMP/bozuk_tip" 2>/dev/null
sed -i.bak 's/"params": \["x: i32"\], "returns": "i32"/"params": ["x: str"], "returns": "i32"/' "$TMP/bozuk_tip/tulpar-ext.json"
(cd "$TMP" && "$TUL" typecheck --ext bozuk_tip kullan.tpr) >"$TMP/p3" 2>&1; rc=$?
if [ $rc -ne 0 ] && grep -q "Argument 1 of 'ornek_kare32': expected str, got int" "$TMP/p3"; then
  gecti "bozuk imza tipi: typecheck 'ornek_kare32' argumanini yakalar"
else
  dustu "bozuk imza tipi yakalanmadi (rc=$rc)"; tail -6 "$TMP/p3" | sed 's/^/         /'
fi
# Ayni program saglam bildirimle typecheck'ten TEMIZ gecmeli (yukaridaki
# yakalama bildirimden geliyor, programdan degil).
(cd "$TMP" && "$TUL" typecheck --ext ext kullan.tpr) >"$TMP/p3ok" 2>&1; rc=$?
[ $rc -eq 0 ] && gecti "saglam bildirimle typecheck temiz" || { dustu "saglam bildirimle typecheck dustu (rc=$rc)"; tail -6 "$TMP/p3ok"; }

# P4) Olmayan dizin, bozuk JSON, yerlesik ad, eksik link bolumu, fazla arguman
(cd "$TMP" && "$TUL" --ext olmayan_dizin kullan.tpr) >"$TMP/p4a" 2>&1; rc=$?
[ $rc -ne 0 ] && grep -q "extension not found: olmayan_dizin (--ext)" "$TMP/p4a" && gecti "olmayan eklenti dizini: adiyla hata" \
  || { dustu "olmayan dizin (rc=$rc)"; head -3 "$TMP/p4a"; }
(cd "$TMP" && TULPAR_EXT_PATH="$TMP/olmayan2" "$TUL" version) >"$TMP/p4v" 2>&1; rc=$?
[ $rc -eq 0 ] && gecti "bozuk TULPAR_EXT_PATH derlemeyen komutu (version) durdurmaz" || dustu "version bozuk TULPAR_EXT_PATH ile dustu"
mkdir -p "$TMP/bozuk_json"
printf '{\n  "tulpar_ext": 1,\n  "name": "x",\n  "functions": [ {"name": "a",} ]\n}\n' > "$TMP/bozuk_json/tulpar-ext.json"
(cd "$TMP" && "$TUL" --ext bozuk_json kullan.tpr) >"$TMP/p4b" 2>&1; rc=$?
[ $rc -ne 0 ] && grep -q "is not valid JSON (line 4)" "$TMP/p4b" && gecti "bozuk JSON: dosya + satir ile hata" \
  || { dustu "bozuk JSON (rc=$rc)"; head -3 "$TMP/p4b"; }
mkdir -p "$TMP/yerlesik"
printf '{"tulpar_ext":1,"name":"yerlesik","functions":[{"name":"len","params":["s: str"],"returns":"i64"}]}' > "$TMP/yerlesik/tulpar-ext.json"
(cd "$TMP" && "$TUL" --ext yerlesik sade.tpr) >"$TMP/p4c" 2>&1; rc=$?
[ $rc -ne 0 ] && grep -q "'len' is a built-in function name" "$TMP/p4c" && gecti "yerlesik adini tasiyan eklenti fonksiyonu reddedilir" \
  || { dustu "yerlesik ad (rc=$rc)"; head -3 "$TMP/p4c"; }
mkdir -p "$TMP/tipsiz"
printf '{"tulpar_ext":1,"name":"tipsiz","functions":[{"name":"t_f","params":["x: u8"]}]}' > "$TMP/tipsiz/tulpar-ext.json"
(cd "$TMP" && "$TUL" --ext tipsiz sade.tpr) >"$TMP/p4d" 2>&1; rc=$?
[ $rc -ne 0 ] && grep -q "unknown parameter type 'u8'" "$TMP/p4d" && gecti "bilinmeyen parametre tipi: adiyla hata" \
  || { dustu "bilinmeyen tip (rc=$rc)"; head -3 "$TMP/p4d"; }
mkdir -p "$TMP/linksiz"
printf '{"tulpar_ext":1,"name":"linksiz","functions":[{"name":"linksiz_f","returns":"i64"}]}' > "$TMP/linksiz/tulpar-ext.json"
printf 'print(linksiz_f());\n' > "$TMP/linksiz.tpr"
(cd "$TMP" && "$TUL" --ext linksiz linksiz.tpr) >"$TMP/p4e" 2>&1; rc=$?
[ $rc -ne 0 ] && grep -q "has no link section for this target" "$TMP/p4e" && gecti "kullanilan eklentinin link bolumu yok: adiyla hata" \
  || { dustu "eksik link bolumu (rc=$rc)"; head -3 "$TMP/p4e"; }
printf 'import "ornek";\nprint(ornek_topla(1, 2, 3));\n' > "$TMP/fazla.tpr"
(cd "$TMP" && "$TUL" --no-typecheck --ext ext fazla.tpr) >"$TMP/p4f" 2>&1; rc=$?
[ $rc -ne 0 ] && grep -q "3 arguments given, the extension manifest declares 2" "$TMP/p4f" && gecti "fazla arguman: derleme hatasi (imzayla)" \
  || { dustu "fazla arguman (rc=$rc)"; head -4 "$TMP/p4f"; }

# --- LSP -------------------------------------------------------------------------
PY=""
for p in python3 python; do command -v "$p" >/dev/null 2>&1 && "$p" -c 'import sys; sys.exit(0 if sys.version_info[0] == 3 else 1)' 2>/dev/null && { PY="$p"; break; }; done
if [ -n "$PY" ]; then
  if "$PY" "$SRC/lsp_sonda.py" "$TUL" "$TMP/proje" >"$TMP/lsp" 2>&1; then
    gecti "LSP: $(grep -c '^lsp: TAMAM' "$TMP/lsp") denetim (hover / tamamlama / imza yardimi; eklenti tulpar.toml'dan)"
  else
    dustu "LSP eklentiyi gormuyor"; sed 's/^/         /' "$TMP/lsp" | head -10
  fi
else
  echo "  ATLANDI  LSP denetimi (python3 yok)"
fi

if [ $fail -ne 0 ]; then echo "yerel eklenti kapisi DUSTU"; exit 1; fi
echo "yerel eklenti kapisi: temiz"
exit 0
