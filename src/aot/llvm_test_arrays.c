#include "../parser/parser.h"
#include "llvm_backend.h"
#include "llvm_values.h"
#include <llvm-c/Analysis.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// AST Mock Helper
ASTNode *mock_int(long long val) {
  ASTNode *node = (ASTNode *)malloc(sizeof(ASTNode));
  memset(node, 0, sizeof(ASTNode));
  node->type = AST_INT_LITERAL;
  node->value.int_value = val;
  return node;
}

ASTNode *mock_array_lit(ASTNode **elements, int count) {
  ASTNode *node = (ASTNode *)malloc(sizeof(ASTNode));
  memset(node, 0, sizeof(ASTNode));
  node->type = AST_ARRAY_LITERAL;
  node->elements = elements;
  node->element_count = count;
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

int main() {
  printf("=== Tulpar LLVM Array Test ===\n");
  LLVMBackend *backend = llvm_backend_create("array_module");
  if (!backend) {
    printf("Failed to create backend.\n");
    return 1;
  }

  // Function: func main() { ... }
  LLVMTypeRef ret_type = backend->vm_value_type;
  LLVMTypeRef param_types[] = {backend->ptr_type}; // VM*
  LLVMTypeRef func_type = LLVMFunctionType(ret_type, param_types, 0,
                                           0); // No params for now in test main
  LLVMValueRef main_func = LLVMAddFunction(backend->module, "main", func_type);
  backend->current_function = main_func;

  LLVMBasicBlockRef entry = LLVMAppendBasicBlock(main_func, "entry");
  LLVMPositionBuilderAtEnd(backend->builder, entry);
  enter_scope(backend);

  // 1. arr = [10, 20, 30]
  printf("Generating: arr = [10, 20, 30]\n");
  ASTNode *elems[] = {mock_int(10), mock_int(20), mock_int(30)};
  ASTNode *arr_lit = mock_array_lit(elems, 3);
  ASTNode *decl = mock_var_decl("arr", arr_lit);
  codegen_statement(backend, decl);

  // 2. return arr
  // Mock AST_RETURN
  // For simplicity, just return a dummy 0 value to compile successfully
  LLVMValueRef ret_val = llvm_vm_val_int(backend, 0);
  LLVMBuildRet(backend->builder, ret_val);

  exit_scope(backend);

  printf("Verifying Module...\n");
  char *error = NULL;
  LLVMVerifyModule(backend->module, LLVMAbortProcessAction, &error);
  LLVMDumpModule(backend->module);

  printf("Test Complete.\n");
  return 0;
}
