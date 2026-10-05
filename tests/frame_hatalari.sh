#!/usr/bin/env bash
# `@frame` (K038) — BELLEK kapisi + ayristirma hata yollari (2026-09-28).
#
# Bellek: ayni program @frame'li ve @frame'siz derlenip tepe RSS (VmHWM)
# karsilastiriliyor. @frame'siz surumun BUYUMESI pozitif kontrol — olcum
# gercekten ayirma goruyor mu. Olculdu (bu makine, 2026-09-28): 20 000 cagri,
# cagri basina ~36 KB gecici dizgi: @frame'siz 733 064 kB, @frame'li
# 2 960 kB; kapi 6 000 cagriyla kosuyor. /proc okunamayan platformda
# (macOS, Windows) bellek kismi ATLANIR (acikca basilir).
#
#   tests/frame_hatalari.sh [tulpar_yolu]
set -uo pipefail
export LC_ALL=C   # ayristirici tanilari tr_en; beklenenler Ingilizce bicim
TUL="${1:-./tulpar}"
[ -x "$TUL" ] || { echo "HATA: '$TUL' calistirilabilir degil" >&2; exit 1; }
TUL="$(cd "$(dirname "$TUL")" && pwd)/$(basename "$TUL")"
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
fail=0
n_gecti=0
gecti() { n_gecti=$((n_gecti + 1)); printf "  \033[0;32mgecti\033[0m  %s\n" "$1"; }
dustu() { printf "  \033[0;31mDUSTU\033[0m  %s\n" "$1"; fail=1; }
reddedilmeli() {
    local ad="$1" bekle="$2" kaynak="$3"
    printf '%s\n' "$kaynak" > "$TMP/$ad.tpr"
    local out rc
    out=$(cd "$TMP" && "$TUL" build "$ad.tpr" "$ad.out" 2>&1); rc=$?
    if [ "$rc" -eq 0 ]; then
        dustu "$ad: derleme BASARILI oldu, hata bekleniyordu"
    elif ! grep -qF -- "$bekle" <<<"$out"; then
        dustu "$ad: reddedildi ama beklenen mesaj yok: '$bekle'"
        echo "$out" | sed 's/^/         /'
    else
        gecti "$ad"
    fi
}

reddedilmeli bilinmeyen_nitelik "unknown attribute '@kareli'" \
'@kareli
func f() { }
f();'

reddedilmeli nitelik_fonksiyonsuz "may only precede a function declaration" \
'@frame
int x = 5;
print(x);'

reddedilmeli frame_async "@frame is not supported on an async function" \
'@frame
async func f(): int { return 1; }
print(1);'

reddedilmeli frame_ve_no_alloc "@frame and @no_alloc cannot be combined" \
'@frame @no_alloc
func f(): int { return 1; }
print(f());'

reddedilmeli frame_tuple "not yet supported on a tuple-returning function" \
'@frame
func f(): (int, int) { return 1, 2; }
int a, b = f();
print(a + b);'

# ---- bellek ----
if [ -r /proc/self/status ]; then
    cat > "$TMP/bellek.tpr" <<'EOF'
@frame
func is_yap(int i): int {
    str s = "";
    for (int k = 0; k < 64; k++) { s = s + "abcdefghijklmnop"; }
    return len(s) + i;
}
func rss_kb(): int {
    str st = read_file("/proc/self/status");
    int p = indexOf(st, "VmHWM:");
    if (p < 0) { return -1; }
    return toInt(trim(replace(substring(st, p + 6, p + 30), "kB", "")));
}
int toplam = 0;
for (int i = 0; i < 6000; i++) { toplam = toplam + is_yap(i); }
print(toString(toplam) + " " + toString(rss_kb()));
EOF
    sed 's/^@frame$//' "$TMP/bellek.tpr" > "$TMP/bellek_duz.tpr"
    a=$(cd "$TMP" && "$TUL" build bellek.tpr b1.out >/dev/null 2>&1 && ./b1.out)
    b=$(cd "$TMP" && "$TUL" build bellek_duz.tpr b2.out >/dev/null 2>&1 && ./b2.out)
    ra=${a##* }; rb=${b##* }
    if [ "${rb:-x}" = "-1" ] || [ "${ra:-x}" = "-1" ]; then
        echo "  ATLANDI: program /proc/self/status okuyamiyor — bellek kapisi olculmuyor"
    else
    if [ "${a%% *}" = "24141000" ] && [ "${b%% *}" = "24141000" ]; then
        gecti "iki surum ayni sonucu veriyor"
    else dustu "sonuclar: '$a' / '$b'"; fi
    if [ "${rb:-0}" -gt 100000 ]; then gecti "pozitif kontrol: @frame'siz tepe RSS ${rb} kB (buyuyor)"
    else dustu "pozitif kontrol: @frame'siz surum buyumedi (${rb} kB) — kapi bir sey olcmuyor"; fi
    if [ "${ra:-999999999}" -lt 50000 ]; then gecti "@frame'li tepe RSS ${ra} kB (geri sariliyor)"
    else dustu "@frame'li tepe RSS ${ra} kB — kare arenasi geri sarilmiyor"; fi
    fi
else
    echo "  ATLANDI: /proc/self/status yok — bellek kapisi bu platformda olculmuyor"
fi

if [ "$fail" -eq 0 ]; then echo "frame_hatalari: $n_gecti/$n_gecti"; fi
exit $fail
