---
tags: [infra]
---

# Cross-platform Shims

Platform detection **şim'ler üzerinden**: `src/common/platform.h`, `platform_sockets.h`, `platform_threads.h`, `platform_dl.h`. Yeni syscall'ları `#ifdef _WIN32` yerine bu header'lardan ekle (Windows portu yeni, şim'i atlayan kod MSVC build'i kırar).

- CMake tanımlar: `PLATFORM_WINDOWS`, `PLATFORM_LINUX`, `PLATFORM_MACOS`.
- Async coroutine'leri platform-spesifik: macOS ucontext, Windows fiber. → [[Async Runtime]]
- WASM: `wasm/` ayrı Emscripten build'i (`wasm/build_wasm.sh`); `wasm/emsdk/` vendored, **read-only / indexleme**.
- **Web'de try/catch (setjmp, 2026-10-02):** try modülde doğrudan `setjmp`e iner; wasm'da gerçek
  `setjmp` yok. emcc bunu kendi clang'ında `-mllvm -enable-emscripten-sjlj` ile alçaltır — biz
  wasm objesini kendi LLVM'imizle ürettiğimiz için sürücü aynı seçeneği `LLVMParseCommandLineOptions`
  ile açıyor (`enable_web_sjlj_lowering`, `llvm_backend.cpp`). Mod runtime arşiviyle aynı
  (em++ varsayılanı `SUPPORT_LONGJMP=emscripten`; `aot_throw`daki longjmp `emscripten_longjmp`e
  iner). LLVM ≤ 18 eski ABI'yi (`saveSetjmp`/`testSetjmp`) üretir, Emscripten 5.0 yalnız yenisini
  (`__wasm_setjmp`) taşır → eski adlar `runtime/web_sjlj_uyum.c`de (yalnız web arşivi). Kapı
  `tests/web_try_catch.sh` gerçekten linkleyip node'da koşar; pozitif kontrol `TULPAR_WEB_SJLJ=0`.
  Bundan önce `import "test"` eden her program da web'de linkte düşüyordu.

## İlgili
[[Build System]] · [[Async Runtime]] · [[Runtime]]
