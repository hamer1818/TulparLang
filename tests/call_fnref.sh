#!/bin/bash
# call() FONKSIYON REFERANSI KAPISI — referans ayirmasiz mi, call() ad
# aramasini gercekten atliyor mu?
#
# 2026-10-01: fonksiyon adi deger olarak kullanilinca (`call(f, x)`,
# `var t = [f, g]`) codegen HER degerlendirmede yeni bir arena dizgisi
# kuruyordu; dongude `acc = call(f, acc)` 20M kez -> 1,26 GB tepe bellek
# (olculdu, Ryzen 7 9800X3D). call() her cagrida adi hash'leyip onbellegi
# yokluyordu: benchmarks/fair/callfn 15 ns/cagri. Artik referans runtime'in
# havuzunda kalici bir dizgi (site basina bir kez), call() havuz kaydini
# tanirsa kutulu giris noktasini dogrudan cagiriyor — arite tutarsa SATIR
# ICINDE (codegen), tutmazsa runtime'da ad aramasiz.
#
# Uc ayak:
#   1. AYIRMA YOK: ayni sonda N=100 000 ve N=3 000 000 ile kosar, tepe RSS
#      farki 16 MB'nin altinda olmali (eski yol: ~2,9M x 64 B = 185 MB).
#      Fork/wait4 gerektiriyor; Windows'ta GORUNUR atlanir.
#   2. SATIR ICI YOL URETILIYOR: IR'de havuz denetimi (`fnref.call`) ve
#      site basina tek `aot_fn_ref` var, referans icin `vm_alloc_string_aot`
#      YOK.
#   3. RUNTIME HAVUZ YOLU SAYILIYOR: TULPAR_CALL_TANI=1 surec sonunda
#      `call-tani: havuz=H hizli=K yerel=Y` basar (Y: yerel int giris
#      noktasi tasiyan kayit; buradaki `h` tipsiz, Y = 0). Sonda arite FARKLI (VOID doldurma)
#      havuz cagrisini tam 1000 kez yapar -> K = 1000; duz dizgi adla yapilan
#      1000 cagri SAYILMAMALI (sayac ayirt ediyor mu — kapinin kendi kontrolu).
set -u
cd "$(dirname "$0")/.."
TULPAR="${1:-./tulpar}"
[ -x "$TULPAR" ] || { echo "call fnref kapisi: $TULPAR yok — atlandi"; exit 0; }

TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
HATA=0

cat > "$TMP/dongu.tpr" <<'TPREOF'
func f(int x): int { return (x * 31 + 7) % 1000003; }
func g(int x): int { return (x * 17 + 3) % 1000003; }
int n = toInt(env("CF_N"));
var tab = [f, g];
int acc = 1;
for (int i = 0; i < n; i = i + 1) {
    acc = call(f, acc);
    acc = call(tab[acc & 1], acc);
}
print(acc);
TPREOF

if ! TULPAR_AOT_EMIT_LL=1 "$TULPAR" build "$TMP/dongu.tpr" "$TMP/dongu" >"$TMP/d.log" 2>&1; then
  echo "call fnref kapisi DUSTU: sonda derlenmedi"; cat "$TMP/d.log"; exit 1
fi

# --- 2. IR ---------------------------------------------------------------
LL="$TMP/dongu.ll"
if [ ! -f "$LL" ]; then
  echo "  IR dokumu yok ($LL)"; HATA=1
else
  NCALL=$(grep -cE "^fnref\.call[0-9]*:" "$LL" || true)
  NREF=$(grep -c "call ptr @aot_fn_ref(" "$LL" || true)
  NALLOC=$(grep -c "@vm_alloc_string_aot(ptr null, ptr nonnull @fn_ref" "$LL" || true)
  [ "$NCALL" -ge 2 ] || { echo "  satir ici havuz cagrisi yok (fnref.call: $NCALL, beklenen >= 2)"; HATA=1; }
  [ "$NREF" -ge 2 ] || { echo "  site basina aot_fn_ref yok ($NREF, beklenen >= 2)"; HATA=1; }
  [ "$NALLOC" -eq 0 ] || { echo "  referans icin hala vm_alloc_string_aot cagriliyor ($NALLOC)"; HATA=1; }
fi

# --- 1. AYIRMA YOK ---------------------------------------------------------
case "$(uname -s)" in
  MINGW*|MSYS*|CYGWIN*)
    echo "  [ayirma ayagi ATLANDI: Windows'ta fork/wait4 yok — rsswrap kosamiyor]" ;;
  *)
    CC_BIN="${CC:-}"
    if [ -z "$CC_BIN" ]; then
      for c in cc gcc clang; do command -v "$c" >/dev/null 2>&1 && { CC_BIN="$c"; break; }; done
    fi
    if [ -z "$CC_BIN" ] || ! "$CC_BIN" -O2 benchmarks/fair/rsswrap.c -o "$TMP/rsswrap" 2>/dev/null; then
      echo "call fnref kapisi DUSTU: rsswrap derlenemedi (C derleyicisi?)"; exit 1
    fi
    rss() { CF_N=$1 "$TMP/rsswrap" "$TMP/dongu" 2>&1 >/dev/null | sed -n 's/^RSS_KB=//p'; }
    R1=$(rss 100000); R2=$(rss 3000000)
    # macOS ru_maxrss BAYT, Linux KB.
    if [ "$(uname -s)" = "Darwin" ]; then R1=$((R1 / 1024)); R2=$((R2 / 1024)); fi
    if [ -z "$R1" ] || [ -z "$R2" ]; then
      echo "  RSS okunamadi (R1='$R1' R2='$R2')"; HATA=1
    else
      FARK=$(( (R2 - R1) / 1024 ))
      if [ "$FARK" -ge 16 ]; then
        echo "  2,9M ek tur tepe RSS'i ${FARK} MB buyuttu (esik 16 MB) — referans/cagri ayiriyor"; HATA=1
      fi
      echo "  ayirma: N=100k ${R1} KB, N=3M ${R2} KB, fark ${FARK} MB (esik 16)"
    fi ;;
esac

# --- 3. RUNTIME HAVUZ YOLU SAYACI -----------------------------------------
cat > "$TMP/sayac.tpr" <<'TPREOF'
func h(a, b) { return 1; }
int s = 0;
for (int i = 0; i < 1000; i = i + 1) {
    s = s + call(h, i);        // arite 2, arguman 1: runtime havuz yolu
    s = s + call("h", i);      // duz dizgi ad: SAYILMAZ
}
print(s);
TPREOF
if ! "$TULPAR" build "$TMP/sayac.tpr" "$TMP/sayac" >"$TMP/s.log" 2>&1; then
  echo "call fnref kapisi DUSTU: sayac sondasi derlenmedi"; cat "$TMP/s.log"; exit 1
fi
SOUT=$(TULPAR_CALL_TANI=1 "$TMP/sayac" 2>"$TMP/s.err" | tr -d '\r')
TANI=$(tr -d '\r' < "$TMP/s.err")
[ "$SOUT" = "2000" ] || { echo "  sayac sondasi ciktisi '$SOUT' (beklenen 2000)"; HATA=1; }
if ! echo "$TANI" | grep -qE "^call-tani: havuz=[1-9][0-9]* hizli=1000 yerel=0$"; then
  echo "  beklenen 'call-tani: havuz=H hizli=1000 yerel=0' yok; gelen: '$TANI'"; HATA=1
fi
SESSIZ=$("$TMP/sayac" 2>&1 >/dev/null | grep -c "call-tani" || true)
[ "$SESSIZ" -eq 0 ] || { echo "  anahtar KAPALIYKEN tani basildi"; HATA=1; }

if [ "$HATA" -ne 0 ]; then
  echo "call fnref kapisi DUSTU"
  exit 1
fi
echo "call fnref kapisi: GECTI (satir ici havuz cagrisi $NCALL site, referans ayirmasiz, runtime havuz yolu 1000/1000 sayildi, duz ad sayilmadi)"
