#!/bin/bash
# STRUCT `var` KACIS ANALIZI — YAPI KAPISI (K064, 2026-09-29).
#
# Anlam testleri tests/struct_kacis.test.tpr'de (kacan her bicim kutulu
# kaliyor mu). Bu betik KARARIN kendisini olcuyor: kacmayan `var q = mk(..)`
# icin uretilen kodda (optimizasyon ONCESI IR) nesne ayirmasi
# (`aot_struct_obj_new` — box_native_struct_as_object'in cagrisi, 2026-10-02'ye
# kadar `vm_allocate_object`; ikisi de sayiliyor) OLMAMALI;
# kacan `var` icin OLMALI. Olcum icin her kontrol ayri bir fonksiyon.
#
# POZITIF KONTROL: TULPAR_NO_STRUCT_ESCAPE=1 analizi kapatir; o derlemede
# kacmayan fonksiyon da ayirma yapmali — gormuyorsak denetim kordur.
# Eski derleyiciyle (analiz yok, olculdu 2026-09-29): 3/4 — asil denetim kirmizi.
set -u
cd "$(dirname "$0")/.."
TULPAR="${1:-./tulpar}"
[ -x "$TULPAR" ] || { echo "struct kacis kapisi: $TULPAR yok — atlandi"; exit 0; }
TULPAR="$(cd "$(dirname "$TULPAR")" && pwd)/$(basename "$TULPAR")"

TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
PASS=0
FAIL=0
ok()  { echo "  gecti  $1"; PASS=$((PASS + 1)); }
bad() { echo "  DUSTU  $1"; FAIL=$((FAIL + 1)); }

cat > "$TMP/k.tpr" <<'EOF'
type P { int x; int y; }
func mk(int a): P { P p = { x: a, y: a + 1 }; return p; }
func kacmaz(int n): int {
    int s = 0;
    for (int i = 0; i < n; i++) {
        var q = mk(i);
        s = s + q.x + q.y;
    }
    return s;
}
func kacar(int n): int {
    int s = 0;
    for (int i = 0; i < n; i++) {
        var q = mk(i);
        var w = q;
        s = s + w.x;
    }
    return s;
}
print(kacmaz(10), kacar(10));
EOF

# `<ad>` fonksiyonunun butun govdeleri (on-optimizasyon IR'i): yerel ABI'li
# `@<ad>(`, kutulu `@t_<ad>(` ve deger ABI'li ikizi `@t_<ad>.f(`.
fn_body() {
  awk -v f="$2" '
    /^define/ { on = ($0 ~ ("@(t_)?" f "(\\.f)?\\(")) }
    on { print }
    on && /^}/ { on = 0 }' "$1"
}

derle() {  # derle <cikti_adi> [ortam...]
  local out="$1"; shift
  (cd "$TMP" && env "$@" TULPAR_AOT_NOCACHE=1 TULPAR_AOT_EMIT_LL_PRE=1 "$TULPAR" build k.tpr "$out" >"$out.log" 2>&1)
}

if ! derle yeni; then
  echo "struct kacis kapisi DUSTU: program derlenemedi"; sed -n '1,10p' "$TMP/yeni.log"; exit 1
fi
out=$("$TMP/yeni")
[ "$out" = "100 45" ] && ok "program dogru sonuc veriyor (100 45)" || bad "program sonucu: '$out' (100 45 bekleniyordu)"

n_kacmaz=$(fn_body "$TMP/yeni.pre.ll" kacmaz | grep -cE 'vm_allocate_object|aot_struct_obj_new')
n_kacar=$(fn_body "$TMP/yeni.pre.ll" kacar | grep -cE 'vm_allocate_object|aot_struct_obj_new')
[ "$n_kacmaz" -eq 0 ] && ok "kacmayan var: nesne ayirmasi yok (tipli yerel)" \
  || bad "kacmayan var: $n_kacmaz nesne ayirmasi (0 bekleniyordu)"
[ "$n_kacar" -gt 0 ] && ok "kacan var (takma ad): kutulu kaliyor ($n_kacar ayirma)" \
  || bad "kacan var: nesne ayirmasi YOK — takma adli var kutusuzlasti (anlam degisir)"

# Pozitif kontrol: analiz kapaliyken kacmayan da ayirmali.
if derle kapali TULPAR_NO_STRUCT_ESCAPE=1; then
  n_kapali=$(fn_body "$TMP/kapali.pre.ll" kacmaz | grep -cE 'vm_allocate_object|aot_struct_obj_new')
  [ "$n_kapali" -gt 0 ] && ok "pozitif kontrol: analiz kapaliyken kacmayan da kutulu ($n_kapali ayirma)" \
    || bad "pozitif kontrol: analiz kapaliyken de ayirma gorulmedi — denetim kor"
else
  bad "pozitif kontrol derlemesi basarisiz"
fi

echo "struct_kacis: $PASS/$((PASS + FAIL))"
[ $FAIL -eq 0 ] || exit 1
exit 0
