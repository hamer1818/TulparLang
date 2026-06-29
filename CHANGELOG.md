# Changelog

All notable changes to TulparLang are documented here. The format is based on
[Keep a Changelog](https://keepachangelog.com/), and the project follows
[Semantic Versioning](https://semver.org/): MAJOR for breaking
language/stdlib/ABI changes, MINOR for backwards-compatible features, PATCH for
fixes. Releases are cut by pushing a `v*` tag (see [RELEASING.md](RELEASING.md));
`tulpar --version` reports the tag at release time and `<version>-dev` otherwise.

## [v3.5.0]

Backwards-compatible feature on top of v3.4.0. No breaking changes.

### Added
- **`hmac_sha256(key: str, msg: str) -> str`** — keyed message
  authentication (HMAC-SHA256, RFC 2104) as a lowercase 64-char hex digest,
  built on the in-tree SHA-256 (no OpenSSL). The signing/verification
  building block for signed cookies, webhook signatures and JWT-style
  tokens — verify by recomputing the MAC and comparing. Wired through
  runtime, AOT codegen, typeinfer and the LSP builtin table. Validated
  against the RFC 4231 test vectors.
- **First registry package: `wings_jwt`** (`packages/wings_jwt/`) — HS256
  signed session tokens for wings apps (`sign` / `sign_ttl` / `verify` /
  `decode` / `from_header`), zero dependencies, 8/8 tests. Built on
  `hmac_sha256` + `base64_encode`. The first real, installable content for
  the `api.pkg.tulparlang.dev` registry beyond smoke packages.

### Fixed
- **`db_execute` typecheck return type** restored to `bool` (was wrongly
  changed to `int` in v3.3.0 when the parameterized-SQL overload was added).
  The runtime always returned `VM_BOOL(rc == SQLITE_OK)`, so the catalog lied
  — under `strict = true` this rejected the idiomatic `bool ok = db_execute(…)`
  with "expected bool, got int", which silently broke strict-mode builds of
  the `tulpar-be` registry. `int ok = db_execute(…)` still works (AOT bool→int
  decl coercion is unaffected).

### Docs
- New **Crypto & security** section in the built-ins reference (EN + TR)
  documenting `sha256` / `hmac_sha256` / `password_hash` / `password_verify`
  / `secure_token` / base64 with guidance on which to use where.

## [v3.4.0]

Backwards-compatible feature on top of v3.3.0. No breaking changes.

### Added
- **`secure_token(n: int) -> str`** — cryptographically secure random base62
  string of length `n`, backed by `std::random_device` (OS CSPRNG / `/dev/urandom`),
  unbiased via rejection sampling. Use this — **not** `randint`/`random` (the
  non-crypto `rand()` seeded with `time()`) — for session tokens, salts and any
  other security-sensitive randomness. Wired through runtime, AOT codegen,
  typeinfer and the LSP builtin table.

## [Unreleased] — v3.3.0 (candidate)

Backwards-compatible features on top of v3.2.1. No breaking changes.

### Added (v3.3.0)
- **Parameterized SQL queries.** `db_query(db, sql, params)` and
  `db_execute(db, sql, params)` now accept an optional array of bound values for
  `?` placeholders (`sqlite3_bind_*`), so user input never touches the SQL text —
  injection-safe without manual quote-escaping. The 2-arg forms are unchanged;
  the cached prepared-statement path is reused (constant SQL = one cache entry).
  `db_execute` returns a success bool.
- **Password hashing KDF.** New `password_hash(pw)` and
  `password_verify(pw, stored)` builtins implementing PBKDF2-HMAC-SHA256
  (100k iterations, random 16-byte salt, self-describing
  `pbkdf2_sha256$iters$salt$dk` string, constant-time verify). Built on the
  in-tree SHA-256 — no OpenSSL dependency. Use these for auth instead of bare
  `sha256`.
- **Wings `patch` / `head` / `options` route helpers.** First-class verbs
  alongside `get`/`post`/`put`/`del` (the router matches the method string
  generically, so PATCH/HEAD/OPTIONS requests dispatch correctly).

### Added (earlier, v3.1.0–v3.2.1)
- **SQLite parallel reads under WAL.** A DB handle is now a `DbConn` descriptor
  index (user API unchanged); file-backed databases open a per-thread `sqlite3`
  connection lazily so `listen_pool` workers read in parallel instead of
  serializing on one connection's mutex. Measured read-by-PK throughput
  23.8k → 35.1k RPS (~+47%); RSS stays flat (~9.9 MB). `:memory:`/temp DBs keep
  a single shared connection.
- **`db_open` server-friendly defaults.** `busy_timeout=5000` + WAL +
  `synchronous=NORMAL` on file-backed DBs (write throughput ~2.3× in the stress
  harness). Opt out with `TULPAR_DB_NO_WAL=1`.
- **Wings ergonomics (FastAPI-level).** Function-reference handlers
  (`get("/users", list_users)`), `req` parameter (`req.params.id`, `req.json`),
  response helpers (`ok`/`created`/`not_found`/…), automatic JSON body parse,
  invisible auto-persist (writes to globals survive the per-request arena),
  schema validation (`body_schema({...})` → automatic 422), automatic `/docs`
  (Swagger UI + `/openapi.json`), and a branded default port 8484.
- **Language: `async` `gather(...)`** (concurrent awaits) and **`match`
  destructuring** — arrays (`[head, ..tail]`), json/object fields
  (`{role: "admin", name}`), typed-struct variants (`Circle{r}`), and nested
  patterns.
- Benchmark + stress harness: Wings vs FastAPI comparison and a multi-threaded
  HTTP + SQLite load generator (`benchmarks/`).

### Fixed
- **Thread-safety audit of the AOT runtime** (affects `listen_pool` /
  `listen_async`):
  - `toString()` used a shared, non-thread-local scratch buffer; concurrent
    callers could clobber it, yielding an empty result → malformed SQL → ~1.1%
    spurious 404s. Now `thread_local`.
  - The exception-handler context (`eh_main`/`eh_cur`) was global; a pooled
    handler using `try`/`throw` could `longjmp` across worker threads
    (crash/UB). Now `thread_local`.
  - The dynamic-call cache published its slot key with a plain store — correct
    on x86 TSO but not on ARM/aarch64 (an Apple Silicon / aarch64 target could
    read a stale function pointer). Now an `std::atomic` release/acquire publish.
- **Per-request memory leak on the Wings hot path** — per-request malloc region
  + runtime write-barrier keep RSS flat (ASan clean).
- `db_last_insert_id` / `db_error` codegen signatures (LLVM module-verification
  warning on every DB program).
- Default arguments (missing trailing args pad to boxed `0`), `\e`/`\0` string
  escapes, boxed unary-minus, and a clean Ctrl+C exit (no misleading
  "compile/link failed").

### Repo hygiene
- Stop tracking accidentally-committed local dirs (`github/` dot-less duplicate
  of `.github/`, `claude/` Claude Code lock, `.opencode/` tool config).

## [3.0.0] — 2026-06-15

### Changed (breaking)
- **AOT-only architecture.** The bytecode VM interpreter, the AST→bytecode
  compiler, and the REPL were removed — Tulpar now follows the C/Rust/Go model
  with a single AOT/LLVM execution path. `--vm`/`--run` are ignored with a
  warning; `--repl`/`-i` print a removal notice. An AOT failure is now a hard
  error (no VM fallback).

### Added
- **`async`/`await` v1** — stackful coroutines + event loop (POSIX `ucontext` /
  Windows fibers), non-blocking `sleep_async`, coroutine-aware exception context.
- **`match` v1.1** — literal / `_` / `|`-alternatives / inclusive ranges.
- Cross-platform async build (macOS `ucontext`, Windows fibers, MinGW).

## [2.2.0] — 2026-06-01

### Changed
- CI switched to **stable-only versioning** — releases are cut only on `v*` tag
  pushes (no more rolling per-commit releases).

### Fixed
- VM typed-struct params pass by value (mirrors AOT semantics); bool→int
  coercion at typed local var declarations; wired 8 utility builtins (arena,
  cpu/time, input, `string_pin`).

[Unreleased]: https://github.com/hamer1818/TulparLang/compare/v3.0.0...HEAD
[3.0.0]: https://github.com/hamer1818/TulparLang/compare/v2.2.0...v3.0.0
[2.2.0]: https://github.com/hamer1818/TulparLang/releases/tag/v2.2.0
