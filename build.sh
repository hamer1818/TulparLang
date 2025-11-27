#!/bin/bash
# ============================================
# TulparLang Build Script for Unix-like Systems
# (Linux, macOS, WSL)
# ============================================

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Detect OS
OS="$(uname -s)"
case "${OS}" in
    Linux*)     PLATFORM=Linux;;
    Darwin*)    PLATFORM=macOS;;
    MINGW*|MSYS*|CYGWIN*)    PLATFORM=Windows;;
    *)          PLATFORM="UNKNOWN:${OS}"
esac

# Parse arguments
ACTION="$1"
TARGET="$2"

if [ "$ACTION" = "clean" ]; then
    echo "Cleaning build artifacts..."
    if [ -d "build" ]; then
        rm -rf build
    fi
    if [ -f "tulpar" ]; then
        rm tulpar
    fi
    echo "Clean complete."
    exit 0
fi

if [ "$ACTION" = "test" ]; then
    # Ensure tulpar exists
    if [ ! -f "tulpar" ]; then
        echo "Executable 'tulpar' not found. Building first..."
        ./build.sh
        if [ $? -ne 0 ]; then
            exit 1
        fi
    fi

    echo ""
    echo "========================================"
    echo "Running tests..."
    echo "========================================"
    echo ""

    TEST_FAILED=0
    INPUT_DIR="examples/inputs"
    # Skip tests that are known to fail or run indefinitely (servers)
    SKIP_TESTS=("26_error_handling.tpr" "26b_error_handling_mod.tpr" "32_socket_server.tpr" "32_socket_client.tpr" "33_socket_wrapper_server.tpr" "35_chat_server.tpr" "36_async_chat_server.tpr")

    run_test() {
        local example="$1"
        local name=$(basename "$example" .tpr)
        local input_file="$INPUT_DIR/$name.txt"
        local temp_out="build/temp_${name}.out"
        local temp_err="build/temp_${name}.err"
        
        printf "Running %s... " "$example"
        
        # Use timeout command if available (Linux/macOS usually have it)
        # timeout 15s ./tulpar ...
        
        if command -v timeout &> /dev/null; then
            TIMEOUT_CMD="timeout 15s"
        elif command -v gtimeout &> /dev/null; then
            TIMEOUT_CMD="gtimeout 15s" # macOS with coreutils
        else
            TIMEOUT_CMD="" # No timeout available
        fi

        if [ -f "$input_file" ]; then
            $TIMEOUT_CMD ./tulpar "$example" < "$input_file" > "$temp_out" 2> "$temp_err"
        else
            $TIMEOUT_CMD ./tulpar "$example" > "$temp_out" 2> "$temp_err"
        fi
        
        local exit_code=$?
        
        if [ $exit_code -eq 124 ]; then
            echo -e "${RED}TIMEOUT${NC}"
            echo "  (Test timed out after 15 seconds)"
            TEST_FAILED=1
        elif [ $exit_code -ne 0 ]; then
            echo -e "${RED}FAILED${NC}"
            echo "  Exit code: $exit_code"
            echo "  Error output:"
            cat "$temp_err"
            echo "  Output:"
            cat "$temp_out"
            TEST_FAILED=1
        else
            echo -e "${GREEN}OK${NC}"
        fi
        
        # Cleanup
        rm -f "$temp_out" "$temp_err"
    }

    if [ -n "$TARGET" ]; then
        if [ ! -f "$TARGET" ]; then
            echo -e "${RED}ERROR: Test file '$TARGET' not found.${NC}"
            exit 1
        fi
        run_test "$TARGET"
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
                printf "SKIP: %s (intentional error test)\n" "$example"
                continue
            fi
            
            run_test "$example"
        done
    fi

    if [ $TEST_FAILED -ne 0 ]; then
        echo ""
        echo -e "${RED}Example tests failed!${NC}"
        exit 1
    fi
    
    echo ""
    echo -e "${GREEN}ALL TESTS PASSED!${NC}"
    exit 0
fi

# Build
echo "========================================"
echo "TulparLang Build Script"
echo "========================================"
echo -e "${YELLOW}Platform detected: ${PLATFORM}${NC}"
echo ""

# Check if CMake is available
if command -v cmake &> /dev/null; then
    echo "Build method: CMake"
    
    mkdir -p build
    cd build
    
    # Configure with CMake
    cmake .. -DCMAKE_BUILD_TYPE=Release
    if [ $? -ne 0 ]; then
        echo -e "${RED}ERROR: CMake configuration failed!${NC}"
        exit 1
    fi
    
    # Build
    cmake --build . --config Release
    if [ $? -ne 0 ]; then
        echo -e "${RED}ERROR: Build failed!${NC}"
        exit 1
    fi
    
    # Copy executable
    cp tulpar ../tulpar
    cd ..
else
    echo "CMake not found, using Makefile"
    make
    if [ $? -ne 0 ]; then
        echo -e "${RED}ERROR: Build failed!${NC}"
        exit 1
    fi
fi

# Make executable
chmod +x tulpar

echo ""
echo -e "${GREEN}BUILD SUCCESSFUL!${NC}"
echo ""
echo "Executable: ./tulpar"
echo "Usage:"
echo "  ./build.sh           - Build only"
echo "  ./build.sh clean     - Clean build artifacts"
echo "  ./build.sh test      - Run all tests"
echo "  ./build.sh test file - Run specific test file"

