# Platform Support

TulparLang supports the following platforms:

## Supported Platforms

- **Linux**: Tested on Ubuntu, Fedora, and Arch Linux.
- **macOS**: Compatible with macOS 10.15 (Catalina) and later.

## Windows Support

Native Windows support has been discontinued. Windows users can run TulparLang through **WSL (Windows Subsystem for Linux)**:

```bash
# Install WSL (PowerShell as Administrator):
wsl --install

# Inside WSL (Ubuntu):
sudo apt-get update
sudo apt-get install build-essential cmake llvm-18-dev clang

# Build TulparLang:
cd /mnt/d/path/to/TulparLang
./build.sh
```

## Prerequisites

Ensure the following tools are installed on your system:
- GCC or Clang
- LLVM 18+
- CMake 3.14+

## Building from Source

Refer to the [Installation](../README.md#installation) section in the main README for detailed instructions on building TulparLang from source.
