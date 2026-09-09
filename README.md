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
programming language built on **LLVM** (18 through 22). It pairs
Python-shaped syntax with native binary performance. On a nine-language microbenchmark suite
where every language reads its workload size from the environment and
the outputs are verified to match, Tulpar AOT **beats C on `fib`
(2.7× vs `gcc -O3 -march=native -flto`) and ties it on `sieve` and
`intloop`**, while C takes `arrayiter` and `strcat` once given equal
flags and the same integer-formatting routine. See
[Performance](#performance) for the flag matrix and the follow-up
attribution run. On a localhost JSON-API
micro-benchmark the `listen_async` Wings listener serves **~1.9× the
throughput of Node.js' built-in `http`**. It ships with a
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

func home(req) {
    return {"hello": "world", "ts": now_iso8601()};
}

get("/", home);
serve(8080);
```

```bash
tulpar build api.tpr && ./api
curl http://127.0.0.1:8080/
# {"hello":"world","ts":"2026-05-07T18:30:00Z"}
```

### A persistent CRUD API in 7 lines

`model()` creates the SQLite table, `resource()` wires five REST routes
plus request validation (422) and Swagger `/docs` — all queries run as
bound-parameter SQL:

```tulpar
import "wings";
import "orm";

database("notes.db");
json Note = model("notes", {"id": "pk", "title": "str!", "done": "bool"});

resource("/notes", Note);   // GET/POST /notes, GET/PUT/DELETE /notes/:id
serve();                    // :8484 + /docs
```

Custom endpoints live right next to it, with ActiveRecord-style calls:
`Note.find(3)`, `Note.where("done = ?", [0])`, `Note.create({...})`,
`Note.save(obj)`. See
[`examples/wings_orm_resource.tpr`](examples/wings_orm_resource.tpr)
and the full cheatsheet in [WINGS_CHEATSHEET.md](WINGS_CHEATSHEET.md).

Need HTTPS? Swap `serve(8080)` for
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

- **Native speed.** LLVM AOT compilation, in C's performance class on
  integer kernels: 2.7× C on `fib`, tied on `sieve` and `intloop`, and
  far ahead of Node/Python/Java/C# throughout. On a localhost JSON-API microbenchmark the `listen_async`
  Wings listener is **1.91× Node.js' `http`** and **2.91× CPython's
  `ThreadingHTTPServer`** in throughput. See
  [Performance](#performance) and
  [benchmarks/fair/README.md](benchmarks/fair/README.md) for the
  methodology.
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
  AOT/LLVM compiler, package manager, formatter, language server, and
  self-updater.
- **Editor-aware.** A real LSP server (`tulpar --lsp`) drives the
  bundled VS Code extension with Rust-style diagnostics, hover types,
  completion, go-to-definition, find-references, rename, and
  signature help.
- **Cross-platform.** Linux, macOS (Apple Silicon + Intel),
  Windows (MSYS2 MinGW build). WebAssembly target available for
  browser use.

## Performance

### Cross-language microbenchmarks

_Wall time, best of 7 runs, in milliseconds. **Lower is faster**; the
fastest entry in each column is bold. Run
`python3 benchmarks/fair/run.py` to reproduce; last run on commit
`3edb4f4` (Linux, LLVM 22)._

| Language | fib(32) | sieve(5M) | strcat(2M) | arrayiter(5M) | intloop(50M) |
|---|---:|---:|---:|---:|---:|
| **Tulpar AOT** | **0.6** | 7.7 | **13.2** | **1.2** | 134.5 |
| C (gcc -O2) | 1.6 | **7.6** | 37.6 | 2.2 | **134.4** |
| C++ (g++ -O2) | 1.9 | 8.0 | 14.7 | 2.7 | 134.9 |
| Rust (-O3) | 3.8 | 8.1 | 18.7 | 1.5 | 144.0 |
| Go | 6.7 | 8.5 | 24.3 | 4.3 | 134.5 |
| Java | 12.4 | 19.7 | 32.9 | 18.2 | 144.1 |
| C# (.NET) | 20.3 | 20.9 | 31.2 | 18.3 | 149.3 |
| Node.js | 24.6 | 28.2 | 96.4 | 18.5 | 712.2 |
| Python | 140.8 | 448.1 | 200.0 | 410.2 | 3127.3 |

Empty-program baseline on the same machine (process start-up, included in
every number above): C 0.17 · **Tulpar 0.23** · C++ 0.44 · Python 5.6 ·
C# 8.2 · Node 10.7 ms.

**Tulpar AOT is the fastest of the nine on `fib`, `strcat` and
`arrayiter`, and within 0.1 ms of C on `sieve` and `intloop`.**

#### Does the lead survive C's best compiler flags?

The table above compiles C with `gcc -O2`, the same generic baseline
Tulpar itself targets (its LLVM target CPU is `"generic"` by default;
`-march=native` is opt-in and *not* used for these numbers). That is a
fair default, but "faster than C" is an extraordinary claim, so the
comparison was re-run with C given its best flags — 12 interleaved
repetitions, median ± MAD, on one AMD Ryzen 7 9800X3D (Zen 5, 5.27 GHz):

| Benchmark | C `-O2` | C `-O3 -march=native -flto` | Tulpar (generic) | Verdict |
|---|---:|---:|---:|---|
| `fib(32)` | 2.49 | 2.08 ± 0.04 | **0.77 ± 0.02** | Tulpar 2.7× — **holds** |
| `strcat(2M)` | 37.95 | 36.98 ± 0.37 | **13.27 ± 0.21** | Tulpar 2.8× — **holds** |
| `sieve(5M)` | 7.86 | 7.75 ± 0.14 | 7.77 ± 0.10 | tie (inside MAD) |
| `arrayiter(5M)` | 2.50 | **1.85** | 2.03 | **C wins with `native`** |

So the honest statement is narrower than a single "beats C": `fib` is a
robust win that survives C's best flags, `sieve` and `intloop` are ties,
and **`arrayiter` is a Tulpar win only at equal generic flags — C takes
it back with `-march=native`.**

**`strcat` was withdrawn as a win.** A follow-up control gave C the same
hand-rolled integer-to-string routine Tulpar uses instead of `snprintf`:
C then runs it in **11.07 ± 0.10 ms against Tulpar's 13.85 ± 0.39** — C
is 1.25× ahead. The original 2.8× "win" was entirely the
`snprintf`-versus-specialised-`itoa` asymmetry, i.e. a standard-library
comparison, not a compiler one.

**And the `fib` win does not generalise.** Running the whole
self-recursion family — `ackermann`, `tak`, `treesum`, plus a mutual
recursion negative control — showed the clone-chain pass gaining 6.33× on
`fib` but only 1.04–1.57× elsewhere, and **regressing `ackermann` by
21%**. Diagnosing that regression found the chain depth was simply too
large: a single clone is enough to break the self-recursion SCC edge, and
deeper chains only add code and layout churn. `SELFREC_DEPTH` went 4 → 1,
which removed the regression and made `fib` faster still (0.67 → 0.47 ms).

Even so, **`gcc` still beats Tulpar across the rest of that family** —
`treesum` 3.6×, `ackermann` 3.2×, `tak` 1.5× — while Tulpar leads `clang`
and `rustc` there. "Faster than C at recursion" is **not** supported;
only `fib` is. Full diagnosis, depth sweep, matched `clang`/`rustc`
baselines and raw CSV:
[`benchmarks/fair/recursion/`](benchmarks/fair/recursion/README.md).

Useful calibration from that run: compiling the *same C source* with gcc
versus clang swings the result by **1.21×–3.82×** on these kernels. Any
cross-language gap smaller than that is a compiler difference, not a
language one.

Two further disclosures the numbers alone don't carry:

- **`strcat` compares different tools, not just different compilers.**
  C's version is a proper geometric-growth buffer (not a naive
  `realloc`-per-iteration loop), but it formats each number with
  `snprintf`, a general-purpose formatter, while Tulpar uses a
  specialised integer-to-string routine that was optimised for exactly
  this path. A C programmer hand-rolling `itoa` would close much of
  that gap.
- **`fib` measures inlining, not just call overhead**, despite its
  label. Both languages genuinely execute the exponential tree — timing
  scales as φ² per +2 in `n` (measured: C 2.67×, Tulpar 2.30×), so
  neither is collapsing it to a linear form. But Tulpar's lower exponent
  shows part of its lead comes from the self-recursion clone chain
  enabling subexpression sharing the C build does not get — and that
  pass, measured across the recursion family, turns out to be a `fib`
  outlier rather than a general win.

#### What this suite does *not* establish

Five integer/string kernels on one machine cannot support "fastest
language". Not covered: floating point and SIMD (matmul, n-body,
mandelbrot), allocation pressure and hash-map/JSON workloads — which is
where the arena model's costs would actually appear — pointer chasing,
sorting, multi-threaded scaling, RSS, and sustained-load p99 latency.
The defensible reading is: Tulpar is **in C's performance class on
integer kernels** and decisively ahead of Node/Python/Java/C#. Its flat
loop and recursion codegen is C-class, not above it; the one verified
win over C is `fib`.

#### How these numbers are kept honest

An earlier version of this table (still visible, clearly marked, in
[benchmarks/RESULTS.md](benchmarks/RESULTS.md)) was audited in September
2026 and **thrown out**. It had three flaws that each invalidated the
result on their own — the worst being that only Tulpar read its workload
size at run time, while `gcc -O2` and `rustc -O3` folded their loops to a
closed form and "won" without executing them. The suite in
[`benchmarks/fair/`](benchmarks/fair/README.md) fixes all three:

- **Every language reads `BENCH_N` from the environment**, so no compiler
  can constant-fold the workload away.
- **Same algorithm, same data structure**, with each language using its
  own idiomatic tool (`StringBuilder` / `std::string` /
  `strings.Builder` / `Int32Array` / `int[]`) — forcing a naive form on
  someone measures the trap, not the language.
- **Outputs are compared across languages**; if they disagree the row is
  reported invalid instead of published.
- Warm-up run discarded, best **and** median reported, and the
  empty-program baseline printed alongside so you can see how much of a
  number is process start-up.

> **Scope.** These are microbenchmarks — tight loops on a single machine.
> They isolate compiler and runtime costs and let Tulpar be compared
> against C, Rust, Go and Node on the same shape of workload. They do
> **not** model production traffic (cold starts, large payloads,
> distributed clients, p99 tail latency, GC pressure under load).

### HTTP throughput

> **\*** These figures come from `listen_pool`, a multi-threaded listener,
> and Wings keeps mutable globals (request counters, the router table).
> Tulpar has **no memory model**: shared mutable state across threads is
> neither atomic nor reliably visible — measured, with reproducers, in
> [docs/mindmap/Concurrency.md](docs/mindmap/Concurrency.md). The throughput
> number itself is real; the concurrency contract underneath it is
> undocumented and untested, so treat the counters as approximate until that
> gate closes.

<!-- BENCH:META START -->
> _Baked from local benchmark run (best of 5). Last run: **2026-05-21T08:23:24Z** UTC · commit [`d91f184`](../../commit/d91f184c4447c2607d73dca070c704faaf87fbf3) · runner `Windows` · `developer machine` (16 CPUs). Methodology + Local Run instructions: [benchmarks/CI.md](benchmarks/CI.md)._
<!-- BENCH:META END -->


3000 GETs across 4 keep-alive connections, single localhost loop. Same
JSON handler running on every server.

<!-- BENCH:HTTP_TABLE START -->
### Low concurrency

_3 000 GETs over 4 keep-alive connections; single localhost run; each server hosting the same JSON handler. Apples-to-apples view: one client thread per connection. Higher req/sec is better._

| Server | Scheduling model | req/sec | vs Node.js |
|---|---|---:|---:|
| **Tulpar listen_pool** | worker pool sized to host CPU count, sharing accept() | 64 085 | **2.77× faster** |
| **Tulpar evented + cache** | evented + wire-byte cache for cached_get routes | 62 057 | **2.68× faster** |
| **Tulpar listen_evented** | single thread, poll()-multiplexed | 60 316 | **2.61× faster** |
| **Tulpar listen_async** | OS thread spawned per connection | 50 244 | **2.17× faster** |
| **Tulpar listen** | single thread, one request at a time | 29 458 | **1.27× faster** |
| Node.js http | single-thread event loop | 23 133 | _(baseline)_ |
| Python ThreadingHTTP | OS thread spawned per request | 14 755 | 1.57× slower |

5 of the Tulpar Wings listeners at this concurrency beat Node.js' built-in `http`, by **1.27×–2.77×** depending on scheduling model.

### High concurrency

_12 000 GETs over 16 keep-alive connections; single localhost run; each server hosting the same JSON handler. Same per-thread workload as the low-concurrency block, scaled up to surface multi-core scaling. Higher req/sec is better._

| Server | Scheduling model | req/sec | vs Node.js |
|---|---|---:|---:|
| **Tulpar listen_evented** | single thread, poll()-multiplexed | 56 303 | **1.65× faster** |
| **Tulpar evented + cache** | evented + wire-byte cache for cached_get routes | 55 843 | **1.64× faster** |
| **Tulpar listen_pool** | worker pool sized to host CPU count, sharing accept() | 55 143 | **1.62× faster** |
| **Tulpar listen_async** | OS thread spawned per connection | 54 385 | **1.59× faster** |
| Node.js http | single-thread event loop | 34 138 | _(baseline)_ |
| **Tulpar listen** | single thread, one request at a time | 28 422 | 1.20× slower |
| Python ThreadingHTTP | OS thread spawned per request | 14 611 | 2.34× slower |

4 of the Tulpar Wings listeners under this load beat Node.js' built-in `http`, by **1.59×–1.65×** depending on scheduling model.
<!-- BENCH:HTTP_TABLE END -->

Reproduce locally: see [benchmarks/CI.md](benchmarks/CI.md) for how to run the full CPU and HTTP benchmark suite.

## Wings — four ways to listen

Wings is the embedded HTTP framework. The front door is `serve()`:
`serve()` picks the branded default port 8484, `serve(8080)` binds an
explicit port, and `serve(8080, 4)` runs a 4-worker thread pool. The
listeners below are the advanced modes behind it; the handler API stays
the same, only the scheduling model changes:

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
tulpar <file.tpr>             # Run via AOT (the only execution path; native speed)
tulpar build <file.tpr> [out] # Standalone native binary

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
│   ├── aot/            # LLVM AOT backend (the only execution path)
│   ├── vm/             # Shared runtime: aot_* builtins, arena allocator, value types
│   ├── lsp/            # Language Server Protocol
│   ├── fmt/            # Source formatter
│   ├── pkg/            # Package manager (tulpar.toml + lockfile)
│   ├── cli/            # Subcommands (update, etc.)
│   └── common/         # Localization, version, platform shims, TLS plumbing
├── lib/                # Standard library (Tulpar source, embedded at build)
├── runtime/            # cJSON, arena heap, native FFI
├── examples/           # 47 example programs
├── benchmarks/         # Multi-language benchmark suite (CPU + HTTP)
├── tests/              # Smoke tests + lib/test.tpr regression suites
├── installer/          # Inno Setup script for Windows installer
├── wasm/               # WebAssembly target (separate build)
└── cmake/              # CMake modules + EmbedLibraries.cmake
```

### Execution model

Tulpar follows the C/Rust/Go model — a **single AOT/LLVM execution path**.
`tulpar <file>` AOT-compiles and runs; there is no VM fallback, so an AOT
failure is a hard error.

| Component          | Role                                                        |
|--------------------|-------------------------------------------------------------|
| **AOT (LLVM)**     | The only execution path. LLVM IR → native via clang.        |
| **`src/vm/` runtime** | Shared runtime linked into AOT'd binaries (the `aot_*` builtins, arena allocator, value types) — *not* an interpreter. |

The bytecode VM interpreter and the REPL were removed in **v3.0.0**
(2026-06-15); `--vm`/`--run` are ignored with a warning and `--repl` prints a
removal notice. The legacy tree-walk interpreter and x64 JIT were retired
earlier, in May 2026. See [STATUS.md](STATUS.md).

## Examples

Dozens of example programs live in [examples/](examples/). A few
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
