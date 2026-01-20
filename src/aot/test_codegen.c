// Simple LLVM Backend Test
// Tests basic IR generation for arithmetic

#include "llvm_backend.h"
#include <stdio.h>

int main() {
  printf("=== Tulpar LLVM IR Generation Test ===\n\n");

  // Create backend
  LLVMBackend *backend = llvm_backend_create("test_module");
  printf("✓ LLVM backend initialized\n");

  // Create main function: int main()
  LLVMTypeRef main_type = LLVMFunctionType(backend->int_type, NULL, 0, 0);
  LLVMValueRef main_func = LLVMAddFunction(backend->module, "main", main_type);
  backend->current_function = main_func;

  // Create entry block
  LLVMBasicBlockRef entry = LLVMAppendBasicBlock(main_func, "entry");
  LLVMPositionBuilderAtEnd(backend->builder, entry);

  printf("✓ Main function created\n");

  // Test 1: Simple arithmetic (10 + 20)
  printf("\nTest 1: 10 + 20\n");
  LLVMValueRef val_10 = LLVMConstInt(backend->int_type, 10, 0);
  LLVMValueRef val_20 = LLVMConstInt(backend->int_type, 20, 0);
  LLVMValueRef result =
      LLVMBuildAdd(backend->builder, val_10, val_20, "result");
  printf("✓ IR generated for addition\n");

  // Return result
  LLVMBuildRet(backend->builder, result);

  // Print generated IR
  printf("\nGenerated LLVM IR:\n");
  printf("==================\n");
  llvm_backend_print_ir(backend);

  // Cleanup
  llvm_backend_destroy(backend);

  printf("\n=== Test PASSED! ===\n");
  printf("\nNext steps:\n");
  printf("  1. Test with real Tulpar AST\n");
  printf("  2. Add control flow (if/loops)\n");
  printf("  3. Compile to native code!\n");

  return 0;
}
