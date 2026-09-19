#!/usr/bin/env bash
# Motor köprüsü bindinglerini SENKRONLAR: motor deposundaki SPEC tablosundan
# bu depodaki dört üretilmiş dosyayı yeniden yazar.
#
# NEDEN BU BETİK VAR
# ------------------
# `eng_*` ailesinin tek kaynağı motorun içindeki
# `engine/tools/gen_engine_bindings.py` (SPEC tablosu, bugün 175 builtin).
# Ürettiği dört dosya ise DERLEYİCİ deposunda yaşıyor:
#   runtime/engine_bindings.cpp
#   src/aot/engine_builtins_table.inc
#   src/typeinfer/engine_builtins_sigs.inc
#   src/lsp/engine_builtins.inc
# Tek ağaçken sapma imkânsızdı. Motor ayrı depoya/submodule'e taşınınca iki
# taraf BAĞIMSIZ hareket etmeye başladı: motorda bir imza değişir, buradaki
# üretilmiş dosyalar tazelenmez ve derleyici ESKİ imzayla derler. "Üreteci
# çalıştırmayı unutma" bir kural değil bir dilektir; bu betik onu tek komuta
# indiriyor, kapı (tests/engine_bindings_freshness.py) unutulduğunda kırmızı
# veriyor.
#
# KULLANIM
#   tools/engine_bindings_sync.sh            # dört dosyayı TAZELE
#   tools/engine_bindings_sync.sh --kontrol  # YAZMA, yalnız farkı bildir (çıkış 1)
#   TULPAR_ENGINE_DIR=/yol/tulpar-engine tools/engine_bindings_sync.sh
#
# ÜRETEÇ HAKKINDA BİLİNMESİ GEREKENLER (okundu, varsayım değil)
#   • Çıktı kökünü `sys.argv[1]` olarak alıyor; verilmezse kendi konumundan
#     ÜÇ dizin yukarısını kök sayıyor. O varsayılan yalnız motor `<depo>/engine`
#     altındayken doğru — motor başka yere klonlanırsa sessizce YANLIŞ ağaca
#     yazar. Bu yüzden kökü DAİMA açıkça veriyoruz.
#   • `write()` yazdığı yolu `os.path.relpath(path)` ile basıyor; bu CWD'ye
#     görelidir ve Windows'ta CWD ile hedef AYRI SÜRÜCÜDEYSE `ValueError`
#     fırlatır. Bu yüzden üreteci hedef kökün İÇİNDE çalıştırıyoruz (cd "$ROOT").
#   • Bağımlılığı yok (yalnız os/sys), yani motor ağacı klonluysa çalışır.
#
# KONTROL KİPİ NEDEN KAPIYA DEVREDİYOR
# ------------------------------------
# `--kontrol` karşılaştırmayı kendisi yapmıyor, `tests/engine_bindings_freshness.py`
# kapısını çağırıyor. Sebep bu betiğin varlık sebebinin aynısı: karşılaştırma
# İKİ yerde yazılırsa geliştiricinin gördüğü sonuçla CI'ın gördüğü sonuç
# ayrışabilir. Tek uygulama, iki giriş kapısı.
set -u

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
ENGINE_DIR="${TULPAR_ENGINE_DIR:-$ROOT/engine}"
GEN="$ENGINE_DIR/tools/gen_engine_bindings.py"
GATE="$ROOT/tests/engine_bindings_freshness.py"

kullanim() {
    echo "kullanim: $0 [--kontrol]"
    echo "  (argumansiz)  motor SPEC'inden dort uretilmis dosyayi yeniden yazar"
    echo "  --kontrol     yazmaz; uretilenle depodakini karsilastirir (fark = cikis 1)"
}

mode="yaz"
while [ $# -gt 0 ]; do
    case "$1" in
        --kontrol | --check) mode="kontrol" ;;
        -h | --help) kullanim; exit 0 ;;
        *) echo "bilinmeyen arguman: $1"; kullanim; exit 2 ;;
    esac
    shift
done

# MOTOR AĞACI ÖNCE DENETLENİR. Klonlanmamış bir submodule BOŞ bir dizin
# bırakır; `-d` tek başına bunu "var" sayar, o yüzden içeriğe de bakıyoruz.
# Bu betik için doğru davranış SESSİZ GEÇMEK DEĞİL: kullanıcı açıkça
# "senkronla" dedi, yapamıyorsak sebebini söyleyip kırmızı veriyoruz.
# (Kapının kuralı farklıdır ve bilerek farklıdır: orada motor yoksa GÖRÜNÜR
# bir ATLAMA var, çünkü derleyici motor olmadan da derlenebilmeli.)
if [ ! -d "$ENGINE_DIR" ] || [ -z "$(ls -A "$ENGINE_DIR" 2> /dev/null)" ]; then
    echo "motor binding senkronu KOSULAMADI: motor agaci yok ya da BOS"
    echo "  Beklenen yer: $ENGINE_DIR"
    echo "  Motor ayri bir depo; SPEC tablosu ve uretec ORADA yasiyor."
    echo "  Klonla:      git submodule update --init --recursive"
    echo "  Baska yerde: TULPAR_ENGINE_DIR=/yol/tulpar-engine $0"
    exit 1
fi
if [ ! -f "$GEN" ]; then
    echo "motor binding senkronu KOSULAMADI: uretec bulunamadi"
    echo "  Beklenen: $GEN"
    echo "  Motor agaci duruyor ama uretecin yolu degismis olabilir."
    echo "  Boyle bir durumda sessiz gecmek tam da gizlemeye calistigimiz sapmadir."
    exit 1
fi

# ARACIN VARLIĞI SEBEBİYLE BİRLİKTE DENETLENİR — bkz. tests/fmt_audit.sh:
# `diff` kurulu değilken denetim her dosyayı "bozuk" ilan edip DOSYALARI
# suçluyordu. Python yoksa suç dosyalarda değil ortamdadır.
PY=""
for aday in python3 python; do
    if command -v "$aday" > /dev/null 2>&1; then PY="$aday"; break; fi
done
if [ -z "$PY" ]; then
    echo "motor binding senkronu KOSULAMADI: python bulunamadi (python3/python)"
    echo "  Kur: MSYS2 -> pacman -S mingw-w64-x86_64-python"
    echo "       Debian/Ubuntu -> apt install python3"
    exit 1
fi

# Kapı da AYNI motor ağacına baksın (aksi halde geliştirici bir ağacı
# senkronlarken kapı başka bir ağacı ölçebilir).
export TULPAR_ENGINE_DIR="$ENGINE_DIR"

if [ "$mode" = "kontrol" ]; then
    if [ ! -f "$GATE" ]; then
        echo "motor binding senkronu KOSULAMADI: kapi betigi yok"
        echo "  Beklenen: $GATE"
        exit 1
    fi
    exec "$PY" "$GATE"
fi

# --- YAZMA KİPİ --------------------------------------------------------------
# Kök AÇIKÇA veriliyor ve CWD kökün kendisi (yukarıdaki üreteç notları).
cd "$ROOT" || exit 1
if ! "$PY" "$GEN" "$ROOT"; then
    echo "motor binding senkronu BASARISIZ: uretec hata verdi"
    exit 1
fi

# YAZDIKTAN SONRA KAPIYI KOŞ. Yazmanın gerçekten TUTTUĞUNU kanıtlar: yanlış
# köke yazma, dosya izni, yarım üretim — hepsi burada görünür. "Komut hata
# vermedi" ile "dosyalar artık doğru" aynı şey değildir.
if [ -f "$GATE" ]; then
    if ! "$PY" "$GATE"; then
        echo "motor binding senkronu BASARISIZ: yazdiktan SONRA bile kapi kirmizi"
        exit 1
    fi
fi

# DEĞİŞENİ BİLDİR — ama `git status` İLE DEĞİL.
#
# Ölçüldü (2026-09-19, bu Windows checkout'u): depoda blobların içinde CRLF
# duruyor ve `core.autocrlf=true` CR'ı index'e girerken kırpıyor; sonuç,
# `git status` bu ağaçta 1737 dosyayı "M" gösteriyor — hiç dokunulmamış
# olanlar dahil (src/main.cpp, src/aot/llvm_backend.cpp...). Böyle bir liste
# "şunları commit'le" demek için KULLANILAMAZ: her koşumda dört dosyayı da
# değişmiş gösterir ve geliştiriciye kırmızıyı görmezden gelmeyi öğretir —
# tam da bu araçların önlemeye çalıştığı şey.
#
# `git diff --ignore-cr-at-eol` satır sonu farkını yok sayar, yani GERÇEKTEN
# içerik değişen dosyaları listeler. git yoksa sessiz kalıyoruz: bilgi
# satırıdır, kapı değil (kapı yukarıda zaten koştu).
if command -v git > /dev/null 2>&1; then
    DEGISEN=$(git -C "$ROOT" diff --ignore-cr-at-eol --name-only -- \
        runtime/engine_bindings.cpp \
        src/aot/engine_builtins_table.inc \
        src/typeinfer/engine_builtins_sigs.inc \
        src/lsp/engine_builtins.inc 2> /dev/null)
    if [ -n "$DEGISEN" ]; then
        echo "motor binding senkronu tamam — SPEC degismis, su dosyalari COMMIT'LE:"
        echo "$DEGISEN" | sed 's/^/    /'
    else
        echo "motor binding senkronu tamam — uretilmis dosyalar zaten guncel."
    fi
else
    echo "motor binding senkronu tamam (git yok: degisen dosya listesi basilamadi)."
fi
