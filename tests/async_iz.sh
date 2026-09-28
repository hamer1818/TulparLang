#!/bin/bash
# ASYNC IZ KAPISI (K156) — async gorevlerde dogan hata NEREDEN geldigini
# soyluyor mu, ve hic await edilmeyen gorevin hatasi yutuluyor mu?
#
# Neden var: senkron kodda yakalanmayan hata mesajini basiyordu, ama async'te
# hatanin HANGI gorevde dogdugu ve hangi await'lerden gectigi kayboluyordu
# (ic ice uc async fonksiyon -> yalniz "Uncaught Exception: boom"). Hic await
# edilmeyen gorevin hatasi ise SESSIZCE yutuluyordu, cikis 0.
#
# Neyi olcuyor (uretilen ikilinin stderr'i, LC_ALL=C — metin yerele bagli):
#   1. yakalanmayan hata: mesaj + await zinciri, en distaki once, dogru sirada
#      (oyun_baslat -> seviye_yukle -> veri_oku), en icteki "thrown here";
#      cikis 1
#   2. yakalanan hata: zincir BASILMAZ (yanlis pozitif yok)
#   3. hic await edilmeyen gorevin hatasi: program sonunda uyari; cikis kodu
#      DEGISMEZ (0)
#   4. iptal edilip await edilmeyen gorev: uyari YOK (asyncio gibi)
# Pozitif kontrol (elle, 2026-09-28): kanca kurulumu ve program sonu raporu
# sokulunce 1 ve 3 numarali denetim KIRMIZI, geri konunca temiz.
set -u
cd "$(dirname "$0")/.."
TULPAR="${1:-./tulpar}"
[ -x "$TULPAR" ] || { echo "async iz kapisi: $TULPAR yok — atlandi"; exit 0; }
case "$(uname -s)" in
  MINGW*|MSYS*|CYGWIN*) ;; # Windows'ta fiber yolu; olcum ayni
esac
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
export LC_ALL=C

cat > "$TMP/zincir.tpr" <<'EOF'
async func veri_oku(int n): int {
    await sleep_async(1);
    if (n > 2) { throw "dosya yok: " + toString(n); }
    return n;
}
async func seviye_yukle(int n): int {
    int v = await veri_oku(n);
    return v * 10;
}
async func oyun_baslat(): int {
    int a = await seviye_yukle(1);
    int b = await seviye_yukle(3);
    return a + b;
}
int s = await oyun_baslat();
print(s);
EOF

cat > "$TMP/yakalanan.tpr" <<'EOF'
async func patlar(): int { await sleep_async(1); throw "x"; }
async func ara(): int { return await patlar(); }
try { int r = await ara(); } catch (e) { print("yakalandi"); }
EOF

cat > "$TMP/unutulan.tpr" <<'EOF'
async func unutulan() { await sleep_async(1); throw "kimse beklemedi"; }
async func iptal_edilen() { await sleep_async(1000); }
unutulan();
var p = iptal_edilen();
cancel(p);
await sleep_async(5);
print("son");
EOF

hata=0
kos() { # $1 ad -> $TMP/$1.out / .err / .rc
  if ! "$TULPAR" build "$TMP/$1.tpr" "$TMP/$1" >"$TMP/$1.build" 2>&1; then
    echo "async iz kapisi DUSTU: $1 derlenemedi"; tail -5 "$TMP/$1.build" | sed 's/^/    /'; exit 1
  fi
  "$TMP/$1" >"$TMP/$1.out" 2>"$TMP/$1.err"
  echo $? >"$TMP/$1.rc"
}
denet() { # $1 kosul (0/1) $2 aciklama
  if [ "$1" = 1 ]; then echo "  gecti  $2"; else echo "  DUSTU  $2"; hata=1; fi
}

kos zincir
e="$TMP/zincir.err"
# Satir numaralari: zincir basligindan sonra uc satir, sirayla.
l_bas=$(grep -n "async trace" "$e" | head -1 | cut -d: -f1)
l1=$(grep -n "at oyun_baslat" "$e" | head -1 | cut -d: -f1)
l2=$(grep -n "seviye_yukle .*re-raised" "$e" | head -1 | cut -d: -f1)
l3=$(grep -n "veri_oku .*thrown here" "$e" | head -1 | cut -d: -f1)
ok=0
[ -n "$l_bas" ] && [ -n "$l1" ] && [ -n "$l2" ] && [ -n "$l3" ] && \
  [ "$l_bas" -lt "$l1" ] && [ "$l1" -lt "$l2" ] && [ "$l2" -lt "$l3" ] && ok=1
denet $ok "yakalanmayan hata: await zinciri en distaki once (oyun_baslat -> seviye_yukle -> veri_oku)"
ok=0; grep -q "^Uncaught Exception: dosya yok: 3" "$e" && [ "$(cat "$TMP/zincir.rc")" = 1 ] && ok=1
denet $ok "yakalanmayan hata: mesaj + cikis 1"

kos yakalanan
ok=0; ! grep -q "async trace" "$TMP/yakalanan.err" && grep -q "yakalandi" "$TMP/yakalanan.out" && ok=1
denet $ok "yakalanan hata: zincir basilmaz"

kos unutulan
u="$TMP/unutulan.err"
ok=0; grep -q "never awaited .*unutulan: kimse beklemedi" "$u" && [ "$(cat "$TMP/unutulan.rc")" = 0 ] && ok=1
denet $ok "await edilmeyen gorevin hatasi: uyari, cikis kodu 0"
ok=0; ! grep -q "iptal_edilen" "$u" && ok=1
denet $ok "iptal edilip await edilmeyen gorev: uyari yok"

if [ $hata -ne 0 ]; then
  echo "async iz kapisi DUSTU"
  echo "  --- zincir.err ---"; sed 's/^/    /' "$TMP/zincir.err"
  echo "  --- unutulan.err ---"; sed 's/^/    /' "$TMP/unutulan.err"
  exit 1
fi
echo "async iz kapisi: 5 denetim temiz"
exit 0
