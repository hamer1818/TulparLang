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

The AOT linker resolves `libtulpar_runtime.a` at runtime via `build_link_search_dirs()` in `src/aot/aot_pipeline.cpp` — it probes the directory containing the running `tulpar` binary first (so the installer can drop the archive next to `tulpar.exe`), then `<exe_dir>/lib`, then the dev-tree `build-<platform>/` directories. The Windows installer (`installer/tulpar.iss`, Inno Setup) ships both `tulpar.exe` and `libtulpar_runtime.a` into `%LOCALAPPDATA%\Programs\Tulpar`; CI passes `/DSourceBinary` and `/DSourceRuntimeLib` to `iscc`.

## Tests

`./build.sh test` is an end-to-end runner over `examples/`:

```bash
./build.sh test                           # Run every examples/*.tpr through AOT
./build.sh test examples/02_basics.tpr    # Run one example
run_tests.bat                             # Windows wrapper around run_tests.ps1
```

It invokes `./tulpar --aot <file>`, expects an `a.out`/`<base>.exe` to be produced, runs it, and compares only the exit status. Interactive examples get stdin from `examples/inputs/<basename>.txt`. The `SKIP_TESTS` array is currently empty; the active filter is `COMPILE_ONLY_TESTS` in `build.sh` (and `$compileOnly` in `run_tests.ps1`) — examples that block on `listen()` / `api_run()` (sockets, router, wings, tulpar_api) plus the import-only `utils.tpr` are compiled but not executed, so a regression in the embedded server/router stdlib path still fails the suite. Update **both** scripts when adding entries.

`run_tests.ps1` uses `tulpar build` per file (not `--aot`) and enforces per-test compile/run timeouts (`-CompileTimeoutSec`, `-RunTimeoutSec`, default 30 s each) so a hung example doesn't block the suite.

A separate `tests/` directory holds focused regression suites that are **not** run by `./build.sh test`:
- `*.test.tpr` — Tulpar source using the embedded `test` library (`import "test"`, jest-style assertions). Run with `./tulpar tests/<file>.test.tpr`.
- `*_smoke.py` / `lsp_smoke.py` — Python harnesses that drive the LSP / formatter / package manager subprocesses. Manual; not yet wired into CI.

CI (`.github/workflows/build.yml`) builds on Ubuntu, macOS, and Windows; only the Linux job runs `./build.sh test` (and it is `continue-on-error`).

A multi-language micro-benchmark harness lives in `benchmarks/` — `run_benchmarks.sh` / `run_benchmarks.ps1` time `fib`/`loopsum` across C, Rust, Go, JS, Python, Java, and Tulpar AOT, writing into `benchmarks/RESULTS.md`. Not run by CI.

## Running Tulpar programs

```bash
./tulpar script.tpr             # Default: AOT compile + run (silent), falls back to VM on failure
./tulpar --vm script.tpr        # Force VM path (faster startup)
./tulpar build script.tpr [out] # Emit standalone native binary
./tulpar --repl                 # Interactive mode (VM-backed)

./tulpar fmt script.tpr         # Source formatter (src/fmt/)
./tulpar pkg <init|add|install> # Package manager (src/pkg/)
./tulpar update [--check]       # Self-update from tulparlang.dev (src/cli/update_cmd.cpp)
./tulpar --lsp                  # LSP server on stdio (src/lsp/)
./tulpar version | --help       # Version / command reference
```

CLI dispatch lives in `main()` in [src/main.cpp](src/main.cpp); `--lsp`, `fmt`, `pkg`, `version`, `--help`, and `update` all short-circuit before the run/build path. Default execution calls `aot_compile_and_run_silent()`; on any AOT failure it silently falls through to the VM, so "it runs" is not evidence the AOT path worked — check with `tulpar build` or `--aot` explicitly. `--lsp` owns stdin/stdout for JSON-RPC, so it must dispatch before any banner/REPL output.

CLI output language follows the system locale; override with `TULPAR_LANG=tr` / `TULPAR_LANG=en`.

## Architecture

Pipeline, top to bottom:

1. **Lexer** (`src/lexer/`) — tokenizes UTF-8 source into `Token*` arrays.
2. **Parser** (`src/parser/`) — hand-written recursive descent, produces AST nodes defined in `parser/ast_nodes.hpp`. A visitor interface lives in `ast_visitor.hpp`.
3. **Type inference** (`src/typeinfer/`) — runs over the AST before codegen. Surfaces as `[typecheck]` warnings on every `tulpar`/`tulpar build`/`tulpar --vm` invocation via the `typeinfer_emit_warnings` pre-pass; the standalone `tulpar typecheck` subcommand is the same checker in error mode. Disable the pre-pass with `--no-typecheck` or `TULPAR_NO_TYPECHECK=1` when shaping new rules.
4. **Backends** — two of them share the same AST:
   - **AOT / LLVM** (`src/aot/`, primary): `aot_pipeline.cpp` is the entry point (`aot_compile`, `aot_compile_and_run`, `aot_compile_and_run_silent`). Actual IR generation is split across `llvm_backend.cpp`, `llvm_types.cpp`, `llvm_values.cpp` — that's the full list in `AOT_SOURCES` (CMakeLists.txt). Architecture-specific LLVM components are selected in `CMakeLists.txt` (`x86*` vs `aarch64*`).
   - **VM** (`src/vm/`): `compiler.cpp` lowers AST → bytecode (`bytecode.cpp`), `vm.cpp` executes it, `runtime_bindings.cpp` implements built-ins (print, sockets, db, threads, etc.). This is also the path AOT'd binaries use at runtime, and the path the REPL uses.
   - **Tree-walk interpreter** (formerly `src/interpreter/`) — sunset on 2026-05-05. The REPL was the last consumer; it now compiles each input through the VM compiler and runs it on a persistent VM. The `--legacy` CLI flag is gone.
   - **x64 JIT** (formerly `src/jit/`) — sunset on 2026-05-05. Threshold-triggered tier-1 native code emitter (~2.2k satır) that compiled hot VM functions to x64. ARM64 had it disabled; production AOT path never invoked it; measured perf delta vs pure VM was within noise (<5% on fib(28)). Removed alongside the bytecode hooks (`ObjFunction.jit_code`, `CallSiteCache.cached_jit`, `LoopTrace`, `jit_helper_call`, `jit_interpreter_call`).
5. **Runtime support** (`runtime/`) — `cJSON`, `tulpar_arc` (automatic reference counting for heap values), `tulpar_native` (FFI).

Auxiliary subsystems share the same AST and live alongside the backends:
- `src/lsp/` — LSP server (`tulpar --lsp`). `document_index.cpp` reparses on every change; `builtins.cpp` registers the native built-in symbol table for completion/hover.
- `src/fmt/` — source formatter (`tulpar fmt`).
- `src/pkg/` — package manager (`tulpar pkg`). `manifest.cpp` reads `tulpar.toml`; `pkg_cli.cpp` vendors `path:` deps into `tulpar_modules/<name>/` (registry deps are still TODO).
- `src/cli/` — extra subcommands (currently `update_cmd.cpp` for `tulpar update`).

### Standard library is embedded at build time

`lib/*.tpr` files are read by `cmake/EmbedLibraries.cmake` and baked into `src/embedded_libs.h` via `configure_file()` from `src/embedded_libs.h.in`. Currently embedded: `wings`, `router`, `http_utils`, `async`, `middleware`, `socket`, `tulpar_api`, `test`, `http_client`, `orm` — i.e. every `.tpr` in `lib/`. To add a new stdlib module, drop it in `lib/`, add an `embed_library(...)` call in `cmake/EmbedLibraries.cmake`, **and** a slot in `embedded_libs.h.in`. `src/embedded_libs.h` is generated build output (gitignored) — never hand-edit; edit the `.in` template and the `lib/*.tpr` source instead.

SQLite (`lib/sqlite3/sqlite3.c`) is vendored and compiled straight into both `tulpar` and `tulpar_runtime`.

### Imports and `tulpar_modules`

`import "name"` resolution (in `src/aot/llvm_backend.cpp` around the import handler, mirrored by the VM in `src/vm/vm.cpp`) probes, in order: embedded stdlib name → literal `name` → `name.tpr` → `tulpar_modules/<name>/<name>.tpr` → `tulpar_modules/<name>.tpr`. The last two slots are how `tulpar pkg install` makes a dep usable: it copies a `path:` spec into `tulpar_modules/<name>/` and the convention is that `<name>.tpr` inside that directory is the entry point.

### Cross-platform shims

Platform detection goes through `src/common/platform.h`, `platform_sockets.h`, `platform_threads.h`, `platform_dl.h`. Always add new syscalls through these headers rather than `#ifdef _WIN32` directly — the Windows port is recent and code that bypasses the shim tends to break the MSVC build. `PLATFORM_WINDOWS`, `PLATFORM_LINUX`, `PLATFORM_MACOS` are defined by CMake.

### WASM target

`wasm/` is a separate build (Emscripten), driven by `wasm/build_wasm.sh`. The `wasm/emsdk/` subtree is a vendored Emscripten SDK — treat it as read-only and do not grep / index it. It is not referenced by the main CMake build.

## Working with this codebase

- `.bak` files in `src/lexer/` and `src/parser/` are dead snapshots; ignore them.
- Only the files listed under `AOT_SOURCES` in `CMakeLists.txt` are part of the AOT build (`aot_pipeline.cpp`, `llvm_backend.cpp`, `llvm_types.cpp`, `llvm_values.cpp`). The repo was cleaned of historical scratch files in commit `f237471` ("chore: repoyu yetim/scratch dosyalardan temizle"); if you find loose `.c`/`.exe`/`.obj`/`.ll` files reappearing in `src/aot/` or the repo root, they're new scratch from your session — not load-bearing.
- The repo root still keeps a `tulpar.exe` copy (build output) plus `test.db` / `test_file.txt` (left behind by example runs). None of those are build inputs.
- [EKSIKLER.md](EKSIKLER.md) (Turkish: "missing items") is the project's running list of known gaps and TODO features; consult it before claiming a feature is broken vs. unimplemented. [OZET.md](OZET.md) is a higher-level project status summary.
- User-facing diagnostic strings should go through `tr_en("<turkish>", "<english>")` from `src/common/localization.hpp` — don't hardcode only one language.
- New examples land in `examples/`, numbered roughly by theme. If they need stdin, drop a fixture in `examples/inputs/<name>.txt`. If they block on `listen()` / `api_run()`, add the filename to `COMPILE_ONLY_TESTS` in `build.sh` **and** `$compileOnly` in `run_tests.ps1`.
