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

### Verifying releases

Every release ships with `SHA256SUMS.txt` (asset hashes) and
`SHA256SUMS.txt.asc` (a detached GPG signature over that manifest by
the TulparLang Release key). The install scripts already verify the
SHA-256 hashes; the GPG signature is an optional extra layer.

```bash
# Import the release-signing public key (one-time)
curl -fsSL https://raw.githubusercontent.com/hamer1818/TulparLang/main/release-public.asc \
  | gpg --import

# Verify (download both files from the release page first)
gpg --verify SHA256SUMS.txt.asc SHA256SUMS.txt
sha256sum -c SHA256SUMS.txt
```

Public key fingerprint:

```
CE5C 22BD EA61 58BC 8221 3A7E  4396 41B3 0E8D FDEE
```

The key is ed25519, valid until 2028-05-09, used only for release
signing. If `gpg --verify` reports `Good signature` and the
fingerprint matches, the manifest came from the release workflow
unchanged.

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

### Streaming (SSE / WebSocket)

Long-lived connections take over the socket and signal
`{"_stream": 1}` so wings skips its normal response envelope:

```tulpar
import "wings";

func events() {
    int fd = wings_current_fd();
    socket_send(fd, wings_sse_headers());
    int i = 0;
    while (i < 5) {
        socket_send(fd, wings_sse_event("tick", "{\"n\":" + toString(i) + "}"));
        sleep(300);
        i = i + 1;
    }
    return {"_stream": 1};
}

get("/events", "events");
listen(8093);
```

`curl -N http://127.0.0.1:8093/events` will print 5 tick frames live.
WebSocket handlers use `wings_ws_upgrade(req)` for the handshake,
`wings_ws_send_text(fd, payload)` + `wings_ws_recv_frame(fd)` for the
frame loop, and the same `{"_stream": 1}` return value.

## Package management

Vendored stdlib (`wings`, `router`, `http_client`, `orm`, …) is
embedded in the binary — no install step. Third-party packages flow
through `tulpar pkg`:

```bash
tulpar pkg init my-app           # writes tulpar.toml + default registry URL
tulpar pkg search                # browse the catalog (empty query = all)
tulpar pkg search wings          # filter by name + description
tulpar pkg info demo             # version list, downloads, install hint
tulpar pkg add demo@^1.0         # add a dependency line
tulpar pkg install               # vendor everything into tulpar_modules/
tulpar pkg publish --token $T    # publish current package to the registry
```

`tulpar pkg init` seeds the canonical registry
(`https://api.pkg.tulparlang.dev`); override via the `[registry] url`
line in `tulpar.toml`, `--registry <url>` flag, or `TULPAR_REGISTRY`
env. Semver ranges are full 2.0.0 (`^`, `~`, `*`, `>=,<`, pre-release
plus build metadata). Installs go through a lockfile (`tulpar.lock`)
with SHA-256 checksums so re-installs are byte-stable.

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

<!-- BENCH:META START -->
> _Auto-updated by CI on every push to `main`. Last run: **2026-05-13T12:46:17Z** UTC · commit [`5e3529d`](../../commit/5e3529d7af7d5e6a6d4fc12c1a6179d3058340c3) · runner `Linux` · `AMD EPYC 7763 64-Core Processor` (4 CPUs). Methodology: [benchmarks/CI.md](benchmarks/CI.md)._
<!-- BENCH:META END -->

> **Scope.** All numbers below are **microbenchmarks** — tight integer
> loops and small JSON handlers on a single machine, localhost loopback
> for HTTP. They isolate compiler / runtime / scheduler costs and let
> Tulpar be compared against C, Rust, Go, Node, etc. on the same shape
> of workload. They do **not** model real production traffic (cold
> starts, large payloads, distributed clients, p99 tail latency, GC
> pressure under load). Treat them as a peer-comparison floor for the
> hot path, not as a production projection.

### CPU benchmarks

10M-iteration sum and recursive `fib(35)`. See
[benchmarks/RESULTS.md](benchmarks/RESULTS.md) for the raw numbers from
the most recent CI run and the optimization-pipeline writeup.

<!-- BENCH:CPU_TABLE START -->
_Wall time of the inner loop, best of 5 runs. **Lower is faster.**_

| Workload | Tulpar AOT (LLVM) | C (gcc -O2) | Rust (-O3) | Go | Node.js | Python |
|---|---:|---:|---:|---:|---:|---:|
| loopsum (ms) | **1.8** | 0.6 | 0.8 | 4.2 | 36.6 | 840.5 |
| fib(35) (ms) | **30.5** | 18.4 | 29.0 | 53.6 | 132.9 | 1216.9 |

Tulpar AOT lands at **1.66×–3.00× of C (gcc -O2)** on these microbenchmarks (i.e. C-comparable, with a small multiplicative gap), **4.4–20.3× faster than Node.js**, and **40–467× faster than Python**.
<!-- BENCH:CPU_TABLE END -->

### HTTP throughput

3000 GETs across 4 keep-alive connections, single localhost loop. Same
JSON handler running on every server.

<!-- BENCH:HTTP_TABLE START -->
### Low concurrency

_3 000 GETs over 4 keep-alive connections; single localhost run; each server hosting the same JSON handler. Apples-to-apples view: one client thread per connection. Higher req/sec is better._

| Server | Scheduling model | req/sec | vs Node.js |
|---|---|---:|---:|
| **Tulpar listen_async** | OS thread spawned per connection | 16 213 | **1.62× faster** |
| **Tulpar listen_evented** | single thread, poll()-multiplexed | 16 077 | **1.60× faster** |
| **Tulpar listen_pool** | worker pool sized to host CPU count, sharing accept() | 16 075 | **1.60× faster** |
| **Tulpar evented + cache** | evented + wire-byte cache for cached_get routes | 15 966 | **1.59× faster** |
| **Tulpar listen** | single thread, one request at a time | 15 900 | **1.59× faster** |
| Node.js http | single-thread event loop | 10 024 | _(baseline)_ |
| Python ThreadingHTTP | OS thread spawned per request | 98 | 102.29× slower |

Tulpar Wings listeners at this concurrency serve **~1.62× the throughput of Node.js** built-in `http`.

### High concurrency

_12 000 GETs over 16 keep-alive connections; single localhost run; each server hosting the same JSON handler. Same per-thread workload as the low-concurrency block, scaled up to surface multi-core scaling. Higher req/sec is better._

| Server | Scheduling model | req/sec | vs Node.js |
|---|---|---:|---:|
| **Tulpar listen_async** | OS thread spawned per connection | 16 520 | **1.28× faster** |
| **Tulpar evented + cache** | evented + wire-byte cache for cached_get routes | 16 378 | **1.27× faster** |
| **Tulpar listen** | single thread, one request at a time | 16 183 | **1.26× faster** |
| **Tulpar listen_pool** | worker pool sized to host CPU count, sharing accept() | 16 170 | **1.25× faster** |
| **Tulpar listen_evented** | single thread, poll()-multiplexed | 16 159 | **1.25× faster** |
| Node.js http | single-thread event loop | 12 886 | _(baseline)_ |
| Python ThreadingHTTP | OS thread spawned per request | 391 | 32.96× slower |

Tulpar Wings listeners under this load serve **~1.28× the throughput of Node.js** built-in `http`.
<!-- BENCH:HTTP_TABLE END -->

Reproduce locally: `python benchmarks/http_bench.py --requests 3000 --connections 4`.

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

Long-lived connection types (Server-Sent Events, WebSocket upgrade)
take over the socket and return `{"_stream": 1}` so the dispatcher
skips its envelope build — see [Streaming](#streaming-sse--websocket)
above and [`examples/api_wings_sse.tpr`](examples/api_wings_sse.tpr) /
[`examples/32_wings_ws_frames.tpr`](examples/32_wings_ws_frames.tpr)
for end-to-end demos.

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
tulpar pkg <subcommand>       # Package manager:
                              #   init, list, add, remove, install,
                              #   search, info, publish
tulpar doc <file.tpr>         # Markdown reference generator
tulpar debug <file.tpr>       # DAP server (VS Code debugger)
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
| Wings helpers   | `wings_openapi`, `wings_metrics_prom`, `wings_cookies`, `log_info`, `log_error`, `wings_current_fd`, `wings_sse_headers`, `wings_sse_event`, `wings_ws_upgrade`, `wings_ws_send_text`, `wings_ws_send_close`, `wings_ws_send_pong`, `wings_ws_send_frame`, `wings_ws_recv_frame`, `wings_ws_accept_key` |
| Crypto / encode | `sha1`, `sha1_hex`, `sha256`, `base64_encode`, `base64_decode`                |
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
(PRs #30 and #31 — see [STATUS.md](STATUS.md)). Single execution
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
| `api_wings_sse.tpr`           | Server-Sent Events via the streaming dispatcher |
| `32_wings_ws_frames.tpr`      | WebSocket send/recv frame round-trip        |
| `31_crypto_sse_ws.tpr`        | `sha1` / `base64` / WS accept-key + SSE formatting |
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

- [docs/PLATFORM_SUPPORT.md](docs/PLATFORM_SUPPORT.md) — per-OS prerequisites, build steps, troubleshooting
- [RELEASING.md](RELEASING.md) — how stable releases are cut
- [STATUS.md](STATUS.md) — current shipping status + open gaps

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
