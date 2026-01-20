#include "../parser/parser.h"
#include "llvm_backend.h"
#include "llvm_values.h"
#include <llvm-c/Analysis.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// --- Mock Helpers ---

ASTNode *mock_int(long long val) {
  ASTNode *node = (ASTNode *)malloc(sizeof(ASTNode));
  memset(node, 0, sizeof(ASTNode));
  node->type = AST_INT_LITERAL;
  node->value.int_value = val;
  return node;
}

ASTNode *mock_string(const char *val) {
  ASTNode *node = (ASTNode *)malloc(sizeof(ASTNode));
  memset(node, 0, sizeof(ASTNode));
  node->type = AST_STRING_LITERAL;
  node->value.string_value = strdup(val);
  return node;
}

ASTNode *mock_object_lit(char **keys, ASTNode **values, int count) {
  ASTNode *node = (ASTNode *)malloc(sizeof(ASTNode));
  memset(node, 0, sizeof(ASTNode));
  node->type = AST_OBJECT_LITERAL;
  node->object_keys = keys;
  node->object_values = values;
  node->object_count = count;
  return node;
}

ASTNode *mock_var_decl(const char *name, ASTNode *init) {
  ASTNode *node = (ASTNode *)malloc(sizeof(ASTNode));
  memset(node, 0, sizeof(ASTNode));
  node->type = AST_VARIABLE_DECL;
  node->name = strdup(name);
  node->right = init;
  return node;
}

ASTNode *mock_ident(const char *name) {
  ASTNode *node = (ASTNode *)malloc(sizeof(ASTNode));
  memset(node, 0, sizeof(ASTNode));
  node->type = AST_IDENTIFIER;
  node->name = strdup(name);
  return node;
}

ASTNode *mock_access(ASTNode *left, ASTNode *index) {
  ASTNode *node = (ASTNode *)malloc(sizeof(ASTNode));
  memset(node, 0, sizeof(ASTNode));
  node->type = AST_ARRAY_ACCESS;
  node->left = left;
  node->index = index;
  return node;
}

ASTNode *mock_assignment(ASTNode *left, ASTNode *right) {
  ASTNode *node = (ASTNode *)malloc(sizeof(ASTNode));
  memset(node, 0, sizeof(ASTNode));
  node->type = AST_ASSIGNMENT;
  node->left = left;
  node->right = right;
  return node;
}

// --- Main Test ---

int main() {
  printf("=== Tulpar LLVM Object Test ===\n");
  LLVMBackend *backend = llvm_backend_create("object_module");
  if (!backend) {
    printf("Failed to create backend.\n");
    return 1;
  }

  // Setup Function Context
  LLVMTypeRef ret_type = backend->vm_value_type;
  LLVMTypeRef param_types[] = {backend->ptr_type};
  LLVMTypeRef func_type = LLVMFunctionType(ret_type, param_types, 0, 0);
  LLVMValueRef main_func = LLVMAddFunction(backend->module, "main", func_type);
  backend->current_function = main_func;

  LLVMBasicBlockRef entry = LLVMAppendBasicBlock(main_func, "entry");
  LLVMPositionBuilderAtEnd(backend->builder, entry);
  enter_scope(backend);

  // 1. Create Object: obj = { "x": 10, "y": 20 }
  printf("Generating: obj = { \"x\": 10, \"y\": 20 }\n");
  char *keys[] = {"x", "y"};
  ASTNode *values[] = {mock_int(10), mock_int(20)};
  ASTNode *obj_lit = mock_object_lit(keys, values, 2);
  ASTNode *decl = mock_var_decl("obj", obj_lit);
  codegen_statement(backend, decl);

  // 2. Access Property: val = obj["x"]
  printf("Generating: val = obj[\"x\"]\n");
  ASTNode *access = mock_access(mock_ident("obj"), mock_string("x"));
  ASTNode *decl_val = mock_var_decl("val", access);
  codegen_statement(backend, decl_val);

  // 3. Set Property: obj["x"] = 99
  printf("Generating: obj[\"x\"] = 99\n");
  ASTNode *assign = mock_assignment(
      mock_access(mock_ident("obj"), mock_string("x")), mock_int(99));
  codegen_statement(backend, assign);

  // Return generic 0
  LLVMValueRef ret_val = llvm_vm_val_int(backend, 0);
  LLVMBuildRet(backend->builder, ret_val);

  exit_scope(backend);

  printf("Verifying Module...\n");
  char *error = NULL;
  // Note: LLVMVerifyModule might segfault if IR is extremely bad, but usually
  // okay.
  if (LLVMVerifyModule(backend->module, LLVMPrintMessageAction, &error)) {
    printf("Error verifying module: %s\n", error);
    return 1;
  }

  LLVMDumpModule(backend->module);
  printf("Test Complete.\n");
  return 0;
}
