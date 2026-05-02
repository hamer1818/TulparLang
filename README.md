# TulparLang

<div align="center">

[![Build](https://github.com/hamer1818/TulparLang/actions/workflows/build.yml/badge.svg)](https://github.com/hamer1818/TulparLang/actions/workflows/build.yml)
[![License](https://img.shields.io/badge/license-MIT-green.svg)](LICENSE)
[![Platform](https://img.shields.io/badge/platform-Linux%20|%20macOS%20|%20Windows-lightgrey.svg)]()
[![Website](https://img.shields.io/badge/site-tulparlang.dev-00e5ff.svg)](https://tulparlang.dev)

A statically-typed programming language with an LLVM 18 backend, native JSON syntax, UTF-8 strings, and a batteries-included standard library — all in a single ~7 MB binary.

</div>

---

## Install

**Windows (PowerShell):**

```powershell
iwr -useb https://tulparlang.dev/install.ps1 | iex
```

**Linux / macOS:**

```bash
curl -fsSL https://tulparlang.dev/install.sh | bash
```

Both scripts download the latest release, drop `tulpar` into a per-user
location (`%LOCALAPPDATA%\Programs\Tulpar` on Windows, `~/.local/bin`
on Linux/macOS), and wire up `PATH`. No admin rights needed. Run
`tulpar update` later to upgrade in place.

Prefer the GUI? Download `tulpar-setup-windows-x64.exe` from the
[latest release](https://github.com/hamer1818/TulparLang/releases/latest)
— per-user install, Start Menu entry, Add/Remove Programs entry.

## Quick start

Create `hello.tpr`:

```tulpar
str message = "Merhaba, dünya!";
print(message);

func square(int n) {
    return n * n;
}

print(square(5));   // 25
```

Run it:

```bash
tulpar hello.tpr
```

The default `tulpar <file>` invocation AOT-compiles via LLVM and runs
the resulting native binary — Python-shaped syntax, near-Rust speed.

## Why TulparLang

- **Native speed.** LLVM 18 AOT compilation. Within ~1.7× of `gcc -O3`
  on standard arithmetic benchmarks; multiple times faster than
  Node.js, PHP, Python.
- **No build step for prototyping.** `tulpar file.tpr` runs in one
  step. `tulpar build file.tpr` produces a standalone native binary
  when you're ready to ship.
- **Batteries included.** Wings (HTTP server), TulparAPI (FastAPI-style
  routing), ORM (SQLite), HTTP client, sockets, threads, and a test
  framework — all in the default install, no package manager required
  for the standard library.
- **First-class JSON.** `json` is a built-in type with literal syntax
  (`{"k": v}`), dot access (`obj.key`), and `toJson` / `fromJson` round-trips.
- **UTF-8 native.** Source code, strings, identifiers all UTF-8.
  Turkish keyword aliases for the localised feel, full English
  fallback everywhere.
- **Single-binary toolchain.** One `tulpar` executable bundles the
  compiler, runtime, package manager, formatter, and language server.
- **Cross-platform.** Linux, macOS, Windows (native MSVC and MSYS2
  MinGW). WebAssembly target also supported.

## Performance

Benchmark results across algorithmic tests (lower is better):

| Rank | Language          | Total Time   | Relative to Tulpar |
|------|-------------------|--------------|--------------------|
| 🥇    | C (`gcc -O3`)     | **8.65 ms**  | 1.66× faster       |
| 🥈    | Rust 1.80         | **9.30 ms**  | 1.54× faster       |
| 🥉    | **Tulpar (AOT)**  | **14.36 ms** | **Reference**      |
| 4    | JavaScript (Node) | 57.13 ms     | 3.98× slower       |
| 5    | PHP 8.3           | 120.83 ms    | 8.41× slower       |
| 6    | Python 3.12       | 3,671 ms     | 255.63× slower     |

Run `./benchmarks/run_benchmarks.sh` (or `.ps1` on Windows) to
reproduce. Detailed methodology in
[benchmarks/RESULTS.md](benchmarks/RESULTS.md).

## Command reference

The full list lives at <https://tulparlang.dev/reference/cli/>; the
short version:

```bash
tulpar <file.tpr>             # Run via AOT (default; native speed)
tulpar --vm <file.tpr>        # Bytecode VM (instant startup)
tulpar --legacy <file.tpr>    # Tree-walk interpreter (debug)
tulpar build <file.tpr> [out] # Standalone native binary

tulpar --repl                 # Interactive REPL
tulpar fmt <file.tpr>         # Source formatter
tulpar pkg <subcommand>       # Package manager (init, add, install)
tulpar --lsp                  # Language server (editor integration)

tulpar version                # Show installed version
tulpar update [--check]       # Self-update from tulparlang.dev
tulpar --help                 # Show command reference
```

CLI output language follows the system locale — Turkish on TR
machines, English everywhere else. Override with `TULPAR_LANG=tr` or
`TULPAR_LANG=en`.

## Build from source

**Prerequisites:** GCC or Clang, LLVM 18+, CMake 3.14+.

**Linux / macOS:**

```bash
./build.sh
```

**Windows (native MSVC):**

```powershell
.\build.ps1
# or: build.bat
# or:
cmake -S . -B build-windows -G "Visual Studio 17 2022" -A x64
cmake --build build-windows --config Release
```

`build.sh` and `build.ps1` wipe the build directory each time. For
incremental rebuilds during development, use CMake directly. The
output is a `tulpar` (`tulpar.exe` on Windows) copied to the repo root
plus `libtulpar_runtime.a` linked by AOT-compiled user binaries.

## Language at a glance

```tulpar
// Static types with inference
int x = 42;
float pi = 3.14159;
str name = "Tulpar";
bool active = true;

// Number literals
int hex = 0xFF;     // 255
int oct = 0o755;    // 493
int bin = 0b1010;   // 10

// First-class JSON
json user = {
    "name": "Ali",
    "age": 30,
    "address": { "city": "Istanbul" }
};
print(user.address.city);  // dot access works

// Custom types
type Person {
    str name;
    int age;
    str city = "Istanbul";   // default
}
Person p = Person("Ali", 25);

// Control flow
for (int i = 0; i < 10; i++) { print(i); }
for (k in user) { print(k); }

// Error handling
try {
    risky();
} catch (e) {
    print("Error:", e);
} finally {
    cleanup();
}

// Threading
int mu = mutex_create();
func worker(arg) {
    mutex_lock(mu);
    // critical section
    mutex_unlock(mu);
}
int t = thread_create("worker", 42);
thread_detach(t);
```

Full guide at <https://tulparlang.dev/guide/syntax/>.

## Standard library

All modules below are **embedded in the binary** — `import "name"`
just works, no package install required.

| Module           | Purpose                                                  |
|------------------|----------------------------------------------------------|
| `wings`          | Lightweight HTTP server, threaded request handling       |
| `tulpar_api`     | FastAPI-style routing & JSON envelopes on top of Wings   |
| `router`         | Express-style URL routing & middleware                   |
| `http_utils`     | HTTP request parsing & response building primitives      |
| `http_client`    | Outbound HTTP/HTTPS — `http_get`, `http_post_json`, …    |
| `middleware`     | Built-in middleware (logger, CORS, auth, rate-limit)     |
| `socket`         | TCP socket wrappers                                      |
| `async`          | `setTimeout` / `setInterval` / Promise scaffolding       |
| `orm`            | SQLite ORM — `define_model`, `orm_create`, `orm_where`   |
| `test`           | Jest-style assertion framework + suite runner            |

Native built-ins (no `import` needed):

| Category        | Functions                                                                     |
|-----------------|-------------------------------------------------------------------------------|
| I/O             | `print`, `input`, `inputInt`, `inputFloat`                                    |
| Type conversion | `toInt`, `toFloat`, `toString`, `toBool`                                      |
| Math            | `abs`, `sqrt`, `pow`, `sin`, `cos`, `tan`, `log`, `exp`, `floor`, `ceil`, `round`, `random`, `randint`, `min`, `max` |
| String          | `length`, `upper`, `lower`, `trim`, `split`, `join`, `replace`, `substring`, `contains`, `startsWith`, `endsWith`, `indexOf` |
| Array           | `push`, `pop`, `length`, `range`                                              |
| JSON            | `toJson`, `fromJson`                                                          |
| Date / Time     | `timestamp`, `time_ms`, `now_iso8601`, `clock_ms`, `sleep`                    |
| File            | `file_read`, `file_write`, `file_exists`, `file_delete`, `file_append`        |
| Sockets         | `socket_server`, `socket_client`, `socket_accept`, `socket_send`, `socket_receive`, `socket_close` |
| Database        | `db_open`, `db_execute`, `db_query`, `db_close` (vendored SQLite3)            |
| Threading       | `thread_create`, `thread_detach`, `mutex_create`, `mutex_lock`, `mutex_unlock` |
| HTTP (native)   | `http_request`, `http_parse_request`, `http_create_response`, `http_status_text`, `path_match`, `parse_query` |

## Architecture

```
TulparLang/
├── src/
│   ├── lexer/          # Tokenization
│   ├── parser/         # Recursive-descent parser, AST nodes
│   ├── typeinfer/      # Type inference
│   ├── aot/            # LLVM 18 AOT backend (primary)
│   ├── vm/             # Bytecode VM + compiler
│   ├── jit/            # x64 JIT (legacy, still linked)
│   ├── interpreter/    # Tree-walk interpreter (legacy, REPL)
│   ├── lsp/            # Language Server Protocol
│   ├── fmt/            # Source formatter
│   ├── pkg/            # Package manager
│   ├── cli/            # Subcommands (update, etc.)
│   └── common/         # Localization, version, platform shims
├── lib/                # Standard library (Tulpar source, embedded at build)
├── runtime/            # cJSON, ARC heap, native FFI
├── examples/           # 30 example programs
├── benchmarks/         # Multi-language benchmark suite
├── installer/          # Inno Setup script for Windows MSI
├── wasm/               # WebAssembly target (separate build)
└── cmake/              # CMake modules + EmbedLibraries.cmake
```

### Execution backends

| Backend            | Status  | Notes                                           |
|--------------------|---------|-------------------------------------------------|
| **AOT (LLVM 18)**  | Primary | LLVM IR → native via clang. Default for `tulpar <file>`. |
| **Bytecode VM**    | Active  | Fast startup; used by `--vm` and as AOT fallback. |
| **x64 JIT**        | Legacy  | Direct emitter; kept for benchmarking.          |
| **Tree-walk**      | Legacy  | REPL-only.                                      |

## Examples

Thirty example programs live in [examples/](examples/). A few highlights:

| File                          | Demonstrates                              |
|-------------------------------|-------------------------------------------|
| `01_hello_world.tpr`          | Basic syntax, types, arithmetic           |
| `06_data_structures.tpr`      | Arrays + JSON                             |
| `10_try_catch.tpr`            | Error handling                            |
| `12_threaded_server.tpr`      | Threaded HTTP server                      |
| `13_database.tpr`             | SQLite via the built-in `db_*` API        |
| `api_wings_crud.tpr`          | REST CRUD on top of Wings                 |
| `tulpar_api_demo.tpr`         | Full TulparAPI app with middleware        |
| `benchmark.tpr`               | The `fib` / `loopsum` benchmark workloads |

Run any of them with `tulpar examples/<file>.tpr`.

## Documentation

Full docs (Türkçe & English) at **<https://tulparlang.dev>**.

Local docs in this repo:

- [docs/QUICKSTART.md](docs/QUICKSTART.md)
- [docs/KULLANIM.md](docs/KULLANIM.md) — language reference (Türkçe)
- [docs/MATH_FUNCTIONS.md](docs/MATH_FUNCTIONS.md)
- [docs/PLATFORM_SUPPORT.md](docs/PLATFORM_SUPPORT.md)

## Contributing

1. Fork the repository
2. Create a feature branch (`git checkout -b feature/name`)
3. Commit your changes
4. Push to the branch
5. Open a Pull Request

## License

MIT — see [LICENSE](LICENSE).

## Author

**Hamza Ortatepe** · [@hamer1818](https://github.com/hamer1818)

---

<div align="center">

[Site](https://tulparlang.dev) · [Docs](https://tulparlang.dev/intro/getting-started/) · [Examples](examples/) · [Releases](https://github.com/hamer1818/TulparLang/releases) · [Issues](https://github.com/hamer1818/TulparLang/issues)

</div>
