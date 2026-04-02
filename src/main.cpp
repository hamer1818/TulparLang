
#include <locale.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "interpreter/interpreter.hpp"
#include "lexer/lexer.hpp"
#include "parser/parser.hpp"
#include "common/localization.hpp"
#include "vm/compiler.hpp"
#include "vm/vm.hpp"
#ifdef TULPAR_AOT_ENABLED
#include "aot/aot_pipeline.hpp"
#endif
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
  fread(buffer, 1, size, file);
  buffer[size] = '\0';

  fclose(file);
  return buffer;
}

// REPL mode
static void run_repl() {
  printf("TulparLang REPL (Interactive Mode)\n");
  printf("Type 'exit' or 'quit' to exit, 'help' for help\n");
  printf("========================================\n\n");

  Interpreter *interp = interpreter_create();
  char line[4096];

  while (1) {
    printf(">>> ");
    fflush(stdout);

    if (!fgets(line, sizeof(line), stdin)) {
      if (feof(stdin)) {
        printf("\n");
        break;
      }
      continue;
    }

    // Remove newline
    size_t len = strlen(line);
    if (len > 0 && line[len - 1] == '\n') {
      line[len - 1] = '\0';
      len--;
    }

    // Skip empty lines
    if (len == 0)
      continue;

    // Handle commands
    if (strcmp(line, "exit") == 0 || strcmp(line, "quit") == 0) {
      break;
    }
    if (strcmp(line, "help") == 0) {
      printf("Commands:\n");
      printf("  exit, quit - Exit REPL\n");
      printf("  help       - Show this help\n");
      printf("  clear      - Clear screen\n");
      continue;
    }
    if (strcmp(line, "clear") == 0) {
      system("clear");
      continue;
    }

    // Execute code
    Lexer *lexer = lexer_create(line);
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
  }

  interpreter_free(interp);
  printf("Goodbye!\n");
}

int main(int argc, char **argv) {
  // Locale setup
  setlocale(LC_ALL, ".UTF8");

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

    AOTResult result = aot_compile(source, output_name);
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
    // Default mode: AOT compile & run (fastest execution)
    // Falls back to VM if AOT compilation fails
    if (!force_vm && !use_legacy) {
      AOTResult aot_result = aot_compile_and_run_silent(source);
      if (aot_result == AOT_OK) {
        free(source);
        return 0;
      }
      // AOT failed - fall through to VM mode silently
    }
#endif
  } else {
    // No arguments - show help
    printf("TulparLang v2.1.0 (LLVM AOT Backend)\n\n");
    printf("Usage:\n");
    printf("  tulpar <source.tpr>              - Run program (AOT native "
           "speed)\n");
    printf("  tulpar build <source.tpr> [out]  - Build standalone native "
           "binary\n");
    printf(
        "  tulpar --vm <source.tpr>         - Run via VM (instant start)\n");
    printf("  tulpar --repl                    - Interactive mode\n");
    printf("\nTulparLang: Python gibi kolay, C gibi hizli.\n");
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
