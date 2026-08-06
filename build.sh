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
    SUITE_FAILED=0
    SUITE_N=0
    for suite in tests/*.test.tpr; do
        [ -f "$suite" ] || continue
        SUITE_N=$((SUITE_N + 1))
        name=$(basename "$suite")
        out=$(DISPLAY= timeout 180 ./tulpar "$suite" 2>&1)
        code=$?
        summary=$(echo "$out" | grep -E '^Tests:' | tail -1)
        if [ $code -ne 0 ]; then
            printf "%-42s ${RED}FAIL${NC} %s\n" "$name" "$summary"
            echo "$out" | grep -E 'FAIL|hata|error' | head -8 | sed 's/^/    /'
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
        echo -e "${RED}Some suites failed!${NC} ($SUITE_N paket)"
        exit 1
    fi
    echo -e "${GREEN}All $SUITE_N suites passed!${NC}"
    exit 0
fi

if [ "$ACTION" = "test" ]; then
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
                        "tame3d_cube.tpr" "tame3d_primitives.tpr" \
                        "tame3d_models.tpr" "tame3d_anim.tpr" "tame3d_lights.tpr" \
                        "tame3d_shadows.tpr" "tame3d_texture.tpr" \
                        "scene3d_collector.tpr" "scene3d_camera.tpr" \
                        "scene3d_arena.tpr" "scene3d_terrain.tpr" \
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
    if [ $TEST_FAILED -ne 0 ]; then
        echo -e "${RED}Some tests failed!${NC}"
        exit 1
    fi
    
    echo -e "${GREEN}All tests passed!${NC}"
    exit 0
fi

# Build with CMake — wipe contents first so we always get a clean configure
echo "Building TulparLang..."
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
