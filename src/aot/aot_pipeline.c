#include "aot_pipeline.h"
#include "../lexer/lexer.h"
#include "../parser/parser.h"
#include "llvm_backend.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <llvm-c/Core.h>
#include <llvm-c/Target.h>
#include <llvm-c/TargetMachine.h>

// Parse source code to AST
static ASTNode *parse_source(const char *source) {
  Lexer *lexer = lexer_create(source);

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
  tokens[token_count++] = token; // EOF

  lexer_free(lexer);

  Parser *parser = parser_create(tokens, token_count);
  ASTNode *ast = parser_parse(parser);

  // Note: tokens are still referenced by AST, careful with cleanup
  parser_free(parser);

  // Free tokens after parser (AST copies what it needs)
  for (int i = 0; i < token_count; i++) {
    token_free(tokens[i]);
  }
  free(tokens);

  return ast;
}

// Compile Tulpar source to object file
AOTResult aot_compile(const char *source, const char *output_name) {
  printf("[AOT] Parsing source...\n");
  ASTNode *ast = parse_source(source);
  if (!ast) {
    fprintf(stderr, "[AOT] Error: Failed to parse source\n");
    return AOT_ERROR_PARSE;
  }

  printf("[AOT] Creating LLVM backend...\n");
  LLVMBackend *backend = llvm_backend_create("tulpar_aot_module");
  if (!backend) {
    fprintf(stderr, "[AOT] Error: Failed to create LLVM backend\n");
    ast_node_free(ast);
    return AOT_ERROR_CODEGEN;
  }

  printf("[AOT] Generating LLVM IR...\n");
  llvm_backend_compile(backend, ast);

  // Generate output filename
  char obj_filename[256];
  char exe_filename[256];
  snprintf(obj_filename, sizeof(obj_filename), "%s.o", output_name);
  snprintf(exe_filename, sizeof(exe_filename), "%s", output_name);

  // Emit IR (Debug)
  char ir_file[256];
  snprintf(ir_file, sizeof(ir_file), "%s.ll", output_name);
  llvm_backend_emit_ir_file(backend, ir_file);

  printf("[AOT] Emitting object file: %s\n", obj_filename);
  if (llvm_backend_emit_object(backend, obj_filename) != 0) {
    fprintf(stderr, "[AOT] Error: Failed to emit object file\n");
    llvm_backend_destroy(backend);
    ast_node_free(ast);
    return AOT_ERROR_EMIT;
  }

  // Link using clang (or system linker)
  printf("[AOT] Linking executable: %s\n", exe_filename);
  char link_cmd[1024];
#ifdef _WIN32
  snprintf(link_cmd, sizeof(link_cmd),
           "clang %s -o %s.exe -L./build -ltulpar_runtime 2>&1", obj_filename,
           exe_filename);
#else
  snprintf(link_cmd, sizeof(link_cmd),
           "clang %s -o %s -no-pie -L./build -ltulpar_runtime -lm -lpthread "
           "-ldl 2>&1",
           obj_filename, exe_filename);
#endif

  int link_result = system(link_cmd);
  if (link_result != 0) {
    fprintf(stderr,
            "[AOT] Warning: Linking failed (code %d). Object file generated.\n",
            link_result);
    // Don't return error - object file is still valid
  } else {
    printf("[AOT] Successfully created: %s\n", exe_filename);
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
  printf("[AOT] Executing generated binary...\n");
#ifdef _WIN32
  system("tulpar_temp.exe");
#else
  system("./tulpar_temp");
#endif

  return AOT_OK;
}
