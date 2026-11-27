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
    # tr: Windows (MSYS2) ciktisi CRLF — beklenen metinle karsilastirma bozulur.
    ( cd "$TMP" && DISPLAY= WAYLAND_DISPLAY= TULPAR_ARENA_ZEHIR="$1" sureli 60 "$TULPAR" "$2" 2>&1 | tr -d '\r' )
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
# Yerine gelmis promise kendi kontrol noktasinin drop'undan SONRA okunur
# (Async RFC A.4, 2026-11-27: promise bolgede izlenir, yerine gelmisse drop'ta
# birakilir). Ayni noktada BEKLEYEN promise ise terfi eder — guvenli
# desenlerde (g) olculuyor.
cat > "$TMP/sark_promise.tpr" <<'T'
import "async";
async func is(): json { return {"r": "promise-" + toString(77)}; }
func yap(): int {
    int wm = arena_save();
    var p = is();
    await sleep_async(1);
    arena_drop(wm);
    for (int i = 0; i < 50; i++) { json cop = {"cop": "c-" + toString(i)}; }
    json r = await p;
    print(r["r"]);
    return 0;
}
yap();
T
z=$(kos 1 sark_promise.tpr)
case "$z" in *promise-77*) dustu "zehir: drop sonrasi yerine gelmis promise okumasi hala dogru ($z)";;
             *) gecti "zehir: drop sonrasi yerine gelmis promise okumasi bozuk gorunuyor";; esac

# Kural 4.2 [DELIK] (2026-11-27): noktanin DISINDA tanimli yerele nokta
# icinde gecici deger yazilir (`acc = acc + x`) — bariyer yalniz global ve
# kalici kaplari kapsar, yerel drop'ta sarkar. Kullanici kurali: persist()
# (guvenli desen i). Derleme aninda typecheck bunu UYARIR (fikstur:
# tests/typeinfer/fail/29_checkpoint_outer_local.tpr); bu kapi calisma
# zamaninda sarktigini olcer. Olculdu: normal kipte de bozuk (coplenen arena
# dizgisi `cop-5-...` basiyor), zehirde bozuk.
cat > "$TMP/sark_yerel.tpr" <<'T'
func yap(): str {
    str acc = "bas";
    int wm = arena_save();
    for (int i = 0; i < 5; i++) { acc = acc + "-parca" + toString(i); }
    arena_drop(wm);
    for (int i = 0; i < 100; i++) { str cop = "cop-" + toString(i) + "-xxxxxxxxxxxxxxxxxxxxxxxx"; }
    return acc;
}
print(yap());
T
z=$(kos 1 sark_yerel.tpr)
case "$z" in *bas-parca0-parca1-parca2-parca3-parca4*) dustu "zehir: noktayi asan yerel (Kural 4.2) hala dogru basiyor ($z)";;
             *) gecti "zehir: noktayi asan yereldeki gecici deger bozuk gorunuyor (Kural 4.2 deligi)";; esac

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
//     Ana akista (Async RFC A.2: coroutine'de acik nokta await'i asamaz;
//     ana akista block-on serbest — gorev ayri async baglaminda kosar).
async func d2_is(): json { return {"r": "sonuc-" + toString(5)}; }
json g_d2 = {};
func d2_kos(): str {
    int wm = arena_save();
    var p = d2_is();
    await sleep_async(1);
    // Async RFC A.4: promise bu noktanin geçici nesnesi — sonucu drop'tan
    // ONCE al; drop'u global (bariyer) asar. Drop'tan sonra `await p`
    // bilinen sarkan desen (pozitif kontrol: sark_promise.tpr).
    g_d2 = await p;
    arena_drop(wm);
    for (int i = 0; i < 50; i++) { json cop = {"cop": "c-" + toString(i)}; }
    return g_d2["r"];
}
// g) Async RFC A.4: drop aninda BEKLEYEN promise terfi eder (gorev hala ona
//    bakiyor) — drop'tan sonra beklemek guvenli.
async func g_gec(): int { await sleep_async(5); return 9; }
func g_bekleyen(): int {
    int wm = arena_save();
    var p = g_gec();
    arena_drop(wm);
    for (int i = 0; i < 50; i++) { json cop = {"cop": "c-" + toString(i)}; }
    return await p;
}
// h) Async RFC A.1: ana akis noktasi acikken bekler; o sirada UZUN coroutine
//    dizgi/json kurar. Noktanin drop'u coroutine'in async baglamina dokunmaz.
async func h_uzun(): str {
    await sleep_async(1);
    str s = "ic-" + toString(3);
    json j = {"s": s};
    await sleep_async(20);
    return j["s"];
}
async func h_kisa(): int { await sleep_async(5); return 1; }
func g_ic_ice(): str {
    var pu = h_uzun();
    int wm = arena_save();
    int k = await h_kisa();
    arena_drop(wm);
    for (int i = 0; i < 50; i++) { str cop = "c-" + toString(i) + "-xxxxxxxxxxxxxxxx"; }
    return await pu;
}
// e) thread arg kopyasi.
int g_thr = 0;
func e_thr(json j) { g_thr = j["x"] + 1; }
// e2) Kural 6.4: thread SONUCU kopyalanarak doner. Thread json/dizgiyi kendi
//     arenasinda kurar; cagiranin noktasi create ile join arasinda drop
//     edilip coplenir; join'in dondurdugu deger saglam olmali.
func e2_thr(json j): json { return {"s": j["ad"] + "-sonuc", "n": [1, 2, 3]}; }
// i) Kural 4.2'nin kullanici kurali: noktayi asan yerele persist() ile yaz.
func i_yerel(): str {
    str acc = "bas";
    int wm = arena_save();
    for (int i = 0; i < 5; i++) { acc = persist(acc + "-parca" + toString(i)); }
    arena_drop(wm);
    for (int i = 0; i < 100; i++) { str cop = "cop-" + toString(i) + "-xxxxxxxxxxxxxxxxxxxxxxxx"; }
    return acc;
}
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
print(d2_kos());
int wm = arena_save();
var t = thread_create("e_thr", {"x": 41});
arena_drop(wm);
thread_join(t);
print(g_thr);
int wm2 = arena_save();
var t2 = thread_create("e2_thr", {"ad": "is-" + toString(8)});
arena_drop(wm2);
for (int i = 0; i < 200; i++) { json cop = {"c": "cop-" + toString(i)}; }
json r2 = thread_join(t2);
for (int i = 0; i < 200; i++) { json cop = {"c": "cop-" + toString(i)}; }
print(r2["s"] + " " + toString(len(r2["n"])));
print(i_yerel());
f_dongu();
print(toString(len(g_birikim)) + " " + g_birikim[0] + " " + g_birikim[19]);
print(g_bekleyen());
print(g_ic_ice());
T
beklenen='kayit-7 3 isim-99
42
frame-0
frame-1
frame-2
42
sonuc-5
42
is-8-sonuc 3
bas-parca0-parca1-parca2-parca3-parca4
20 satir-0 satir-19
9
ic-3'
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
     out=$(printf "%s\n" "$out" | tr -d "\r");
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
echo "PASS: sarkan_isaretci — zehir sarkan json/dizgiyi ve bilerek sarkan testi yakaliyor; 9 guvenli desen iki kipte ayni; $paket_n paket zehirde temiz"
