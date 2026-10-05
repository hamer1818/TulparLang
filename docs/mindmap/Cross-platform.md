---
tags: [infra]
---

# Cross-platform Shims

Platform detection **şim'ler üzerinden**: `src/common/platform.h`, `platform_sockets.h`, `platform_threads.h`, `platform_dl.h`. Yeni syscall'ları `#ifdef _WIN32` yerine bu header'lardan ekle (Windows portu yeni, şim'i atlayan kod MSVC build'i kırar).

- CMake tanımlar: `PLATFORM_WINDOWS`, `PLATFORM_LINUX`, `PLATFORM_MACOS`.
- Async coroutine'leri platform-spesifik: macOS ucontext, Windows fiber. → [[Async Runtime]]
- WASM: web arşivleri `wasm/build_tame_web.sh` ile (runtime + raylib + tame, `wasm/dist/`); kaynak masaüstüyle AYNI (`src/vm/runtime_bindings.cpp` dahil). `wasm/emsdk/` vendored, **read-only / indexleme**. Eski playground (`wasm/CMakeLists.txt`, `build_wasm.sh`, `runtime_bindings_wasm.c`, `tulpar_wasm_api.c/h`) silinmiş VM dosyalarını (`lexer.c`, `compiler.c`, `vm.h`) ve artık olmayan `arr->items` alanını kullanıyordu, hiçbir derlemeye girmiyordu — 2026-10-02'de silindi.
- **Web'de try/catch (setjmp, 2026-10-02):** try modülde doğrudan `setjmp`e iner; wasm'da gerçek
  `setjmp` yok. emcc bunu kendi clang'ında `-mllvm -enable-emscripten-sjlj` ile alçaltır — biz
  wasm objesini kendi LLVM'imizle ürettiğimiz için sürücü aynı seçeneği `LLVMParseCommandLineOptions`
  ile açıyor (`enable_web_sjlj_lowering`, `llvm_backend.cpp`). Mod runtime arşiviyle aynı
  (em++ varsayılanı `SUPPORT_LONGJMP=emscripten`; `aot_throw`daki longjmp `emscripten_longjmp`e
  iner). LLVM ≤ 18 eski ABI'yi (`saveSetjmp`/`testSetjmp`) üretir, Emscripten 5.0 yalnız yenisini
  (`__wasm_setjmp`) taşır → eski adlar `runtime/web_sjlj_uyum.c`de (yalnız web arşivi). Kapı
  `tests/web_try_catch.sh` gerçekten linkleyip node'da koşar; pozitif kontrol `TULPAR_WEB_SJLJ=0`.
  Bundan önce `import "test"` eden her program da web'de linkte düşüyordu.

- **Yayın ikilisinin dinamik bağları (2026-10-02):** `tulpar` yalnız hedef sistemin kendi
  kitaplıklarına bağlanmalı. v3.38.0 macOS ikilisi dört Homebrew dylib'ine bağlıydı (llvm@18
  libunwind, zstd, openssl@3) ve biri eksik makinede açılmıyordu. macOS'ta OpenSSL statik
  (`OPENSSL_USE_STATIC_LIBS`), LLVM bileşenlerinin link zincirindeki sistem dışı dylib'ler statik
  ikizine çevrilir — `cmake/MacOSTasinabilir.cmake`. libunwind'in kaynağı ayrıydı: genel
  `link_directories(LLVM_LIBRARY_DIRS)` AppleClang'in örtük `-lc++`'sini Homebrew libc++'ya
  yönlendiriyordu, o da libunwind'i yeniden dışa aktarıyor; macOS'ta o `-L` kaldırıldı.
  Kapı `tools/dinamik_bag_denetle.sh` (Linux + macOS işleri; ayrıntı [[Build System]]).
  macOS varlığı **yalnız arm64** — 2026-10-05'ten beri adı da öyle: `tulpar-macos-arm64`
  (eski `tulpar-macos-universal` adı geçiş için aynı dosyanın kopyası olarak yayınlanıyor;
  v3.38.5 ve öncesinin `tulpar update`i, install.sh ve motor CI'ı onu indiriyor). Gerçek
  universal (x86_64 dilimi) değerlendirildi, alınmadı: x86_64 dilimini KOŞTURACAK Intel
  koşucu yok (ölçülmeyen ikili yayınlanmaz), çapraz derleme için Rosetta altında ikinci bir
  Homebrew (x86_64 llvm@18 + openssl@3) ağacı ve macOS işinin derleme süresinin ~iki katı.
- **Kullanıcı programlarının dinamik bağları (2026-10-05):** sürücünün taşınabilir olması
  ürettiğinin taşınabilir olduğunu söylemez. macOS'ta `tulpar build` çıktısı Homebrew
  openssl@3'e bağlanıyordu (`-L<openssl@3>/lib -lssl -lcrypto`; ld64 aynı dizinde `.dylib`i
  `.a`ya tercih eder), openssl@3 yoksa link düşüyordu. Çözüm: OpenSSL statik arşivleri
  `libtulpar_runtime.a`nın içinde (`libtool -static`, CMake `TULPAR_TLS_IN_RUNTIME`); sürücü
  macOS'ta `-lssl/-lcrypto` ve OpenSSL `-L` yazmaz. Kapı `tests/kullanici_ikili_bag.sh`
  (Linux + macOS). Linux'ta kullanıcı ikilisi `libssl.so.3`/`libcrypto.so.3`e dinamik bağlı
  kalır (dağıtımın temel paketi; `--as-needed` sayesinde yalnız TLS kullanan programda).
- **Yerinde üzerine yazılan Mach-O (2026-10-05):** çalıştırılmış ikilinin aynı inode'una
  `cp` → belirlenimsiz `Killed: 9` ([[Tuzaklar]] 7l). Kopya her zaman yeni inode'a.

## İlgili
[[Build System]] · [[Async Runtime]] · [[Runtime]]
