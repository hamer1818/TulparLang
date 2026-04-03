# Platform Support

TulparLang supports the following platforms:

## Fully Supported Platforms

### Linux
- **Status**: Primary development platform ✅
- **Tested on**: Ubuntu 20.04+, Fedora 35+, Arch Linux
- **Compilers**: GCC 9+, Clang 12+
- **Build System**: CMake + Make
- **Architectures**: x86_64, ARM64

### macOS
- **Status**: Fully supported ✅
- **Tested on**: macOS 10.15 (Catalina) and later
- **Architectures**: x86_64 (Intel) and ARM64 (Apple Silicon)
- **Compilers**: Clang (via Xcode Command Line Tools)
- **Build System**: CMake + Make

### Windows
- **Status**: Native support (NEW!) ✅
- **Tested on**: Windows 10/11
- **Compilers**: MSVC (Visual Studio 2019/2022)
- **Build System**: CMake + MSBuild
- **Alternative**: WSL2 with Linux build

## Prerequisites

### All Platforms
- **CMake**: 3.14 or later
- **LLVM**: 18.0 or later
- **C++ Compiler**: Supporting C++17 standard

### Platform-Specific Dependencies

#### Linux
```bash
# Ubuntu/Debian
sudo apt-get install build-essential cmake llvm-18-dev clang

# Fedora
sudo dnf install gcc-c++ cmake llvm-devel clang

# Arch Linux
sudo pacman -S base-devel cmake llvm clang
```

#### macOS
```bash
# Install Homebrew if not already installed
/bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"

# Install dependencies
brew install cmake llvm@18

# Add LLVM to PATH
export PATH="/opt/homebrew/opt/llvm@18/bin:$PATH"
```

#### Windows

**Option 1: Native Windows Build (Recommended)**

1. **Visual Studio**: Install Visual Studio 2019 or 2022
   - During installation, select "Desktop development with C++" workload
   - This includes MSVC compiler and MSBuild

2. **CMake**: Download from [cmake.org](https://cmake.org/download/)
   - Use the Windows installer
   - Add CMake to system PATH during installation

3. **LLVM**: Download LLVM 18.x Windows installer
   - Get from [LLVM GitHub Releases](https://github.com/llvm/llvm-project/releases)
   - Look for `LLVM-18.1.8-win64.exe` or similar
   - **Important**: Check "Add LLVM to system PATH" during installation

**Option 2: WSL (Windows Subsystem for Linux)**

If you prefer a Linux environment:
```bash
# Install WSL (PowerShell as Administrator)
wsl --install

# Inside WSL, follow Linux instructions
```

## Building from Source

### Linux/macOS
```bash
git clone https://github.com/hamer1818/OLang.git
cd OLang
./build.sh
```

### Windows (Native)

**Using Batch Script:**
```batch
build.bat
```

**Using PowerShell:**
```powershell
.\build.ps1
```

**Using CMake Directly:**
```batch
mkdir build-windows
cd build-windows
cmake .. -G "Visual Studio 17 2022" -A x64 -DCMAKE_BUILD_TYPE=Release
cmake --build . --config Release
```

## Platform-Specific Features

### Networking
- **Linux/macOS**: Uses POSIX sockets (`sys/socket.h`)
- **Windows**: Uses Winsock2 (`winsock2.h`)
- All platforms support the same TulparLang socket API

### Threading
- **Linux/macOS**: POSIX threads (pthread)
- **Windows**: Windows threading API (CreateThread, Critical Sections)
- Cross-platform abstraction in `src/common/platform_threads.h`

### Dynamic Loading
- **Linux/macOS**: `dlopen`, `dlsym`, `dlclose`
- **Windows**: `LoadLibrary`, `GetProcAddress`, `FreeLibrary`
- Unified interface in `src/common/platform_dl.h`

### File Paths
- **Linux/macOS**: Forward slashes (`/`)
- **Windows**: Backslashes (`\`) or forward slashes (both supported)

## Known Limitations

### Windows-Specific
- **Console UTF-8**: For best UTF-8 support, use Windows Terminal
- **Legacy Console**: Enable UTF-8 in Console Properties if using cmd.exe
- **DLL Dependencies**: LLVM DLLs must be in PATH or same directory as executable

### WebAssembly Build
- **Networking**: Not available in WASM builds (`TULPAR_WASM_BUILD` disables sockets)
- **SQLite**: Compiled conditionally
- **File I/O**: Limited to Emscripten virtual filesystem

## Troubleshooting

### Windows Build Issues

**Problem: LLVM not found**
```
Solution: Ensure LLVM bin directory is in system PATH
1. Search for "Environment Variables" in Windows
2. Add LLVM bin path (e.g., C:\Program Files\LLVM\bin) to PATH
3. Restart terminal/IDE
```

**Problem: UTF-8 console errors**
```
Solution: Use Windows Terminal or enable UTF-8 in Console
- Windows Terminal: https://aka.ms/terminal
- Or: chcp 65001  (in cmd.exe to enable UTF-8)
```

**Problem: Missing DLLs when running tulpar.exe**
```
Solution: Install Visual C++ Redistributable
Download from: https://aka.ms/vs/17/release/vc_redist.x64.exe
```

**Problem: CMake can't find LLVM**
```
Solution: Specify LLVM_DIR manually
cmake .. -G "Visual Studio 17 2022" -A x64 -DLLVM_DIR="C:/Program Files/LLVM/lib/cmake/llvm"
```

### Linux Build Issues

**Problem: LLVM version mismatch**
```bash
# Install specific LLVM version
sudo apt install llvm-18-dev llvm-18
```

**Problem: Missing pthread**
```bash
# Should be included in build-essential
sudo apt install build-essential
```

### macOS Build Issues

**Problem: LLVM not found**
```bash
# Ensure LLVM is in PATH
export PATH="/opt/homebrew/opt/llvm@18/bin:$PATH"
echo 'export PATH="/opt/homebrew/opt/llvm@18/bin:$PATH"' >> ~/.zshrc
```

**Problem: Apple Silicon (M1/M2) specific**
```bash
# LLVM should auto-detect ARM64
# Verify with: arch
# Should show: arm64
```

## Testing Your Build

After building, test with a simple program:

**Linux/macOS:**
```bash
echo 'print("Hello, TulparLang!");' > test.tpr
./tulpar test.tpr
```

**Windows:**
```batch
echo print("Hello, TulparLang!"); > test.tpr
tulpar.exe test.tpr
```

Expected output: `Hello, TulparLang!`

## Continuous Integration

TulparLang uses GitHub Actions for automated builds:
- **Linux**: Ubuntu latest with LLVM 18
- **macOS**: macOS latest with LLVM 18 (both Intel and Apple Silicon)
- **Windows**: Windows latest with Visual Studio 2022 and LLVM 18

All platforms build successfully on every commit to `main` branch.

## Performance Notes

- **Compiled performance** is similar across all platforms (native LLVM code)
- **JIT performance** varies by architecture:
  - x64 (Intel/AMD): Optimized
  - ARM64 (Apple Silicon, ARM servers): Experimental
- **VM mode**: Consistent across all platforms

## Support Matrix

| Feature | Linux | macOS | Windows |
|---------|-------|-------|---------|
| AOT Compilation | ✅ | ✅ | ✅ |
| VM Mode | ✅ | ✅ | ✅ |
| JIT Compiler | ✅ | ✅ | ✅ |
| Networking | ✅ | ✅ | ✅ |
| Threading | ✅ | ✅ | ✅ |
| SQLite | ✅ | ✅ | ✅ |
| File I/O | ✅ | ✅ | ✅ |
| REPL | ✅ | ✅ | ✅ |
| WebAssembly | ✅ | ✅ | ✅ |

## Getting Help

If you encounter platform-specific issues:
1. Check this documentation
2. Review closed issues on GitHub
3. Open a new issue with:
   - Operating system and version
   - Compiler version
   - LLVM version
   - Complete error message
   - Steps to reproduce
