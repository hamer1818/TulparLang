// Tulpar AOT Compiler Driver
// Phase 3: Object Emission & Auto-Linking & Optimization

#include "../lexer/lexer.h"
#include "../parser/parser.h"
#include "llvm_backend.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


// Stub for read_file
char *read_file_stub(const char *path) {
  FILE *file = fopen(path, "rb");
  if (!file) {
    fprintf(stderr, "Could not open file \"%s\".\n", path);
    exit(74);
  }
  fseek(file, 0L, SEEK_END);
  size_t fileSize = ftell(file);
  rewind(file);
  char *buffer = (char *)malloc(fileSize + 1);
  if (!buffer) {
    fprintf(stderr, "Not enough memory to read \"%s\".\n", path);
    exit(74);
  }
  size_t bytesRead = fread(buffer, sizeof(char), fileSize, file);
  buffer[bytesRead] = '\0';
  fclose(file);
  return buffer;
}

int main(int argc, char *argv[]) {
  if (argc < 2) {
    printf("Usage: tulpar_aot <script.tpr>\n");
    return 1;
  }

  char *source_file = argv[1];
  printf("Compiling %s...\n", source_file);

  char *source = read_file_stub(source_file);
  Lexer *lexer = lexer_create(source);

  Token **tokens = (Token **)malloc(sizeof(Token *) * 10000);
  int token_count = 0;
  Token *token = lexer_next_token(lexer);
  while (token->type != TOKEN_EOF && token_count < 10000) {
    tokens[token_count++] = token;
    token = lexer_next_token(lexer);
  }
  tokens[token_count++] = token;

  Parser *parser = parser_create(tokens, token_count);
  ASTNode *ast = parser_parse(parser);

  if (!ast) {
    printf("Parsing failed.\n");
    return 1;
  }

  LLVMBackend *backend = llvm_backend_create(source_file);
  llvm_backend_compile(backend, ast);

  // Apply optimizations
  llvm_backend_optimize(backend);

  // printf("\n=== Generated Code ===\n");
  // llvm_backend_print_ir(backend);

  // Generate Object File
  char obj_file[256];
  snprintf(obj_file, sizeof(obj_file), "%s.obj", source_file);

  if (llvm_backend_emit_object(backend, obj_file) == 0) {
    printf("Object file written to %s\n", obj_file);

    // Automatic Linking
    char exe_file[256];
    strcpy(exe_file, source_file);
    char *ext = strrchr(exe_file, '.');
    if (ext)
      *ext = '\0';
    strcat(exe_file, ".exe");

    printf("Linking to %s...\n", exe_file);
    char cmd[1024];
    // Use clang to link.
    snprintf(cmd, sizeof(cmd), "clang \"%s\" -o \"%s\"", obj_file, exe_file);

    int ret = system(cmd);
    if (ret == 0) {
      printf("Build successful: %s\n", exe_file);
    } else {
      printf("Linking failed (exit code %d).\n", ret);
    }

  } else {
    printf("Failed to generate object file.\n");
  }

  llvm_backend_destroy(backend);
  return 0;
}
