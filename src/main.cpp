
#include <locale.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sys/stat.h>
#ifdef _WIN32
// WIN32_LEAN_AND_MEAN keeps windows.h from pulling in the legacy winsock.h,
// which would conflict with the winsock2.h that platform_sockets.h includes
// further down through the interpreter/parser/vm headers.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#include "interpreter/interpreter.hpp"
#include "lexer/lexer.hpp"
#include "parser/parser.hpp"
#include "common/localization.hpp"
#include "vm/compiler.hpp"
#include "vm/vm.hpp"
#ifdef TULPAR_AOT_ENABLED
#include "aot/aot_pipeline.hpp"
#endif
#include "lsp/lsp_server.hpp"
#include "fmt/formatter.hpp"
#include "pkg/pkg_cli.hpp"
#include "cli/update_cmd.hpp"
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
  std::printf("  tulpar --vm <source.tpr>         %s\n",
              tulpar::i18n::tr_en("- VM ile calistir (anlik baslangic)",
                                  "- Run via VM (instant start)"));
  std::printf("  tulpar --legacy <source.tpr>     %s\n",
              tulpar::i18n::tr_en("- Tree-walk yorumlayici (eski)",
                                  "- Tree-walk interpreter (legacy)"));
  std::printf("  tulpar --repl, -i                %s\n",
              tulpar::i18n::tr_en("- Etkilesimli mod (REPL)",
                                  "- Interactive mode (REPL)"));

  std::printf("\n%s\n",
              tulpar::i18n::tr_en("Aletler:", "Tools:"));
  std::printf("  tulpar fmt <source.tpr>          %s\n",
              tulpar::i18n::tr_en("- Kaynak kodu formatla",
                                  "- Format source code"));
  std::printf("  tulpar pkg <komut>               %s\n",
              tulpar::i18n::tr_en("- Paket yoneticisi (init/install/...)",
                                  "- Package manager (init/install/...)"));
  std::printf("  tulpar --lsp                     %s\n",
              tulpar::i18n::tr_en("- LSP sunucusu (editor entegrasyonu)",
                                  "- LSP server (editor integration)"));

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

// Returns true when the accumulated REPL buffer's brackets/parens/braces
// are balanced and we aren't sitting inside an unterminated string or
// block comment — i.e. the user has typed enough source to make a
// well-formed input attempt. Used to decide between primary (`>>> `) and
// continuation (`... `) prompts. String / line-comment / block-comment
// state is tracked so braces inside them don't trick us into prompting
// early. Tracking is approximate (we don't care about syntactic nesting,
// only character-level matching) but that's fine for this UX shortcut —
// the parser still has the final say on validity.
static int repl_input_complete(const char *s) {
  int paren = 0, brace = 0, bracket = 0;
  int in_string = 0, in_line_comment = 0, in_block_comment = 0;
  char string_quote = 0;

  for (size_t i = 0; s[i]; i++) {
    char c = s[i];
    char nx = s[i + 1];

    if (in_line_comment) {
      if (c == '\n') in_line_comment = 0;
      continue;
    }
    if (in_block_comment) {
      if (c == '*' && nx == '/') {
        in_block_comment = 0;
        i++;
      }
      continue;
    }
    if (in_string) {
      if (c == '\\' && nx) { i++; continue; }
      if (c == string_quote) in_string = 0;
      continue;
    }

    if (c == '/' && nx == '/') { in_line_comment = 1; i++; continue; }
    if (c == '/' && nx == '*') { in_block_comment = 1; i++; continue; }
    if (c == '"' || c == '\'') { in_string = 1; string_quote = c; continue; }

    if (c == '(') paren++;
    else if (c == ')') paren--;
    else if (c == '{') brace++;
    else if (c == '}') brace--;
    else if (c == '[') bracket++;
    else if (c == ']') bracket--;
  }

  if (paren > 0 || brace > 0 || bracket > 0) return 0;
  if (in_string || in_block_comment) return 0;
  return 1;
}

// REPL mode
static void run_repl() {
  printf("TulparLang REPL (Interactive Mode)\n");
  printf("Type 'exit' or 'quit' to exit, 'help' for help\n");
  printf("========================================\n\n");

  Interpreter *interp = interpreter_create();
  char line[4096];

  // Accumulator for multi-line input. Most interactive sessions stay
  // tiny, but loops/function bodies/JSON literals do span lines. 64 KB
  // is plenty for any reasonable typed input; on overflow we reset and
  // surface a clear message rather than silently truncating.
  size_t buf_cap = 65536;
  char *buf = static_cast<char *>(malloc(buf_cap));
  if (!buf) {
    printf("error: REPL buffer allocation failed\n");
    interpreter_free(interp);
    return;
  }
  buf[0] = '\0';
  size_t buf_len = 0;
  int continuation = 0;

  while (1) {
    printf(continuation ? "... " : ">>> ");
    fflush(stdout);

    if (!fgets(line, sizeof(line), stdin)) {
      if (feof(stdin)) {
        printf("\n");
        break;
      }
      continue;
    }

    size_t line_len = strlen(line);

    // First-line meta-commands. We only intercept `exit`/`quit`/`help`/
    // `clear` on a fresh prompt; once a multi-line input is in progress
    // any line goes into the buffer verbatim (so `exit` inside a string
    // literal isn't hijacked).
    if (!continuation) {
      char trimmed[sizeof(line)];
      memcpy(trimmed, line, line_len + 1);
      size_t tlen = line_len;
      if (tlen > 0 && trimmed[tlen - 1] == '\n') {
        trimmed[tlen - 1] = '\0';
        tlen--;
      }

      if (tlen == 0) continue;
      if (strcmp(trimmed, "exit") == 0 || strcmp(trimmed, "quit") == 0) break;
      if (strcmp(trimmed, "help") == 0) {
        printf("Commands:\n");
        printf("  exit, quit - Exit REPL\n");
        printf("  help       - Show this help\n");
        printf("  clear      - Clear screen\n");
        printf("  Multi-line: '{', '(', '[' open a continuation prompt ('... ')\n");
        continue;
      }
      if (strcmp(trimmed, "clear") == 0) {
        int clear_status = system("clear");
        (void)clear_status;
        continue;
      }
    } else {
      // Continuation mode escape hatch: a literal blank line aborts the
      // in-progress multi-line buffer instead of forcing the user to
      // close every brace they've opened by mistake.
      int only_ws = 1;
      for (size_t i = 0; line[i]; i++) {
        if (line[i] != ' ' && line[i] != '\t' && line[i] != '\r' &&
            line[i] != '\n') {
          only_ws = 0;
          break;
        }
      }
      if (only_ws) {
        buf_len = 0;
        buf[0] = '\0';
        continuation = 0;
        continue;
      }
    }

    // Append (with newline preserved so multi-line source-location
    // diagnostics report the right line number).
    if (buf_len + line_len + 1 >= buf_cap) {
      printf("error: input too long, resetting REPL buffer\n");
      buf_len = 0;
      buf[0] = '\0';
      continuation = 0;
      continue;
    }
    memcpy(buf + buf_len, line, line_len);
    buf_len += line_len;
    buf[buf_len] = '\0';

    // Need more input?
    if (!repl_input_complete(buf)) {
      continuation = 1;
      continue;
    }

    // Trim trailing whitespace before the terminator check below.
    while (buf_len > 0 &&
           (buf[buf_len - 1] == '\n' || buf[buf_len - 1] == '\r' ||
            buf[buf_len - 1] == ' ' || buf[buf_len - 1] == '\t')) {
      buf_len--;
      buf[buf_len] = '\0';
    }
    if (buf_len == 0) {
      continuation = 0;
      continue;
    }

    // REPL convenience: forgiving terminator. Tulpar's grammar requires
    // ';' after expression statements, but typing it on every interactive
    // line is friction users don't expect (Python/Node REPLs don't ask
    // for one). If the input doesn't already end with a statement
    // terminator (`;`) or block closer (`}`), append `;` before lexing.
    char last = buf[buf_len - 1];
    if (last != ';' && last != '}' && buf_len + 1 < buf_cap) {
      buf[buf_len] = ';';
      buf[buf_len + 1] = '\0';
      buf_len++;
    }

    // Execute code
    Lexer *lexer = lexer_create(buf);
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
    tokens[token_count++] = token;

    lexer_free(lexer);

    Parser_C *parser = parser_create(tokens, token_count);
    ASTNode_C *ast = parser_parse(parser);

    if (ast && ast->type == AST_PROGRAM && ast->statement_count > 0) {
      ASTNode_C *stmt = ast->statements[0];
      // Check if it's a function call or expression that can be evaluated
      if (stmt->type == AST_FUNCTION_CALL) {
        // Function call - evaluate and print result if not void
        Value *result = interpreter_eval(interp, stmt);
        if (result && result->type != VAL_VOID) {
          value_print(result);
          printf("\n");
        }
        value_free(result);
      } else if (stmt->type == AST_IDENTIFIER || stmt->type == AST_BINARY_OP ||
                 stmt->type == AST_UNARY_OP || stmt->type == AST_ARRAY_ACCESS) {
        // Expression - evaluate and print result
        Value *result = interpreter_eval(interp, stmt);
        if (result && result->type != VAL_VOID) {
          value_print(result);
          printf("\n");
        }
        value_free(result);
      } else {
        // It's a statement, execute it
        interpreter_execute_statement(interp, stmt);
      }
    }

    ast_node_free(ast);
    parser_free(parser);

    for (int i = 0; i < token_count; i++) {
      token_free(tokens[i]);
    }
    free(tokens);

    // Done with this input — reset the multi-line accumulator so the
    // next prompt is the primary one again.
    buf_len = 0;
    buf[0] = '\0';
    continuation = 0;
  }

  free(buf);
  interpreter_free(interp);
  printf("Goodbye!\n");
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

  // Flags
  int force_vm = 0;    // --vm / --run forces VM path, skips AOT
  int use_legacy = 0;  // --legacy forces interpreter (not VM)
  int build_mode = 0;  // build / --build / --aot: save native binary
  int arg_offset = 1;  // index of first non-flag arg

  // Parse flags
  for (int i = 1; i < argc; i++) {

    if (strcmp(argv[i], "--vm") == 0 || strcmp(argv[i], "--run") == 0) {
      force_vm = 1;
      arg_offset = i + 1;
    } else if (strcmp(argv[i], "--legacy") == 0) {
      use_legacy = 1;
      arg_offset = i + 1;
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

  // Check for REPL mode
  if (argc > 1 &&
      (strcmp(argv[1], "--repl") == 0 || strcmp(argv[1], "-i") == 0)) {
    run_repl();
    return 0;
  }

#ifdef TULPAR_AOT_ENABLED
  // Build mode: save native binary (tulpar build file.tpr [output])
  if (!force_vm && build_mode) {
    if (arg_offset + 1 >= argc) {
      printf("Usage: tulpar build <source.tpr> [output_name]\n");
      return 1;
    }
    char *source = read_file(argv[arg_offset + 1]);
    if (!source) {
      return 1;
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

    AOTResult result = aot_compile_with_filename(
        source, output_name, argv[arg_offset + 1]);
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

#ifdef TULPAR_AOT_ENABLED
    // Default mode: AOT compile & run (fastest execution).
    // Fall back to VM only when AOT failed for *infrastructure* reasons
    // (object emit / link). Source-level errors (parse, codegen) are
    // already user-visible and re-running them through the VM would just
    // print the same diagnostic a second time — that's the duplicate
    // error users used to see on Windows where the runtime archive is
    // often missing.
    if (!force_vm && !use_legacy) {
      AOTResult aot_result = aot_compile_and_run_silent_with_filename(
          source, argv[arg_offset]);
      if (aot_result == AOT_OK) {
        free(source);
        return 0;
      }
      if (aot_result == AOT_ERROR_PARSE ||
          aot_result == AOT_ERROR_CODEGEN) {
        free(source);
        return 1;
      }
      // AOT_ERROR_EMIT / AOT_ERROR_LINK: clang or libtulpar_runtime.a
      // missing — fall through to VM silently so the program still runs.
    }
#endif
  } else {
    // No arguments — fall through to the same help text users reach via
    // explicit `--help` / `-h` / `?`. Single source of truth.
    print_help();
    return 0;
  }

  if (!from_file) {
    printf("========================================\n");
    printf("   TulparLang v2.1.0 (LLVM Backend)\n");
    printf("========================================\n\n");
  }

  // ========================================
  // 1. LEXER (Tokenization)
  // ========================================
  if (!from_file) {
    printf("1. LEXER (Tokenization)\n");
    printf("========================\n");
  }
  Lexer *lexer = lexer_create(source);

  // Token dizisi oluştur
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
    if (!from_file) {
      token_print(token);
    }
  }
  tokens[token_count++] = token; // EOF token'ı ekle

  lexer_free(lexer);

  // ========================================
  // 2. PARSER (AST Oluşturma)
  // ========================================
  if (!from_file) {
    printf("\n2. PARSER (AST Olusturma)\n");
    printf("==========================\n");
  }

  Parser_C *parser = parser_create(tokens, token_count);
  ASTNode_C *ast = parser_parse(parser);
  if (!ast) {
    fprintf(stderr, "%s\n",
            tulpar::i18n::tr_en("Hata: Parse asamasi basarisiz.",
                                "Error: Parse phase failed."));
    parser_free(parser);
    for (int i = 0; i < token_count; i++) {
      token_free(tokens[i]);
    }
    free(tokens);
    free(source);
    return 1;
  }

  if (!from_file) {
    printf("Abstract Syntax Tree:\n");
    ast_print(ast, 0);
  }

  // ========================================
  // 3. RUNTIME (VM vs Interpreter)
  // ========================================
  int use_vm = !use_legacy; // Use VM unless --legacy flag was set

  if (use_vm) {
    if (!from_file) {
      printf("\n3. COMPILER & VM (Bytecode Execution)\n");
      printf("========================================\n");
    }

    // Compile AST to Bytecode
    Chunk *chunk = compile(ast);

    if (!chunk) {
      printf("%s\n",
             tulpar::i18n::tr_en("Hata: Derleme basarisiz!",
                                 "Error: Compilation failed!"));
      return 1;
    }

    if (!from_file) {
      // chunk_disassemble(chunk, "Main Script");
    }

    // Initialize VM
    VM *vm = vm_create();

    // Create main script function
    ObjFunction *script = vm_new_function(vm);

    // Transfer chunk ownership to script function
    script->chunk = *chunk; // Copy struct content
    free(chunk);            // Free the container, keep the content

    // Run VM
    clock_t start = clock();
    VMResult result = vm_run(vm, script);
    clock_t end = clock();

    if (!from_file) {
      double cpu_time_used = ((double)(end - start)) / CLOCKS_PER_SEC * 1000.0;
      printf("\nUnpacked Execution Time: %.2f ms\n", cpu_time_used);

      if (result == VM_OK) {
        printf("\n========================================\n");
        printf("   Tulpar VM basariyla calisti! ✓\n");
        printf("========================================\n");
      } else {
        printf("\n========================================\n");
        printf("   Tulpar VM hatayla sonlandi! ✗\n");
        printf("========================================\n");
      }
    }

    vm_free(vm);

  } else {
    // Legacy Interpreter
    if (!from_file) {
      printf("\n3. INTERPRETER (Legacy Execution)\n");
      printf("=================================\n");
    }

    Interpreter *interp = interpreter_create();
    interpreter_execute(interp, ast);

    // Sonuçları göster
    if (!from_file) {
      printf("\nDegisken Degerleri:\n");
      printf("-------------------\n");

      // Tüm global değişkenleri göster
      for (int i = 0; i < interp->global_scope->var_count; i++) {
        printf("%s = ", interp->global_scope->variables[i]->name);
        value_print(interp->global_scope->variables[i]->value);
        printf("\n");
      }

      printf("\n========================================\n");
      printf("   TulparLang (Legacy) basariyla calisti! ✓\n");
      printf("========================================\n");
    }
    interpreter_free(interp);
  }

  // Temizlik
  ast_node_free(ast);
  parser_free(parser);

  for (int i = 0; i < token_count; i++) {
    token_free(tokens[i]);
  }
  free(tokens);
  free(source);

  return 0;
}
