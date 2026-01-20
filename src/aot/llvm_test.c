
#include "llvm_backend.h"
#include <stdio.h>

int main() {
  printf("=== Tulpar LLVM Backend Test ===\n");
  printf("Initializing Backend...\n");

  LLVMBackend *backend = llvm_backend_create("tulpar_test_module");

  if (!backend) {
    fprintf(stderr, "Failed to create backend!\n");
    return 1;
  }

  printf("Backend created successfully.\n");

  // Verify Types
  printf("Verifying Types:\n");
  if (backend->vm_value_type) {
    char *name = LLVMGetStructName(backend->vm_value_type);
    printf("  [OK] VMValue Type: %s\n", name ? name : "unnamed");
  } else {
    printf("  [FAIL] VMValue Type is NULL\n");
  }

  if (backend->obj_type) {
    char *name = LLVMGetStructName(backend->obj_type);
    printf("  [OK] Obj Type: %s\n", name ? name : "unnamed");
  } else {
    printf("  [FAIL] Obj Type is NULL\n");
  }

  if (backend->obj_string_type) {
    char *name = LLVMGetStructName(backend->obj_string_type);
    printf("  [OK] ObjString Type: %s\n", name ? name : "unnamed");
  } else {
    printf("  [FAIL] ObjString Type is NULL\n");
  }

  llvm_backend_destroy(backend);
  printf("\nTest Complete.\n");
  return 0;
}
