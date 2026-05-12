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
- **Counter atomic RMW (PR #81):** Top-level int globals için
  `LLVMBuildAtomicRMW` superinstruction; `_wings_requests_total/2xx/4xx/5xx`
  data-race-free.
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

- **CLI:** `init/list/add/remove/install/publish` + `.tpkg` JSON bundle
  (multi-file) + content sniff (raw `.tpr` vs `.tpkg`).
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
  get_json/post_json`, OpenSSL-gated `https://` (skeleton hazır).
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
- **LSP:** init, diagnostics, hover, completion, go-to-definition,
  find-references, rename. `tests/lsp_smoke.py` 9/9 check.
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
- **DAP server (Plan 07 Parça B — PR'lar #178/#180/#186/#188/#190,
  ayrıca TulparLang-ext PR #1):** `tulpar debug <file.tpr>` stdio
  JSON-RPC DAP server. AOT-build with --debug → spawn
  `gdb --interpreter=mi3` (cross-platform: Windows `CreatePipe` +
  `CreateProcessW`, POSIX `pipe` + `fork`) → background reader
  thread parses MI3 records (`*stopped`, `=...`, `~`/`@`/`&` stream)
  ve DAP `stopped`/`terminated`/`output` event'lerine çevirir.
  Yapılan handler'lar: `initialize`/`launch`/`setBreakpoints`/
  `configurationDone`/`threads`/`stackTrace`/`scopes`/`variables`/
  `continue`/`pause`/`next`/`stepIn`/`stepOut`/`terminate`/
  `disconnect`. MI tuple/list splitter `mi_split_list` ile
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

Öncelik sırası: 🔴 kritik, 🟡 önemli, 🟢 nice-to-have.

### Dil seviyesi

- 🟡 **Closures / first-class functions.** Nested `func` syntax
  parse oluyor ama outer scope capture'ı yok; `make_counter() →
  func tick() { n = n + 1; }` deseni sessizce çöküyor (program
  output vermiyor). Higher-order patterns için ortam (environment)
  yapısı ve indirect call gerek.
- 🟡 **Lambda ifadeleri.** `(int a, int b) => a + b` yok; map/filter
  benzeri kullanım sözdizimsiz.
- 🟢 **Pattern matching / `match` ifadesi.** Switch yok; if-else
  zinciri ile yapılıyor.
- 🟢 **Generics.** Type system overhaul; `func f<T>(T x): T` parse
  hatası veriyor.
- 🟢 **`async/await` keywords.** `lib/async.tpr` var ama dil-seviyesi
  state-machine transform yok.

### Runtime + codegen

- 🔴 **Wings cookies miscompile.** PR #84 hot-fix'i durdu, root cause
  hâlâ açık. 2026-05-12 araştırma turunda yeni bulgular:
  - **Windows AOT'ta da çöküyor** — PR #84 "Linux-only" demişti ama
    `examples/api_wings.tpr`'de `_request["cookies"] = wings_cookies(req);`
    satırını geri koyduğumda Windows'ta da ilk istekte segfault. Yani
    OS-spesifik değil, hem `-O3` hem `--debug` (`-O0` "verify" pipeline)
    altında tetikleniyor → LLVM optimizer bug'ı değil, Tulpar
    codegen'inde.
  - **Çöküş tam olarak subscript-write adımında** — `vm_set_element_ptr`
    çağrısı, `vm_object_set obj=NULL` ile. Crash'ten hemen önce
    `print(_request["method"])` doğru değeri ("GET") yazıyor; demek ki
    `_request` o noktada geçerli. `wings_cookies(req)` başarıyla geri
    dönüyor. ANCAK `_request["cookies"] = result;` adımında crash.
    Aynı satırı `_request["cookies"] = {};` literal RHS ile yazınca
    sorun yok.
  - Tetiklemek için `examples/api_wings.tpr` shape'i gerekiyor
    (`listen()` + arena_save + birden çok `_request[k]=...` yazımı).
    `examples/30_wings_cookies_regression.tpr` (PR #194) standalone
    pattern'i çalıştırıyor ve CI'da yeşil — yani basit sıralama
    yetmiyor, full wings function-table + ordering gerekiyor.
  - Sıradaki adım: IR diff'de `vm_set_element_ptr` çağrısının
    `target_set_tmp` packing'inde `_request.5` (obj_val) field'ının
    nasıl yüklendiğini takip etmek; muhtemelen `align 4` struct
    load/store kombinasyonu ile `struct.VMValue zeroinitializer` TLS
    init arasında bir interaction var.
- 🟢 **Codegen full atomic:** imported pass globals'a `LLVMBuildAtomicRMW`
  (top-level int globals zaten yapıldı).

### Tooling

- 🟢 **DAP server polish (post-Plan 07 Parça B).** Çekirdek + ana
  polish dilimleri 2026-05-12 turunda kapandı: `evaluate` (PR #199),
  `setVariable` (PR #201), conditional + hit-count breakpoints
  (PR #203), log breakpoints (PR #205), `restart` (PR #207),
  struct/array drill-down via `-var-create` (PR #209),
  `setFunctionBreakpoints` (PR #211). Kalan: `logMessage`
  `{expr}` interpolation (reader-thread'i callback-based result
  delivery'ye geçirmek lazım — bugünkü `wait_for_result` reader
  thread'den çağrıldığında deadlock olur), data breakpoints
  (watchpoints), instruction breakpoints.
- 🟢 **Parser multi-error mode.** Şu an ilk fatal'da duruyor; recovery
  ve bağımsız hata bildirimi eksik.
- 🟢 **LSP signature help.** Parametre içindeyken aktif imza ipucu.
- 🟢 **LSP variable + type hover.** Şu an sadece function/keyword.
- 🟢 **Incremental document sync** (`change: 2`) — büyük dosyalar.
- 🟢 **`tulpar doc` generator.** Docstring → HTML/MD.
- 🟢 **JetBrains plugin.** Sadece VS Code var.

### Ağ / I/O / TLS

- 🟢 **TLS client trust-store config.** `http_client.tpr` hâlâ
  `SSL_VERIFY_NONE` ile gidiyor — gerçek prod CA bundle entegrasyonu
  ve `https://` GET'in tam doğrulama smoke'u eksik. (Server-side
  listener tarafı PR #197 ile end-to-end smoke'lu.)
- 🟢 **WebSocket / SSE.** Wings'te uzun yaşayan bağlantı tipi yok.
- 🟢 **HTTP/2.** HTTP/1.1 keep-alive + multi-thread bugünkü iş
  yüklerini karşılıyor.

### Pkg ekosistemi (içerik)

- 🟡 **Gerçek 3rd-party paket içeriği.** Registry'de şu an sadece
  `demo` + `multipkg` smoke-test paketleri var. Stdlib paketleri
  (`wings`, `router`, `http_utils`, vb.) eski markdown listelerinde
  geçiyor ama registry'de publish edilmemiş.
- 🟢 **`pkg search` + featured packages.** Discovery zayıf.
- 🟢 **Registry binary release asset support.** Şu an sadece source
  bundles; `tulpar update` benzeri binary distribution registry'den
  henüz çekmiyor.

### Dokümantasyon

- 🟡 **Quickstart + dil reference + pkg guide → `tulparlang.dev/docs/`.**
  Şu an dokümantasyon dağınık (README + örnekler + bu dosya).

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
