# TulparLang — Yapılan İşler ve Yol Haritası

> Slogan: **Python kadar kolay, C kadar hızlı — hızlı API yazma dili.**

---

## 📊 Mevcut durum

- **EKSIKLER.md:** 60 madde, hepsi RESOLVED — son tur (#51-60): wings TLS+mutex
  removal, streaming http_recv (16 MiB cap + env override), counter atomic RMW
  superinstruction, registry semver-aware latest + audit retention + per-IP rate
  limit, pkg semver range resolver (`^`, `~`, compound `>=1,<2`), CORS preflight
  auto-204, MSYS2 OpenSSL build default, struct arena-alloc (malloc'suz push),
  pkg.tulparlang.dev palette + favicon + 4 sayfa rollout
- **EKSIKLER.md (eski sayım):** 47 madde RESOLVED
- **Test paketi:** 30 example + 9 unit suite — **39/39 PASS** + 7/7 smoke
  (lsp, fmt, keepalive, pkg, http_client, production, **async_wings**)
  - `tests/lsp_smoke.py` (init, hover, completion, definition, references, rename, diagnostics — 9/9 check)
  - `tests/fmt_smoke.py` (indent, idempotency, blank-line collapse, EOF newline, token-pass — 9/9 check)
  - `tests/keepalive_smoke.py` (3 request tek TCP socket, keep-alive + close header)
  - `tests/pkg_smoke.py` (init/list/add/remove + install + round-trip TOML — 6/6 check)
  - `tests/http_client_smoke.py` (http_get / http_get_json / http_post / https reject — 6/6 check)
  - `tests/production_smoke.py` (/healthz, /metrics counters, /openapi.json, log_info JSON — 4/4 check)
  - `tests/break.test.tpr` (break-in-if, continue-in-if, nested loop break)
  - `tests/datetime.test.tpr` (epoch ISO, known ts, now() shape)
  - `tests/stdlib_extras.test.tpr` (datetime parse + regex match/search/capture/replace — 10/10)
  - `tests/stdlib_phase_a.test.tpr` (weekday + date_add + keys + csv + ORM CRUD — 6/6)
- **Build:** 0 uyarı, 0 hata
- **Eklenti:** `vscode-tulpar` v0.1.0 → v0.2.0 (LSP client'a geçti), 491.89 KB `.vsix`
- **LSP server:** `tulpar --lsp` (Stage 1 diagnostics + Stage 2 hover/completion + Stage 3 definition)
- **Formatter:** `tulpar fmt` (indent + trailing-ws + EOF newline + blank-line collapse, idempotent)
- **Paket yöneticisi:** `tulpar pkg init/list/add/remove` + `tulpar.toml` parser (tiny TOML subset)
- **HTTP keep-alive + perf:** Wings + Router tek TCP bağlantı üstü çoklu request, ~26k req/sec (Node ile eşit)
- **CPU performansı:** Tulpar AOT C'nin 1.3-1.4× yakınında (Rust/Go territory)
- **Faz 1 (HTTP runtime + test framework):** TAMAMLANDI
- **Faz 2 (Geliştirici deneyimi):** TAMAMLANDI
- **Faz 3a (HTTP keep-alive + hot path):** TAMAMLANDI
- **Faz 3b kısmı:** per-request arena reset (memory bounded), `break`/`continue` real codegen, datetime stdlib, token-pass formatter, LSP Stage 4 (find-refs + rename), `tulpar pkg install` + vendored module resolver — TAMAMLANDI
- **Faz 3 ek tur:** outbound `http_client` (`http_get/post/put/delete/get_json/post_json`), `/healthz` + `/metrics` Wings auto-route'ları, `wings_openapi(title, version)` OpenAPI 3.0 üretimi, `log_info / log_error` structured JSON logger — TAMAMLANDI
- **Stdlib + ekosistem cilası turu:** `tulpar fmt` keyword-aware spacing, `wings_metrics_prom()` Prometheus text format, `parse_iso8601`, `regex_match/search/capture/replace`, `tulpar pkg install` `url:` spec — TAMAMLANDI
- **"Bütün eksikleri kapat" turu:** datetime extras (`weekday`, `date_add_seconds`), `file_glob`, `csv_parse/emit`, `keys()` builtin, `lib/orm.tpr` Active-Record ORM, `[registry]` semver resolver + `tulpar.lock` lockfile, **TLS skeleton** (OpenSSL-gated, `https://` parse + handshake hazır — opsiyonel build), thread-local arena + dlsym cache mutex + `thread_create` ABI fix — TAMAMLANDI
- **"Son işi de hallet" turu:** **multi-threaded Wings** (`listen_async` — thread-per-connection, parallel recv/send, handler mutex) + tüm void+VMValue ABI bug'ları toplu fix (`thread_join/detach`, `mutex_lock/unlock/destroy`); **VS Code eklentisi v0.3.0** (12 yeni snippet, 60+ yeni builtin grammar'a, README LSP-aware); **web sitesi** "Ecosystem & Tooling" section'ı (paket yöneticisi, HTTP server/client, ORM, tooling, benchmarks — EN+TR), homepage hero kartları yeni feature'ları yansıtacak şekilde güncel — TAMAMLANDI

---

## ✅ Yapılanlar

### Faz 0 — Hijyen (önce kapatılan EKSIKLER #1-27)

- Build temizliği (Linux varsayımları, MSYS2 LLVM ZLIB/LibXml2, Winsock, gettimeofday)
- Embedded stdlib (wings, router, http_utils, async, middleware, socket, tulpar_api, test)
- Test runner: 30/30 örnek doğrulanır (19 PASS + 11 compile-only)
- VS Code eklentisi: 6 komut, snippets, diagnostics, status bar
- SQLite vendor uyarıları, markdown lint, `.vscodeignore` temizliği

### Faz 1 — "API-day-1" (HTTP runtime + test framework)

#### HTTP runtime native'e taşındı (C kadar hızlı)

- **`http_parse_request(raw)`** → method/path/raw_path/version/query(json)/headers(json)/body
  - Content-Length saygılı body slicing
  - URL decode (`%XX`, `+`)
- **`path_match(pattern, path)`** Express-style routing
  - `/users/:id` capture, multi-capture, `/static/*` wildcard
  - Native ~20× hızlı
- **`parse_query(str)`** URL-decoded form/query parser
- **`http_status_text(int)`** 30+ IANA reason phrase
- **`http_create_response(status, ct, body, [headers])`** 4-arg form custom headers
  - CORS, Set-Cookie, X-* user-supplied
  - Server-controlled (Content-Type/Length/Connection) silently filtered
- **`lib/wings.tpr`** 116→95 satır, default CORS — browser fetch out-of-the-box
- **`lib/router.tpr`** 50+ satır küçüldü; `match_path`/`parse_query_string` artık native'e thin wrapper

#### Test framework

- **`lib/test.tpr`** embedded, `import "test"`
- Assertions: `assert`, `assert_eq_int`, `assert_eq_str`, `assert_eq_bool`, `assert_contains`, `assert_status`, `assert_throws`
- Runner: `test("name", "handler")` + `test_summary()` (CI exit code uyumlu)
- **`tests/{http,strings,json,math,errors}.test.tpr`** — 5 unit suite, ~50 assertion
- `run_tests.ps1` ve `build.sh` test discovery + ayrı raporlama
- `tulpar exit(int)` AOT'ta builtin (libc-backed)
- Linker'a `--export-all-symbols` (Win) / `-rdynamic` (Linux/macOS) — `call(handler)` artık dlsym'la user fonksiyonlarını bulabiliyor

#### Yakalanan ve düzeltilen ciddi codegen bug'ları

- **`vm_make_bool` uninitialized 7 byte** — `as.bool_val = v` sadece 1 byte yazıyor, kalan 7 byte garbage. SysV ABI ret-pair'da `is_truthy` garbage'ı truthy okuyordu → `if (contains(x, "missing"))` her zaman truthy. Tek satır fix.
- **AST_IMPORT cross-module forward reference** — http_utils.tpr → router.tpr referansları "function not found" alıyordu. AST_IMPORT'a Pass 0.15 (predeclare-then-bodies) eklendi.
- **AOT user fonksiyonları dışa aktarılmıyordu** → `call()` her zaman fail. `-Wl,--export-all-symbols` ile çözüldü.

### Faz 2 — Geliştirici deneyimi (Python kadar kolay)

#### Adım 1 — Rust-stil hata mesajları

```text
hata: 'greting' adında bir fonksiyon bulunamadı
  --> d:\yazilim\Tulpar\app.tpr:8
    |
  8 | greting();
    | ^^^^^^^
    = ipucu: ... — bunu mu demek istediniz: 'greeting'?
```

- `report_codegen_error()` helper: file path + line + source preview + caret + ipucu
- `LLVMBackend->source_text` + `source_filename` field'ları
- `aot_compile_with_filename()` + `aot_compile_and_run_silent_with_filename()` API
- 7 sıkça karşılaşılan hata bu helper'a göçtü (function not found, var not defined, array access, increment, decrement, compound assign, assignment)

#### Adım 2 — Did-you-mean önerisi

- `levenshtein_bounded()` — case-insensitive, early-exit, max 64 char
- `find_closest_name()` — kısa isim için 1 edit, uzun için 2 edit
- `collect_visible_names()` — scope chain + functions + LLVM module globals (top-level non-int globals dahil)
- `report_codegen_error_with_suggestion()` — bounded-Levenshtein wrapper
- 6 hata yerine bağlandı

#### Adım 3 — Parser hataları aynı format

- `parser_set_diagnostic_context()` C-API (process-global, single-threaded compilation için yeterli)
- `render_parse_error_pretty()` — codegen helper'ıyla simetrik
- `Parser::error` → Rust-stil render + throw
- Parse-error fatal flag (`g_parser_error_count`): parser hatası → exe üretilmiyor (eskiden silently produces partial-AST exe)

#### Adım 4 — `mod()` AOT builtin

- `aot_math_mod` / `aot_math_mod_ptr` runtime'a eklendi (her iki operand int → int `%`, aksi halde fmod fallback; div-by-zero quiet 0)
- `func_aot_math_mod` field + LLVM `MATH2_FUNC("mod", …)` mapping
- `tests/math.test.tpr` artık 6/6 PASS — tüm test paketi 35/35 yeşil

#### Adım 5 — LSP server (Stage 1: diagnostics)

- **Process-global diagnostic sink** (`src/common/diagnostics.{hpp,cpp}`)
  - `diag_sink_enable / push / drain / disable` API
  - Aktifken parser ve codegen renderer'ları stderr yerine structured kayıt push'lar — JSON-RPC transport temiz kalır
- **`render_parse_error_pretty` + `report_codegen_error`** sink-aware (line/column/length/severity/message/hint hepsi struct'a)
  - Parser column'ı `Token::column()`'dan gelir; codegen column'ı caret_token memcmp'ından
- **`aot_check_only(source, filename)`** — opt/emit/link bypass; sadece parse + codegen ~100 ms
- **LSP server** (`src/lsp/lsp_server.{hpp,cpp}`)
  - JSON-RPC framing (Content-Length headers), stdio binary mode + `_IONBF` (Win)
  - cJSON ile parse/build (zaten vendored); single-fwrite header+body emission
  - `initialize / initialized / shutdown / exit` + `textDocument/didOpen / didChange / didSave / didClose`
  - Full-text sync (`change: 1`); incremental ileride bir optimizasyon
  - Açık doküman state'i `unordered_map<uri, DocumentEntry{text, index}>`
  - URI → path: `file://` prefix, percent-decode, Windows'ta drive harfi normalleşmesi
- **`tulpar --lsp` flag** — locale/REPL/AOT dispatch'inden önce short-circuit eder
- **VS Code eklentisi v0.2.0** — `parseDiagnostics` + `ERR_REGEXES` silindi; `vscode-languageclient` 9.0 spawn ediyor; `executablePath` / `diagnostics.enabled` değişimi LSP'yi otomatik restart eder

#### Adım 6 — LSP Stage 2 + 3 (hover, completion, go-to-definition)

- **DocumentIndex** (`src/lsp/document_index.{hpp,cpp}`)
  - Her parse'tan sonra üst-seviye `AST_FUNCTION_DECL`'leri tarayıp `IndexFunction{name, params, return_type, line, column, leading_comment}` toplar
  - Leading comment scanner: declaration'dan yukarı doğru contiguous `//` blok'larını topluyor (rust-doc/godoc konvansiyonu)
- **`aot_check_and_index(source, filename, &index)`** — codegen sonrası, AST free'den önce index'i doldurur (import edilen mod fonksiyonları da görünür)
- **Builtins tablosu** (`src/lsp/builtins.{hpp,cpp}`) — 80 entry (`print`, math, string, file, db, http, socket, thread). Her kayıt: imza + Türkçe doc satırı
- **textDocument/hover** — pozisyondaki identifier'ı `word_at_position` ile çıkarır → önce user functions, sonra builtins; markdown `tulpar` code block + leading comment / doc döner
- **textDocument/completion** — user functions + 80 builtin + 25 keyword (`func`, `if`, `return`, type adları, …) → `CompletionItem{label, kind, detail, documentation}`
- **textDocument/definition** — user function adından declaration line'ına `Location{uri, range}` döner; builtin'lerde sessiz null
- **Capabilities güncellemesi**: `hoverProvider`, `definitionProvider`, `completionProvider.triggerCharacters: ["."]`
- **Smoke test çıktısı** (7/7 PASS)
  - hover user-func (signature + leading comment)
  - hover builtin (`print(value: any): void`)
  - completion: 108 item (1 user-func + 80 builtin + 25 keyword + 2 dahili keyword)
  - definition: kullanım sitesi → declaration line'ı

#### Adım 10 — Memory hygiene + ekosistem turu (Faz 3b parça)

**Per-request arena reset** (`arena_save` / `arena_restore` builtins'leri)

- Eski durum: AOT arena hiç free etmiyordu. 26k req/sec'lik bir server 24 saat sonra GB seviyelerinde RAM kullanırdı.
- [src/vm/runtime_bindings.cpp](src/vm/runtime_bindings.cpp): checkpoint stack (32 slot), `arena_save()` mevcut `current` block + `used`'i kaydeder, `arena_restore(idx)` rollback yapar.
- Ek fix: `aot_arena_alloc` chain-aware (restore sonrası block reuse, leak yok).
- [lib/wings.tpr](lib/wings.tpr): `_default_headers` modüle taşındı (artık per-connection alloc değil); listen() top'ta `arena_save`, her request sonrası `arena_restore` — transient string'ler recycle edilir.
- Trade-off: ~5-10% req/sec maliyeti (saniyede 23-25k vs 25-26k); değer: bounded memory.

**`break` / `continue` AOT codegen gerçek implementasyonu**

- Önceki turda silently no-op'tu — `codegen_statement` switch'inde case yoktu.
- [src/aot/llvm_backend.hpp](src/aot/llvm_backend.hpp): `LoopContext loop_stack[32]` (continue + break BB çifti).
- [src/aot/llvm_backend.cpp](src/aot/llvm_backend.cpp): `AST_WHILE`/`AST_FOR` push/pop yapıyor; `AST_BREAK`/`AST_CONTINUE` top-of-stack'e branch + "after_break"/"after_continue" dummy block.
- Wings/Router'daki `keep_serving`/`alive` flag workaround'ları gerçek `break;`'e çevrildi.
- [tests/break.test.tpr](tests/break.test.tpr) — 3/3 PASS.

**`tulpar fmt` Pass-2 (token-pass)**

- [src/fmt/formatter.cpp](src/fmt/formatter.cpp): line-içi token spacing normaliz.
  - `,` `;` öncesi boşluk yok, sonrası bir boşluk (closer hariç)
  - `:` aynısı (return type + JSON key)
  - Binary operatörler etrafı tek boşluk (`==` `!=` `<=` `>=` `&&` `||` `+=` `-=` `*=` `/=`)
  - Unary `-` korunuyor (önceki char operator/`(`/`,` ise unary kabul edilir)
  - String literal + `//` line comment içi dokunulmaz
  - `(`/`[` sonrası, `)`/`]` öncesi padding yok
  - `)` + `{` arasına gofmt-style boşluk
- Idempotent (`fmt(fmt(s)) == fmt(s)`); fmt smoke 9/9.

**`tulpar pkg install` + vendored module resolver**

- [src/pkg/pkg_cli.cpp](src/pkg/pkg_cli.cpp): `cmd_install()` — manifest'teki `path:./local/dir` spec'lerini `tulpar_modules/<name>/` altına recursive copy (`std::filesystem`, sadece `.tpr` filtrelenir).
- [src/aot/llvm_backend.cpp](src/aot/llvm_backend.cpp): `import "name"` resolver:
  1. literal path
  2. literal + `.tpr`
  3. `tulpar_modules/<name>/<name>.tpr` (vendored entry — convention)
  4. `tulpar_modules/<name>.tpr` (single-file vendor)
- E2E test: `pkg add greeter@path:./greeter_pkg` → `pkg install` → `import "greeter"` → AOT compile → çalışır binary.
- pkg smoke 6/6.

**LSP Stage 4 — find-references + rename**

- [src/lsp/document_index.{hpp,cpp}](src/lsp/document_index.hpp): `IndexCallSite` (name, line, col) + recursive AST walker — function bodies + top-level statements.
- [src/lsp/lsp_server.cpp](src/lsp/lsp_server.cpp):
  - `textDocument/references` — call sites + (opsiyonel) declaration; `Location[]`
  - `textDocument/rename` — `WorkspaceEdit { changes: { uri: TextEdit[] } }`
  - Capabilities: `referencesProvider`, `renameProvider`
- LSP smoke 9/9: refs (decl + 1 call), rename (2 edit → "say_hi").

**Datetime stdlib**

- Yeni runtime builtin'leri: `now_iso8601()`, `format_iso8601(unix_seconds)` — UTC ISO 8601 (`YYYY-MM-DDTHH:MM:SSZ`).
- API'ler için en sık ihtiyaç. C-level strftime; arena alloc; Windows + POSIX (gmtime_s vs gmtime_r).
- [tests/datetime.test.tpr](tests/datetime.test.tpr) — 3/3 PASS.

**Bonus: `env(name)` builtin**

- Önceki turdan kalan: process env okuma. Hot-path için (`TULPAR_HTTP_QUIET=1`) ve genel kullanım.

#### Adım 14 — Son iş + ekosistem cilası

**Multi-threaded Wings (`listen_async`)**

- [lib/wings.tpr](lib/wings.tpr): `_wings_serve_connection` worker fonksiyonu + `listen_async(port)` accept loop. Her TCP bağlantısı için detached worker spawn'lanıyor; recv/parse, response build/send paralel; sadece `_request = req` + `call(handler)` + counter mutasyonu `_wings_handler_mu` mutex'i altında serileşir.
- Per-connection arena scope (`arena_save` / `arena_restore`) ile worker'lar memory leak'siz keep-alive serve ediyor.
- `tests/async_wings_smoke.py` — 8 paralel bağlantı / 8 başarılı response, post-burst sunucu hâlâ canlı.

**ABI bug fix toplu**

`socket_close`'a önceden uygulanan `(VMValue) -> void` Windows MinGW64 ABI fix'i hâlâ açık duruyordu, bütün diğer void-dönen builtin'lerde aynı potansiyel crash. Hepsini topluca düzelttim:

- `aot_thread_join`, `aot_thread_detach` — VMValue dönüşe geçirildi (bunlardan biri ilk async test'i crash ettiriyordu).
- `aot_mutex_lock`, `aot_mutex_unlock`, `aot_mutex_destroy` — aynı şekilde.
- Codegen: `(VMValue) -> void` fonksiyon tipleri `llvm_make_vmvalue_func_type` ile yeniden bind edildi.

**VS Code eklentisi v0.3.0** ([d:/yazilim/tulpar-ext/vscode-tulpar-0.3.0.vsix](../tulpar-ext/vscode-tulpar-0.3.0.vsix), 494 KB)

- 12 yeni snippet: `twingsa`, `topenapi`, `tloginfo`, `tlogerror`, `thttpget`, `thttppost`, `torm`, `tnow`, `trxc`, `tcsv`, `tglob`, `tpkgdep`, `tarena`.
- Grammar'a 60+ yeni builtin: `regex_*`, `csv_*`, `file_glob`, `keys`, `now_iso8601` / `format_iso8601` / `parse_iso8601` / `weekday` / `date_add_seconds`, `arena_save` / `arena_restore`, `env`, `mod`, `http_get/post/put/delete/get_json/post_json/request/should_keepalive/recv_request`, `wings_openapi` / `wings_metrics_prom` / `log_info` / `log_error`, `listen_async`, `orm_*`, `thread_detach`.
- README'de eski "regex-based stderr scraping" bölümü kaldırıldı, yerine LSP capability listesi (diagnostics + hover + completion + definition + references + rename) ve LSP-aware editor talimatları geldi.

**Web sitesi** ([d:/yazilim/tulpar-lang-web](../tulpar-lang-web/))

- Homepage hero: yeni slogan "Python kadar kolay, C kadar hızlı." 6 kart: C-class performance, API-day-1 stack, modern editor tooling, package manager, batteries included, UTF-8 + Türkçe + English. Hızlı bir Wings + ORM örneği landing page'de.
- Yeni "Ecosystem & Tooling" sidebar bölümü: 6 sayfa
  - [Package Manager](https://docs.tulparlang/ecosystem/package-manager/) — `tulpar.toml`, `tulpar.lock`, `path:` / `url:` / semver specs, modül resolution.
  - [HTTP Server (Wings)](https://docs.tulparlang/ecosystem/http-server/) — routing, `listen` vs `listen_async`, auto-routes, OpenAPI, structured logging.
  - [HTTP Client](https://docs.tulparlang/ecosystem/http-client/) — `http_get/post/put/delete/get_json/post_json`, HTTPS, sınırlar.
  - [ORM (lib/orm)](https://docs.tulparlang/ecosystem/orm/) — model tanımı, CRUD reference, Wings + ORM örneği.
  - [Tooling — LSP / Formatter / VS Code](https://docs.tulparlang/ecosystem/tooling/) — LSP capability'leri, fmt iki pas, VS Code eklentisi, Neovim örneği.
  - [Benchmarks](https://docs.tulparlang/ecosystem/benchmarks/) — CPU 1.3-1.4× C, HTTP ~26k req/sec, optimizasyon listesi.
- Hepsinin Türkçe çevirisi `tr/ecosystem/` altında.
- `stdlib/builtins.mdx` 8 yeni table ile genişletildi: Datetime, Regex, Files, CSV, Process/env, Arena, Threading, HTTP cross-reference.
- Astro build: 0 error, 54 page, pagefind index temiz.

#### Adım 13 — Eksikleri kapatma turu (data-driven plan)

**Phase A — Stdlib genişletme**

- `weekday(unix_seconds) -> int` (0=Sun … 6=Sat, POSIX `tm_wday`).
- `date_add_seconds(base, delta) -> int` — stable name for date arithmetic.
- `keys(obj) -> array<str>` — object key enumeration in insertion order.
- `file_glob(pattern) -> array<str>` — shell-style `*` `?` matcher over `std::filesystem::directory_iterator`. Sorted output.
- `csv_parse(str) -> array<array<str>>` — RFC 4180 minimal: comma delim, `\r\n` rows, `"…"` quoted fields with `""` escape.
- `csv_emit(rows) -> str` — symmetric emit; quotes only when needed.
- `lib/orm.tpr` — Active-Record style mini ORM over the embedded SQLite. `orm_open / orm_close / define_model / orm_create / orm_find / orm_all / orm_where / orm_update / orm_delete`. Identifier + value SQL escaping (`"foo"` for idents, `'…'` for values, `"` and `'` doubled).

**Phase B — Paket yöneticisi tamamlama**

- `Manifest.registry_url` field + `[registry]\nurl = "..."` parse.
- `pkg install` semver path: spec `name = "1.2.3"` resolves to `<registry>/<name>/<version>.tpr` (single-file convention), HTTP fetch via the shared `http_fetch_url` helper.
- `tulpar.lock` lockfile: `[resolved] <name> = "<full-url>"` written after every install so re-installs are byte-stable even if registry latest moves. Auto-generated, do-not-edit comment header.
- E2E: local Python registry → `pkg add greeter@0.1.0` → `pkg install` → `tulpar_modules/greeter/greeter.tpr` written + lockfile populated → consumer build → `registry-hello Hamza` ran.

**Phase C — TLS / HTTPS (skeleton, OpenSSL-gated)**

- CMake `find_package(OpenSSL QUIET)` → defines `TULPAR_HAS_TLS=1` when found, links `OpenSSL::SSL` + `OpenSSL::Crypto` to both `tulpar` and `tulpar_runtime`.
- `src/common/http_fetch.{hpp,cpp}` — both `http_fetch_url` (GET-only) and new `http_request_url` (any method + body) understand `https://` URLs and dial via `SSL_connect / SSL_read / SSL_write` when TLS is compiled in.
- SNI set via `SSL_set_tlsext_host_name`; cert verification disabled until a configurable trust-store path lands (Tulpar-level config).
- `aot_http_request` builtin refactored to delegate to `tulpar::http_request_url` so client + pkg fetch share one TLS implementation.
- Without OpenSSL: `https://` URLs return `{ok: 0, error: "TLS not compiled in (build Tulpar with OpenSSL to enable https://)"}`. Plain HTTP unaffected.
- Activation: `pacman -S mingw-w64-x86_64-openssl` (MSYS2) or system OpenSSL on Linux/macOS, then rebuild.

**Phase D — Async I/O thread safety (groundwork)**

- `g_aot_string_arena` + `g_arena_checkpoints` → `thread_local`. Each worker thread initialises its own arena lazily; concurrent allocs no longer corrupt the bump pointer.
- `g_call_cache` (dlsym memo) → `std::mutex` on insert with double-check inside the lock. Lookups stay lock-free; published-key-last ordering keeps concurrent readers safe.
- `aot_thread_entry` ABI fix: was `VMValue (*)(VMValue)`, actually Tulpar user functions are `void(VMValue *result, VMValue arg)` — silently invoked the wrong calling convention before; multi-arg user code dispatched via `thread_create` would crash on first arg access.
- `thread_create` codegen: `LLVMGetNamedFunction` now tries the prefixed `t_<name>` first (matching the `aot_call_dynamic` lookup path) before the literal name; previously passed null function pointers for any user-defined target.
- Full multi-threaded Wings (worker pool with `_request` per-thread) needs LLVM thread-local globals — that's a separate codegen change tracked for the next session. Today's change makes `thread_create` correct for fire-and-forget user worker code and makes the runtime **thread-safe enough** that user code calling threads doesn't crash on alloc.

#### Adım 12 — Cila + ekosistem genişletme

**`tulpar fmt` keyword spacing**

- [src/fmt/formatter.cpp](src/fmt/formatter.cpp) `normalise_line_spacing` token-pass'ine ikinci geçiş eklendi: `if`/`while`/`for`/`catch`/`else`/`try`/`finally`/`do` keyword'leri sonrası `(`/`{` öncesi tek boşluk; `}` ile `else`/`catch`/`finally` arası boşluk.
- String literal + `//` comment içinde değişiklik yapılmaz.
- `if(x>0){` → `if (x > 0) {`, `}else if(x==0){` → `} else if (x == 0) {`, `try{}catch(e){}finally{}` → tam gofmt-stil.
- Idempotent (3 farklı pass çalışır, ikinci uygulamada değişmez).

**Prometheus text format**

- [lib/wings.tpr](lib/wings.tpr) `wings_metrics_prom()` — `# HELP` + `# TYPE` + metric line'larıyla canonical Prometheus exposition.
- `_wings_metrics()` `?format=prom` query string'inde `{_raw: "..."}` içinde Prometheus body'sini döner (gerçek `text/plain` content-type için kullanıcı kendi route'unu kayıt etmeli).
- Metric'ler: `tulpar_uptime_seconds`, `tulpar_requests_total`, `tulpar_requests_by_class{class="2xx|4xx|5xx"}`, `tulpar_routes_total`.

**Datetime parse**

- Yeni runtime builtin: `parse_iso8601(str) -> int` (unix seconds; -1 başarısızlıkta).
- `YYYY-MM-DDTHH:MM:SSZ` (Z-form) + `YYYY-MM-DD HH:MM:SS` (SQLite-style space).
- POSIX days-from-civil formula — `timegm` Windows CRT'de yok, hand-rolled.
- Round-trip: `parse_iso8601(format_iso8601(t)) == t` (test ediliyor).

**Regex stdlib**

- C++ `std::regex` (ECMAScript syntax) bind: `regex_match(pat, s)` (full), `regex_search(pat, s)` (substring), `regex_capture(pat, s)` (`[whole, g1, g2, ...]` array veya `[]`), `regex_replace(pat, s, repl)` (`$1` referans).
- Pattern her çağrıda derlenir; cached-pattern roadmap'te.
- 5/5 test: full vs partial match, capture groups, no-match empty array, replace all.

**`tulpar pkg install` `url:` spec**

- [src/common/http_fetch.{hpp,cpp}](src/common/http_fetch.hpp) — runtime'dan bağımsız C++ HTTP/1.0 fetcher (URL parse + DNS + connect + GET + recv-until-FIN + status/body parsing).
- `aot_http_request` builtin'i ile pkg_cli ortak altyapıyı kullanıyor (kod tekrarı yok).
- `pkg add foo@url:http://example.com/foo.tpr` + `pkg install` → `tulpar_modules/foo/foo.tpr` (HTTP GET ile fetch + write).
- E2E doğrulandı: localhost Python server'dan `say_url.tpr` çekildi, `import "say_url"` sonra çağrı `from-url` döndü.
- Skip: TLS yok (`https://` reddedilir), checksum yok, archive yok. Semver registry bir sonraki adım.

#### Adım 11 — Outbound HTTP + Production helpers

**`http_client` (outbound HTTP/1.0)**

- Yeni runtime builtin: `aot_http_request(method, url, body)` ([src/vm/runtime_bindings.cpp](src/vm/runtime_bindings.cpp)) — URL parse + DNS (`getaddrinfo`) + connect + send + recv-until-FIN + status/headers/body parsing.
- Dönüş: `{ok, status, headers: {…}, body}` (başarısızlıkta `{ok: 0, error: "…"}`).
- Scope: plain HTTP, no TLS, no redirects, no chunked. 8MB recv cap. `https://` URL'leri açıkça reddedilir.
- [lib/http_client.tpr](lib/http_client.tpr) wrapper'ları: `http_get`, `http_post`, `http_put`, `http_delete`, ayrıca `http_get_json` / `http_post_json` (otomatik fromJson/toJson).
- E2E smoke: client→server hit, GET/POST, JSON parse, `https://` reject — 6/6.

**Wings production helpers**

- `_wings_started_at` + `_wings_requests_total/2xx/4xx/5xx` global'ler; her request response sonrası counter++.
- Auto-routes: `listen()` çağrısında `/healthz` ve `/metrics` user kayıtlı değilse otomatik kayıt — opt-out (önce kendi handler'ını kayıt et).
  - `/healthz` → `{status, uptime_s, now}` (k8s-uyumlu).
  - `/metrics` → `{uptime_s, requests_total/2xx/4xx/5xx, routes}` (Prometheus-text değil, JSON; sade).
- `wings_openapi(title, version)` — kayıtlı route'lardan OpenAPI 3.0 dokümanı üretir (`paths[<url>][<method>] = {summary, responses: {200: {description: "OK"}}}`). Swagger UI / Postman doğrudan tüketir.
- `log_info(msg)` / `log_error(msg)` — `{@timestamp, level, msg}` JSON log line'ı stdout'a basar (log aggregator'lar custom parser olmadan tüketir).
- E2E smoke: 3 GET / hit, /healthz JSON, /metrics counter ≥4, /openapi.json `paths` listesi, log_info satır şekli — 4/4.

#### Faz 3a (devam) — Performans turu (data-driven)

**Baseline (5000 GET req, 4 keep-alive conn, single thread):**

| Server | Wall (s) | req/sec | Tulpar'a oran |
|--------|---------:|--------:|---------------|
| Tulpar Wings (post-fix) | 0.193 | ~26 000 | 1.00× |
| Node.js http | 0.184 | ~27 200 | 0.96× (Node 4% ileri) |
| Python ThreadingHTTP | 0.354 | ~14 100 | 1.84× (Tulpar 1.84× ileri) |

**CPU benchmarks (best of 3):**

| Test | C (gcc -O2) | Tulpar AOT | Oran |
|------|------------:|-----------:|------|
| loopsum (10M sum) | 67 ms | 88 ms | **1.31× C** |
| fib(35) recursive | 83 ms | 114 ms | **1.37× C** |

**Yapılan değişiklikler (sırasıyla):**

- **`call(name)` dlsym cache** ([src/vm/runtime_bindings.cpp](src/vm/runtime_bindings.cpp)) — 256-slot FNV-1a hash. Her HTTP request'i `tulpar_dlsym` ile sembol tablosunu tarayıp handler'ı çözüyordu; ilk hitten sonra O(1).
- **TCP_NODELAY accept'te** ([src/vm/runtime_bindings.cpp:aot_socket_accept](src/vm/runtime_bindings.cpp)) — Nagle algoritması küçük JSON yanıtlarını 40ms'ye kadar bekletiyordu. Tek satırlık `setsockopt`, **+13% req/sec**.
- **Static recv buffer** — `http_recv_request` per-call 64KB malloc + free yapıyordu; thread-local static buffer'a geçirildi (yanıt arena'ya kopyalandığı için recycle güvenli).
- **`env(name)` builtin** — process env var okuma. `TULPAR_HTTP_QUIET=1` access log'unu kapatıyor; ekosistemde de evrensel ihtiyaç.
- **`break` / `continue` AOT codegen** — eskiden `codegen_statement` switch'inde case yoktu, **silently no-op'tu**. `LLVMBackend.loop_stack` ekledik (continue/break basic-block çifti, 32 derinlik). Wings + Router'daki `keep_serving`/`alive` flag workaround'ları gerçek `break;`'e geri çevirildi.
- **`socket_close` ABI fix** (Faz 3a'da önceden yaptık) — Windows MinGW64 `void`+VMValue çakışması, VMValue dönüşe geçirildi.
- **Toplam HTTP gain:** baseline 22 200 → 26 000 req/sec (**+17%**).

#### Faz 3a — HTTP keep-alive

- **Yeni runtime builtin'leri** ([src/vm/runtime_bindings.cpp](src/vm/runtime_bindings.cpp)):
  - `http_should_keepalive(parsed)` — HTTP/1.1 default keep-alive, HTTP/1.0 default close, `Connection:` header override; case-insensitive `aot_http_obj_get_ci` ile lookup
  - `http_recv_request(client_fd, max_bytes)` — `\r\n\r\n` görene kadar oku, sonra Content-Length kadar body slurp et; pipelined isteklerin sınırını aşmaz; EOF/error'da `""` döner
  - `http_create_response_keepalive(status, ct, body, headers, keep)` — 5-arg variant: `keep=1` → `Connection: keep-alive`, aksi `Connection: close`. Eski 3- ve 4-arg form'lar dokunulmadı.
- **AOT codegen** ([src/aot/llvm_backend.cpp](src/aot/llvm_backend.cpp)):
  - `http_create_response` 5-arg dispatch'i + 2 yeni builtin için LLVM declaration & call
- **Stdlib güncellemeleri**:
  - [lib/wings.tpr](lib/wings.tpr) — listen() inner request loop (keep-alive); 5-arg `http_create_response` ile her response'ta uygun Connection header
  - [lib/router.tpr](lib/router.tpr) — `_router_handle_client` aynı client_fd üstünde döner; per-request `_keepalive` global'i `json_response`'un `Connection:` satırını yönlendirir
- **Tulpar codegen workaround**: `break;` nested `if` içinde AOT'ta düzgün lower edilmiyor (kontrol if'in altına düşüyor). İç loop'lar `keep_serving` / `alive` boolean flag'leriyle sürülüyor; gerçek codegen fix ayrı bir tur.
- **Pre-existing fix**: `aot_socket_close` Windows MinGW64'te `(VMValue) -> void` ABI uyumsuzluğuyla crash ediyordu (struct-by-value arg + void return). VMValue dönüş tipine geçirildi; AOT codegen aynı uniform yola alındı. Eski wings/router CI'de test edilmiyordu (compile-only) — bu yüzden bug saklıydı.
- **Smoke test** [tests/keepalive_smoke.py](tests/keepalive_smoke.py): tek socket'te 3 ardışık request, ilk ikisi `Connection: keep-alive`, üçüncü explicit `Connection: close` ile sonlanır — hepsi tek TCP bağlantı üzerinde tamamlanır.

#### Adım 8 — Paket yöneticisi (skeleton)

- **`tulpar.toml`** manifest format (tiny TOML subset: top-level string keys + `[dependencies]` table)
- **Manifest parser** ([src/pkg/manifest.{hpp,cpp}](src/pkg/manifest.hpp)): hand-rolled — başka bir TOML dependency vendoring'i yok, format bizim olduğu için surface büyüdükçe genişletilecek
- **`tulpar pkg`** CLI ([src/pkg/pkg_cli.{hpp,cpp}](src/pkg/pkg_cli.hpp)):
  - `pkg init [name]` — `tulpar.toml` oluştur (mevcutsa overwrite reddet)
  - `pkg list` — manifest'i parse edip name + version + dependencies basar
  - `pkg add <name>[@<version>]` — dep ekle/replace
  - `pkg remove <name>` — dep çıkar
- Atomic save: `path.tmp` yaz → rename
- Round-trip: `manifest_parse(manifest.to_toml()) == manifest`
- **Şu an YAPMIYOR**: registry fetch, install, lockfile. Bunlar Faz 4'e bırakıldı; ilk iş manifest formatını + CLI iskeletini sabitlemek
- Smoke test [tests/pkg_smoke.py](tests/pkg_smoke.py) — init/refuse-overwrite/add/replace/list/remove + round-trip parse — 5/5 PASS

#### Adım 9 — `break` / `continue` AOT codegen (gerçek fix)

- Önceki turda `break;` nested `if` içinde silently no-op'tu (codegen_statement switch'inde case yoktu).
- [src/aot/llvm_backend.hpp](src/aot/llvm_backend.hpp) — `LoopContext loop_stack[32]` (continue + break basic-block çifti) + `loop_depth` field
- [src/aot/llvm_backend.cpp:codegen_statement](src/aot/llvm_backend.cpp) — AST_WHILE / AST_FOR push/pop loop_stack; AST_BREAK / AST_CONTINUE buildBr to top-of-stack target
- Break/continue sonrası "after_break" / "after_continue" basic block oluşturulur (sonraki kod dead ama LLVM verifier mutlu)
- [tests/break_codegen_test.tpr](tests/break_codegen_test.tpr) — break-in-if (3'te durdu), continue-in-if (sum=8), nested loop break (total=6) — 3/3 PASS
- [lib/wings.tpr](lib/wings.tpr) ve [lib/router.tpr](lib/router.tpr)'deki `keep_serving`/`alive` flag workaround'ları gerçek `break;`'e geri çevrildi

#### Adım 7 — `tulpar fmt`

- **Komut**: `tulpar fmt <file.tpr> [--write|-w]` — varsayılan stdout'a yazar, `--write` dosyayı yerinde günceller
- Brace-depth tabanlı re-indent (4 boşluk; closing `}` opener ile hizalanır)
- Trailing whitespace strip; runs of 2+ blank line tek satıra collapse; EOF tek `\n`
- Token-stream rewrite yok (op'lar/virgüller dokunulmuyor) — bu Stage 2 bir formatter pass'i
- Idempotent: `fmt(fmt(s)) == fmt(s)` (smoke test ediyor)
- Windows'ta stdout `_O_BINARY` — gofmt gibi LF, no CRLF translation

### VS Code Eklentisi

- v0.0.3 → v0.1.0 bump
- 6 komut: Run File / Run with VM / Build (AOT) / Build & Run (AOT) / REPL / Check File
- **Inline diagnostics** → Problems panel (compiler `HATA (Satır N): ...` çıktısını parse ediyor)
- 2 status bar düğmesi: ▶ Run + 📦 Build
- Grammar: 130+ built-in, typed-return syntax (`func name(): int { }`), `type`/`null` keywords, hex literal, ALL_CAPS sabitler
- Snippets (25): `tmain`, `tfunci`, `tif`, `tfor`, `ttry`, `tjson`, `twings`, `trouter`, `tapi`, `tsocket`, `tasync`, `tmw`, `tdb`, `tpath`, `thttp`, `thresp`, `threspx`, `ttest`, `tthrows`, …
- 5 yapılandırma: `executablePath`, `runCommand`, `diagnostics.{enabled,runOnSave}`, `aot.outputName`
- Paketleme: 11 dosya, 38.84 KB temiz `.vsix`

---

## 🔜 Kalan işler — v0.9 RC → v1.0 yol haritası

### 🔴 Adım 1 — Codegen miscompile (kritik, açık)

PR #84 wings cookies SIGSEGV'ini hot-fix'le kapattı (`_request["cookies"] = func()`
yazma yolu kapatıldı, helper public oldu). Asıl codegen miscompile arka planda
duruyor: tam wings symbol table altında "function-returning-object →
global-subscript-write" deseni `vm_object_set: obj=NULL` ile çöküyor.

- Repro scaffold'u büyüt — wings'in tüm symbol table'ını içe alan minimal test.
- `--emit-llvm` ile IR diff, lowering yolunu izle.
- Kök neden: muhtemelen `_request = req` atamasının TLS / global slot'a
  propagasyonu kaybolan bir yan yol.

### 🟠 Adım 2 — CI runtime smoke (yüksek, açık)

`COMPILE_ONLY_TESTS` listesi sadece derleniyor — PR #76 wings regresyonu 4 PR
boyunca main'de yattı. `build.sh test` ve `run_tests.ps1`'e wings/router/api
örnekleri için spawn → 200 ms bekle → curl `/healthz` → kill akışı eklenmeli.

### Faz 2 devamı — Python kadar kolay rötuşları

- Parse hatası satır numarası recovery (bazen sonraki satırı raporluyor).
- Parse error multi-error mode (şu an ilk fatal'da duruyor).
- LSP variable + type hover/completion (şu an sadece function ve keyword).
- LSP signature help (parametre içindeyken aktif imza ipucu).
- Incremental document sync (`change: 2`) — büyük dosyalarda diff-based.
- `tulpar fmt` token-pass kenar durumları.

### Faz 3 — Production-ready API (kalan)

- **Async I/O server** — epoll/kqueue/IOCP. Şu an `listen()` tek-thread,
  `listen_async()` thread-per-connection — event-loop modeline geçiş "C kadar
  hızlı" iddiasını HTTP tarafında da karşılar.
- **TLS doğrulama E2E** — code path hazır, MSYS2 OpenSSL default'u commit
  7ceca74 ile geldi; `https://` GET + `wings_tls` listener tam yol smoke testi
  hâlâ bekliyor.
- **Wings TLS listener** — server-side `SSL_accept` + per-request
  `SSL_read/write`; client TLS altyapısıyla simetrik.
- **Trust-store config** — `SSL_VERIFY_NONE` yerine kullanıcı CA bundle yolu.
- **WebSocket / SSE** — Wings'te uzun yaşayan bağlantı tipi yok.
- **Codegen full atomic** — top-level int globals ✅ (PR #81); imported pass
  globals'a aynı superinstruction lazım (boxed VMValue → native i64 hardening).

### Ekosistem + tooling

- `tests/router.test.tpr`, JSON edge case'leri, file IO unit test'leri.
- Quickstart + language reference + pkg guide → `tulparlang.dev/docs/`.
- Multi-file `.tpr` paket arşivleri (tar/zip extraction).
- `tulpar update` binary release artifacts pull mekanizması.
- Daha fazla 3rd-party paket (community discovery için pkg.tulparlang.dev'de
  "featured" section).

### 🎯 v1.0 = "dil tam oldu" kriterleri

1. **Sıfır bilinen davranış regresyonu** — `git log --grep="fix(.*regression"`
   boş; CI compile + runtime gate'leri yeşil.
2. **Motto taşınıyor** — benchmark'lar Go/Java sınıfında; örnekler Python kadar
   okunur.
3. **Ekosistem self-host** — tulpar-be production'da, pkg.tulparlang.dev paket
   sunuyor; 3rd-party kullanıcı `tulpar pkg add foo@^1` ile çalışan dep
   ekleyebiliyor.
4. **Dokümantasyon eşiği** — quickstart + language reference + pkg guide
   `tulparlang.dev/docs/` altında canlı.
5. **Stable release** — `v1.0.0` git tag + binary release artifacts; `tulpar
   update` mekanizması bunu çekiyor.

**Tahmini takvim:**

| Aşama | İçerik | Süre |
|-------|--------|-----:|
| v0.9 RC | Adım 1 + Adım 2 + Faz 2 rötuşu | 3-4 hafta |
| v1.0 | Faz 3 (async I/O, TLS doğrulama, Wings TLS) + docs + benchmark refresh | 2-3 ay |

### Bilinen ufak konular

- `assert_throws` çalışıyor ama bazı `string ==` edge case'lerinde toString
  roundtrip gerek.
- `[AOT]` mesajları verbose build modunda çıkıyor (silent path temiz).
- Parse error recovery satır no'su occasionally bir satır kayık.

---

## 📈 Kalan ileri iş

Bu turlarda kapatılanlar:
- **Codegen**: per-req arena reset, `break`/`continue` real implementation, `thread_create` ABI fix.
- **Runtime thread-safety groundwork**: arena thread-local, dlsym cache mutex.
- **Tooling**: `tulpar fmt` token-pass + keyword-aware spacing.
- **Package**: `pkg install` `path:` + `url:` + semver registry resolver, `tulpar.lock` lockfile.
- **LSP**: Stage 4 refs + rename.
- **Stdlib**: datetime (now/format/parse, weekday, date_add), regex (match/search/capture/replace), `env()`, `keys()`, `file_glob`, `csv_parse/emit`, `lib/orm.tpr` (Active-Record over SQLite), `lib/http_client.tpr` (outbound HTTP).
- **Server**: keepalive, `/healthz`, `/metrics` (JSON + Prometheus), `log_info/error`, `wings_openapi()` OpenAPI 3.0.
- **TLS skeleton**: OpenSSL-gated `https://` fetch ready; activates the moment OpenSSL is on the system.

**Geriye kalan opsiyonel iyileştirmeler:**

### LLVM thread-local globals (handler paralelliği için)

- Bugün'kü durum: `listen_async` çalışıyor, paralel recv/send aktif, ama handler exec `_wings_handler_mu` altında serileşiyor.
- Sonraki adım: AST'e `tls` keyword'ü veya isim-tabanlı codegen heuristic ile `_request` / `_response` global'lerini `LLVMSetThreadLocalMode(g, LLVMGeneralDynamicTLSModel)` ile işaretle. Sonra mutex kalkar, handler dispatch de paralel olur.
- Tahmini efor: ~half-day, ekstra LSP/runtime API yok.

### TLS aktivasyonu

- Code skeleton hazır (`TULPAR_HAS_TLS` define + `SSL_connect` / `SSL_read` / `SSL_write` patikleri).
- Tek eksik: OpenSSL kütüphanesi MSYS2'de kurulmalı: `pacman -S mingw-w64-x86_64-openssl`. Sonra `cmake -S . -B build-windows && build-windows/tulpar.exe` ile otomatik aktive olur, `https://` URL'leri çalışır.
- Sandbox sebebiyle bu turda kuramadım; TLS akışını doğrulamak için kullanıcı tarafında install gerekli.

### Daha küçük kalanlar

- **Object key inline cache** — analiz edildi, HTTP path'inde 0.3% kazanç. Düşük öncelik.
- **String concat coalescing** — analiz edildi, 0.1% kazanç. Düşük öncelik.
- **`tulpar pkg install` tar/zip multi-file packages** — şu an single-file `.tpr` per package; multi-file için archive extraction lazım.
- **Wings TLS listener** (`lib/wings_tls.tpr`) — cert/key path config + SSL_accept + per-request SSL_read/write. Client TLS ile aynı altyapı, server tarafı.
- **Real semver range resolution** (`^1.2`, `>=1,<2`) — şu an version string verbatim URL'ye giriyor.
- **Trust-store config** — TLS şu an `SSL_VERIFY_NONE`; production cert validation için Tulpar config'inde path lazım.
- **WebSocket / Server-Sent Events** — Wings'te uzun yaşayan bağlantı tipi yok henüz.
- **HTTP/2** — büyük iş; HTTP/1.1 keep-alive + multi-thread bugünkü iş yüklerini karşılıyor.

İstediğin zaman dur, devam ederiz.
