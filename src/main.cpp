
#include <locale.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sys/stat.h>
#ifdef _WIN32
// WIN32_LEAN_AND_MEAN keeps windows.h from pulling in the legacy winsock.h,
// which would conflict with the winsock2.h that platform_sockets.h includes
// further down through the parser/vm headers.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#include "lexer/lexer.hpp"
#include "parser/parser.hpp"
#include "common/localization.hpp"
#include "common/platform_sockets.h"
#include "vm/vm.hpp" // shared runtime types (VMValue, Obj*) — NOT the VM engine
#ifdef TULPAR_AOT_ENABLED
#include "aot/aot_pipeline.hpp"
#endif
#include "lsp/lsp_server.hpp"
#include "fmt/formatter.hpp"
#include "pkg/manifest.hpp"
#include "pkg/pkg_cli.hpp"
#include "cli/debug_cmd.hpp"
#include "cli/doc_cmd.hpp"
#include "cli/typecheck_cmd.hpp"
#include "cli/update_cmd.hpp"
#include "typeinfer/typeinfer_warn.hpp"
#include "common/version.hpp"
#include <ctime>

// Dosyadan kaynak kodu oku
char *read_file(const char *filename) {
  FILE *file = fopen(filename, "rb");
  if (!file) {
    printf("%s '%s'!\n",
           tulpar::i18n::tr_en("Hata: Dosya acilamadi",
                               "Error: Could not open file"),
           filename);
    return nullptr;
  }

  // Dosya boyutunu bul
  fseek(file, 0, SEEK_END);
  long size = ftell(file);
  fseek(file, 0, SEEK_SET);

  // Bellek ayır ve oku
  char *buffer = static_cast<char *>(malloc(size + 1));
  size_t bytes_read = fread(buffer, 1, size, file);
  (void)bytes_read;
  buffer[size] = '\0';

  fclose(file);
  return buffer;
}

// Print the top-level command reference. Shared between explicit
// `tulpar --help` / `-h` / `help` / `?` invocations and the no-args
// fallback so both stay in sync.
static void print_help() {
  std::printf("TulparLang %s (LLVM AOT Backend)\n\n", tulpar::kVersion);

  std::printf("%s\n", tulpar::i18n::tr_en("Kullanim:", "Usage:"));
  std::printf("  tulpar <source.tpr>              %s\n",
              tulpar::i18n::tr_en("- Programi calistir (AOT, native hiz)",
                                  "- Run program (AOT, native speed)"));
  std::printf("  tulpar build <source.tpr> [out]  %s\n",
              tulpar::i18n::tr_en("- Bagimsiz native ikili olustur",
                                  "- Build standalone native binary"));

  std::printf("\n%s\n",
              tulpar::i18n::tr_en("Aletler:", "Tools:"));
  std::printf("  tulpar fmt <source.tpr>          %s\n",
              tulpar::i18n::tr_en("- Kaynak kodu formatla",
                                  "- Format source code"));
  std::printf("  tulpar typecheck <source.tpr>    %s\n",
              tulpar::i18n::tr_en("- Statik tip kontrolu (deneysel)",
                                  "- Static type check (experimental)"));
  std::printf("  tulpar doc <source.tpr>          %s\n",
              tulpar::i18n::tr_en(
                  "- Markdown referansi uret (fonksiyon + global'lerin "
                  "leading-comment docstring'leri)",
                  "- Emit a markdown reference from function + global "
                  "leading-comment docstrings"));
  std::printf("  --no-typecheck                   %s\n",
              tulpar::i18n::tr_en(
                  "- run/build oncesi tip uyarilarini kapat",
                  "- Disable pre-build [typecheck] warnings"));
  std::printf("  --strict                         %s\n",
              tulpar::i18n::tr_en(
                  "- [typecheck] uyarilarini hata olarak ele al "
                  "(tulpar.toml `strict = true` kalici hale getirir)",
                  "- Promote [typecheck] warnings to errors "
                  "(set `strict = true` in tulpar.toml to make permanent)"));
  std::printf("  tulpar pkg <komut>               %s\n",
              tulpar::i18n::tr_en("- Paket yoneticisi (init/install/...)",
                                  "- Package manager (init/install/...)"));
  std::printf("  tulpar --lsp                     %s\n",
              tulpar::i18n::tr_en("- LSP sunucusu (editor entegrasyonu)",
                                  "- LSP server (editor integration)"));
  std::printf("  tulpar debug <source.tpr>        %s\n",
              tulpar::i18n::tr_en(
                  "- DAP hata ayiklayici (deneysel — Plan 07)",
                  "- DAP debug adapter (experimental — Plan 07)"));

  std::printf("\n%s\n",
              tulpar::i18n::tr_en("Surum & guncelleme:",
                                  "Version & updates:"));
  std::printf("  tulpar version, --version, -v    %s\n",
              tulpar::i18n::tr_en("- Surum bilgisini yazdir",
                                  "- Print version"));
  std::printf("  tulpar update [--check]          %s\n",
              tulpar::i18n::tr_en("- Guncellemeleri kontrol et / yukle",
                                  "- Check for / install updates"));

  std::printf("\n%s\n",
              tulpar::i18n::tr_en("Yardim:", "Help:"));
  std::printf("  tulpar --help, -h, help, ?       %s\n",
              tulpar::i18n::tr_en("- Bu yardim metnini goster",
                                  "- Show this help"));

  std::printf("\n%s\n",
              tulpar::i18n::tr_en(
                  "Daha fazlasi: https://tulparlang.dev",
                  "More info: https://tulparlang.dev"));
}

int main(int argc, char **argv) {
  // Locale setup
  setlocale(LC_ALL, ".UTF8");

#ifdef _WIN32
  // Windows console defaults to OEM code page (CP437/CP850), which renders
  // our UTF-8 diagnostic strings as garbage like "ayr─▒┼şt─▒rma". Switch
  // both input and output to UTF-8 so Turkish characters print correctly.
  SetConsoleOutputCP(CP_UTF8);
  SetConsoleCP(CP_UTF8);
#endif

  // LSP mode short-circuits everything else: it owns stdin/stdout for
  // the JSON-RPC transport and must not be polluted by any startup
  // banner or REPL prompt. Check before any other flag dispatch.
  for (int i = 1; i < argc; i++) {
    if (std::strcmp(argv[i], "--lsp") == 0) {
      return tulpar::lsp_run_server();
    }
  }

  // `tulpar fmt <file>` runs the source formatter and exits. Keep it
  // before AOT dispatch — `fmt` shares no state with the compile
  // pipeline.
  if (argc >= 2 && std::strcmp(argv[1], "fmt") == 0) {
    return tulpar::fmt_cli_main(argc, argv);
  }

  // `tulpar pkg <subcommand>` runs the package manager and exits. Same
  // reasoning — no overlap with compile / run / build paths.
  if (argc >= 2 && std::strcmp(argv[1], "pkg") == 0) {
    return tulpar::pkg_cli_main(argc, argv);
  }

  // `tulpar typecheck <file>` runs the static type-inference pass and
  // reports issues. Build/run pipelines do not invoke it yet; surfacing
  // it as a tool so authors and CI can opt in while we shape the rules.
  if (argc >= 2 && std::strcmp(argv[1], "typecheck") == 0) {
    return tulpar::typecheck_cmd_main(argc, argv);
  }

  // `tulpar doc <file>` emits a markdown reference (functions +
  // top-level globals, with leading-comment docstrings) for the
  // file. Same content the LSP `hover` surfaces in the editor —
  // routed through `aot_check_and_index` so the two stay in lockstep.
  if (argc >= 2 && std::strcmp(argv[1], "doc") == 0) {
    return tulpar::doc_cmd_main(argc, argv);
  }

  // `tulpar version` / `tulpar --version` / `-v` — print and exit. Kept
  // before the source-file dispatch so a file literally named `version`
  // would not shadow it (vanishingly unlikely but trivial to guard).
  if (argc >= 2 && (std::strcmp(argv[1], "version") == 0 ||
                    std::strcmp(argv[1], "--version") == 0 ||
                    std::strcmp(argv[1], "-v") == 0)) {
    std::printf("TulparLang %s\n", tulpar::kVersion);
    return 0;
  }

  // `tulpar --help` / `-h` / `help` / `?` — print command reference and
  // exit. Same dispatch list users reach for after `tulpar` with no args
  // shows them the banner.
  if (argc >= 2 && (std::strcmp(argv[1], "--help") == 0 ||
                    std::strcmp(argv[1], "-h") == 0 ||
                    std::strcmp(argv[1], "help") == 0 ||
                    std::strcmp(argv[1], "?") == 0)) {
    print_help();
    return 0;
  }

  // `tulpar update [--check]` — query GitHub for latest release and
  // (optionally) re-run the install script to upgrade in place. Shells
  // out to PowerShell/curl for the HTTPS fetch so we don't link OpenSSL
  // just for the update path.
  if (argc >= 2 && std::strcmp(argv[1], "update") == 0) {
    return tulpar::update_cmd_main(argc, argv);
  }

  // `tulpar debug <file.tpr>` — Plan 07 Part B. Opens a Debug Adapter
  // Protocol server on stdio. Owns stdin/stdout (every diagnostic
  // line goes to stderr), so it must dispatch before any banner /
  // REPL output — same constraint `--lsp` carries.
  if (argc >= 2 && std::strcmp(argv[1], "debug") == 0) {
    return tulpar::debug_cmd_main(argc, argv);
  }

  // Flags
  int build_mode = 0;  // build / --build / --aot: save native binary
  int skip_typecheck = 0;  // --no-typecheck disables the pre-pass warnings
  // Plan 07 PR 1: `tulpar build --debug` (or `-g`) requests an AOT
  // build that keeps debug symbols. Today this just forwards `-g` to
  // clang at link time; the LLVMDIBuilder metadata that lets gdb /
  // lldb step through .tpr source lines lands in Plan 07 PR 2-3 (the
  // backend slot is already plumbed through so callers don't need
  // another signature break later).
  int emit_debug = 0;
  // --strict flips the typeinfer pre-pass from informational to
  // exit-blocking. Precedence (lowest to highest):
  //   1. Default: 0 (warnings only)
  //   2. tulpar.toml `strict = true` in CWD (project default)
  //   3. Env var TULPAR_STRICT — non-empty value, "0" disables, anything
  //      else enables. Lets CI override the project default explicitly.
  //   4. CLI --strict flag — always enables.
  // --no-typecheck still overrides everything (no warnings, no exit).
  int strict_typecheck = 0;
  {
    // Only surface a parse error if the file actually exists — silent
    // skip when there's no manifest at all (most scripts run without one).
    struct stat st;
    if (stat("tulpar.toml", &st) == 0) {
      tulpar::Manifest cwd_manifest;
      std::string manifest_err;
      if (tulpar::manifest_load("tulpar.toml", cwd_manifest, manifest_err)) {
        if (cwd_manifest.strict_typecheck) strict_typecheck = 1;
      } else {
        std::fprintf(stderr, "[manifest] tulpar.toml: %s (ignoring)\n",
                     manifest_err.c_str());
      }
    }
    const char *env = getenv("TULPAR_STRICT");
    if (env && *env) strict_typecheck = (strcmp(env, "0") != 0) ? 1 : 0;
  }
  int arg_offset = 1;  // index of first non-flag arg

  // Parse flags
  for (int i = 1; i < argc; i++) {

    if (strcmp(argv[i], "--vm") == 0 || strcmp(argv[i], "--run") == 0) {
      // AOT-only (see CLAUDE.md): the bytecode VM execution path is gone.
      // Accept the flag but ignore it so old scripts/CI don't hard-fail;
      // warn once so the habit fades.
      fprintf(stderr, "%s\n",
              tulpar::i18n::tr_en(
                  "Uyari: --vm/--run kaldirildi (Tulpar yalnizca AOT). "
                  "Yok sayiliyor; AOT ile calistiriliyor.",
                  "Warning: --vm/--run removed (Tulpar is AOT-only). "
                  "Ignoring; running via AOT."));
      arg_offset = i + 1;
    } else if (strcmp(argv[i], "--no-typecheck") == 0) {
      // Suppress the pre-pass typeinfer warnings without touching the
      // env-var override (TULPAR_NO_TYPECHECK=1). Doesn't shift arg_offset
      // on its own — the next non-flag arg still sets the file index.
      skip_typecheck = 1;
    } else if (strcmp(argv[i], "--strict") == 0) {
      // Promote `[typecheck]` warnings to exit-blocking errors. Format
      // stays the same; we just summarise and exit 1 when count > 0.
      strict_typecheck = 1;
    } else if (strcmp(argv[i], "--debug") == 0 ||
               strcmp(argv[i], "-g") == 0) {
      // `tulpar build --debug` opt-in. Doesn't shift arg_offset on its
      // own — the next non-flag arg still sets the file index.
      emit_debug = 1;
    } else if (strcmp(argv[i], "--aot") == 0 ||
               strcmp(argv[i], "--build") == 0 ||
               strcmp(argv[i], "build") == 0) {
      build_mode = 1;
      arg_offset = i;
      break;
    } else if (argv[i][0] != '-') {
      // First non-flag argument is the file
      arg_offset = i;
      break;
    }
  }

  // REPL / --vm removed: Tulpar is AOT-only (see CLAUDE.md "AOT-ONLY").
  // The bytecode VM and interactive REPL were retired on 2026-06-15; there is
  // no interpreter to host them. Surface a clear message instead of silently
  // doing something else.
  if (argc > 1 &&
      (strcmp(argv[1], "--repl") == 0 || strcmp(argv[1], "-i") == 0)) {
    fprintf(stderr, "%s\n",
            tulpar::i18n::tr_en(
                "REPL kaldirildi: Tulpar artik yalnizca AOT derler. "
                "Bir .tpr dosyasini dogrudan calistirin: tulpar script.tpr",
                "REPL removed: Tulpar is now AOT-only. Run a .tpr file "
                "directly: tulpar script.tpr"));
    return 2;
  }

#ifdef TULPAR_AOT_ENABLED
  // Build mode: save native binary (tulpar build file.tpr [output])
  if (build_mode) {
    if (arg_offset + 1 >= argc) {
      printf("Usage: tulpar build <source.tpr> [output_name]\n");
      return 1;
    }
    char *source = read_file(argv[arg_offset + 1]);
    if (!source) {
      return 1;
    }

    // Surface static type-inference findings as `[typecheck]` warnings
    // before the LLVM build. Informational by default — never blocks
    // the build. Suppressed by `--no-typecheck` or
    // `TULPAR_NO_TYPECHECK=1`. With `--strict` (or `TULPAR_STRICT=1`),
    // we keep the same `[typecheck]` format but treat any warning as
    // an exit-blocking error.
    if (!skip_typecheck) {
      int n = tulpar::typeinfer_emit_warnings(source, argv[arg_offset + 1]);
      if (strict_typecheck && n > 0) {
        fprintf(stderr,
                "[typecheck] %d %s (strict mode); aborting build.\n",
                n, n == 1 ? "error" : "errors");
        free(source);
        return 1;
      }
    }

    // Output name strategy:
    // 1. Explicit argument
    // 2. Derive from input filename (e.g., hello.tpr -> hello)
    // 3. Fallback to "a.out"

    char default_output_name[256];
    const char *output_name;

    if (arg_offset + 2 < argc) {
      // User provided explicit output name
      output_name = argv[arg_offset + 2];
    } else {
      // Derive from input file
      const char *input_path = argv[arg_offset + 1];
      const char *base_name = strrchr(input_path, '/');
      if (!base_name)
        base_name = input_path;
      else
        base_name++; // Skip separator

      // Copy up to extension
      const char *ext = strrchr(base_name, '.');
      size_t len = ext ? (size_t)(ext - base_name) : strlen(base_name);
      if (len >= sizeof(default_output_name))
        len = sizeof(default_output_name) - 1;

      strncpy(default_output_name, base_name, len);
      default_output_name[len] = '\0';

      if (len == 0)
        strcpy(default_output_name, "a.out"); // Fallback

      output_name = default_output_name;
    }

    // Cache check: skip the whole AOT pipeline (~400ms link cost on Win)
    // when the output binary is newer than both the source file and the
    // compiler driver itself. Disable with TULPAR_AOT_NOCACHE=1 or by
    // deleting the output file. We only check this for the persistent
    // `tulpar build` path; the silent run path uses a temp output.
    {
      const char *nocache = getenv("TULPAR_AOT_NOCACHE");
      if (!(nocache && *nocache && *nocache != '0')) {
        char exe_path[512];
#ifdef _WIN32
        snprintf(exe_path, sizeof(exe_path), "%s.exe", output_name);
#else
        snprintf(exe_path, sizeof(exe_path), "%s", output_name);
#endif
        struct stat src_st, exe_st, drv_st;
        if (stat(exe_path, &exe_st) == 0 &&
            stat(argv[arg_offset + 1], &src_st) == 0) {
          int driver_ok = (stat(argv[0], &drv_st) == 0);
          if (exe_st.st_mtime >= src_st.st_mtime &&
              (!driver_ok || exe_st.st_mtime >= drv_st.st_mtime)) {
            printf("[AOT] Cache hit: %s up-to-date\n", exe_path);
            free(source);
            return 0;
          }
        }
      }
    }

    AOTResult result = aot_compile_with_filename_debug(
        source, output_name, argv[arg_offset + 1], emit_debug);
    free(source);
    return (result == AOT_OK) ? 0 : 1;
  }
#endif

  char *source = nullptr;
  int from_file = 0;

  // Command line arguments
  if (argc > arg_offset) {
    source = read_file(argv[arg_offset]);
    if (!source) {
      return 1;
    }
    from_file = 1;

    // Run the typeinfer pre-pass before either AOT or VM dispatch so users
    // see `[typecheck]` warnings on default execution (`tulpar foo.tpr`)
    // and `tulpar --vm foo.tpr` alike. Build mode runs its own call above.
    if (!skip_typecheck) {
      int n = tulpar::typeinfer_emit_warnings(source, argv[arg_offset]);
      if (strict_typecheck && n > 0) {
        fprintf(stderr,
                "[typecheck] %d %s (strict mode); aborting run.\n",
                n, n == 1 ? "error" : "errors");
        free(source);
        return 1;
      }
    }

#ifdef TULPAR_AOT_ENABLED
    // Tulpar is AOT-only (see CLAUDE.md "AOT-ONLY"): this is the single
    // execution path. Any AOT failure is a hard error — there is NO VM
    // fallback. "It ran" therefore always means the AOT path ran.
    AOTResult aot_result = aot_compile_and_run_silent_with_filename(
        source, argv[arg_offset]);
    free(source);
    if (aot_result == AOT_OK)
      return 0;
    if (aot_result == AOT_ERROR_PARSE || aot_result == AOT_ERROR_CODEGEN)
      return 1; // diagnostic already printed
    if (aot_result == AOT_RAN_NONZERO)
      return 1; // the program compiled, ran, and exited non-zero — its own
                // output already explained why; don't print a toolchain error.
    // AOT_ERROR_EMIT / AOT_ERROR_LINK: toolchain or runtime archive missing.
    fprintf(stderr, "%s\n",
            tulpar::i18n::tr_en(
                "AOT derleme/baglama basarisiz: clang ve libtulpar_runtime.a "
                "mevcut mu? (Tulpar yalnizca AOT calisir; VM yedegi yok.)",
                "AOT compile/link failed: are clang and libtulpar_runtime.a "
                "present? (Tulpar is AOT-only; there is no VM fallback.)"));
    return 1;
#else
    free(source);
    fprintf(stderr, "This build has no AOT backend (TULPAR_AOT_ENABLED off).\n");
    return 1;
#endif
  } else {
    // No arguments — fall through to the same help text users reach via
    // explicit `--help` / `-h` / `?`. Single source of truth.
    print_help();
    return 0;
  }

  // Unreachable: every branch above returns. The legacy bytecode-VM
  // execution path that used to live here was removed with the VM
  // (AOT-only, 2026-06-15).
  return 0;
}
