#!/usr/bin/env bash
# TameEngine'i İNDİRİLEBİLİR bir pakete koyar.
#
# NİYE PAKET, NİYE ÇIPLAK İKİLİ DEĞİL: editörün varlık gezginleri yolları
# ÇALIŞMA DİZİNİNE göre gezinir —
#   `examples/assets/dokular/*.png`  (doku listesi)
#   `examples/assets/sesler/*.wav`   (ses listesi)
#   `varliklar/*.glb`                (model listesi)
# ve şablonlar (`_tpl_doku3`/`_tpl_ses3`) aynı yolları kullanır. Tek başına
# indirilen bir ikili bu dosyaları bulamaz: doku/ses listeleri boş çıkar ve
# şablonlar dokusuz kurulur. Paket o düzeni koruyor, kullanıcı da klasörün
# İÇİNDEN çalıştırıyor.
#
#   tools/package_tameengine.sh linux-x64 [cikti_dizini]
#
# Çıktı: <cikti>/TameEngine-<platform>.tar.gz
set -euo pipefail

PLATFORM="${1:?kullanim: package_tameengine.sh <linux-x64|macos-universal> [cikti_dizini]}"
OUTDIR="${2:-dist}"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

[ -x ./tulpar ] || { echo "HATA: ./tulpar yok — once derleyin." >&2; exit 1; }

NAME="TameEngine-${PLATFORM}"
STAGE="$(mktemp -d)"
BUNDLE="$STAGE/$NAME"
trap 'rm -rf "$STAGE"' EXIT
mkdir -p "$BUNDLE"

echo "[paket] editor derleniyor..."
# Çıktı YUTULMUYOR, saklanıyor: başarıda sessiz kalması iyi ama başarısızlıkta
# derleyici/linker'ın söylediği tek şey o. İlk denemede `>/dev/null` yüzünden
# macOS'taki link hatası CI günlüğünde HİÇ görünmedi ve bir tur boşa gitti —
# tanıyı gizleyen her yönlendirme aynı bedeli ödetiyor.
build_log="$STAGE/build.log"
if ! DISPLAY= ./tulpar build examples/scene3d_editor.tpr "$BUNDLE/TameEngine" \
        > "$build_log" 2>&1; then
    echo "HATA: editor derlenemedi — derleyici ciktisi:" >&2
    tail -30 "$build_log" >&2
    exit 1
fi
if [ ! -f "$BUNDLE/TameEngine" ]; then
    echo "HATA: editor ikilisi uretilemedi — derleyici ciktisi:" >&2
    tail -30 "$build_log" >&2
    exit 1
fi
chmod +x "$BUNDLE/TameEngine"
# AOT ara ciktilarini SIL. `tulpar build` nesne (.o) ve bazen IR (.ll)
# dosyasini ciktinin yanina birakiyor; ilk pakette `TameEngine.o` kullanicinin
# eline gecmisti — 4 MB'lik anlamsiz bir dosya ve "bu ne?" sorusu.
rm -f "$BUNDLE"/*.o "$BUNDLE"/*.ll

echo "[paket] varliklar kopyalaniyor..."
# Yollar KORUNUYOR — editör bu adları globluyor, yeniden düzenlemek listeleri
# boşaltır.
mkdir -p "$BUNDLE/examples/assets"
cp -r examples/assets/dokular "$BUNDLE/examples/assets/"
cp -r examples/assets/sesler  "$BUNDLE/examples/assets/"
# Model gezgini `varliklar/` diye ayrı bir kök kullanıyor (doku/sesle
# tutarsız, ama motor sözleşmesi bu). Depoda o dizin yok; paket, gezgin boş
# çıkmasın diye örnek modeli oraya koyuyor.
mkdir -p "$BUNDLE/varliklar"
cp examples/assets/robot.glb "$BUNDLE/varliklar/"
# Örnek sahneler: editör dosya argümanı alıyor (`./TameEngine sahne.json`).
if [ -d examples/scenes ]; then
    mkdir -p "$BUNDLE/sahneler"
    cp examples/scenes/*.scene.json "$BUNDLE/sahneler/" 2>/dev/null || true
fi

cat > "$BUNDLE/OKUBENI.txt" <<'TXT'
TameEngine — TulparLang 3B sahne editörü
========================================

ÇALIŞTIRMA — bu klasörün İÇİNDEN:

    ./TameEngine                    boş sahne
    ./TameEngine sahneler/toplayici.scene.json     örnek sahne

Klasörün içinden çalıştırmak ŞART: editörün doku/ses/model listeleri
dosyaları çalışma dizinine göre arar (examples/assets/…, varliklar/…).
Başka bir yerden çalıştırırsan editör açılır ama o listeler boş görünür.

İÇİNDE NE VAR
    TameEngine              editör (tek dosya, kurulum yok)
    examples/assets/dokular  doku paleti (.png)
    examples/assets/sesler   ses paleti (.wav)
    varliklar/               model paleti (.glb)
    sahneler/                örnek sahneler (.json)

KISAYOLLAR: editörün İÇİNDE — H tuşu ya da menüdeki "?" düğmesi.

YAZI TİPİ: sistem fontlarından biri otomatik bulunur. Bulunamazsa yanına
"ui.ttf" adında bir TrueType dosyası koyman yeterli.

SAHNENİ OYUNA ÇEVİRMEK: editörde kurduğun sahneyi JSON olarak kaydet, sonra
TulparLang derleyicisiyle çalıştırılabilir Tulpar koduna dök:

    tulpar examples/scene3d_export.tpr sahnem.json > kur.tpr

Derleyici: https://github.com/hamer1818/TulparLang/releases
Belgeler:  https://tulparlang.dev/games/editor
TXT

# ---- Paketin KENDİSİ sınanıyor -------------------------------------------
# "tar oluştu" bir şey kanıtlamıyor. Kullanıcının göreceği şey editörün
# listelerinin dolu olması, o yüzden dosyaların VARLIĞI burada denetleniyor;
# eksikse paket ÜRETİLMİYOR (kullanıcının eline bozuk paket geçmesin).
echo "[paket] icerik denetleniyor..."
fail=0
for d in "examples/assets/dokular:png" "examples/assets/sesler:wav"; do
    dir="${d%%:*}"; ext="${d##*:}"
    n=$(find "$BUNDLE/$dir" -name "*.$ext" 2>/dev/null | wc -l)
    if [ "$n" -eq 0 ]; then echo "HATA: $dir bos (.$ext yok)" >&2; fail=1
    else echo "  $dir: $n dosya"; fi
done
[ -s "$BUNDLE/OKUBENI.txt" ] || { echo "HATA: OKUBENI.txt yok" >&2; fail=1; }
[ -f "$BUNDLE/varliklar/robot.glb" ] || { echo "HATA: ornek model yok" >&2; fail=1; }
# ARA CIKTI SIZINTISI: kullanicinin paketinde derleme artigi olmamali.
stray=$(find "$BUNDLE" \( -name "*.o" -o -name "*.ll" -o -name "*.obj" \) | head -5)
if [ -n "$stray" ]; then
    echo "HATA: pakete derleme artigi sizmis:" >&2; echo "$stray" >&2; fail=1
fi
[ "$fail" -eq 0 ] || exit 1

# Başlatma dumanı YALNIZ Linux'ta: pencere açmadan (DISPLAY boş) çalıştırıp
# zarif çıkışı görüyoruz — bu, ikilinin GERÇEKTEN linklendiğini ve açıldığını
# kanıtlıyor. macOS'ta ATLANIYOR: orada DISPLAY yok, program pencere açmayı
# dener ve başsız koşucuda ASILABİLİR.
if [ "${PLATFORM#linux}" != "$PLATFORM" ]; then
    echo "[paket] baslatma dumani (penceresiz)..."
    out=$(cd "$BUNDLE" && DISPLAY= WAYLAND_DISPLAY= timeout 20 ./TameEngine 2>&1 || true)
    if echo "$out" | grep -qiE "Pencere acilamadi|could not be opened"; then
        echo "  ikili aciliyor (grafik ortami yok, zarif cikis)"
    else
        echo "HATA: ikili beklenen sekilde baslamadi:" >&2
        echo "$out" | tail -5 >&2
        exit 1
    fi
fi

mkdir -p "$OUTDIR"
tar -czf "$OUTDIR/$NAME.tar.gz" -C "$STAGE" "$NAME"
echo "[paket] hazir: $OUTDIR/$NAME.tar.gz ($(du -h "$OUTDIR/$NAME.tar.gz" | cut -f1))"
