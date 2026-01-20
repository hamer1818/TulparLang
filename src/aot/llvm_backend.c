// Tulpar LLVM Backend - Implementation
// Phase 3 Fix: Printf types

#define _CRT_SECURE_NO_WARNINGS
#define _CRT_NONSTDC_NO_DEPRECATE

#include "llvm_backend.h"
#include "llvm_types.h"
#include "llvm_values.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

char *my_strdup(const char *s) {
  if (!s)
    return NULL;
  size_t len = strlen(s);
  char *copy = (char *)malloc(len + 1);
  if (copy)
    strcpy(copy, s);
  return copy;
}

// Declare external runtime functions
void declare_runtime_functions(LLVMBackend *backend) {
  // printf: i32 printf(i8*, ...)
  LLVMTypeRef printf_params[] = {backend->string_type};
  LLVMTypeRef printf_type =
      LLVMFunctionType(backend->int32_type, printf_params, 1, 1);
  backend->func_printf =
      LLVMAddFunction(backend->module, "printf", printf_type);

  // vm_alloc_string: ObjString* vm_alloc_string(VM*, i8*, i32)
  // We'll treat VM* as void* (i8*) for now since we don't need its fields here
  // yet
  LLVMTypeRef alloc_str_params[] = {
      backend->ptr_type,    // VM* vm
      backend->string_type, // char* chars
      backend->int32_type   // int length
  };
  LLVMTypeRef alloc_str_type = LLVMFunctionType(
      LLVMPointerType(backend->obj_string_type, 0), alloc_str_params, 3, 0);
  backend->func_vm_alloc_string =
      LLVMAddFunction(backend->module, "vm_alloc_string_aot", alloc_str_type);

  // print_value: void print_value(VMValue)
  // VMValue is passed by value (struct)
  LLVMTypeRef print_val_params[] = {backend->vm_value_type};
  LLVMTypeRef print_val_type =
      LLVMFunctionType(backend->void_type, print_val_params, 1, 0);
  backend->func_print_value =
      LLVMAddFunction(backend->module, "print_value", print_val_type);

  // vm_binary_op: void vm_binary_op(VM* vm, VMValue* a, VMValue* b, int op,
  // VMValue* result)
  LLVMTypeRef bin_op_params[] = {backend->ptr_type, backend->ptr_type,
                                 backend->ptr_type, backend->int32_type,
                                 backend->ptr_type};
  LLVMTypeRef bin_op_type =
      LLVMFunctionType(backend->void_type, bin_op_params, 5, 0);
  backend->vm_binary_op_type = bin_op_type; // Store type
  backend->func_vm_binary_op =
      LLVMAddFunction(backend->module, "vm_binary_op", bin_op_type);

  // Array Functions
  // vm_allocate_array(VM*) -> ObjArray*
  LLVMTypeRef alloc_arr_params[] = {backend->ptr_type};
  LLVMTypeRef alloc_arr_type = LLVMFunctionType(
      backend->ptr_type, alloc_arr_params, 1, 0); // ObjArray* is ptr
  backend->func_vm_allocate_array = LLVMAddFunction(
      backend->module, "vm_allocate_array_aot_wrapper", alloc_arr_type);

  // vm_array_push(VM*, ObjArray*, VMValue)
  LLVMTypeRef push_params[] = {backend->ptr_type, backend->ptr_type,
                               backend->vm_value_type};
  // vm_array_push (void return in stub/wrapper) - wait, wrapper is
  // vm_array_push_wrapper Wrapper signature: void(VM*, ObjArray*, VMValue)
  LLVMTypeRef push_type =
      LLVMFunctionType(backend->void_type, push_params, 3, 0);
  backend->func_vm_array_push =
      LLVMAddFunction(backend->module, "vm_array_push_aot_wrapper", push_type);

  // vm_array_get(ObjArray*, int) -> VMValue
  LLVMTypeRef get_params[] = {backend->ptr_type, backend->int32_type};
  LLVMTypeRef get_type =
      LLVMFunctionType(backend->vm_value_type, get_params, 2, 0);
  backend->func_vm_array_get =
      LLVMAddFunction(backend->module, "vm_array_get", get_type);

  // vm_array_set(ObjArray*, int, VMValue)
  LLVMTypeRef set_params[] = {backend->ptr_type, backend->int32_type,
                              backend->vm_value_type};
  LLVMTypeRef set_type = LLVMFunctionType(backend->void_type, set_params, 3, 0);
  backend->func_vm_array_set =
      LLVMAddFunction(backend->module, "vm_array_set", set_type);

  // Object/Generic Functions

  // vm_allocate_object(VM*) -> ObjObject*
  LLVMTypeRef alloc_obj_params[] = {backend->ptr_type};
  LLVMTypeRef alloc_obj_type =
      LLVMFunctionType(backend->ptr_type, alloc_obj_params, 1, 0);
  backend->func_vm_allocate_object = LLVMAddFunction(
      backend->module, "vm_allocate_object_aot_wrapper", alloc_obj_type);

  // vm_object_set(VM*, ObjObject*, char*, VMValue)
  LLVMTypeRef obj_set_params[] = {backend->ptr_type, backend->ptr_type,
                                  backend->ptr_type, backend->vm_value_type};
  LLVMTypeRef obj_set_type =
      LLVMFunctionType(backend->void_type, obj_set_params, 4, 0);
  backend->func_vm_object_set = LLVMAddFunction(
      backend->module, "vm_object_set_aot_wrapper", obj_set_type);

  // vm_get_element(VMValue, VMValue) -> VMValue
  LLVMTypeRef get_el_params[] = {backend->vm_value_type,
                                 backend->vm_value_type};
  LLVMTypeRef get_el_type =
      LLVMFunctionType(backend->vm_value_type, get_el_params, 2, 0);
  backend->func_vm_get_element =
      LLVMAddFunction(backend->module, "vm_get_element", get_el_type);

  // vm_set_element(VM*, VMValue, VMValue, VMValue)
  LLVMTypeRef set_el_params[] = {backend->ptr_type, backend->vm_value_type,
                                 backend->vm_value_type,
                                 backend->vm_value_type};
  LLVMTypeRef set_el_type =
      LLVMFunctionType(backend->void_type, set_el_params, 4, 0);
  backend->func_vm_set_element =
      LLVMAddFunction(backend->module, "vm_set_element", set_el_type);

  // ====== AOT Builtin Functions ======

  // aot_to_string(VMValue) -> char*
  LLVMTypeRef to_str_params[] = {backend->vm_value_type};
  LLVMTypeRef to_str_type =
      LLVMFunctionType(backend->vm_value_type, to_str_params, 1, 0);
  backend->func_aot_to_string =
      LLVMAddFunction(backend->module, "aot_to_string", to_str_type);

  // aot_to_int(VMValue) -> int64
  LLVMTypeRef to_int_params[] = {backend->vm_value_type};
  LLVMTypeRef to_int_type =
      LLVMFunctionType(backend->int_type, to_int_params, 1, 0);
  backend->func_aot_to_int =
      LLVMAddFunction(backend->module, "aot_to_int", to_int_type);

  // aot_to_json(VMValue) -> VMValue
  LLVMTypeRef to_json_params[] = {backend->vm_value_type};
  LLVMTypeRef to_json_type =
      LLVMFunctionType(backend->vm_value_type, to_json_params, 1, 0);
  backend->func_aot_to_json =
      LLVMAddFunction(backend->module, "aot_to_json", to_json_type);

  // aot_to_float(VMValue) -> double
  LLVMTypeRef to_float_params[] = {backend->vm_value_type};
  LLVMTypeRef to_float_type =
      LLVMFunctionType(backend->float_type, to_float_params, 1, 0);
  backend->func_aot_to_float =
      LLVMAddFunction(backend->module, "aot_to_float", to_float_type);

  // aot_len(VMValue) -> int64
  LLVMTypeRef len_params[] = {backend->vm_value_type};
  LLVMTypeRef len_type = LLVMFunctionType(backend->int_type, len_params, 1, 0);
  backend->func_aot_len = LLVMAddFunction(backend->module, "aot_len", len_type);

  // aot_array_push(VMValue, VMValue) -> void
  LLVMTypeRef push_aot_params[] = {backend->vm_value_type,
                                   backend->vm_value_type};
  LLVMTypeRef push_aot_type =
      LLVMFunctionType(backend->void_type, push_aot_params, 2, 0);
  backend->func_aot_array_push =
      LLVMAddFunction(backend->module, "aot_array_push", push_aot_type);

  // aot_array_pop(VMValue) -> VMValue
  LLVMTypeRef pop_params[] = {backend->vm_value_type};
  LLVMTypeRef pop_type =
      LLVMFunctionType(backend->vm_value_type, pop_params, 1, 0);
  backend->func_aot_array_pop =
      LLVMAddFunction(backend->module, "aot_array_pop", pop_type);

  // aot_input() -> VMValue
  LLVMTypeRef input_type = LLVMFunctionType(backend->vm_value_type, NULL, 0, 0);
  backend->func_aot_input =
      LLVMAddFunction(backend->module, "aot_input", input_type);

  // aot_trim(VMValue) -> VMValue
  LLVMTypeRef trim_params[] = {backend->vm_value_type};
  LLVMTypeRef trim_type =
      LLVMFunctionType(backend->vm_value_type, trim_params, 1, 0);
  backend->func_aot_trim =
      LLVMAddFunction(backend->module, "aot_trim", trim_type);

  // aot_replace(VMValue, VMValue, VMValue) -> VMValue
  LLVMTypeRef replace_params[] = {
      backend->vm_value_type, backend->vm_value_type, backend->vm_value_type};
  LLVMTypeRef replace_type =
      LLVMFunctionType(backend->vm_value_type, replace_params, 3, 0);
  backend->func_aot_replace =
      LLVMAddFunction(backend->module, "aot_replace", replace_type);

  // aot_split(VMValue, VMValue) -> VMValue
  LLVMTypeRef split_params[] = {backend->vm_value_type, backend->vm_value_type};
  LLVMTypeRef split_type =
      LLVMFunctionType(backend->vm_value_type, split_params, 2, 0);
  backend->func_aot_split =
      LLVMAddFunction(backend->module, "aot_split", split_type);

  // File I/O Functions
  // aot_read_file(path) -> VMValue
  LLVMTypeRef read_params[] = {backend->vm_value_type};
  LLVMTypeRef read_type =
      LLVMFunctionType(backend->vm_value_type, read_params, 1, 0);
  backend->func_aot_read_file =
      LLVMAddFunction(backend->module, "aot_read_file", read_type);

  // aot_write_file(path, content) -> VMValue
  LLVMTypeRef write_params[] = {backend->vm_value_type, backend->vm_value_type};
  LLVMTypeRef write_type =
      LLVMFunctionType(backend->vm_value_type, write_params, 2, 0);
  backend->func_aot_write_file =
      LLVMAddFunction(backend->module, "aot_write_file", write_type);

  // aot_append_file(path, content) -> VMValue
  backend->func_aot_append_file =
      LLVMAddFunction(backend->module, "aot_append_file", write_type);

  backend->func_aot_file_exists =
      LLVMAddFunction(backend->module, "aot_file_exists", read_type);

  // Exception Handling Functions
  // aot_try_push() -> jmp_buf* (ptr)
  LLVMTypeRef try_push_type = LLVMFunctionType(backend->ptr_type, NULL, 0, 0);
  backend->func_aot_try_push =
      LLVMAddFunction(backend->module, "aot_try_push", try_push_type);

  // aot_try_pop() -> void
  LLVMTypeRef try_pop_type = LLVMFunctionType(backend->void_type, NULL, 0, 0);
  backend->func_aot_try_pop =
      LLVMAddFunction(backend->module, "aot_try_pop", try_pop_type);

  // aot_throw(VMValue) -> void (noreturn)
  LLVMTypeRef throw_params[] = {backend->vm_value_type};
  LLVMTypeRef throw_type =
      LLVMFunctionType(backend->void_type, throw_params, 1, 0);
  backend->func_aot_throw =
      LLVMAddFunction(backend->module, "aot_throw", throw_type);

  // aot_get_exception() -> VMValue
  LLVMTypeRef get_exc_type =
      LLVMFunctionType(backend->vm_value_type, NULL, 0, 0);
  backend->func_aot_get_exception =
      LLVMAddFunction(backend->module, "aot_get_exception", get_exc_type);

  // setjmp(jmp_buf*) -> int
  LLVMTypeRef setjmp_params[] = {backend->ptr_type};
  LLVMTypeRef setjmp_type =
      LLVMFunctionType(backend->int32_type, setjmp_params, 1, 0);
  backend->func_setjmp =
      LLVMAddFunction(backend->module, "setjmp", setjmp_type);
}

LLVMBackend *llvm_backend_create(const char *module_name) {
  LLVMBackend *backend = (LLVMBackend *)malloc(sizeof(LLVMBackend));
  LLVMInitializeNativeTarget();

  backend->context = LLVMContextCreate();
  backend->module =
      LLVMModuleCreateWithNameInContext(module_name, backend->context);
  backend->builder = LLVMCreateBuilderInContext(backend->context);

  backend->int_type = LLVMInt64TypeInContext(backend->context);
  backend->int32_type = LLVMInt32TypeInContext(backend->context); // i32
  backend->float_type = LLVMDoubleTypeInContext(backend->context);
  backend->bool_type = LLVMInt1TypeInContext(backend->context);
  backend->void_type = LLVMVoidTypeInContext(backend->context);
  backend->ptr_type =
      LLVMPointerType(LLVMInt8TypeInContext(backend->context), 0);
  backend->string_type = backend->ptr_type;

  // Initialize VM Types
  llvm_init_types(backend);

  backend->current_function = NULL;
  backend->current_scope = NULL;
  backend->current_function = NULL;
  backend->current_scope = NULL;
  backend->function_count = 0;
  backend->imported_count = 0;

  // Declare Runtime
  declare_runtime_functions(backend);

  return backend;
}

void llvm_backend_destroy(LLVMBackend *backend) {
  if (!backend)
    return;
  LLVMDisposeBuilder(backend->builder);
  LLVMDisposeModule(backend->module);
  LLVMContextDispose(backend->context);
  for (int i = 0; i < backend->function_count; i++)
    free(backend->functions[i].name);
  free(backend);
}

void register_function(LLVMBackend *backend, const char *name,
                       LLVMTypeRef type) {
  if (backend->function_count < 128) {
    backend->functions[backend->function_count].name = my_strdup(name);
    backend->functions[backend->function_count].type = type;
    backend->function_count++;
  }
}

LLVMTypeRef get_function_type(LLVMBackend *backend, const char *name) {
  if (strcmp(name, "print") == 0 || strcmp(name, "printf") == 0) {
    LLVMValueRef f = LLVMGetNamedFunction(backend->module, "printf");
    if (f)
      return LLVMGlobalGetValueType(f);
  }
  for (int i = 0; i < backend->function_count; i++) {
    if (strcmp(backend->functions[i].name, name) == 0)
      return backend->functions[i].type;
  }
  return NULL;
}

void enter_scope(LLVMBackend *backend) {
  Scope *scope = (Scope *)malloc(sizeof(Scope));
  scope->count = 0;
  scope->parent = backend->current_scope;
  backend->current_scope = scope;
}

void exit_scope(LLVMBackend *backend) {
  if (backend->current_scope) {
    Scope *old = backend->current_scope;
    backend->current_scope = old->parent;
    for (int i = 0; i < old->count; i++)
      free(old->vars[i].name);
    free(old);
  }
}

void add_local(LLVMBackend *backend, const char *name, LLVMValueRef val) {
  if (!backend->current_scope)
    return;
  Scope *s = backend->current_scope;
  if (s->count < 256) {
    s->vars[s->count].name = my_strdup(name);
    s->vars[s->count].value = val;
    s->count++;
  }
}

LLVMValueRef get_local(LLVMBackend *backend, const char *name) {
  Scope *s = backend->current_scope;
  while (s) {
    for (int i = 0; i < s->count; i++) {
      if (strcmp(s->vars[i].name, name) == 0)
        return s->vars[i].value;
    }
    s = s->parent;
  }
  return NULL;
}

LLVMValueRef codegen_expression(LLVMBackend *backend, ASTNode *node);
LLVMValueRef codegen_statement(LLVMBackend *backend, ASTNode *node);

LLVMValueRef codegen_expression(LLVMBackend *backend, ASTNode *node) {
  if (!node)
    return NULL;

  switch (node->type) {
  case AST_INT_LITERAL:
    return llvm_vm_val_int(backend, node->value.int_value);

  case AST_FLOAT_LITERAL: {
    LLVMValueRef f =
        LLVMConstReal(backend->float_type, node->value.float_value);
    return llvm_build_vm_val_float(backend, f);
  }

  case AST_BOOL_LITERAL:
    return llvm_vm_val_bool(backend, node->value.bool_value);

  case AST_STRING_LITERAL: {
    // Call runtime: vm_alloc_string(vm, "str", len)
    // For now passing NULL as VM context (dangerous but temp)
    LLVMValueRef const_str = LLVMBuildGlobalStringPtr(
        backend->builder, node->value.string_value, "str_lit");
    int len = strlen(node->value.string_value);

    LLVMValueRef args[] = {LLVMConstNull(backend->ptr_type), // vm (null)
                           const_str,
                           LLVMConstInt(backend->int32_type, len, 0)};

    LLVMValueRef str_obj = LLVMBuildCall2(
        backend->builder, LLVMGlobalGetValueType(backend->func_vm_alloc_string),
        backend->func_vm_alloc_string, args, 3, "alloc_str");
    return llvm_build_vm_val_obj(backend, str_obj);
  }

  case AST_ARRAY_LITERAL: {
    // 1. Allocate Array: vm_allocate_array(vm)
    LLVMValueRef alloc_args[] = {LLVMConstNull(backend->ptr_type)};
    LLVMValueRef arr_obj = LLVMBuildCall2(
        backend->builder,
        LLVMGlobalGetValueType(backend->func_vm_allocate_array),
        backend->func_vm_allocate_array, alloc_args, 1, "alloc_arr");

    // 2. Loop elements and push: vm_array_push_wrapper(vm, arr, val)
    if (node->elements) {
      for (int i = 0; i < node->element_count; i++) {
        LLVMValueRef val = codegen_expression(backend, node->elements[i]);
        LLVMValueRef push_args[] = {LLVMConstNull(backend->ptr_type), arr_obj,
                                    val};
        LLVMBuildCall2(backend->builder,
                       LLVMGlobalGetValueType(backend->func_vm_array_push),
                       backend->func_vm_array_push, push_args, 3, "");
      }
    }
    return llvm_build_vm_val_obj(backend, arr_obj);
  }

  case AST_OBJECT_LITERAL: {
    // 1. Allocate Object: vm_allocate_object(NULL)
    LLVMValueRef args[] = {LLVMConstPointerNull(backend->ptr_type)};
    LLVMValueRef obj_ptr =
        LLVMBuildCall2(backend->builder,
                       LLVMGlobalGetValueType(backend->func_vm_allocate_object),
                       backend->func_vm_allocate_object, args, 1, "alloc_obj");

    // 2. Iterate keys/values and Set
    for (int i = 0; i < node->object_count; i++) {
      char *key_str = node->object_keys[i];
      LLVMValueRef key_global =
          LLVMBuildGlobalStringPtr(backend->builder, key_str, "key_str");

      LLVMValueRef val = codegen_expression(backend, node->object_values[i]);

      LLVMValueRef set_args[] = {
          LLVMConstPointerNull(backend->ptr_type), // VM*
          obj_ptr,                                 // ObjObject*
          key_global,                              // char* key
          val                                      // VMValue value
      };
      LLVMBuildCall2(backend->builder,
                     LLVMGlobalGetValueType(backend->func_vm_object_set),
                     backend->func_vm_object_set, set_args, 4, "");
    }

    return llvm_build_vm_val_obj(backend, obj_ptr);
  }

  case AST_ARRAY_ACCESS: {
    LLVMValueRef left_val = NULL;

    // Parser stores first identifier access in node->name, not node->left
    if (node->name) {
      LLVMValueRef val_ptr = get_local(backend, node->name);
      if (val_ptr) {
        left_val = LLVMBuildLoad2(backend->builder, backend->vm_value_type,
                                  val_ptr, node->name);
      } else {
        fprintf(stderr, "Undefined var in array access: %s\n", node->name);
      }
    } else if (node->left) {
      // Nested access uses node->left
      left_val = codegen_expression(backend, node->left);
    }

    LLVMValueRef idx_val = codegen_expression(backend, node->index);

    // Fallback to avoid crash
    if (!left_val)
      left_val = llvm_vm_val_int(backend, 0);
    if (!idx_val)
      idx_val = llvm_vm_val_int(backend, 0);

    LLVMValueRef args[] = {left_val, idx_val};
    return LLVMBuildCall2(backend->builder,
                          LLVMGlobalGetValueType(backend->func_vm_get_element),
                          backend->func_vm_get_element, args, 2, "element_val");
  }

  case AST_IDENTIFIER: {
    // printf("[AOT-DEBUG] AST_IDENTIFIER: %s\n", node->name);
    LLVMValueRef val_ptr = get_local(backend, node->name);
    if (val_ptr) {
      // Load the full VMValue struct
      return LLVMBuildLoad2(backend->builder, backend->vm_value_type, val_ptr,
                            node->name);
    }
    fprintf(stderr, "Undefined var: %s\n", node->name);
    return NULL;
  }

  // TODO: Binary Ops Update
  case AST_BINARY_OP: {
    LLVMValueRef L = codegen_expression(backend, node->left);
    LLVMValueRef R = codegen_expression(backend, node->right);
    if (!L || !R)
      return llvm_vm_val_int(backend, 0);

    // AOT: VM context is not passed to functions yet, so pass NULL.
    LLVMValueRef vm_ptr = LLVMConstPointerNull(backend->ptr_type);

    // Create stack slots for L and R to pass by pointer
    LLVMValueRef L_ptr =
        LLVMBuildAlloca(backend->builder, backend->vm_value_type, "L_ptr");
    LLVMBuildStore(backend->builder, L, L_ptr);
    // Bitcast to i8* (generic ptr) consistent with runtime signature
    LLVMValueRef L_void =
        LLVMBuildBitCast(backend->builder, L_ptr, backend->ptr_type, "L_void");

    LLVMValueRef R_ptr =
        LLVMBuildAlloca(backend->builder, backend->vm_value_type, "R_ptr");
    LLVMBuildStore(backend->builder, R, R_ptr);
    LLVMValueRef R_void =
        LLVMBuildBitCast(backend->builder, R_ptr, backend->ptr_type, "R_void");

    // Result Slot
    LLVMValueRef res_ptr =
        LLVMBuildAlloca(backend->builder, backend->vm_value_type, "res_ptr");
    LLVMValueRef res_void = LLVMBuildBitCast(backend->builder, res_ptr,
                                             backend->ptr_type, "res_void");

    LLVMValueRef args[] = {vm_ptr, L_void, R_void,
                           LLVMConstInt(backend->int32_type, node->op, 0),
                           res_void};

    LLVMBuildCall2(backend->builder,
                   backend->vm_binary_op_type, // Use cached type
                   backend->func_vm_binary_op, args, 5, "");

    // Load result
    return LLVMBuildLoad2(backend->builder, backend->vm_value_type, res_ptr,
                          "bin_op_res");
  }

  // TODO: Function Call Update
  case AST_FUNCTION_CALL: {
    // Check for builtin "print" function
    if (node->name && strcmp(node->name, "print") == 0) {
      // Generate print calls for each argument
      for (int i = 0; i < node->argument_count; i++) {
        ASTNode *arg = node->arguments[i];

        // Special case: string literal - use printf directly
        if (arg->type == AST_STRING_LITERAL) {
          LLVMValueRef fmt =
              LLVMBuildGlobalStringPtr(backend->builder, "%s\n", "fmt_str");
          LLVMValueRef str = LLVMBuildGlobalStringPtr(
              backend->builder, arg->value.string_value, "str_arg");
          LLVMValueRef printf_args[] = {fmt, str};
          LLVMBuildCall2(backend->builder,
                         LLVMGlobalGetValueType(backend->func_printf),
                         backend->func_printf, printf_args, 2, "");
        } else {
          // Other types: use print_value(VMValue)
          LLVMValueRef arg_val = codegen_expression(backend, arg);
          if (arg_val) {
            LLVMValueRef args[] = {arg_val};
            LLVMBuildCall2(backend->builder,
                           LLVMGlobalGetValueType(backend->func_print_value),
                           backend->func_print_value, args, 1, "");
          }
        }
      }
      return llvm_vm_val_int(backend, 0); // void return
    }

    // ====== Builtin Functions ======

    // toString(value) -> string (for print, returns char*)
    if (node->name && strcmp(node->name, "toString") == 0 &&
        node->argument_count >= 1) {
      LLVMValueRef arg = codegen_expression(backend, node->arguments[0]);
      LLVMValueRef args[] = {arg};
      LLVMValueRef result = LLVMBuildCall2(
          backend->builder, LLVMGlobalGetValueType(backend->func_aot_to_string),
          backend->func_aot_to_string, args, 1, "to_str");
      return result;
    }

    // toInt(value) -> int
    if (node->name && strcmp(node->name, "toInt") == 0 &&
        node->argument_count >= 1) {
      LLVMValueRef arg = codegen_expression(backend, node->arguments[0]);
      LLVMValueRef args[] = {arg};
      LLVMValueRef result = LLVMBuildCall2(
          backend->builder, LLVMGlobalGetValueType(backend->func_aot_to_int),
          backend->func_aot_to_int, args, 1, "to_int");
      return llvm_vm_val_int_val(backend, result);
    }

    // toJson(value) -> String
    if (node->name && strcmp(node->name, "toJson") == 0 &&
        node->argument_count >= 1) {
      LLVMValueRef arg = codegen_expression(backend, node->arguments[0]);
      LLVMValueRef args[] = {arg};
      LLVMValueRef result = LLVMBuildCall2(
          backend->builder, LLVMGlobalGetValueType(backend->func_aot_to_json),
          backend->func_aot_to_json, args, 1, "to_json");
      return result;
    }

    // toFloat(value) -> float
    if (node->name && strcmp(node->name, "toFloat") == 0 &&
        node->argument_count >= 1) {
      LLVMValueRef arg = codegen_expression(backend, node->arguments[0]);
      LLVMValueRef args[] = {arg};
      LLVMValueRef result = LLVMBuildCall2(
          backend->builder, LLVMGlobalGetValueType(backend->func_aot_to_float),
          backend->func_aot_to_float, args, 1, "to_float");
      return llvm_build_vm_val_float(backend, result);
    }

    // len(value) -> int
    if (node->name && strcmp(node->name, "len") == 0 &&
        node->argument_count >= 1) {
      LLVMValueRef arg = codegen_expression(backend, node->arguments[0]);
      LLVMValueRef args[] = {arg};
      LLVMValueRef result = LLVMBuildCall2(
          backend->builder, LLVMGlobalGetValueType(backend->func_aot_len),
          backend->func_aot_len, args, 1, "len_result");
      return llvm_vm_val_int_val(backend, result);
    }

    // length(value) -> int (alias for len)
    if (node->name && strcmp(node->name, "length") == 0 &&
        node->argument_count >= 1) {
      LLVMValueRef arg = codegen_expression(backend, node->arguments[0]);
      LLVMValueRef args[] = {arg};
      LLVMValueRef result = LLVMBuildCall2(
          backend->builder, LLVMGlobalGetValueType(backend->func_aot_len),
          backend->func_aot_len, args, 1, "len_result");
      return llvm_vm_val_int_val(backend, result);
    }

    // push(array, value)
    if (node->name && strcmp(node->name, "push") == 0 &&
        node->argument_count >= 2) {
      LLVMValueRef arr = codegen_expression(backend, node->arguments[0]);
      LLVMValueRef val = codegen_expression(backend, node->arguments[1]);
      LLVMValueRef args[] = {arr, val};
      LLVMBuildCall2(backend->builder,
                     LLVMGlobalGetValueType(backend->func_aot_array_push),
                     backend->func_aot_array_push, args, 2, "");
      return llvm_vm_val_int(backend, 0);
    }

    // pop(array) -> value
    if (node->name && strcmp(node->name, "pop") == 0 &&
        node->argument_count >= 1) {
      LLVMValueRef arr = codegen_expression(backend, node->arguments[0]);
      LLVMValueRef args[] = {arr};
      return LLVMBuildCall2(backend->builder,
                            LLVMGlobalGetValueType(backend->func_aot_array_pop),
                            backend->func_aot_array_pop, args, 1, "pop_result");
    }

    // input() -> String
    if (node->name && strcmp(node->name, "input") == 0) {
      if (!backend->func_aot_input)
        fprintf(stderr, "Fatal: func_aot_input is null\n");
      LLVMValueRef result = LLVMBuildCall2(
          backend->builder, LLVMGlobalGetValueType(backend->func_aot_input),
          backend->func_aot_input, NULL, 0, "input_res");
      return result;
    }

    // trim(str) -> String
    if (node->name && strcmp(node->name, "trim") == 0 &&
        node->argument_count >= 1) {
      LLVMValueRef arg = codegen_expression(backend, node->arguments[0]);
      LLVMValueRef args[] = {arg};
      LLVMValueRef result = LLVMBuildCall2(
          backend->builder, LLVMGlobalGetValueType(backend->func_aot_trim),
          backend->func_aot_trim, args, 1, "trim_res");
      return result;
    }

    // replace(str, old, new) -> String
    if (node->name && strcmp(node->name, "replace") == 0 &&
        node->argument_count >= 3) {
      LLVMValueRef str = codegen_expression(backend, node->arguments[0]);
      LLVMValueRef oldVal = codegen_expression(backend, node->arguments[1]);
      LLVMValueRef newVal = codegen_expression(backend, node->arguments[2]);
      LLVMValueRef args[] = {str, oldVal, newVal};
      LLVMValueRef result = LLVMBuildCall2(
          backend->builder, LLVMGlobalGetValueType(backend->func_aot_replace),
          backend->func_aot_replace, args, 3, "replace_res");
      return result;
    }

    // split(str, del) -> Array
    if (node->name && strcmp(node->name, "split") == 0 &&
        node->argument_count >= 2) {
      LLVMValueRef str = codegen_expression(backend, node->arguments[0]);
      LLVMValueRef del = codegen_expression(backend, node->arguments[1]);
      LLVMValueRef args[] = {str, del};
      LLVMValueRef result = LLVMBuildCall2(
          backend->builder, LLVMGlobalGetValueType(backend->func_aot_split),
          backend->func_aot_split, args, 2, "split_res");
      return result;
    }

    if (strcmp(node->name, "read_file") == 0) {
      LLVMValueRef args[] = {codegen_expression(backend, node->arguments[0])};
      return LLVMBuildCall2(backend->builder,
                            LLVMGlobalGetValueType(backend->func_aot_read_file),
                            backend->func_aot_read_file, args, 1, "read_res");
    }
    if (strcmp(node->name, "write_file") == 0) {
      LLVMValueRef args[] = {codegen_expression(backend, node->arguments[0]),
                             codegen_expression(backend, node->arguments[1])};
      return LLVMBuildCall2(
          backend->builder,
          LLVMGlobalGetValueType(backend->func_aot_write_file),
          backend->func_aot_write_file, args, 2, "write_res");
    }
    if (strcmp(node->name, "append_file") == 0) {
      LLVMValueRef args[] = {codegen_expression(backend, node->arguments[0]),
                             codegen_expression(backend, node->arguments[1])};
      return LLVMBuildCall2(
          backend->builder,
          LLVMGlobalGetValueType(backend->func_aot_append_file),
          backend->func_aot_append_file, args, 2, "append_res");
    }
    if (strcmp(node->name, "file_exists") == 0) {
      LLVMValueRef args[] = {codegen_expression(backend, node->arguments[0])};
      return LLVMBuildCall2(
          backend->builder,
          LLVMGlobalGetValueType(backend->func_aot_file_exists),
          backend->func_aot_file_exists, args, 1, "exists_res");
    }

    // User-defined function call
    // (Existing code)

    LLVMValueRef func = LLVMGetNamedFunction(backend->module, node->name);
    if (func) {
      LLVMTypeRef func_type = get_function_type(backend, node->name);
      if (!func_type) {
        func_type = LLVMGlobalGetValueType(func);
      }

      // Prepare arguments
      LLVMValueRef *args =
          malloc(sizeof(LLVMValueRef) * (node->argument_count + 1));
      int arg_offset = 0;

      // If function expects VM* as first arg, pass null for now
      unsigned param_count = LLVMCountParams(func);
      if (param_count > 0 && node->argument_count < (int)param_count) {
        args[0] = LLVMConstPointerNull(backend->ptr_type);
        arg_offset = 1;
      }

      for (int i = 0; i < node->argument_count; i++) {
        args[i + arg_offset] = codegen_expression(backend, node->arguments[i]);
      }

      LLVMValueRef result =
          LLVMBuildCall2(backend->builder, func_type, func, args,
                         node->argument_count + arg_offset, "call_result");
      free(args);
      return result;
    }

    fprintf(stderr, "Unknown function: %s\n", node->name);
    return llvm_vm_val_int(backend, 0);
  }

  default:
    return llvm_vm_val_int(backend, 0); // null/void
  }
}

LLVMValueRef codegen_statement(LLVMBackend *backend, ASTNode *node) {
  if (!node)
    return NULL;
  switch (node->type) {
  case AST_VARIABLE_DECL: {
    LLVMValueRef init = node->right ? codegen_expression(backend, node->right)
                                    : llvm_vm_val_int(backend, 0);
    LLVMValueRef alloca =
        LLVMBuildAlloca(backend->builder, backend->vm_value_type, node->name);
    LLVMBuildStore(backend->builder, init, alloca);
    add_local(backend, node->name, alloca);
    return alloca;
  }
  case AST_ASSIGNMENT: {
    LLVMValueRef val = codegen_expression(backend, node->right);

    // Check for Array/Object Assignment: arr[i] = val OR obj["k"] = val
    if (node->left && node->left->type == AST_ARRAY_ACCESS) {
      ASTNode *access = node->left;
      LLVMValueRef target = codegen_expression(backend, access->left);
      LLVMValueRef index = codegen_expression(backend, access->index);

      if (target && index) {
        // Use generic vm_set_element(vm, target, index, value)
        LLVMValueRef args[] = {LLVMConstNull(backend->ptr_type), target, index,
                               val};
        LLVMBuildCall2(backend->builder,
                       LLVMGlobalGetValueType(backend->func_vm_set_element),
                       backend->func_vm_set_element, args, 4, "");
      }
      return val;
    }

    // Standard Variable Assignment
    if (node->name) {
      LLVMValueRef target = get_local(backend, node->name);
      if (target)
        LLVMBuildStore(backend->builder, val, target);
      else
        fprintf(stderr, "Undefined var in assignment: %s\n", node->name);
    }
    return val;
  }
  case AST_BLOCK: {
    LLVMValueRef last = NULL;
    if (node->statements) {
      for (int i = 0; i < node->statement_count; i++)
        last = codegen_statement(backend, node->statements[i]);
    }
    return last;
  }
  case AST_IF: {
    LLVMValueRef cond = codegen_expression(backend, node->condition);
    cond = llvm_build_is_truthy(backend, cond);
    LLVMBasicBlockRef thenB =
        LLVMAppendBasicBlock(backend->current_function, "then");
    LLVMBasicBlockRef elseB =
        LLVMAppendBasicBlock(backend->current_function, "else");
    LLVMBasicBlockRef mergeB =
        LLVMAppendBasicBlock(backend->current_function, "merge");
    LLVMBuildCondBr(backend->builder, cond, thenB, elseB);
    LLVMPositionBuilderAtEnd(backend->builder, thenB);
    codegen_statement(backend, node->then_branch);
    if (!LLVMGetBasicBlockTerminator(thenB))
      LLVMBuildBr(backend->builder, mergeB);
    LLVMPositionBuilderAtEnd(backend->builder, elseB);
    if (node->else_branch)
      codegen_statement(backend, node->else_branch);
    if (!LLVMGetBasicBlockTerminator(elseB))
      LLVMBuildBr(backend->builder, mergeB);
    LLVMPositionBuilderAtEnd(backend->builder, mergeB);
    return NULL;
  }
  case AST_WHILE: {
    LLVMBasicBlockRef condB =
        LLVMAppendBasicBlock(backend->current_function, "cond");
    LLVMBasicBlockRef bodyB =
        LLVMAppendBasicBlock(backend->current_function, "body");
    LLVMBasicBlockRef exitB =
        LLVMAppendBasicBlock(backend->current_function, "exit");
    LLVMBuildBr(backend->builder, condB);
    LLVMPositionBuilderAtEnd(backend->builder, condB);
    LLVMValueRef c = codegen_expression(backend, node->condition);
    c = llvm_build_is_truthy(backend, c);
    LLVMBuildCondBr(backend->builder, c, bodyB, exitB);
    LLVMPositionBuilderAtEnd(backend->builder, bodyB);
    codegen_statement(backend, node->body);
    if (!LLVMGetBasicBlockTerminator(bodyB))
      LLVMBuildBr(backend->builder, condB);
    LLVMPositionBuilderAtEnd(backend->builder, exitB);
    return NULL;
  }
  case AST_FOR: {
    // for (init; condition; increment) { body }
    if (node->init)
      codegen_statement(backend, node->init);

    LLVMBasicBlockRef condB =
        LLVMAppendBasicBlock(backend->current_function, "for_cond");
    LLVMBasicBlockRef bodyB =
        LLVMAppendBasicBlock(backend->current_function, "for_body");
    LLVMBasicBlockRef incrB =
        LLVMAppendBasicBlock(backend->current_function, "for_incr");
    LLVMBasicBlockRef exitB =
        LLVMAppendBasicBlock(backend->current_function, "for_exit");

    LLVMBuildBr(backend->builder, condB);
    LLVMPositionBuilderAtEnd(backend->builder, condB);

    LLVMValueRef c =
        node->condition
            ? llvm_build_is_truthy(backend,
                                   codegen_expression(backend, node->condition))
            : LLVMConstInt(backend->bool_type, 1, 0); // true if no condition
    LLVMBuildCondBr(backend->builder, c, bodyB, exitB);

    LLVMPositionBuilderAtEnd(backend->builder, bodyB);
    codegen_statement(backend, node->body);
    if (!LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(backend->builder)))
      LLVMBuildBr(backend->builder, incrB);

    LLVMPositionBuilderAtEnd(backend->builder, incrB);
    if (node->increment)
      codegen_statement(backend, node->increment);
    LLVMBuildBr(backend->builder, condB);

    LLVMPositionBuilderAtEnd(backend->builder, exitB);
    return NULL;
  }
  case AST_RETURN: {
    LLVMValueRef ret =
        node->return_value
            ? codegen_expression(backend, node->return_value)
            : llvm_vm_val_int(backend, 0); // Return 0/Void if no value
    return LLVMBuildRet(backend->builder, ret);
  }
  case AST_TRY_CATCH: {
    // jmp_buf* buf = aot_try_push()
    LLVMValueRef buf = LLVMBuildCall2(
        backend->builder, LLVMGlobalGetValueType(backend->func_aot_try_push),
        backend->func_aot_try_push, NULL, 0, "eh_buf");

    // int result = setjmp(buf)
    LLVMValueRef setjmp_args[] = {buf};
    LLVMValueRef result = LLVMBuildCall2(
        backend->builder, LLVMGlobalGetValueType(backend->func_setjmp),
        backend->func_setjmp, setjmp_args, 1, "setjmp_res");

    // if (result == 0) { try_block } else { catch_block }
    LLVMValueRef is_try =
        LLVMBuildICmp(backend->builder, LLVMIntEQ, result,
                      LLVMConstInt(backend->int32_type, 0, 0), "is_try");

    LLVMBasicBlockRef tryB =
        LLVMAppendBasicBlock(backend->current_function, "try");
    LLVMBasicBlockRef catchB =
        LLVMAppendBasicBlock(backend->current_function, "catch");
    LLVMBasicBlockRef finallyB =
        node->finally_block
            ? LLVMAppendBasicBlock(backend->current_function, "finally")
            : NULL;
    LLVMBasicBlockRef endB =
        LLVMAppendBasicBlock(backend->current_function, "try_end");

    LLVMBuildCondBr(backend->builder, is_try, tryB, catchB);

    // Try block
    LLVMPositionBuilderAtEnd(backend->builder, tryB);
    codegen_statement(backend, node->try_block);
    // Pop handler on normal exit
    LLVMBuildCall2(backend->builder,
                   LLVMGlobalGetValueType(backend->func_aot_try_pop),
                   backend->func_aot_try_pop, NULL, 0, "");
    if (!LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(backend->builder)))
      LLVMBuildBr(backend->builder, finallyB ? finallyB : endB);

    // Catch block
    LLVMPositionBuilderAtEnd(backend->builder, catchB);
    if (node->catch_var && node->catch_block) {
      // Get exception: VMValue e = aot_get_exception()
      LLVMValueRef exc = LLVMBuildCall2(
          backend->builder,
          LLVMGlobalGetValueType(backend->func_aot_get_exception),
          backend->func_aot_get_exception, NULL, 0, "exception");
      // Store to catch variable
      LLVMValueRef alloca = LLVMBuildAlloca(
          backend->builder, backend->vm_value_type, node->catch_var);
      LLVMBuildStore(backend->builder, exc, alloca);
      add_local(backend, node->catch_var, alloca);

      codegen_statement(backend, node->catch_block);
    }
    if (!LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(backend->builder)))
      LLVMBuildBr(backend->builder, finallyB ? finallyB : endB);

    // Finally block
    if (finallyB) {
      LLVMPositionBuilderAtEnd(backend->builder, finallyB);
      codegen_statement(backend, node->finally_block);
      if (!LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(backend->builder)))
        LLVMBuildBr(backend->builder, endB);
    }

    LLVMPositionBuilderAtEnd(backend->builder, endB);
    return NULL;
  }
  case AST_THROW: {
    LLVMValueRef exc = node->throw_expr
                           ? codegen_expression(backend, node->throw_expr)
                           : llvm_vm_val_int(backend, 0);
    LLVMValueRef args[] = {exc};
    LLVMBuildCall2(backend->builder,
                   LLVMGlobalGetValueType(backend->func_aot_throw),
                   backend->func_aot_throw, args, 1, "");
    LLVMBuildUnreachable(backend->builder);
    return NULL;
  }
  case AST_FUNCTION_DECL:
    return NULL;
  case AST_FUNCTION_CALL:
    return codegen_expression(backend, node);
  case AST_IMPORT: {
    const char *rel_path = node->value.string_value;
    // Check duplication
    for (int i = 0; i < backend->imported_count; i++) {
      if (strcmp(backend->imported_files[i], rel_path) == 0)
        return NULL;
    }
    backend->imported_files[backend->imported_count++] = strdup(rel_path);

    printf("[AOT] Importing: %s\n", rel_path);

    FILE *f = fopen(rel_path, "rb");
    if (!f) {
      fprintf(stderr, "Error: Could not import file '%s'\n", rel_path);
      return NULL;
    }
    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    fseek(f, 0, SEEK_SET);

    char *source = (char *)malloc(fsize + 1);
    size_t read_size = fread(source, 1, fsize, f);
    (void)read_size;
    source[fsize] = 0;
    fclose(f);

    Lexer *lexer = lexer_create(source);
    int token_capacity = 1024;
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
    ASTNode *module_ast = parser_parse(parser);
    parser_free(parser);

    if (module_ast) {
      // Compile function definitions from module
      if (module_ast->type == AST_PROGRAM && module_ast->statements) {
        for (int i = 0; i < module_ast->statement_count; i++) {
          if (module_ast->statements[i]->type == AST_FUNCTION_DECL) {
            codegen_func_def(backend, module_ast->statements[i]);
          }
        }
        // Execute top-level statements in current scope
        for (int i = 0; i < module_ast->statement_count; i++) {
          if (module_ast->statements[i]->type != AST_FUNCTION_DECL) {
            codegen_statement(backend, module_ast->statements[i]);
          }
        }
      }
    }

    for (int i = 0; i < token_count; i++) {
      token_free(tokens[i]);
    }
    free(tokens);
    free(source);
    return NULL;
  }
  default:
    return codegen_expression(backend, node);
  }
}

void codegen_func_def(LLVMBackend *backend, ASTNode *node) {
  int param_count = node->param_count;

  // Param types: VM* (context) + arg count * VMValue
  // For now, let's stick to simple args: [VMValue, VMValue...]
  // TODO: Add VM* context later

  LLVMTypeRef *param_types =
      (LLVMTypeRef *)malloc(sizeof(LLVMTypeRef) * param_count);
  for (int i = 0; i < param_count; i++)
    param_types[i] = backend->vm_value_type;

  LLVMTypeRef func_type =
      LLVMFunctionType(backend->vm_value_type, param_types, param_count, 0);

  LLVMValueRef func = LLVMAddFunction(backend->module, node->name, func_type);
  register_function(backend, node->name, func_type);

  LLVMValueRef prev_func = backend->current_function;
  LLVMBasicBlockRef prev_block = LLVMGetInsertBlock(backend->builder);
  backend->current_function = func;

  LLVMBasicBlockRef entry = LLVMAppendBasicBlock(func, "entry");
  LLVMPositionBuilderAtEnd(backend->builder, entry);

  enter_scope(backend);
  for (int i = 0; i < param_count; i++) {
    if (node->parameters[i]) {
      LLVMValueRef p = LLVMGetParam(func, i);
      LLVMValueRef alloca = LLVMBuildAlloca(
          backend->builder, backend->vm_value_type, node->parameters[i]->name);
      LLVMBuildStore(backend->builder, p, alloca);
      add_local(backend, node->parameters[i]->name, alloca);
    }
  }

  codegen_statement(backend, node->body);

  // Default return if missing
  if (!LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(backend->builder)))
    LLVMBuildRet(backend->builder, llvm_vm_val_int(backend, 0));

  exit_scope(backend);
  free(param_types);

  backend->current_function = prev_func;
  if (prev_block)
    LLVMPositionBuilderAtEnd(backend->builder, prev_block);
}

void llvm_backend_compile(LLVMBackend *backend, ASTNode *node) {
  if (node->type != AST_PROGRAM)
    return;
  if (node->statements) {
    for (int i = 0; i < node->statement_count; i++) {
      if (node->statements[i]->type == AST_FUNCTION_DECL)
        codegen_func_def(backend, node->statements[i]);
    }
  }

  // MAIN FUNCTION: int main() -> returns raw i32 (OS exit code)
  // But inside it works with VMValues.
  LLVMTypeRef main_type = LLVMFunctionType(backend->int32_type, NULL, 0, 0);
  LLVMValueRef main_func = LLVMAddFunction(backend->module, "main", main_type);
  backend->current_function = main_func;
  LLVMBasicBlockRef entry = LLVMAppendBasicBlock(main_func, "entry");
  LLVMPositionBuilderAtEnd(backend->builder, entry);

  enter_scope(backend);
  if (node->statements) {
    for (int i = 0; i < node->statement_count; i++) {
      if (node->statements[i]->type != AST_FUNCTION_DECL)
        codegen_statement(backend, node->statements[i]);
    }
  }

  if (!LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(backend->builder)))
    LLVMBuildRet(backend->builder, LLVMConstInt(backend->int32_type, 0, 0));

  exit_scope(backend);
}

// Emitting IR file
int llvm_backend_emit_ir_file(LLVMBackend *backend, const char *filename) {
  char *error = NULL;
  if (LLVMPrintModuleToFile(backend->module, filename, &error) != 0) {
    fprintf(stderr, "Error emitting IR file: %s\n", error);
    LLVMDisposeMessage(error);
    return 1;
  }
  printf("Generated IR file: %s\n", filename);
  return 0;
}

int llvm_backend_emit_object(LLVMBackend *backend, const char *filename) {
  // Initialize only native target (X86 on Linux/Windows)
  LLVMInitializeNativeTarget();
  LLVMInitializeNativeAsmParser();
  LLVMInitializeNativeAsmPrinter();
  char *triple = LLVMGetDefaultTargetTriple();
  LLVMTargetRef target;
  char *error = NULL;
  if (LLVMGetTargetFromTriple(triple, &target, &error) != 0)
    return 1;
  LLVMTargetMachineRef machine = LLVMCreateTargetMachine(
      target, triple, "generic", "", LLVMCodeGenLevelDefault, LLVMRelocDefault,
      LLVMCodeModelDefault);
  LLVMSetModuleDataLayout(backend->module, LLVMCreateTargetDataLayout(machine));
  LLVMSetTarget(backend->module, triple);
  if (LLVMTargetMachineEmitToFile(machine, backend->module, filename,
                                  LLVMObjectFile, &error) != 0)
    return 1;
  LLVMDisposeTargetMachine(machine);
  LLVMDisposeMessage(triple);
  return 0;
}
