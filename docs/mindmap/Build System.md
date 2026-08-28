---
tags: [build, infra]
---

# Build System

CMake 3.14+ + **LLVM 18+** (hard requirement). C++17 zorunlu.

## Komutlar
- `./build.sh` — Linux/macOS, `build-linux/` veya `build-macos/`'da derler, `./tulpar`'ı repo köküne kopyalar. **Her çağrıda `$BUILD_DIR` siler** (incremental yok).
- `./build.sh clean` — build dizinleri + artefaktları sil.
- `./build.sh test` — `examples/*.tpr` üzerinde e2e (AOT → çalıştır → exit status karşılaştır). `COMPILE_ONLY_TESTS` listen/api_run bloklayan örnekleri yalnız derler.
- `build.bat` / `build.ps1` — Windows (MSVC). `run_tests.ps1` per-file timeout'lu.
- Incremental için doğrudan `cmake -S . -B build && cmake --build build -j`.

## İki hedef
`tulpar` (derleyici) + `tulpar_runtime` (static lib, `-DTULPAR_RUNTIME_ONLY`, AOT binary'ler linkler). → [[Runtime]]

## ⚠️ WSL stale-build
İncremental build saat kayması yüzünden stale obje kullanabiliyor — özellikle `lib/*.tpr` ([[Standard Library]]) değişince. **`./build.sh clean` yap.** Belirti: kaynak/`embedded_libs.h` yeni ama binary eski davranıyor, ya da LLVM "Incorrect number of arguments".

## Gömülü stdlib + SQLite
`lib/*.tpr` → `embedded_libs.h` (`EmbedLibraries.cmake`). SQLite `lib/sqlite3/sqlite3.c` ikisine de derlenir. → [[Standard Library]] · [[SQLite and DB]]

## CI / Installer
`.github/workflows/build.yml` (Ubuntu/macOS/Windows; yalnız Linux `build.sh test`, continue-on-error). Windows installer `installer/tulpar.iss` (Inno Setup) `tulpar.exe` + `libtulpar_runtime.a` paketler.

## İlgili
[[Standard Library]] · [[Runtime]] · [[Cross-platform]] · [[AOT Backend]]
