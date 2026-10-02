#!/usr/bin/env bash
# DEGERIN METNI: print == toString kapisi (2026-10-02, Tuzaklar 7j).
#
# tests/deger_metni.test.tpr toString'i surec ICINDEN sinar; print'in ciktisi
# ise ancak surec DISINDAN okunabilir. Bu kapi her deger icin iki satir basan
# bir program derler — `print(x)` ve `print(toString(x))` — ve olcer:
#
#   1. print'in metni BEKLENEN metin (dizi, json, struct dizisi, tuple...).
#   2. print ile toString AYNI metni veriyor (iki yol tek bicimleyiciden).
#
# 2026-10-02'ye kadar print dizide "<array>", struct dizisinde "<obj>", json
# nesnesinde "<object>", tuple'da `__tup_int_float { _0: 3, _1: 1.5 }`
# basiyordu ve toString yine baska bir sey donduruyordu. Pozitif kontrol
# (olculdu 2026-10-02): 9dddaf38 derleyicisiyle bu kapi 17 maddenin 15'inde
# DUSTU (yalniz tekil struct satirlari gecti — onlar zaten dogruydu).
#
#   tests/deger_metni.sh [tulpar_yolu]
set -uo pipefail
TUL="${1:-./tulpar}"
[ -x "$TUL" ] || { echo "HATA: '$TUL' calistirilabilir degil" >&2; exit 1; }
TUL="$(cd "$(dirname "$TUL")" && pwd)/$(basename "$TUL")"
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
export LC_ALL=C
fail=0
gecti() { printf "  \033[0;32mgecti\033[0m  %s\n" "$1"; }
dustu() { printf "  \033[0;31mDUSTU\033[0m  %s\n" "$1"; fail=1; }

# Her deger: ad | ifade | beklenen metin. Ifade programda iki kez yazilir.
DEGERLER="$TMP/degerler.txt"
cat > "$DEGERLER" <<'EOF'
int_dizi|ia|[1, 2, 3]
genis_int_dizi|wa|[5000000000, -2]
bos_dizi|ea|[]
float_dizi|fa|[1.5, 2, 0.1]
dolu_float_dizi|fd|[0.25, 0.25]
str_dizi|sa|["x", "y"]
karisik|ma|[1, "a", [2, 3], 4.5, true, null]
json_nesne|jo|{"k": [1, 2], "s": "v"}
struct_dizi|ps|[P { x: 1, y: 2.5, b: 0 }, P { x: 3, y: 4, b: 1 }]
struct_eleman|ps[1]|P { x: 3, y: 4, b: 1 }
struct|q|P { x: 3, y: 4, b: 1 }
tuple_yerel|t|(3, 1.5)
tuple_cagri|iki()|(3, 1.5)
kutulu_struct|kb|{"name": "ali", "n": 2}
kendini_iceren|kd|[1, [...]]
EOF

{
    cat <<'EOF'
type P { int x; float y; bool b; }
type S { str name; int n; }
func iki(): (int, float) { return 3, 1.5; }
int[] ia = [1, 2, 3];
int[] wa = [1, -2];
wa[0] = 5000000000;
int[] ea = [];
float[] fa = [1.5, 2.0, 0.1];
float[] fd = array_fill(2, 0.25);
str[] sa = ["x", "y"];
var ma = [1, "a", [2, 3], 4.5, true, null];
json jo = {"k": [1, 2], "s": "v"};
P[] ps = [];
push(ps, { x: 1, y: 2.5, b: false });
P q = { x: 3, y: 4.0, b: true };
push(ps, q);
var t = iki();
S kb = { name: "ali", n: 2 };
array kd = [1];
push(kd, kd);
EOF
    while IFS='|' read -r ad ifade _; do
        printf 'print(%s);\nprint(toString(%s));\n' "$ifade" "$ifade"
    done < "$DEGERLER"
    # Cok argumanli print: her arguman ayni bicimleyiciden, aralarda bosluk.
    printf 'print("ia:", ia, t);\n'
    # Uzun dizi: print yigindaki 256 baytlik tamponu bircok kez bosaltiyor.
    printf 'int[] ba = [];\nfor (int i = 0; i < 300; i++) { push(ba, i); }\nprint(ba);\n'
} > "$TMP/p.tpr"

if ! (cd "$TMP" && TULPAR_AOT_NOCACHE=1 "$TUL" build p.tpr p.out > derle.log 2>&1); then
    dustu "program derlenemedi"; sed 's/^/         /' "$TMP/derle.log"; exit 1
fi
# Windows (MinGW) metin kipinde satir sonu \r\n: karsilastirmadan once at.
(cd "$TMP" && ./p.out) 2>&1 | tr -d '\r' > "$TMP/cikti.txt"

n=0
while IFS='|' read -r ad ifade bekle; do
    n=$((n + 1))
    p=$(sed -n "$((2 * n - 1))p" "$TMP/cikti.txt")
    s=$(sed -n "$((2 * n))p" "$TMP/cikti.txt")
    if [ "$p" != "$bekle" ]; then
        dustu "$ad: print(${ifade}) = '$p', beklenen '$bekle'"
    elif [ "$s" != "$p" ]; then
        dustu "$ad: toString(${ifade}) = '$s', print '$p' — iki yol ayrisiyor"
    else
        gecti "$ad: $p"
    fi
done < "$DEGERLER"

son=$(sed -n "$((2 * n + 1))p" "$TMP/cikti.txt")
if [ "$son" = "ia: [1, 2, 3] (3, 1.5)" ]; then gecti "cok argumanli print: $son"
else dustu "cok argumanli print: '$son', beklenen 'ia: [1, 2, 3] (3, 1.5)'"; fi

uzun=$(sed -n "$((2 * n + 2))p" "$TMP/cikti.txt")
uzun_bekle="[0"; for ((i = 1; i < 300; i++)); do uzun_bekle+=", $i"; done; uzun_bekle+="]"
if [ "$uzun" = "$uzun_bekle" ]; then gecti "uzun dizi (300 eleman, ${#uzun} karakter)"
else dustu "uzun dizi: ${#uzun} karakter, beklenen ${#uzun_bekle}"; fi

if [ $fail -ne 0 ]; then
    echo "--- program ciktisi ---"; sed 's/^/    /' "$TMP/cikti.txt"
    exit 1
fi
exit 0
