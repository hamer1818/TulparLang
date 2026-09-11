#!/bin/bash
# ============================================
# TulparLang Build Script (LLVM Backend)
# Version 2.1.0
# ============================================

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

echo -e "${BLUE}========================================${NC}"
echo -e "${BLUE}TulparLang Build Script (LLVM Backend)${NC}"
echo -e "${BLUE}========================================${NC}"
echo ""

# Detect OS
OS="$(uname -s)"
case "${OS}" in
    Linux*)     PLATFORM=Linux;;
    Darwin*)    PLATFORM=macOS;;
    MINGW*|MSYS*|CYGWIN*)    PLATFORM=Windows;;
    *)          PLATFORM="UNKNOWN:${OS}"
esac
echo -e "${YELLOW}Platform: ${PLATFORM}${NC}"

# Parse arguments
ACTION="$1"

# --- Donanım kaynak göstergesi ----------------------------------------------
# Uzun koşumların NEYE MAL OLDUĞUNU gösterir: RAM zirvesi, CPU frekansı ve
# sıcaklığı, GPU kullanımı. Sondaların hepsi tools/hwstat.sh içinde ayrı ayrı
# korumalı — eksik arayüz sessizce atlanıyor, koşum bundan dolayı ASLA
# düşmüyor (CI'da lm-sensors da nvidia-smi de yok).
# TULPAR_NO_HWSTAT=1 tamamen susturur.
# MUTLAK yol: derleme fazı `cd "$BUILD_DIR"` yaptıktan sonra da çağırıyor,
# göreli yol orada çözülmezdi.
HWSTAT="$(cd "$(dirname "$0")" && pwd)/tools/hwstat.sh"
HW_PID=""
HW_FILE=""

hw_begin() {
    [ -x "$HWSTAT" ] || return 0
    "$HWSTAT" info
    HW_FILE=$(mktemp 2>/dev/null) || return 0
    HW_PID=$("$HWSTAT" start "$HW_FILE" 2>/dev/null)
}

hw_end() {
    [ -x "$HWSTAT" ] || return 0
    [ -n "$HW_PID" ] || return 0
    "$HWSTAT" stop "$HW_PID" "$HW_FILE" "${1:-kosum}" 2>/dev/null
    HW_PID=""
}
TARGET="$2"

# Single platform-suffixed build directory (mirrors build.bat behaviour:
# contents are wiped on every build so a stale runtime archive never lingers).
case "${OS}" in
    Linux*)     BUILD_DIR="build-linux";;
    Darwin*)    BUILD_DIR="build-macos";;
    *)          BUILD_DIR="build";;
esac

if [ "$ACTION" = "clean" ]; then
    echo "Cleaning build artifacts..."
    rm -rf "$BUILD_DIR"
    rm -f tulpar a.out *.o *.ll
    echo -e "${GREEN}Clean complete.${NC}"
    exit 0
fi

# ============================================
# Check Dependencies
# ============================================
echo ""
echo "Checking dependencies..."

# Check CMake
if ! command -v cmake &> /dev/null; then
    echo -e "${RED}ERROR: CMake is required.${NC}"
    echo "Install with:"
    echo "  Ubuntu/Debian: sudo apt install cmake"
    echo "  macOS:         brew install cmake"
    exit 1
fi
echo -e "  CMake: ${GREEN}OK${NC}"

# Check LLVM
LLVM_CONFIG=""
for cmd in llvm-config llvm-config-18 llvm-config-17 llvm-config-16; do
    if command -v $cmd &> /dev/null; then
        LLVM_CONFIG=$cmd
        break
    fi
done

if [ -z "$LLVM_CONFIG" ]; then
    echo -e "${RED}ERROR: LLVM is required.${NC}"
    echo ""
    echo "Install LLVM with:"
    echo "  Ubuntu/Debian: sudo apt install llvm-18-dev"
    echo "  macOS:         brew install llvm@18"
    echo "  Fedora:        sudo dnf install llvm-devel"
    echo ""
    echo "After installation, ensure llvm-config is in PATH."
    exit 1
fi

LLVM_VERSION=$($LLVM_CONFIG --version)
echo -e "  LLVM:  ${GREEN}OK${NC} (version $LLVM_VERSION)"

# Check compiler
if command -v gcc &> /dev/null; then
    echo -e "  GCC:   ${GREEN}OK${NC}"
elif command -v clang &> /dev/null; then
    echo -e "  Clang: ${GREEN}OK${NC}"
else
    echo -e "${RED}ERROR: No C compiler found.${NC}"
    exit 1
fi

echo ""

# ============================================
# Build
# ============================================
# ---------------------------------------------------------------------------
# ./build.sh suites — tests/*.test.tpr regresyon paketlerini koşar.
#
# `./build.sh test` YALNIZ examples/ üzerinde dolaşır ve orada tek ölçüt çıkış
# kodudur. tests/ altındaki gömülü `test` kitaplığını kullanan paketler
# (jest tarzı assert'ler) uzun süre HİÇBİR otomasyonda koşmadı — CI'da da,
# build.sh'de de. 2026-08-04'te `assert` fonksiyonunun bool koşullarda hiçbir
# zaman başarısız olmadığı ortaya çıktı; hata tam olarak bu körlükte yaşadı,
# çünkü paketler elle koşulduğunda yeşil görünüyordu ve hiçbir şey onları
# sürekli doğrulamıyordu. Bu hedef o boşluğu kapatıyor.
#
# Ölçüt çıkış kodu: test_summary() başarısızlıkta exit(1) çağırıyor. Özet
# çağırmayan bir paket ASLA kırmızı olamaz, o yüzden aşağıda `Tests:` satırı
# olmayan paket de hata sayılıyor — sessizce yutulmasın.
if [ "$ACTION" = "suites" ]; then
    if [ ! -x "./tulpar" ]; then
        echo -e "${RED}ERROR: ./tulpar yok — önce ./build.sh çalıştırın.${NC}"
        exit 1
    fi
    # `timeout` GNU coreutils'te; macOS'ta YOK (orada brew ile `gtimeout`).
    # Bu satir uzun sure ciplak `timeout 180` idi ve macOS'ta HER paketi
    # "command not found" ile dusururdu — fark edilmedi cunku suites orada
    # hic kosmuyordu. `test` hedefi ayni sorunu zaten cozmustu; ayni kalibi
    # buraya da tasiyoruz. Hicbiri yoksa zaman siniri olmadan kosuyor:
    # CI adim zaman asimi yedek.
    SUITE_TIMEOUT_CMD=""
    if command -v timeout >/dev/null 2>&1; then
        SUITE_TIMEOUT_CMD="timeout 180"
    elif command -v gtimeout >/dev/null 2>&1; then
        SUITE_TIMEOUT_CMD="gtimeout 180"
    fi
    hw_begin
    SUITE_FAILED=0
    SUITE_N=0
    for suite in tests/*.test.tpr; do
        [ -f "$suite" ] || continue
        SUITE_N=$((SUITE_N + 1))
        name=$(basename "$suite")
        out=$(DISPLAY= $SUITE_TIMEOUT_CMD ./tulpar "$suite" 2>&1)
        code=$?
        summary=$(echo "$out" | grep -E '^Tests:' | tail -1)
        if [ $code -ne 0 ]; then
            printf "%-42s ${RED}FAIL${NC} %s\n" "$name" "$summary"
            # ONCE gercek test satirlari. Eski hali `grep -E 'FAIL|hata|error'`
            # idi ve ilk 8 satiri aliyordu: bir kutuphane stderr'e gurultu
            # basinca (ALSA "ses aygiti yok" satirlari gibi) o gurultu 'error'
            # ile eslesip FAIL satirlarini DISARI itiyordu. Olculdu
            # (2026-09-01): CI kirmizi dondu ama HANGI testin dustugu ciktida
            # hic gorunmedi — bir tur bosa gitti. Genel gurultu artik yalniz
            # FAIL satiri HIC yoksa (paket cokmusse) yedek olarak basiliyor.
            #
            # `head` yerine `awk`: `head` erken cikinca yukaridaki grep SIGPIPE
            # aliyor ve ciktiya "write error: Broken pipe" satirlari dusuyordu.
            fails=$(echo "$out" | grep -E '^[[:space:]]*FAIL' | awk 'NR<=10')
            if [ -n "$fails" ]; then
                echo "$fails" | sed 's/^/    /'
            else
                echo "$out" | grep -E 'hata|error|Error' | awk 'NR<=8' | sed 's/^/    /'
            fi
            SUITE_FAILED=1
        elif [ -z "$summary" ]; then
            printf "%-42s ${RED}FAIL${NC} (test_summary() cagirmiyor — cikis kodu uretmiyor)\n" "$name"
            SUITE_FAILED=1
        else
            printf "%-42s ${GREEN}PASS${NC} %s\n" "$name" "$summary"
        fi
    done
    echo ""
    if [ $SUITE_FAILED -ne 0 ]; then
        hw_end "suites"
        echo -e "${RED}Some suites failed!${NC} ($SUITE_N paket)"
        exit 1
    fi
    # Builtin tablosu ↔ codegen ↔ LSP tutarlılık denetimi (derleme gerektirmez).
    if command -v python3 >/dev/null 2>&1; then
        echo ""
        if ! python3 tests/builtin_audit.py; then
            echo -e "${RED}Builtin denetimi basarisiz!${NC}"
            exit 1
        fi
        # Kama mesh'i: rampa artık gerçek bir mesh. GÖZLE doğrulamak pencere
        # açmayı gerektirir (depoda yasak) ve ters sarılmış bir üçgen
        # arkayüz ayıklamasıyla sessizce GÖRÜNMEZ olur — yani hata "hata yok"
        # gibi durur. Denetim üçgenleri C kaynağından okuyup sarma yönünü,
        # kapalılığı ve eğimin fizikle aynı tanımda olduğunu ölçüyor.
        if ! python3 tests/wedge_mesh_check.py; then
            echo -e "${RED}Kama mesh denetimi basarisiz!${NC}"
            exit 1
        fi
        # Önceden derlenmiş web/Android arşivleri ↔ builtin tablosu.
        # Aşağıdaki zaman damgası denetiminin YERİNE geçiyor (o yalnız
        # "kaynak daha yeni" diyebiliyordu); bu, EKSİK SEMBOLLERİ adıyla
        # sayıyor. Ayrım işe yaradı: wasm/dist beş gün bayat kaldı, scene3d'nin
        # her web derlemesi link'te patlıyordu ve sarı satırı kimse okumadı.
        if ! python3 tests/dist_archive_audit.py; then
            echo -e "${RED}Dist arsiv denetimi basarisiz!${NC}"
            exit 1
        fi
        # Dongu-sekli gezicisi ASTNode_C'nin TUM cocuk alanlarini geziyor mu?
        # Bir dal atlanirsa "govdede cagri yok" kaniti delinir ve atlanan
        # dalda duran bir push bellek bozar — suitler yesil kalarak.
        if ! python3 tests/ast_child_fields_audit.py; then
            echo -e "${RED}AST cocuk alani denetimi basarisiz!${NC}"
            exit 1
        fi
        # Sessiz bozulma sondalari: paketler "dogru yazilmis" programlari
        # kosuyor, bu kenar durumlari kosuyor ve ozellikle derleyicinin
        # "basarili" deyip yanlis sonuc urettigi / ikilinin coktugu sinifi
        # ariyor. Iki gercek hatayi boyle bulduk (bkz. dosya basligi).
        if ! python3 tests/silent_failure_probe.py; then
            echo -e "${RED}Sessiz hata sondalari basarisiz!${NC}"
            exit 1
        fi
        # S4 — AKIS SOZLESMESI (uc faz: akis oncesi / akis ortasi / surec).
        # Bu sozlesme iki tur once KODDA kapandi ama hicbir test onu
        # sinamiyordu; retrofit sayimi (#21) borcu yakaladi. Fikstur ham
        # soket kullanir (#22: yorumlayan arac ihlali gizler — curl P48'de
        # tam bunu yapti) ve kendi kirmiziya-donebilirligini tasiyan bir
        # ihlal rotasi (/raw) icerir.
        if ! DISPLAY= WAYLAND_DISPLAY= python3 tests/stream_contract_smoke.py; then
            echo -e "${RED}S4 akis sozlesmesi basarisiz!${NC}"
            exit 1
        fi
        # S3 — ARENA SOZLESMESI (restore birakmaz / drop birakir).
        # Ayni #21 borcu: `arena_restore` uc suitte geciyordu ama hepsi
        # sozlesmenin "hayatta kalir" yarisini sinaniyordu; "serbest
        # birakmaz" yarisi hic sinanmamisti.
        if ! DISPLAY= WAYLAND_DISPLAY= python3 tests/arena_contract_smoke.py; then
            echo -e "${RED}S3 arena sozlesmesi basarisiz!${NC}"
            exit 1
        fi
        # YIGIN SIZINTISI (R11). Dongu govdesine dusen bir `alloca`
        # yinelemede yigin harciyor ve program YETERINCE UZUN dondugunde
        # SIGSEGV veriyor — derleme sessiz, suitler yesil. `AST_ARRAY_LITERAL`
        # tam bunu yapiyordu ve 175 000 yinelemede oluyordu. Sekil basina
        # cikti-mutabakati var: dongu elenirse sekil "olctum" diyemez.
        if ! DISPLAY= WAYLAND_DISPLAY= python3 tests/stack_growth_smoke.py; then
            echo -e "${RED}Yigin sizintisi taramasi basarisiz!${NC}"
            exit 1
        fi
        # TypedValue UC ALANI DA ILKLENDIRILIR (#32 sinifi).
        #
        # `TypedValue sc;` yazmak yasak: `.boxed` yigin copu kalir ve bu
        # dosyada 30 yerde okunuyor. Olculdu (2026-09-11): kisa-devre
        # duzeltmesinde tek bir ilklendirilmemis alan, CI'da 16 ornegin
        # derleyicisini SEGV ettirdi (`01_hello_world` dahil) — ama YERELDE
        # Release/LLVM22'de gorunmuyordu, cunku yigin cogu zaman sifirdi.
        # Ortama bagli sessizlik, bu sinifin imzasi.
        # ⚠ Desen SATIR BASINA BAGLI OLAMAZ. Ilk yazimda `^[[:space:]]*`
        # ile basliyordu ve enjeksiyon testi onu HEMEN kacirdi
        # (`... { TypedValue bozuk; }` tek satirda). Yani koruma, korudugu
        # hatanin kendi turunu yapiyordu; enjeksiyon olmasaydi yesil kalirdi.
        # Yorum satirlari elenir: bu denetimin KENDI aciklama satiri
        # (`TypedValue sc;` ornegi) deseni tetikliyordu — koruma kendi
        # belgesine takiliyordu.
        TV_BARE=$(grep -nE "\bTypedValue[[:space:]]+[a-zA-Z_][a-zA-Z0-9_]*[[:space:]]*;" src/aot/*.cpp \
                  | grep -vE "^[^:]*:[0-9]+:[[:space:]]*(//|\*|/\*)" || true)
        if [ -n "$TV_BARE" ]; then
            echo -e "${RED}ILKLENDIRILMEMIS TypedValue — uc alani da yazin!${NC}"
            echo "$TV_BARE" | head -5 | sed 's/^/  /'
            echo "  dogru kalip: TypedValue x = {nullptr, INFERRED_UNKNOWN, nullptr};"
            exit 1
        fi
        echo -e "${GREEN}TypedValue ilklendirmesi tam${NC}"
        # KORPUS TANI TABANI (#26). Taban SAYI degil METIN tutuyor: bir tani
        # sessizce dogarsa ya da kaybolursa kirmizi verir. Tarayici, is
        # yapmadan once KENDINI siniyor (tani uretmesi kesin bir fikstur
        # uzerinde); goremezse "temiz korpus" demek yerine hata veriyor —
        # P23'un taban olcumunun ilk uc surumu tam bu yuzden yanlis "0 tani"
        # demisti.
        if ! DISPLAY= WAYLAND_DISPLAY= python3 tests/typecheck_corpus_scan.py --check; then
            echo -e "${RED}Korpus tani tabani degisti!${NC}"
            exit 1
        fi
    fi

    # LSP. Editör eklentisinin dayandığı yüzey ve hiçbir otomasyonda yoktu:
    # bozulsa derleyici de süitler de yeşil kalır, yalnız editörde tamamlama
    # ve hover sessizce ölürdü. "İlan et ↔ uygula" denetimi: initialize hangi
    # *Provider'ı bildiriyorsa o metot gerçekten çağrılıp anlamlı cevap
    # verdiği sınanıyor (~0.03 sn).
    if command -v python3 >/dev/null 2>&1 && [ -f tests/lsp_audit.py ]; then
        if ! python3 tests/lsp_audit.py; then
            echo -e "${RED}LSP denetimi basarisiz!${NC}"
            exit 1
        fi
    fi

    # BİÇİMLENDİRİCİ. `tulpar fmt --write` KULLANICININ KAYNAĞINI değiştiriyor,
    # yani buradaki bir hata doğrudan veri kaybı — ve bu denetim yokken üç
    # ayrı bozulma birden hayatta kaldı: `i++` → `i + +`, `=>` → `= >`,
    # `/*` → `/ *` (blok yorumun içi kod gibi işleniyordu). Üçü de
    # DERLENMEYEN kod üretiyordu. ~2 sn.
    if [ -x tests/fmt_audit.sh ]; then
        echo ""
        if ! bash tests/fmt_audit.sh; then
            echo -e "${RED}Bicimlendirici denetimi basarisiz!${NC}"
            exit 1
        fi
    fi

    # BELGE ÜRETECİ. `tulpar doc` da denetimsizdi ve üç stdlib modülü HİÇ
    # belgelenemiyordu (kardeş modüllerin sembollerine baktıkları için tek
    # başlarına derlenmiyorlar; `doc` kodgen hatasında her şeyi atıyordu).
    # ~5 sn.
    if [ -x tests/doc_audit.sh ]; then
        if ! bash tests/doc_audit.sh; then
            echo -e "${RED}Belge denetimi basarisiz!${NC}"
            exit 1
        fi
    fi

    # PAKET YÖNETİCİSİ. Kullanıcının proje dizinine yazıyor (`tulpar.toml`,
    # `tulpar_modules/`). Asıl iddia "dosya kopyalandı" değil, vendor edilen
    # paketin GERÇEKTEN import edilebilmesi. ~0.1 sn.
    if [ -x tests/pkg_audit.sh ]; then
        if ! bash tests/pkg_audit.sh; then
            echo -e "${RED}Paket denetimi basarisiz!${NC}"
            exit 1
        fi
    fi

    # UÇTAN UCA AOT DUMANI: derleyici gerçekten program üretebiliyor mu?
    # "İkili oluştu" ile "ikili çalışıyor" ayrı iddialar; ikincisi macOS'ta
    # uzun süre hiç ölçülmedi ve yayınlanan macOS ikilisiyle hiçbir program
    # derlenemediği aylarca fark edilmedi. ~10 sn, grafiksiz.
    if [ -x tests/aot_smoke.sh ]; then
        if ! bash tests/aot_smoke.sh ./tulpar; then
            echo -e "${RED}AOT dumani basarisiz!${NC}"
            exit 1
        fi
    fi

    # ANDROID DERLEME DENETİMİ. Arşiv sembolleri tamam olsa bile derleme yolu
    # (manifest yazımı, PIC reloc, link bayrakları, NDK bulma) kırık olabilir
    # ve bunu hiçbir şey denetlemiyordu: hedef "Temmuz'da emülatörde
    # doğrulandı" diye duruyordu, arada İKİ ayrı kırık sessizce birikti
    # (bayat arşivler + NDK aramasının Android Studio kurulumunu görmemesi).
    # Masaüstü build'i, 59 süit ve tüm örnekler bu süre boyunca yeşildi.
    #
    # scene3d SEÇİLDİ çünkü tame'i de içeriyor — tek derleme iki arşivi birden
    # sınıyor. NDK yoksa ATLANIYOR: NDK'sı olmayan geliştiriciyi kırmızıya
    # boğmak yanlış olur (dist denetimindeki aynı gerekçe).
    if [ -z "$TULPAR_NO_ANDROID_SMOKE" ] && [ -x ./tulpar ]; then
        ASMOKE=$(mktemp -d)
        ASMOKE_OUT=$(DISPLAY= ./tulpar build --target=android                             examples/scene3d_collector.tpr "$ASMOKE/tulparsmoke" 2>&1)
        ASMOKE_MAN="$ASMOKE/tulparsmoke_apk/AndroidManifest.xml"
        # ÖN KOŞUL EKSİKLİĞİ ile KIRIK DERLEME ayrı şeyler. İkisi de "başarısız"
        # gibi görünüyor ama yalnız ikincisi bir kusur.
        #
        # NDK yoksa zaten atlanıyordu; ARŞİVLER yoksa atlanmıyordu ve CI tam
        # bu yüzden kırmızı döndü (2026-09-01): `android/dist` gitignore'lu,
        # yerelde NDK ile üretiliyor, temiz bir checkout'ta hiç yok. Denetim
        # ortamın eksikliğini kodun kusuru sanıyordu.
        #
        # Atlama SESSİZ DEĞİL: sebebiyle birlikte yazılıyor, yoksa "asla
        # kırmızıya dönemeyen denetim" tuzağına düşerdi.
        if echo "$ASMOKE_OUT" | grep -qE "NDK gerekir|needs the NDK"; then
            echo "android derleme denetimi: NDK yok — atlandi"
        elif echo "$ASMOKE_OUT" | grep -qE "android/dist ars|android/dist archives"; then
            echo "android derleme denetimi: android/dist arsivleri yok — atlandi"
        elif [ -f "$ASMOKE/tulparsmoke_apk/lib/arm64-v8a/libtulpargame.so" ] &&
             [ -f "$ASMOKE/tulparsmoke_apk/lib/x86_64/libtulpargame.so" ] &&
             [ -f "$ASMOKE_MAN" ] &&
             grep -q 'package="dev.tulparlang.tulparsmoke"' "$ASMOKE_MAN"; then
            # Paket kimliği ÇIKTI ADINDAN türüyor. Sabit bir varsayılan
            # (`dev.tulparlang.game`), tulpar.toml yazmayan her oyuna aynı
            # kimliği verir ve cihazda ikinci oyun birinciyi SİLER — sebebi
            # hiçbir yerde yazmadan. Denetim bunu her koşumda ölçüyor.
            echo -e "${GREEN}android derlemesi calisiyor${NC} (iki ABI + manifest + ada gore paket)"
        else
            echo -e "${RED}Android derlemesi basarisiz!${NC}"
            echo "$ASMOKE_OUT" | tail -12
            rm -rf "$ASMOKE"
            exit 1
        fi
        rm -rf "$ASMOKE"
    fi

    # Önceden derlenmiş arşivlerin TAZELİĞİ. Sürücü bunu web/Android link'i
    # sırasında zaten uyarıyor, ama o uyarıyı görmek için o hedefi derlemek
    # gerekiyor — yani arşivler aylarca sessizce çürüyebiliyor (ölçüldü:
    # android/dist 11 Ağustos'tan kalmıştı ve 25-26 Ağustos'ta eklenen ALTI
    # sembolün hiçbirini içermiyordu; o arşivle her Android derlemesi link'te
    # patlardı).
    #
    # Burada yalnız zaman damgası karşılaştırılıyor: hiçbir araç zinciri
    # (emsdk / NDK) gerekmiyor, dolayısıyla her makinede çalışıyor.
    # HATA DEĞİL uyarı: arşiv yoksa o hedef zaten kullanılmıyor demektir ve
    # emsdk'sı olmayan bir geliştiriciyi kırmızıya boğmak yanlış olur.
    #
    # YEDEK YOL: python3 varsa yukarıdaki SEMBOL denetimi zaten koştu ve o
    # daha kesin konuşuyor ("şu 31 fonksiyon yok"). Aynı arşiv için iki ayrı
    # sarı satır basmak uyarıyı gürültüye çevirirdi — tam da bu denetimin
    # kapatmaya çalıştığı hata. Bu blok yalnız python3 yokken devreye giriyor.
    DIST_SRC_NEWEST=""
    if ! command -v python3 >/dev/null 2>&1; then
        DIST_SRC_NEWEST=$(ls -t runtime/tame_impl.c runtime/tame_bindings.cpp                               src/vm/runtime_bindings.cpp src/vm/vm.cpp 2>/dev/null | head -1)
    fi
    if [ -n "$DIST_SRC_NEWEST" ]; then
        for d in wasm/dist android/dist/arm64-v8a android/dist/x86_64; do
            [ -d "$d" ] || continue
            oldest=$(ls -t "$d"/*.a 2>/dev/null | tail -1)
            [ -n "$oldest" ] || continue
            if [ "$DIST_SRC_NEWEST" -nt "$oldest" ]; then
                echo -e "${YELLOW}arsiv BAYAT: $d ($DIST_SRC_NEWEST daha yeni)${NC}"
                case "$d" in
                    wasm*) echo "   tazele: wasm/build_tame_web.sh" ;;
                    *)     echo "   tazele: android/build_tame_android.sh" ;;
                esac
            fi
        done
    fi
    # packages/ testleri. Bunlar birinci-taraf yayınlanabilir paketler ve
    # HİÇBİR otomasyonda koşmuyorlardı. `import "<ad>"` ÇALIŞMA DİZİNİNE göre
    # çözüldüğü için paketin kendi klasöründen çalıştırılmaları gerekiyor —
    # depo kökünden koşmak "Import dosyasi acilamadi" veriyor.
    if [ -d packages ]; then
        PKG_ROOT="$(pwd)"
        for pkgtest in packages/*/*.test.tpr; do
            [ -f "$pkgtest" ] || continue
            pkgdir=$(dirname "$pkgtest")
            pkgfile=$(basename "$pkgtest")
            pkgout=$(cd "$pkgdir" && "$PKG_ROOT/tulpar" "$pkgfile" 2>&1)
            if echo "$pkgout" | grep -q "^Tests:"; then
                if echo "$pkgout" | grep -qE "Fail: [1-9]"; then
                    printf "%-42s ${RED}FAIL${NC} %s\n" "$pkgtest" \
                        "$(echo "$pkgout" | grep '^Tests:')"
                    echo "$pkgout" | grep '  FAIL' | head -5
                    exit 1
                fi
                printf "%-42s ${GREEN}PASS${NC} %s\n" "$pkgtest" \
                    "$(echo "$pkgout" | grep '^Tests:')"
            else
                # Özet YOKSA başarısız sayılır — test_summary() çağırmayı
                # unutan bir süit asla kızaramaz (suites'in kendi kuralı).
                printf "%-42s ${RED}OZET YOK${NC}\n" "$pkgtest"
                echo "$pkgout" | tail -3
                exit 1
            fi
        done
    fi

    # Ayrılmış kelime tanılaması: `int tip = 5` gibi bir çarpışmada hata
    # mesajı SUÇLU KELİMEYİ söylemeli. Genel "ad bekleniyordu" mesajı bu
    # durumda belirtiyi anlatıyor, sebebi değil — ve bu oturumda üç kez yanlış
    # yere baktırdı.
    RW_TMP=$(mktemp -d)
    printf 'int tip = 5;\n' > "$RW_TMP/rw.tpr"
    RW_OUT=$(./tulpar typecheck "$RW_TMP/rw.tpr" 2>&1 || true)
    if echo "$RW_OUT" | grep -q "'tip'"; then
        echo -e "${GREEN}ayrilmis kelime tanilamasi calisiyor${NC}"
    else
        echo -e "${RED}Ayrilmis kelime tanilamasi suclu kelimeyi soylemiyor!${NC}"
        echo "$RW_OUT" | head -3
        rm -rf "$RW_TMP"
        exit 1
    fi
    # PARAMETRE ADI ayrı bir yol: orada kelime TİP olarak yutuluyor ve hata
    # ')' üzerinde patlıyordu, yani suçlu kelime hiç geçmiyordu. Dördüncü
    # kez aynı tuzağa düşülünce ayrıca sınanır oldu.
    printf 'func f(ad, metin) { print(ad); }\n' > "$RW_TMP/rwp.tpr"
    RWP_OUT=$(./tulpar typecheck "$RW_TMP/rwp.tpr" 2>&1 || true)
    if echo "$RWP_OUT" | grep -q "'metin'"; then
        echo -e "${GREEN}parametre adi tanilamasi calisiyor${NC}"
    else
        echo -e "${RED}Parametre adi tanilamasi suclu kelimeyi soylemiyor!${NC}"
        echo "$RWP_OUT" | head -3
        rm -rf "$RW_TMP"
        exit 1
    fi
    rm -rf "$RW_TMP"

    # OPTİMİZASYON SEVİYESİ denetimi.
    #
    # `LLVMRunPasses`ten sonra modül doğrulayıcıdan geçemezse derleyici
    # O3 -> O2 -> O1 merdivenine düşüyor ve bunu tek satır stderr notuyla
    # söylüyor. 2026-09-06'ya kadar bu not KİMSENİN GÖRMEDİĞİ bir yerde
    # duruyordu: temel blokları KÜRESEL bağlamda yaratmak yüzünden
    # vektörleşen her döngü doğrulamayı düşürüyor ve program sessizce
    # O1'de derleniyordu. Testler yeşil, çıktı doğru, kod %25 yavaş.
    #
    # Bu yüzden bir SÜRAT özelliği burada, doğruluk paketlerinin yanında
    # sınanıyor: sessizce yavaşlamak da bir gerileme.
    OPT_TMP=$(mktemp -d)
    cat > "$OPT_TMP/vec.tpr" <<'TPREOF'
int[] a = array_fill(4096, 0);
for (int i = 0; i < len(a); i = i + 1) { a[i] = i; }
print(a[4095]);
TPREOF
    OPT_OUT=$(TULPAR_AOT_EMIT_LL=1 ./tulpar build "$OPT_TMP/vec.tpr" "$OPT_TMP/vec" 2>&1)
    if echo "$OPT_OUT" | grep -q "aggressive O3 IR invalid"; then
        echo -e "${RED}Optimizasyon O3'ten DUSTU — uretilen kod sessizce yavas!${NC}"
        echo "$OPT_OUT" | grep -i "o3\|note" | head -3
        rm -rf "$OPT_TMP"
        exit 1
    fi
    echo -e "${GREEN}optimizasyon O3'te kaldi${NC}"
    # Bekçisiz KANITLI YAZMA gerçekten üretiliyor mu?
    #
    # İlk yazılan denetim sökülen `main` içinde SIMD komutu arıyordu.
    # ÖLÇÜLDÜ ve BIRAKILDI: LLVM 18'de (CI'ın sürümü) aynı döngü
    # vektörleşmiyor — komut sayısı 0, oysa üretilen kod doğru ve O3'ten
    # de düşmüyor. Yani o denetim CI'ı, gerçek bir gerileme olmadan
    # kırardı. Vektörleşme LLVM'in aşağı akıştaki kararı; BİZİM
    # garantimiz bekçisiz depo komutunun üretilmesi.
    #
    # `set.pep` = kanıtlı eleman yazmasının GEP'i (llvm_backend.cpp,
    # AST_ASSIGNMENT / ARRAY_ACCESS dalı). Adı değiştirirsen burayı da
    # değiştir.
    LLOUT="$OPT_TMP/vec.ll"
    if [ ! -f "$LLOUT" ]; then LLOUT=$(ls "$OPT_TMP"/*.ll 2>/dev/null | head -1); fi
    if [ -n "$LLOUT" ] && [ -f "$LLOUT" ] && grep -q "set\.pep" "$LLOUT"; then
        echo -e "${GREEN}kanitli eleman yazmasi uretiliyor${NC} (bekcisiz depo)"
    else
        echo -e "${RED}Kanitli eleman yazmasi URETILMIYOR — doldurma dongusu bekcili!${NC}"
        rm -rf "$OPT_TMP"
        exit 1
    fi
    rm -rf "$OPT_TMP"

    # ÖZYİNELEME ZİNCİRİ gerçekten kuruluyor ve işe yarıyor mu?
    #
    # LLVM doğrudan kendini çağıran bir fonksiyonu satır içine ALMAZ (satır
    # içi alıcı bir SCC kenarını kendi içine açmayı reddediyor). Arka uç bu
    # yüzden K=4 klon üretip halka kuruyor — llvm_backend.cpp `selfrec_*`.
    # Ölçüldü 2026-09-07: fib(32) 4,90 -> 0,71 ms; gcc -O2 1,70, Rust 3,70.
    #
    # Denetim İKİ ayaklı, çünkü her ayak tek başına sessizce boşa çıkabilir:
    #   1) IR'de klon TANIMI var mı  -> zincir kuruluyor mu
    #   2) zincirli ikili, zincirsizden en az 2 kat hızlı mı (SÜREÇ AÇILIŞI
    #      ÇIKARILMIŞ iş payı üzerinden — aşağıdaki nota bak)
    # Yalnız (1) olsaydı: klonlar üretilip hiç satır içi ALINMADIĞINDA
    # (ör. yanlışlıkla noinline) denetim yeşil kalır, kazanç sıfır olurdu.
    # Yalnız (2) olsaydı: makineye bağlı olurdu. Ölçüm GÖRELİ — aynı makine,
    # aynı an, tek değişken TULPAR_NO_SELFREC. N=32'de ölçülen oran ~6-7 kat,
    # eşik 2 kat. (N=30 ile başlanmıştı: iş yükü küçük olduğu için süreç
    # açılışı oranı 2,7 kata indiriyordu — eşiğe fazla yakın. Payı iş yükünü
    # büyüterek açtık, eşiği düşürerek değil.)
    #
    # N ortamdan okunuyor: sabit olsaydı LLVM `fib(30)`u derleme zamanında
    # katlar ve iki ikili de 0 ms sürerdi — ölçüm hiçbir şey ölçmezdi.
    SR_TMP=$(mktemp -d)
    cat > "$SR_TMP/fib.tpr" <<'TPREOF'
func fib(int n): int {
    if (n <= 1) { return n; }
    return fib(n - 1) + fib(n - 2);
}
int n = toInt(env("SR_N"));
if (n <= 0) { n = 32; }
print(fib(n));
TPREOF
    # 1. ayak: zincirin ETKISI IR'de gorunuyor mu?
    #
    # Bu ayak once `@fib.rec` sembolunu ariyordu. YANLISTI: derinlik K=1'e
    # inince klon TAMAMEN satir ici alinip yok oluyor — yani optimizasyon
    # HER ZAMANKINDEN IYI calisirken (8,85x) denetim dusuyordu. Sembol bir
    # UYGULAMA ARTEFAKTI; sinanmasi gereken sey ETKI.
    #
    # Kalici degismez: zincir, ozyinelemenin bir seviyesinin acilmasini
    # saglar, dolayisiyla zincirli `@fib` govdesi zincirsizden DAHA COK
    # oz-cagri icerir (olculdu: 3 vs 1). Sembol hayatta kalsa da kalmasa da
    # bu dogru.
    SR_LLDIR=$(mktemp -d)
    TULPAR_AOT_EMIT_LL=1 ./tulpar build "$SR_TMP/fib.tpr" "$SR_LLDIR/fib_on" >/dev/null 2>&1
    SR_LL_ON=$(ls "$SR_LLDIR"/*.ll 2>/dev/null | head -1)
    [ -n "$SR_LL_ON" ] && mv "$SR_LL_ON" "$SR_LLDIR/on.ll"
    TULPAR_NO_SELFREC=1 TULPAR_AOT_EMIT_LL=1 ./tulpar build "$SR_TMP/fib.tpr" "$SR_LLDIR/fib_off" >/dev/null 2>&1
    SR_LL_OFF=$(ls "$SR_LLDIR"/*.ll 2>/dev/null | grep -v 'on\.ll' | head -1)
    [ -n "$SR_LL_OFF" ] && mv "$SR_LL_OFF" "$SR_LLDIR/off.ll"
    sr_selfcalls() {
        [ -f "$1" ] || { echo 0; return; }
        awk '/^define .*@fib\(/,/^}/' "$1" | grep -c 'call .*@fib'
    }
    SR_C_ON=$(sr_selfcalls "$SR_LLDIR/on.ll")
    SR_C_OFF=$(sr_selfcalls "$SR_LLDIR/off.ll")
    if [ "$SR_C_ON" -le "$SR_C_OFF" ]; then
        echo -e "${RED}Ozyineleme zinciri ETKISIZ — ozyinelemeli kod satir ici alinmiyor!${NC}"
        echo "  @fib icindeki oz-cagri: zincirli=$SR_C_ON zincirsiz=$SR_C_OFF (zincirli DAHA COK olmali)"
        rm -rf "$SR_TMP" "$SR_LLDIR"
        exit 1
    fi
    rm -rf "$SR_LLDIR"
    cp "$SR_TMP/fib.tpr" "$SR_TMP/fib_copy.tpr" 2>/dev/null
    ./tulpar build "$SR_TMP/fib.tpr" "$SR_TMP/fib_on" >/dev/null 2>&1
    TULPAR_NO_SELFREC=1 ./tulpar build "$SR_TMP/fib.tpr" "$SR_TMP/fib_off" >/dev/null 2>&1
    if [ ! -x "$SR_TMP/fib_on" ] || [ ! -x "$SR_TMP/fib_off" ]; then
        echo -e "${RED}Ozyineleme olcumu icin ikililer uretilemedi!${NC}"
        rm -rf "$SR_TMP"
        exit 1
    fi
    # Cikti esitligi: hizlanma dogru sonuc uzerinde olmali.
    SR_OUT_ON=$(SR_N=32 "$SR_TMP/fib_on")
    SR_OUT_OFF=$(SR_N=32 "$SR_TMP/fib_off")
    if [ "$SR_OUT_ON" != "$SR_OUT_OFF" ] || [ "$SR_OUT_ON" != "2178309" ]; then
        echo -e "${RED}Ozyineleme zinciri SONUCU DEGISTIRDI! zincirli=$SR_OUT_ON zincirsiz=$SR_OUT_OFF beklenen=2178309${NC}"
        rm -rf "$SR_TMP"
        exit 1
    fi
    sr_best_us() {
        local best=99999999 i t0 t1 d
        for i in 1 2 3; do
            t0=$(date +%s%N); SR_N=$2 "$1" >/dev/null 2>&1; t1=$(date +%s%N)
            d=$(( (t1 - t0) / 1000 ))
            [ "$d" -lt "$best" ] && best=$d
        done
        echo "$best"
    }
    # SÜREÇ AÇILIŞINI ÇIKAR — yoksa eşik platforma bağlı olur.
    #
    # Ölçülen süre `fork+exec+dyld+fib`. Linux'ta açılış ~0,2 ms ve N=32'lik
    # iş onu gölgede bırakıyor, oran ~7 kat çıkıyor. macOS arm64'te açılış
    # ~10 ms; aynı sabit HER İKİ tarafa da eklenince oranı 1,0'a doğru EZİYOR
    # ve zincir kusursuz çalışırken denetim düşüyor. Ölçüldü (2026-09-09, CI
    # macOS arm64): zincirsiz 18525us / zincirli 12210us = 1,52 kat — oysa
    # iş payının oranı ~4 kat.
    #
    # Çözüm: açılışı AYNI ikiliden N=1 ile ölçüp çıkarmak. Aynı binary, aynı
    # kod yerleşimi, tek değişen iş miktarı; kalan yalnızca fib işi. Eşik
    # böylece makineden bağımsız hale geliyor. (Eşiği düşürmek YANLIŞ cevap
    # olurdu: ölçüm hatasını gizler, gerçek bir gerilemeyi de kaçırırdı.)
    SR_ON=$(sr_best_us "$SR_TMP/fib_on" 32)
    SR_OFF=$(sr_best_us "$SR_TMP/fib_off" 32)
    SR_ON_BASE=$(sr_best_us "$SR_TMP/fib_on" 1)
    SR_OFF_BASE=$(sr_best_us "$SR_TMP/fib_off" 1)
    SR_ON_W=$(( SR_ON - SR_ON_BASE ));  [ "$SR_ON_W" -lt 1 ] && SR_ON_W=1
    SR_OFF_W=$(( SR_OFF - SR_OFF_BASE )); [ "$SR_OFF_W" -lt 1 ] && SR_OFF_W=1
    if [ "$SR_OFF_W" -gt $(( SR_ON_W * 2 )) ]; then
        echo -e "${GREEN}ozyineleme zinciri calisiyor${NC} (is: ${SR_OFF_W}us -> ${SR_ON_W}us, acilis ~${SR_ON_BASE}us cikarildi)"
    else
        echo -e "${RED}Ozyineleme zinciri KAZANC VERMIYOR — klonlar satir ici alinmiyor!${NC}"
        echo "  is payi: zincirsiz=${SR_OFF_W}us zincirli=${SR_ON_W}us (en az 2 kat bekleniyor)"
        echo "  ham: zincirsiz=${SR_OFF}us zincirli=${SR_ON}us, acilis=${SR_OFF_BASE}/${SR_ON_BASE}us"
        rm -rf "$SR_TMP"
        exit 1
    fi

    # GERILEME KAPISI: zincir BASKA sekillerde ZARAR VERMEMELI.
    #
    # Bu denetim uzun sure YALNIZ fib'i sinadi ve tam bu yuzden gercek bir
    # gerilemeyi kacirdi: K=4'te `ackermann` zincirli surumde zincirsizden
    # %29 YAVASTI (13,74 vs 10,66 ms) ve butun testler yesil kaliyordu.
    # Olculdu 2026-09-09: derinlik K taranınca hicbir K her cekirdekte en iyi
    # degil, ama K=1'in EN KOTU durumu 1,03x (notr) iken K=4'unki 0,78x
    # (zarar). Bir optimizasyonun bazi programlari yavaslatmasi HATADIR;
    # bazilarina daha az kazandirmasi degil. Bu yuzden K=1 ve bu kapi.
    #
    # ackermann secildi cunku sekli fib'den farkli: ozyinelemeli cagri baska
    # bir cagrinin argumaninda (ack(m-1, ack(m, n-1))). fib'de yan yana.
    AR_TMP=$(mktemp -d)
    cat > "$AR_TMP/ack.tpr" <<'TPREOF'
func ack(int m, int n): int {
    if (m == 0) { return n + 1; }
    if (n == 0) { return ack(m - 1, 1); }
    return ack(m - 1, ack(m, n - 1));
}
int n = toInt(env("SR_N"));
if (n <= 0) { n = 9; }
print(ack(3, n));
TPREOF
    ./tulpar build "$AR_TMP/ack.tpr" "$AR_TMP/ack_on" >/dev/null 2>&1
    TULPAR_NO_SELFREC=1 ./tulpar build "$AR_TMP/ack.tpr" "$AR_TMP/ack_off" >/dev/null 2>&1
    if [ ! -x "$AR_TMP/ack_on" ] || [ ! -x "$AR_TMP/ack_off" ]; then
        echo -e "${RED}Gerileme kapisi icin ackermann ikilileri uretilemedi!${NC}"
        rm -rf "$AR_TMP"; exit 1
    fi
    AR_O1=$(SR_N=9 "$AR_TMP/ack_on"); AR_O2=$(SR_N=9 "$AR_TMP/ack_off")
    if [ "$AR_O1" != "$AR_O2" ] || [ "$AR_O1" != "4093" ]; then
        echo -e "${RED}Zincir ackermann SONUCUNU DEGISTIRDI! on=$AR_O1 off=$AR_O2 beklenen=4093${NC}"
        rm -rf "$AR_TMP"; exit 1
    fi
    AR_ON=$(sr_best_us "$AR_TMP/ack_on" 9);   AR_ONB=$(sr_best_us "$AR_TMP/ack_on" 1)
    AR_OFF=$(sr_best_us "$AR_TMP/ack_off" 9); AR_OFFB=$(sr_best_us "$AR_TMP/ack_off" 1)
    AR_ON_W=$(( AR_ON - AR_ONB ));   [ "$AR_ON_W" -lt 1 ] && AR_ON_W=1
    AR_OFF_W=$(( AR_OFF - AR_OFFB )); [ "$AR_OFF_W" -lt 1 ] && AR_OFF_W=1
    # Zincirli, zincirsizin 1,25 katindan yavas OLMAMALI (%25 pay olcum
    # oynamasi icin; gercek gerileme %29'du, yani bu kapi onu yakalardi).
    if [ $(( AR_ON_W * 100 )) -gt $(( AR_OFF_W * 125 )) ]; then
        echo -e "${RED}Zincir ackermann'i GERILETIYOR — karlilik modeli bozuk!${NC}"
        echo "  is payi: zincirli=${AR_ON_W}us zincirsiz=${AR_OFF_W}us (>%25 yavas)"
        rm -rf "$AR_TMP"; exit 1
    fi
    echo -e "${GREEN}zincir baska sekillerde zarar vermiyor${NC} (ackermann: ${AR_OFF_W}us -> ${AR_ON_W}us)"

    # TANI TEK KAPIDAN CIKAR — mekanik garanti (#19).
    #
    # Calisma zamani tanilari `aot_runtime_error` / `vm_runtime_error`den
    # gecmeli: ikisi de stderr'e yazar ve strict modda firlatir. Ham `printf`
    # ile yazilan bir tani (a) programin kendi ciktisina karisir, (b) firlatmaz,
    # yani surec 0 ile cikar.
    #
    # NEDEN MEKANIK: flip sirasinda printf->throw donusumu REGEX'le yapildi ve
    # farkli bicimli bir printf'i KACIRDI — `aot_div_error`in tasma dali stdout'a
    # yazip 0 ile cikmaya devam etti, sifira bolme ise firlatiyordu. Ayni
    # aileden iki hata, iki farkli sozlesme. Kacan dali yakalayan sey bir
    # sondanin ESKI beklentiyle GECMESI oldu — yani sans. Metin-deseniyle
    # yapilan donusum envanteri yeniden uretmez; bu denetim envanteri kalici
    # kilar: bir sonraki kacak yesil sonda degil, KIRMIZI BUILD olur.
    # ⚠ COK SATIRLI OLMALI. Ilk yazimda bu denetim satir-bazliydi
    # (`grep printf | grep Hatasi`) ve HEMEN yanlis yesil verdi: kaynakta
    # `printf("%s\n",` bir satirda, hata metni SONRAKI satirda olan IKI
    # sizinti vardi ve denetim ikisini de goremedi. Yani koruma, korumak icin
    # yazildigi hatanin (metin-deseni envanteri yeniden uretmez, #19) TAM
    # OLARAK aynisini yapiyordu. Simdi cagri parantez dengesiyle taraniyor.
    RT_LEAK=$(python3 - <<'PYEOF'
import re, pathlib
txt = pathlib.Path("src/vm/runtime_bindings.cpp").read_text(encoding="utf-8")
for m in re.finditer(r'\bprintf\s*\(', txt):
    seg = txt[m.start():m.start() + 600]
    depth = 0
    call = seg
    for i, ch in enumerate(seg):
        if ch == '(':
            depth += 1
        elif ch == ')':
            depth -= 1
            if depth == 0:
                call = seg[:i + 1]
                break
    if re.search(r'Hatasi|Runtime Error', call):
        print(f"  satir {txt[:m.start()].count(chr(10)) + 1}: {call.splitlines()[0][:70]}")
PYEOF
)
    if [ -n "$RT_LEAK" ]; then
        echo -e "${RED}TANI STDOUT'A SIZIYOR — aot_runtime_error kullanin!${NC}"
        echo "$RT_LEAK" | head -5 | sed 's/^/  /'
        exit 1
    fi
    # SAYISAL BEKLENEN-DEGER: desen-guvenligi tek basina yetmez — denetim
    # sessizce korlese "sizinti yok" der. Yetkili cikis noktalarinin sayisini
    # da raporluyoruz; beklenen minimumun altina duserse denetim degil, KAPI
    # kaybolmus demektir.
    RT_GATES=$(grep -c "aot_runtime_error(" src/vm/runtime_bindings.cpp)
    if [ "$RT_GATES" -lt 15 ]; then
        echo -e "${RED}Yetkili tani kapisi sayisi beklenenin altinda ($RT_GATES < 15)${NC}"
        echo "  Tanilar baska bir yola mi tasindi? Denetim korlesmis olabilir."
        exit 1
    fi
    echo -e "${GREEN}tani tek kapidan cikiyor${NC} (ham printf yok, $RT_GATES yetkili kapi)"
    rm -rf "$AR_TMP"
    rm -rf "$SR_TMP"

    # KUTULU (tipsiz) YOLUN satır içi hızlı yolları duruyor mu?
    #
    # Bu ikisi SESSİZCE geri alınabilir: ikisi de yalnızca hız değiştiriyor,
    # sonucu değil. `tests/boxed_fast_paths.test.tpr` doğruluğu kilitliyor ama
    # hızlı yol silinse geri düşüş AYNI cevabı verir ve paket yeşil kalır —
    # yani o paket tek başına bu gerilemeyi göremez. Denetim burada YAPISAL.
    #
    #   1) `srem`  = kutulu `%` için satır içi tamsayı yolu (build_checked_div).
    #      Yoksa her modulo vm_binary_op'a gidiyor: ölçüldü 134,6 -> 46,5 ms.
    #   2) `ap.isobj` = global atamasındaki aot_persist etiket denetimi.
    #      Yoksa her kutulu global ataması koşulsuz runtime çağrısı:
    #      ölçüldü 90,8 -> 46,3 ms.
    #
    # Değerler ortamdan okunuyor: sabit olsaydı LLVM katlar, `srem` hiç
    # üretilmezdi ve denetim kendi kendini yanlış kırardı.
    BX_TMP=$(mktemp -d)
    cat > "$BX_TMP/bx.tpr" <<'TPREOF'
var a = toInt(env("BX_A"));
if (a <= 0) { a = 17; }
var b = toInt(env("BX_B"));
if (b <= 0) { b = 5; }
var g = 0;
g = a % b;
print(g);
TPREOF
    TULPAR_AOT_EMIT_LL=1 ./tulpar build "$BX_TMP/bx.tpr" "$BX_TMP/bx" >/dev/null 2>&1
    BX_LL=$(ls "$BX_TMP"/*.ll 2>/dev/null | head -1)
    if [ -z "$BX_LL" ] || ! grep -q "srem" "$BX_LL"; then
        echo -e "${RED}Kutulu '%' satir ici yolu YOK — her modulo runtime cagrisi!${NC}"
        rm -rf "$BX_TMP"
        exit 1
    fi
    if ! grep -q "ap\.isobj" "$BX_LL"; then
        echo -e "${RED}aot_persist etiket bekcisi YOK — her kutulu global atamasi runtime cagrisi!${NC}"
        rm -rf "$BX_TMP"
        exit 1
    fi
    BX_OUT=$(BX_A=17 BX_B=5 "$BX_TMP/bx")
    if [ "$BX_OUT" != "2" ]; then
        echo -e "${RED}Kutulu '%' YANLIS sonuc veriyor: $BX_OUT (2 olmali)${NC}"
        rm -rf "$BX_TMP"
        exit 1
    fi
    echo -e "${GREEN}kutulu hizli yollar duruyor${NC} (satir ici %, persist bekcisi)"
    rm -rf "$BX_TMP"

    # DİZİ ELEMAN GENİŞLİĞİ: kanıtlı erişim 32-BİT mi?
    #
    # Kutusuz `int[]` 32-bit başlıyor (eleman başına 8 yerine 4 bayt); ölçüldü:
    # elek 8,06 -> 7,80, arrayiter 1,5 -> 1,3. Doğruluğu
    # `tests/array_width.test.tpr` kilitliyor ama o paket, hızlı yol sessizce
    # 64-bit'e dönse de YEŞİL kalır — sonuç yine doğru olur, yalnız yavaş.
    # Bu yüzden denetim YAPISAL: sürümlenmiş döngünün kanıtlı erişimi
    # `arr.pep.p32` (i32 GEP) üretmeli.
    AW_TMP=$(mktemp -d)
    cat > "$AW_TMP/aw.tpr" <<'TPREOF'
int n = toInt(env("AW_N"));
if (n <= 0) { n = 64; }
int[] a = array_fill(n, 0);
int t = 0;
int i = 0;
while (i < n) { t = t + a[i]; i = i + 1; }
print(t);
TPREOF
    TULPAR_AOT_EMIT_LL=1 ./tulpar build "$AW_TMP/aw.tpr" "$AW_TMP/aw" >/dev/null 2>&1
    AW_LL=$(ls "$AW_TMP"/*.ll 2>/dev/null | head -1)
    if [ -z "$AW_LL" ] || ! grep -q "arr\.pep\.p32" "$AW_LL"; then
        echo -e "${RED}Kanitli dizi erisimi 32-BIT DEGIL — eleman genisligi kazanci gitti!${NC}"
        rm -rf "$AW_TMP"
        exit 1
    fi
    AW_OUT=$(AW_N=64 "$AW_TMP/aw")
    if [ "$AW_OUT" != "0" ]; then
        echo -e "${RED}Genislik denetimi YANLIS sonuc verdi: $AW_OUT${NC}"
        rm -rf "$AW_TMP"
        exit 1
    fi
    echo -e "${GREEN}dizi elemani 32-bit${NC} (kanitli erisim i32)"
    rm -rf "$AW_TMP"

    # KUTULU FONKSİYONLARIN DEĞER ABI'si duruyor mu?
    #
    # Kutulu gövde `t_<ad>.f` içinde yaşıyor ve VMValue'yu DEĞER olarak
    # alıp döndürüyor (SysV'de iki yazmaç); `t_<ad>` ince sarmalayıcı.
    # Ölçüldü: tipsiz `fib(32)` 11,86 -> 7,69 ms.
    #
    # Denetim YAPISAL: ABI sessizce işaretçiye dönse sonuç yine doğru olur,
    # yalnız 1,5 kat yavaş — `tests/boxed_value_abi.test.tpr` bunu göremez.
    VA_TMP=$(mktemp -d)
    cat > "$VA_TMP/va.tpr" <<'TPREOF'
func topla(a, b) { return a + b; }
var n = toInt(env("VA_N"));
if (n <= 0) { n = 20; }
print(topla(n, 22));
TPREOF
    TULPAR_AOT_EMIT_LL=1 ./tulpar build "$VA_TMP/va.tpr" "$VA_TMP/va" >/dev/null 2>&1
    VA_LL=$(ls "$VA_TMP"/*.ll 2>/dev/null | head -1)
    if [ -z "$VA_LL" ] || ! grep -q "t_topla\.f" "$VA_LL"; then
        echo -e "${RED}Kutulu deger ABI'si YOK — tipsiz cagrilar bellekten geciyor!${NC}"
        rm -rf "$VA_TMP"
        exit 1
    fi
    VA_OUT=$(VA_N=20 "$VA_TMP/va")
    if [ "$VA_OUT" != "42" ]; then
        echo -e "${RED}Deger ABI'si YANLIS sonuc verdi: $VA_OUT (42 olmali)${NC}"
        rm -rf "$VA_TMP"
        exit 1
    fi
    echo -e "${GREEN}kutulu deger ABI'si duruyor${NC} (t_<ad>.f)"
    rm -rf "$VA_TMP"

    # Kod üretimi DENKLİK denetimi: sahne JSON'undan üretilen Tulpar kodu
    # derlenip çalıştırılıyor ve kurduğu sahne yeniden serileştirilerek
    # kaynakla karşılaştırılıyor. "Kod da aynı sahneyi kuruyor" iddiasını
    # ölçen tek şey bu — üretilen metni gözle okumak yetmez.
    # İKİ sahne üzerinden koşuyor. `toplayici` gerçek bir demo (davranışlar,
    # iki bölüm, isimli hedef); `kod_uretimi_tam` ise KASITLI olarak kapsamı
    # doldurmak için üretildi — bölge kutu+küre, eylem+miktar+ses, tek atım ve
    # kapalı bölge, sesli/sessiz kural. Gerek vardı: demo sahnesinde HİÇ bölge
    # yok, o yüzden bölge kod üretimi denetimsizdi ve `bolge_eylem3d`/
    # `bolge_ses3d`'nin hiç yazılmadığı (JSON'da var, üretilen kodda yok)
    # sessizce aylarca durabilirdi. Yeni bir alan ekleyen, düzeneği de büyütsün.
    CODEGEN_SCENES="examples/scenes/toplayici.scene.json tests/kod_uretimi_tam.scene.json"
    if [ -f "examples/scene3d_export.tpr" ]; then
        echo ""
        for CODEGEN_SCENE in $CODEGEN_SCENES; do
            [ -f "$CODEGEN_SCENE" ] || continue
            CG_TMP=$(mktemp -d)
            if ./tulpar examples/scene3d_export.tpr "$CODEGEN_SCENE" 2>/dev/null > "$CG_TMP/kur.tpr" \
               && ./tulpar examples/scene3d_export.tpr "$CODEGEN_SCENE" --dogrula 2>/dev/null > "$CG_TMP/src.json"; then
                {
                    echo 'import "scene3d";'
                    cat "$CG_TMP/kur.tpr"
                    echo 'kur();'
                    echo 'print(sahne_json3d());'
                } > "$CG_TMP/verify.tpr"
                if ./tulpar "$CG_TMP/verify.tpr" 2>/dev/null > "$CG_TMP/gen.json" \
                   && diff -q "$CG_TMP/src.json" "$CG_TMP/gen.json" >/dev/null; then
                    echo -e "${GREEN}kod uretimi denk${NC} ($CODEGEN_SCENE)"
                else
                    echo -e "${RED}Kod uretimi DENK DEGIL!${NC} ($CODEGEN_SCENE)"
                    diff "$CG_TMP/src.json" "$CG_TMP/gen.json" | head -20
                    rm -rf "$CG_TMP"
                    exit 1
                fi
            else
                echo -e "${RED}Kod uretimi denetimi calistirilamadi!${NC} ($CODEGEN_SCENE)"
                rm -rf "$CG_TMP"
                exit 1
            fi
            rm -rf "$CG_TMP"
        done
    fi

    hw_end "suites ($SUITE_N paket)"
    echo -e "${GREEN}All $SUITE_N suites passed!${NC}"
    exit 0
fi

if [ "$ACTION" = "test" ]; then
    hw_begin
    # Ensure tulpar exists
    if [ ! -f "tulpar" ]; then
        echo "Executable 'tulpar' not found. Building first..."
        $0  # Recursive call to build
        if [ $? -ne 0 ]; then
            exit 1
        fi
    fi

    echo ""
    echo -e "${BLUE}========================================${NC}"
    echo -e "${BLUE}Running tests...${NC}"
    echo -e "${BLUE}========================================${NC}"
    echo ""

    TEST_FAILED=0
    INPUT_DIR="examples/inputs"
    # Parallel workers drop multi-line failure detail here (one <name>.log
    # per failing example); the driver dumps them, sorted, after the run.
    FAIL_DIR=$(mktemp -d)
    SKIP_TESTS=()
    # Compile-only smoke tests: server/listener examples that block on
    # listen()/api_run(), plus utils.tpr (module-only — has no top-level
    # program, but we still verify it parses/lowers). We verify the build
    # succeeds (catches regressions in the embedded server/router/api
    # stdlib path) but do not run the binary.
    COMPILE_ONLY_TESTS=("09_socket_simple.tpr" "09_socket_server.tpr" \
                        "09_socket_client.tpr" "11_router_app.tpr" \
                        "12_threaded_server.tpr" "14_api_server.tpr" \
                        "api_wings.tpr" "api_wings_crud.tpr" \
                        "api_wings_tls.tpr" "api_wings_sse.tpr" \
                        "api_router_crud.tpr" \
                        "demo_users_api.tpr" "wings_simple_test.tpr" \
                        "wings_middleware_test.tpr" "wings_groups_test.tpr" \
                        "wings_query_test.tpr" "wings_response_model_test.tpr" \
                        "wings_upload_test.tpr" "wings_di_test.tpr" \
                        "wings_todo_api.tpr" "wings_auth_api.tpr" \
                        "wings_notes_db.tpr" "wings_redirect.tpr" \
                        "wings_features_api.tpr" "wings_orm_resource.tpr" \
                        "tulpar_api_demo.tpr" "utils.tpr" \
                        "tame_hello.tpr" "tame_sprite_demo.tpr" \
                        "tame_run_demo.tpr" "tame_web_mini.tpr" \
                        "tame_snake.tpr" \
                        "arcade_topla.tpr" "arcade_zipla.tpr" \
                        "arcade_nisan.tpr" "arcade_tugla.tpr" \
                        "arcade_uzay.tpr" "arcade_labirent.tpr" \
                        "arcade_karsiya.tpr" "arcade_ucus.tpr" \
                        "arcade_goktasi.tpr" "arcade_launcher.tpr" "arcade_yilan.tpr" \
                        "arcade_2048.tpr" "arcade_pong.tpr" "arcade_vur.tpr" \
                        "scene3d_data_game.tpr" "scene3d_editor.tpr" \
                        "scene3d_export.tpr" \
                        "tame3d_cube.tpr" "tame3d_primitives.tpr" \
                        "tame3d_models.tpr" "tame3d_anim.tpr" "tame3d_lights.tpr" \
                        "tame3d_shadows.tpr" "tame3d_texture.tpr" \
                        "scene3d_collector.tpr" "scene3d_camera.tpr" \
                        "scene3d_arena.tpr" "scene3d_terrain.tpr" \
                        "scene3d_karakter.tpr" "scene3d_labirent.tpr" \
                        "scene3d_ses_testi.tpr" \
                        "41_struct_entities.tpr")
    # tame_*.tpr: display'li makinede pencere açıp kullanıcı kapatana
    # dek bloklar (headless'ta zarif hata ile hemen çıkar) — deterministik
    # olsun diye compile-only. Derlemeleri libtulpar_tame.a link zincirini
    # (vendored raylib + aot_tm_* binding'leri) uçtan uca doğrular.

    # HTTP smoke probes. The 2-second alive check above only verifies the
    # process didn't crash during startup — wings/router examples block
    # in accept() so they always pass that bar even if their first request
    # handler segfaults. Issue #86 (PR #76 cookies regression) sat on main
    # for four PRs precisely because nobody actually sent an HTTP request
    # to a wings binary in CI. Each probe entry below = "after the binary
    # is alive at +2s, GET this URL with a 5s timeout and require any HTTP
    # response code". We DON'T validate the body or status — the bug fires
    # before the response is even built — we just need a roundtrip.
    smoke_probe_for() {
        case "$1" in
            api_wings.tpr)        echo "http://127.0.0.1:3000/" ;;
            api_wings_crud.tpr)   echo "http://127.0.0.1:3000/" ;;
            api_wings_tls.tpr)    echo "https://127.0.0.1:8443/" ;;
            api_wings_sse.tpr)    echo "http://127.0.0.1:8093/" ;;
            api_router_crud.tpr)  echo "http://127.0.0.1:8080/" ;;
            11_router_app.tpr)    echo "http://127.0.0.1:8080/" ;;
            12_threaded_server.tpr) echo "http://127.0.0.1:8089/" ;;
            *) echo "" ;;
        esac
    }

    # run_test runs ONE example and returns 0 (pass) / 1 (fail).
    #
    # It is invoked both serially (single-file `./build.sh test <file>`) and
    # from parallel `xargs -P` workers, so it must be subshell-safe:
    #   * NEVER set a parent variable to signal failure (a subshell can't) —
    #     the exit status is the only channel. xargs turns any non-zero
    #     worker exit into its own 123 exit, which the driver checks.
    #   * Print exactly ONE complete line per test via a single printf, so
    #     concurrent workers can't interleave mid-line. Multi-line failure
    #     detail goes to $FAIL_DIR/<name>.log instead and is dumped, in
    #     order, after the whole run finishes.
    run_test() {
        local example="$1"
        local compile_only="$2"
        local name=$(basename "$example" .tpr)
        local input_file="$INPUT_DIR/$name.txt"
        # `tulpar --aot <foo.tpr>` derives its output binary from the source
        # basename (`<name>` here), NOT `a.out` — the historical fallback
        # only kicks in when the basename strips down to empty (which never
        # happens for our examples). Earlier versions of this runner checked
        # `[ -f a.out ]` and silently failed every example on Linux CI.
        local out_path="$name"
        local compile_log
        compile_log=$(mktemp)
        # Failure detail sink. FAIL_DIR is exported by the driver; when
        # run_test is called outside the driver (defensive) fall back to a
        # temp dir so the redirects below always have somewhere to go.
        local fail_dir="${FAIL_DIR:-$(mktemp -d)}"

        # Separate compile vs run budgets. These used to share one 30s cap,
        # which was sized for a SERIAL suite: under `xargs -P$(nproc)` the
        # heaviest example (arcade_launcher — 13 namespaced games + arcade +
        # tame, ~10s of LLVM work unloaded) competes with N-1 other AOT
        # compiles for the same cores and blew straight past 30s on a
        # 4-core CI runner. Compile time legitimately scales with parallel
        # contention, so it gets a generous ceiling that still catches a
        # genuinely hung codegen; execution time does NOT scale that way —
        # a program that runs long is a bug, so it keeps the tight cap.
        # The step-level CI timeout is the backstop for both.
        local compile_timeout="${TULPAR_COMPILE_TIMEOUT:-180}"
        local run_timeout="${TULPAR_RUN_TIMEOUT:-30}"
        local TIMEOUT_BIN=""
        if command -v timeout &> /dev/null; then
            TIMEOUT_BIN="timeout"
        elif command -v gtimeout &> /dev/null; then
            TIMEOUT_BIN="gtimeout"
        fi
        local COMPILE_TIMEOUT_CMD=""
        local TIMEOUT_CMD=""
        if [ -n "$TIMEOUT_BIN" ]; then
            COMPILE_TIMEOUT_CMD="$TIMEOUT_BIN ${compile_timeout}s"
            TIMEOUT_CMD="$TIMEOUT_BIN ${run_timeout}s"
        fi

        # Run AOT compilation and (optionally) execution. Capture stderr+stdout
        # to a tempfile so we can echo it on failure — silent failures here
        # used to hide every diagnostic and turn CI into "all FAIL, no clue why".
        # Keep the exit code: `timeout` reports a kill as 124, and a timeout
        # otherwise looks identical to a compile error with an empty log
        # (which is exactly how the first parallel CI run presented itself).
        $COMPILE_TIMEOUT_CMD ./tulpar --aot "$example" > "$compile_log" 2>&1
        local compile_rc=$?
        if [ $compile_rc -eq 0 ] && [ -f "$out_path" ]; then
            if [ "$compile_only" = "1" ]; then
                # Runtime smoke test for COMPILE_ONLY examples: spawn the
                # binary in the background, give it 2s to either start
                # serving (wings/router will block on accept) or crash,
                # then check whether it's still alive. PR #64 was
                # exactly this regression: every wings example built
                # cleanly on Linux but segfaulted at socket_server() —
                # would have been caught here at CI time instead of
                # silently shipping in a release. SIGTERM the survivors;
                # any non-zero exit before the SIGTERM means a real
                # runtime failure.
                local smoke_log
                smoke_log=$(mktemp)
                # DISPLAY/WAYLAND_DISPLAY scrubbed: on WSLg/desktop the
                # tame/arcade examples would otherwise open a real game
                # window for the 2-second smoke — 15 windows popping over
                # whatever the user is doing on every test run. Headless,
                # tame fails gracefully with exit 0 (the InitWindow patch),
                # and the wings/router examples never used a display.
                DISPLAY= WAYLAND_DISPLAY= "./$out_path" > "$smoke_log" 2>&1 &
                local smoke_pid=$!
                sleep 2
                if kill -0 "$smoke_pid" 2>/dev/null; then
                    # Still alive after startup. If we have an HTTP probe
                    # for this example, hit it now — otherwise the silent
                    # "process is alive" pass hides handler-level bugs.
                    local probe_url
                    probe_url=$(smoke_probe_for "$(basename "$example")")
                    local probe_status="ok"
                    if [ -n "$probe_url" ] && command -v curl &> /dev/null; then
                        # -s: silent. -o /dev/null: drop body. --max-time 5:
                        # bound the whole request. -w "%{http_code}": print
                        # status code (or 000 on connect/timeout failure).
                        # -k: trust self-signed certs — needed for the
                        # api_wings_tls.tpr smoke (the fixture cert in
                        # tests/fixtures/ has no CA chain), no-op for
                        # plain HTTP probes.
                        local code
                        code=$(curl -s -o /dev/null -w "%{http_code}" --max-time 5 -k "$probe_url" 2>/dev/null)
                        if [ -z "$code" ] || [ "$code" = "000" ]; then
                            probe_status="probe_failed_no_response"
                        elif ! kill -0 "$smoke_pid" 2>/dev/null; then
                            probe_status="server_died_after_probe"
                        fi
                    fi
                    kill -TERM "$smoke_pid" 2>/dev/null
                    wait "$smoke_pid" 2>/dev/null
                    if [ "$probe_status" = "ok" ]; then
                        if [ -n "$probe_url" ]; then
                            printf "Testing %s... ${GREEN}PASS (compile-only +smoke +probe)${NC}\n" "$example"
                        else
                            printf "Testing %s... ${GREEN}PASS (compile-only +smoke)${NC}\n" "$example"
                        fi
                    else
                        printf "Testing %s... ${RED}FAIL (smoke %s)${NC}\n" "$example" "$probe_status"
                        {
                            echo "----- smoke log: $example -----"
                            sed 's/^/    /' "$smoke_log" | head -n 40
                            echo "----- end log -----"
                        } > "$fail_dir/$name.log" 2>&1
                        rm -f "$smoke_log" "$out_path" "$out_path.ll" "$out_path.o" "$compile_log"
                        return 1
                    fi
                else
                    # Already exited; check status. Bash's $? after
                    # wait on a known-dead pid returns the exit status.
                    wait "$smoke_pid" 2>/dev/null
                    local smoke_rc=$?
                    if [ "$smoke_rc" = "0" ]; then
                        # Cleanly exited within 2s — usually a script
                        # that runs to completion and doesn't actually
                        # call listen(). That's still PASS.
                        printf "Testing %s... ${GREEN}PASS (compile-only +smoke)${NC}\n" "$example"
                    else
                        printf "Testing %s... ${RED}FAIL (smoke crashed, exit %s)${NC}\n" "$example" "$smoke_rc"
                        {
                            echo "----- smoke log: $example -----"
                            sed 's/^/    /' "$smoke_log" | head -n 40
                            echo "----- end log -----"
                        } > "$fail_dir/$name.log" 2>&1
                        rm -f "$smoke_log" "$out_path" "$out_path.ll" "$out_path.o" "$compile_log"
                        return 1
                    fi
                fi
                rm -f "$smoke_log"
                rm -f "$out_path" "$out_path.ll" "$out_path.o" "$compile_log"
                return 0
            fi
            if [ -f "$input_file" ]; then
                # Bu kosucu "yalnizca cikis kodunu" karsilastiriyor. Calisma
                # zamani hatasi cikis kodunu degistirmediginde bu KOR olur: bir
                # ornege `kk[999]` enjekte edildiginde suite "All tests passed!"
                # diyordu (R2, olculdu). Bir sure harness `TULPAR_STRICT_RUNTIME=1`
                # ile dil varsayilanindan siki kostu; FLIP'ten sonra (2026-09-10)
                # strict zaten varsayilan, o yuzden bayrak kaldirildi — iki
                # hakikat tablosu birakmamak icin.
                $TIMEOUT_CMD "./$out_path" < "$input_file" > /dev/null 2>&1
            else
                $TIMEOUT_CMD "./$out_path" > /dev/null 2>&1
            fi

            if [ $? -eq 0 ]; then
                printf "Testing %s... ${GREEN}PASS${NC}\n" "$example"
            else
                printf "Testing %s... ${RED}FAIL (execution)${NC}\n" "$example"
                echo "----- execution failed (non-zero exit): $example -----" \
                    > "$fail_dir/$name.log" 2>&1
                rm -f "$out_path" "$out_path.ll" "$out_path.o" "$compile_log"
                return 1
            fi
        elif [ $compile_rc -eq 124 ]; then
            printf "Testing %s... ${RED}FAIL (compile timeout >%ss)${NC}\n" "$example" "$compile_timeout"
            {
                echo "----- compile TIMEOUT (${compile_timeout}s): $example -----"
                echo "    The AOT compile was killed, not rejected — raise"
                echo "    TULPAR_COMPILE_TIMEOUT or lower TULPAR_TEST_JOBS if"
                echo "    this box is just heavily loaded."
                sed 's/^/    /' "$compile_log" | head -n 40
                echo "----- end log -----"
            } > "$fail_dir/$name.log" 2>&1
            rm -f "$out_path" "$out_path.ll" "$out_path.o" "$compile_log"
            return 1
        else
            printf "Testing %s... ${RED}FAIL (compilation)${NC}\n" "$example"
            {
                echo "----- compile log: $example -----"
                sed 's/^/    /' "$compile_log" | head -n 40
                echo "----- end log -----"
            } > "$fail_dir/$name.log" 2>&1
            rm -f "$out_path" "$out_path.ll" "$out_path.o" "$compile_log"
            return 1
        fi

        rm -f "$out_path" "$out_path.ll" "$out_path.o" "$compile_log"
        return 0
    }

    if [ -n "$TARGET" ]; then
        if [ ! -f "$TARGET" ]; then
            echo -e "${RED}ERROR: Test file '$TARGET' not found.${NC}"
            exit 1
        fi
        # Honor the COMPILE_ONLY list for single-file runs too, so a windowed
        # / blocking example (tame_*, arcade_*) is compiled but not executed.
        target_file=$(basename "$TARGET")
        compile_only=0
        for co_file in "${COMPILE_ONLY_TESTS[@]}"; do
            if [ "$target_file" = "$co_file" ]; then
                compile_only=1
                break
            fi
        done
        run_test "$TARGET" "$compile_only" || TEST_FAILED=1
    else
        # ------------------------------------------------------------------
        # Parallel example runner.
        #
        # Every example is an independent full AOT compile (LLVM codegen +
        # link against libtulpar_runtime.a, ~4s each) — 87 of them serially
        # was ~5m45s and made up 87% of the Linux CI job's wall time, while
        # macOS/Windows finished in under 3 minutes because they never run
        # this suite at all. The work is embarrassingly parallel:
        #   * each test's output binary is named after its unique source
        #     basename, so no two writes collide;
        #   * the only examples touching shared on-disk state use DIFFERENT
        #     files (08_file_io -> test_file.txt, 13_database -> test.db);
        #   * the compile-only server smokes each bind a distinct port,
        #     and their 34 x `sleep 2` startup waits now overlap instead of
        #     summing to ~68s of serial idling.
        # ------------------------------------------------------------------
        work_list=$(mktemp)
        for example in examples/*.tpr; do
            [ -f "$example" ] || continue

            example_file=$(basename "$example")
            skip=0
            for skip_file in "${SKIP_TESTS[@]}"; do
                if [ "$example_file" = "$skip_file" ]; then
                    skip=1
                    break
                fi
            done

            if [ $skip -eq 1 ]; then
                printf "SKIP: %s\n" "$example"
                continue
            fi

            compile_only=0
            for co_file in "${COMPILE_ONLY_TESTS[@]}"; do
                if [ "$example_file" = "$co_file" ]; then
                    compile_only=1
                    break
                fi
            done

            # "<path> <0|1>" per line; xargs -n2 hands both to one worker.
            # Example paths are plain [a-z0-9_]+.tpr (no spaces/quotes), so
            # xargs' default word splitting is safe here.
            printf '%s %s\n' "$example" "$compile_only" >> "$work_list"
        done

        # examples/en/ — İngilizce ikizler. Bu klasör HİÇ test edilmiyordu:
        # döngü yalnız `examples/*.tpr` üzerinde geziyordu, yani ikizler
        # kaynak dilden ayrışsa (yeniden adlandırılmış bir API, kaldırılmış
        # bir builtin) kimse görmezdi.
        #
        # Hepsi COMPILE-ONLY: ikizler oyun, yani pencere açıp kullanıcı
        # kapatana dek bloklarlar. Derlemeleri yine de gerçek kazanç —
        # ayrışmanın belirtisi zaten "derlenmiyor" oluyor.
        for example in examples/en/*.tpr; do
            [ -f "$example" ] || continue
            printf '%s %s\n' "$example" "1" >> "$work_list"
        done

        # Default to the machine's core count; TULPAR_TEST_JOBS overrides
        # (e.g. TULPAR_TEST_JOBS=1 to get the old serial behaviour back when
        # bisecting a flaky test).
        if [ -n "$TULPAR_TEST_JOBS" ]; then
            test_jobs="$TULPAR_TEST_JOBS"
        else
            test_jobs=$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)
        fi
        echo "Running $(wc -l < "$work_list") examples on $test_jobs parallel jobs..."
        echo ""

        # Workers are separate bash processes, so run_test + everything it
        # reads must be exported. FAIL_DIR is where they drop multi-line
        # failure detail (see run_test's header comment).
        export INPUT_DIR FAIL_DIR GREEN RED NC
        export -f run_test smoke_probe_for

        # xargs exits 123 if ANY worker exited non-zero — that is the
        # failure channel (a worker subshell cannot set TEST_FAILED).
        xargs -P "$test_jobs" -n 2 \
            bash -c 'run_test "$0" "$1"' < "$work_list" || TEST_FAILED=1
        rm -f "$work_list"

    fi

    # Dump the collected failure logs in a deterministic (sorted) order,
    # after the live PASS/FAIL lines — parallel workers can't print
    # multi-line blocks safely while others are still writing. Outside the
    # if/else on purpose: the single-file path funnels detail through
    # $FAIL_DIR too, and dumping only in the parallel branch silently threw
    # away the compile log for `./build.sh test <one-file>`.
    if [ -n "$(ls -A "$FAIL_DIR" 2>/dev/null)" ]; then
        echo ""
        echo -e "${RED}===== failure details =====${NC}"
        for log in "$FAIL_DIR"/*.log; do
            [ -f "$log" ] || continue
            cat "$log"
        done
    fi

    rm -rf "$FAIL_DIR"

    echo ""
    hw_end "ornekler"
    if [ $TEST_FAILED -ne 0 ]; then
        echo -e "${RED}Some tests failed!${NC}"
        exit 1
    fi

    echo -e "${GREEN}All tests passed!${NC}"
    exit 0
fi

# Build with CMake — wipe contents first so we always get a clean configure
echo "Building TulparLang..."
# Derleme en ağır faz; kaynak kullanımı burada da ölçülüyor.
hw_begin
echo "Preparing $BUILD_DIR (wiping contents)..."
rm -rf "$BUILD_DIR"
mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

cmake .. -DCMAKE_BUILD_TYPE=Release
if [ $? -ne 0 ]; then
    echo -e "${RED}ERROR: CMake configuration failed!${NC}"
    exit 1
fi

cmake --build . --config Release -j$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)
if [ $? -ne 0 ]; then
    echo -e "${RED}ERROR: Build failed!${NC}"
    exit 1
fi

# Copy executable
hw_end "derleme"
cp tulpar ../tulpar
# Copy the runtime archive next to the executable too. The AOT linker
# probes the directory of the running `tulpar` first, so leaving a stale
# (e.g. Windows/MinGW `.obj`) libtulpar_runtime.a in the repo root makes
# every `--aot` link fail with undefined `operator new` / `__mingw_*`
# references even though the build itself succeeded.
cp libtulpar_runtime.a ../libtulpar_runtime.a
# tame (2D oyun) arşivi de aynı sebeple köke kopyalanır — `import "tame"`
# eden programların linki bunu exe'nin yanında arar.
cp libtulpar_tame.a ../libtulpar_tame.a
cd ..

# Make executable
chmod +x tulpar

echo ""
echo -e "${GREEN}========================================${NC}"
echo -e "${GREEN}BUILD SUCCESSFUL!${NC}"
echo -e "${GREEN}========================================${NC}"
echo ""
echo "Executable: ./tulpar"
echo ""
echo "Usage:"
echo "  ./tulpar --aot file.tpr      - Compile to native binary"
echo "  ./tulpar --aot file.tpr out  - Compile with custom output name"
echo "  ./build.sh clean             - Clean build artifacts"
echo "  ./build.sh test              - Run all tests"
