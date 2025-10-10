#!/bin/bash
# ============================================
# OLang Build Script for Unix-like Systems
# (Linux, macOS, WSL)
# ============================================

echo "========================================"
echo "OLang Build Script"
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
    echo "Building OLang..."
    cmake --build . --config Release

    if [ $? -ne 0 ]; then
        echo -e "${RED}ERROR: Build failed!${NC}"
        cd ..
        exit 1
    fi

    # Copy executable to root
    echo ""
    echo "Copying executable..."
    cp olang ../olang

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
chmod +x olang

echo ""
echo -e "${GREEN}========================================"
echo "BUILD SUCCESSFUL!"
echo "========================================${NC}"
echo ""
echo "Executable: ./olang"
echo ""
echo "To run examples:"
echo "  ./olang examples/01_hello_world.olang"
echo ""
