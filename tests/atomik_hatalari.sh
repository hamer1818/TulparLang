#!/usr/bin/env bash
# ATOMIKLER ve @thread_local (K040, 2026-09-28) — hata yollari + lint.
#
# Derleme hatalari: atomigin ilk argumani ust duzey `int` global olmali;
# bellek sirasi dizgi sabiti ve isleme uygun olmali; @thread_local yalniz
# ust duzey int/float/bool global'e. Lint: `atomic_store` bir YAZMA — ayni
# global'i duz okumak hala uyari; `atomic_load` senkronize okuma — uyari yok;
# @thread_local global paylasilmiyor — uyari yok. Pozitif kontrol: gecerli
# kaynak derlenir ve dogru sonucu basar.
#
#   tests/atomik_hatalari.sh [tulpar_yolu]
set -uo pipefail
export LC_ALL=C
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
    elif ! echo "$out" | grep -qF -- "$bekle"; then
        dustu "$ad: reddedildi ama beklenen mesaj yok: '$bekle'"
        echo "$out" | sed 's/^/         /'
    else
        gecti "$ad"
    fi
}

reddedilmeli yerel_arguman "ilk arguman ust duzey bir \`int\` global olmali" \
'func f(): int { int k = 0; return atomic_add(k, 1); }
print(f());'

reddedilmeli float_global "ilk arguman ust duzey bir \`int\` global olmali" \
'float x = 0.0;
print(atomic_load(x));'

reddedilmeli bilinmeyen_sira "bilinmeyen bellek sirasi" \
'int n = 0;
print(atomic_load(n, "hizli"));'

reddedilmeli okumada_release "okumada gecersiz" \
'int n = 0;
print(atomic_load(n, "release"));'

reddedilmeli yazmada_acquire "yazmada gecersiz" \
'int n = 0;
atomic_store(n, 1, "acquire");
print(n);'

reddedilmeli tl_dizgi "@thread_local currently supports only int/float/bool" \
'@thread_local str s = "a";
print(s);'

reddedilmeli tl_yerel "only meaningful on a TOP-LEVEL global" \
'func f(): int {
    if (true) {
        @thread_local int k = 0;
        return k;
    }
    return 0;
}
print(f());'

reddedilmeli tl_fonksiyon "applies only to a global variable declaration" \
'@thread_local
func f(): int { return 1; }
print(f());'

# ---- lint ----
lint() {   # lint <ad> <beklenen: var|yok> <kaynak>
    local ad="$1" bekle="$2" kaynak="$3"
    printf '%s\n' "$kaynak" > "$TMP/$ad.tpr"
    local out var=yok
    out=$(cd "$TMP" && "$TUL" typecheck "$ad.tpr" 2>&1)
    echo "$out" | grep -qF "in thread worker" && var=var
    echo "$out" | grep -qF "thread iscisi" && var=var
    if [ "$var" = "$bekle" ]; then gecti "$ad: lint uyarisi $bekle"
    else dustu "$ad: lint uyarisi '$var', beklenen '$bekle'"; echo "$out" | sed 's/^/         /'; fi
}
lint store_duz_okuma var \
'int f = 0;
func w(int x): int { atomic_store(f, 1); return 0; }
int t = thread_create(w, 0);
int s = 0;
while (f == 0 && s < 1000) { s = s + 1; }
thread_join(t);'

lint store_atomik_okuma yok \
'int f = 0;
func w(int x): int { atomic_store(f, 1, "release"); return 0; }
int t = thread_create(w, 0);
int s = 0;
while (atomic_load(f, "acquire") == 0 && s < 1000) { s = s + 1; }
thread_join(t);'

lint thread_local_global yok \
'@thread_local int c = 0;
func w(int x): int { c = c + 1; return c; }
int t = thread_create(w, 0);
print(c);
thread_join(t);'

printf '%s\n' 'int n = 0;
func w(int k): int { for (int i = 0; i < k; i++) { atomic_add(n, 1); } return 0; }
int[] th = [];
for (int j = 0; j < 4; j++) { push(th, thread_create(w, 25000)); }
for (int j = 0; j < 4; j++) { thread_join(th[j]); }
print(toString(atomic_load(n)));' > "$TMP/gecerli.tpr"
out=$(cd "$TMP" && "$TUL" build gecerli.tpr gecerli.out 2>&1 && ./gecerli.out 2>&1); rc=$?
if [ "$rc" -eq 0 ] && echo "$out" | grep -q "^100000$"; then gecti "gecerli atomik sayac derlenir ve 100000 sayar (pozitif kontrol)"
else dustu "gecerli atomik sayac: rc=$rc cikti='$out'"; fi

if [ "$fail" -eq 0 ]; then echo "atomik_hatalari: $n_gecti/$n_gecti"; fi
exit $fail
