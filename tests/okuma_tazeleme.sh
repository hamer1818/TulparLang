#!/usr/bin/env bash
# OKUMA YAVAS YOLUNDAN SONRA SEKIL ONBELLEGI TAZELENMIYOR (2026-10-06).
#
# Dongunun sekil onbellegi (idata/count/genislik) dongu basinda bir kez
# okunuyor; YAZMA yavas yolu (vm_set_element: genisletme, kutulama) depoyu
# degistirebildigi icin ondan sonra tazeleniyor. OKUMA yavas yolu
# (vm_get_element) da eskiden tazeliyordu — vm_array_get bir zamanlar okurken
# diziyi kutuya cevirip idata'yi FREE ediyordu. O yol kapandi, okuma depoya
# dokunmuyor; tazeleme (her cagri onbellekteki her dizi icin satir ici ~20
# komut) yalniz derleme suresi oduyordu: nbody 342 -> 260 ms.
#
# Kapi:
#   1. ETKI (IR): okuma yavas yolu olan dongude tazeleme cagrisi SAYISI
#      varsayilanda TULPAR_OKUMA_TAZELE=1'den az (iki yon).
#   2. ANLAM: onbellekli dongude okuma yavas yolu (dizgi indeks, kutulu dizi,
#      dizgi hedef) alindiktan sonra hizli okuma/yazma dogru — varsayilan,
#      tazelemeli ve surumsuz derleme ayni, beklenen deger.
#   3. POZITIF KONTROL: ayni bicim YAZMA yavas yoluyla (int depoya float yazmak
#      diziyi kutuya ceviriyor) — TULPAR_YAZMA_TAZELEME_SINAMA=atla yazma
#      tazelemesini atlayinca program YANLIS sonuc / cokme veriyor. Yani test
#      bayat onbellegi gercekten yakaliyor; okuma yolunun tazelemesiz dogru
#      kalmasi bu yuzden anlamli.
#
#   tests/okuma_tazeleme.sh [tulpar_yolu]
set -uo pipefail
TUL="${1:-./tulpar}"
[ -x "$TUL" ] || { echo "HATA: '$TUL' calistirilabilir degil" >&2; exit 1; }
TUL="$(cd "$(dirname "$TUL")" && pwd)/$(basename "$TUL")"
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
export LC_ALL=C
fail=0
n_gecti=0
gecti() { n_gecti=$((n_gecti + 1)); printf "  \033[0;32mgecti\033[0m  %s\n" "$1"; }
dustu() { printf "  \033[0;31mDUSTU\033[0m  %s\n" "$1"; fail=1; }

# Okuma yavas yollari: k dizgi (calisma zamaninda — itag != INT), b kutulu
# dizi, s dizgi hedef. Her biri onbellekli dongunun ICINDE; ardindan a'nin
# hizli okumasi ve yazmasi.
cat > "$TMP/oku.tpr" <<'EOF'
func f(int[] a, int n, var k, var b, var s): int {
    int t = 0;
    for (int i = 0; i < n; i = i + 1) {
        t = t + a[i];
        if (i % 7 == 3) { t = t + a[k] + b[1] + len(s[0]); }
        a[i] = a[i] + 1;
        t = t + a[n - 1 - i];
    }
    return t;
}
int[] a = array_fill(64, 0);
for (int i = 0; i < 64; i = i + 1) { a[i] = i * 3; }
var b = [1, 7, "x"];
var r = 0;
for (int rep = 0; rep < 3; rep = rep + 1) { r = r + f(a, 64, "k", b, "abc"); }
int c = 0;
for (int i = 0; i < 64; i = i + 1) { c = c + a[i]; }
print(r);
print(c);
EOF
# Yazma yavas yolu: int depoya float yazmak diziyi KUTUYA ceviriyor (idata
# serbest). Ardindan onbellekli okumalar.
cat > "$TMP/yaz.tpr" <<'EOF'
func g(int[] a, int n): int {
    int t = 0;
    for (int i = 0; i < n; i = i + 1) {
        if (i == 5) { a[2] = 1.5; }
        t = t + a[i] * 2;
        a[i] = a[i] + 1;
    }
    return t;
}
int[] a = array_fill(4096, 0);
for (int i = 0; i < 4096; i = i + 1) { a[i] = i; }
// Serbest kalan bellek baska ayirmalarla dolsun (bayat okuma cop gorsun).
var tut = [];
print(g(a, 4096));
for (int q = 0; q < 2000; q = q + 1) { push(tut, toString(q * 7919)); }
var c = 0;
for (int i = 0; i < 4096; i = i + 1) { c = c + a[i]; }
print(c);
EOF

derle() {   # derle <kaynak> <cikti> <ek ortam>
    rm -f "$TMP/$2" "$TMP/$2.pre.ll"
    (cd "$TMP" && env $3 TULPAR_AOT_NOCACHE=1 TULPAR_AOT_EMIT_LL_PRE=1 "$TUL" build "$1.tpr" "$2" >/dev/null 2>&1)
}
kos() { (cd "$TMP" && ./"$1" 2>&1 | tr -d '\r'); }

# 1. ETKI
derle oku oku_yeni "OT_X=1"
derle oku oku_eski "TULPAR_OKUMA_TAZELE=1"
n_yeni=$(grep -c 'call void @tulpar.shape_refill' "$TMP/oku_yeni.pre.ll" 2>/dev/null)
n_eski=$(grep -c 'call void @tulpar.shape_refill' "$TMP/oku_eski.pre.ll" 2>/dev/null)
if [ -n "$n_yeni" ] && [ -n "$n_eski" ] && [ "$n_eski" -gt "$n_yeni" ]; then
    gecti "IR: tazeleme cagrisi $n_eski -> $n_yeni (TULPAR_OKUMA_TAZELE=1 eski sayiyi geri getiriyor)"
else dustu "IR: tazeleme sayisi varsayilan '$n_yeni', TULPAR_OKUMA_TAZELE=1 '$n_eski' — fark yok"; fi

# 2. ANLAM
derle oku oku_surumsuz "TULPAR_NO_IVER=1 TULPAR_NO_FVER=1"
o1=$(kos oku_yeni); o2=$(kos oku_eski); o3=$(kos oku_surumsuz)
beklenen=$'36984\n6240'   # Python ile bagimsiz hesaplandi
if [ "$o1" = "$beklenen" ] && [ "$o2" = "$beklenen" ] && [ "$o3" = "$beklenen" ]; then
    gecti "okuma yavas yolu sonrasi hizli erisim dogru (varsayilan / tazelemeli / surumsuz ayni)"
else dustu "okuma: varsayilan '$o1' / tazelemeli '$o2' / surumsuz '$o3' (beklenen '$beklenen')"; fi

# 3. POZITIF KONTROL
derle yaz yaz_dogru "OT_X=1"
derle yaz yaz_bayat "TULPAR_YAZMA_TAZELEME_SINAMA=atla"
y1=$(kos yaz_dogru); y2=$(kos yaz_bayat)
if [ -n "$y1" ] && [ "$y1" != "$y2" ]; then
    gecti "pozitif kontrol: yazma tazelemesi atlaninca sonuc bozuluyor ('$(echo $y1)' -> '$(echo $y2 | cut -c1-40)')"
else dustu "pozitif kontrol: yazma tazelemesiz derleme de ayni sonucu verdi ('$y1') — test bayat onbellegi olcmuyor"; fi

if [ "$fail" -eq 0 ]; then echo "okuma_tazeleme: $n_gecti/$n_gecti"; fi
exit $fail
