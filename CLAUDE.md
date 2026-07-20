# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project

TulparLang is a statically-typed, **AOT-compiled** language implemented in C++17 with an LLVM backend. Source files use the `.tpr` extension. The project is primarily authored in Turkish; user-facing strings often flow through `src/common/localization.hpp` (`tulpar::i18n::tr_en`) so both Turkish and English messages exist in the source.

The tree builds against **LLVM 18 through 22** — CMake exposes the major version as `TULPAR_LLVM_MAJOR` and codegen `#if`-gates the API spellings LLVM renamed across releases (e.g. `LLVMDIBuilderInsertDeclareAtEnd` ≤ 18 vs `…RecordAtEnd` ≥ 19 in `llvm_backend.cpp`). Don't hardcode a single LLVM version's API; guard it on `TULPAR_LLVM_MAJOR`. CI runs 18 (Linux apt / macOS brew); MSYS2 ships 19; recent MinGW configs ship 22.

## ⛔ AOT-ONLY — there is no VM execution engine (read before adding a backend)

**Decision (2026-06-15): Tulpar follows the C/Rust/Go model — a single AOT/LLVM execution path. The bytecode VM interpreter and the REPL were removed.** Do **not** reintroduce them.

- `tulpar foo.tpr` AOT-compiles and runs. **There is no VM fallback** — an AOT failure is a hard error (so "it ran" now genuinely means AOT ran).
- `--vm` / `--run` are accepted but **ignored with a warning**; `--repl` / `-i` print a removal notice and exit. Do not wire them back to an interpreter.
- **A new language feature only needs the AOT path.** Implement it once in `src/aot/` (+ lexer/parser/typeinfer). Do **not** add a parallel bytecode-compiler / interpreter case — that was the dual-path maintenance/parity burden this decision exists to kill. (`async/await`, `match`, closures, etc. live only in AOT.)
- **What survives in `src/vm/` is shared runtime, NOT a VM:** `runtime_bindings.cpp` (every `aot_*` built-in the AOT binary links), `vm.cpp` (arena allocator + object/string allocators — `allocate_object`, `vm_alloc_string`, `vm_allocate_array/object`, `vm_array_push`), and the type definitions in `vm.hpp` (`VMValue`, `Obj*`). The `VMValue`/`Obj` naming is historical — treat it as "the runtime value representation," not "the VM." `bytecode.cpp/.hpp` remain only for the `Chunk` type embedded in `ObjFunction`.
- **Removed and gone:** `src/vm/compiler.cpp` (AST→bytecode), `vm_run` (the interpreter loop, formerly the bulk of `vm.cpp`), `run_repl`. If you find yourself writing an opcode dispatch, `compile_statement`, or a REPL loop — stop; that's the deleted VM.

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

Three CMake targets are built:
- `tulpar` — the compiler/driver executable.
- `tulpar_runtime` — static library (`libtulpar_runtime.a` / `.lib`) that AOT-compiled user binaries link against. It is compiled with `-DTULPAR_RUNTIME_ONLY` and contains a different source subset from `tulpar` itself. When editing runtime-visible code (anything in `src/vm/runtime_bindings.cpp`, `runtime/*`, lexer, parser, `vm/`, `jit/`, SQLite), both targets must stay buildable.
- `tulpar_tame` — static library (`libtulpar_tame.a`) with the Tame 2D game runtime: vendored raylib (`lib/raylib/`) + the 49 `aot_tm_*` bindings (`runtime/tame_impl.c` + `runtime/tame_bindings.cpp` — a deliberate two-TU split so raylib.h never meets the runtime/windows headers). Linked into a user binary **only** when the program imports `"tame"` or calls a `tm_*` builtin (`tame_link_flags()` in `aot_pipeline.cpp`, spliced before `-ltulpar_runtime`); ordinary binaries keep zero GL/window dependency. Tame builtins are table-driven — to add one, add a row to `k_tame_builtins` in `llvm_backend.cpp` (declare+dispatch come free) instead of hand-rolling the usual 5-point wiring's codegen part; the other points (impl, typeinfer, LSP) still apply.

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
- `wings_tls_smoke.py` — the remaining Python harness, drives the compiled `tulpar` binary as a subprocess to exercise the TLS serve path. Manual; not run by CI. (The other `*_smoke.py` harnesses for LSP / formatter / pkg / serve-modes were removed — they were never wired into CI.)

CI (`.github/workflows/build.yml`) builds on Ubuntu, macOS, and Windows; only the Linux job runs `./build.sh test` (and it is `continue-on-error`).

A multi-language micro-benchmark harness lives in `benchmarks/` — `run_benchmarks.sh` / `run_benchmarks.ps1` time `fib`/`loopsum` across C, Rust, Go, JS, Python, Java, and Tulpar AOT, writing into `benchmarks/RESULTS.md`. Not run by CI.

## Running Tulpar programs

```bash
./tulpar script.tpr             # AOT compile + run (the only execution path; AOT failure = hard error)
./tulpar build script.tpr [out] # Emit standalone native binary
# --vm / --run: removed (ignored with a warning). --repl / -i: removed (exits with a notice).

./tulpar fmt script.tpr         # Source formatter (src/fmt/)
./tulpar typecheck script.tpr   # Standalone type checker in error mode (src/cli/typecheck_cmd.cpp)
./tulpar doc script.tpr         # Markdown reference from leading-comment docstrings (src/cli/doc_cmd.cpp)
./tulpar debug script.tpr       # Experimental DAP debug adapter (src/cli/debug_cmd.cpp)
./tulpar pkg <init|add|install> # Package manager (src/pkg/)
./tulpar update [--check]       # Self-update from tulparlang.dev (src/cli/update_cmd.cpp)
./tulpar --lsp                  # LSP server on stdio (src/lsp/)
./tulpar version | --help       # Version / command reference
```

`--no-typecheck` (or `TULPAR_NO_TYPECHECK=1`) skips the pre-build `[typecheck]` pass; `--strict` promotes those warnings to hard errors (persist it with `strict = true` in `tulpar.toml`).

CLI dispatch lives in `main()` in [src/main.cpp](src/main.cpp); `--lsp`, `fmt`, `pkg`, `version`, `--help`, and `update` all short-circuit before the run/build path. Default execution calls `aot_compile_and_run_silent()`; an AOT failure (parse/codegen/emit/link) returns non-zero — there is no fallback, so "it ran" means AOT ran. `--lsp` owns stdin/stdout for JSON-RPC, so it must dispatch before any banner output.

CLI output language follows the system locale; override with `TULPAR_LANG=tr` / `TULPAR_LANG=en`.

## Architecture

Pipeline, top to bottom:

1. **Lexer** (`src/lexer/`) — tokenizes UTF-8 source into `Token*` arrays.
2. **Parser** (`src/parser/`) — hand-written recursive descent, produces AST nodes defined in `parser/ast_nodes.hpp`. A visitor interface lives in `ast_visitor.hpp`.
3. **Type inference** (`src/typeinfer/`) — runs over the AST before codegen. Surfaces as `[typecheck]` warnings on every `tulpar`/`tulpar build` invocation via the `typeinfer_emit_warnings` pre-pass; the standalone `tulpar typecheck` subcommand is the same checker in error mode. Disable the pre-pass with `--no-typecheck` or `TULPAR_NO_TYPECHECK=1` when shaping new rules.
4. **Backend — AOT/LLVM only** (the VM/interpreter backends were removed; see "AOT-ONLY" above):
   - **AOT / LLVM** (`src/aot/`, primary): `aot_pipeline.cpp` is the entry point (`aot_compile`, `aot_compile_and_run`, `aot_compile_and_run_silent`). Actual IR generation is split across `llvm_backend.cpp`, `llvm_types.cpp`, `llvm_values.cpp` — that's the full list in `AOT_SOURCES` (CMakeLists.txt). Architecture-specific LLVM components are selected in `CMakeLists.txt` (`x86*` vs `aarch64*`).
   - **Shared runtime** (`src/vm/`, despite the directory name — *not* an execution engine): `runtime_bindings.cpp` implements every `aot_*` built-in (print, sockets, db, threads, async, etc.) that the AOT binary links; `vm.cpp` provides the arena allocator + object/string allocators; `vm.hpp` defines the runtime value types (`VMValue`, `Obj*`); `bytecode.cpp/.hpp` keep the `Chunk` type embedded in `ObjFunction`. The `tulpar_runtime` static library (linked into AOT'd user binaries) is built from this set.
   - **Bytecode VM + REPL** — **removed 2026-06-15** (AOT-only). `src/vm/compiler.cpp` (AST→bytecode) and `vm_run` (the interpreter dispatch loop) are deleted; `--vm`/`--run`/`--repl` are gone. See "AOT-ONLY" at the top — do not rebuild them.
   - **Tree-walk interpreter** (formerly `src/interpreter/`) — sunset 2026-05-05. **x64 JIT** (formerly `src/jit/`) — sunset 2026-05-05 (perf delta vs VM was within noise). Both predate the AOT-only move.
5. **Runtime support** (`runtime/`) — `cJSON`, `tulpar_arc` (automatic reference counting for heap values), `tulpar_native` (FFI), `tulpar_async` (stackful-coroutine event loop behind `async`/`await`), `tulpar_gzip` (in-tree DEFLATE/gzip — fixed Huffman + LZ77 + CRC-32, no zlib dependency, so AOT'd binaries stay self-contained).

Auxiliary subsystems share the same AST and live alongside the backends:
- `src/lsp/` — LSP server (`tulpar --lsp`). `document_index.cpp` reparses on every change; `builtins.cpp` registers the native built-in symbol table for completion/hover.
- `src/fmt/` — source formatter (`tulpar fmt`).
- `src/pkg/` — package manager (`tulpar pkg`). `manifest.cpp` reads `tulpar.toml`; `pkg_cli.cpp` vendors `path:` deps into `tulpar_modules/<name>/` (registry deps are still TODO).
- `src/cli/` — extra subcommands: `update_cmd.cpp` (`tulpar update`), `typecheck_cmd.cpp` (`tulpar typecheck`, the pre-pass checker in error mode), `doc_cmd.cpp` (`tulpar doc`), `debug_cmd.cpp` (`tulpar debug`, experimental DAP), plus `line_edit.cpp` (shared line editor).

### Standard library is embedded at build time

`lib/*.tpr` files are read by `cmake/EmbedLibraries.cmake` and baked into `src/embedded_libs.h` via `configure_file()` from `src/embedded_libs.h.in`. Currently embedded: `wings`, `router`, `http_utils`, `async`, `middleware`, `socket`, `tulpar_api`, `test`, `http_client`, `orm`, `wings_tls`, `tame`, `arcade` — i.e. every `.tpr` in `lib/`. To add a new stdlib module, drop it in `lib/`, add an `embed_library(...)` call in `cmake/EmbedLibraries.cmake`, **and** a slot in `embedded_libs.h.in`. `src/embedded_libs.h` is generated build output (gitignored) — never hand-edit; edit the `.in` template and the `lib/*.tpr` source instead.

SQLite (`lib/sqlite3/sqlite3.c`) is vendored and compiled straight into both `tulpar` and `tulpar_runtime`. raylib 5.5 (`lib/raylib/`, zlib license, pruned + carrying a `TULPAR PATCH`-marked InitWindow fix in `rcore.c` and MIT X11 compat headers in `x11_compat/`) is vendored the same way but compiles only into `tulpar_tame`.

### `lib/arcade.tpr` — the game preset layer (pure Tulpar, on top of `tame`)

`import "arcade"` is a ~800-line **pure-Tulpar** preset engine over the `tame` bindings — no C/runtime wiring, so editing it only needs `./build.sh clean` to re-embed. It's the layer the 10 shipped browser games use (`examples/arcade_*.tpr` + `examples/tame_snake.tpr`, each with an English twin under `examples/en/`). It gives short games a parallel-array entity store, a bilingual API (every function has a TR name and an EN alias, e.g. `oyuncu`/`player`, `uret`/`spawn`, `bolum`/`level`), a level/bölüm system, and MTV wall collision. Non-obvious constraints, all load-bearing (details in memory `arcade-preset-engine.md` / `tulpar-keyword-callback-gotchas.md`):
- **Entity ids are generation-tagged handles, not array indices** — `_egen[slot]*1048576 + slot`, resolved via `_slot_of()`. Never index a parallel array (`_evx[id]`) with a raw handle; go through a getter (`vx_of`, `get_x`, …) that calls `_slot_of` first. (arcade predates the struct-in-array fix below; a fresh engine could use `struct Ent` in an array instead of parallel arrays.)
- **`call(fn, a, b, …)` now forwards N args** (fixed 2026-07-20; was a segfault that forced 0-arg callbacks + global context). The dispatcher (`aot_call_dynamic_n` / `aot_invoke_boxed_n` in `runtime_bindings.cpp`) calls the target through its registered arity, so a `func on_hit(a, b)` handler can be `call()`-ed directly.
- **Level switches are deferred** (`_lvl_pending`, applied at frame end) so `next_level()`/`bolum_gec()` can't mutate the entity arrays mid-collision-iteration. This is deliberate, correct game-loop design (structural changes at a safe point) — NOT a language limitation; mutating a collection mid-iteration is unsafe in most languages.
- Reserved keywords bite here: `move` **and** `don` (=`return`, Turkish "dön") are tokens — a `bool don = ...` local silently truncates the module / errors.

The browser build path: `tulpar build --target=web examples/arcade_zipla.tpr -o out/jump` → `.html/.js/.wasm`. The HTML shell (`write_web_shell_html()` in `src/aot/aot_pipeline.cpp`) carries a **touch gamepad** that appears only on `pointer:coarse` devices and drives raylib via synthetic `KeyboardEvent`s — every web game gets mobile controls for free. Games are published bilingually to `tulparlang.dev/oyunlar/` (source in `tulpar-lang-web/public/oyunlar/`, index generated by `web_demo/gen_index.py`) with a Starlight docs page at `examples/games`. **Never open a raylib window to verify a game** (see memory `no-raylib-windows-verify.md`) — run window-opening commands under `DISPLAY=` and verify the web build via headless Chrome.

### Imports and `tulpar_modules`

`import "name"` resolution (in `src/aot/llvm_backend.cpp` around the import handler, mirrored by the VM in `src/vm/vm.cpp`) probes, in order: embedded stdlib name → literal `name` → `name.tpr` → `tulpar_modules/<name>/<name>.tpr` → `tulpar_modules/<name>.tpr`. The last two slots are how `tulpar pkg install` makes a dep usable: it copies a `path:` spec into `tulpar_modules/<name>/` and the convention is that `<name>.tpr` inside that directory is the entry point.

`import "name" as alias;` namespaces the imported module — every top-level `func` defined in the module is renamed to `<alias>__<name>` and intra-module calls are rewritten in lockstep, so two libraries that both export `route` (or `helper`, etc.) can coexist. Built-ins (`print`, `len`, ...) and references to the importer's own functions are not touched. The rewrite lives in `src/parser/import_alias.cpp` and is invoked from both the AOT (`AST_IMPORT` codegen) and VM (`OP_IMPORT` runtime) paths. Plain `import "name";` (no alias) preserves the historical "all names land in global scope" behaviour.

Call sites can be written either way: `m.func(args)` (Python-style) and `m__func(args)` (literal mangled form) are equivalent. `parse_postfix` rewrites `<expr>.<name>(args)` — for **any** receiver expression: a bare identifier (`m.func()`), an array element (`ents[i].area()`), or a call result (`mk(3,5).area()`) — into a single `FunctionCall` whose `name` is the unmangled member and whose `receiver` field holds the head. `resolve_qualified_call` (`src/parser/import_alias.cpp`, invoked from AOT codegen and mirrored in `typeinfer.cpp`) then decides: if the receiver is an identifier and `<recv>__<name>` resolves, it's a **module-alias** call; otherwise it's **method dispatch** — the receiver is prepended as the first positional arg, i.e. `<name>(<recv>, args…)`. So free functions act as methods (`func area(Rect r)` → `r.area()`), and this works on `json`, struct locals, and boxed-struct array elements alike (`ents[i].area()` — the boxed VM_OBJECT receiver is unpacked into the `Rect` param via `emit_unpack_boxed_struct_into`). Standard `obj.field` reads/writes with no parens (`p.x`, `cfg.host`) keep falling through to the ArrayAccess desugar. Not supported: calling a closure *stored in a field* via dot (`obj.handler()` where `handler` is a value, not a top-level function) — that resolves as method dispatch and reports "function not found".

### Adding a runtime built-in (the "5-point binding")

A native built-in (`print`, `ord`, `gzip_compress`, `secure_token`, `hmac_sha256`, …) is not one edit — STATUS.md calls this "5 noktada bağlandı" and every builtin follows the same wiring. To add `aot_foo`, touch all five or codegen/typecheck/tooling drift out of sync:

1. **`src/vm/runtime_bindings.cpp`** — the actual `extern "C"` implementation `aot_foo(...)` plus its `aot_foo_ptr` address accessor. This is the single source of truth (it links into `tulpar_runtime`, so AOT'd binaries call it directly).
2. **`src/aot/llvm_backend.hpp`** — declare the LLVMValueRef field / entry for the function.
3. **`src/aot/llvm_backend.cpp`** — declare the LLVM function signature and wire the call dispatch so `foo(...)` in source emits a call to `aot_foo`. Pointer-ABI builtins (returning heap strings/objects) follow the existing `aot_*_ptr` split pattern (see `aot_split_ptr`, `parse_multipart`).
4. **`src/typeinfer/typeinfer.cpp`** — add the signature to the builtin table (e.g. `{"foo", TYPE_INT, {TYPE_STRING, TYPE_INT}}`) so type inference knows the return/arg types.
5. **`src/lsp/builtins.cpp`** — register the symbol (signature + doc string) for LSP completion/hover.

Then rebuild **both** `tulpar` and `tulpar_runtime`, and refresh the repo-root `libtulpar_runtime.a` if AOT link probing needs it. Pure-Tulpar helpers (most of Wings/ORM) don't need any of this — they live in `lib/*.tpr` and only require a `./build.sh clean` to re-embed.

### Cross-platform shims

Platform detection goes through `src/common/platform.h`, `platform_sockets.h`, `platform_threads.h`, `platform_dl.h`. Always add new syscalls through these headers rather than `#ifdef _WIN32` directly — the Windows port is recent and code that bypasses the shim tends to break the MSVC build. `PLATFORM_WINDOWS`, `PLATFORM_LINUX`, `PLATFORM_MACOS` are defined by CMake.

### WASM target

`tulpar build --target=web game.tpr` (or `--web`) emits a `wasm32-unknown-emscripten` object from the normal LLVM backend and links it with `em++` (`source wasm/emsdk/emsdk_env.sh` first) against the archives in `wasm/dist/` — build those once with `wasm/build_tame_web.sh` (async-free runtime + raylib `PLATFORM_WEB` + tame bindings). Output: `game.html + .js + .wasm`; serve over HTTP (not `file://`). Assets embed via `TULPAR_WEB_ASSETS=<dir>`. The output name is a **base** — the `.html`/`.js`/`.wasm` are appended, and a trailing `.html` you write yourself is stripped, so `-o game` and `-o game.html` are equivalent. The output **directory must already exist** or the object emit fails with a bare "No such file or directory". `import "arcade"` works on the web target unchanged (verified 2026-07-17: input, physics, and the `call()`-based hook/collision registry all behave in-browser). Maintainer notes: on wasm32 the VMValue C ABI is sret+byval like Win64 (`vmvalue_abi_uses_sret()` in `llvm_values.cpp` picks at runtime; `backend->target_web` must be set before `declare_runtime_functions`), and `tulpar_async` is excluded from the web runtime (ucontext). The `wasm/emsdk/` subtree is a vendored Emscripten SDK — treat it as read-only and do not grep / index it; if `emcc` reports "clang executable not found", the `upstream/bin` symlinks (clang→clang-23, wasm-ld→lld, ...) were lost in a copy and must be recreated. The legacy `wasm/CMakeLists.txt` playground references deleted VM sources and is dead — do not revive it.

## Working with this codebase

- `.bak` files in `src/lexer/` and `src/parser/` are dead snapshots; ignore them.
- Only the files listed under `AOT_SOURCES` in `CMakeLists.txt` are part of the AOT build (`aot_pipeline.cpp`, `llvm_backend.cpp`, `llvm_types.cpp`, `llvm_values.cpp`). The repo was cleaned of historical scratch files in commit `f237471` ("chore: repoyu yetim/scratch dosyalardan temizle"); if you find loose `.c`/`.exe`/`.obj`/`.ll` files reappearing in `src/aot/` or the repo root, they're new scratch from your session — not load-bearing.
- The repo root still keeps a `tulpar.exe` copy (build output) plus `test.db` / `test_file.txt` (left behind by example runs). None of those are build inputs.
- [STATUS.md](STATUS.md) is the project's single "where do we stand?" reference: current status, what's done (PR-grouped summary), open gaps (priority-tagged 🔴/🟡/🟢), and the v1.0 criteria. Consult it before claiming a feature is broken vs. unimplemented. The legacy `EKSIKLER.md` (60-item running gap list, all RESOLVED) and `OZET.md` (phase-by-phase history) were consolidated into STATUS.md and removed.
- User-facing diagnostic strings should go through `tr_en("<turkish>", "<english>")` from `src/common/localization.hpp` — don't hardcode only one language.
- New examples land in `examples/`, numbered roughly by theme. If they need stdin, drop a fixture in `examples/inputs/<name>.txt`. If they block on `listen()` / `api_run()`, add the filename to `COMPILE_ONLY_TESTS` in `build.sh` **and** `$compileOnly` in `run_tests.ps1`.
- `packages/` holds **first-party publishable packages** written in TulparLang (e.g. `wings_jwt` — HS256 tokens), each a self-contained dir with `tulpar.toml` + `<name>.tpr` + `<name>.test.tpr`. These are source for the registry, not part of the compiler build; run their tests with `./tulpar packages/<name>/<name>.test.tpr`. The registry itself (`tulpar-be`) and its web frontend (`pkg.tulparlang.dev`) are **separate sibling repos**, both with their own CLAUDE.md.
