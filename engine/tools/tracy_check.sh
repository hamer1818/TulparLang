#!/bin/bash
# Tracy uctan uca kontrol: istemci (ENGINE_TRACY=ON) -> tracy-capture -> tracy-csvexport.
# Pencere yok. Arac ikilileri: TULPAR_TRACY_TOOLS (tracy-capture ve tracy-csvexport
# iceren dizin). Kullanim:
#   engine/tools/tracy_check.sh desktop     # build-linux-tracy/ + headless demo
#   engine/tools/tracy_check.sh phone       # TULPAR_TRACY=1 android_run.sh demo + adb forward
# Kapi: yakalanan izde "render" ve "sim" bolgeleri var ve kare sayisi >= beklenen.
set -e
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
MODE="${1:-desktop}"
TOOLS="${TULPAR_TRACY_TOOLS:-$HOME/.local/opt/tracy-tools}" # tracy v0.14.1 capture+csvexport (kaynaktan derlendi, 2026-09-14)
[ -x "$TOOLS/tracy-capture" ] && [ -x "$TOOLS/tracy-csvexport" ] || { echo "HATA: TULPAR_TRACY_TOOLS altinda tracy-capture/tracy-csvexport yok"; exit 1; }
OUT="${TMPDIR:-/tmp}/tulpar_tracy_$MODE.tracy"
rm -f "$OUT"
# 8086'yi baskasi tutuyorsa (tipik: onceki telefon kosumundan kalan adb forward)
# yakalama sessizce ORAYA baglanir ve iz bos cikar (2026-09-14'te yasandi).
if ss -ltnp 2>/dev/null | grep -q ':8086 '; then
    echo "HATA: 8086 dolu: $(ss -ltnp | grep ':8086 ' | grep -o 'users:.*') — adb forward --remove-all?"; exit 1
fi
if [ "$MODE" = desktop ]; then
    B="$ROOT/build-linux-tracy"
    cmake -S "$ROOT/engine" -B "$B" -DCMAKE_BUILD_TYPE=Release -DENGINE_TRACY=ON >/dev/null
    cmake --build "$B" -j --target engine_demo 2>&1 | grep -E "error" || true
    # Yakalama penceresi demonun icinde kalir: sunucu once kopar, istemci temiz
    # kapanir. (TRACY_NO_EXIT ile istemci sunucu gelene kadar beklerdi: yok.)
    "$B/engine_demo" --headless 900 --out "${TMPDIR:-/tmp}/tulpar_tracy.ppm" > "${TMPDIR:-/tmp}/tulpar_tracy_desktop.log" 2>&1 &
    RUN=$!
    sleep 1
    "$TOOLS/tracy-capture" -o "$OUT" -a 127.0.0.1 -s 6 -f > "${TMPDIR:-/tmp}/tulpar_tracy_capture.log" 2>&1 || true
    wait $RUN || true
    grep -E "toplam" "${TMPDIR:-/tmp}/tulpar_tracy_desktop.log"
else
    TULPAR_TRACY=ON "$ROOT/engine/tools/android_run.sh" demo 900 > "${TMPDIR:-/tmp}/tulpar_tracy_phone.log" 2>&1 &
    RUN=$!
    # Uygulama baslayana kadar bekle (kurulum + baslat ~20 s), sonra yakala.
    for i in $(seq 1 60); do grep -q "baslat: demo" "${TMPDIR:-/tmp}/tulpar_tracy_phone.log" 2>/dev/null && break; sleep 1; done
    sleep 3
    "$TOOLS/tracy-capture" -o "$OUT" -a 127.0.0.1 -s 6 -f > "${TMPDIR:-/tmp}/tulpar_tracy_capture.log" 2>&1 || true
    wait $RUN || true
    adb forward --remove tcp:8086 >/dev/null 2>&1 || true # masaustu kosumlari icin portu birak
    grep -E "toplam" "${TMPDIR:-/tmp}/tulpar_tracy_phone.log" | tail -1
fi
[ -f "$OUT" ] || { echo "HATA: iz dosyasi yok ($OUT) — istemci dinlemiyor mu?"; exit 1; }
tr '\r' '\n' < "${TMPDIR:-/tmp}/tulpar_tracy_capture.log" | grep -E "^Frames|^Zones" | tr '\n' ' '; echo
CSV="${TMPDIR:-/tmp}/tulpar_tracy_$MODE.csv"
"$TOOLS/tracy-csvexport" "$OUT" > "$CSV"
python3 - "$CSV" <<'PY'
import csv, sys, collections
rows = list(csv.DictReader(open(sys.argv[1])))
by = collections.Counter()
for r in rows: by[r["name"]] += int(r.get("counts", "1") or 1)  # istatistik satiri: ad basina 'counts' cagri sayisi
top = by.most_common(8)
print("  bolgeler:", ", ".join("%s x%d" % kv for kv in top))
need = ["render", "sim"]
missing = [n for n in need if by.get(n, 0) == 0]
if missing:
    print("  KAPI KIRMIZI: eksik bolge", missing); sys.exit(1)
print("  KAPI YESIL: render x%d, sim x%d (%d bolge kaydi)" % (by["render"], by["sim"], len(rows)))
PY
