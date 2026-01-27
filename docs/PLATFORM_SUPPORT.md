# Platform Support and Installation

TulparLang is designed with cross-platform support in mind and has been tested on the following operating systems.

---

## Supported Platforms

- **Linux**: Tested on Ubuntu, Fedora, and Arch Linux.
- **macOS**: macOS 10.15 (Catalina) and later.
- **Windows**: Windows 10 and later.

Some installation steps are summarized in the main `README.md`; this document provides a more detailed guide.

---

## Common Prerequisites

Make sure the following tools are installed on your system:

- GCC or Clang (C compiler)
- LLVM 18+ (libraries and tools like `llvm-config`)
- CMake 3.14+
- Git (to clone the source code)

For LLVM installation:

- On Linux/macOS, ensure the package manager provides version 18 or newer.
- On Windows, ensure the official LLVM installer adds the `bin` folder to your PATH and, if needed, that `LLVM_DIR` is visible to CMake.

---

## Building on Linux

### 1. Install Required Packages

Commands vary by distribution; examples:

- **Ubuntu / Debian based:**

  ```bash
  sudo apt update
  sudo apt install -y build-essential cmake llvm-18 llvm-18-dev clang git
  ```

- **Fedora:**

  ```bash
  sudo dnf install -y gcc gcc-c++ cmake llvm llvm-devel clang git
  ```

- **Arch Linux:**

  ```bash
  sudo pacman -S --needed base-devel cmake llvm clang git
  ```

### 2. Clone and Build

```bash
git clone https://github.com/hamer1818/TulparLang.git
cd TulparLang
./build.sh
```

The `build.sh` script creates the CMake build directory and compiles the project. When the build finishes, you should have a `tulpar` executable in the directory.

### 3. Quick Test

```bash
./tulpar examples/01_hello_world.tpr
```

---

## Building on macOS

### 1. Install Tools via Homebrew

If you do not have Homebrew installed, follow the instructions at [Homebrew](https://brew.sh).

```bash
brew install llvm cmake git
```

In some cases you may need to add LLVM’s `bin` directory to your PATH:

```bash
echo 'export PATH="/opt/homebrew/opt/llvm/bin:$PATH"' >> ~/.zshrc
source ~/.zshrc
```

### 2. Clone and Build

```bash
git clone https://github.com/hamer1818/TulparLang.git
cd TulparLang
./build.sh
```

### 3. Run

```bash
./tulpar examples/01_hello_world.tpr
```

---

## Building on Windows

On Windows, you can build using an MSVC-based CMake project or MinGW/Clang. The `build.bat` script is provided to simplify a typical MSVC/CMake build.

### 1. Required Tools

- **Microsoft Visual Studio** (or Build Tools) with C/C++ components
- **CMake** (GUI or CLI)
- **LLVM 18+** (official Windows installer)
- **Git**

During installation:

- For LLVM, ensure \"Add LLVM to the system PATH\" is selected.
- For Visual Studio, ensure the \"Desktop development with C++\" workload is installed.

### 2. Clone and Build

In PowerShell or CMD:

```powershell
git clone https://github.com/hamer1818/TulparLang.git
cd TulparLang
.\build.bat
```

`build.bat` uses CMake to generate project files and build with the appropriate compiler. When finished, you should see `tulpar.exe` in the directory.

### 3. Run

```powershell
.\tulpar.exe examples\01_hello_world.tpr
```

`src/main.c` includes configuration for UTF‑8 console output and WinSock initialization, so non‑ASCII characters (e.g. Turkish) should display correctly.

---

## Execution Modes (All Platforms)

Once built, the command-line interface is similar across platforms:

- **Run via VM (default):**

  ```bash
  ./tulpar program.tpr
  ```

- **AOT compilation (native binary):**

  ```bash
  ./tulpar --aot program.tpr program_bin
  ./program_bin
  ```

- **Force VM execution:**

  ```bash
  ./tulpar --vm program.tpr
  ./tulpar --run program.tpr
  ```

- **REPL (interactive shell):**

  ```bash
  ./tulpar --repl
  ```

For more details, see the \"Execution Modes\" section in `docs/KULLANIM.md`.

---

## Frequently Asked Questions (FAQ)

### LLVM not found / `llvm-config` error

- Linux/macOS:
  - Check the installed LLVM version (`llvm-config --version`).
  - If multiple versions are installed, you may need to point CMake to the correct one.
- Windows:
  - Ensure LLVM was added to PATH during installation.
  - If necessary, pass `CMAKE_PREFIX_PATH` or `LLVM_DIR` explicitly to CMake.

### Compiler not found (`gcc`, `clang`, `cl` errors)

- Linux/macOS: Ensure `build-essential` / Xcode Command Line Tools are installed.
- Windows: Make sure the Visual Studio C++ components are installed and that you are using a Developer Command Prompt or an environment where `cl.exe` is available.

### Incorrect or garbled Unicode characters on Windows

- `src/main.c` calls `SetConsoleOutputCP(65001)`, but your terminal must also be in UTF‑8 mode.
- Windows Terminal or the integrated terminal in editors like VS Code typically work fine; older `cmd.exe` windows may require extra configuration.

### Program runs but there are network/file permission issues

- Linux/macOS: Check file permissions (e.g. `chmod +x tulpar`) and whether the port you use is already taken or requires elevated privileges.
- Windows: Ensure that firewall and antivirus software are not blocking the socket server.

---

For detailed usage and language features, see:

- Quick start: `docs/QUICKSTART.md`
- Language reference: `docs/KULLANIM.md`
- Math functions: `docs/MATH_FUNCTIONS.md`