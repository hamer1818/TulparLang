---
tags: [infra]
---

# Cross-platform Shims

Platform detection **şim'ler üzerinden**: `src/common/platform.h`, `platform_sockets.h`, `platform_threads.h`, `platform_dl.h`. Yeni syscall'ları `#ifdef _WIN32` yerine bu header'lardan ekle (Windows portu yeni, şim'i atlayan kod MSVC build'i kırar).

- CMake tanımlar: `PLATFORM_WINDOWS`, `PLATFORM_LINUX`, `PLATFORM_MACOS`.
- Async coroutine'leri platform-spesifik: macOS ucontext, Windows fiber. → [[Async Runtime]]
- WASM: `wasm/` ayrı Emscripten build'i (`wasm/build_wasm.sh`); `wasm/emsdk/` vendored, **read-only / indexleme**.

## İlgili
[[Build System]] · [[Async Runtime]] · [[Runtime]]
