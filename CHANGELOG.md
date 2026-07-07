# Changelog

All notable changes to TulparLang are documented here. The format is based on
[Keep a Changelog](https://keepachangelog.com/), and the project follows
[Semantic Versioning](https://semver.org/): MAJOR for breaking
language/stdlib/ABI changes, MINOR for backwards-compatible features, PATCH for
fixes. Releases are cut by pushing a `v*` tag (see [RELEASING.md](RELEASING.md));
`tulpar --version` reports the tag at release time and `<version>-dev` otherwise.

## [v3.9.0]

New backwards-compatible builtin for interactive terminal UIs.

### Added
- **`read_key(): str`** — blocks for a single keypress with no Enter and no echo,
  and returns its name. Arrow keys resolve to `"up"` / `"down"` / `"left"` /
  `"right"`; other special keys to `"enter"` / `"esc"` / `"space"` / `"tab"` /
  `"backspace"`; printable keys return the character itself. Backed by `_getch`
  on Windows and a `termios` raw-mode read on POSIX (with a graceful plain-read
  fallback when stdin is not a TTY). This is the missing primitive for building
  real app-like TUIs — arrow-key navigation, live selection, in-place repaint —
  instead of line-based `input()` prompts.

## [v3.8.1]

### Fixed
- **Windows console UTF-8 for AOT programs.** AOT-compiled binaries run their
  own entry point and call `aot_runtime_init()`, which set the UTF-8 locale and
  started Winsock but never switched the console code page. On Windows this made
  `print(...)` output with box-drawing, emoji, or Turkish characters render as
  code-page mojibake. `aot_runtime_init()` now calls `SetConsoleOutputCP(CP_UTF8)`
  / `SetConsoleCP(CP_UTF8)` (the compiler driver already did this for itself), so
  every compiled program renders UTF-8 correctly with no per-program workaround.

## [v3.8.0]

New backwards-compatible builtin plus a native-codegen correctness fix.

### Added
- **`sys_run(cmd: str): int`** — runs a shell command with inherited stdio (its
  output streams live) and returns the process exit code (`0` = success). Lets
  Tulpar programs drive external tools such as `winget`, `git`, or any CLI.
  Wired through the standard builtin path: runtime binding (`aot_sys_run`),
  typeinfer signature, LSP doc, and LLVM codegen dispatch.
- `TULPAR_AOT_EMIT_LL_PRE=1` — dump the pre-optimization LLVM IR (debug aid,
  companion to `TULPAR_AOT_EMIT_LL`).

### Fixed
- Native (all-int) fast-path codegen mis-compiled two cases exposed by
  value-returning functions that read globals:
  - `while` / `for` conditions that come back boxed (e.g. `i < length(arr)`)
    were emitted as `br i1 true` — an infinite loop. The boxed payload is now
    tested `!= 0`, matching the `if`-condition path.
  - reading an array/object subscript (`arr[i]`) inside a native function
    returned garbage; such statements now fall back to the boxed VMValue
    codegen, mirroring the existing subscript-assignment bail.

## [v3.7.0]

Backwards-compatible **developer-experience round** (design doc:
[WINGS_DX.md](WINGS_DX.md), cheatsheet: [WINGS_CHEATSHEET.md](WINGS_CHEATSHEET.md)).
No breaking changes — every rename keeps the old name as a delegating wrapper.

### Added — ORM v2 (lib/orm.tpr, pure Tulpar)
- **Model handles + UFCS:** `database(path)` + `model(table, schema)` return a
  plain-json handle used method-style — `Note.create({...})`, `Note.find(id)`,
  `Note.all()`, `Note.where("done = ?", [0])`, `Note.first(cond, params)`,
  `Note.count()`, `Note.update(id, {...})`, `Note.save(obj)` (upsert),
  `Note.remove(id)`, `Note.raw(where_sql)` (escape hatch).
- **Schema shorthands** shared with `body_schema` vocabulary: `"pk"`, `"str"`,
  `"int"`, `"float"`, `"bool"` (+ `!` = NOT NULL); anything unrecognized passes
  through as raw SQL (`"TEXT UNIQUE"`). `"bool"` columns are cast to
  `true`/`false` on read — no more hand-written `row_to_note()` converters.
- **Parameterized SQL everywhere:** every generated statement binds values via
  the 3-arg `db_query`/`db_execute` (`?` placeholders); nothing is ever
  string-interpolated into SQL. Multiple databases work — each handle carries
  its own connection.
- New regression suite `tests/orm.test.tpr` (11 cases).

### Fixed — ORM
- **Zero/empty values are no longer silently dropped.** v1 selected columns by
  value truthiness, so `orm_update(id, {"done": 0})` (or `""`/`false`) wrote
  nothing. Column selection now iterates `keys(attrs)` — explicit `0`/`""`/
  `false` persist. Applies to the v1 wrappers too (intentional behavior fix).

### Added — wings DX layer (lib/wings.tpr, pure Tulpar)
- **`resource(path, Model[, opts])`** — automatic REST CRUD from an ORM model
  handle: `GET/POST path`, `GET/PUT/DELETE path/:id`, request schema derived
  from the model (`"str!"` → required, others optional → auto-422), rows
  bool-cast, `/docs` + OpenAPI fed automatically.
  `opts: {"only": [...]}` / `{"except": [...]}`
  (actions: index/show/create/update/destroy). A persistent CRUD API is now
  7 lines (`examples/wings_orm_resource.tpr`).
- **`serve(port, workers)`** — one front door for the server:
  `serve()` → 8484, `serve(8080)` → explicit port, `serve(8080, 4)` →
  `listen_pool`. The `listen*` family remains as advanced modes.
- **Short names (old names still work):** `cookies(req)` (`wings_cookies`),
  `ws_upgrade/ws_send/ws_close/ws_pong` (`wings_ws_*`), `sse_headers/sse_event`
  (`wings_sse_*`), `metrics_prom()` (`wings_metrics_prom`), `gzip(min?)`
  (`enable_gzip`), `delete(path, h)` (`del`), `accepts(schema)` (`body_schema`),
  `returns(schema)` (`response_model`).
- New regression suite `tests/wings_dx.test.tpr` (10 cases: route wiring,
  only/except filters, schema derivation, CRUD envelope statuses, aliases).

### Changed — tooling & docs
- LSP builtin table (`src/lsp/builtins.cpp`): added the wings DX layer, the
  previously unregistered `patch/head/options`/`body_schema`/`response_model`
  and request readers (`param`/`query`/`form`), and the **entire ORM v2
  surface** (ORM had zero LSP presence before) — everything now shows up in
  completion/hover.
- `examples/wings_notes_db.tpr` modernized: bound-parameter SQL (its comments
  wrongly claimed parameterized queries don't exist — stale since v3.3.0),
  function-ref handlers, `accepts()`/`delete()`; repositioned as the
  "hand-rolled SQL" teaching counterpart to `wings_orm_resource.tpr`.
- `examples/api_wings_crud.tpr` modernized: `req` parameter + `req.json`
  instead of the `_request` global, function-ref handlers, `accepts()` schema,
  `serve(3000)`.
- README: modern 8-line hello (function refs + `serve`), new 7-line
  persistent-CRUD showcase, `serve()` documented as the front door.
- New docs: `WINGS_DX.md` (design study), `WINGS_CHEATSHEET.md` (one-page
  UFCS-first API map).

## [v3.6.0]

Backwards-compatible feature round on top of v3.5.0: **wings completeness**
(the framework-parity follow-up from HANDOFF §2). No breaking changes.

### Added — wings (lib/wings.tpr, pure Tulpar)
- **Cookie SET side:** `set_cookie(res, name, val, opts)` /
  `delete_cookie(res, name)` build the `Set-Cookie` response header
  (opts: `path`, `domain`, `max_age`, `same_site`, `secure`, `http_only` —
  HttpOnly + SameSite=Lax + Path=/ by default). One cookie per response
  (response headers are a dict; last call wins).
- **Signed cookies:** `set_signed_cookie(res, name, val, secret, opts)` /
  `get_signed_cookie(req, name, secret)` — value carries an
  `hmac_sha256(secret, name + "." + value)` MAC (name-bound, so cookies
  can't be swapped); tampered/absent → `""`. MAC comparison is
  double-HMAC'd so string-compare timing reveals nothing.
- **Server-side sessions:** `session_start(req, secret)` (existing valid
  signed `tsid` cookie or fresh `secure_token(32)` id),
  `session_attach(res, sid, secret)`, `session_set/get(sid, key[, val])`,
  `session_destroy(sid)`. In-memory (`_wings_sessions` global, auto-persist);
  process-lifetime only.
- **Configurable CORS:** `cors(origin, {credentials, methods, headers,
  expose, max_age})` replaces the static wildcard defaults; sets
  `Vary: Origin` for specific origins and supports
  `Access-Control-Allow-Credentials: true` (the wildcard can't — the exact
  gap the registry frontend hit). Startup-only, covers the automatic
  OPTIONS/preflight 204.
- **Rate limiting:** `rate_limit(max, window_s)` — fixed-window,
  `use()`-based middleware keyed on `X-Forwarded-For`/`X-Real-IP` (first
  hop) with a global-bucket fallback; over-limit → 429 + `Retry-After`.
  Table resets each window, so memory stays bounded.
- **JWT guard:** `jwt_guard(secret)` — bearer-token middleware
  wire-compatible with the `wings_jwt` package (HS256, base64url segments,
  hex-MAC signature). Missing/bad/expired → 401; valid → claims injected as
  `req["jwt"]`. `jwt_public(path)` exempts paths (exact or trailing-`*`
  prefix); `/healthz`, `/metrics`, `/docs`, `/openapi.json` exempt by
  default. Also flags bearer auth in the OpenAPI doc.
- **HTML + templates:** `html(body)` (text/html response) and
  `render(tpl, vars)` — `{{key}}` substitution over a vars dict.
- **Typed path params:** `param(req, name, fb)` / `param_int` /
  `param_bool` — mirrors of the query helpers for `:name` route params.
- **ETag / conditional requests:** `cached_get` routes now serve a strong
  body-derived `ETag` and answer a matching `If-None-Match` with an empty
  `304` instead of the cached body.
- **Response compression:** `enable_gzip(min_bytes)` — transparent gzip for
  responses ≥ threshold when the client sends `Accept-Encoding: gzip`
  (adds `Content-Encoding: gzip` + `Vary: Accept-Encoding`; skips
  `cached_get` routes, whose pinned bytes are shared by all clients; skips
  when compression doesn't shrink the body).
- **OpenAPI completeness:** `response_model` schemas now document the 200
  response body in `/openapi.json`; `jwt_guard()` (or
  `docs_security("bearer")`) advertises a `bearerAuth` security scheme so
  Swagger UI shows Authorize.

### Added — runtime / language
- **`gzip_compress(s: str) -> str`** — gzip (RFC 1952) stream of the input
  bytes via a new **in-tree DEFLATE** (`runtime/tulpar_gzip.cpp`: fixed
  Huffman + greedy LZ77 over a 32K window + CRC-32). No zlib dependency —
  AOT user binaries stay self-contained. Binary-safe both directions
  (length-tracked strings). Wired through runtime, AOT codegen, typeinfer,
  LSP. Verified byte-exact against Python `gzip.decompress` and
  `curl --compressed` (CRC checked) on text, full-byte-range binary and
  random payloads; 16.4 KB HTML compresses to 274 B (1.7%).

### Fixed
- **`response_model` no longer drops `_headers`** — the output filter now
  preserves the `_headers` envelope key, so `set_cookie(...)` /
  `redirect(...)` headers survive response-model filtering.

### Tests / examples
- `tests/wings_features.test.tpr` — 13/13 (cookie builder, signed-cookie
  round-trip + tamper, sessions, CORS, rate-limit buckets, path params,
  JWT verify + middleware, html/render, `_headers` preservation, OpenAPI
  extensions, gzip).
- `examples/wings_features_api.tpr` — live showcase of the whole round
  (compile-only in CI; every endpoint verified with curl: sessions
  visits 1→2, tamper → 401, 100×200 → 429 + per-IP isolation, ETag → 304,
  gzip byte-exact).

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
