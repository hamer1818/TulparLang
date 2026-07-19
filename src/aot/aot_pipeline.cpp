#include "aot_pipeline.hpp"
#include "../lexer/lexer.hpp"
#include "../parser/parser.hpp"
#include "../common/localization.hpp"
#include "../common/platform.h"
#include "../lsp/document_index.hpp"
#include "llvm_backend.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#if !PLATFORM_WINDOWS
#include <csignal>   // SIGINT
#include <sys/wait.h> // WIFSIGNALED / WTERMSIG on system() status
#endif
#include <chrono>
#include <string>

#include <llvm-c/Core.h>
#include <llvm-c/Target.h>
#include <llvm-c/TargetMachine.h>

#if PLATFORM_MACOS
  #include <mach-o/dyld.h>
#endif

// Optional per-phase wallclock breakdown. Enable with TULPAR_AOT_TIME=1.
static int aot_timing_enabled() {
  static int cached = -1;
  if (cached < 0) {
    const char *e = getenv("TULPAR_AOT_TIME");
    cached = (e && *e && *e != '0') ? 1 : 0;
  }
  return cached;
}

// Per-phase `[AOT] …` progress chatter. Off by default so `tulpar build`
// prints only the final "Successfully created" confirmation (and any
// errors); set TULPAR_AOT_VERBOSE=1 to see the parse/codegen/link steps.
// The silent run path (`tulpar foo.tpr`) builds its own quiet backend and
// never reaches these prints.
static int aot_verbose_enabled() {
  static int cached = -1;
  if (cached < 0) {
    const char *e = getenv("TULPAR_AOT_VERBOSE");
    cached = (e && *e && *e != '0') ? 1 : 0;
  }
  return cached;
}
#define AOT_PROGRESS(...) do { if (aot_verbose_enabled()) printf(__VA_ARGS__); } while (0)

struct AOTPhaseTimer {
  const char *name;
  std::chrono::steady_clock::time_point start;
  AOTPhaseTimer(const char *n) : name(n) {
    if (aot_timing_enabled()) start = std::chrono::steady_clock::now();
  }
  ~AOTPhaseTimer() {
    if (!aot_timing_enabled()) return;
    auto end = std::chrono::steady_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
    fprintf(stderr, "[AOT-TIME] %-12s %4lldms\n", name, (long long)ms);
  }
};

// Platform-specific link flags / temp paths.
//
// `AOT_LINK_LIB_FLAGS` only carries platform-fixed switches and the
// link line itself; library search paths (`-L`) are computed at
// runtime in build_link_search_dirs() so installed `tulpar.exe`
// finds `libtulpar_runtime.a` next to itself, while developer
// builds still pick it up from `./build-<platform>/`.
//
// Note on `--export-all-symbols` / `-rdynamic`:
// Tulpar's `call(name)` builtin uses dlsym/GetProcAddress to dispatch to
// user-defined functions by name (wings/router handlers, lib/test.tpr
// runners, ...). By default an executable does NOT export its own internal
// symbols, so without these flags the lookup silently fails on the actual
// function and the runtime hits its "Function not found" path. We pay the
// minor binary-size cost to make `call()` work uniformly across Windows
// (MinGW) and Linux/macOS.
//
// Note on `-lssl -lcrypto`:
// `libtulpar_runtime.a` ships `http_fetch.cpp.o` which references OpenSSL
// (`SSL_CTX_new`, `OPENSSL_init_ssl`, ...) when the driver was built with
// `TULPAR_HAS_TLS=1`. Static archives don't bake their own deps in, so the
// AOT-pipeline's hand-rolled clang line has to forward those flags itself
// or every user binary fails with "undefined reference to OPENSSL_init_ssl"
// at link time. Linux CI hit this for the entire examples/ suite once the
// runner started actually invoking `tulpar --aot`. The flag is gated on
// `TULPAR_HAS_TLS` so a TLS-disabled build (no OpenSSL on the host) still
// produces a working linker line.
#if defined(TULPAR_HAS_TLS)
  #if PLATFORM_WINDOWS
    // MSYS2's static libcrypto.a pulls in CertFindCertificateInStore,
    // CertCloseStore, CertOpenSystemStoreW (winstore_store provider) +
    // raw socket calls (getsockopt, WSA*) on top of the usual ws2_32
    // surface. Without -lcrypt32 the produced exe fails to link with
    // a parade of `undefined reference to __imp_CertFindCertificateInStore`.
    // ws2_32 is already in the Windows base flags below, so we don't
    // duplicate it here.
    #define AOT_TLS_LINK_FLAGS " -lssl -lcrypto -lcrypt32"
  #else
    #define AOT_TLS_LINK_FLAGS " -lssl -lcrypto"
  #endif
#else
  #define AOT_TLS_LINK_FLAGS ""
#endif

// Note on Windows static linking:
// User-produced AOT binaries pulled `libgcc_s_seh-1.dll`, `libstdc++-6.dll`,
// and `libwinpthread-1.dll` from MinGW out of the linker. None of these
// ship with stock Windows, so a binary built on a developer's machine and
// copied to a fresh Win10/11 install would fail at launch with
// STATUS_DLL_NOT_FOUND (0xC0000135) — defeating the "tulpar build foo.tpr
// produces a standalone exe" promise. `-static` switches the link mode
// for the whole AOT line (every -l<name> resolves to its `.a` form);
// `-static-libgcc -static-libstdc++` are belt-and-suspenders so the GCC
// support libs go in even if a downstream change reintroduces a -Bdynamic
// segment. ws2_32 / kernel32 etc. only have import-lib form, so `-static`
// happily pulls them from the same `.a` files MinGW always uses for them.
// Trade-off: roughly +3 MB per produced exe (from ~3 MB to ~5–6 MB).
// Worth it: the user can now zip/email a single .exe to any 64-bit
// Windows box and have it run.
#if PLATFORM_WINDOWS
  // Link order matters under MinGW's GNU ld: an archive only resolves
  // symbols requested by libraries that came BEFORE it on the command
  // line. libssl / libcrypto pull in WSA*, getsockopt, CertFind*, etc.,
  // so they must appear LEFT of `-lws2_32 -lcrypt32`. The pre-PR-#92
  // order had ws2_32 before libssl and produced a wall of
  // `undefined reference to __imp_WSAGetLastError` once OpenSSL was
  // available at build time on Windows.
  #define AOT_LINK_LIB_FLAGS \
      "-Wl,--export-all-symbols " \
      "-static -static-libgcc -static-libstdc++ " \
      "-ltulpar_runtime" AOT_TLS_LINK_FLAGS \
      " -lws2_32 -lwsock32"
  #define AOT_LINK_PIE_FLAG ""
  #define AOT_EXE_SUFFIX ".exe"
  #define AOT_TMP_RUN_BASE ".tulpar_run"
#else
  #define AOT_LINK_LIB_FLAGS \
      "-rdynamic " \
      "-ltulpar_runtime -lm -lpthread -ldl" AOT_TLS_LINK_FLAGS
  #define AOT_LINK_PIE_FLAG "-no-pie"
  #define AOT_EXE_SUFFIX ""
  #define AOT_TMP_RUN_BASE "/tmp/.tulpar_run"
#endif

// Resolve the directory holding the running tulpar binary. Used so
// AOT-compiled programs link against `libtulpar_runtime.a` shipped
// next to `tulpar.exe` (or `tulpar`) by the installer, rather than
// relying on a hard-coded `./build-windows/` style fallback.
//
// Empty string means "couldn't figure it out" — the caller still
// adds dev-tree fallbacks so a fresh-from-`build.sh` checkout works.
static std::string get_executable_dir() {
#if PLATFORM_WINDOWS
  char buf[MAX_PATH];
  DWORD len = GetModuleFileNameA(nullptr, buf, MAX_PATH);
  if (len == 0 || len >= MAX_PATH) return "";
  std::string p(buf, len);
  size_t slash = p.find_last_of("\\/");
  return (slash == std::string::npos) ? "" : p.substr(0, slash);
#elif PLATFORM_MACOS
  char buf[1024];
  uint32_t size = sizeof(buf);
  if (_NSGetExecutablePath(buf, &size) != 0) return "";
  std::string p(buf);
  size_t slash = p.find_last_of('/');
  return (slash == std::string::npos) ? "" : p.substr(0, slash);
#else
  char buf[1024];
  ssize_t len = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
  if (len <= 0) return "";
  buf[len] = '\0';
  std::string p(buf);
  size_t slash = p.find_last_of('/');
  return (slash == std::string::npos) ? "" : p.substr(0, slash);
#endif
}

// Build the `-L<dir>` switches passed to clang++ at link time.
//
// Order matters: clang searches the first path that contains the
// requested library first, so the installer location should win
// over the dev-tree fallbacks if both happen to be present.
static std::string build_link_search_dirs() {
  std::string out;
  auto add = [&](const std::string &dir) {
    if (dir.empty()) return;
    out += "-L\"";
    out += dir;
    out += "\" ";
  };

  std::string exe_dir = get_executable_dir();
  if (!exe_dir.empty()) {
    add(exe_dir);          // installer drops libtulpar_runtime.a here
    add(exe_dir + "/lib"); // package-manager-style /lib subdir variant
  }

  // Dev-tree fallbacks — running `tulpar` straight out of the repo
  // root after `./build.sh` should keep working without TULPAR_HOME.
#if PLATFORM_WINDOWS
  add("./build-windows");
  add("./build-windows/Release");
#elif PLATFORM_MACOS
  add("./build-macos");
#else
  add("./build-linux");
#endif
  add("./build");

  // Last resort override — operators with custom layouts can set
  // TULPAR_RUNTIME_DIR=/some/path to point at the runtime archive
  // without recompiling.
  if (const char *env = getenv("TULPAR_RUNTIME_DIR"); env && *env) {
    add(env);
  }

  return out;
}

// Optional extra flags spliced into the final clang++ AOT link command. Set
// TULPAR_AOT_LINK_FLAGS to forward switches to the link step — e.g.
// "-fsanitize=address" to leak/UB-check the AOT'd binary against an ASan-built
// libtulpar_runtime.a. Returns a leading-space-prefixed string (or empty).
static std::string aot_extra_link_flags() {
  const char *e = getenv("TULPAR_AOT_LINK_FLAGS");
  if (!e || !*e) return "";
  return std::string(" ") + e;
}

// --- Web hedefi (wasm32-unknown-emscripten) ---------------------------------
// `tulpar build --target=web` main.cpp'den bu bayrağı kurar. Codegen'e
// backend->target_web olarak taşınır (VMValue ABI'sini sret+byval'a çevirir
// ve emit'te wasm32 triple'ı seçer); link adımı clang++ yerine em++ ile
// wasm/dist arşivlerine (build_tame_web.sh çıktıları) yapılır.
static int g_target_web = 0;

void aot_set_target_web(int enable) {
  g_target_web = enable ? 1 : 0;
  // Backend'e de kur: declare_runtime_functions llvm_backend_create'in
  // İÇİNDE koşar, tip şekilleri (sret vs SysV) o anda belirlenir.
  llvm_backend_set_target_web(g_target_web);
}

// em++'ın -L arama yolları: dev-tree wasm/dist + tulpar exe'sinin yanı +
// TULPAR_WEB_LIB_DIR ortam değişkeni.
static std::string build_web_link_search_dirs() {
  std::string out;
  auto add = [&](const std::string &dir) {
    if (dir.empty()) return;
    out += "-L\"";
    out += dir;
    out += "\" ";
  };
  std::string exe_dir = get_executable_dir();
  if (!exe_dir.empty()) {
    add(exe_dir + "/wasm/dist"); // kurulum yanına kopyalanmış layout
    add(exe_dir + "/lib");
  }
  add("./wasm/dist"); // dev-tree (repo kökünden çalıştırma)
  if (const char *env = getenv("TULPAR_WEB_LIB_DIR"); env && *env) add(env);
  return out;
}

// Web hedefinin HTML kabuğu. em++'a .html ürettirmiyoruz: emcc'nin HTML
// çıktısı html-minifier-terser (npm) ister ve vendored SDK'larda yoktur;
// kendi kabuğumuz hem bağımlılıksız hem de oyuna uygun (koyu, ortalanmış
// canvas). Emscripten GLFW canvas'ı Module.canvas'tan bulur.
static void write_web_shell_html(const char *exe_filename) {
  char html_path[512];
  snprintf(html_path, sizeof(html_path), "%s.html", exe_filename);
  const char *base = strrchr(exe_filename, '/');
#if PLATFORM_WINDOWS
  const char *base2 = strrchr(exe_filename, '\\');
  if (base2 && (!base || base2 > base)) base = base2;
#endif
  base = base ? base + 1 : exe_filename;
  FILE *f = fopen(html_path, "wb");
  if (!f) return;
  fprintf(f,
          "<!doctype html>\n<html lang=\"tr\">\n<head>\n"
          "<meta charset=\"utf-8\">\n"
          "<meta name=\"viewport\" content=\"width=device-width,"
          "initial-scale=1\">\n"
          "<title>%s — Tulpar Tame</title>\n"
          "<style>html,body{margin:0;height:100%%;background:#14141e;"
          "display:flex;align-items:center;justify-content:center;"
          "overflow:hidden}canvas{max-width:100%%;max-height:100%%;"
          "outline:none}#err{position:fixed;left:0;right:0;bottom:0;"
          "max-height:45%%;overflow:auto;background:#300;color:#faa;"
          "font:12px monospace;padding:8px;white-space:pre-wrap;"
          "display:none;z-index:9}"
          // --- Dokunmatik kontroller (yalniz dokunmatik cihazlarda gorunur) ---
          "#touch{display:none;position:fixed;left:0;right:0;bottom:0;z-index:8;"
          "justify-content:space-between;align-items:flex-end;padding:14px;"
          "pointer-events:none;-webkit-user-select:none;user-select:none}"
          "#touch>div{pointer-events:auto}"
          "#touch button{width:58px;height:58px;margin:3px;border-radius:12px;"
          "border:1px solid #3a3a5a;background:rgba(40,40,64,.62);color:#ffd45e;"
          "font:700 22px system-ui;touch-action:none;-webkit-tap-highlight-color:transparent}"
          "#touch button.act{background:rgba(255,212,94,.55);color:#111}"
          "#touch .row{display:flex;justify-content:center}"
          "#touch .big{width:74px;height:74px;font-size:20px}"
          "#touch .act-group{display:flex;align-items:center}"
          "@media (pointer:coarse){#touch{display:flex}}</style>\n"
          "</head>\n<body>\n"
          "<canvas id=\"canvas\" oncontextmenu=\"event.preventDefault()\" "
          "tabindex=\"-1\"></canvas>\n"
          // Dokunmatik gamepad: yon pad'i (sol) + aksiyon/R (sag). Butonlar
          // sentetik KeyboardEvent uretir (dogrulandi: raylib/emscripten bunlari
          // okuyor); masaustunde @media pointer:coarse ile gizli.
          "<div id=\"touch\">"
          "<div class=\"pad\">"
          "<div class=\"row\"><button id=\"tU\">&#9650;</button></div>"
          "<div class=\"row\"><button id=\"tL\">&#9664;</button>"
          "<button id=\"tD\">&#9660;</button>"
          "<button id=\"tR\">&#9654;</button></div></div>"
          "<div class=\"act-group\"><button id=\"tS\">R</button>"
          "<button id=\"tA\" class=\"big\">&#9679;</button></div>"
          "</div>\n"
          "<pre id=\"err\"></pre>\n"
          "<script>\n"
          "// Hata/stderr'i sayfada goster — konsol acmadan teshis icin.\n"
          "var errBox=document.getElementById('err');\n"
          "function showErr(m){errBox.style.display='block';"
          "errBox.textContent+=m+'\\n';}\n"
          "window.onerror=function(m,s,l,c,e)"
          "{showErr('[onerror] '+m+' @'+s+':'+l);};\n"
          "window.addEventListener('unhandledrejection',function(ev)"
          "{showErr('[promise] '+ev.reason);});\n"
          "var Module={canvas:document.getElementById('canvas'),"
          "printErr:function(t){console.error(t);showErr(t);},"
          "onAbort:function(w){showErr('[abort] '+w);}};\n"
          "</script>\n"
          "<script src=\"%s.js\"></script>\n"
          // Dokunmatik butonlari klavye olaylarina bagla (pointerdown->keydown,
          // birak->keyup). Tutulan yon (key_down) ve tek dokunus (key_pressed)
          // ikisi de calisir. Canvas + document + window'a gonderilir.
          "<script>\n"
          "(function(){var C=document.getElementById('canvas');"
          "function K(code,kc){return{code:code,"
          "key:code.indexOf('Arrow')===0?code:(code==='Space'?' ':'r'),"
          "keyCode:kc,which:kc,bubbles:true,cancelable:true};}"
          "function fire(t,k){var e=new KeyboardEvent(t,k);"
          "(C||document).dispatchEvent(e);document.dispatchEvent(e);"
          "window.dispatchEvent(e);}"
          "function bind(id,code,kc){var el=document.getElementById(id);"
          "if(!el)return;var k=K(code,kc);"
          "var d=function(e){e.preventDefault();fire('keydown',k);"
          "el.classList.add('act');};"
          "var u=function(e){e.preventDefault();fire('keyup',k);"
          "el.classList.remove('act');};"
          "el.addEventListener('pointerdown',d);"
          "el.addEventListener('pointerup',u);"
          "el.addEventListener('pointerleave',u);"
          "el.addEventListener('pointercancel',u);}"
          "bind('tL','ArrowLeft',37);bind('tR','ArrowRight',39);"
          "bind('tU','ArrowUp',38);bind('tD','ArrowDown',40);"
          "bind('tA','Space',32);bind('tS','KeyR',82);})();\n"
          "</script>\n"
          "</body>\n</html>\n",
          base, base);
  fclose(f);
}

// tame (2D oyun kütüphanesi) link bayrakları — yalnız program "tame" import
// ettiğinde (veya doğrudan bir tm_* builtin çağırdığında) eklenir; sıradan
// binary'ler GL/pencere bağımlılığı almaz. libtulpar_tame.a = vendored raylib
// + aot_tm_* binding'leri (bkz. CMakeLists "Tame" bölümü).
//
// Sıralama: tame, AOT_LINK_LIB_FLAGS'ten (yani -ltulpar_runtime'dan) ÖNCE
// gelmeli — GNU ld arşivleri soldan sağa çözer; kullanıcı objesinin aot_tm_*
// referanslarını tame karşılar, tame'in vm_make_* referanslarını sağındaki
// libtulpar_runtime.a karşılar.
//
// Linux'ta -lX11/-lGL gerekmez: raylib'in GLFW'si X11 kütüphanelerini ve GL'i
// çalışma zamanında dlopen'lar (glad + GLFW modül yükleyicisi); -ldl zaten
// temel bayraklarda var.
static const char *tame_link_flags(int uses_tame) {
  if (!uses_tame) return "";
#if PLATFORM_WINDOWS
  return " -ltulpar_tame -lopengl32 -lgdi32 -lwinmm";
#elif PLATFORM_MACOS
  return " -ltulpar_tame -framework Cocoa -framework IOKit"
         " -framework CoreVideo -framework OpenGL"
         " -framework CoreAudio -framework AudioToolbox";
#else
  return " -ltulpar_tame";
#endif
}

// Parse source code to AST. Caller-provided `source_filename` is
// optional and only used by parse-time diagnostics for the file path
// in `--> path:line` headers.
static ASTNode_C *parse_source(const char *source,
                               const char *source_filename = nullptr) {
  // Hand source + filename to the parser for Rust-style diagnostics.
  parser_set_diagnostic_context(source, source_filename);
  Lexer *lexer = lexer_create(source);

  int token_capacity = 100;
  int token_count = 0;
  Token **tokens = static_cast<Token **>(malloc(sizeof(Token *) * token_capacity));

  Token *token;
  while ((token = lexer_next_token(lexer))->type() != TOKEN_EOF) {
    if (token_count >= token_capacity) {
      token_capacity *= 2;
      tokens = (Token **)realloc(tokens, sizeof(Token *) * token_capacity);
    }
    tokens[token_count++] = token;
  }
  tokens[token_count++] = token; // EOF

  lexer_free(lexer);

  Parser_C *parser = parser_create(tokens, token_count);
  ASTNode_C *ast = parser_parse(parser);

  // Note: tokens are still referenced by AST, careful with cleanup
  parser_free(parser);

  // Free tokens after parser (AST copies what it needs)
  for (int i = 0; i < token_count; i++) {
    token_free(tokens[i]);
  }
  free(tokens);

  return ast;
}

// Compile Tulpar source to object file.
//
// The `_with_filename` variant is the canonical entry point — it pipes the
// source filename through to LLVMBackend so codegen diagnostics can render
// `--> path/file.tpr:42` headers. The plain `aot_compile` is kept as a
// thin wrapper that defaults the filename to NULL (legacy behaviour: shows
// `(stdin)` in diagnostics).
AOTResult aot_compile(const char *source, const char *output_name) {
  return aot_compile_with_filename(source, output_name, nullptr);
}

AOTResult aot_compile_with_filename(const char *source,
                                    const char *output_name,
                                    const char *source_filename) {
  return aot_compile_with_filename_debug(source, output_name,
                                         source_filename, /*emit_debug=*/0);
}

AOTResult aot_compile_with_filename_debug(const char *source,
                                          const char *output_name,
                                          const char *source_filename,
                                          int emit_debug_info) {
  ASTNode_C *ast;
  {
    AOTPhaseTimer t("parse");
    AOT_PROGRESS("[AOT] Parsing source...\n");
    ast = parse_source(source, source_filename);
  }
  if (!ast) {
    fprintf(stderr, "%s", tulpar::i18n::tr_for_en("[AOT] Error: Failed to parse source\n"));
    return AOT_ERROR_PARSE;
  }

  LLVMBackend *backend;
  {
    AOTPhaseTimer t("backend-init");
    AOT_PROGRESS("[AOT] Creating LLVM backend...\n");
    backend = llvm_backend_create("tulpar_aot_module");
  }
  if (!backend) {
    fprintf(stderr, "%s", tulpar::i18n::tr_for_en("[AOT] Error: Failed to create LLVM backend\n"));
    ast_node_free(ast);
    return AOT_ERROR_CODEGEN;
  }

  // Hand the source text to the backend so codegen errors can render a
  // Rust-style line excerpt + caret. (Borrow only — caller owns the buffer.)
  backend->source_text = source;
  backend->source_filename = source_filename;
  backend->emit_debug_info = emit_debug_info;
  // Web hedefi declare_runtime_functions'tan ÖNCE set edilmeli — VMValue
  // çağrı tiplerinin şekli (sret+byval vs SysV) buna bağlı.
  backend->target_web = g_target_web;

  // Plan 07 PR 2: open the debug-info graph before codegen runs so
  // subsequent passes can hang `DISubprogram` / `!dbg` metadata off
  // the compile unit set up here. No-op when --debug was not passed.
  llvm_backend_init_debug_info(backend, source_filename);

  {
    AOTPhaseTimer t("codegen");
    AOT_PROGRESS("[AOT] Generating LLVM IR...\n");
    llvm_backend_compile(backend, ast);
  }

  if (backend->had_error) {
    fprintf(stderr, "%s", tulpar::i18n::tr_for_en(
        "[AOT] Error: Codegen reported errors above; aborting build.\n"));
    llvm_backend_destroy(backend);
    ast_node_free(ast);
    return AOT_ERROR_CODEGEN;
  }

  // Debug: dump raw front-end IR BEFORE optimization. Enable with
  // TULPAR_AOT_EMIT_LL_PRE=1 to inspect codegen output pre-opt.
  {
    const char *e = getenv("TULPAR_AOT_EMIT_LL_PRE");
    if (e && *e && *e != '0') {
      char ir_file[256];
      snprintf(ir_file, sizeof(ir_file), "%s.pre.ll", output_name);
      llvm_backend_emit_ir_file(backend, ir_file);
    }
  }

  {
    AOTPhaseTimer t("optimize");
    AOT_PROGRESS("[AOT] Optimizing...\n");
    llvm_backend_optimize(backend);
  }

  // Plan 07 PR 2: close the debug-info graph before any IR consumer
  // (emit_ir_file / emit_object) walks it. Finalize must come AFTER
  // optimisation so any optimiser pass that touches `!dbg` metadata
  // has run; doing it before would leave dangling references that
  // the verifier rejects. No-op when --debug was not passed.
  llvm_backend_finalize_debug_info(backend);

  // Generate output filename
  //
  // Web hedefinde çıktı adı bir taban addır: `.html` (kabuk), `.js` ve `.wasm`
  // hep buna EKLENİR. Kullanıcı doğal olarak `-o game.html` yazdığında bu
  // `game.html.html` + `game.html.js` üretiyordu. Sondaki `.html`'i bir kez
  // soy: `game` ve `game.html` artık aynı (doğru) çıktıyı verir.
  std::string web_base;
  if (g_target_web && output_name) {
    web_base = output_name;
    const std::string dot_html = ".html";
    if (web_base.size() > dot_html.size() &&
        web_base.compare(web_base.size() - dot_html.size(), dot_html.size(),
                         dot_html) == 0) {
      web_base.erase(web_base.size() - dot_html.size());
      output_name = web_base.c_str();
    }
  }

  char obj_filename[256];
  char exe_filename[256];
  snprintf(obj_filename, sizeof(obj_filename), "%s.o", output_name);
  snprintf(exe_filename, sizeof(exe_filename), "%s", output_name);

  // Emit IR (Debug). Off by default — costs measurable I/O on bigger inputs
  // and almost nobody reads the .ll. Enable with TULPAR_AOT_EMIT_LL=1.
  {
    const char *e = getenv("TULPAR_AOT_EMIT_LL");
    if (e && *e && *e != '0') {
      AOTPhaseTimer t("emit-ll");
      char ir_file[256];
      snprintf(ir_file, sizeof(ir_file), "%s.ll", output_name);
      llvm_backend_emit_ir_file(backend, ir_file);
    }
  }

  {
    AOTPhaseTimer t("emit-obj");
    AOT_PROGRESS("[AOT] Emitting object file: %s\n", obj_filename);
    if (llvm_backend_emit_object(backend, obj_filename) != 0) {
      fprintf(stderr, "%s", tulpar::i18n::tr_for_en("[AOT] Error: Failed to emit object file\n"));
      llvm_backend_destroy(backend);
      ast_node_free(ast);
      return AOT_ERROR_EMIT;
    }
  }

  // Link using clang++ (need C++ runtime for tulpar_runtime).
  // `-g` is forwarded to clang when --debug was requested so debug
  // sections emitted in the object file survive linking into the
  // final binary. Today the object has no `!dbg` metadata yet
  // (Plan 07 PR 2 wires up LLVMDIBuilder), so `-g` is effectively
  // a no-op until that lands — but plumbing the switch through now
  // keeps the CLI surface stable across the PR series.
  AOT_PROGRESS("[AOT] Linking executable: %s\n", exe_filename);
  std::string search_dirs = build_link_search_dirs();
  std::string extra_flags = aot_extra_link_flags();
  const char *debug_flag = emit_debug_info ? "-g " : "";
  char link_cmd[2048];
  if (g_target_web) {
    // Web hedefi: em++ (Emscripten) linkler → <out>.html + .js + .wasm.
    // - USE_GLFW=3: raylib PLATFORM_WEB, Emscripten'in GLFW JS
    //   implementasyonunu kullanır (rglfw.c web arşivinde yok).
    // - ASYNCIFY: Tulpar oyunları bloklu `while (running())` döngüsü yazar;
    //   raylib'in EndDrawing'i web'de emscripten_sleep çağırır — ASYNCIFY
    //   bunu tarayıcının event-loop'una çevirir.
    // - Arşiv sırası native ile aynı: tame, runtime'dan önce.
    // - TULPAR_WEB_ASSETS=<dizin> → --preload-file (oyun varlıkları sanal
    //   dosya sistemine aynı yolda gömülür).
    std::string web_dirs = build_web_link_search_dirs();
    std::string preload;
    if (const char *assets = getenv("TULPAR_WEB_ASSETS"); assets && *assets) {
      preload = std::string(" --preload-file ") + assets;
    }
    // Çıktı .js (+ .wasm): emcc'nin .html üreticisi npm bağımlılığı ister;
    // HTML kabuğunu link sonrası write_web_shell_html kendimiz yazarız.
    // ASYNCIFY_STACK_SIZE: unwind sırasında canlı wasm local'leri bu
    // tampona yazılır; Tulpar main'i entry-hoisted VMValue alloca'larıyla
    // dolu olduğundan varsayılan 4KB kolayca taşar ("Aborted(Asyncify
    // stack overflow)") — 128KB güvenli.
    // EXPORTED_RUNTIME_METHODS=HEAPF32: raylib'in ses arka ucu (miniaudio,
    // raudio.c) ScriptProcessorNode geri-çağrısında `Module.HEAPF32.buffer`
    // okur. Emscripten (>=3.x, burada 5.0) HEAP view'lerini artık Module'e
    // OTOMATİK bağlamaz → `Module.HEAPF32` undefined → ses açan her oyun
    // (load_sound/load_music) her audio-frame'de "Cannot read properties of
    // undefined (reading 'buffer')" ile çöker. HEAPF32'yi Module'e export
    // etmek (büyümede otomatik yeniden atanır) bunu giderir; sessiz oyunlar
    // için de zararsız.
    snprintf(
        link_cmd, sizeof(link_cmd),
        "em++ %s -o %s.js -O2 -sUSE_GLFW=3 -sASYNCIFY "
        "-sASYNCIFY_STACK_SIZE=131072 "
        "-sALLOW_MEMORY_GROWTH -sSTACK_SIZE=2097152 "
        "-sEXPORTED_RUNTIME_METHODS=HEAPF32 "
        "%s-ltulpar_tame_web -ltulpar_runtime_web%s%s 2>&1",
        obj_filename, exe_filename, web_dirs.c_str(), preload.c_str(),
        extra_flags.c_str());
  } else {
  snprintf(
      link_cmd, sizeof(link_cmd),
      "clang++ %s%s -o %s%s %s %s%s%s%s 2>&1",
      debug_flag, obj_filename, exe_filename, AOT_EXE_SUFFIX,
      AOT_LINK_PIE_FLAG, search_dirs.c_str(),
      tame_link_flags(backend->uses_tame), " " AOT_LINK_LIB_FLAGS,
      extra_flags.c_str());
  }

  int link_result;
  {
    AOTPhaseTimer t("link");
    link_result = system(link_cmd);
  }
  if (link_result != 0) {
    fprintf(stderr, tulpar::i18n::tr_for_en(
            "[AOT] Error: Linking failed (code %d). Check clang installation and libraries.\n"),
            link_result);
    if (g_target_web) {
      fprintf(stderr, "%s\n",
              tulpar::i18n::tr_en(
                  "[AOT] Web hedefi icin em++ gerekir: 'source "
                  "wasm/emsdk/emsdk_env.sh' calistirin ve wasm/dist "
                  "arsivlerinin (wasm/build_tame_web.sh) mevcut olduguna "
                  "emin olun.",
                  "[AOT] The web target needs em++: run 'source "
                  "wasm/emsdk/emsdk_env.sh' and make sure the wasm/dist "
                  "archives (wasm/build_tame_web.sh) exist."));
    }
    llvm_backend_destroy(backend);
    ast_node_free(ast);
    return AOT_ERROR_LINK;
  } else {
    if (g_target_web) {
      write_web_shell_html(exe_filename);
      printf("[AOT] Successfully created: %s.html (+ .js, .wasm)\n",
             exe_filename);
    } else {
      printf("[AOT] Successfully created: %s\n", exe_filename);
    }
  }

  llvm_backend_destroy(backend);
  ast_node_free(ast);

  return AOT_OK;
}

// Compile and immediately run (for development/testing)
AOTResult aot_compile_and_run(const char *source) {
  AOTResult result = aot_compile(source, "tulpar_temp");
  if (result != AOT_OK) {
    return result;
  }

  // Try to execute
  AOT_PROGRESS("[AOT] Executing generated binary...\n");
#if PLATFORM_WINDOWS
  // cmd.exe does not auto-search the current directory unless an explicit
  // path is given, so prefix with .\ to ensure the binary is found.
  int run_status = system(".\\tulpar_temp.exe");
#else
  int run_status = system("./tulpar_temp");
#endif
  (void)run_status;

  return AOT_OK;
}

// Silent compile to native binary (no output, temp files)
static AOTResult aot_compile_silent(const char *source,
                                    const char *output_name,
                                    const char *source_filename) {
  ASTNode_C *ast = parse_source(source, source_filename);
  if (!ast) {
    return AOT_ERROR_PARSE;
  }

  LLVMBackend *backend = llvm_backend_create("tulpar_aot_module");
  if (!backend) {
    ast_node_free(ast);
    return AOT_ERROR_CODEGEN;
  }
  backend->quiet = 1; // Suppress [AOT] messages
  backend->source_text = source;
  backend->source_filename = source_filename;

  llvm_backend_compile(backend, ast);
  if (backend->had_error) {
    llvm_backend_destroy(backend);
    ast_node_free(ast);
    return AOT_ERROR_CODEGEN;
  }
  llvm_backend_optimize(backend);

  char obj_filename[256];
  char exe_filename[256];
  snprintf(obj_filename, sizeof(obj_filename), "%s.o", output_name);
  snprintf(exe_filename, sizeof(exe_filename), "%s", output_name);

  if (llvm_backend_emit_object(backend, obj_filename) != 0) {
    llvm_backend_destroy(backend);
    ast_node_free(ast);
    return AOT_ERROR_EMIT;
  }

  // Link silently (suppress output)
  std::string silent_search_dirs = build_link_search_dirs();
  std::string silent_extra_flags = aot_extra_link_flags();
  char link_cmd[2048];
#if PLATFORM_WINDOWS
  snprintf(
      link_cmd, sizeof(link_cmd),
      "clang++ %s -o %s%s %s %s%s%s%s 2>NUL",
      obj_filename, exe_filename, AOT_EXE_SUFFIX,
      AOT_LINK_PIE_FLAG, silent_search_dirs.c_str(),
      tame_link_flags(backend->uses_tame), " " AOT_LINK_LIB_FLAGS,
      silent_extra_flags.c_str());
#else
  snprintf(
      link_cmd, sizeof(link_cmd),
      "clang++ %s -o %s%s %s %s%s%s%s 2>/dev/null",
      obj_filename, exe_filename, AOT_EXE_SUFFIX,
      AOT_LINK_PIE_FLAG, silent_search_dirs.c_str(),
      tame_link_flags(backend->uses_tame), " " AOT_LINK_LIB_FLAGS,
      silent_extra_flags.c_str());
#endif

  int link_result = system(link_cmd);

  // Cleanup object file
  remove(obj_filename);

  llvm_backend_destroy(backend);
  ast_node_free(ast);

  if (link_result != 0) {
    return AOT_ERROR_LINK;
  }

  return AOT_OK;
}

// Silent compile and run - used as default execution mode.
// Compiles to temp binary, runs it, cleans up. No [AOT] output.
AOTResult aot_compile_and_run_silent(const char *source) {
  return aot_compile_and_run_silent_with_filename(source, nullptr);
}

AOTResult aot_compile_and_run_silent_with_filename(const char *source,
                                                   const char *source_filename) {
#if PLATFORM_WINDOWS
  const char *base = "tulpar_run_tmp";
  AOTResult result = aot_compile_silent(source, base, source_filename);
  if (result != AOT_OK) {
    return result;
  }
  // cmd.exe does not auto-search the current directory unless an explicit
  // path is given, so prefix with .\ to ensure the binary is found.
  int run_result = system(".\\tulpar_run_tmp.exe");
  remove("tulpar_run_tmp.exe");
  remove("tulpar_run_tmp.ll");
  remove("tulpar_run_tmp.o");
#else
  const char *base = "/tmp/.tulpar_run";
  AOTResult result = aot_compile_silent(source, base, source_filename);
  if (result != AOT_OK) {
    return result;
  }
  int run_result = system("/tmp/.tulpar_run");
  remove("/tmp/.tulpar_run");
  remove("/tmp/.tulpar_run.ll");
#endif

  // The compile + link already succeeded above (we returned early otherwise),
  // so a non-zero status here means the PROGRAM ran and exited non-zero — not
  // a toolchain failure. Report it as AOT_RAN_NONZERO so the driver propagates
  // the failure without the misleading "compile/link failed" banner.
#if !PLATFORM_WINDOWS
  // Ctrl+C (SIGINT) on a long-running server is a normal stop, not a failure.
  if (WIFSIGNALED(run_result) && WTERMSIG(run_result) == SIGINT) {
    return AOT_OK;
  }
#endif
  return (run_result == 0) ? AOT_OK : AOT_RAN_NONZERO;
}

// Check-only pipeline: parse + codegen pass, no optimisation, no object
// emission, no link. Used by the LSP server (`tulpar --lsp`) to gather
// structured diagnostics on every keystroke. Cost is dominated by
// llvm_backend_compile, but staying off the linker keeps end-to-end
// latency in the ~100ms range on typical files.
AOTResult aot_check_only(const char *source, const char *source_filename) {
  return aot_check_and_index(source, source_filename, nullptr);
}

AOTResult aot_check_and_index(const char *source, const char *source_filename,
                              void *out_index) {
  ASTNode_C *ast = parse_source(source, source_filename);
  if (!ast) {
    return AOT_ERROR_PARSE;
  }

  LLVMBackend *backend = llvm_backend_create("tulpar_lsp_check");
  if (!backend) {
    ast_node_free(ast);
    return AOT_ERROR_CODEGEN;
  }
  backend->quiet = 1;
  backend->source_text = source;
  backend->source_filename = source_filename;

  llvm_backend_compile(backend, ast);
  AOTResult result = backend->had_error ? AOT_ERROR_CODEGEN : AOT_OK;

  // Build the symbol index *after* codegen but before ast_node_free —
  // codegen might mutate AST node fields (e.g. importing module bodies),
  // so we capture the post-import state for hover/completion.
  if (out_index) {
    auto *idx = static_cast<tulpar::DocumentIndex *>(out_index);
    tulpar::document_index_build(ast, source, *idx);
  }

  llvm_backend_destroy(backend);
  ast_node_free(ast);
  return result;
}
