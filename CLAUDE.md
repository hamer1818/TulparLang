# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project

TulparLang is a statically-typed scripting language implemented in C++17 with an LLVM 18 backend. Source files use the `.tpr` extension. The project is primarily authored in Turkish; user-facing strings often flow through `src/common/localization.hpp` (`tulpar::i18n::tr_en`) so both Turkish and English messages exist in the source.

## Build

Requires **CMake 3.14+** and **LLVM 18+** (hard requirement — `find_package(LLVM REQUIRED)`). C++17 is mandatory (`std::variant`, `std::optional`).

```bash
./build.sh                     # Linux/macOS — configures + builds in build-linux/ or build-macos/, copies ./tulpar to repo root
./build.sh clean               # Wipe build dirs and artifacts
build.bat                      # Windows (MSVC) — builds in build-windows/, copies tulpar.exe
build.ps1                      # Windows (PowerShell) — same as build.bat
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j   # Direct CMake
```

`build.sh` wipes `$BUILD_DIR` on every invocation (no incremental builds). Use direct CMake if you want incremental rebuilds during development.

The build script leaves a `./tulpar` (or `tulpar.exe`) copied out of the build directory; developer tooling assumes that copy exists. Different OSes use **different build directories** (`build-linux`, `build-macos`, `build-windows`) — the repo already contains stale copies of several; don't assume one is current.

Two CMake targets are built:
- `tulpar` — the compiler/driver executable.
- `tulpar_runtime` — static library (`libtulpar_runtime.a` / `.lib`) that AOT-compiled user binaries link against. It is compiled with `-DTULPAR_RUNTIME_ONLY` and contains a different source subset from `tulpar` itself. When editing runtime-visible code (anything in `src/vm/runtime_bindings.cpp`, `runtime/*`, lexer, parser, `vm/`, `jit/`, SQLite), both targets must stay buildable.

## Tests

There is no unit-test framework. `./build.sh test` is an end-to-end runner:

```bash
./build.sh test                           # Run every examples/*.tpr through AOT
./build.sh test examples/02_basics.tpr    # Run one example
run_tests.bat                             # Windows equivalent
```

It invokes `./tulpar --aot <file>`, expects an `a.out` to be produced, runs it, and compares only the exit status. Interactive examples get stdin from `examples/inputs/<basename>.txt`. A hardcoded `SKIP_TESTS` list in `build.sh` (and a parallel one in `run_tests.bat`) excludes network/server-style examples and ones still pending features: `utils.tpr`, `09_socket_simple.tpr`, `09_socket_server.tpr`, `09_socket_client.tpr`, `11_router_app.tpr`, `12_threaded_server.tpr`, `14_api_server.tpr`. Update **both** scripts when adding skips.

CI (`.github/workflows/build.yml`) builds on Ubuntu, macOS, and Windows; only the Linux job runs `./build.sh test` (and it is `continue-on-error`).

A multi-language micro-benchmark harness lives in `benchmarks/` — `run_benchmarks.sh` / `run_benchmarks.ps1` time `fib`/`loopsum` across C, Rust, Go, JS, Python, Java, and Tulpar AOT, writing into `benchmarks/RESULTS.md`. Not run by CI.

## Running Tulpar programs

```bash
./tulpar script.tpr            # Default: AOT compile + run (silent), falls back to VM on failure
./tulpar --vm script.tpr       # Force VM path (faster startup)
./tulpar --legacy script.tpr   # Force tree-walk interpreter
./tulpar build script.tpr [out]  # Emit standalone native binary
./tulpar --repl                # Interactive mode (interpreter-backed)
```

CLI parsing lives at the top of `main()` in `src/main.cpp:153`; `--aot`, `--build`, and the positional `build` subcommand all set build-mode. Default execution calls `aot_compile_and_run_silent()`; on any AOT failure it silently falls through to the VM, so "it runs" is not evidence the AOT path worked — check with `tulpar build` or `--aot` explicitly.

## Architecture

Pipeline, top to bottom:

1. **Lexer** (`src/lexer/`) — tokenizes UTF-8 source into `Token*` arrays.
2. **Parser** (`src/parser/`) — hand-written recursive descent, produces AST nodes defined in `parser/ast_nodes.hpp`. A visitor interface lives in `ast_visitor.hpp`.
3. **Type inference** (`src/typeinfer/`) — runs over the AST before codegen.
4. **Backends** — four of them share the same AST:
   - **AOT / LLVM** (`src/aot/`, primary): `aot_pipeline.cpp` is the entry point (`aot_compile`, `aot_compile_and_run`, `aot_compile_and_run_silent`). Actual IR generation is split across `llvm_backend.cpp`, `llvm_types.cpp`, `llvm_values.cpp` — that's the full list in `AOT_SOURCES` (CMakeLists.txt). Other `.cpp` files in `src/aot/` (notably `llvm_backend_modern.cpp`) are dead/experimental and not built — don't edit them expecting changes to take effect. Architecture-specific LLVM components are selected in `CMakeLists.txt` (`x86*` vs `aarch64*`).
   - **VM** (`src/vm/`): `compiler.cpp` lowers AST → bytecode (`bytecode.cpp`), `vm.cpp` executes it, `runtime_bindings.cpp` implements built-ins (print, sockets, db, threads, etc.). This is also the path AOT'd binaries use at runtime.
   - **JIT** (`src/jit/`): x64 direct emitter. Labelled legacy in `CMakeLists.txt` but still linked into `tulpar` and the runtime archive.
   - **Interpreter** (`src/interpreter/`): tree-walk, labelled legacy. Still used by the REPL.
5. **Runtime support** (`runtime/`) — `cJSON`, `tulpar_arc` (automatic reference counting for heap values), `tulpar_native` (FFI).

### Standard library is embedded at build time

`lib/*.tpr` files are read by `cmake/EmbedLibraries.cmake` and baked into `src/embedded_libs.h` via `configure_file()` from `src/embedded_libs.h.in`. If you add a new stdlib module (currently `wings`, `router`, `http_utils` are embedded; `async`, `middleware`, `socket`, `tulpar_api` exist as files), add an `embed_library(...)` call there **and** a slot in `embedded_libs.h.in`. `src/embedded_libs.h` is generated build output (gitignored) — never hand-edit; edit the `.in` template and the `lib/*.tpr` source instead.

SQLite (`lib/sqlite3/sqlite3.c`) is vendored and compiled straight into both `tulpar` and `tulpar_runtime`.

### Cross-platform shims

Platform detection goes through `src/common/platform.h`, `platform_sockets.h`, `platform_threads.h`, `platform_dl.h`. Always add new syscalls through these headers rather than `#ifdef _WIN32` directly — the Windows port is recent and code that bypasses the shim tends to break the MSVC build. `PLATFORM_WINDOWS`, `PLATFORM_LINUX`, `PLATFORM_MACOS` are defined by CMake.

### WASM target

`wasm/` is a separate build (Emscripten), driven by `wasm/build_wasm.sh`. The `wasm/emsdk/` subtree is a vendored Emscripten SDK — treat it as read-only and do not grep / index it. It is not referenced by the main CMake build.

## Working with this codebase

- `.bak` files in `src/lexer/` and `src/parser/` are dead snapshots; ignore them.
- `src/aot/` contains a lot of loose `.c`, `.exe`, `.obj`, and standalone test scripts from manual codegen experiments. Only the files listed under `AOT_SOURCES` in `CMakeLists.txt` are actually part of the build.
- The repo root accumulates loose artifacts from ad-hoc compile sessions: `loopsum.exe`, `turkish_full_syntax.exe`, `*.ll`, `*.o`, a Linux `tulpar` binary, an old `Makefile`, scratch `test.tpr` / `test.db` files. None of them are inputs to the build — leave them alone unless cleaning up.
- `EKSIKLER.md` (Turkish: "missing items") is the project's running list of known gaps and TODO features; consult it before claiming a feature is broken vs. unimplemented.
- User-facing diagnostic strings should go through `tr_en("<turkish>", "<english>")` — don't hardcode only one language.
- New examples land in `examples/`, numbered roughly by theme; if they need stdin, drop a fixture in `examples/inputs/<name>.txt` or add the filename to `SKIP_TESTS` in `build.sh` **and** `run_tests.bat`.
