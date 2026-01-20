// Tulpar LLVM Backend - Core Interface

#ifndef TULPAR_LLVM_BACKEND_H
#define TULPAR_LLVM_BACKEND_H

#include "../parser/parser.h"
#include <llvm-c/Core.h>
#include <llvm-c/Target.h>
#include <llvm-c/TargetMachine.h>

// Scope definition
typedef struct {
  char *name;
  LLVMValueRef value;
} LocalVar;

typedef struct Scope {
  LocalVar vars[256];
  int count;
  struct Scope *parent;
} Scope;

typedef struct {
  char *name;
  LLVMTypeRef type;
} FunctionEntry;

typedef struct {
  LLVMContextRef context;
  LLVMModuleRef module;
  LLVMBuilderRef builder;

  // Type cache
  LLVMTypeRef int_type;
  LLVMTypeRef int32_type; // New for printf return
  LLVMTypeRef float_type;
  LLVMTypeRef bool_type;
  LLVMTypeRef void_type;
  LLVMTypeRef ptr_type;    // Generic pointer type (i8*)
  LLVMTypeRef string_type; // i8*

  // VM Types
  // VM Types
  LLVMTypeRef vm_value_type;   // struct VMValue
  LLVMTypeRef obj_type;        // struct Obj
  LLVMTypeRef obj_string_type; // struct ObjString

  // Runtime Functions
  LLVMValueRef func_printf;
  LLVMValueRef func_vm_alloc_string;
  LLVMValueRef func_print_value;  // Helper: print_value(VMValue)
  LLVMValueRef func_vm_binary_op; // Helper: vm_binary_op(VMValue, VMValue, int)

  // Array Runtime
  LLVMValueRef func_vm_allocate_array;
  LLVMValueRef func_vm_array_push;
  LLVMValueRef func_vm_array_get;
  LLVMValueRef func_vm_array_set;

  // Object/Generic Runtime
  LLVMValueRef func_vm_allocate_object;
  LLVMValueRef func_vm_object_set;  // Need this for object literals
  LLVMValueRef func_vm_get_element; // Generic get
  LLVMValueRef func_vm_set_element; // Generic set

  LLVMTypeRef vm_binary_op_type; // Explicitly store type

  LLVMValueRef current_function;
  Scope *current_scope;

  FunctionEntry functions[128];
  int function_count;

  // Import tracking
  char *imported_files[128];
  int imported_count;

  // AOT Builtin Functions
  LLVMValueRef func_aot_to_string;
  LLVMValueRef func_aot_to_int;
  LLVMValueRef func_aot_to_float;
  LLVMValueRef func_aot_len;
  LLVMValueRef func_aot_array_push;
  LLVMValueRef func_aot_array_pop;
  LLVMValueRef func_aot_to_json;
  LLVMValueRef func_aot_input;
  LLVMValueRef func_aot_trim;
  LLVMValueRef func_aot_replace;
  LLVMValueRef func_aot_split;

  LLVMValueRef func_aot_read_file;
  LLVMValueRef func_aot_write_file;
  LLVMValueRef func_aot_append_file;
  LLVMValueRef func_aot_file_exists;

  // Exception Handling
  LLVMValueRef func_aot_try_push;
  LLVMValueRef func_aot_try_pop;
  LLVMValueRef func_aot_throw;
  LLVMValueRef func_aot_get_exception;
  LLVMValueRef func_setjmp;

} LLVMBackend;

LLVMBackend *llvm_backend_create(const char *module_name);
void llvm_backend_destroy(LLVMBackend *backend);
void llvm_backend_compile(LLVMBackend *backend, ASTNode *node);
// void llvm_backend_optimize(LLVMBackend* backend); // Removed to fix build
// error
int llvm_backend_emit_object(LLVMBackend *backend, const char *filename);
int llvm_backend_emit_ir_file(LLVMBackend *backend, const char *filename);

// Helper functions for testing
void enter_scope(LLVMBackend *backend);
void exit_scope(LLVMBackend *backend);
void add_local(LLVMBackend *backend, const char *name, LLVMValueRef val);
LLVMValueRef get_local(LLVMBackend *backend, const char *name);
LLVMValueRef codegen_expression(LLVMBackend *backend, ASTNode *node);
LLVMValueRef codegen_statement(LLVMBackend *backend, ASTNode *node);

#endif
