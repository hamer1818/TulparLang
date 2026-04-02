#include "aot_pipeline.hpp"
#include "../lexer/lexer.hpp"
#include "../parser/parser.hpp"
#include "../common/localization.hpp"
#include "llvm_backend.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <llvm-c/Core.h>
#include <llvm-c/Target.h>
#include <llvm-c/TargetMachine.h>

// Parse source code to AST
static ASTNode_C *parse_source(const char *source) {
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

// Compile Tulpar source to object file
AOTResult aot_compile(const char *source, const char *output_name) {
  printf("[AOT] Parsing source...\n");
  ASTNode_C *ast = parse_source(source);
  if (!ast) {
    fprintf(stderr, "%s", tulpar::i18n::tr_for_en("[AOT] Error: Failed to parse source\n"));
    return AOT_ERROR_PARSE;
  }

  printf("[AOT] Creating LLVM backend...\n");
  LLVMBackend *backend = llvm_backend_create("tulpar_aot_module");
  if (!backend) {
    fprintf(stderr, "%s", tulpar::i18n::tr_for_en("[AOT] Error: Failed to create LLVM backend\n"));
    ast_node_free(ast);
    return AOT_ERROR_CODEGEN;
  }

  printf("[AOT] Generating LLVM IR...\n");
  llvm_backend_compile(backend, ast);

  printf("[AOT] Optimizing...\n");
  llvm_backend_optimize(backend);

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
    fprintf(stderr, "%s", tulpar::i18n::tr_for_en("[AOT] Error: Failed to emit object file\n"));
    llvm_backend_destroy(backend);
    ast_node_free(ast);
    return AOT_ERROR_EMIT;
  }

  // Link using clang (or system linker)
  printf("[AOT] Linking executable: %s\n", exe_filename);
  char link_cmd[1024];
  snprintf(
      link_cmd, sizeof(link_cmd),
      "clang -O3 %s -o %s -no-pie -L./build -L./build-linux -L./build-macos "
      "-ltulpar_runtime -lm -lpthread -ldl 2>&1",
      obj_filename, exe_filename);

  int link_result = system(link_cmd);
  if (link_result != 0) {
    fprintf(stderr, tulpar::i18n::tr_for_en(
            "[AOT] Error: Linking failed (code %d). Check clang installation and libraries.\n"),
            link_result);
    llvm_backend_destroy(backend);
    ast_node_free(ast);
    return AOT_ERROR_LINK;
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
  system("./tulpar_temp");

  return AOT_OK;
}

// Silent compile to native binary (no output, temp files)
static AOTResult aot_compile_silent(const char *source,
                                    const char *output_name) {
  ASTNode_C *ast = parse_source(source);
  if (!ast) {
    return AOT_ERROR_PARSE;
  }

  LLVMBackend *backend = llvm_backend_create("tulpar_aot_module");
  if (!backend) {
    ast_node_free(ast);
    return AOT_ERROR_CODEGEN;
  }
  backend->quiet = 1; // Suppress [AOT] messages

  llvm_backend_compile(backend, ast);
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
  char link_cmd[1024];
  snprintf(
      link_cmd, sizeof(link_cmd),
      "clang -O3 %s -o %s -no-pie -L./build -L./build-linux -L./build-macos "
      "-ltulpar_runtime -lm -lpthread -ldl 2>/dev/nullptr",
      obj_filename, exe_filename);

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

// Silent compile and run - used as default execution mode
// Compiles to temp binary, runs it, cleans up. No [AOT] output.
AOTResult aot_compile_and_run_silent(const char *source) {
  AOTResult result = aot_compile_silent(source, "/tmp/.tulpar_run");
  if (result != AOT_OK) {
    return result;
  }

  // Execute the compiled binary
  int run_result = system("/tmp/.tulpar_run");

  // Cleanup temp files
  remove("/tmp/.tulpar_run");
  remove("/tmp/.tulpar_run.ll");

  return (run_result == 0) ? AOT_OK : AOT_ERROR_LINK;
}
