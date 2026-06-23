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
                        "wings_notes_db.tpr" \
                        "tulpar_api_demo.tpr" "utils.tpr")

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

        printf "Testing %s... " "$example"

        # Use timeout if available
        TIMEOUT_CMD=""
        if command -v timeout &> /dev/null; then
            TIMEOUT_CMD="timeout 30s"
        elif command -v gtimeout &> /dev/null; then
            TIMEOUT_CMD="gtimeout 30s"
        fi

        # Run AOT compilation and (optionally) execution. Capture stderr+stdout
        # to a tempfile so we can echo it on failure — silent failures here
        # used to hide every diagnostic and turn CI into "all FAIL, no clue why".
        if $TIMEOUT_CMD ./tulpar --aot "$example" > "$compile_log" 2>&1 && [ -f "$out_path" ]; then
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
                "./$out_path" > "$smoke_log" 2>&1 &
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
                            echo -e "${GREEN}PASS (compile-only +smoke +probe)${NC}"
                        else
                            echo -e "${GREEN}PASS (compile-only +smoke)${NC}"
                        fi
                    else
                        echo -e "${RED}FAIL (smoke $probe_status)${NC}"
                        echo "----- smoke log: $example -----"
                        sed 's/^/    /' "$smoke_log" | head -n 40
                        echo "----- end log -----"
                        TEST_FAILED=1
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
                        echo -e "${GREEN}PASS (compile-only +smoke)${NC}"
                    else
                        echo -e "${RED}FAIL (smoke crashed, exit $smoke_rc)${NC}"
                        echo "----- smoke log: $example -----"
                        sed 's/^/    /' "$smoke_log" | head -n 40
                        echo "----- end log -----"
                        TEST_FAILED=1
                    fi
                fi
                rm -f "$smoke_log"
                rm -f "$out_path" "$out_path.ll" "$out_path.o"
                return
            fi
            if [ -f "$input_file" ]; then
                $TIMEOUT_CMD "./$out_path" < "$input_file" > /dev/null 2>&1
            else
                $TIMEOUT_CMD "./$out_path" > /dev/null 2>&1
            fi

            if [ $? -eq 0 ]; then
                echo -e "${GREEN}PASS${NC}"
            else
                echo -e "${RED}FAIL (execution)${NC}"
                TEST_FAILED=1
            fi
        else
            echo -e "${RED}FAIL (compilation)${NC}"
            echo "----- compile log: $example -----"
            sed 's/^/    /' "$compile_log" | head -n 40
            echo "----- end log -----"
            TEST_FAILED=1
        fi

        rm -f "$out_path" "$out_path.ll" "$out_path.o" "$compile_log"
    }

    if [ -n "$TARGET" ]; then
        if [ ! -f "$TARGET" ]; then
            echo -e "${RED}ERROR: Test file '$TARGET' not found.${NC}"
            exit 1
        fi
        run_test "$TARGET" 0
    else
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

            run_test "$example" "$compile_only"
        done
    fi

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
