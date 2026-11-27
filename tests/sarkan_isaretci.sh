#!/bin/bash
# SARKAN ISARETCI KAPISI (2026-11-26) — arena + bolge + ARC sinirlari.
#
# NEDEN. Geri sarilan arena bolgesi uzerine yazilana kadar DOGRU okunur:
# checkpoint icinde kurulup drop'tan sonra okunan gecici deger (Bellek
# Sozlesmesi Kural 2.1) cogu kosuda "calisir", hata bellek yeniden
# kullanilinca baska bir yerde sessizce yanlis veri olarak cikar. ASan bunu
# goremez: runtime arsivi enstrumante degil, arena malloc degil.
# TULPAR_ARENA_ZEHIR=1 geri sarmada olu baytlari 0xDB ile doldurur ve bolge
# kaplarini karantinaya alir (runtime_bindings.cpp "ZEHIR KIPI").
#
# NE OLCULUYOR
#   1. POZITIF KONTROL: bilinen sarkan desenler (drop'tan sonra json ve dizgi
#      okuma) zehirde dogru degeri BASAMAZ. Normal kipte dizgi deseni dogru
#      basiyor — tehlikenin sessiz oldugunun kaniti, o da olculur.
#   2. GUVENLI DESENLER iki kipte AYNI ve dogru: global'e atama (bariyer
#      kopyasi), drop'tan once skalere alma, @frame donus degeri, async
#      gorev arg'i ve async sonucu (ARC) checkpoint disina, thread arg
#      kopyasi (ad dizgiyle), restore dongusunde kalici birikim. Uc desen bu
#      kapi yazilirken BULUNAN hatalardi: tulpar_async.cpp argumani/sonucu
#      yalniz arc_retain ile tutuyordu, geri sarma refcount'a bakmiyor ->
#      normal kipte sessizce 0 ("ARC + ARENA SINIRI"); thread_create("ad", x)
#      bos isaretciyle derleniyordu -> thread hic calismiyordu.
#   3. BUTUN regresyon paketleri zehirde gecer — tek istisna asagidaki
#      BILEREK sarkan test (adiyla; baska bir test duserse kirmizi).
set -u
cd "$(dirname "$0")/.."
TULPAR="$(pwd)/tulpar"
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
FAIL=0
# Sureli kosum: `timeout` macOS'ta yok (CI arm64'te "command not found"), perl
# her yerde var — tests/sanitize_smoke.sh ile ayni yol.
sureli() { perl -e 'alarm shift; exec @ARGV' "$@"; }
export -f sureli
gecti() { echo "  gecti  $1"; }
dustu() { echo "  DUSTU  $1"; FAIL=1; }
kos() {  # kos <zehir 0|1> <dosya>
    ( cd "$TMP" && DISPLAY= WAYLAND_DISPLAY= TULPAR_ARENA_ZEHIR="$1" sureli 60 "$TULPAR" "$2" 2>&1 )
}

# --- 1. pozitif kontrol ---------------------------------------------------
cat > "$TMP/sark_json.tpr" <<'T'
func yap(): json {
    int wm = arena_save();
    json t = {"a": "deger-" + toString(42), "b": [1, 2, 3]};
    arena_drop(wm);
    return t;
}
json j = yap();
for (int i = 0; i < 100; i++) { json y = {"x": i}; }
print(j["a"]);
T
cat > "$TMP/sark_dizgi.tpr" <<'T'
func yap(): str {
    int wm = arena_save();
    str s = "gecici-" + toString(12345) + "-uzun-bir-dizgi";
    arena_drop(wm);
    return s;
}
print(yap());
T
z=$(kos 1 sark_json.tpr)
case "$z" in *deger-42*) dustu "zehir: drop sonrasi json okumasi hala dogru basiyor ($z)";;
             *) gecti "zehir: drop sonrasi json okumasi bozuk gorunuyor";; esac
n=$(kos 0 sark_dizgi.tpr); z=$(kos 1 sark_dizgi.tpr)
case "$n" in *gecici-12345-uzun-bir-dizgi*) gecti "normal kip: sarkan dizgi SESSIZCE dogru basiyor (tehlike gorunmez)";;
             *) echo "  not    normal kipte sarkan dizgi zaten bozuk: $n";; esac
case "$z" in *gecici-12345-uzun-bir-dizgi*) dustu "zehir: drop sonrasi dizgi okumasi hala dogru ($z)";;
             *) gecti "zehir: drop sonrasi dizgi okumasi bozuk gorunuyor";; esac

# --- 2. guvenli desenler --------------------------------------------------
cat > "$TMP/guvenli.tpr" <<'T'
import "async";
json g_son = {};
str g_ad = "";
array g_birikim = [];

// a) global'e atama: bariyer kaliciya kopyalar.
func a_global() {
    int wm = arena_save();
    g_son = {"ad": "kayit-" + toString(7), "liste": [1, 2, 3]};
    g_ad = "isim-" + toString(99);
    arena_drop(wm);
}
// b) drop'tan once skalere al.
func b_skaler(): int {
    int wm = arena_save();
    json t = {"n": 40 + 2};
    int n = t["n"];
    arena_drop(wm);
    return n;
}
// c) @frame: donus degeri cerceve disina tasinir.
@frame
func c_frame(int k): str {
    json t = {"k": "frame-" + toString(k)};
    return t["k"];
}
// d) async gorev: checkpoint icinde baslatilip disarida beklenir (ARC).
async func d_is(json girdi): int {
    await sleep_async(1);
    return girdi["v"] * 2;
}
async func d_kos(): int {
    int wm = arena_save();
    var p = d_is({"v": 21});
    arena_drop(wm);
    for (int i = 0; i < 50; i++) { json cop = {"cop": i}; }
    return await p;
}
// d2) gorev checkpoint ICINDE biter, sonucu checkpoint DISINDA beklenir.
async func d2_is(): json { return {"r": "sonuc-" + toString(5)}; }
async func d2_kos(): str {
    int wm = arena_save();
    var p = d2_is();
    await sleep_async(1);
    arena_drop(wm);
    for (int i = 0; i < 50; i++) { json cop = {"cop": "c-" + toString(i)}; }
    json r = await p;
    return r["r"];
}
// e) thread arg kopyasi.
int g_thr = 0;
func e_thr(json j) { g_thr = j["x"] + 1; }
// f) tek save + cok restore dongusu, kaliciya birikim.
func f_dongu() {
    int wm = arena_save();
    for (int i = 0; i < 20; i++) {
        json r = {"i": i, "s": "satir-" + toString(i)};
        push(g_birikim, r["s"]);
        arena_restore(wm);
    }
    arena_drop(wm);
}

a_global();
for (int i = 0; i < 200; i++) { json cop = {"c": "cop-" + toString(i)}; }
print(g_son["ad"] + " " + toString(len(g_son["liste"])) + " " + g_ad);
print(b_skaler());
for (int i = 0; i < 3; i++) { print(c_frame(i)); }
print(await d_kos());
print(await d2_kos());
int wm = arena_save();
var t = thread_create("e_thr", {"x": 41});
arena_drop(wm);
thread_join(t);
print(g_thr);
f_dongu();
print(toString(len(g_birikim)) + " " + g_birikim[0] + " " + g_birikim[19]);
T
beklenen='kayit-7 3 isim-99
42
frame-0
frame-1
frame-2
42
sonuc-5
42
20 satir-0 satir-19'
n=$(kos 0 guvenli.tpr); z=$(kos 1 guvenli.tpr)
if [ "$n" = "$beklenen" ]; then gecti "guvenli desenler normal kipte dogru"; else dustu "guvenli desenler normal kipte YANLIS:"; echo "$n" | sed 's/^/      /'; fi
if [ "$z" = "$beklenen" ]; then gecti "guvenli desenler zehirde de dogru (yanlis alarm yok)"; else dustu "guvenli desenler zehirde YANLIS:"; echo "$z" | sed 's/^/      /'; fi

# --- 3. butun paketler zehirde --------------------------------------------
# BILEREK sarkan tek test: drop'tan sonra olu dizgiyi okuyup bariyerin
# "kurtardigini" sabitliyor (eski davranisla uyum). Zehrin onu yakalamasi
# BEKLENEN — ikinci pozitif kontrol.
BILEREK="gecici_dizgi.test.tpr|olu dizgi kopyalanarak kurtarilir"
ls tests/*.test.tpr > "$TMP/paketler"
paket_n=$(wc -l < "$TMP/paketler")
xargs -P "${TULPAR_TEST_JOBS:-$(nproc 2>/dev/null || echo 4)}" -I{} sh -c \
    'out=$(DISPLAY= WAYLAND_DISPLAY= TULPAR_ARENA_ZEHIR=1 perl -e "alarm shift; exec @ARGV" 180 "$1" "$2" 2>&1); rc=$?;
     b=$(basename "$2"); printf "%s\n" "$out" | grep -E "^ *FAIL" | sed "s/^ *FAIL /$b|/" ;
     printf "%s\n" "$out" | grep -q "Tests:" || echo "$b|OZET-YOK rc=$rc"' _ "$TULPAR" {} \
    < "$TMP/paketler" > "$TMP/zehir_fail" 2>&1
if grep -q "^$BILEREK" "$TMP/zehir_fail"; then
    gecti "zehir bilerek sarkan testi yakaladi ($BILEREK)"
else
    dustu "zehir bilerek sarkan testi YAKALAMADI — kip etkisiz mi?"
    sed 's/^/      /' "$TMP/zehir_fail" | head -5
fi
beklenmeyen=$(grep -v "^$BILEREK" "$TMP/zehir_fail" | grep -v '^$' || true)
if [ -z "$beklenmeyen" ]; then
    gecti "$paket_n paket zehirde gecti (bilerek sarkan tek test disinda)"
else
    dustu "zehirde beklenmeyen dusus:"
    echo "$beklenmeyen" | head -10 | sed 's/^/      /'
fi

if [ $FAIL -ne 0 ]; then echo "FAIL: sarkan_isaretci"; exit 1; fi
echo "PASS: sarkan_isaretci — zehir sarkan json/dizgiyi ve bilerek sarkan testi yakaliyor; 7 guvenli desen iki kipte ayni; $paket_n paket zehirde temiz"
