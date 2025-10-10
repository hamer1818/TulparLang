# OLang Platform Desteği 🌍

OLang, çoklu platform desteği ile birlikte gelir. Tüm büyük işletim sistemlerinde çalışabilir!

## 🎯 Desteklenen Platformlar

| Platform | Durum | Mimari | Build Metodu |
|----------|-------|--------|--------------|
| **Linux** | ✅ Tam Destek | x86_64, ARM | CMake / Makefile |
| **macOS** | ✅ Tam Destek | Intel, Apple Silicon | CMake / Makefile |
| **Windows** | ✅ Tam Destek | x86_64 | CMake (MinGW) / Visual Studio |
| **WSL** | ✅ Tam Destek | x86_64 | CMake / Makefile |

---

## 📦 Kurulum

### Linux (Ubuntu/Debian)

```bash
# Gerekli araçları yükle
sudo apt-get update
sudo apt-get install -y build-essential cmake git

# OLang'i indir
git clone https://github.com/yourusername/OLang.git
cd OLang

# Build et
chmod +x build.sh
./build.sh

# Çalıştır
./olang examples/01_hello_world.olang
```

### Linux (Fedora/RHEL)

```bash
# Gerekli araçları yükle
sudo dnf install gcc cmake git

# OLang'i indir ve build et
git clone https://github.com/yourusername/OLang.git
cd OLang
chmod +x build.sh
./build.sh

# Çalıştır
./olang examples/01_hello_world.olang
```

### macOS

```bash
# Homebrew yükle (eğer yoksa)
/bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"

# Gerekli araçları yükle
brew install cmake git

# OLang'i indir
git clone https://github.com/yourusername/OLang.git
cd OLang

# Build et
chmod +x build.sh
./build.sh

# Çalıştır
./olang examples/01_hello_world.olang
```

**Not**: Apple Silicon (M1/M2/M3) tam destekleniyor!

### Windows (MinGW)

#### Chocolatey ile:

```cmd
REM Chocolatey yükle (Admin PowerShell)
Set-ExecutionPolicy Bypass -Scope Process -Force; [System.Net.ServicePointManager]::SecurityProtocol = [System.Net.ServicePointManager]::SecurityProtocol -bor 3072; iex ((New-Object System.Net.WebClient).DownloadString('https://community.chocolatey.org/install.ps1'))

REM Gerekli araçları yükle
choco install mingw cmake git -y

REM OLang'i indir
git clone https://github.com/yourusername/OLang.git
cd OLang

REM Build et
build.bat

REM Çalıştır
olang.exe examples\01_hello_world.olang
```

#### Manuel:

1. [MinGW-w64](https://www.mingw-w64.org/downloads/) indir ve kur
2. [CMake](https://cmake.org/download/) indir ve kur
3. [Git](https://git-scm.com/download/win) indir ve kur
4. Yukarıdaki komutları çalıştır

### Windows (Visual Studio)

```cmd
REM Visual Studio 2019/2022 gerekli (C++ tools)

REM OLang'i indir
git clone https://github.com/yourusername/OLang.git
cd OLang

REM Build directory oluştur
mkdir build
cd build

REM CMake ile configure et (Visual Studio)
cmake .. -G "Visual Studio 16 2019"
REM veya Visual Studio 2022 için:
REM cmake .. -G "Visual Studio 17 2022"

REM Build et
cmake --build . --config Release

REM Çalıştır
cd ..
build\Release\olang.exe examples\01_hello_world.olang
```

### Windows (WSL - Windows Subsystem for Linux)

```bash
# WSL içinde Linux komutlarını kullan
sudo apt-get update
sudo apt-get install -y build-essential cmake git

git clone https://github.com/yourusername/OLang.git
cd OLang
chmod +x build.sh
./build.sh

./olang examples/01_hello_world.olang
```

---

## 🛠️ Build Metotları

### 1. CMake (Önerilen - Cross-Platform)

```bash
# Configure
cmake -B build -DCMAKE_BUILD_TYPE=Release

# Build
cmake --build build --config Release

# Executable
./build/olang  # Unix
.\build\Release\olang.exe  # Windows
```

### 2. Makefile (Unix-like)

```bash
make clean
make
./olang examples/01_hello_world.olang
```

### 3. Build Scripts

```bash
# Unix-like (Linux, macOS, WSL)
./build.sh

# Windows
build.bat
```

---

## 🔧 Platform-Specific Notlar

### Linux

- **Gereksinimler**: GCC 7+, CMake 3.10+
- **Paket Yöneticisi**: apt, dnf, pacman, etc.
- **Otomatik build**: `build.sh`
- **Math library**: Otomatik link edilir (`-lm`)

### macOS

- **Gereksinimler**: Xcode Command Line Tools, CMake 3.10+
- **Paket Yöneticisi**: Homebrew önerilen
- **Apple Silicon**: Native ARM64 destekli
- **Universal Binary**: Intel + ARM64 için build edilebilir

```bash
# Universal binary oluşturmak için
cmake -B build -DCMAKE_OSX_ARCHITECTURES="x86_64;arm64"
```

### Windows

#### MinGW
- **Gereksinimler**: MinGW-w64, CMake 3.10+
- **Otomatik build**: `build.bat`
- **Avantaj**: Lightweight, hızlı

#### Visual Studio
- **Gereksinimler**: VS 2019/2022 C++ tools
- **CMake Generator**: Visual Studio
- **Avantaj**: Native Windows, debugging

#### WSL
- **En kolay yöntem**: Linux gibi çalışır
- **Performans**: Native'e yakın
- **Önerilen**: Ubuntu 20.04+

---

## 🚀 CI/CD - GitHub Actions

OLang, GitHub Actions ile otomatik build destekler:

- ✅ Linux (Ubuntu)
- ✅ macOS (Intel)
- ✅ Windows (MinGW)

Her commit'te otomatik test ve build:

```yaml
# .github/workflows/build.yml
- Linux: gcc build + test
- macOS: clang build + test
- Windows: MinGW build + test
```

---

## 📊 Platform Karşılaştırma

| Özellik | Linux | macOS | Windows (MinGW) | Windows (VS) |
|---------|-------|-------|-----------------|--------------|
| Build Speed | ⚡⚡⚡ | ⚡⚡ | ⚡⚡ | ⚡ |
| Binary Size | 🔹 Küçük | 🔹 Küçük | 🔹 Küçük | 🔸 Orta |
| Debugging | ✅ GDB | ✅ LLDB | ✅ GDB | ✅ VS Debugger |
| Package Manager | apt/dnf | brew | choco | vcpkg |
| Kurulum Kolaylığı | ⭐⭐⭐⭐⭐ | ⭐⭐⭐⭐ | ⭐⭐⭐ | ⭐⭐⭐ |

---

## 🧪 Test Platformları

### Otomatik Test
```bash
# Linux/macOS/WSL
./olang examples/10_test_phase1.olang
./olang examples/11_test_phase2.olang
./olang examples/13_json_arrays.olang

# Windows
olang.exe examples\10_test_phase1.olang
olang.exe examples\11_test_phase2.olang
olang.exe examples\13_json_arrays.olang
```

### Manuel Test
Her platform için 13 örnek dosya:
- 01-04: Temel özellikler
- 05-08: Orta seviye
- 09-13: İleri seviye

---

## 🐛 Platform-Specific Sorunlar

### Linux

**Sorun**: `math.h` fonksiyonları bulunamıyor
```bash
# Çözüm: CMake otomatik halleder, Makefile için:
gcc ... -lm
```

### macOS

**Sorun**: Apple Silicon'da build hatası
```bash
# Çözüm: CMake otomatik detect eder, veya:
cmake -DCMAKE_OSX_ARCHITECTURES=arm64 ..
```

### Windows (MinGW)

**Sorun**: `_CRT_SECURE_NO_WARNINGS`
```bash
# Çözüm: CMake otomatik ekler
```

**Sorun**: Path'te MinGW yok
```cmd
REM Çözüm: Environment Variables'a ekle
setx PATH "%PATH%;C:\mingw64\bin"
```

### WSL

**Sorun**: File permissions
```bash
# Çözüm:
chmod +x build.sh
chmod +x olang
```

---

## 📦 Binary Dağıtımı

### Release Build

```bash
# Linux/macOS
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build

# Windows
cmake -B build -DCMAKE_BUILD_TYPE=Release -G "MinGW Makefiles"
cmake --build build
```

### Strip (Boyut Küçültme)

```bash
# Linux/macOS
strip olang

# Windows
strip olang.exe
```

### Package Oluşturma

```bash
# CMake install
cmake --install build --prefix /usr/local

# Veya manuel
sudo cp olang /usr/local/bin/
sudo cp -r examples /usr/local/share/olang/
```

---

## 🎯 Platform Önerileri

| Kullanım | Platform | Neden |
|----------|----------|-------|
| Development | Linux/macOS | Hızlı build, iyi tooling |
| Production | Linux | Stabil, performanslı |
| End Users (Windows) | MinGW | Kolay kurulum |
| Enterprise (Windows) | Visual Studio | Native, debugger |
| Quick Test | WSL | Windows'ta Linux avantajı |

---

## 📝 Gelecek Platform Desteği

- 🔄 **WebAssembly**: Browser'da çalışma (Planning)
- 🔄 **Android**: Termux/NDK desteği (Future)
- 🔄 **iOS**: Cross-compile (Future)
- 🔄 **FreeBSD**: BSD desteği (Future)

---

## 🆘 Yardım

Platform-specific sorunlar için:

1. **GitHub Issues**: Bug report açın
2. **Discussions**: Soru sorun
3. **Wiki**: Detaylı dokümantasyon

---

**OLang Versiyonu**: 1.2.2  
**Son Güncelleme**: 9 Ekim 2025  
**Platform Desteği**: Linux, macOS, Windows (MinGW/VS/WSL)

**Happy Coding on All Platforms!** 🚀🌍

