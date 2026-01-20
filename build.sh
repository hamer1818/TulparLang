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

if [ "$ACTION" = "clean" ]; then
    echo "Cleaning build artifacts..."
    rm -rf build
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
    SKIP_TESTS=("utils.tpr" "09_socket_server.tpr" "09_socket_client.tpr" "11_router_app.tpr" "12_threaded_server.tpr")

    run_test() {
        local example="$1"
        local name=$(basename "$example" .tpr)
        local input_file="$INPUT_DIR/$name.txt"
        
        printf "Testing %s... " "$example"
        
        # Use timeout if available
        TIMEOUT_CMD=""
        if command -v timeout &> /dev/null; then
            TIMEOUT_CMD="timeout 30s"
        elif command -v gtimeout &> /dev/null; then
            TIMEOUT_CMD="gtimeout 30s"
        fi

        # Run AOT compilation and execution
        if $TIMEOUT_CMD ./tulpar --aot "$example" > /dev/null 2>&1 && [ -f "a.out" ]; then
            if [ -f "$input_file" ]; then
                $TIMEOUT_CMD ./a.out < "$input_file" > /dev/null 2>&1
            else
                $TIMEOUT_CMD ./a.out > /dev/null 2>&1
            fi
            
            if [ $? -eq 0 ]; then
                echo -e "${GREEN}PASS${NC}"
            else
                echo -e "${RED}FAIL (execution)${NC}"
                TEST_FAILED=1
            fi
        else
            echo -e "${RED}FAIL (compilation)${NC}"
            TEST_FAILED=1
        fi
        
        rm -f a.out a.out.ll a.out.o
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
                printf "SKIP: %s\n" "$example"
                continue
            fi
            
            run_test "$example"
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

# Build with CMake
echo "Building TulparLang..."
mkdir -p build
cd build

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
