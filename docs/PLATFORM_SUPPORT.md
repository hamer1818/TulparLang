# Platform Support

This page says what is **built and measured**, not what might work. Every row
below names the evidence. (Rewritten 2026-09-28: the previous version still
described MSVC/Visual Studio builds, a JIT, a VM mode and a REPL — none of
which exist any more — and claimed Intel + Apple Silicon for macOS.)

## Host platforms (the `tulpar` compiler itself)

| Platform | Status | Evidence |
|---|---|---|
| **Linux x86_64** | ✅ primary | CI `build-linux` (Ubuntu, LLVM 18): build, `build.sh test` (every runnable example), `build.sh suites` (all `tests/*.test.tpr` + audits), typeinfer gate. Developer machines: Arch/CachyOS with LLVM 22. |
| **macOS arm64 (Apple Silicon)** | ✅ | CI `build-macos` (`macos-latest`, Homebrew `llvm@18`): build, AOT smoke, `build.sh suites`. This is the only place the AArch64 code path runs in CI. |
| **Windows x86_64 (MSYS2 MINGW64)** | ✅ since 2026-09-21 | CI `build-windows`: build with MinGW gcc, `build.sh test`, `build.sh suites`, typeinfer gate, DLL-import gate, Inno Setup installer. `errors.test.tpr` was skipped until 2026-09-28 (a `throw` re-raised across a `call()` boundary crashed on MinGW: the generated `_setjmpex` passed a frame, turning longjmp into an SEH unwind); #404 fixes it and runs the suite on Windows too. |
| macOS x86_64 (Intel) | ⚠️ not built | No CI job. The macOS release asset is `tulpar-macos-arm64` (Apple Silicon only, no `lipo`). Until 2026-10-05 it was called `tulpar-macos-universal`; that name is still published as a byte-identical transition copy because older `tulpar update` binaries download it. A real universal build was evaluated and declined: no Intel runner to *run* the x86_64 slice, and a second Homebrew LLVM/OpenSSL tree under Rosetta for the cross build. |
| Linux arm64 | ⚠️ not built | No CI job; the AArch64 backend is exercised only on macOS. |
| Windows MSVC / Visual Studio | ❌ | Not built, not tested. There is no `build.ps1` / `build.bat`; use MSYS2 (`pacman -S mingw-w64-x86_64-{gcc,clang,cmake,ninja,llvm,zlib,zstd,libxml2,openssl}`, then `./build.sh`). |

Requirements everywhere: CMake 3.14+, **LLVM 18–22**, a C++17 compiler. The
AOT link step calls `clang++`, so `clang` must be on
`PATH` to compile programs — also on Windows.

## Target platforms (`tulpar build --target=…`)

| Target | Status | Evidence / notes |
|---|---|---|
| native (host) | ✅ | Every CI job above. |
| `web` (wasm32, Emscripten) | ✅ | Prebuilt `wasm/dist` archives (`wasm/build_tame_web.sh`); `tests/dist_archive_audit.py` checks their symbols. `async` is not available (no ucontext). |
| `android` (arm64-v8a + x86_64) | ✅ | Prebuilt `android/dist` archives (`android/build_tame_android.sh`, NDK); `build.sh suites` links a real game for both ABIs when the archives exist. `async` is not available (bionic has no `makecontext`). |

## Release assets

`tulpar-linux-x64`, `tulpar-macos-arm64` (+ the old-name copy `tulpar-macos-universal`, see above),
`tulpar-windows-x64.zip` (portable, `tulpar.exe` + the MinGW/OpenSSL DLLs it
imports), `tulpar-setup-windows-x64.exe` (per-user installer),
`libtulpar_runtime-<platform>.a`, `SHA256SUMS.txt` (+ `.asc`). `tulpar update`
verifies every download against `SHA256SUMS.txt`.

## Execution model

There is **one** execution path: AOT through LLVM. The bytecode VM, the REPL,
the tree-walk interpreter and the x64 JIT were removed (2026-05 / 2026-06-15);
`--vm` / `--run` are ignored with a warning, `--repl` exits with a notice.

## Getting help

Open an issue with: OS and version, `tulpar version`, `llvm-config --version`,
the complete error message and the steps to reproduce.
