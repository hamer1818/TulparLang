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
# TEK KURAL (2026-10-02, ikinci tur): struct alani ve tuple elemani TEKIL
# degerin metniyle yazilir (float en kisa geri donen, bool true/false; f32
# alan okundugu double ile) — `pf` satiri ile `pf.y` satiri ayni float'i
# ayni yaziyor. `"..." + p`, t"{p}" ve sb_append(sb, p) print(p) ile ayni.
# NaN/sonsuz platformdan bagimsiz: "nan" (isaretsiz), "inf", "-inf"; JSON'da
# null. Bu kapi build.sh suites'te uc CI platformunda (Linux x86_64, macOS
# arm64, Windows MinGW) kosuyor — "nan" satirlarinin asil olcumu o: NaN
# calisma aninda uretiliyor ve x86_64'te isaret bitli, AArch64'te isaretsiz.
# Pozitif kontrol (olculdu 2026-10-02): 6d0dc633 derleyicisiyle 37 maddenin
# 17'si DUSTU (struct bool 0/1, alan %g, `"p=" + p` json bicimi, Linux'ta
# "-nan", toJson'da `-nan`/`inf`, `1e400` -> 0, `5e-324` -> 0).
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
struct_dizi|ps|[P { x: 1, y: 2.5, b: false }, P { x: 3, y: 4, b: true }]
struct_eleman|ps[1]|P { x: 3, y: 4, b: true }
struct|q|P { x: 3, y: 4, b: true }
tuple_yerel|t|(3, 1.5)
tuple_cagri|iki()|(3, 1.5)
kutulu_struct|kb|{"name": "ali", "n": 2}
kendini_iceren|kd|[1, [...]]
struct_alan_float|pf|P { x: 1, y: 0.30000000000000004, b: true }
tekil_alan_float|pf.y|0.30000000000000004
tekil_alan_bool|pf.b|true
tuple_bool|uc()|(7, true, 0.25)
f32_alan|tp|Tepe { x: 0.10000000149011612, renk: -3 }
tekil_f32_alan|tp.x|0.10000000149011612
f32_struct_dizi|tps|[Tepe { x: 0.10000000149011612, renk: -3 }]
birlestirme_struct|"p=" + pf|p=P { x: 1, y: 0.30000000000000004, b: true }
tdizgi_struct|t"<{pf}>"|<P { x: 1, y: 0.30000000000000004, b: true }>
nan|n|nan
eksi_nan|-n|nan
inf|1.0 / z|inf
eksi_inf|-1.0 / z|-inf
eksi_sifir|z * -1.0|-0
nan_dizi|[n, -n, 1.0 / z]|[nan, nan, inf]
nan_birlestirme|"n=" + n|n=nan
json_nan|toJson([n, 1.0 / z, 1.5])|[null,null,1.5]
tasan_literal|1e400|inf
alt_normal_literal|5e-324|5e-324
EOF

{
    cat <<'EOF'
type P { int x; float y; bool b; }
type S { str name; int n; }
type Tepe { f32 x; i32 renk; }
func iki(): (int, float) { return 3, 1.5; }
func uc(): (int, bool, float) { return 7, true, 0.25; }
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
P pf = { x: 1, y: 0.1 + 0.2, b: true };
Tepe tp = { x: 0.1, renk: -3 };
Tepe[] tps = [];
push(tps, tp);
// NaN CALISMA ANINDA uretilsin (sabit katlanmasin): x86_64'te 0/0 ISARET
// BITLI NaN verir (glibc "-nan"), AArch64'te isaretsiz — eski derleyicide
// Linux "-nan", macOS "nan" basiyordu. clock_ms() * 0.0 katlanamaz.
float z = clock_ms() * 0.0;
float n = z / z;
EOF
    while IFS='|' read -r ad ifade _; do
        printf 'print(%s);\nprint(toString(%s));\n' "$ifade" "$ifade"
    done < "$DEGERLER"
    # Cok argumanli print: her arguman ayni bicimleyiciden, aralarda bosluk.
    printf 'print("ia:", ia, t);\n'
    # Uzun dizi: print yigindaki 256 baytlik tamponu bircok kez bosaltiyor.
    printf 'int[] ba = [];\nfor (int i = 0; i < 300; i++) { push(ba, i); }\nprint(ba);\n'
    # sb_append(sb, <struct>): print(<struct>) ile ayni metin.
    printf 'int sb = StringBuilder(16);\nsb_append(sb, pf);\nprint(sb_tostring(sb));\n'
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

sbs=$(sed -n "$((2 * n + 3))p" "$TMP/cikti.txt")
sbs_bekle="P { x: 1, y: 0.30000000000000004, b: true }"
if [ "$sbs" = "$sbs_bekle" ]; then gecti "sb_append(sb, <struct>): $sbs"
else dustu "sb_append(sb, <struct>): '$sbs', beklenen '$sbs_bekle'"; fi

if [ $fail -ne 0 ]; then
    echo "--- program ciktisi ---"; sed 's/^/    /' "$TMP/cikti.txt"
    exit 1
fi
exit 0
