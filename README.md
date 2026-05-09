# TulparLang

<div align="center">

[![Build](https://github.com/hamer1818/TulparLang/actions/workflows/build.yml/badge.svg)](https://github.com/hamer1818/TulparLang/actions/workflows/build.yml)
[![License](https://img.shields.io/badge/license-MIT-green.svg)](LICENSE)
[![Platform](https://img.shields.io/badge/platform-Linux%20|%20macOS%20|%20Windows-lightgrey.svg)]()
[![Website](https://img.shields.io/badge/site-tulparlang.dev-00e5ff.svg)](https://tulparlang.dev)
[![Releases](https://img.shields.io/github/v/release/hamer1818/TulparLang?label=release)](https://github.com/hamer1818/TulparLang/releases/latest)

**Python-easy syntax. C-class performance. HTTP-ready out of the box.**

</div>

---

## What is TulparLang?

**TulparLang** is an open-source, statically-typed, ahead-of-time compiled
programming language built on **LLVM 18**. It pairs Python-shaped syntax
with native binary performance — within ~1.5–1.8× of `gcc -O2` on
integer microbenchmarks (`loopsum`, `fib(35)`), and on a localhost
JSON-API micro-benchmark the `listen_async` Wings listener serves
**~1.9× the throughput of Node.js' built-in `http`** — and ships with a
batteries-included standard library so you can build a production
HTTP/HTTPS API without installing a single external dependency.

Files use the `.tpr` extension. The whole toolchain — compiler, runtime,
package manager, formatter, language server, and REST framework — is a
single ~7 MB executable. Source code, identifiers, and strings are
UTF-8 throughout, with first-class support for both Turkish and English
keywords and diagnostics.

**Use it for:** dependency-free native backend services, REST APIs and
microservices, CLI tools, scripting and automation. The sweet spot is
"Python-shaped ergonomics with a single-binary deploy and no runtime to
install on the target machine" — if that matches your shape, the rest
of the README is for you.

**Highlights:**

- **Native HTTP/HTTPS server** — four listener flavours (sync, thread-
  per-connection, worker pool, single-thread event loop) plus a TLS
  listener with OpenSSL, all in the bundled `wings` / `wings_tls`
  stdlib.
- **First-class JSON** — built-in type, literal syntax, dot access,
  `toJson` / `fromJson` round-trips, no library import required.
- **AOT-compiled native binaries** — `tulpar build foo.tpr` produces
  a standalone executable you can ship without the Tulpar toolchain.
- **One-line installer** with auto-update — `iwr ... | iex` on Windows,
  `curl ... | bash` on Linux/macOS, then `tulpar update` to roll
  forward.

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

func square(int n): int {
    return n * n;
}

print(square(5));   // 25
```

Run it:

```bash
tulpar hello.tpr
```

The default `tulpar <file>` invocation AOT-compiles via LLVM and runs
the resulting native binary — Python-shaped syntax, native-binary
performance (see [Performance](#performance) for the actual numbers).

## Build a REST API in 8 lines

```tulpar
import "wings";

func home() {
    return {"hello": "world", "ts": now_iso8601()};
}

get("/", "home");
listen(8080);
```

```bash
tulpar build api.tpr && ./api
curl http://127.0.0.1:8080/
# {"hello":"world","ts":"2026-05-07T18:30:00Z"}
```

Need HTTPS? Swap `listen(8080)` for
`wings_tls(8443, "server.crt", "server.key")` and you're terminating
TLS via OpenSSL with the same handler API. Wings auto-registers
`/healthz` + `/metrics` and answers CORS preflights so a browser can
hit the API immediately.

## Why TulparLang

- **Native speed.** LLVM 18 AOT compilation. ~1.5–1.8× of `gcc -O2`
  on integer microbenchmarks; on a localhost JSON-API microbenchmark
  the `listen_async` Wings listener is **1.91× Node.js' `http`** and
  **2.91× CPython's `ThreadingHTTPServer`** in throughput. See
  [benchmarks/RESULTS.md](benchmarks/RESULTS.md) for the full table
  and methodology.
- **No build step for prototyping.** `tulpar file.tpr` runs in one step.
  `tulpar build file.tpr` produces a standalone native binary when you
  want to ship.
- **Batteries included.** Wings (HTTP server, four listener flavours +
  TLS), TulparAPI (FastAPI-style routing), ORM (SQLite), HTTP client,
  sockets, threads, regex, CSV, datetime, structured logging, OpenAPI
  generation — all in the default install. No package manager required
  for the standard library.
- **First-class JSON.** `json` is a built-in type with literal syntax
  (`{"k": v}`), dot access (`obj.key`), and `toJson` / `fromJson`
  round-trips that survive Unicode, embedded quotes, deeply nested
  structures, and large arrays.
- **HTTPS out of the box.** Statically linked OpenSSL on every
  platform. Both client (`http_get("https://...")`) and server
  (`wings_tls(...)`) work with no extra setup.
- **UTF-8 native.** Source code, strings, identifiers, error messages
  all UTF-8. Localised CLI follows the system locale; override with
  `TULPAR_LANG=tr` or `TULPAR_LANG=en`.
- **Single-binary toolchain.** One `tulpar` executable bundles the
  AOT compiler, bytecode VM, package manager, formatter, language
  server, and self-updater.
- **Editor-aware.** A real LSP server (`tulpar --lsp`) drives the
  bundled VS Code extension with Rust-style diagnostics, hover types,
  completion, go-to-definition, find-references, rename, and
  signature help.
- **Cross-platform.** Linux, macOS (Apple Silicon + Intel),
  Windows (MSYS2 MinGW build). WebAssembly target available for
  browser use.

## Performance

> **Scope.** All numbers below are **microbenchmarks** — tight integer
> loops and small JSON handlers on a single machine, localhost loopback
> for HTTP. They isolate compiler / runtime / scheduler costs and let
> Tulpar be compared against C, Rust, Go, Node, etc. on the same shape
> of workload. They do **not** model real production traffic (cold
> starts, large payloads, distributed clients, p99 tail latency, GC
> pressure under load). Treat them as a peer-comparison floor for the
> hot path, not as a production projection.

### CPU benchmarks

10M-iteration sum and recursive Fibonacci(35), best of 5 runs on
Windows 10/x86_64. See [benchmarks/RESULTS.md](benchmarks/RESULTS.md)
for raw numbers and the full optimization-pipeline writeup.

| Benchmark      | Tulpar AOT  | C (gcc -O2) | Rust 1.80 | Go    | Node.js | Python 3.12 |
|----------------|------------:|------------:|----------:|------:|--------:|------------:|
| loopsum (ms)   | **31.4**    | 20.5        | 19.5      | 30.2  | 47.1    | 510.6       |
| fib(35) (ms)   | **55.6**    | 31.6        | 41.2      | 62.3  | 106.8   | 959.7       |

Tulpar lands at **1.53–1.76× C(gcc -O2)** — same neighbourhood as Go,
faster than Node, an order of magnitude ahead of Python.

### HTTP throughput

3000 GETs across 4 keep-alive connections, single localhost loop. Same
JSON handler running on every server.

| Server                  | req/sec    | × Tulpar `listen` | vs Node.js |
|-------------------------|-----------:|------------------:|-----------:|
| **Tulpar `listen_async`**   | **34 095** | 1.87×             | **1.91× faster** |
| Tulpar `listen_pool x8` | 33 026     | 1.81×             | 1.85× faster |
| Tulpar `listen_evented` | 31 032     | 1.70×             | 1.74× faster |
| Node.js `http`          | 17 805     | 0.98×             | reference  |
| Tulpar `listen` (sync)  | 18 207     | 1.00×             | 1.02×      |
| Python `ThreadingHTTP`  | 11 717     | 0.64×             | 0.66×      |

Reproduce: `python benchmarks/http_bench.py --requests 3000 --connections 4`.

## Wings — four ways to listen

Wings is the embedded HTTP framework. The handler API stays the same;
the listener you pick changes the scheduling model:

| Listener          | Best for                                     |
|-------------------|----------------------------------------------|
| `listen(port)`    | Sync. One in-flight request. Simplest.       |
| `listen_async(port)` | Thread per connection. Best for short bursts. |
| `listen_pool(port, n)` | Pre-spawned workers. Best sustained RPS. |
| `listen_evented(port)` | Single-thread + `poll()` / `WSAPoll`. Best for many idle keep-alive conns (chat, dashboards, SSE). |
| `wings_tls(port, cert, key)` | HTTPS via OpenSSL. Same handler API. |

All five share `_request`, `_response`, route counters, `/healthz` /
`/metrics` auto-routes, OPTIONS preflight handling, structured
logging, and OpenAPI 3.0 generation via `wings_openapi(title, version)`.

## Command reference

The full list lives at <https://tulparlang.dev/reference/cli/>; the
short version:

```bash
tulpar <file.tpr>             # Run via AOT (default; native speed)
tulpar --vm <file.tpr>        # Bytecode VM (instant startup)
tulpar build <file.tpr> [out] # Standalone native binary

tulpar --repl                 # Interactive REPL
tulpar fmt <file.tpr>         # Source formatter
tulpar typecheck <file.tpr>   # Standalone typechecker
tulpar pkg <subcommand>       # Package manager (init, add, install, publish)
tulpar --lsp                  # Language server (editor integration)

tulpar version                # Show installed version
tulpar update [--check]       # Self-update from tulparlang.dev
tulpar --help                 # Show command reference
```

CLI output language follows the system locale — Turkish on TR
machines, English everywhere else. Override with `TULPAR_LANG=tr` or
`TULPAR_LANG=en`. The `[typecheck]` warnings always run as a build
pre-pass; suppress with `--no-typecheck` or `TULPAR_NO_TYPECHECK=1`.

## Build from source

**Prerequisites:** GCC or Clang, LLVM 18+, CMake 3.14+. Optional:
OpenSSL (enables `https://` for the HTTP client and `wings_tls`).

**Linux / macOS:**

```bash
./build.sh
```

**Windows (MSYS2 MinGW64):**

```powershell
.\build.ps1
# or: build.bat
```

**Direct CMake (incremental):**

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
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
}
Person p = { name: "Ali", age: 25 };

// Control flow
for (int i = 0; i < 10; i++) { print(i); }

// Error handling
try {
    risky();
} catch (e) {
    print("Error: " + toString(e));
} finally {
    cleanup();
}

// Threading
int mu = mutex_create();
func worker(json arg) {
    mutex_lock(mu);
    // critical section
    mutex_unlock(mu);
}
int t = thread_create(worker, 42);
thread_detach(t);
```

Full guide at <https://tulparlang.dev/guide/syntax/> · single-page
language reference at <https://tulparlang.dev/reference/language/>.

## Standard library

All modules below are **embedded in the binary** — `import "name"`
just works, no package install required.

| Module           | Purpose                                                  |
|------------------|----------------------------------------------------------|
| `wings`          | HTTP server — sync, async, pool, and evented listeners   |
| `wings_tls`      | HTTPS server with OpenSSL — same handler API as `wings`  |
| `tulpar_api`     | FastAPI-style routing & JSON envelopes on top of Wings   |
| `router`         | Express-style URL routing & middleware                   |
| `http_utils`     | HTTP request parsing & response building primitives      |
| `http_client`    | Outbound HTTP/HTTPS — `http_get`, `http_post_json`, …    |
| `middleware`     | Built-in middleware (logger, CORS, auth, rate-limit)     |
| `socket`         | TCP socket wrappers + `poll` / non-blocking helpers      |
| `async`          | `setTimeout` / `setInterval` / promise scaffolding       |
| `orm`            | SQLite ORM — `define_model`, `orm_create`, `orm_where`   |
| `test`           | Jest-style assertion framework + suite runner            |

Native built-ins (no `import` needed):

| Category        | Functions                                                                     |
|-----------------|-------------------------------------------------------------------------------|
| I/O             | `print`, `input`, `inputInt`, `inputFloat`                                    |
| Type conversion | `toInt`, `toFloat`, `toString`, `toBool`                                      |
| Math            | `abs`, `sqrt`, `pow`, `mod`, `sin`, `cos`, `tan`, `log`, `exp`, `floor`, `ceil`, `round`, `random`, `randint`, `min`, `max` |
| String          | `length`, `upper`, `lower`, `trim`, `split`, `join`, `replace`, `substring`, `contains`, `startsWith`, `endsWith`, `indexOf` |
| Regex           | `regex_match`, `regex_search`, `regex_capture`, `regex_replace`               |
| CSV             | `csv_parse`, `csv_emit`                                                       |
| Array / object  | `push`, `pop`, `length`, `range`, `keys`                                      |
| JSON            | `toJson`, `fromJson`, `parse_cookies`                                         |
| Date / time     | `timestamp`, `time_ms`, `now_iso8601`, `format_iso8601`, `parse_iso8601`, `weekday`, `date_add_seconds`, `clock_ms`, `sleep` |
| File            | `file_read`, `file_write`, `file_exists`, `file_delete`, `file_append`, `file_glob` |
| Process / env   | `env`, `exit`                                                                 |
| Sockets         | `socket_server`, `socket_client`, `socket_accept`, `socket_send`, `socket_receive`, `socket_close`, `socket_set_nonblocking`, `socket_poll` |
| TLS (server)    | `tls_init`, `tls_accept`, `tls_recv`, `tls_send`, `tls_close`, `tls_ctx_free` |
| HTTP (native)   | `http_request`, `http_parse_request`, `http_create_response`, `http_status_text`, `path_match`, `parse_query`, `http_recv_request`, `http_should_keepalive` |
| Wings helpers   | `wings_openapi`, `wings_metrics_prom`, `wings_cookies`, `log_info`, `log_error` |
| Database        | `db_open`, `db_execute`, `db_query`, `db_close` (vendored SQLite3)            |
| Threading       | `thread_create`, `thread_detach`, `thread_join`, `mutex_create`, `mutex_lock`, `mutex_unlock`, `mutex_destroy` |
| Memory arena    | `arena_save`, `arena_restore` (per-request bounded memory)                    |

## Architecture

```
TulparLang/
├── src/
│   ├── lexer/          # Tokenization
│   ├── parser/         # Recursive-descent parser, AST nodes
│   ├── typeinfer/      # Type inference (build pre-pass)
│   ├── aot/            # LLVM 18 AOT backend (primary execution path)
│   ├── vm/             # Bytecode VM + compiler (REPL + AOT fallback)
│   ├── lsp/            # Language Server Protocol
│   ├── fmt/            # Source formatter
│   ├── pkg/            # Package manager (tulpar.toml + lockfile)
│   ├── cli/            # Subcommands (update, etc.)
│   └── common/         # Localization, version, platform shims, TLS plumbing
├── lib/                # Standard library (Tulpar source, embedded at build)
├── runtime/            # cJSON, ARC heap, native FFI
├── examples/           # 38 example programs
├── benchmarks/         # Multi-language benchmark suite (CPU + HTTP)
├── tests/              # Smoke tests + lib/test.tpr regression suites
├── installer/          # Inno Setup script for Windows installer
├── wasm/               # WebAssembly target (separate build)
└── cmake/              # CMake modules + EmbedLibraries.cmake
```

### Execution backends

| Backend            | Status   | Notes                                            |
|--------------------|----------|--------------------------------------------------|
| **AOT (LLVM 18)**  | Primary  | LLVM IR → native via clang. Default for `tulpar <file>`. |
| **Bytecode VM**    | Active   | Fast startup; powers `--vm` and the REPL, plus AOT-failure fallback. |

The legacy tree-walk interpreter and x64 JIT were retired in May 2026
(PRs #30 and #31 — see [EKSIKLER.md](EKSIKLER.md)). Single execution
path means bug fixes apply uniformly to AOT and VM.

## Examples

Thirty-eight example programs live in [examples/](examples/). A few
highlights:

| File                          | Demonstrates                                |
|-------------------------------|---------------------------------------------|
| `01_hello_world.tpr`          | Basic syntax, types, arithmetic             |
| `06_data_structures.tpr`      | Arrays + JSON                               |
| `10_try_catch.tpr`            | Error handling                              |
| `12_threaded_server.tpr`      | Threaded HTTP server                        |
| `13_database.tpr`             | SQLite via the built-in `db_*` API          |
| `api_wings_crud.tpr`          | REST CRUD on top of Wings                   |
| `tulpar_api_demo.tpr`         | Full TulparAPI app with middleware          |
| `benchmark.tpr`               | The `fib` / `loopsum` benchmark workloads   |

Run any of them with `tulpar examples/<file>.tpr`.

## Documentation

Full docs (Türkçe & English) at **<https://tulparlang.dev>**.

Quick links:

- [Getting Started](https://tulparlang.dev/intro/getting-started/) — install + first program
- [Language Reference](https://tulparlang.dev/reference/language/) — single-page lookup
- [Wings HTTP Server](https://tulparlang.dev/ecosystem/http-server/) — listener API + routing
- [Package Manager](https://tulparlang.dev/ecosystem/package-manager/) — `tulpar.toml`, lockfile, semver ranges
- [CLI Reference](https://tulparlang.dev/reference/cli/) — every flag and subcommand

Local docs in this repo:

- [docs/QUICKSTART.md](docs/QUICKSTART.md)
- [docs/KULLANIM.md](docs/KULLANIM.md) — language reference (Türkçe)
- [RELEASING.md](RELEASING.md) — how stable releases are cut

## Contributing

1. Fork the repository
2. Create a feature branch (`git checkout -b feature/name`)
3. Commit your changes
4. Push to the branch
5. Open a Pull Request

Issues and design discussions welcome at
<https://github.com/hamer1818/TulparLang/issues>.

## License

MIT — see [LICENSE](LICENSE).

## Author

**Hamza Ortatepe** · [@hamer1818](https://github.com/hamer1818)

---

<div align="center">

[Site](https://tulparlang.dev) · [Docs](https://tulparlang.dev/intro/getting-started/) · [Examples](examples/) · [Releases](https://github.com/hamer1818/TulparLang/releases) · [Issues](https://github.com/hamer1818/TulparLang/issues)

</div>
