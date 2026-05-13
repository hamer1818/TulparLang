# TulparLang — Durum ve Yol Haritası

> **Motto:** Python kadar kolay, C kadar hızlı — hızlı API yazma dili.

Bu dosya, projenin tek "ne durumda?" referansıdır. Eski `EKSIKLER.md`
(60 madde, hepsi RESOLVED) ve `OZET.md` (faz-bazlı tarihçe) bu dosyada
toplandı. Yeni eksiklikler buradaki **Açık eksikler** bölümüne eklenir;
çözülen iş **Yapılanlar** altına özet bir satırla taşınır.

---

## 📊 Mevcut durum (özet)

- **Çekirdek sözdizimi tam:** statik tipler, fonksiyonlar (opsiyonel
  dönüş-tipi notasyonu), kontrol akışı, modüller (`import "x" as alias`).
- **İki backend:** LLVM 18 AOT (`tulpar build`) + VM (`--vm`, REPL, AOT
  fallback). Tree-walk interpreter ve x64 JIT sunset edildi (2026-05).
- **Cross-platform:** Windows (Inno Setup installer), Linux, macOS,
  WASM. `libtulpar_runtime.a` AOT'a static link.
- **Stdlib gömülü:** wings, router, http_utils, http_client, async,
  middleware, socket, tulpar_api, orm, test. SQLite vendored.
- **Pkg manager:** `init/add/install/publish` + semver (`^`, `~`,
  `>=,<`) + lockfile + `.tpkg` multi-file bundle. Canlı registry
  `api.pkg.tulparlang.dev` (tulpar-be, Tulpar'da yazılmış).
- **Tooling:** `tulpar fmt`, `tulpar --lsp`, `tulpar typecheck`,
  `tulpar update`. VS Code eklentisi (`vscode-tulpar` v0.3+) LSP
  client.
- **Performans:** AOT loopsum ~1.9 ms (C'nin 3.17×ı), fib(35) ~32 ms
  (C'nin 1.87×ı, Rust seviyesinde). HTTP Wings ~22-26k rps CI Linux,
  Node.js'i 1.7-2.1× geçiyor. Bench auto-refresh CI'da çalışıyor,
  README + RESULTS.md her commit'te güncelleniyor.

---

## ✅ Yapılanlar

### Çekirdek dil + derleme zinciri

- **Parser/codegen sertleştirmesi (2026-04 turu, EKSIKLER #1-47):**
  postfix-after-unary, recursive forward reference, `func name(...): T`
  syntax, AOT cross-platform (Windows MSYS2, MSVC `gettimeofday`,
  Winsock makro çakışması, CMake LLVM `ZLIB::ZLIB`/`LibXml2`), fn-local
  json/string codegen, hot-loop alloca, BubbleSort stack-overrun, imported
  modüllerde fn signature forward-decl, Rust-stil hata mesajları (source
  preview + caret + "did you mean?" Levenshtein).
- **Modül namespace'leri (2026-05, PR #35):** `import "x" as y` her
  top-level fn'i `y__name` olarak yeniden adlandırır; intra-module
  çağrılar lockstep rewrite edilir. `m.func(args)` postfix syntax
  rewrite ile `m__func(args)`'a inilir.
- **Real method calls on objects (Plan 01):** `obj.method(args)`
  obj modül alias değilse `method(obj, args)` formuna çözünür ve
  mevcut serbest-fn dispatch'i üzerinden çalışır. Parser
  `parse_postfix` fallback'i ile sağlanıyor.
- **try / catch / finally + throw:** dil-seviyesi exception handling
  çalışıyor. `throw "oops"` → en yakın `catch (e)` clause'a sıçrar,
  `finally` her iki yolda da çalışır.
- **VM string concat fn-local fix (PR ~#35 öncesi):**
  `OP_LOAD_ADD_STORE`'a string dalı.
- **Type inference pre-pass (PR #32):** `tulpar typecheck` standalone,
  her `build`/`run`/`--vm` çağrısında `[typecheck]` uyarıları.
  `--no-typecheck` / `TULPAR_NO_TYPECHECK=1` opt-out.
- **Sunset turu (2026-05-04, PR #30+#31):** Tree-walk interpreter
  (−6109 satır), x64 JIT (−2162 satır). REPL artık VM compiler'a
  feed eder.

### HTTP runtime (Wings/Router native)

- **Native HTTP parser:** `http_parse_request(raw)`,
  `path_match(pattern, path)` (Express-style, `:id` capture, `*`
  wildcard), `parse_query(str)` URL-decoded, `http_status_text(int)`
  (30+ IANA reason), `http_create_response(status, ct, body, [headers])`
  4-arg form custom headers (CORS, Set-Cookie, X-*).
- **Wings handlers:** default CORS on, auto OPTIONS → 204 preflight,
  `_request` TLS (mutex kalkmış), `_wings_handler_mu` artık yok.
- **Streaming `http_recv_request`:** Content-Length-aware,
  `TULPAR_HTTP_MAX_BODY` env override (default 16 MiB).
- **4 listener varyantı:** `listen` (sync), `listen_async`
  (thread-per-conn), `listen_pool(port, n)` (worker pool, n=0 →
  `cpu_count()`), `listen_evented` (poll()-multiplex).
- **Wire-byte cache:** `cached_get(path, handler)` — handler bir kez
  çalışır, sonraki istekler cached bytes'i tek syscall ile gönderir.
- **Counter atomic RMW (PR #81):** Top-level VE imported-module
  int globals için `LLVMBuildAtomicRMW` superinstruction;
  `_wings_requests_total/2xx/4xx/5xx` data-race-free. Imported
  int globals AOT'da `add_local_typed(.., INFERRED_INT, ig)` ile
  kayıtlanır → `AST_ASSIGNMENT` typed-int fast-path'i
  `global_needs_atomic_rmw()` whitelist'ine takılan yazımları
  `atomicrmw add monotonic`'e indirir. `import "wings"`'le çekildiğinde
  de race-free.
- **Thread-local `_request` (PR #79):** `global_needs_tls()` AOT
  helper'ı `_request` global'ini `LLVMGeneralDynamicTLSModel` ile
  işaretliyor → her thread kendi slot'una yazar, mutex serileşmesi
  yok. Wings 4 listener varyantında handler dispatch tamamen paralel
  ilerliyor; `_wings_handler_mu` kalıntısı da PR #79'la kaldırıldı.
- **Native fast path'ler:** `wings_build_response` (envelope+toJson+framing
  tek C çağrısı), `wings_find_route` (route lookup tek C çağrısı),
  `fast_u32_itoa` + el-yazısı framing, co-located ObjString+chars
  allocation. Localhost loopback bind (LAN firewall prompt yok).
- **HTTPS listener (`lib/wings_tls.tpr`):** `wings_tls(port, cert, key)`
  açılışta tek bir `SSL_CTX` kurar, kabul edilen her bağlantıyı
  thread-per-conn worker'la `SSL_accept` → `SSL_read` →
  handler dispatch → `SSL_write` → `SSL_shutdown` zincirinde
  servis eder. OpenSSL gating CMake `find_package(OpenSSL)` ile.
  PR #197 ile CI smoke probe (`examples/api_wings_tls.tpr` +
  `tests/fixtures/tls_dev.crt/.key` 10-yıl geçerli self-signed
  fixture) end-to-end doğrulama altında.

### Pkg ekosistemi

- **CLI:** `init/list/add/remove/install/publish/search/info` + `.tpkg`
  JSON bundle (multi-file) + content sniff (raw `.tpr` vs `.tpkg`).
  `pkg search [query]` registry catalog'unu ASCII-lowercase substring
  match ile filtreler (boş query = tüm listele), `pkg info <name>`
  ise tek paket detayı (versiyon listesi, indirme sayısı, install
  hint string). İkisi de `--registry <url>` flag'i kabul eder ve
  registry'siz çağrıyı hata ile reddeder.
- **Manifest:** TOML alt-kümesi, `[registry]` + `[dependencies]`
  bölümleri, `strict = true` typecheck pre-pass eskaltörü.
- **Semver range:** exact, caret (`^1.2.3`), tilde (`~1.2.3`),
  any (`*`/`""`), bare comparator (`>=`, `<`, `=`), compound
  (`>=1,<2`). Pre-release + build-metadata (`1.0.0-rc1+ci.42`)
  parser + comparator: semver 2.0.0 §11 ordering, build
  metadata round-trips but doesn't affect precedence.
  `tests/pkg_smoke.py` covers add/remove/lockfile round-trip.
- **Lockfile (`tulpar.lock`):** `[resolved]` + `[checksums]` (SHA-256),
  byte-stable re-install + tamper detection.
- **Canlı registry (tulpar-be, Tulpar dilinde yazılmış):**
  `/v1/packages`, `/v1/packages/:name`,
  `/v1/packages/:name/versions/:version`,
  `/v1/packages/:name/versions/:version/source`, `/v1/publish`
  (Bearer auth). Audit log + per-IP rate limit + semver-aware
  `latest`.
- **`pkg init` default registry URL** (PR #147): yeni manifest'te
  `[registry] url = "https://api.pkg.tulparlang.dev"` yazılır;
  out-of-the-box install çalışır.
- **Registry source endpoint hardening** (tulpar-be PR #27):
  not-found durumunda 200+comment yerine 404 döner — silent
  corrupt install önlendi.

### Stdlib + tooling

- **Test framework (`lib/test.tpr`):** jest-style `assert`,
  `assert_eq_int/str/bool`, `assert_contains`, `assert_status`,
  `assert_throws`. Runner: `test()` + `test_summary()`.
- **HTTP client (`lib/http_client.tpr`):** `http_get/post/put/delete/
  get_json/post_json`, OpenSSL-gated `https://`. Trust-store
  doğrulaması default açık: `SSL_VERIFY_PEER` + RFC 6125 hostname
  check via `X509_VERIFY_PARAM_set1_host`. `TULPAR_CA_BUNDLE=<pem>`
  ile özel CA bundle (MSYS2 builds default trust-store'a sahip
  değil), `TULPAR_TLS_INSECURE=1` ile self-signed / dev fixture'a
  düşüş — `make_client_tls_ctx` + `apply_tls_hostname_check` hem
  `tulpar pkg install` hem `http_request` builtin'inden geçer,
  handshake hatası mesajında `cert verify: …` detayı taşır.
- **Kripto / encoding builtins:** `sha1(s)` (20-bayt ikili),
  `sha1_hex(s)` (40 hex), `base64_encode(s)`, `base64_decode(s)`,
  `wings_ws_accept_key(s)` (RFC 6455 §4.2.2 base64+sha1+GUID).
  Saf C++ (`runtime_bindings.cpp`), OpenSSL bağımlılığı yok —
  signed cookies, JWT HMAC, ETag, content-addressed cache vb. için
  ortak yapı taşı. `examples/31_crypto_sse_ws.tpr` FIPS 180-1
  TEST1 ve RFC 6455 §1.3 ws-handshake vektörlerini smoke test
  ediyor.
- **Wings WebSocket upgrade + frame I/O + SSE helpers:**
  `wings_ws_upgrade(req)` 101 Switching Protocols +
  Sec-WebSocket-Accept üretir; `wings_ws_send_frame(fd, opcode, payload)`
  FIN+opcode + length-encoded frame (< 126 / 16-bit / 64-bit
  uzunluk yolları) bir tek `send()` çağrısıyla yazar;
  `wings_ws_recv_frame(fd)` header parse + 7/16/64-bit length
  decode + masking key XOR'unu yapar, `{ok, opcode, fin, payload}`
  veya `{ok=0, error}` döner. `lib/wings.tpr` ergonomic
  wrapper'ları: `wings_ws_send_text/close/pong` +
  `WS_OPCODE_TEXT/BINARY/CLOSE/PING/PONG` sabitleri. SSE tarafında
  `wings_sse_headers()` + `wings_sse_event(name, data)` —
  `text/event-stream` yanıt başlığı + `data: …\r\n\r\n` event
  çerçevesi formatter'ları. `examples/32_wings_ws_frames.tpr`
  round-trip smoke + accept-key vector doğruluyor.
- **Wings streaming dispatcher (SSE / WS keep-alive akış):**
  Handler `{"_stream": 1}` döndürdüğünde wings dispatcher yanıt
  envelope build'ini atlar, soketi force-close eder
  (keep-alive bir stream'i hayatta tutamaz). Handler aktif istek
  fd'sini `wings_current_fd()` ile alır (C-tarafı thread-local,
  her istek başında dispatcher tarafından set ediliyor), sonra
  `socket_send` + `wings_sse_*` / `wings_ws_*` formatter'ları ile
  doğrudan yazar. Sidechannel `_request[k] = …` yazılımından
  bilinçli olarak kaçınıyor — açık wings cookies miscompile'ı
  tetiklememek için. `examples/api_wings_sse.tpr` end-to-end
  smoke: 5 tick SSE stream + done event, `/` + `/healthz` +
  `/metrics` paralel çalışıyor.
- **Datetime + regex stdlib:** `now`, `format`, `parse_iso8601`,
  `weekday`, `date_add_seconds`, `regex_match/search/capture/replace`.
- **CSV + glob + env:** `csv_parse/emit`, `file_glob`, `env()`,
  `keys()`.
- **ORM (`lib/orm.tpr`):** Active-Record over SQLite (define
  table, insert, find, update, delete).
- **Wings prod observability:** `/healthz`, `/metrics` (JSON ve
  Prometheus text), `wings_openapi(title, version)` OpenAPI 3.0
  generator, `log_info/error` structured JSON.
- **Formatter:** indent + trailing-ws + EOF newline + blank-line
  collapse + keyword-aware spacing, idempotent token-pass.
- **Parser multi-error mode:** ilk hatadan sonra panic-mode
  synchronisation (statement-terminator `;`/`}` veya
  statement-başlangıcı keyword'üne kadar atla) ile devam edip
  geri kalan dosyada bağımsız hata bildirimleri üretiyor. Tek
  derleme çağrısında birden fazla `parse error` rapor
  edilebiliyor — `tulpar build` çıkışında "missing `;`"
  diagnosticleri sıralı bir liste halinde geliyor.
- **`tulpar doc` (PR #219):** Markdown reference üretici.
  `tulpar doc <file.tpr>` → fonksiyon + top-level global +
  leading-comment docstring'leri stdout'a Markdown olarak
  basar. `--include-internal` underscore-prefixed isimleri de
  ekler. LSP hover'ı besleyen aynı `aot_check_and_index`
  helper'ından geçer; iki yüzey lockstep.
- **LSP:** init, diagnostics, hover (functions, builtins, AND
  scope-aware variables), completion, go-to-definition,
  find-references, rename, signatureHelp (active-parameter aware
  for both user functions and builtins), **incremental document
  sync** (`change=2`; full-text replace still accepted for
  clients that don't negotiate the cheaper form).
  `tests/lsp_smoke.py` 9/9 check.
- **AOT debug info (Plan 07 Parça A — PR'lar #160–#173):**
  `tulpar [--debug | -g] build <file>` opt-in. LLVM IR'da `!dbg`
  metadata: `DICompileUnit` + `DIFile` + module flags, her user
  fonksiyon (+ main) için `DISubprogram`, her statement için
  `DILocation` (top-level + boxed + typed-int body), her int/boxed
  VMValue local + parameter için `DILocalVariable` + `dbg.declare`,
  top-level + imported-module globals için
  `DIGlobalVariableExpression` + `dbg` metadata kind. Optimizer
  `--debug` modunda `verify` (`-O0`) — 1:1 source mapping korunur.
  `CMakeLists.txt` `TULPAR_LLVM_MAJOR` macro'su LLVM 18 (CI) ile
  LLVM 19 (MSYS2) arası C API rename'leri için hazır. `gdb
  ./binary` step-through + breakpoint + `info locals/variables`
  artık `.tpr` satırlarına çözünüyor.
- **DAP server (Plan 07 Parça B — TulparLang #178/#180/#186/#188/#190
  ve tüm polish bundle'ları #199–#223, ayrıca TulparLang-ext PR #1):**
  `tulpar debug <file.tpr>` stdio JSON-RPC DAP server. AOT-build
  with --debug → spawn `gdb --interpreter=mi3` (cross-platform:
  Windows `CreatePipe` + `CreateProcessW`, POSIX `pipe` + `fork`) →
  background reader thread MI3 records (`*stopped`, `=...`,
  `~`/`@`/`&` stream) parse edip DAP
  `stopped`/`terminated`/`output` event'lerine çevirir.
  Yapılan handler'lar: `initialize`/`launch`/`setBreakpoints`/
  `setFunctionBreakpoints`/`setDataBreakpoints`/`dataBreakpointInfo`/
  `setInstructionBreakpoints`/`configurationDone`/`threads`/
  `stackTrace`/`scopes`/`variables` (struct/array drill-down via
  `-var-create`)/`evaluate`/`setVariable`/`continue`/`pause`/`next`/
  `stepIn`/`stepOut`/`restart`/`terminate`/`disconnect`.
  Breakpoint'lerin tamamı condition + hit-count destekli; logpoint
  `logMessage`'ları `{expr}` interpolation ile detached worker
  thread'de evaluate edilir + `output` event yayınlanıp
  `-exec-continue` ile devam edilir (reader thread'den synchronous
  evaluate deadlock olur, worker thread bunu sidestep eder). MI
  tuple/list splitter `mi_split_list` ile
  `stack=[frame={...},frame={...}]` ve `variables=[{...},{...}]`
  parse edilir. SIGINT disambiguation (`pause` vs `exception`).
  `tests/dap_smoke.py` gdb-missing branch + gdb-present
  setBreakpoints → stack inspection → continue → terminated
  round-trip'i doğruluyor. `vscode-tulpar` v0.4.0
  `contributes.debuggers` (`type: "tulpar"`) + `breakpoints` +
  `tulpar.debug` komutu ile F5 ergonomi.

### Test harness

- **CI runtime smoke (build.sh + run_tests.ps1):** her
  `COMPILE_ONLY_TESTS` örneği derleme sonrası 2 s spawn edilir,
  `kill -0` (PS: `HasExited`) ile hayatta mı kontrol edilir, kayıtlı
  HTTP probe URL'i varsa (wings/router examples için
  `127.0.0.1:3000/` veya `:8080/`) `curl --max-time 5` ile çağrılır,
  sonra `SIGTERM` ile durdurulur. Probe HTTP responsesı bekleyenden
  farklıysa veya server probe sırasında ölürse FAIL. `continue-on-error`
  mask'i CI workflow'undan kaldırıldı; gerçek bir runtime regresyonu
  iş kırmızı yapar, release'i bloke eder. PR #76 wings cookies
  miscompile gibi 4 PR boyunca main'de yatan regresyonların
  tekrarını önler.

### Benchmark harness

- `benchmarks/ci_run.py` her main push'unda çalışır, RESULTS.json +
  RESULTS.md + README badge bloğu otomatik güncellenir (bench-bot
  PR'ları).
- CPU bench (loopsum, fib): C, Rust, Go, Node, Python karşılaştırma.
- HTTP bench: 5 Tulpar varyantı + Node.js + Python ThreadingHTTP;
  düşük (3000/4) ve yüksek (12000/16) concurrency iki tablo.
- Localhost-only socket bind: Windows Firewall prompt yok.

### Build / release / CI

- CMake-presets (`CMakePresets.json`), MSYS2 OpenSSL default
  detection, AOT linker `--export-all-symbols` / `-rdynamic`
  (kullanıcı fonksiyonları `call()` ile erişilebilir).
- Inno Setup Windows installer (`installer/tulpar.iss`):
  `tulpar.exe` + `libtulpar_runtime.a` `%LOCALAPPDATA%`'a.
- GitHub Actions: docs-only commit'ler step-level skip (gates
  hala "success"), build-linux/macos/windows + benchmark + auto-merge
  chain.
- VS Code marketplace: `vscode-tulpar` v0.3+ (12 snippet, 60+ builtin
  grammar, LSP-aware README).

### Doc + site

- `pkg.tulparlang.dev` Astro static site (Cloudflare Pages) +
  Pages Functions (legacy `<name>/<version>.tpr` resolver proxy →
  `api.pkg.tulparlang.dev`).
- README + landing güncel performans rakamlarıyla.

---

## 🔜 Açık eksikler

Öncelik sırası: 🔴 kritik, 🟡 önemli, 🟢 nice-to-have. Her madde
**"Sıradaki adım"** satırıyla: bir sonraki oturuma cold-start
girildiğinde ne yapacağımı bilelim.

### Dil seviyesi

- 🟡 **Closures / first-class functions.** Nested `func` syntax
  parse oluyor ama outer scope capture'ı yok; `make_counter() →
  func tick() { n = n + 1; }` deseni sessizce çöküyor (program
  output vermiyor). Higher-order patterns için ortam (environment)
  yapısı ve indirect call gerek.
  - **Sıradaki adım:** Mini-plan: (1) closure object layout
    (heap env ptr + fn ptr), (2) AST'ye capture analizi,
    (3) codegen'de env struct + indirect call site. Lambda PR'ı
    (aşağıda) parser scaffold'unu hazırladı; closure body'sini
    onun üzerine kur.

- 🟡 **Lambda ifadeleri.** `(int a, int b) => a + b` parser
  scaffolding'i yerinde (lexer'a `TOKEN_FAT_ARROW` eklendi,
  `(<type> ...) =>` deseni ve bare `=>` parser'da "lambda
  expressions not yet supported" diagnostic'iyle yakalanıyor) —
  AST node + codegen + env capture follow-up PR'lar için bekliyor.
  - **Sıradaki adım:** Diagnostic'i kaldır, `AST_LAMBDA` üret.
    Sıfır-capture lambda'lardan başla (anonim fn olarak codegen,
    fn pointer döndür); `examples/lambda_zero_capture.tpr` ile
    smoke. Capture'lı versiyon closures iş başlayınca eklenir.

- 🟢 **Pattern matching / `match` ifadesi.** Switch yok; if-else
  zinciri ile yapılıyor.
  - **Sıradaki adım:** Sözdizim kararı (`match x { 1 => …, _ => … }`?).
    Sonra parser node + AST + codegen — basit literal pattern'larla
    başla; struct/array destructuring v1.1'e ertelenebilir.

- 🟢 **Generics.** Type system overhaul; `func f<T>(T x): T` parse
  hatası veriyor.
  - **Sıradaki adım:** v1.0 sonrası. Typecheck pre-pass'te `<T>`
    syntax'ını parse + iz sürebilecek şekilde genişlet,
    monomorphization stratejisini seç.

- 🟢 **`async/await` keywords.** `lib/async.tpr` var ama dil-seviyesi
  state-machine transform yok.
  - **Sıradaki adım:** Closures + first-class fn iş bittikten
    sonra. Async fn'leri state-machine'e çevirmek callback-based
    runtime üzerine kurulu.

### Runtime + codegen

- 🔴 **Wings cookies miscompile.** PR #84 hot-fix'i durdu, root cause
  hâlâ açık. Mevcut bulgular:
  - **Windows + Linux AOT'ta çöküyor** —
    `examples/api_wings.tpr`'de `_request["cookies"] = wings_cookies(req);`
    satırını geri koyduğumda ilk istekte segfault. OS-spesifik
    değil, hem `-O3` hem `--debug` (`-O0` "verify") altında
    tetikleniyor → LLVM optimizer bug'ı değil, Tulpar codegen'inde.
  - **Çöküş tam olarak subscript-write adımında** —
    `vm_set_element_ptr` çağrısı `vm_object_set obj=NULL` ile.
    `wings_cookies(req)` başarıyla dönüyor; `_request["cookies"] = result;`
    yazımında crash. Aynı satırı `_request["cookies"] = {};` literal
    RHS ile yazınca sorun yok.
  - **Yeni bulgu (2026-05-13, PR #233):** Sadece "function call
    RHS" tetiklemiyor. SSE dispatcher denerken
    `_request["_fd"] = client;` (local int RHS) eklediğimde bir
    sonraki `_request["params"] = route_match["params"];`
    crashladı. Yani iki ardışık `_request[k]=...` yazımı
    yeterli — RHS'in fn-call olması zorunlu değil.
  - **Minimal repro ÇALIŞMIYOR:** İzole bir
    `_request = {…}; _request[k1]=…; _request[k2]=…;` programı
    plain global + TLS global ikisinde de sorunsuz çalışıyor. Bug
    "import wings + tam dispatcher + listen() loop" bağlamı
    gerektiriyor. Full wings function-table + ordering kritik;
    `examples/30_wings_cookies_regression.tpr` (PR #194)
    standalone pattern'i CI'da yeşil.
  - **PR #213** `_request`'in TLS modelini `GeneralDynamic` →
    `LocalExec` swapladı, bug değişmedi → access mekanizmasında
    değil, store/load shape'inde.
  - **Sıradaki adım:** `lib/wings.tpr`'a `_request["cookies"] = wings_cookies(req);`
    geri koy. `examples/api_wings.tpr`'i `tulpar build --emit-llvm`
    (varsa) veya intermediate `.ll`/`.o` çıktısıyla derle.
    Crash öncesi `vm_set_element_ptr` çağrısının `target_set_tmp`
    packing'inde `_request`'in struct field 5 (`obj_val`) nasıl
    yüklendiğini incele — align 4 vs align 8, TLS slot init'i ile
    load arası interaction, RIP-rel'den hangi sembole erişiliyor.
    `addr2line` + `objdump -dS` ile crashed PC → kaynak satır.
    Hedef: tek bir LLVM IR farkı bulup minimal fix.

- 🟢 **WebSocket recv multi-call calling-conv quirk** (PR #231
  development sırasında çıktı, doğrudan kullanıcı etkisi yok ama
  smoke'da multi-frame round-trip yazamadık): ardışık
  `wings_ws_recv_frame(fd)` çağrılarında ikinci çağrı `fdVal`
  parametresini ilk çağrıdan farklı (server thread'in accepted
  fd'si) görüyor. Debug print'lerinde `[recv] enter fd=344` sonrası
  `[recv] enter fd=356` çıkıyor. Tek-frame round-trip çalışıyor
  (`examples/32_wings_ws_frames.tpr` PASS).
  - **Sıradaki adım:** AOT'da `wings_ws_recv_frame` dispatch
    site'ına bak — VMValue 1-arg fn ABI'si MinGW64 SysV-shaped
    struct'da sıkışıklık yaşıyor olabilir (aot_socket_close'a
    yapılan ABI fix'inin benzeri). Şu an `string_pin_type`
    kullanılıyor; tek-i64 alan ayrı bir signature deneyebilir,
    veya çağrı taraflı `LLVMBuildAlloca` + value-by-pointer
    ABI'ye geç.

### Tooling

- 🟢 **JetBrains plugin.** Sadece VS Code var.
  - **Sıradaki adım:** IntelliJ Platform SDK + grammar skeleton.
    LSP4IJ ile LSP proxy en kestirme; native plugin v2.0.

### Ağ / I/O

- 🟢 **HTTP/2.** HTTP/1.1 keep-alive + multi-thread bugünkü iş
  yüklerini karşılıyor.
  - **Sıradaki adım:** v1.x sonrası. nghttp2 entegre etmek
    (`wings_h2(port, …)` listener) veya saf C HPACK decoder —
    karar gerekli.

### Pkg ekosistemi (içerik)

- 🟡 **Gerçek 3rd-party paket içeriği.** Registry'de şu an sadece
  `demo` + `multipkg` smoke-test paketleri var. Stdlib paketleri
  (`wings`, `router`, `http_utils`, vb.) eski markdown listelerinde
  geçiyor ama registry'de publish edilmemiş.
  - **Sıradaki adım:** v1.0 release sonrası gerçek kullanıcı
    paketlerini bekle; veya kendi wings-extension'larımızı
    (`wings_jwt`, `wings_postgres` vb.) publish et.

- 🟢 **Server-side `/v1/search` endpoint.** Bugün `pkg search`
  client-side filtreliyor — registry catalog'u büyüdüğünde
  server-side full-text + featured-flag endpoint'ine geçmek gerek.
  Şu anki paket sayısı (2) ile gereksiz.
  - **Sıradaki adım:** N>50 paket olduğunda; tulpar-be'ye
    `/v1/search?q=…` ekle, SQLite FTS5 ile.

- 🟢 **Registry binary release asset support.** Şu an sadece source
  bundles; `tulpar update` benzeri binary distribution registry'den
  henüz çekmiyor.
  - **Sıradaki adım:** Manifest'e `[release.binaries] linux-x64 = "url"`
    şeması ekle; `pkg add` opt-in binary çekme.

### Dokümantasyon

- 🟡 **`tulparlang.dev/docs/` deep dives.** README'de Quick start +
  Build a REST API + Streaming (SSE/WS) + Package management
  bölümleri var (PR #234 ile), CLI reference + Standard library
  tablosu güncel — fakat tulparlang.dev/docs/ altındaki tam dil
  reference, derleyici flag matrisi, OS-spesifik kurulum
  kılavuzları hâlâ eksik. README "quickstart" eşiğini karşılıyor;
  reference + pkg/wings derinlemesine kılavuzları için ayrı bir
  docs commit turu gerek.
  - **Sıradaki adım:** `pkg.tulparlang.dev` Astro repo'sunu klonla
    (veya yeni `docs.tulparlang.dev` aç), `docs/` collection'ı
    ekle, README içeriğini bölümlere ayır + genişlet. v1.0
    release blocker.

---

## 🎯 v1.0 = "dil tam oldu" kriterleri

1. **Sıfır bilinen davranış regresyonu** — CI compile + runtime gate'leri
   yeşil; `git log --grep="fix(.*regression"` boş.
2. **Motto taşınıyor** — bench'ler C/Rust sınıfında (loopsum 3×C içinde,
   HTTP Node'u 2×+ geçiyor); örnekler Python kadar okunur.
3. **Ekosistem self-host** — tulpar-be prod'da, kullanıcı `tulpar pkg
   add foo@^1` ile çalışan dep ekleyebiliyor (✓ teknik altyapı; içerik
   eksik).
4. **Dokümantasyon eşiği** — quickstart + reference + pkg guide
   `tulparlang.dev/docs/` altında canlı.
5. **Stable release** — `v1.0.0` git tag + binary release artifacts;
   `tulpar update` mekanizması bunu çekiyor.

**Şu an konumumuz:** v0.9 RC yakını. (1) ve (2) büyük ölçüde karşılandı.
(3) altyapı tam, içerik az. (4) eksik. (5) installer hazır, tag süreci
hazır.

---

## Notlar

- Bilinen ufak konular:
  - Parse error recovery satır no'su occasionally bir satır kayık.
  - `[AOT]` mesajları verbose build modunda çıkıyor (silent path temiz).
  - `assert_throws` çalışıyor, string `==` edge case'lerinde toString
    roundtrip gerekebilir.
- `tulpar-be` ayrı bir repo (`d:/yazilim/tulpar-be`); Tulpar dilinde
  yazılmış (`registry.tpr`, ~60 KB), Docker'da deploy.
- `pkg.tulparlang.dev` Astro static site, ayrı bir repo.
- `EKSIKLER.md` ve `OZET.md` (eski) bu dosyada toplandı, repodan kaldırıldı.
