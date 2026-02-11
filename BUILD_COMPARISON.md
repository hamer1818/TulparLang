# Build Scripts Karşılaştırması: Linux vs Windows

## Genel Bakış

Bu doküman `build.sh` (Linux/macOS) ve `build.bat` (Windows) scriptlerinin karşılaştırmalı analizini içerir.

## Özellik Karşılaştırması

### 1. Platform Algılama

**Linux (`build.sh`):**
- ✅ OS algılama: `uname -s` ile Linux/macOS/Windows tespiti
- ✅ Platform bilgisi gösterimi
- ✅ Renkli çıktı desteği

**Windows (`build.bat`):**
- ❌ OS algılama yok (sadece Windows için)
- ❌ Renkli çıktı yok (basit echo)
- ✅ MSYS2/MinGW64 path yönetimi var

**Sonuç:** Linux script'i daha esnek ve bilgilendirici.

---

### 2. Bağımlılık Kontrolü

**Linux (`build.sh`):**
- ✅ CMake kontrolü: `command -v cmake`
- ✅ LLVM kontrolü: Birden fazla versiyon kontrolü (`llvm-config`, `llvm-config-18`, `llvm-config-17`, `llvm-config-16`)
- ✅ LLVM versiyon bilgisi gösterimi
- ✅ Compiler kontrolü: GCC veya Clang
- ✅ Detaylı hata mesajları ve kurulum talimatları

**Windows (`build.bat`):**
- ✅ CMake kontrolü: `cmake --version`
- ⚠️ LLVM kontrolü: Sadece `llvm-config` kontrolü, versiyon kontrolü yok
- ⚠️ LLVM bulunamazsa uyarı veriyor ama build'i durdurmuyor
- ✅ Clang kontrolü: `where clang`
- ⚠️ Hata mesajları daha az detaylı

**Sonuç:** Linux script'i daha kapsamlı bağımlılık kontrolü yapıyor.

---

### 3. Build Dizini Yönetimi

**Linux (`build.sh`):**
- ✅ Sabit dizin: `build-linux`
- ✅ Legacy `build` dizini temizleme
- ✅ Basit ve net

**Windows (`build.bat`):**
- ✅ Sabit dizin: `build-win`
- ✅ CMake cache kontrolü: Farklı platform/path'ten cache varsa temizliyor
- ✅ Stale cache tespiti (MSYS2 path sorunları için)
- ✅ Daha gelişmiş cache yönetimi

**Sonuç:** Windows script'i cache yönetimi açısından daha gelişmiş.

---

### 4. CMake Konfigürasyonu

**Linux (`build.sh`):**
```bash
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build . --config Release -j$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)
```
- ✅ Otomatik CPU sayısı tespiti (`nproc` veya `sysctl`)
- ✅ Paralel derleme desteği
- ✅ Basit ve etkili

**Windows (`build.bat`):**
```batch
cmake .. -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release
mingw32-make -j4
```
- ⚠️ Sabit thread sayısı (`-j4`)
- ✅ MinGW Makefiles generator belirtilmiş
- ⚠️ CPU sayısı otomatik tespit edilmiyor

**Sonuç:** Linux script'i paralel derleme açısından daha iyi.

---

### 5. Test Desteği

**Linux (`build.sh`):**
- ✅ Kapsamlı test sistemi
- ✅ Timeout desteği (`timeout` veya `gtimeout`)
- ✅ Input dosyası desteği (`examples/inputs/`)
- ✅ Skip listesi (socket, server örnekleri)
- ✅ AOT derleme ve çalıştırma testi
- ✅ Detaylı test çıktısı (PASS/FAIL)

**Windows (`build.bat`):**
- ⚠️ Test sistemi var ama varsayılan olarak **devre dışı**
- ⚠️ `RUN_AOT_TESTS_ON_WINDOWS=1` set edilmeden test çalışmıyor
- ✅ Skip listesi var
- ✅ Input dosyası desteği var
- ⚠️ Timeout desteği yok
- ⚠️ Daha az detaylı çıktı

**Sonuç:** Linux script'i test açısından çok daha iyi.

---

### 6. Hata Yönetimi

**Linux (`build.sh`):**
- ✅ Renkli hata mesajları (kırmızı)
- ✅ Detaylı kurulum talimatları
- ✅ Exit kodları doğru kullanılmış
- ✅ Recursive build çağrısı (test için)

**Windows (`build.bat`):**
- ⚠️ Basit hata mesajları
- ✅ Exit kodları doğru (`exit /b 1`)
- ⚠️ Daha az bilgilendirici mesajlar

**Sonuç:** Linux script'i kullanıcı deneyimi açısından daha iyi.

---

### 7. Clean İşlemi

**Linux (`build.sh`):**
```bash
rm -rf "$BUILD_DIR"
rm -rf build  # legacy
rm -f tulpar a.out *.o *.ll
```

**Windows (`build.bat`):**
```batch
rmdir /s /q %BUILD_DIR% 2>nul
rmdir /s /q build 2>nul
del tulpar.exe 2>nul
del a.out.exe 2>nul
del a.out 2>nul
del a.out.ll 2>nul
del a.out.o 2>nul
```

**Sonuç:** Her ikisi de benzer, Windows biraz daha detaylı.

---

### 8. Çıktı ve Kullanıcı Deneyimi

**Linux (`build.sh`):**
- ✅ Renkli çıktı (GREEN, RED, YELLOW, BLUE)
- ✅ Platform bilgisi
- ✅ LLVM versiyon bilgisi
- ✅ Detaylı kullanım talimatları

**Windows (`build.bat`):**
- ❌ Renkli çıktı yok
- ⚠️ Daha az bilgilendirici
- ✅ Temel bilgiler mevcut

**Sonuç:** Linux script'i görsel açıdan daha iyi.

---

## Sorunlar ve İyileştirme Önerileri

### Windows Script Sorunları:

1. **Sabit thread sayısı**: `-j4` yerine CPU sayısı tespit edilmeli
2. **Test varsayılan olarak kapalı**: Kullanıcılar test çalıştıramıyor
3. **LLVM kontrolü zayıf**: Versiyon kontrolü yok, bulunamazsa uyarı verip devam ediyor
4. **Renkli çıktı yok**: Kullanıcı deneyimi daha kötü

### Linux Script Sorunları:

1. **Cache yönetimi yok**: Windows'taki gibi stale cache kontrolü yok
2. **Build dizini sabit**: macOS'ta da `build-linux` kullanılıyor (isim yanıltıcı)

---

## Genel Değerlendirme

### Linux Script (`build.sh`): ⭐⭐⭐⭐⭐ (5/5)
- ✅ Kapsamlı bağımlılık kontrolü
- ✅ İyi test desteği
- ✅ Renkli ve bilgilendirici çıktı
- ✅ Otomatik CPU tespiti
- ✅ Platform algılama

### Windows Script (`build.bat`): ⚠️⭐⭐⭐ (3/5)
- ✅ İyi cache yönetimi
- ✅ MSYS2 path yönetimi
- ⚠️ Test varsayılan olarak kapalı
- ⚠️ Zayıf LLVM kontrolü
- ⚠️ Sabit thread sayısı
- ❌ Renkli çıktı yok

---

## Öneriler

### Windows Script İyileştirmeleri:

1. **CPU sayısı tespiti ekle:**
```batch
for /f %%i in ('powershell -command "(Get-WmiObject Win32_Processor).NumberOfLogicalProcessors"') do set CPU_COUNT=%%i
mingw32-make -j%CPU_COUNT%
```

2. **Test'i varsayılan olarak aç:**
```batch
REM AOT tests are flaky on Windows; skip unless explicitly forced
REM if /I "%OS%"=="Windows_NT" if not "%RUN_AOT_TESTS_ON_WINDOWS%"=="1" (
REM     echo Skipping AOT tests...
REM     exit /b 0
REM )
```

3. **LLVM versiyon kontrolü ekle:**
```batch
for /f "tokens=*" %%v in ('llvm-config --version 2^>nul') do set LLVM_VER=%%v
if defined LLVM_VER (
    echo   LLVM: OK (version !LLVM_VER!)
) else (
    echo WARNING: Could not determine LLVM version
)
```

4. **Renkli çıktı ekle (PowerShell kullanarak):**
```batch
powershell -Command "Write-Host 'ERROR: ...' -ForegroundColor Red"
```

### Linux Script İyileştirmeleri:

1. **Build dizini adını platforma göre ayarla:**
```bash
case "${OS}" in
    Linux*)     BUILD_DIR="build-linux";;
    Darwin*)    BUILD_DIR="build-macos";;
    *)          BUILD_DIR="build";;
esac
```

2. **Cache kontrolü ekle (Windows'taki gibi)**

---

## Sonuç

**Linux script'i (`build.sh`) genel olarak daha iyi çalışıyor ve daha kapsamlı.** Windows script'i (`build.bat`) bazı iyi özelliklere sahip (cache yönetimi) ama test desteği ve kullanıcı deneyimi açısından geride kalıyor.

**Öncelikli İyileştirme:** Windows script'ine test desteğini varsayılan olarak açmak ve CPU sayısı tespiti eklemek.
