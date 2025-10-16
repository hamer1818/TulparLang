# TulparLang Hızlı Kurulum Rehberi ⚡

Platform seçin ve adımları takip edin!

---

## 🐧 Linux

### Ubuntu/Debian

```bash
# 1. Gerekli araçları yükle (tek satır)
sudo apt-get update && sudo apt-get install -y build-essential cmake git

# 2. TulparLang'i indir
git clone https://github.com/hamer1818/TulparLang.git
cd TulparLang

# 3. Build et
./build.sh

# 4. Test et
./TulparLang examples/01_hello_world.tpr

# ✅ TAMAMLANDI!
```

### Fedora/RHEL/CentOS

```bash
# 1. Gerekli araçları yükle
sudo dnf install gcc cmake git

# 2-4. Yukarıdaki adımları takip et
git clone https://github.com/hamer1818/TulparLang.git
cd TulparLang
./build.sh
./TulparLang examples/01_hello_world.tpr
```

### Arch Linux

```bash
# 1. Gerekli araçları yükle
sudo pacman -S base-devel cmake git

# 2-4. Yukarıdaki adımları takip et
```

---

## 🍎 macOS

### Homebrew ile (Önerilen)

```bash
# 1. Homebrew yükle (eğer yoksa)
/bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"

# 2. Gerekli araçları yükle
brew install cmake git

# 3. TulparLang'i indir ve build et
git clone https://github.com/hamer1818/TulparLang.git
cd TulparLang
./build.sh

# 4. Test et
./TulparLang examples/01_hello_world.tpr

# ✅ TAMAMLANDI!
```

**Not**: Apple Silicon (M1/M2/M3) otomatik desteklenir! 🚀

---

## 🪟 Windows

### Yöntem 1: Chocolatey (Önerilen - En Kolay)

```powershell
# 1. Chocolatey yükle (Admin PowerShell - TEK SATIR)
Set-ExecutionPolicy Bypass -Scope Process -Force; [System.Net.ServicePointManager]::SecurityProtocol = [System.Net.ServicePointManager]::SecurityProtocol -bor 3072; iex ((New-Object System.Net.WebClient).DownloadString('https://community.chocolatey.org/install.ps1'))

# 2. Gerekli araçları yükle (Admin CMD/PowerShell)
choco install mingw cmake git -y

# 3. Yeni terminal aç (normal kullanıcı) ve:
git clone https://github.com/hamer1818/TulparLang.git
cd TulparLang
build.bat

# 4. Test et
TulparLang.exe examples\01_hello_world.tpr

# ✅ TAMAMLANDI!
```

### Yöntem 2: WSL (Windows Subsystem for Linux)

```bash
# 1. WSL yükle (Admin PowerShell)
wsl --install

# 2. Bilgisayarı yeniden başlat

# 3. Ubuntu terminal aç ve:
sudo apt-get update
sudo apt-get install -y build-essential cmake git

# 4. TulparLang'i kur
git clone https://github.com/hamer1818/TulparLang.git
cd TulparLang
./build.sh

# 5. Test et
./TulparLang examples/01_hello_world.tpr

# ✅ TAMAMLANDI!
```

### Yöntem 3: Manuel (MinGW)

1. **MinGW-w64 İndir**: https://www.mingw-w64.org/downloads/
   - İndir: `x86_64-posix-seh` versiyonu
   - Kur: `C:\mingw64` klasörüne
   - PATH'e ekle: `C:\mingw64\bin`

2. **CMake İndir**: https://cmake.org/download/
   - Installer'ı çalıştır
   - "Add CMake to PATH" seçeneğini işaretle

3. **Git İndir**: https://git-scm.com/download/win
   - Installer'ı çalıştır
   - Varsayılan ayarlarla kur

4. **TulparLang Build Et**:
   ```cmd
   git clone https://github.com/hamer1818/TulparLang.git
   cd TulparLang
   build.bat
   TulparLang.exe examples\01_hello_world.tpr
   ```

### Yöntem 4: Visual Studio

1. **Visual Studio 2019/2022** indir (Community Edition ücretsiz)
   - "Desktop development with C++" workload'unu seç

2. **CMake ve Git** yükle (yukarıdaki gibi)

3. **Build**:
   ```cmd
   git clone https://github.com/hamer1818/TulparLang.git
   cd TulparLang
   mkdir build
   cd build
   cmake .. -G "Visual Studio 17 2022"
   cmake --build . --config Release
   cd ..
   build\Release\TulparLang.exe examples\01_hello_world.tpr
   ```

---

## 🎯 Hangi Yöntemi Seçmeliyim?

### Linux Kullanıcıları
- ✅ **Tercih**: `build.sh` (en hızlı)
- Package manager zaten var, direkt kurulum

### macOS Kullanıcıları
- ✅ **Tercih**: Homebrew + `build.sh`
- Apple Silicon'da native çalışır

### Windows Kullanıcıları

| Seviye | Yöntem | Avantaj | Dezavantaj |
|--------|--------|---------|------------|
| 🟢 Başlangıç | **WSL** | Çok kolay, Linux gibi | WSL gerekli |
| 🟡 Orta | **Chocolatey** | Otomatik, hızlı | Package manager öğrenme |
| 🟠 İleri | **Manuel MinGW** | Tam kontrol | Manuel setup |
| 🔴 Pro | **Visual Studio** | Native debugging | Büyük kurulum |

---

## ⚡ Tek Satırda Kurulum

### Linux (Ubuntu)
```bash
sudo apt-get update && sudo apt-get install -y build-essential cmake git && git clone https://github.com/hamer1818/TulparLang.git && cd TulparLang && ./build.sh && ./TulparLang examples/01_hello_world.tpr
```

### macOS
```bash
brew install cmake git && git clone https://github.com/hamer1818/TulparLang.git && cd TulparLang && ./build.sh && ./TulparLang examples/01_hello_world.tpr
```

### Windows (Chocolatey - 2 satır, Admin gerekli)
```powershell
# Admin PowerShell:
choco install mingw cmake git -y

# Normal terminal:
git clone https://github.com/hamer1818/TulparLang.git; cd TulparLang; build.bat; TulparLang.exe examples\01_hello_world.tpr
```

---

## 🧪 Kurulumu Test Et

### Basit Test
```bash
# Linux/macOS/WSL
./TulparLang examples/01_hello_world.tpr

# Windows
TulparLang.exe examples\01_hello_world.tpr
```

Çıktı:
```
=== TulparLang'E HOŞ GELDİNİZ! ===
...
✅ Tüm temel özellikler çalışıyor!
```

### Tam Test
```bash
# Faz 1 testi
./TulparLang examples/10_test_phase1.tpr

# Faz 2 testi
./TulparLang examples/11_test_phase2.tpr

# JSON arrays
./TulparLang examples/13_json_arrays.tpr
```

---

## 🐛 Sorun Giderme

### "cmake: command not found"
```bash
# Linux
sudo apt-get install cmake
# macOS
brew install cmake
# Windows
choco install cmake
```

### "gcc: command not found"
```bash
# Linux
sudo apt-get install build-essential
# macOS
xcode-select --install
# Windows
choco install mingw
```

### Windows: "Permission denied"
```cmd
REM Admin olarak CMD/PowerShell aç
```

### WSL: "build.sh: Permission denied"
```bash
chmod +x build.sh
./build.sh
```

---

## 📦 Kurulum Sonrası

### Binary'yi PATH'e Ekle

#### Linux/macOS
```bash
# ~/.bashrc veya ~/.zshrc dosyasına ekle
export PATH="$PATH:$HOME/TulparLang"

# Veya sistem geneli:
sudo cp TulparLang /usr/local/bin/
```

#### Windows
1. Sistem Özellikleri → Gelişmiş → Ortam Değişkenleri
2. PATH'e ekle: `C:\path\to\TulparLang`
3. Terminal'i yeniden başlat

### IDE Entegrasyonu

#### VS Code
```json
// .vscode/tasks.json
{
  "label": "Run TulparLang",
  "type": "shell",
  "command": "./TulparLang ${file}",
  "problemMatcher": []
}
```

#### Sublime Text
```json
// TulparLang.sublime-build
{
  "cmd": ["TulparLang", "$file"],
  "selector": "source.tpr"
}
```

---

## ✨ İlk Programınızı Yazın

```TulparLang
// hello.tpr
print("Merhaba TulparLang!");

int x = 42;
str mesaj = "Başarılı!";

print("x =", x);
print(mesaj);
```

Çalıştır:
```bash
./TulparLang hello.tpr
```

---

## 📚 Sonraki Adımlar

1. **Örnekleri İncele**: `examples/` klasöründe 13 örnek
2. **Dokümantasyonu Oku**: `README.md`, `QUICKSTART.md`
3. **Deney Yap**: Kendi programlarını yaz!

---

## 🆘 Yardım Alın

- **GitHub Issues**: Bug report
- **Discussions**: Sorular
- **Dokümantasyon**: Tüm `.md` dosyalar

---

**Kurulum süresi**: 2-5 dakika 🚀  
**Zorluk**: 🟢 Kolay  
**Platform**: Windows, Linux, macOS

**Happy Coding!** 💻✨

