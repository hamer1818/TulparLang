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
- **Tek backend — AOT-only (2026-06-15):** LLVM 18 AOT tek yürütme yolu
  (C/Rust/Go modeli). Bytecode VM yorumlayıcısı (`vm_run`), bytecode
  derleyicisi (`compiler.cpp`) ve REPL **kaldırıldı**; `--vm`/`--run`
  yok sayılır (uyarı), `--repl` çıkış verir. AOT hatası = sert hata
  (VM yedeği yok). `src/vm/` artık yalnızca **paylaşılan runtime**:
  `runtime_bindings.cpp` (aot_* builtin'leri), `vm.cpp` (arena +
  allocator'lar), `vm.hpp` (VMValue/Obj tipleri). Tree-walk interpreter
  ve x64 JIT zaten 2026-05'te sunset edilmişti. **Yeni dil özelliği
  yalnızca AOT'ta yazılır — VM/bytecode paritesi aranmaz.**
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
- **VM/AOT builtin paritesi (2026-05-13, PR'lar #243 → #264):** Sunset
  sonrası VM, `tulpar build`'in dispatch ettiği ~40 builtin için ya
  hiç bağlı değildi ya da paralel — ama tutarsız — bir
  implementasyon taşıyordu. Tüm yol şu şekle indirildi: compiler.cpp
  `OP_CALL_BUILTIN <slot>` çıkartır, vm.cpp ilgili `case`
  doğrudan `extern "C" aot_X(...)` çağırır, AOT impl'i tek doğruluk
  kaynağı olur. Kapsam: array `pop`, JSON (`fromJson`/`toJson`
  duplicate VM serializer'ın silinmesi + escape düzeltmesi), `keys`,
  HTTP (`http_parse_request`/`http_status_text`/`http_should_keepalive`/
  `path_match`/`http_create_response` 3/4/5-arity), regex (4), math
  (`pow`/`round`/`min`/`max`/`mod`/`random` + `abs` artık int
  korur), datetime (`format`/`parse_iso8601`, `now_iso8601`,
  `weekday`, `date_add_seconds`), string (`repeat`), csv, db
  (`db_execute`/`db_error`/`db_last_insert_id`), arena, time
  (`time_ms`/`timestamp`/`cpu_count`), input (`input_int`/
  `input_float`), `string_pin`, `parse_cookies`/`parse_query`. Aynı
  turda runtime_error stack trace'lerindeki `[line -1]` placeholder'ı
  `chunk_line_at()` binary search'üyle gerçek satır numarasına
  yükseldi (PR #251); `Chunk::line_offsets[]` yan dizisi RLE'ye
  yer-offset ekledi. Etki: `--vm` altında `tests/break/errors/http/
  json/json_edges/methods/router/datetime/strings/math/stdlib_extras/
  stdlib_phase_a` paketlerinin builtin'e dayalı failure'ları
  kapandı — geriye sadece `struct_native`'in by-value param
  semantiği kaldı, o native struct desteğini VM'ye getirmeyi
  gerektirir (ayrı plan).

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

- ✅ **Lambda + closures (AOT) ÇALIŞIYOR.** `(int a, int b) => a + b`
  lambda'ları, outer-scope capture'lı closure'lar (`make_counter`,
  `make_adder`, sayaç mutasyonu), higher-order ve nested kullanım,
  ayrıca top-level `var f = (..) => ..` hepsi AOT'ta yeşil
  (`tests/closure_basic.test.tpr` 3/3, `tests/closure_capture.test.tpr`
  4/4). Closure modeli: heap env array (slot 0 = parent env, slot 1+ =
  yakalanan değişkenler) + `aot_create_closure`/`aot_call_closure` +
  `analyze_module_captures` capture analizi.
  - **Kök bug (çözüldü):** capture analizi + env codegen vardı ama
    `vm_array_set` 16-baytlık VMValue'yi **değer-geçişiyle** alıyordu;
    SysV/MinGW struct-arg ABI'sinde payload eightbyte düşüyor → her
    yakalanan değişken sessizce 0'a sıfırlanıyordu. Fix: env yazımı
    `vm_array_set_aot_ptr_wrapper` ile **pointer-geçişine** çevrildi
    (`vm_array_push` gibi); ayrıca `codegen_typed_expr`'in
    `AST_IDENTIFIER`'ına ve `AST_VARIABLE_DECL`'e captured-var dalları
    eklendi, `aot_call_closure`'daki `[DEBUG]` print'leri kaldırıldı.
  - **Not:** AOT tek yürütme yolu (AOT-only); VM kaldırıldığı için
    "VM closure paritesi" diye bir iş artık yok.

- ✅ **Pattern matching / `match` ifadesi ÇALIŞIYOR.** Rust-stili
  `match subject { 1 => "a", _ => "f" }`. Hem ifade (değer döndürür,
  `str g = match score {…}`) hem statement pozisyonunda; int/str/bool
  subject + `_` wildcard; arm gövdesi expr veya blok. AOT'ta yeşil
  (`tests/match.test.tpr` 6/6, `examples/33_match.tpr`).
  Pattern'lar: literal, `_` wildcard, `|`-alternatif (`1 | 2 | 3`) ve
  inclusive range (`10 .. 20`). Lowering: subject bir kez değerlendirilir
  (VM'de temp local slot), her atom `==`/range/OR testiyle if-else
  zincirine iner. `match` artık ayrılmış keyword (TR: `eşle`/`esle`),
  `..`=`TOKEN_DOTDOT`, `|`=`TOKEN_PIPE`.
  - **Kalan (🟢, v1.2):** struct/array destructuring pattern'ları
    (`Point{x, y} => …`). Şu an pattern'lar skaler değer eşleştirir.

- 🟢 **Generics.** Type system overhaul; `func f<T>(T x): T` parse
  hatası veriyor.
  - **Sıradaki adım:** v1.0 sonrası. Typecheck pre-pass'te `<T>`
    syntax'ını parse + iz sürebilecek şekilde genişlet,
    monomorphization stratejisini seç.

- ✅ **`async/await` (AOT) ÇALIŞIYOR.** `async func f(){ await sleep_async(10);
  return 1; }`, `var p = f(); int r = await p;`. Gerçek kooperatif eşzamanlılık:
  bir coroutine `await`'te askıya alınırken diğerleri çalışır (10ms timer'lı
  task, 20ms'liden önce yerleşir — `examples/34_async.tpr` çıktısıyla doğrulandı).
  `tests/async.test.tpr` 4/4 yeşil, `examples/34_async.tpr` suite'te PASS.
  - **Mimari (state-machine DEĞİL):** stackful coroutine + event loop
    (`runtime/tulpar_async.{h,cpp}`). POSIX `ucontext` / Windows Fiber ile
    yığın takası → compiler dokunuşu minimal (CPS/state-machine transform yok).
    `async func` boxed `t_<name>(ret*,args*)` ABI'ye zorlanır; çağrı
    `aot_async_spawn` ile coroutine başlatıp promise döndürür; `await`
    `aot_await` ile yield eder; `sleep_async(ms)` bloklamayan timer; program
    sonunda `aot_event_loop_run()` kalan task'ları drene eder.
  - **Promise:** yeni `OBJ_PROMISE` obj tipi (`vm.hpp`), `arena_allocated=1`
    ile ARC erken-free etmez (scheduler raw pointer tutar).
  - **Kalan (🟡):** (1) `gather`/`Promise.all`, reject/try üzerinden hata
    yayılımı, gerçek async I/O (şu an timer + compute) sonraki turlar.
    (2) `>8` parametreli async fn desteklenmiyor. (VM paritesi yok — AOT-only.)

### Runtime + codegen

- 🟢 **Wings cookies miscompile.** Çözüldü ve yeşil yapıldı.
  - **Kök Neden:** AOT/HTTP ayrıştırıcısı (HTTP parser) tarafından oluşturulan yerel HTTP istek nesneleri (request objects/arrays) AOT arena alanında (`aot_arena_alloc`) tahsis edilmektedir ve `arena_allocated = 1` bayrağı taşımaktadır. `lib/wings.tpr` içerisinde bu nesnelere ardışık yeni anahtarlar eklendiğinde (`_request["params"]`, `_request["cookies"]`), nesnenin kapasitesi dolduğu için runtime üzerindeki `vm_object_set` veya `vm_object_set_aot_wrapper` fonksiyonları nesneyi yeniden boyutlandırmaya (resize) çalışıyordu. Orijinal kod, arena üzerinde tahsis edilmiş olan bu nesnelerin `keys`/`values` veya `items` işaretçilerini standart `realloc` ile büyütmeye çalışıyordu. Arena bellek alanları doğrudan `malloc` ile alınmadığı için, `realloc` çağrısı doğrudan heap bozulmasına (`STATUS_HEAP_CORRUPTION` - `0xC0000374`) ve çökmeye yol açıyordu.
  - **Çözüm:** `src/vm/runtime_bindings.cpp` dosyasındaki `vm_object_set`, `vm_object_set_aot_wrapper`, `vm_array_push_aot_wrapper` ve `aot_array_push` fonksiyonları güncellenerek `arena_allocated` bayrağı kontrol edildi ve bu nesnelerin kapasiteleri büyütülürken `realloc` yerine `aot_arena_alloc` kullanılarak elemanlar yeni bir arena bloğuna kopyalandı. `lib/wings.tpr` dosyasında `_request["cookies"] = wings_cookies(req);` satırı başarıyla geri yüklendi ve tüm AOT web testleri başarıyla geçmektedir.

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

- 🟢 **VM'de native struct desteği yok.** `tests/struct_native.test.tpr`
  altında 17/19 yeşil ama `_translate_x(Point p, int dx)` gibi
  "typed struct param + field mutation + return" yolu fail ediyor:
  expected 101 got 1 — yani mutasyon hiç oluşmadan dönüyor. AOT
  Plan 04 native struct codegen'i tamam, VM tarafında karşılığı yok
  (her şey boxed VMValue üzerinden ilerliyor).
  - **Sıradaki adım:** VM'ye typed-struct lokal slot kavramını
    getir — `Chunk::struct_layouts[]` benzeri bir yapı + her
    typed param için kopya semantiği. Önce typed-int field
    okuma/yazmayı düzelt, sonra by-value parametre kopyalamayı
    ekle. Düşük öncelik: AOT primary path, VM kullanıcıları struct
    yoluyla geçmiyor.

- ✅ **AOT bool→int variable-decl coercion divergence (ÇÖZÜLDÜ).**
  `int ok = db_execute(...);` artık AOT'ta da int olarak tutuluyor
  (`toString` "1"). Fix `llvm_backend.cpp` AST_VARIABLE_DECL boxed
  yolunda (~5474): `node->data_type == TYPE_INT` ise boxed VMValue'nun
  type-tag'i `VM_VAL_BOOL` → `VM_VAL_INT` olarak yeniden yazılıyor
  (slot 2 payload her iki tag için de aynı int olduğundan kayıpsız).
  Not: ilk hipotez (i1→i64 zext) yanlıştı — `INFERRED_BOOL` değerleri
  `codegen_typed_expr`'de zaten i64; sorun store değil type-tag idi.

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
