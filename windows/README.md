# Windows hedefi — Linux'tan çapraz derleme + Wine ile koşma

Tulpar'ın Windows yapısı **Linux hosttan çapraz derlenir** ve yerel doğrulama **Wine** altında
yapılır. Gerçek bir Windows makinesi ya da CI gerekmez; Wine hızlı döngü içindir, kabul testi yine
gerçek Windows'ta yapılmalıdır.

    windows/
      setup_sysroot.py   MSYS2 mingw64 sysroot'u (+ --host-gcc: çapraz GCC, sudo'suz)
      packages.lock      sürüm + sha256 kilidi (depoda durur)
      build.sh           çapraz derleme -> build-windows/
      wine_env.sh        Wine ortamı (prefix, PATH, TULPAR_CC)
      check_imports.sh   DLL bağımlılık kapısı (import tablosu ⊆ stok Windows + paketlenen)
      dist/              türetilmiş, gitignore'da: mingw64/ (sysroot) + host/ (çapraz GCC)
      cache/             indirilen paketler (~500 MB) — kurulumdan sonra silinebilir

Disk: `dist/` ~3 GB, `cache/` ~0.5 GB. İkisi de gitignore'da; `cache/` silinirse
kurulum yeniden indirir (kilit sayesinde aynı sürümleri).

## Kurulum (bir kez)

```bash
python3 windows/setup_sysroot.py             # hedef ağaç: LLVM 22.1.8 + mingw CRT + openssl (~1.4 GB)
python3 windows/setup_sysroot.py --host-gcc  # çapraz GCC 16.2.0 (sudo YOK; pacman -Sp ile indirir)
```

`sudo pacman -S --needed mingw-w64-gcc mingw-w64-binutils mingw-w64-crt mingw-w64-headers
mingw-w64-winpthreads` de aynı işi görür; sistemde varsa o kullanılır.

> **GCC zorunlu, clang değil.** Host clang ile de derleniyor ve ilk bakışta çalışıyor, ama
> clang nesneleri GCC'nin libstdc++'ıyla karışınca **istisna yolu bozuluyor**: `catch` içinden
> atılan bir hata `call()` sınırını geçerken süreç sessizce (çıkış kodu 0) ölüyor. Ölçüldü,
> ayrıntı: [[Tuzaklar]] 9f. clang yolu yalnız acil durum yedeğidir ve `windows/build.sh` bunu
> kırmızı bir uyarıyla söyler.

## Derleme ve koşma

```bash
windows/build.sh                  # build-windows/tulpar.exe + libtulpar_runtime.a + libtulpar_tame.a
windows/check_imports.sh          # DLL kapısı
source windows/wine_env.sh
wine build-windows/tulpar.exe version
wine build-windows/tulpar.exe merhaba.tpr     # AOT: derle + linkle + çalıştır
```

## Testler

```bash
./build.sh windows test      # examples/ (aynı koşucu, aynı COMPILE_ONLY listesi)
./build.sh windows suites    # tests/*.test.tpr
```

`windows` kipi yalnız üç değişkeni değiştirir — `TULPAR_BIN`, `RUN_PREFIX`, `EXE_SUFFIX` — geri
kalan her şey Linux koşucusuyla aynı koddur. Bu bilinçli: 3.13.0 öncesinde `COMPILE_ONLY_TESTS`
listesi hem `build.sh`'de hem `run_tests.ps1`'de duruyordu ve elle senkron tutulmak zorundaydı.

**Aynı dizinde paralel koşum yapma:** doğrudan çalıştırma yolu geçici dosyayı sabit adla
(`tulpar_temp.o/.exe`) çalışma dizinine yazar; test paketi koşarken elle sonda çalıştırmak ikisini
de bozar ve hata tamamen alakasız görünür ([[Tuzaklar]] 9e).

## Dağıtım paketi (başka bir makineye göndermek için)

`build-windows/` klasörünü olduğu gibi zip'lemek **yetmez**: DLL'ler sysroot'ta kalır, AOT'un
ikinci adımı için hedefte MinGW bulunmaz ve motor ikilileri `TULPAR_ENGINE_ASSETS` yoksa derleme
zamanında gömülü Linux yoluna düşer. Paketleyici bu üçünü de çözer:

```bash
windows/package.sh --zip        # dist-windows/ + dist-windows.zip (~64 MB sıkıştırılmış)
windows/verify_package.sh       # paketi YALITILMIŞ kopyada Wine ile sınar
```

Paket içeriği: `tulpar.exe` + AOT'un linklediği arşivler + geçişli DLL kapanışı,
`linkleyici/` (g++ + collect2 + ld + CRT + gerçekten kullanılan arşivler; `cc1plus` ve `lto1`
**yok** — C++ derlemiyoruz, yalnız LLVM'in ürettiği `.o`'yu bağlıyoruz), `motor/` (demo, editör,
sahnec + varlıklar), `ornekler/`, `tulpar.cmd` sarmalayıcısı ve `BENIOKU.txt`.

Doğrulayıcı paketi **ayrı bir dizine kopyalayıp** sysroot'u ve `TULPAR_CC`'yi ortamdan çıkararak
koşar — geliştirme makinesinde sysroot zaten PATH'te olduğu için yalıtım şart. İlk sürüm tam bu
sayede iki kez düştü: `liblto_plugin.dll` eksikti (GCC link ederken istiyor) ve `ld.exe`
`libintl-8.dll` bulamadığı için **hiç başlamıyordu** (`collect2: ld returned 53`, sebebini
söylemeden). DLL listesi artık elle tutulmuyor: import tablosundan özyinelemeli çıkarılıyor.

## Mimari: hangi toolchain nerede

| # | Ne | Nerede koşar | Ne için |
|---|---|---|---|
| 1 | Arch çapraz GCC 16.2.0 (**sürücü**) | Linux | `tulpar.exe`, `libtulpar_runtime.a`, motor arşivleri derlenir |
| 2 | MSYS2 sysroot (**ağaç**) | — | başlıklar, CRT, libstdc++, startfile, LLVM, OpenSSL |
| 3 | MSYS2 `g++.exe` | Wine | AOT link adımı: `.o` → `.exe` (`TULPAR_CC=g++`) |

**Tek ağaç kuralı:** sürücü (1) başka bir paketten gelir ama başlık/CRT/libstdc++/startfile'ların
hepsi (2)'den gelir — `-nostdinc -nostdinc++` + MSYS2 başlıkları, `-B<sysroot>/lib/` (startfile
için; `-L` yetmez) ve `-L<sysroot>/lib`. Böylece derleme zamanı ağacı ile Wine'daki link zamanı
ağacı **aynı** olur. Hepsi MSVCRT tabanlı; **UCRT** tabanlı bir toolchain (MSYS2 ucrt64,
llvm-mingw) karıştırılmaz.

## Bilinen sınırlar

* **`engine/` (Vulkan motoru) Windows'ta ÇALIŞIYOR** (2026-09-18 portlandı). MinGW derlemesinde
  varsayılan AÇIK (`TULPAR_WIN_ENGINE`); MSVC'de kapalı, çünkü motorun Windows yolu GCC'ye özgü
  (fiber geçişi GNU asm). Ölçülenler:
  * `engine_tests.exe` Wine'da **469/469 geçti** (79 görünür atlama: Vulkan örneği tükenmesi,
    ses cihazı yok, gerçek zamanlı öncelik yok).
  * `engine_demo.exe --headless` render etti ve çıktı **Linux'unkiyle bayt bayt aynı**
    (sim özeti `15777b195a7f882a`, PPM birebir) — determinizm sözleşmesi platformlar arası tutuyor.
  * `import "engine"` eden Tulpar programı derlendi, linklendi, çalıştı;
    `tests/engine_bridge.test.tpr` **19/19**.
  * Portun getirdikleri: `platform/dl.hpp` (LoadLibrary), `platform/fs.hpp` (mkdir + MapViewOfFile),
    VirtualAlloc arena, Win64 fiber geçişi (XMM6-15 + TEB alanları), vectored SEH çökme raporu,
    `vulkan-1.dll` yükleyici, `PeekNamedPipe` ile bloklamayan konsol yakalama.
* **Web/Android hedefleri Windows hosttan desteklenmiyor** (em++ ve NDK yolları Linux/macOS için).
* **`tulpar update`** Windows'ta `curl.exe`/PowerShell çağırır; Wine'da ikisi de yok, o yüzden
  yerelde "ağ hatası" der. Gerçek Windows 10+'ta `curl.exe` stok gelir. Gömülü `http_client`
  (OpenSSL) ayrı yoldur ve Wine'da https dahil çalışır.
* **Wine ≠ Windows.** Konsol kod sayfası, `cmd.exe` tırnaklama, winsock kenar durumları ve GPU
  sürücüsü farklı davranabilir. Wine yeşili "Windows'ta çalışır"ın *kanıtı değil, göstergesidir*.
