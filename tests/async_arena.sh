#!/bin/bash
# ASYNC ARENA KAPISI (2026-11-27) — "RFC: Tulpar Async Bellek Sozlesmesi v0".
#
# NEDEN. Arena (dizgi arenasi + kontrol noktasi yigini + bolge) iplik basina,
# async coroutine'ler ise ayni iplikte sirayla kosar. Duzeltmeden once bir
# kontrol noktasi await'i asinca araya giren BASKA coroutine'in degerleri o
# noktanin drop'unda oluyordu: json'da calisma zamani hatasi, dizgide SESSIZCE
# `<object>`. gather argumanlari kaliciya gecmiyordu (sessiz `<object>`), her
# await ~71 bayt promise birakiyordu (hic serbest kalmiyordu).
#
# NE OLCULUYOR (her biri normal + TULPAR_ARENA_ZEHIR=1)
#   A.1 ana akis noktasi acikken await; uzun coroutine o sirada dizgi/json
#       kurar; drop coroutine'in degerine dokunmaz.
#   A.2 coroutine'de acik noktayla await YAKALANABILIR hata (iki coroutine
#       ic ice gecerken ve tek coroutine'de) — mesaj adiyla.
#   A.3 kontrol noktasinda gecici dizi gather'a verilir, drop, sonra beklenir.
#   A.4 kontrol noktali 20k await dongusunde yigin (heap_in_use) duz kalir,
#       noktasiz dongude buyur (ayni kosuda pozitif kontrol); drop aninda
#       BEKLEYEN promise terfi eder ve promoted_promises'te sayilir.
# SABOTAJ (2026-11-27, elle): ayni kapi duzeltmesiz main (77c16604) ikilisiyle
# kosuldu — A.1/A.2/A.3/A.4 denetimlerinin hepsi KIRMIZI (bkz. commit mesaji).
set -u
cd "$(dirname "$0")/.."
TULPAR="${1:-$(pwd)/tulpar}"
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
FAIL=0
sureli() { perl -e 'alarm shift; exec @ARGV' "$@"; }
gecti() { echo "  gecti  $1"; }
dustu() { echo "  DUSTU  $1"; FAIL=1; }
kos() {  # kos <zehir 0|1> <dosya>
    ( cd "$TMP" && DISPLAY= WAYLAND_DISPLAY= TULPAR_ARENA_ZEHIR="$1" sureli 60 "$TULPAR" --no-typecheck "$2" 2>&1 | tr -d '\r' )
}
iki_kip() {  # iki_kip <ad> <dosya> <beklenen>
    local n z
    n=$(kos 0 "$2"); z=$(kos 1 "$2")
    if [ "$n" = "$3" ] && [ "$z" = "$3" ]; then gecti "$1 (normal + zehir)"
    else dustu "$1"; echo "      normal: $n" | head -4; echo "      zehir:  $z" | head -4; fi
}

# --- A.1 ---------------------------------------------------------------------
cat > "$TMP/a1.tpr" <<'T'
async func uzun(): str {
    await sleep_async(1);
    str s = "dizgi-" + toString(7) + "-uzun-bir-metin";
    json j = {"v": "json-" + toString(8)};
    await sleep_async(40);
    return s + " " + j["v"];
}
async func kisa(): int { await sleep_async(10); return 5; }
var pu = uzun();
int wm = arena_save();
str ana = "ana-" + toString(1);
int r = await kisa();
arena_drop(wm);
for (int i = 0; i < 50; i++) { str cop = "ustune-" + toString(i) + "-xxxxxxxxxxxxxxxxxxxxxxxx"; }
print(toString(r) + " " + await pu);
T
iki_kip "A.1: ana akis noktasi await'i asar, coroutine degerleri drop'tan sag cikar" a1.tpr \
  "5 dizgi-7-uzun-bir-metin json-8"

# --- A.2 ---------------------------------------------------------------------
cat > "$TMP/a2.tpr" <<'T'
async func a() {
    int wm = arena_save();
    json x = {"v": "a-" + toString(1)};
    try {
        await sleep_async(10);
        print("A2 YANLIS: await gecti");
    } catch (e) {
        print("yakalandi: " + toString(contains(toString(e), "await")));
    }
    arena_drop(wm);
    await sleep_async(1);
    return 1;
}
async func b() {
    await sleep_async(1);
    json y = {"v": "b-" + toString(2)};
    await sleep_async(30);
    return y["v"];
}
var pa = a();
var pb = b();
print(await pa);
print(await pb);
T
iki_kip "A.2: coroutine'de acik noktayla await yakalanabilir hata; oteki coroutine saglam" a2.tpr \
  "yakalandi: true
1
b-2"
# Yakalanmazsa: mesaj + async iz, cikis 1.
cat > "$TMP/a2b.tpr" <<'T'
async func tek() {
    int wm = arena_save();
    await sleep_async(1);
    arena_drop(wm);
    return 1;
}
print(await tek());
print("A2 YANLIS: devam etti");
T
out=$(kos 0 a2b.tpr); rc=$?
if grep -q "await" <<<"$out" && grep -q "at tek" <<<"$out" && ! grep -q "A2 YANLIS" <<<"$out"; then
  gecti "A.2: yakalanmayan hata mesaj + async iz ile durur (tek coroutine)"
else
  dustu "A.2 yakalanmayan yol: $(head -3 <<<"$out")"
fi

# --- A.3 ---------------------------------------------------------------------
cat > "$TMP/a3.tpr" <<'T'
func kur(): array {
    int wm = arena_save();
    array d = [1, 2, 3];
    push(d, "x-" + toString(4));
    var g = gather(d, sleep_async(5));
    arena_drop(wm);
    return [g];
}
array k = kur();
for (int i = 0; i < 50; i++) { array cop = [i, "c-" + toString(i)]; }
var sonuc = await k[0];
print(sonuc[0]);
T
iki_kip "A.3: gather argumani kontrol noktasindan kacar" a3.tpr '[1, 2, 3, "x-4"]'

# --- A.4 ---------------------------------------------------------------------
cat > "$TMP/a4.tpr" <<'T'
async func f(int x) { return x + 1; }
async func gec(): int { await sleep_async(5); return 9; }
int s = 0;
// Isinma KONTROL NOKTALI: ilk arena_save ana akisin 1 MB'lik dizgi blogunu
// bir kez ayirir — olcumun icine dusmesin (ilk yazimda 1 MB "sizinti" gibi
// gorundu).
for (int i = 0; i < 2000; i++) { int w = arena_save(); s = s + await f(i); arena_drop(w); }
for (int i = 0; i < 2000; i++) { s = s + await f(i); }
int h0 = mem_stats()["heap_in_use"];
for (int i = 0; i < 20000; i++) {
    int wm = arena_save();
    s = s + await f(i);
    arena_drop(wm);
}
int h1 = mem_stats()["heap_in_use"];
for (int i = 0; i < 20000; i++) { s = s + await f(i); }
int h2 = mem_stats()["heap_in_use"];
// Bekleyen promise YERELDE (ust duzey `var p` global olurdu: bariyer onu
// zaten yerinde terfi ettirir, drop'a kalmaz).
func bekleyen(): str {
    int p0 = mem_stats()["promoted_promises"];
    int wm = arena_save();
    var p = gec();
    arena_drop(wm);
    int p1 = mem_stats()["promoted_promises"];
    for (int i = 0; i < 50; i++) { json cop = {"c": "c-" + toString(i)}; }
    return "bekleyen " + toString(await p) + " terfi " + toString(p1 - p0);
}
print(bekleyen());
if (h0 < 0) { print("heap bilinmiyor"); }
else {
    print("noktali_kb " + toString((h1 - h0) / 1024));
    print("noktasiz_kb " + toString((h2 - h1) / 1024));
}
T
for z in 0 1; do
  out=$(kos $z a4.tpr)
  if grep -qx "bekleyen 9 terfi 1" <<<"$out"; then gecti "A.4 (zehir=$z): drop aninda bekleyen promise terfi eder, sayilir, beklenebilir"
  else dustu "A.4 bekleyen promise (zehir=$z): $(head -3 <<<"$out")"; fi
  if grep -q "heap bilinmiyor" <<<"$out"; then
    echo "  atla   A.4 (zehir=$z): heap_in_use bu platformda yok"
  elif [ "$z" = 0 ]; then
    nk=$(sed -n 's/^noktali_kb //p' <<<"$out"); sk=$(sed -n 's/^noktasiz_kb //p' <<<"$out")
    # Pozitif kontrol: noktasiz dongu buyumeli (20k promise ~1.4 MB); noktali duz.
    if [ -n "$nk" ] && [ -n "$sk" ] && [ "$sk" -gt 700 ] && [ "$nk" -lt 200 ]; then
      gecti "A.4: kontrol noktali 20k await ${nk} KB, noktasiz ${sk} KB (promise geri aliniyor)"
    else
      dustu "A.4 bellek: noktali=${nk:-?} KB noktasiz=${sk:-?} KB (beklenen <200 / >700)"
    fi
  fi
done

if [ $FAIL -eq 0 ]; then echo "PASS: async_arena — A.1-A.4 normal ve zehir kipte"; else echo "FAIL: async_arena"; exit 1; fi
