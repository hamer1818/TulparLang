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

# Bir "smoke" alt surecini GUVENILIR sekilde oldurur.
#
# NEDEN AYRI BIR FONKSIYON: duz `kill -TERM $pid; wait $pid` Windows'ta
# ASILIYOR. MSYS2 sinyalleri yalniz MSYS'e baglI surecler icin taklit eder;
# `tulpar`in urettigi ikili NATIF bir Windows surecidir ve SIGTERM'i hic
# gormez. Sonuc olculdu (2026-09-19): `wait` donmedi, kosucu 50 dakika
# ilerlemedi ve o sirada baslatilmis 13 raylib penceresi ekranda ACIK kaldi
# (tam da DISPLAY temizliginin Linux'ta onledigi sey). Cozum: once POSIX
# sinyali (Linux/macOS yolu aynen korunur), sonra MSYS2'nin /proc/<pid>/winpid
# eslemesiyle taskkill — agac dahil (/T), cunku ikili alt surec baslatabilir.
_smoke_kill() {
    local pid="$1"
    kill -TERM "$pid" 2>/dev/null
    if [ -r "/proc/$pid/winpid" ]; then
        local wpid
        wpid=$(cat "/proc/$pid/winpid" 2>/dev/null)
        if [ -n "$wpid" ]; then
            MSYS2_ARG_CONV_EXCL='*' taskkill /F /T /PID "$wpid" >/dev/null 2>&1
        fi
    fi
    wait "$pid" 2>/dev/null
}

# Ornek bir PENCERE aciyor mu? raylib tabanli her sey (tame/arcade/scene3d) ve
# motor koprusu. Windows'ta bunlarin "2 saniyelik smoke"u GERCEK bir pencere
# acar: raylib'in Win32 yolunda headless kip YOK ve `DISPLAY=`/`WAYLAND_DISPLAY=`
# temizligi orada hicbir sey ifade etmez (o sadece X11/Wayland icindir).
_opens_window() {
    grep -qE '^[[:space:]]*import[[:space:]]+"(tame|arcade|scene3d|engine)"' "$1" 2>/dev/null
}

# --- WINDOWS KIPI -----------------------------------------------------------
# `./build.sh windows test` / `./build.sh windows suites`: aynı test
# koşucuları, Linux'tan çapraz derlenmiş `tulpar.exe` ile ve üretilen ikililer
# Wine altında koşarak. Liste TEK KAYNAKTA kalsın diye ayrı bir koşucu YOK —
# 3.13.0 öncesinde COMPILE_ONLY_TESTS hem burada hem run_tests.ps1'de duruyordu
# ve elle senkron tutulması gerekiyordu (CHANGELOG bunu açıkça yan fayda diye
# yazıyor). Aynı hatayı tekrarlamıyoruz: yalnız ÜÇ değişken değişiyor.
#   TULPAR_RUN  — derleyiciyi çağırma biçimi (host tarafı)
#   RUN_PREFIX  — üretilen ikiliyi çalıştırma biçimi
#   EXE_SUFFIX  — üretilen ikilinin uzantısı
# Ayrıca `TULPAR_BIN` MİSAFİR tarafa (Wine içindeki süreçlere) ihraç edilir:
# derleyiciyi alt süreç olarak çağıran testler (gramer_bosluklari) onu okur ve
# Windows'ta `./tulpar` çalışmaz — `build-windows\tulpar.exe` gerekir.
# Windows derlemesi: windows/build.sh (bkz. windows/README ve cmake/toolchain-mingw64.cmake).
TULPAR_RUN="./tulpar"
RUN_PREFIX=""
EXE_SUFFIX=""
WINDOWS_MODE=0

# --- YEREL WINDOWS (MSYS2 MINGW64 kabugu) -----------------------------------
# `./build.sh test|suites` GERCEK Windows'ta, Wine olmadan kosar. Yukaridaki
# `windows` kipinden tek farki RUN_PREFIX'in BOS kalmasi: ikili natif calisir.
#
# EXE_SUFFIX sart: AOT uretilen ikiliyi `<ad>.exe` diye yaziyor, kosucu ise
# uzantisiz ariyordu ve "ikili uretilmedi" diye DUSUYORDU — yani her ornek
# Windows'ta yanlis sebeple kirmizi veriyordu.
#
# NATIVE_EXE ayri tutuluyor cunku EXE_SUFFIX'i Wine kipi de set ediyor; bu
# degisken yalniz HOST ikilisinin (tulpar.exe) uzantisini anlatiyor ve
# derleme sonundaki kopyalama adiminda kullaniliyor.
NATIVE_EXE=""
NATIVE_WINDOWS=0
case "${OS}" in
    MINGW*|MSYS*|CYGWIN*)
        NATIVE_WINDOWS=1
        NATIVE_EXE=".exe"
        TULPAR_RUN="./tulpar.exe"
        EXE_SUFFIX=".exe"
        # gramer_bosluklari.test.tpr derleyiciyi ALT SUREC olarak cagiriyor
        # (`sys_run`), ve `sys_run` Windows'ta cmd.exe'ye gidiyor. cmd.exe `/`
        # ile baslayan bir sozcugu ANAHTAR sanir: `./tulpar.exe` orada
        # CALISMAZ (olculdu: cikis 1, `.\tulpar.exe` ve `tulpar.exe` cikis 0).
        # Wine kipi de bu yuzden ters bolu kullaniyor. TULPAR_BIN yoksa test
        # `./tulpar`a dusuyor — Windows'ta o dosya zaten YOK.
        export TULPAR_BIN='.\tulpar.exe'
        ;;
esac
if [ "$ACTION" = "windows" ]; then
    WINDOWS_MODE=1
    ACTION="$2"
    TARGET="$3"
    WIN_EXE="build-windows/tulpar.exe"
    if [ ! -f "$WIN_EXE" ]; then
        echo -e "${RED}HATA: $WIN_EXE yok — önce windows/build.sh çalıştırın.${NC}"
        exit 1
    fi
    if ! command -v wine &> /dev/null; then
        echo -e "${RED}HATA: wine yok (Windows ikilisi çalıştırılamaz).${NC}"
        exit 1
    fi
    # Wine ortamı: ayrı prefix, sysroot'un bin'i Windows PATH'inde (AOT link
    # adımı orada g++ arıyor), TULPAR_CC=g++.
    # shellcheck disable=SC1091
    . "$(dirname "$0")/windows/wine_env.sh" > /dev/null
    TULPAR_RUN="wine $WIN_EXE"
    RUN_PREFIX="wine"
    export TULPAR_BIN='build-windows\tulpar.exe'
    EXE_SUFFIX=".exe"
    echo -e "${YELLOW}Windows kipi: $TULPAR_RUN (üretilen ikililer Wine altında)${NC}"
fi

# Single platform-suffixed build directory (mirrors build.bat behaviour:
# contents are wiped on every build so a stale runtime archive never lingers).
case "${OS}" in
    Linux*)     BUILD_DIR="build-linux";;
    Darwin*)    BUILD_DIR="build-macos";;
    # YEREL WINDOWS (MSYS2 MINGW64): capraz derlemeyle AYNI dizin adi. Ad
    # yuk tasiyor — src/aot/aot_pipeline.cpp build_link_search_dirs()
    # Windows'ta once `build-windows` (ve `build-windows/Release`) deniyor.
    # Onceden burasi `*)` dalina dusup `build` seciyordu; arsiv yine de
    # bulunuyordu (add_dev("build") yedegi var) ama motor arsivleri
    # `build-windows/engine` yolundan aranidigi icin `import "engine"`
    # eden program linklenemiyordu.
    MINGW*|MSYS*|CYGWIN*) BUILD_DIR="build-windows";;
    *)          BUILD_DIR="build";;
esac
[ "$WINDOWS_MODE" = "1" ] && BUILD_DIR="build-windows"

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
    if [ "$WINDOWS_MODE" != "1" ] && [ ! -x "./tulpar" ]; then
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
    # WINDOWS KIPI — motor (engine/) Windows'a taşınmadı: `import "engine"` eden
    # paket link edilemez. FAIL değil ATLANMIŞ sayılır ama SESSİZCE değil.
    SUITE_SKIP=""
    if [ "$WINDOWS_MODE" = "1" ] && [ ! -f "build-windows/engine/libengine_core.a" ]; then
        SUITE_SKIP="engine_bridge.test.tpr"
    fi
    for suite in tests/*.test.tpr; do
        [ -f "$suite" ] || continue
        SUITE_N=$((SUITE_N + 1))
        name=$(basename "$suite")
        case " $SUITE_SKIP " in
            *" $name "*)
                printf "%-42s ${YELLOW}ATLANDI${NC} (motor bu yapıda derlenmedi)\n" "$name"
                continue
                ;;
        esac
        out=$(DISPLAY= $SUITE_TIMEOUT_CMD $TULPAR_RUN "$suite" 2>&1)
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
                # PAKET COKTU (FAIL satiri YOK). Burada en cok ihtiyac duyulan
                # iki sey CIKIS KODU ve ciktinin SONU — ikisi de eskiden
                # basilmiyordu ve 2026-09-16'da bir CI turu tam olarak bunun
                # yuzunden bosa gitti: macOS'ta engine_bridge ozetsiz dustu,
                # elimizde yalnizca "hata" gecen 8 satir vardi ve onlar da
                # kapanis raporunun ortasindan gelmisti, yani teshis icin
                # HICBIR SEY soylemiyordu.
                #
                # Cikis kodu sinifi TEK BASINA belirliyor: 139 = SIGSEGV,
                # 134 = abort, 124 = zaman asimi (timeout), 1 = normal hata.
                sig=""
                case $code in
                    124) sig=" (ZAMAN ASIMI — $SUITE_TIMEOUT_CMD)";;
                    134) sig=" (SIGABRT — abort/assert)";;
                    139) sig=" (SIGSEGV — bellek erisimi)";;
                    136) sig=" (SIGFPE)";;
                    *) ;;
                esac
                echo "    cikis kodu $code$sig; FAIL satiri YOK -> paket ozetine varmadan oldu."
                echo "    --- ciktinin son 12 satiri ---"
                echo "$out" | tail -12 | sed 's/^/    /'
                # Motor koprusu (teng_init) cokme raporcusunu KURUYOR ve
                # rapor dosyasini CWD'ye yaziyor — ama onu kimse basmiyordu,
                # yani saha kosumunda yigin izi uretilip cope gidiyordu.
                for cr in crash_*.txt; do
                    [ -f "$cr" ] || continue
                    echo "    --- cokme raporu $cr ---"
                    sed 's/^/    /' "$cr" | awk 'NR<=25'
                    rm -f "$cr"
                done
            fi
            SUITE_FAILED=1
        elif [ -z "$summary" ]; then
            printf "%-42s ${RED}FAIL${NC} (test_summary() cagirmiyor — cikis kodu uretmiyor)\n" "$name"
            SUITE_FAILED=1
        else
            printf "%-42s ${GREEN}PASS${NC} %s\n" "$name" "$summary"
        fi
    done
    # ENGINE (Faz 0) birim testleri — C++ L0/L1 (engine/). macOS arm64 kosumu
    # AArch64 fiber gecisini, Linux x86_64 kosumu SysV gecisini sinar; A2
    # kapisi (karede 0 ayirma, pozitif kontrollu) ve crash reporter her
    # build'de burada kosar. Ikili yoksa bu bir build sapmasidir, atlama
    # degil: kirmizi. Zaman olcumleri [profiler]/[bilgi] satirlariyla BILGI
    # olarak basilir, karar vermez (Tuzaklar 1l/1p).
    # CI `build/` icinde derliyor (workflow: mkdir build; cmake ..), yerel
    # build.sh ise build-<platform>/; ikisine de bak. Bulunamazsa KIRMIZI.
    #
    # WINDOWS KIPINDE KOSULMAZ: motor Windows hedefinin parçası değil (CMake'te
    # `if(NOT WIN32)`), yani burada koşulacak olan HOST'un Linux ikilisidir —
    # Windows hakkında hiçbir şey ölçmez ama çıkış kodunu boyar. Görünür atlama.
    ENGINE_TESTS=""
    for d in "$BUILD_DIR" build build-linux build-macos; do
        if [ -x "$d/engine/engine_tests" ]; then ENGINE_TESTS="$d/engine/engine_tests"; break; fi
    done
    SUITE_N=$((SUITE_N + 1))
    # Windows kipinde MOTORUN KENDI WINDOWS IKILISI koşulur (varsa): host'un
    # Linux ikilisini koşmak Windows hakkında hiçbir şey ölçmezdi.
    if [ "$WINDOWS_MODE" = "1" ]; then
        if [ -x "build-windows/engine/engine_tests.exe" ]; then
            ENGINE_TESTS="build-windows/engine/engine_tests.exe"
        elif [ -x "build-win-engine/engine/engine_tests.exe" ]; then
            ENGINE_TESTS="build-win-engine/engine/engine_tests.exe"
        else
            ENGINE_TESTS=""
        fi
    fi
    if [ "$WINDOWS_MODE" = "1" ] && [ -z "$ENGINE_TESTS" ]; then
        printf "%-42s ${YELLOW}ATLANDI${NC} (motor bu yapıda derlenmedi: -DTULPAR_WIN_ENGINE=ON)\n" "engine_tests"
    elif [ -z "$ENGINE_TESTS" ]; then
        ENGINE_TESTS="$BUILD_DIR/engine/engine_tests"
        printf "%-42s ${RED}FAIL${NC} (ikili yok: %s — engine hedefi derlenmedi mi?)\n" "engine_tests" "$ENGINE_TESTS"
        SUITE_FAILED=1
    else
        out=$(DISPLAY= $SUITE_TIMEOUT_CMD $RUN_PREFIX "$ENGINE_TESTS" 2>&1); code=$?
        summary=$(echo "$out" | grep -E '^engine tests:' | tail -1)
        if [ $code -ne 0 ] || [ -z "$summary" ]; then
            printf "%-42s ${RED}FAIL${NC} %s\n" "engine_tests" "$summary"
            # Teshis icin GPU/yetenek satirlari da basilir (ilk lavapipe
            # kosumunda hangi cihazin bulundugu gorunmedi).
            # Dusen kosumda TUM [bilgi] satirlari: teshis icin gereken sayilar
            # (cihaz acilamadi sebebi, altin ozet farki) burada.
            # Once DUSEN kontroller (hepsi; 28 satir siniri bunlari kesiyordu, CI'da
            # hangi testin dustugu gorunmuyordu 2026-09-14), sonra bilgi satirlari.
            echo "$out" | grep -E 'FAIL|FATAL|COKME' | sed 's/^/    /'
            echo "$out" | grep -E 'ATLANDI|\[bilgi\]' | awk 'NR<=80' | sed 's/^/    /'
            SUITE_FAILED=1
        else
            printf "%-42s ${GREEN}PASS${NC} %s\n" "engine_tests" "$summary"
            echo "$out" | grep -E '^\[profiler\]|\[bilgi\]|ATLANDI' | sed 's/^/    /'
        fi
    fi
    echo ""
    if [ $SUITE_FAILED -ne 0 ]; then
        hw_end "suites"
        echo -e "${RED}Some suites failed!${NC} ($SUITE_N paket)"
        exit 1
    fi
    # Builtin tablosu ↔ codegen ↔ LSP tutarlılık denetimi (derleme gerektirmez).
    if command -v python3 >/dev/null 2>&1; then
        echo ""
        # GÖRÜNÜR + SAYILAN KAPI ATLAMALARI. `engine/` artık ayrı bir git
        # submodule; recursive klonlanmamış bir ağaçta motora bakan kapıların
        # ölçecek şeyi YOKTUR. İki yanlış yanıt var: yokluğu kusur sayıp
        # herkesi sahte kırmızıya boğmak, ya da sessizce `return 0` deyip
        # yeşile yazmak. Doğrusu ortası: atlama ekrana yazılır, SAYILIR ve
        # süitin sonunda tekrar söylenir — "koşmayan kapı YEŞİL DEĞİLDİR".
        GATE_SKIPS=0
        GATE_SKIP_NAMES=""
        if ! python3 tests/builtin_audit.py; then
            echo -e "${RED}Builtin denetimi basarisiz!${NC}"
            exit 1
        fi
        # KAPILARIN KENDI SELF-TESTI (kural #10'un kalici hali).
        # Kaynak tarayan kapilar, KAPSAMLARINI kaybettiklerinde temiz bir
        # agacta da "temiz" derler — duyarlilik degil KAPSAM kaybi, ve bu
        # sessizdir. 2026-09-11 denetiminde iki kapinin ikisi de tam bu
        # durumdaydi. Her kapi artik yakalaMASI gereken kacis bicimlerinin
        # ve gecirMESI gereken mesru bicimlerin tablosunu tasiyor; tablo
        # her kosumda dogrulaniyor. Yeni bir kacis bulundugunda once
        # tabloya eklenir (kirmizi verir), sonra desen duzeltilir.
        if ! python3 tests/source_gates.py --selftest; then
            echo -e "${RED}Kapi self-testi basarisiz!${NC}"
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
        # Motor KÖPRÜSÜ bindingleri. Motor artık AYRI bir depo (submodule), yani
        # üreteç (engine/tools/gen_engine_bindings.py içindeki SPEC tablosu) ile
        # bu depodaki DÖRT üretilmiş dosya bağımsız hareket edebiliyor. Tek
        # ağaçken üreteci koşmamak zaten derlemeyi kırıyordu; artık motor
        # tarafında imza değişip dosyalar tazelenmediğinde HER ŞEY YEŞİL kalıyor
        # ve derleyici eski ABI ile derliyor — Tuzaklar 8aq'nın kardeşi, tek
        # farkı sınırın arşiv değil DEPO olması.
        #
        # Aşağıdaki dist_archive_audit yalnız ad + arite bakıyor: parametre TİPİ
        # ya da dönüş tipi değişince o YEŞİL kalır. Bu kapı üretileni depodakiyle
        # BAYT BAYT karşılaştırdığı için o sınıfı da görüyor. Motor klonlu
        # değilse GÖRÜNÜR biçimde atlıyor (derleyici tek başına derlenebilmeli).
        # ÖNCE koşuyor: dist_archive_audit beklentisini aynı SPEC'ten türetiyor,
        # dosyalar bayatsa sebebi önce adıyla söylensin.
        if ! python3 tests/engine_bindings_freshness.py; then
            echo -e "${RED}Motor binding tazelik denetimi basarisiz!${NC}"
            echo -e "${YELLOW}   tazele: bash tools/engine_bindings_sync.sh${NC}"
            exit 1
        fi
        # Önceden derlenmiş web/Android arşivleri ↔ builtin tablosu.
        # Aşağıdaki zaman damgası denetiminin YERİNE geçiyor (o yalnız
        # "kaynak daha yeni" diyebiliyordu); bu, EKSİK SEMBOLLERİ adıyla
        # sayıyor. Ayrım işe yaradı: wasm/dist beş gün bayat kaldı, scene3d'nin
        # her web derlemesi link'te patlıyordu ve sarı satırı kimse okumadı.
        # Uc aileyi de sayiyor: tame (aot_tm_*), cekirdek runtime ve motor
        # koprusu (aot_eng_* + teng_*, 156 builtin). Ucuncusu 2026-09-15'e
        # kadar ELENIYORDU: denetim "temiz" derken `import "engine"` eden her
        # android derlemesi bayat arsivde link'te patlayabilirdi.
        if ! python3 tests/dist_archive_audit.py; then
            echo -e "${RED}Dist arsiv denetimi basarisiz!${NC}"
            exit 1
        fi
        # PAKET BOYUTU + SPIR-V TAZELIK + ACILIS SURESI.
        #
        # Ucu de "sessizce bozulan" sinifindan ve hicbiri otomasyonda degildi:
        #  • boyut  — mobil kurulum butcesi ancak OLCULUP esige baglanirsa butce.
        #  • SPIR-V — GLSL kaynagi degisip *_spv.h yeniden uretilmezse derleme
        #             YESIL kalir ve GPU ESKI shader'i kosturur (sessiz yanlis
        #             sonuc; ayrica "runtime'da shader derlemesi yok" kapisinin
        #             kaniti uretilmis basligin depoda TAZE durmasidir).
        #  • acilis — motor kurulumu uzarsa kimse fark etmez, oyun yine calisir.
        #
        # Cikis kodu KAPI: 0 disi ise suite duser. Atlamalar (emsdk/NDK/GPU yok)
        # sebebiyle birlikte basiliyor, gorunmez `return` ile degil.
        if ! python3 tests/paket_boyut_audit.py; then
            echo -e "${RED}Paket boyutu/SPIR-V/acilis denetimi basarisiz!${NC}"
            exit 1
        fi
        # BICIMLENDIRICI denetimi: `tulpar fmt` gecerli kaynagi DERLENMEYEN
        # hale getirebiliyor (olculdu: `1.5e-8` -> `1.5e - 8`, `a <<= 1` ->
        # `a < <= 1`). Idempotans TEK BASINA yetmez — bozuk bir ciktiyi ikinci
        # kez bicimlendirmek ayni bozuk ciktiyi verir, yani "kararli" ile
        # "dogru" karisir. Bu yuzden asil olcut: bicimlenmis metnin ayristirma
        # hatasi sayisi ONCESINE gore ARTMAMALI.
        if ! python3 tests/fmt_audit.py; then
            echo -e "${RED}Bicimlendirici denetimi basarisiz!${NC}"
            exit 1
        fi
        # CPU-GPU YERLESIM denetimi: shader'in std140/std430 yerlesimi ile C++
        # struct'inin bayt yerlesimi ayrisirsa hicbir sey kizarmaz — GPU baska
        # bir ofsetten okur, goruntu "biraz yanlis" olur. Yerlesim SPIR-V'den
        # (glslc'nin gercekte urettigi ofsetler) okunuyor, C++ tarafi da derleyiciye
        # sorduruluyor; iki taraf da elle hesaplanmiyor. `static_assert(sizeof)`
        # bu sinifin yalniz YARISINI gorur: ayni boyutta alan sirasi degisimi
        # ondan gecer (olculdu), bu denetimden gecmez.
        # ÇIKIŞ 3 = "ölçecek şey yok" (betiğin kendi sözleşmesi: 0 temiz /
        # 1 uyuşmazlık / 2 kapı bozuk / 3 araç yok). `engine/` submodule'u
        # klonlanmamışsa hem ölçülen shader tarafı hem ölçen araç yoktur;
        # eskiden bu dal sessizce 0 dönüyordu, yani kapı hiçbir şey ölçmeden
        # yeşile yazılıyordu. Artık ATLAMA olarak sayılıyor: yeşil değil, ama
        # kırmızı da değil — çünkü derleyici motor olmadan da derlenip test
        # edilebilmeli.
        python3 tests/layout_audit.py
        LAYOUT_RC=$?
        if [ $LAYOUT_RC -eq 3 ]; then
            GATE_SKIPS=$((GATE_SKIPS + 1))
            GATE_SKIP_NAMES="$GATE_SKIP_NAMES layout_audit"
            echo -e "${YELLOW}CPU-GPU yerlesim denetimi ATLANDI${NC} — olculecek/olcen taraf yok (engine/ submodule'u baslatilmamis olabilir: git submodule update --init --recursive)"
        elif [ $LAYOUT_RC -ne 0 ]; then
            echo -e "${RED}CPU-GPU yerlesim denetimi basarisiz!${NC}"
            exit 1
        fi
        # Faz 8 fizibilite kapisi: Tulpar sozdiziminin GPU alt kumesi (.tprs)
        # -> GLSL -> SPIR-V cevirisi hala depodaki *_spv.h ile BAYT AYNI mi.
        # Bayt esitligi secildi cunku `glslc -O` ciktisi isim bagimsiz ve
        # yeniden uretilebilir (olculdu) — yani "benzer" degil "ayni" diyebiliyoruz.
        # glslc yoksa GORUNUR atlar (CI'da glslc yok, bu bilinen ve yazili).
        # Referans GLSL degismisse o dosya gorunur atlanir: ne yanlis suclama,
        # ne olcmeden gecme. Prototip oldugu icin atlama suite'i DUSURMEZ,
        # ama gercek bir ayrisma (cikis 1) duSurur.
        if ! python3 tests/faz8_shader_audit.py; then
            echo -e "${RED}Faz 8 shader cevirici denetimi basarisiz!${NC}"
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
        # Desen ve ORNEK TABLOSU artik tests/source_gates.py'de — tek tanim.
        # Neden tasindi: bu kapi burada satir ici bir grep'ti ve iki tur ust
        # uste KOR CIKTI. Once satir basina bagliydi (`... { TypedValue
        # bozuk; }` kaciyordu), sonra tek degisken adina bagliydi
        # (`TypedValue a, b;` ve `TypedValue d[2];` kaciyordu — 2026-09-11
        # bagimsiz denetimi olctu). Her seferinde ELLE enjeksiyon yapilip
        # duzeltildi ve o kanit buharlasti; hicbir sey onu tekrar kosmadi.
        # source_gates.py kaniti kalici kiliyor: kapi her kosumda KENDI
        # kacis-bicimi tablosuna karsi sinaniyor (`--selftest`), yani desen
        # kapsamini kaybederse temiz kaynakta bile KIRMIZI verir.
        if ! python3 tests/source_gates.py --gate="TypedValue ilklendirmesi"; then
            exit 1
        fi
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
        # OTOMASYON DISI KALMIS IKI SONDA — artik iceride (DOGRULAMA D.4).
        #
        # Ikisi de gercek soket kullaniyor ve sozlesmenin YAZILI olup
        # SINANMAYAN yarisini kapatiyor: `ws_masked_client_smoke.py` RFC 6455'in
        # istemci->sunucu MASKELI yonunu (ornekler yalniz maskesiz yonu
        # kapsiyordu), `wings_tls_smoke.py` ise `wings_tls` dinleyicisini.
        #
        # ⚠ NEDEN GECIKTI VE NE BULUNDU: bunlar "elle kosulur" diye duruyordu
        # ve 2026-09-11 denetiminde ILK KEZ kosuldugunda `wings_tls_smoke.py`
        # LINUX'TA TAMAMEN BOZUK cikti — ikili arayicisi listenin basinda
        # `tulpar.exe` ariyordu ve depo kokundeki 3 ay bayat PE32+ artigi
        # seciliyordu. Wine onu calistirmaya kalkiyor, eksik DLL ile duusuyor,
        # sonda da "FAIL: build failed" diyordu: TLS hakkinda bir hukum, aslinda
        # eksik bir libcrypto DLL'inden. Kosmayan bir sonda bozuldugunu
        # soylemez; kosturuldugu gun SINADIGI SEYI suclar.
        #
        # Guvenilirlik olculdu (6/6 temiz, her ikisi de) — bu oturumun dersi
        # zaten "ag/zaman duyarli bir testi once olc, sonra sert kapiya koy".
        #
        # SKIP GORUNUR: `wings_tls_smoke.py` openssl yoksa ya da ikili TLS'siz
        # derlendiyse exit 0 ile ATLIYOR. Sessiz gecerse kapi bos yere yesil
        # verir, o yuzden sondanin kendi satiri basiliyor ve SKIP sari yazilir.
        for smoke in ws_masked_client_smoke wings_tls_smoke; do
            SMOKE_OUT=$(DISPLAY= WAYLAND_DISPLAY= python3 "tests/$smoke.py" 2>&1)
            SMOKE_RC=$?
            SMOKE_LAST=$(printf '%s\n' "$SMOKE_OUT" | tail -1)
            if [ "$SMOKE_RC" -ne 0 ]; then
                echo -e "${RED}$smoke basarisiz!${NC}"
                printf '%s\n' "$SMOKE_OUT" | tail -12 | sed 's/^/  /'
                exit 1
            fi
            case "$SMOKE_LAST" in
                SKIP*) echo -e "${YELLOW}$smoke ATLANDI${NC} — $SMOKE_LAST" ;;
                *)     echo -e "${GREEN}$SMOKE_LAST${NC}" ;;
            esac
        done
        # Atlamalar TEKRAR sayılıyor: yukarıda tek satır olarak akıp giden bir
        # ATLANDI, yüzlerce satırın ardından "hepsi geçti" okuyan kişiye
        # ulaşmaz. Sayı sıfırdan büyükse kapsamın eksik olduğu burada yazılı.
        if [ "${GATE_SKIPS:-0}" -gt 0 ]; then
            echo -e "${YELLOW}kapi atlamalari: $GATE_SKIPS${NC} —$GATE_SKIP_NAMES (olculmedi; bu kapilar YESIL SAYILMAZ)"
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
    # ⚠ DEJENERE OLCUM YESIL VERMEZ.
    #
    # Bu kapi once her kolu ayri olcup acilisi cikariyordu ve cikarma NEGATIFE
    # dustugunde sonucu 1e KELEPCELIYORDU. Kelepcelenmis 1, `off_w > on_w * 2`
    # esiginden rahatca gecer. Olculdu (2026-09-11, CI macOS arm64):
    #
    #     ozyineleme zinciri calisiyor (is: 9835us -> 1us, acilis ~16329us)
    #
    # `-> 1us` kelepcenin kendisi: macOS'ta surec acilisi ~16 ms ve zincirli
    # fib(32) toplam suresi onun altinda kaliyor, yani is payi OLCULEMIYOR.
    # Kapi `9835 > 2` diye YESIL veriyordu — zincirli kol hic olculmemisti.
    # Linux'ta ayni kapi gercek olcuyor (4034 -> 270).
    #
    # Artik: olcum tur-esli (tests/perf_pair.py, bkz. ackermann kapisi) ve
    # kelepcelenen tur SAYILIYOR. Kelepce varsa kapi yesil IDDIA ETMEZ; ne
    # olculemedigini ve neyin hala kapsadigini soyler. Bu bir GERILEME degil,
    # platformun olcum siniri — o yuzden kirmizi degil, GORUNUR sari.
    #
    # ⚠ TAKIP — sari dal macOS'ta KALICI bir bosluk. Iki cikis yolu var:
    #   (a) N'i buyutmek: fib(40) zincirli kolu macOS acilisinin ustune
    #       cikarir ama ZINCIRSIZ kol ~1,4 sn olur; 5 tur x 2 kol = ~7 sn
    #       CI suresi, yalnizca sariyi yesile cevirmek icin.
    #   (b) DOGRU cozum: olcumu SUREC ICINE almak — program kendi isini
    #       `time_ms()` ile olcup bassin. O zaman cikarilacak acilis KALMAZ,
    #       dejenerasyon da imkansiz olur. perf_pair.py duvar saati olctugu
    #       icin bu ayri bir yardimci ister; yapilana kadar sari dal durur.
    SR_RES=$(python3 tests/perf_pair.py "$SR_TMP/fib_on" "$SR_TMP/fib_off" 32 1 5)
    SR_RATIO=$(echo "$SR_RES" | cut -d' ' -f1)
    SR_ON_W=$(echo "$SR_RES" | cut -d' ' -f2)
    SR_OFF_W=$(echo "$SR_RES" | cut -d' ' -f3)
    SR_DEG=$(echo "$SR_RES" | cut -d' ' -f4)
    if [ -z "$SR_RATIO" ]; then
        echo -e "${RED}Ozyineleme zinciri kapisi OLCUM URETEMEDI${NC}"
        rm -rf "$SR_TMP"; exit 1
    fi
    if [ "$SR_DEG" -gt 0 ]; then
        echo -e "${YELLOW}ozyineleme ZAMAN kapisi olcemedi${NC} — 5 turun ${SR_DEG} tanesinde is payi surec acilisinin altinda kaldi (bu platformda fib(32) cok hizli)."
        echo "  Bu bir gerileme DEGIL, olcum siniri. Zincirin VARLIGINI yukaridaki IR"
        echo "  kapisi (@fib icindeki oz-cagri sayisi) zaten dogruladi; kazanc olcusu burada yok."
    elif [ "$SR_RATIO" -le 50 ]; then
        echo -e "${GREEN}ozyineleme zinciri calisiyor${NC} (is: ${SR_OFF_W}us -> ${SR_ON_W}us, oran %${SR_RATIO}, acilis cikarildi)"
    else
        echo -e "${RED}Ozyineleme zinciri KAZANC VERMIYOR — klonlar satir ici alinmiyor!${NC}"
        echo "  is payi: zincirsiz=${SR_OFF_W}us zincirli=${SR_ON_W}us (en az 2 kat bekleniyor)"
        echo "  medyan tur, oran %${SR_RATIO} (esik: zincirli <= zincirsizin yarisi)"
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
    #
    # ⚠ 2026-09-13: KAPI DUVAR SAATINDEN IR DUZEYINE INDI. Hikaye:
    # Kapi K=1 zincirli ackermann'in zincirsizden <= %125 olmasini duvar
    # saatiyle olcuyordu ve CI Linux'ta "bazen" dusuyordu. Iki kez gurultu
    # sanildi ve iki kez olcum duzeltildi (tur eslemesi, N=9 -> 11); ikisi de
    # gerekliydi ama kirmizi bitmedi. Ucuncu bakista sonuc [makine] satiriyla
    # eslestirilince gurultu OLMADIGI cikti — KOSUCUNUN CPU'SU sonucu
    # belirliyordu (ayni ikili: ccache %100 isabet, ayni LLVM 18.1.3, ayni
    # imaj; 8/8 kosumda CPU modeli sonucu belirledi):
    #
    #     EPYC 9V74 (Zen 4)   %125 / %126 / %134 / %149  <- 4/4 kirmizi/sinirda
    #     EPYC 7763 (Zen 3)   %107 / %108 / %110
    #     EPYC 9V45 (Zen 5)   %66
    #     9800X3D  (Zen 5)    %87-93 (yerel)
    #     Apple arm64         %62
    #
    # Yani kapi HAKLIYDI: K=1 zincir, ic ice oz-cagri seklini Zen 4'te %49
    # yavaslatiyor. Ilkeye gore bu HATA ve cozumu kapinin esigi degil,
    # derleyicinin karlilik modeli: ic ice oz-cagri sekli artik
    # ZINCIRLENMIYOR (llvm_backend.cpp, selfrec_scan `nested`). Kuralin
    # bedeli Zen 5 / arm64'teki %9-38 kazanc — olculdu, kabul edildi
    # (bkz. Tuzaklar 1p).
    #
    # Kapi artik uc ayakli ve ilk ikisi BELIRLENIMLI (makineden bagimsiz):
    #   1) varsayilan derlemede @ack'in oz-cagri sayisi zincirsizle AYNI
    #      -> sekil kurali yerinde (kalkarsa 3 > 1, kirmizi)
    #   2) POZITIF KONTROL: TULPAR_SELFREC_DEPTH=1 ile zorlaninca oz-cagri
    #      sayisi ARTIYOR -> "zincirlenmedi" KURAL yuzunden, kaza eseri degil.
    #      (ack native yoldan dusse ya da dugum butcesini assa 1. ayak yine
    #      gecerdi ama hicbir sey sinanmamis olurdu — bu ayak onu yakalar.)
    #   3) cikti dogrulugu: varsayilan ve zorlanmis ikili ayni sonucu verir.
    # Duvar saati OLCUMU KALDI ama KARAR VERMIYOR: zorla-K=1 / zincirsiz
    # orani `[bilgi]` satiri olarak makine adiyla basilir. Sekil kurali bes
    # CPU'luk veriyle kondu; bu satir her CI kosumunda veri setini buyutur —
    # kural gozden gecirilecekse kanit orada birikir. Karar vermedigi icin
    # kosucu piyangosu CI'i kirmaz.
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
    # ar_emit <ad> [ENV=deger ...]: ikili $AR_TMP/<ad>, IR $AR_TMP/<ad>.ll
    ar_emit() {
        local name="$1"; shift
        local d; d=$(mktemp -d)
        env "$@" TULPAR_AOT_EMIT_LL=1 ./tulpar build "$AR_TMP/ack.tpr" "$d/$name" >/dev/null 2>&1
        local ll; ll=$(ls "$d"/*.ll 2>/dev/null | head -1)
        [ -n "$ll" ] && mv "$ll" "$AR_TMP/$name.ll"
        [ -x "$d/$name" ] && mv "$d/$name" "$AR_TMP/$name"
        rm -rf "$d"
    }
    ar_emit ack_on
    ar_emit ack_off TULPAR_NO_SELFREC=1
    ar_emit ack_force TULPAR_SELFREC_DEPTH=1
    for v in ack_on ack_off ack_force; do
        if [ ! -x "$AR_TMP/$v" ] || [ ! -f "$AR_TMP/$v.ll" ]; then
            echo -e "${RED}Gerileme kapisi icin ackermann ikilisi/IR uretilemedi: $v${NC}"
            rm -rf "$AR_TMP"; exit 1
        fi
    done
    ar_selfcalls() { awk '/^define .*@ack\(/,/^}/' "$1" | grep -c 'call .*@ack'; }
    AR_C_ON=$(ar_selfcalls "$AR_TMP/ack_on.ll")
    AR_C_OFF=$(ar_selfcalls "$AR_TMP/ack_off.ll")
    AR_C_FORCE=$(ar_selfcalls "$AR_TMP/ack_force.ll")
    if [ "$AR_C_OFF" -lt 1 ]; then
        echo -e "${RED}Ackermann kapisi IR'de @ack'i bulamadi — kapi hicbir sey sinamiyor!${NC}"
        echo "  zincirsiz .ll'de @ack icindeki oz-cagri: $AR_C_OFF (en az 1 bekleniyor)"
        rm -rf "$AR_TMP"; exit 1
    fi
    if [ "$AR_C_ON" -ne "$AR_C_OFF" ]; then
        echo -e "${RED}IC ICE oz-cagri sekli ZINCIRLENDI — sekil kurali kalkmis (Zen 4'te %49 gerileme)!${NC}"
        echo "  @ack icindeki oz-cagri: varsayilan=$AR_C_ON zincirsiz=$AR_C_OFF (AYNI olmali)"
        rm -rf "$AR_TMP"; exit 1
    fi
    if [ "$AR_C_FORCE" -le "$AR_C_OFF" ]; then
        echo -e "${RED}Ackermann kapisinin POZITIF KONTROLU dustu — zorla K=1 bile zincirlemiyor!${NC}"
        echo "  @ack icindeki oz-cagri: zorla=$AR_C_FORCE zincirsiz=$AR_C_OFF (zorla DAHA COK olmali)"
        echo "  ack zincir adayi bile degil (native yoldan dusmus ya da dugum butcesi asilmis);"
        echo "  1. ayak bosa gecti, sekil kurali SINANMADI."
        rm -rf "$AR_TMP"; exit 1
    fi
    AR_O1=$(SR_N=11 "$AR_TMP/ack_on"); AR_O2=$(SR_N=11 "$AR_TMP/ack_force")
    if [ "$AR_O1" != "$AR_O2" ] || [ "$AR_O1" != "16381" ]; then
        echo -e "${RED}Zincir ackermann SONUCUNU DEGISTIRDI! varsayilan=$AR_O1 zorla=$AR_O2 beklenen=16381${NC}"
        rm -rf "$AR_TMP"; exit 1
    fi
    echo -e "${GREEN}ic ice oz-cagri sekli zincirlenmiyor${NC} (@ack oz-cagri: varsayilan=$AR_C_ON zincirsiz=$AR_C_OFF zorla-K=1=$AR_C_FORCE; cikti 16381)"
    # [bilgi] — KARAR YOK. Zorla-K=1 / zincirsiz orani, makine adiyla (bkz. yukari).
    AR_RES=$(python3 tests/perf_pair.py "$AR_TMP/ack_force" "$AR_TMP/ack_off" 11 1 5)
    AR_CPU=$(awk -F': ' '/^model name/{print $2; exit}' /proc/cpuinfo 2>/dev/null)
    [ -z "$AR_CPU" ] && AR_CPU=$(sysctl -n machdep.cpu.brand_string 2>/dev/null)
    [ -z "$AR_CPU" ] && AR_CPU=$(uname -m)
    echo "  [bilgi] zorla zincirli ackermann bu makinede: oran %$(echo "$AR_RES" | cut -d' ' -f1) (zorla=$(echo "$AR_RES" | cut -d' ' -f2)us zincirsiz=$(echo "$AR_RES" | cut -d' ' -f3)us) — $AR_CPU"
    rm -rf "$AR_TMP"

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
    # Desen, yetkili-kapi esigi ve ORNEK TABLOSU tests/source_gates.py'de.
    # Buradaki satir ici surum `\bprintf\(` ariyordu ve `\b` yuzunden butun
    # fprintf'leri eliyordu: `fprintf(stdout, "... Hatasi ...")` ve
    # `puts("... Hatasi ...")` kapidan geciyordu (2026-09-11 denetimi). Ikisi
    # de tam olarak bu kapinin yasakladigi seyi yapar. Artik her kosumda
    # kacis bicimleri tablosuna karsi sinaniyor.
    if ! python3 tests/source_gates.py --gate="tani tek kapi"; then
        exit 1
    fi
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
    # SESSIZ KOSUM. Ornek kosucusu pencere acan oyunlari 2 saniyelik bir
    # "smoke" ile GERCEKTEN calistiriyor; arcade/tame oyunlari raylib ses
    # aygitini acip carpisma/skor/olum sesleri caliyor. Otomatik bir kosumun
    # makinenin basindaki insana ses dinletmesi icin hicbir sebep yok
    # (kullanici 2026-09-16'da bildirdi). Aygit YINE aciliyor — kapanmasi
    # kapsamayi sessizce dusururdu, cunku o zaman `load_sound` -1 doner ve ses
    # yolu hic kosmaz; yalniz ana seviye 0'a cekiliyor.
    # Duymak icin: TULPAR_TAME_MUTE=0 ./build.sh test
    export TULPAR_TAME_MUTE="${TULPAR_TAME_MUTE:-1}"
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
    SKIP_REASON=""
    # WINDOWS KIPI — motor (engine/) henüz Windows'a taşınmadı: `import "engine"`
    # eden örnekler link edilemez (libengine_*.a yok, CMake'te WIN32'de kapalı).
    # Bunlar FAIL değil ATLANMIŞ sayılır, ama SESSİZCE değil: kapı neyi
    # ölçmediğini söylemeli. Motorun Windows portu bitince bu blok kalkacak.
    # Motor arşivleri YOKSA atla (TULPAR_WIN_ENGINE=OFF ile derlenmiş yapı);
    # varsa örnekler normal koşar — Windows motor portu tamamlandı.
    if [ "$WINDOWS_MODE" = "1" ] && [ ! -f "build-windows/engine/libengine_core.a" ]; then
        SKIP_TESTS+=("engine_ilk_oyun.tpr" "engine_arena.tpr" "engine_aksiyon.tpr")
        SKIP_REASON=" (motor bu yapıda derlenmedi: -DTULPAR_WIN_ENGINE=ON ile derleyin)"
    fi
    # Compile-only smoke tests: server/listener examples that block on
    # listen()/api_run(), plus utils.tpr (module-only — has no top-level
    # program, but we still verify it parses/lowers). We verify the build
    # succeeds (catches regressions in the embedded server/router/api
    # stdlib path) but do not run the binary.
    COMPILE_ONLY_TESTS=("engine_ilk_oyun.tpr" "engine_arena.tpr" "engine_aksiyon.tpr" "09_socket_simple.tpr" "09_socket_server.tpr" \
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
        # Windows kipinde üretilen ikili <name>.exe; nesne/IR dosyaları yine
        # <name>.o / <name>.ll (uzantı yalnız çalıştırılabilirde).
        local out_base="$name"
        local out_path="$name$EXE_SUFFIX"
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
        $COMPILE_TIMEOUT_CMD $TULPAR_RUN --aot "$example" > "$compile_log" 2>&1
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
                # WINDOWS: pencere acan ornegin smoke'u KOSULMAZ. Linux'ta
                # DISPLAY temizligi raylib'i baslatmiyor ve InitWindow yamasi
                # zarifce exit 0 veriyor; Windows'ta oyle bir kapi YOK, ikili
                # gercekten aciliyor. Sessizce PASS demiyoruz — atlama GORUNUR,
                # cunku kosmayan bir sonda kosuldugunu soylememeli.
                if [ "$NATIVE_WINDOWS" = "1" ] || [ "$WINDOWS_MODE" = "1" ]; then
                    if _opens_window "$example"; then
                        printf "Testing %s... ${GREEN}PASS (compile-only)${NC} ${YELLOW}[smoke ATLANDI: Windows'ta pencere acar]${NC}
" "$example"
                        rm -f "$out_path" "$out_base.ll" "$out_base.o" "$compile_log"
                        return 0
                    fi
                fi
                local smoke_log
                smoke_log=$(mktemp)
                # DISPLAY/WAYLAND_DISPLAY scrubbed: on WSLg/desktop the
                # tame/arcade examples would otherwise open a real game
                # window for the 2-second smoke — 15 windows popping over
                # whatever the user is doing on every test run. Headless,
                # tame fails gracefully with exit 0 (the InitWindow patch),
                # and the wings/router examples never used a display.
                DISPLAY= WAYLAND_DISPLAY= $RUN_PREFIX "./$out_path" > "$smoke_log" 2>&1 &
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
                        # SORGU YENIDEN DENENIR (en cok ~10 s). Tek atis
                        # yarisliydi: `sleep 2` sunucunun HAZIR oldugunu degil,
                        # yalnizca 2 saniye gectigini olcuyor. 16 paralel is
                        # altinda o pencere yetmiyor ve saglam bir ornek
                        # "probe_failed_no_response" ile KIRMIZI veriyor.
                        # Olculdu 2026-09-19 (Windows, 38 ornek paralel):
                        # api_wings_tls.tpr paket icinde dustu, tek basina
                        # ayni kosucuyla 3/3 gecti ve elle olcumde 1.5 s'de
                        # HTTP 200 donuyordu. Yani hata TLS'te degil, sondada.
                        # Dongu sunucu OLURSE hemen cikar, yani gercek bir
                        # cokme hala aninda yakalanir ve yesile donmez.
                        local code=""
                        local _try=0
                        while [ $_try -lt 20 ]; do
                            code=$(curl -s -o /dev/null -w "%{http_code}" --max-time 5 -k "$probe_url" 2>/dev/null)
                            [ -n "$code" ] && [ "$code" != "000" ] && break
                            kill -0 "$smoke_pid" 2>/dev/null || break
                            _try=$((_try + 1))
                            sleep 0.5
                        done
                        if [ -z "$code" ] || [ "$code" = "000" ]; then
                            probe_status="probe_failed_no_response"
                        elif ! kill -0 "$smoke_pid" 2>/dev/null; then
                            probe_status="server_died_after_probe"
                        fi
                    fi
                    _smoke_kill "$smoke_pid"
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
                        rm -f "$smoke_log" "$out_path" "$out_base.ll" "$out_base.o" "$compile_log"
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
                        rm -f "$smoke_log" "$out_path" "$out_base.ll" "$out_base.o" "$compile_log"
                        return 1
                    fi
                fi
                rm -f "$smoke_log"
                rm -f "$out_path" "$out_base.ll" "$out_base.o" "$compile_log"
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
                $TIMEOUT_CMD $RUN_PREFIX "./$out_path" < "$input_file" > /dev/null 2>&1
            else
                $TIMEOUT_CMD $RUN_PREFIX "./$out_path" > /dev/null 2>&1
            fi

            if [ $? -eq 0 ]; then
                printf "Testing %s... ${GREEN}PASS${NC}\n" "$example"
            else
                printf "Testing %s... ${RED}FAIL (execution)${NC}\n" "$example"
                echo "----- execution failed (non-zero exit): $example -----" \
                    > "$fail_dir/$name.log" 2>&1
                rm -f "$out_path" "$out_base.ll" "$out_base.o" "$compile_log"
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
            rm -f "$out_path" "$out_base.ll" "$out_base.o" "$compile_log"
            return 1
        else
            printf "Testing %s... ${RED}FAIL (compilation)${NC}\n" "$example"
            {
                echo "----- compile log: $example -----"
                sed 's/^/    /' "$compile_log" | head -n 40
                echo "----- end log -----"
            } > "$fail_dir/$name.log" 2>&1
            rm -f "$out_path" "$out_base.ll" "$out_base.o" "$compile_log"
            return 1
        fi

        rm -f "$out_path" "$out_base.ll" "$out_base.o" "$compile_log"
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
                printf "SKIP: %s%s\n" "$example" "$SKIP_REASON"
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
        # TULPAR_BIN/RUN_PREFIX/EXE_SUFFIX de ihracat listesinde: Windows
        # kipinde worker'lar ayri bash surecleridir ve bunlar dis kabukta
        # ayarlaniyor. Unutulursa worker'lar sessizce LINUX ikilisine doner
        # (ve "her sey yesil" gorunur — olculen sey Windows olmaz).
        # Wine degiskenleri de ayni sebeple: AOT link adimi worker icinde kosuyor.
        export INPUT_DIR FAIL_DIR GREEN RED NC
        export TULPAR_RUN RUN_PREFIX EXE_SUFFIX
        export WINEPREFIX WINEDEBUG WINEPATH TULPAR_CC
        # NATIVE_WINDOWS/WINDOWS_MODE + iki yardimci de ihracat listesinde.
        # Unutulursa worker'da `_smoke_kill: command not found` cikiyor ve
        # pencere acan ornegin smoke'u ATLANMIYOR — olculdu 2026-09-19:
        # kosum sirasinda 5 oyun penceresi acildi ve surecler asili kaldi.
        # Bu dosyanin kendi kurali: worker ayri bir bash surecidir, run_test'in
        # OKUDUGU her sey ihrac edilmeli.
        export NATIVE_WINDOWS WINDOWS_MODE
        export -f run_test smoke_probe_for _smoke_kill _opens_window

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
cp "tulpar$NATIVE_EXE" "../tulpar$NATIVE_EXE"
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
chmod +x "tulpar$NATIVE_EXE"

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
