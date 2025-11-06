#!/bin/bash
# ============================================
# TulparLang Build Script for Unix-like Systems
# (Linux, macOS, WSL)
# ============================================

echo "========================================"
echo "TulparLang Build Script"
echo "========================================"
echo ""

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

echo -e "${YELLOW}Platform detected: ${PLATFORM}${NC}"
echo ""

# Check if CMake is available
if command -v cmake &> /dev/null; then
    echo "Build method: CMake"
    BUILD_METHOD="cmake"
else
    echo "CMake not found, using Makefile"
    BUILD_METHOD="makefile"
fi

echo ""

if [ "$BUILD_METHOD" = "cmake" ]; then
    # CMake Build
    # Clean old build
    if [ -d "build" ]; then
        echo "Cleaning old build..."
        rm -rf build
    fi

    # Create build directory
    mkdir -p build
    cd build

    # Configure with CMake
    echo ""
    echo "Configuring with CMake..."
    cmake .. -DCMAKE_BUILD_TYPE=Release

    if [ $? -ne 0 ]; then
        echo -e "${RED}ERROR: CMake configuration failed!${NC}"
        cd ..
        exit 1
    fi

    # Build
    echo ""
    echo "Building TulparLang..."
    cmake --build . --config Release

    if [ $? -ne 0 ]; then
        echo -e "${RED}ERROR: Build failed!${NC}"
        cd ..
        exit 1
    fi

    # Copy executable to root
    echo ""
    echo "Copying executable..."
    cp tulpar ../tulpar

    cd ..
else
    # Makefile Build
    echo "Cleaning..."
    make clean &> /dev/null

    echo ""
    echo "Building with Makefile..."
    make

    if [ $? -ne 0 ]; then
        echo -e "${RED}ERROR: Build failed!${NC}"
        exit 1
    fi
fi

# Make executable
chmod +x tulpar

echo ""
echo "Running examples test suite..."

TEST_FAILED=0
INPUT_DIR="examples/inputs"
SKIP_TESTS=("26_error_handling.tpr" "26b_error_handling_mod.tpr")

for example in examples/*.tpr; do
    [ -f "$example" ] || continue
    name=$(basename "$example" .tpr)
    input_file="$INPUT_DIR/$name.txt"

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

    printf "Running %s... " "$example"

    if [ -f "$input_file" ]; then
        if ./tulpar "$example" < "$input_file" > /dev/null 2>&1; then
            echo -e "${GREEN}OK${NC}"
        else
            echo -e "${RED}FAILED${NC}"
            TEST_FAILED=1
        fi
    else
        if ./tulpar "$example" > /dev/null 2>&1; then
            echo -e "${GREEN}OK${NC}"
        else
            echo -e "${RED}FAILED${NC}"
            TEST_FAILED=1
        fi
    fi
done

if [ $TEST_FAILED -ne 0 ]; then
    echo ""
    echo -e "${RED}Example tests failed!${NC}"
    exit 1
fi

echo ""
echo -e "${GREEN}========================================"
echo "BUILD SUCCESSFUL!"
echo "========================================${NC}"
echo ""
echo "Executable: ./tulpar"
echo ""
echo "To run examples:"
echo "  ./tulpar examples/01_hello_world.tpr"
echo ""
