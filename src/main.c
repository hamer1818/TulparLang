
#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#include <windows.h>
#include <winsock2.h>

#endif
#include "interpreter/interpreter.h"
#include "lexer/lexer.h"
#include "parser/parser.h"
#include "vm/compiler.h"
#include "vm/vm.h"
#ifdef TULPAR_AOT_ENABLED
#include "aot/aot_pipeline.h"
#endif
#include <time.h>

// Dosyadan kaynak kodu oku
char *read_file(const char *filename) {
  FILE *file = fopen(filename, "rb");
  if (!file) {
    printf("Error: Could not open file '%s'!\n", filename);
    return NULL;
  }

  // Dosya boyutunu bul
  fseek(file, 0, SEEK_END);
  long size = ftell(file);
  fseek(file, 0, SEEK_SET);

  // Bellek ayır ve oku
  char *buffer = (char *)malloc(size + 1);
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
#ifdef _WIN32
      system("cls");
#else
      system("clear");
#endif
      continue;
    }

    // Execute code
    Lexer *lexer = lexer_create(line);
    int token_capacity = 100;
    int token_count = 0;
    Token **tokens = (Token **)malloc(sizeof(Token *) * token_capacity);

    Token *token;
    while ((token = lexer_next_token(lexer))->type != TOKEN_EOF) {
      if (token_count >= token_capacity) {
        token_capacity *= 2;
        tokens = (Token **)realloc(tokens, sizeof(Token *) * token_capacity);
      }
      tokens[token_count++] = token;
    }
    tokens[token_count++] = token;

    lexer_free(lexer);

    Parser *parser = parser_create(tokens, token_count);
    ASTNode *ast = parser_parse(parser);

    if (ast && ast->type == AST_PROGRAM && ast->statement_count > 0) {
      ASTNode *stmt = ast->statements[0];
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
// Windows UTF-8 support
#ifdef _WIN32
  SetConsoleOutputCP(65001); // CP_UTF8
  SetConsoleCP(65001);
  _setmode(_fileno(stdout), _O_BINARY);
  _setmode(_fileno(stderr), _O_BINARY);
  _setmode(_fileno(stdin), _O_BINARY);
  setvbuf(stdout, NULL, _IONBF, 0);
#endif

  // Locale setup
  setlocale(LC_ALL, ".UTF8");

  // Check for REPL mode
  if (argc > 1 &&
      (strcmp(argv[1], "--repl") == 0 || strcmp(argv[1], "-i") == 0)) {
    run_repl();
    return 0;
  }

  // Check for AOT mode
#ifdef TULPAR_AOT_ENABLED
  if (argc > 1 && strcmp(argv[1], "--aot") == 0) {
    if (argc < 3) {
      printf("Usage: tulpar --aot <source.tpr> [output_name]\n");
      return 1;
    }
    char *source = read_file(argv[2]);
    if (!source) {
      return 1;
    }

    // Output name defaults to input file without extension
    const char *output_name = (argc > 3) ? argv[3] : "a.out";

    AOTResult result = aot_compile(source, output_name);
    free(source);
    return (result == AOT_OK) ? 0 : 1;
  }
#endif

  char *source = NULL;
  int from_file = 0;

  // Command line arguments
  if (argc > 1) {
    // Check for explicit --aot flag OR if file ends with .tpr (auto-AOT)
#ifdef TULPAR_AOT_ENABLED
    int use_aot = 0;
    int file_arg_index = 1;
    const char *output_name = "a.out";

    if (strcmp(argv[1], "--aot") == 0) {
      use_aot = 1;
      file_arg_index = 2;
      if (argc > 3)
        output_name = argv[3];
    } else {
      // Auto-detect .tpr files -> use AOT
      const char *ext = strrchr(argv[1], '.');
      if (ext && strcmp(ext, ".tpr") == 0) {
        use_aot = 1;
        if (argc > 2)
          output_name = argv[2];
      }
    }

    if (use_aot) {
      if (file_arg_index >= argc) {
        printf("TulparLang v2.1.0 (LLVM Backend)\n");
        printf("Usage:\n");
        printf("  tulpar <source.tpr>           - Compile to native binary\n");
        printf("  tulpar <source.tpr> <output>  - Compile with custom output "
               "name\n");
        printf("  tulpar --aot <source.tpr>     - Explicit AOT compilation\n");
        printf("  tulpar --repl                 - Interactive mode\n");
        return 1;
      }

      source = read_file(argv[file_arg_index]);
      if (!source)
        return 1;

      printf("TulparLang v2.1.0 (LLVM Backend)\n");
      printf("Compiling %s -> %s\n", argv[file_arg_index], output_name);

      AOTResult result = aot_compile(source, output_name);
      free(source);
      return (result == AOT_OK) ? 0 : 1;
    }
#endif

    // Legacy: VM execution (kept for compatibility)
    source = read_file(argv[1]);
    if (!source) {
      return 1;
    }
    from_file = 1;
  } else {
    // No arguments - show help
    printf("TulparLang v2.1.0 (LLVM Backend)\n\n");
    printf("Usage:\n");
    printf("  tulpar <source.tpr>           - Compile to native binary\n");
    printf(
        "  tulpar <source.tpr> <output>  - Compile with custom output name\n");
    printf("  tulpar --repl                 - Interactive mode\n");
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
  Token **tokens = (Token **)malloc(sizeof(Token *) * token_capacity);

  Token *token;
  while ((token = lexer_next_token(lexer))->type != TOKEN_EOF) {
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

  Parser *parser = parser_create(tokens, token_count);
  ASTNode *ast = parser_parse(parser);

  if (!from_file) {
    printf("Abstract Syntax Tree:\n");
    ast_print(ast, 0);
  }

  // ========================================
  // 3. RUNTIME (VM vs Interpreter)
  // ========================================
  int use_vm = 1; // Default to VM

  if (argc > 2 && strcmp(argv[2], "--legacy") == 0) {
    use_vm = 0;
  }

  if (use_vm) {
    if (!from_file) {
      printf("\n3. COMPILER & VM (Bytecode Execution)\n");
      printf("========================================\n");
    }

    // Compile AST to Bytecode
    Chunk *chunk = compile(ast);

    if (!chunk) {
      printf("Error: Compilation failed!\n");
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
