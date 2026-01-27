// Tulpar LLVM Backend - Implementation
// Phase 3 Fix: Printf types

#define _CRT_SECURE_NO_WARNINGS
#define _CRT_NONSTDC_NO_DEPRECATE

#include "llvm_backend.h"
#include "../lexer/lexer.h"
#include "../parser/parser.h"
#include "llvm_types.h"
#include "llvm_values.h"
#include <llvm-c/Analysis.h>
#include <llvm-c/Target.h>
#include <llvm-c/Transforms/PassBuilder.h>
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

// Helper to build alloca in entry block
LLVMValueRef llvm_build_alloca_at_entry(LLVMBackend *backend, LLVMTypeRef type,
                                        const char *name) {
  LLVMValueRef func = backend->current_function;
  LLVMBasicBlockRef entry = LLVMGetEntryBasicBlock(func);
  LLVMBuilderRef builder = LLVMCreateBuilderInContext(backend->context);
  LLVMValueRef first = LLVMGetFirstInstruction(entry);
  if (first)
    LLVMPositionBuilderBefore(builder, first);
  else
    LLVMPositionBuilderAtEnd(builder, entry);
  LLVMValueRef alloca = LLVMBuildAlloca(builder, type, name);
  LLVMDisposeBuilder(builder);
  return alloca;
}

// Forward declarations
void codegen_func_def(LLVMBackend *backend, ASTNode *node);

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
  // print_value: void print_value(VMValue*) - takes pointer for ABI
  // compatibility
  LLVMTypeRef print_val_params[] = {backend->ptr_type};
  LLVMTypeRef print_val_type =
      LLVMFunctionType(backend->void_type, print_val_params, 1, 0);
  backend->func_print_value =
      LLVMAddFunction(backend->module, "aot_print_value", print_val_type);

  // print_value_inline: void print_value_inline(VMValue*) - no newline
  backend->func_print_value_inline = backend->func_print_value;

  // print_newline: void print_newline()
  LLVMTypeRef print_nl_type = LLVMFunctionType(backend->void_type, NULL, 0, 0);
  backend->func_print_newline =
      LLVMAddFunction(backend->module, "print_newline", print_nl_type);

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

  // vm_get_element_ptr(VMValue*, VMValue*) -> VMValue (pointer-based for ABI
  // compatibility)
  LLVMTypeRef get_el_params[] = {backend->ptr_type, backend->ptr_type};
  LLVMTypeRef get_el_type =
      LLVMFunctionType(backend->vm_value_type, get_el_params, 2, 0);
  backend->func_vm_get_element =
      LLVMAddFunction(backend->module, "vm_get_element_ptr", get_el_type);

  // vm_set_element_ptr(VM*, VMValue*, VMValue*, VMValue*) (pointer-based for
  // ABI compatibility)
  LLVMTypeRef set_el_params[] = {backend->ptr_type, backend->ptr_type,
                                 backend->ptr_type, backend->ptr_type};
  LLVMTypeRef set_el_type =
      LLVMFunctionType(backend->void_type, set_el_params, 4, 0);
  backend->func_vm_set_element =
      LLVMAddFunction(backend->module, "vm_set_element_ptr", set_el_type);

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

  // aot_array_push(VMValue*, VMValue*) -> void (pointer ABI)
  LLVMTypeRef push_aot_params[] = {backend->ptr_type, backend->ptr_type};
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

  // ====== Fast Array Access (value-based, no alloca) ======
  // aot_array_get_fast(VMValue arr, i64 index) -> VMValue
  LLVMTypeRef get_fast_params[] = {backend->vm_value_type, backend->int_type};
  LLVMTypeRef get_fast_type =
      LLVMFunctionType(backend->vm_value_type, get_fast_params, 2, 0);
  backend->func_aot_array_get_fast =
      LLVMAddFunction(backend->module, "aot_array_get_fast", get_fast_type);

  // aot_array_set_fast(VMValue arr, i64 index, VMValue value) -> void
  LLVMTypeRef set_fast_params[] = {backend->vm_value_type, backend->int_type,
                                   backend->vm_value_type};
  LLVMTypeRef set_fast_type =
      LLVMFunctionType(backend->void_type, set_fast_params, 3, 0);
  backend->func_aot_array_set_fast =
      LLVMAddFunction(backend->module, "aot_array_set_fast", set_fast_type);

  // ====== RAW Pointer Array Access (Maximum Performance) ======
  // aot_array_get_raw(VMValue arr) -> ObjArray* (ptr)
  LLVMTypeRef get_raw_params[] = {backend->vm_value_type};
  LLVMTypeRef get_raw_type =
      LLVMFunctionType(backend->ptr_type, get_raw_params, 1, 0);
  backend->func_aot_array_get_raw =
      LLVMAddFunction(backend->module, "aot_array_get_raw", get_raw_type);

  // aot_array_get_raw_fast(ObjArray* arr, i64 index) -> VMValue
  LLVMTypeRef get_raw_fast_params[] = {backend->ptr_type, backend->int_type};
  LLVMTypeRef get_raw_fast_type =
      LLVMFunctionType(backend->vm_value_type, get_raw_fast_params, 2, 0);
  backend->func_aot_array_get_raw_fast = LLVMAddFunction(
      backend->module, "aot_array_get_raw_fast", get_raw_fast_type);

  // aot_array_set_raw_fast(ObjArray* arr, i64 index, VMValue value) -> void
  LLVMTypeRef set_raw_fast_params[] = {backend->ptr_type, backend->int_type,
                                       backend->vm_value_type};
  LLVMTypeRef set_raw_fast_type =
      LLVMFunctionType(backend->void_type, set_raw_fast_params, 3, 0);
  backend->func_aot_array_set_raw_fast = LLVMAddFunction(
      backend->module, "aot_array_set_raw_fast", set_raw_fast_type);

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

  // aot_clock_ms() -> VMValue (float ms)
  LLVMTypeRef clock_type = LLVMFunctionType(backend->vm_value_type, NULL, 0, 0);
  backend->func_aot_clock_ms =
      LLVMAddFunction(backend->module, "aot_clock_ms", clock_type);

  // Socket Functions
  // aot_socket_server(host, port) -> int (fd)
  LLVMTypeRef sock_server_params[] = {backend->vm_value_type,
                                      backend->vm_value_type};
  LLVMTypeRef sock_server_type =
      LLVMFunctionType(backend->vm_value_type, sock_server_params, 2, 0);
  backend->func_aot_socket_server =
      LLVMAddFunction(backend->module, "aot_socket_server", sock_server_type);

  // aot_socket_client(host, port) -> int (fd)
  backend->func_aot_socket_client =
      LLVMAddFunction(backend->module, "aot_socket_client", sock_server_type);

  // aot_socket_accept(server_fd) -> int (client_fd)
  LLVMTypeRef sock_accept_params[] = {backend->vm_value_type};
  LLVMTypeRef sock_accept_type =
      LLVMFunctionType(backend->vm_value_type, sock_accept_params, 1, 0);
  backend->func_aot_socket_accept =
      LLVMAddFunction(backend->module, "aot_socket_accept", sock_accept_type);

  // aot_socket_send(fd, data) -> int (bytes)
  LLVMTypeRef sock_send_params[] = {backend->vm_value_type,
                                    backend->vm_value_type};
  LLVMTypeRef sock_send_type =
      LLVMFunctionType(backend->vm_value_type, sock_send_params, 2, 0);
  backend->func_aot_socket_send =
      LLVMAddFunction(backend->module, "aot_socket_send", sock_send_type);

  // aot_socket_receive(fd, size) -> string
  // Uses same signature as send (2 params)
  backend->func_aot_socket_receive =
      LLVMAddFunction(backend->module, "aot_socket_receive", sock_send_type);

  // aot_socket_close(fd) -> void
  LLVMTypeRef sock_close_params[] = {backend->vm_value_type};
  LLVMTypeRef sock_close_type =
      LLVMFunctionType(backend->void_type, sock_close_params, 1, 0);
  backend->func_aot_socket_close =
      LLVMAddFunction(backend->module, "aot_socket_close", sock_close_type);

  // ====== Fast String Operations ======
  // aot_string_concat_fast(VMValue, VMValue) -> VMValue
  LLVMTypeRef str_concat_params[] = {backend->vm_value_type,
                                     backend->vm_value_type};
  LLVMTypeRef str_concat_type =
      LLVMFunctionType(backend->vm_value_type, str_concat_params, 2, 0);
  backend->func_aot_string_concat_fast = LLVMAddFunction(
      backend->module, "aot_string_concat_fast", str_concat_type);

  // ====== StringBuilder Functions ======
  // aot_stringbuilder_new(int capacity) -> ptr
  LLVMTypeRef sb_new_params[] = {backend->int32_type};
  LLVMTypeRef sb_new_type =
      LLVMFunctionType(backend->ptr_type, sb_new_params, 1, 0);
  backend->func_aot_stringbuilder_new =
      LLVMAddFunction(backend->module, "aot_stringbuilder_new", sb_new_type);

  // aot_stringbuilder_append_vmvalue(ptr, VMValue) -> void
  LLVMTypeRef sb_append_params[] = {backend->ptr_type, backend->vm_value_type};
  LLVMTypeRef sb_append_type =
      LLVMFunctionType(backend->void_type, sb_append_params, 2, 0);
  backend->func_aot_stringbuilder_append_vmvalue = LLVMAddFunction(
      backend->module, "aot_stringbuilder_append_vmvalue", sb_append_type);

  // aot_stringbuilder_to_string(ptr) -> VMValue
  LLVMTypeRef sb_tostring_params[] = {backend->ptr_type};
  LLVMTypeRef sb_tostring_type =
      LLVMFunctionType(backend->vm_value_type, sb_tostring_params, 1, 0);
  backend->func_aot_stringbuilder_to_string = LLVMAddFunction(
      backend->module, "aot_stringbuilder_to_string", sb_tostring_type);

  // aot_stringbuilder_free(ptr) -> void
  LLVMTypeRef sb_free_params[] = {backend->ptr_type};
  LLVMTypeRef sb_free_type =
      LLVMFunctionType(backend->void_type, sb_free_params, 1, 0);
  backend->func_aot_stringbuilder_free =
      LLVMAddFunction(backend->module, "aot_stringbuilder_free", sb_free_type);

  // ====== Threading Functions ======
  // aot_thread_create(func_ptr, arg) -> VMValue (thread_id)
  LLVMTypeRef thread_create_params[] = {backend->ptr_type,
                                        backend->vm_value_type};
  LLVMTypeRef thread_create_type =
      LLVMFunctionType(backend->vm_value_type, thread_create_params, 2, 0);
  backend->func_aot_thread_create =
      LLVMAddFunction(backend->module, "aot_thread_create", thread_create_type);

  // aot_thread_join(thread_id) -> void
  LLVMTypeRef thread_join_params[] = {backend->vm_value_type};
  LLVMTypeRef thread_join_type =
      LLVMFunctionType(backend->void_type, thread_join_params, 1, 0);
  backend->func_aot_thread_join =
      LLVMAddFunction(backend->module, "aot_thread_join", thread_join_type);

  // aot_thread_detach(thread_id) -> void
  backend->func_aot_thread_detach =
      LLVMAddFunction(backend->module, "aot_thread_detach", thread_join_type);

  // aot_mutex_create() -> VMValue (mutex_ptr)
  LLVMTypeRef mutex_create_type =
      LLVMFunctionType(backend->vm_value_type, NULL, 0, 0);
  backend->func_aot_mutex_create =
      LLVMAddFunction(backend->module, "aot_mutex_create", mutex_create_type);

  // aot_mutex_lock(mutex) -> void
  LLVMTypeRef mutex_op_params[] = {backend->vm_value_type};
  LLVMTypeRef mutex_op_type =
      LLVMFunctionType(backend->void_type, mutex_op_params, 1, 0);
  backend->func_aot_mutex_lock =
      LLVMAddFunction(backend->module, "aot_mutex_lock", mutex_op_type);

  // aot_mutex_unlock(mutex) -> void
  backend->func_aot_mutex_unlock =
      LLVMAddFunction(backend->module, "aot_mutex_unlock", mutex_op_type);

  // aot_mutex_destroy(mutex) -> void
  backend->func_aot_mutex_destroy =
      LLVMAddFunction(backend->module, "aot_mutex_destroy", mutex_op_type);

  // ====== HTTP Functions ======
  // aot_http_parse_request(raw) -> VMValue (object)
  LLVMTypeRef http_parse_params[] = {backend->vm_value_type};
  LLVMTypeRef http_parse_type =
      LLVMFunctionType(backend->vm_value_type, http_parse_params, 1, 0);
  backend->func_aot_http_parse_request = LLVMAddFunction(
      backend->module, "aot_http_parse_request", http_parse_type);

  // aot_http_create_response(status, content_type, body) -> VMValue (string)
  LLVMTypeRef http_response_params[] = {
      backend->vm_value_type, backend->vm_value_type, backend->vm_value_type};
  LLVMTypeRef http_response_type =
      LLVMFunctionType(backend->vm_value_type, http_response_params, 3, 0);
  backend->func_aot_http_create_response = LLVMAddFunction(
      backend->module, "aot_http_create_response", http_response_type);

  // ====== Math Functions ======
  // Single param math: abs, sqrt, floor, ceil, round, sin, cos, tan, asin,
  // acos, atan, exp, log, log10, log2, sinh, cosh, tanh, cbrt, trunc
  // Special case: abs takes pointer to avoid ABI issues (void return, result
  // ptr, arg ptr)
  LLVMTypeRef math1_ptr_params[] = {backend->ptr_type, backend->ptr_type};
  LLVMTypeRef math1_ptr_type =
      LLVMFunctionType(backend->void_type, math1_ptr_params, 2, 0);
  backend->func_aot_math_abs =
      LLVMAddFunction(backend->module, "aot_math_abs", math1_ptr_type);

  // Single param math (Standard): sqrt, floor, ceil, ...
  LLVMTypeRef math1_params[] = {backend->vm_value_type};
  LLVMTypeRef math1_type =
      LLVMFunctionType(backend->vm_value_type, math1_params, 1, 0);

  // backend->func_aot_math_abs = ... (Removed from here)
  backend->func_aot_math_sqrt =
      LLVMAddFunction(backend->module, "aot_math_sqrt", math1_type);
  backend->func_aot_math_floor =
      LLVMAddFunction(backend->module, "aot_math_floor", math1_type);
  backend->func_aot_math_ceil =
      LLVMAddFunction(backend->module, "aot_math_ceil", math1_type);
  backend->func_aot_math_round =
      LLVMAddFunction(backend->module, "aot_math_round", math1_type);
  backend->func_aot_math_sin =
      LLVMAddFunction(backend->module, "aot_math_sin", math1_type);
  backend->func_aot_math_cos =
      LLVMAddFunction(backend->module, "aot_math_cos", math1_type);
  backend->func_aot_math_tan =
      LLVMAddFunction(backend->module, "aot_math_tan", math1_type);
  backend->func_aot_math_asin =
      LLVMAddFunction(backend->module, "aot_math_asin", math1_type);
  backend->func_aot_math_acos =
      LLVMAddFunction(backend->module, "aot_math_acos", math1_type);
  backend->func_aot_math_atan =
      LLVMAddFunction(backend->module, "aot_math_atan", math1_type);
  backend->func_aot_math_exp =
      LLVMAddFunction(backend->module, "aot_math_exp", math1_type);
  backend->func_aot_math_log =
      LLVMAddFunction(backend->module, "aot_math_log", math1_type);
  backend->func_aot_math_log10 =
      LLVMAddFunction(backend->module, "aot_math_log10", math1_type);
  backend->func_aot_math_log2 =
      LLVMAddFunction(backend->module, "aot_math_log2", math1_type);
  backend->func_aot_math_sinh =
      LLVMAddFunction(backend->module, "aot_math_sinh", math1_type);
  backend->func_aot_math_cosh =
      LLVMAddFunction(backend->module, "aot_math_cosh", math1_type);
  backend->func_aot_math_tanh =
      LLVMAddFunction(backend->module, "aot_math_tanh", math1_type);
  backend->func_aot_math_cbrt =
      LLVMAddFunction(backend->module, "aot_math_cbrt", math1_type);
  backend->func_aot_math_trunc =
      LLVMAddFunction(backend->module, "aot_math_trunc", math1_type);

  // Two param math: pow, atan2, hypot, fmod, min, max, randint
  LLVMTypeRef math2_params[] = {backend->vm_value_type, backend->vm_value_type};
  LLVMTypeRef math2_type =
      LLVMFunctionType(backend->vm_value_type, math2_params, 2, 0);

  backend->func_aot_math_pow =
      LLVMAddFunction(backend->module, "aot_math_pow", math2_type);
  backend->func_aot_math_atan2 =
      LLVMAddFunction(backend->module, "aot_math_atan2", math2_type);
  backend->func_aot_math_hypot =
      LLVMAddFunction(backend->module, "aot_math_hypot", math2_type);
  backend->func_aot_math_fmod =
      LLVMAddFunction(backend->module, "aot_math_fmod", math2_type);
  backend->func_aot_math_min =
      LLVMAddFunction(backend->module, "aot_math_min", math2_type);
  backend->func_aot_math_max =
      LLVMAddFunction(backend->module, "aot_math_max", math2_type);
  backend->func_aot_math_randint =
      LLVMAddFunction(backend->module, "aot_math_randint", math2_type);

  // No param math: random
  LLVMTypeRef math0_type = LLVMFunctionType(backend->vm_value_type, NULL, 0, 0);
  backend->func_aot_math_random =
      LLVMAddFunction(backend->module, "aot_math_random", math0_type);

  // ====== String Functions ======
  // Single param: upper, lower, reverse, isEmpty, capitalize, isDigit, isAlpha
  LLVMTypeRef str1_params[] = {backend->vm_value_type};
  LLVMTypeRef str1_type =
      LLVMFunctionType(backend->vm_value_type, str1_params, 1, 0);

  backend->func_aot_string_upper =
      LLVMAddFunction(backend->module, "aot_string_upper", str1_type);
  backend->func_aot_string_lower =
      LLVMAddFunction(backend->module, "aot_string_lower", str1_type);
  backend->func_aot_string_reverse =
      LLVMAddFunction(backend->module, "aot_string_reverse", str1_type);
  backend->func_aot_string_is_empty =
      LLVMAddFunction(backend->module, "aot_string_is_empty", str1_type);
  backend->func_aot_string_capitalize =
      LLVMAddFunction(backend->module, "aot_string_capitalize", str1_type);
  backend->func_aot_string_is_digit =
      LLVMAddFunction(backend->module, "aot_string_is_digit", str1_type);
  backend->func_aot_string_is_alpha =
      LLVMAddFunction(backend->module, "aot_string_is_alpha", str1_type);

  // Two param: contains, startsWith, endsWith, indexOf, repeat, count, join
  LLVMTypeRef str2_params[] = {backend->vm_value_type, backend->vm_value_type};
  LLVMTypeRef str2_type =
      LLVMFunctionType(backend->vm_value_type, str2_params, 2, 0);

  backend->func_aot_string_contains =
      LLVMAddFunction(backend->module, "aot_string_contains", str2_type);
  backend->func_aot_string_starts_with =
      LLVMAddFunction(backend->module, "aot_string_starts_with", str2_type);
  backend->func_aot_string_ends_with =
      LLVMAddFunction(backend->module, "aot_string_ends_with", str2_type);
  backend->func_aot_string_index_of =
      LLVMAddFunction(backend->module, "aot_string_index_of", str2_type);
  backend->func_aot_string_repeat =
      LLVMAddFunction(backend->module, "aot_string_repeat", str2_type);
  backend->func_aot_string_count =
      LLVMAddFunction(backend->module, "aot_string_count", str2_type);
  backend->func_aot_string_join =
      LLVMAddFunction(backend->module, "aot_string_join", str2_type);

  // Three param: substring(str, start, end)
  LLVMTypeRef str3_params[] = {backend->vm_value_type, backend->vm_value_type,
                               backend->vm_value_type};
  LLVMTypeRef str3_type =
      LLVMFunctionType(backend->vm_value_type, str3_params, 3, 0);
  backend->func_aot_string_substring =
      LLVMAddFunction(backend->module, "aot_string_substring", str3_type);

  // ====== Time Functions ======
  LLVMTypeRef time0_type = LLVMFunctionType(backend->vm_value_type, NULL, 0, 0);
  backend->func_aot_timestamp =
      LLVMAddFunction(backend->module, "aot_timestamp", time0_type);
  backend->func_aot_time_ms =
      LLVMAddFunction(backend->module, "aot_time_ms", time0_type);

  LLVMTypeRef sleep_params[] = {backend->vm_value_type};
  LLVMTypeRef sleep_type =
      LLVMFunctionType(backend->void_type, sleep_params, 1, 0);
  backend->func_aot_sleep =
      LLVMAddFunction(backend->module, "aot_sleep", sleep_type);

  // ====== JSON Functions ======
  // aot_from_json(str) -> VMValue
  LLVMTypeRef from_json_params[] = {backend->vm_value_type};
  LLVMTypeRef from_json_type =
      LLVMFunctionType(backend->vm_value_type, from_json_params, 1, 0);
  backend->func_aot_from_json =
      LLVMAddFunction(backend->module, "aot_from_json", from_json_type);

  // ====== Input Functions ======
  // aot_input_int(prompt) -> VMValue
  // aot_input_float(prompt) -> VMValue
  LLVMTypeRef input_prompt_params[] = {backend->vm_value_type};
  LLVMTypeRef input_prompt_type =
      LLVMFunctionType(backend->vm_value_type, input_prompt_params, 1, 0);
  backend->func_aot_input_int =
      LLVMAddFunction(backend->module, "aot_input_int", input_prompt_type);
  backend->func_aot_input_float =
      LLVMAddFunction(backend->module, "aot_input_float", input_prompt_type);

  // ====== Range Function ======
  // aot_range(end) -> VMValue (array)
  LLVMTypeRef range_params[] = {backend->vm_value_type};
  LLVMTypeRef range_type =
      LLVMFunctionType(backend->vm_value_type, range_params, 1, 0);
  backend->func_aot_range =
      LLVMAddFunction(backend->module, "aot_range", range_type);

  // ====== SQLite Database Functions ======
  // aot_db_open(path) -> int64 (db handle)
  LLVMTypeRef db_open_params[] = {backend->vm_value_type};
  LLVMTypeRef db_open_type =
      LLVMFunctionType(backend->vm_value_type, db_open_params, 1, 0);
  backend->func_aot_db_open =
      LLVMAddFunction(backend->module, "aot_db_open", db_open_type);

  // aot_db_close(db) -> void
  LLVMTypeRef db_close_params[] = {backend->vm_value_type};
  LLVMTypeRef db_close_type =
      LLVMFunctionType(backend->void_type, db_close_params, 1, 0);
  backend->func_aot_db_close =
      LLVMAddFunction(backend->module, "aot_db_close", db_close_type);

  // aot_db_execute(db, sql) -> bool
  LLVMTypeRef db_exec_params[] = {backend->vm_value_type,
                                  backend->vm_value_type};
  LLVMTypeRef db_exec_type =
      LLVMFunctionType(backend->vm_value_type, db_exec_params, 2, 0);
  backend->func_aot_db_execute =
      LLVMAddFunction(backend->module, "aot_db_execute", db_exec_type);

  // aot_db_query(db, sql) -> array
  backend->func_aot_db_query =
      LLVMAddFunction(backend->module, "aot_db_query", db_exec_type);

  // aot_db_last_insert_id(db) -> int64
  backend->func_aot_db_last_insert_id =
      LLVMAddFunction(backend->module, "aot_db_last_insert_id", db_open_type);

  // aot_db_error(db) -> string
  backend->func_aot_db_error =
      LLVMAddFunction(backend->module, "aot_db_error", db_open_type);

  // ====== Type Checking Functions ======
  LLVMTypeRef type_check_params[] = {backend->vm_value_type};
  LLVMTypeRef type_check_type =
      LLVMFunctionType(backend->vm_value_type, type_check_params, 1, 0);

  backend->func_aot_typeof =
      LLVMAddFunction(backend->module, "aot_typeof", type_check_type);
  backend->func_aot_is_int =
      LLVMAddFunction(backend->module, "aot_is_int", type_check_type);
  backend->func_aot_is_float =
      LLVMAddFunction(backend->module, "aot_is_float", type_check_type);
  backend->func_aot_is_string =
      LLVMAddFunction(backend->module, "aot_is_string", type_check_type);
  backend->func_aot_is_array =
      LLVMAddFunction(backend->module, "aot_is_array", type_check_type);
  backend->func_aot_is_object =
      LLVMAddFunction(backend->module, "aot_is_object", type_check_type);
  backend->func_aot_is_bool =
      LLVMAddFunction(backend->module, "aot_is_bool", type_check_type);
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
  
  // Enable static typing by default for performance
  backend->use_static_typing = 1;

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
    s->vars[s->count].known_type = INFERRED_UNKNOWN;
    s->vars[s->count].native_value = NULL;
    s->count++;
  }
}

// Add local with known type for unboxed operations
void add_local_typed(LLVMBackend *backend, const char *name, LLVMValueRef val,
                     InferredType type, LLVMValueRef native_val) {
  if (!backend->current_scope)
    return;
  Scope *s = backend->current_scope;
  if (s->count < 256) {
    s->vars[s->count].name = my_strdup(name);
    s->vars[s->count].value = val;
    s->vars[s->count].known_type = type;
    s->vars[s->count].native_value = native_val;
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

// Get the inferred type of a local variable
InferredType get_local_type(LLVMBackend *backend, const char *name) {
  Scope *s = backend->current_scope;
  while (s) {
    for (int i = 0; i < s->count; i++) {
      if (strcmp(s->vars[i].name, name) == 0)
        return s->vars[i].known_type;
    }
    s = s->parent;
  }
  return INFERRED_UNKNOWN;
}

// Get the native (unboxed) value for a local if available
LLVMValueRef get_local_native(LLVMBackend *backend, const char *name) {
  Scope *s = backend->current_scope;
  while (s) {
    for (int i = 0; i < s->count; i++) {
      if (strcmp(s->vars[i].name, name) == 0)
        return s->vars[i].native_value;
    }
    s = s->parent;
  }
  return NULL;
}

LLVMValueRef codegen_expression(LLVMBackend *backend, ASTNode *node);
LLVMValueRef codegen_statement(LLVMBackend *backend, ASTNode *node);

// Typed expression result for unboxed operations
typedef struct {
  LLVMValueRef value; // Native value (i64, double, or VMValue)
  InferredType type;  // Known type at compile time
  LLVMValueRef boxed; // Boxed VMValue (lazy, may be NULL)
} TypedValue;

// Forward declarations for typed codegen
TypedValue codegen_typed_expr(LLVMBackend *backend, ASTNode *node);
LLVMValueRef box_typed_value(LLVMBackend *backend, TypedValue tv);

// Box a typed value to VMValue when needed
LLVMValueRef box_typed_value(LLVMBackend *backend, TypedValue tv) {
  if (tv.boxed)
    return tv.boxed;

  switch (tv.type) {
  case INFERRED_INT:
    return llvm_vm_val_int_val(backend, tv.value);
  case INFERRED_FLOAT:
    return llvm_build_vm_val_float(backend, tv.value);
  case INFERRED_BOOL: {
    // Convert i64 to bool VMValue
    LLVMValueRef s = LLVMGetUndef(backend->vm_value_type);
    s = LLVMBuildInsertValue(backend->builder, s,
                             LLVMConstInt(backend->int32_type, 2, 0), 0, "");
    s = LLVMBuildInsertValue(backend->builder, s, tv.value, 2, "");
    return s;
  }
  default:
    return tv.value; // Already boxed
  }
}

// Typed expression codegen - returns native values when possible
TypedValue codegen_typed_expr(LLVMBackend *backend, ASTNode *node) {
  TypedValue result = {NULL, INFERRED_UNKNOWN, NULL};

  if (!node)
    return result;

  switch (node->type) {
  case AST_INT_LITERAL:
    result.value = LLVMConstInt(backend->int_type, node->value.int_value, 1);
    result.type = INFERRED_INT;
    return result;

  case AST_FLOAT_LITERAL:
    result.value = LLVMConstReal(backend->float_type, node->value.float_value);
    result.type = INFERRED_FLOAT;
    return result;

  case AST_BOOL_LITERAL:
    result.value = LLVMConstInt(backend->int_type, node->value.bool_value, 0);
    result.type = INFERRED_BOOL;
    return result;

  case AST_IDENTIFIER: {
    InferredType var_type = get_local_type(backend, node->name);
    LLVMValueRef native = get_local_native(backend, node->name);

    if (var_type != INFERRED_UNKNOWN && native) {
      result.value = LLVMBuildLoad2(
          backend->builder,
          var_type == INFERRED_FLOAT ? backend->float_type : backend->int_type,
          native, node->name);
      result.type = var_type;
    } else {
      // Fall back to boxed
      LLVMValueRef val_ptr = get_local(backend, node->name);
      if (val_ptr) {
        result.boxed = LLVMBuildLoad2(backend->builder, backend->vm_value_type,
                                      val_ptr, node->name);
        result.value = result.boxed;
      }
    }
    return result;
  }

  case AST_FUNCTION_CALL: {
    // Look up the function
    LLVMValueRef func = LLVMGetNamedFunction(backend->module, node->name);
    if (func) {
      LLVMTypeRef func_type = LLVMGlobalGetValueType(func);
      LLVMTypeRef ret_type = LLVMGetReturnType(func_type);
      
      // Check if it's a native function (returns i64 directly)
      if (ret_type == backend->int_type) {
        // Build arguments - convert to i64
        int arg_count = node->argument_count;
        LLVMValueRef *args = NULL;
        if (arg_count > 0) {
          args = (LLVMValueRef *)malloc(sizeof(LLVMValueRef) * arg_count);
          for (int i = 0; i < arg_count; i++) {
            TypedValue arg = codegen_typed_expr(backend, node->arguments[i]);
            if (arg.type == INFERRED_INT || arg.type == INFERRED_BOOL) {
              args[i] = arg.value;
            } else if (arg.boxed) {
              // Extract int from boxed value
              args[i] = LLVMBuildExtractValue(backend->builder, arg.boxed, 2, "arg_int");
            } else {
              args[i] = arg.value;
            }
          }
        }
        
        result.value = LLVMBuildCall2(backend->builder, func_type, func,
                                       args, arg_count, "call_native");
        result.type = INFERRED_INT;
        if (args) free(args);
        return result;
      }
    }
    
    // Fall back to boxed codegen
    result.boxed = codegen_expression(backend, node);
    result.value = result.boxed;
    return result;
  }

  case AST_BINARY_OP: {
    TypedValue L = codegen_typed_expr(backend, node->left);
    TypedValue R = codegen_typed_expr(backend, node->right);

    // Fast path: both are known integers
    if (L.type == INFERRED_INT && R.type == INFERRED_INT) {
      switch (node->op) {
      case TOKEN_PLUS:
        result.value = LLVMBuildAdd(backend->builder, L.value, R.value, "add");
        result.type = INFERRED_INT;
        return result;
      case TOKEN_MINUS:
        result.value = LLVMBuildSub(backend->builder, L.value, R.value, "sub");
        result.type = INFERRED_INT;
        return result;
      case TOKEN_MULTIPLY:
        result.value = LLVMBuildMul(backend->builder, L.value, R.value, "mul");
        result.type = INFERRED_INT;
        return result;
      case TOKEN_DIVIDE:
        result.value = LLVMBuildSDiv(backend->builder, L.value, R.value, "div");
        result.type = INFERRED_INT;
        return result;
      case TOKEN_LESS:
        result.value = LLVMBuildZExt(
            backend->builder,
            LLVMBuildICmp(backend->builder, LLVMIntSLT, L.value, R.value, "lt"),
            backend->int_type, "zext");
        result.type = INFERRED_BOOL;
        return result;
      case TOKEN_GREATER:
        result.value = LLVMBuildZExt(
            backend->builder,
            LLVMBuildICmp(backend->builder, LLVMIntSGT, L.value, R.value, "gt"),
            backend->int_type, "zext");
        result.type = INFERRED_BOOL;
        return result;
      case TOKEN_LESS_EQUAL:
        result.value = LLVMBuildZExt(
            backend->builder,
            LLVMBuildICmp(backend->builder, LLVMIntSLE, L.value, R.value, "le"),
            backend->int_type, "zext");
        result.type = INFERRED_BOOL;
        return result;
      case TOKEN_GREATER_EQUAL:
        result.value = LLVMBuildZExt(
            backend->builder,
            LLVMBuildICmp(backend->builder, LLVMIntSGE, L.value, R.value, "ge"),
            backend->int_type, "zext");
        result.type = INFERRED_BOOL;
        return result;
      case TOKEN_EQUAL:
        result.value = LLVMBuildZExt(
            backend->builder,
            LLVMBuildICmp(backend->builder, LLVMIntEQ, L.value, R.value, "eq"),
            backend->int_type, "zext");
        result.type = INFERRED_BOOL;
        return result;
      case TOKEN_NOT_EQUAL:
        result.value = LLVMBuildZExt(
            backend->builder,
            LLVMBuildICmp(backend->builder, LLVMIntNE, L.value, R.value, "ne"),
            backend->int_type, "zext");
        result.type = INFERRED_BOOL;
        return result;
      default:
        break;
      }
    }

    // Fall back to boxed path
    LLVMValueRef L_boxed = box_typed_value(backend, L);
    LLVMValueRef R_boxed = box_typed_value(backend, R);
    result.boxed = codegen_expression(backend, node); // Use existing path
    result.value = result.boxed;
    return result;
  }

  default:
    // Fall back to boxed codegen
    result.boxed = codegen_expression(backend, node);
    result.value = result.boxed;
    return result;
  }
}

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

    // Check if index is a string literal (object key access)
    // In that case, use generic vm_get_element for object property access
    if (node->index && node->index->type == AST_STRING_LITERAL) {
      // Object property access: obj["key"] - use generic function
      // vm_get_element_ptr takes (VMValue*, VMValue*) and returns VMValue
      LLVMValueRef target_temp = LLVMBuildAlloca(
          backend->builder, backend->vm_value_type, "target_tmp");
      LLVMBuildStore(backend->builder, left_val, target_temp);
      LLVMValueRef index_temp = LLVMBuildAlloca(
          backend->builder, backend->vm_value_type, "index_tmp");
      LLVMBuildStore(backend->builder, idx_val, index_temp);

      LLVMValueRef args[] = {target_temp, index_temp};
      return LLVMBuildCall2(
          backend->builder,
          LLVMGlobalGetValueType(backend->func_vm_get_element),
          backend->func_vm_get_element, args, 2, "obj_element");
    }

    // ============================================================
    // SAFE ELEMENT ACCESS - Use runtime function for all access
    // This handles arrays, strings, and objects correctly with type checking
    // ============================================================

    // Use generic vm_get_element_ptr for all element access
    // This function properly handles: arrays (int index), strings (int index),
    // and objects (string key)
    LLVMValueRef target_temp =
        LLVMBuildAlloca(backend->builder, backend->vm_value_type, "target_tmp");
    LLVMBuildStore(backend->builder, left_val, target_temp);
    LLVMValueRef index_temp =
        LLVMBuildAlloca(backend->builder, backend->vm_value_type, "index_tmp");
    LLVMBuildStore(backend->builder, idx_val, index_temp);

    LLVMValueRef args[] = {target_temp, index_temp};
    return LLVMBuildCall2(backend->builder,
                          LLVMGlobalGetValueType(backend->func_vm_get_element),
                          backend->func_vm_get_element, args, 2, "element");
  }

  case AST_INCREMENT: {
    LLVMValueRef val_ptr = get_local(backend, node->name);
    if (val_ptr) {
      LLVMValueRef old_val = LLVMBuildLoad2(
          backend->builder, backend->vm_value_type, val_ptr, node->name);
      // Convert to int, add 1, convert back
      LLVMValueRef int_val = llvm_extract_vm_val_int(backend, old_val);
      LLVMValueRef new_int =
          LLVMBuildAdd(backend->builder, int_val,
                       LLVMConstInt(backend->int_type, 1, 0), "inc");
      LLVMValueRef new_val = llvm_vm_val_int_val(backend, new_int);
      LLVMBuildStore(backend->builder, new_val, val_ptr);
      return old_val; // Post-increment returns old value
    }
    fprintf(stderr, "Undefined var in increment: %s\n", node->name);
    return llvm_vm_val_int(backend, 0);
  }

  case AST_DECREMENT: {
    LLVMValueRef val_ptr = get_local(backend, node->name);
    if (val_ptr) {
      LLVMValueRef old_val = LLVMBuildLoad2(
          backend->builder, backend->vm_value_type, val_ptr, node->name);
      LLVMValueRef int_val = llvm_extract_vm_val_int(backend, old_val);
      LLVMValueRef new_int =
          LLVMBuildSub(backend->builder, int_val,
                       LLVMConstInt(backend->int_type, 1, 0), "dec");
      LLVMValueRef new_val = llvm_vm_val_int_val(backend, new_int);
      LLVMBuildStore(backend->builder, new_val, val_ptr);
      return old_val;
    }
    fprintf(stderr, "Undefined var in decrement: %s\n", node->name);
    return llvm_vm_val_int(backend, 0);
  }

  case AST_COMPOUND_ASSIGN: {
    LLVMValueRef val_ptr = get_local(backend, node->name);
    if (!val_ptr) {
      fprintf(stderr, "Undefined var in compound assign: %s\n", node->name);
      return llvm_vm_val_int(backend, 0);
    }

    LLVMValueRef old_val = LLVMBuildLoad2(
        backend->builder, backend->vm_value_type, val_ptr, node->name);
    LLVMValueRef right_val = codegen_expression(backend, node->right);

    // Perform operation: old op right
    // Reuse binary op logic via runtime call
    LLVMValueRef args[] = {
        LLVMConstPointerNull(backend->ptr_type), old_val, right_val,
        LLVMConstInt(backend->int32_type, node->op, 0),
        LLVMConstPointerNull(backend->ptr_type)}; // res_void not needed for
                                                  // call? NO, wrapper needs it?

    // Use vm_binary_op wrapper or direct call?
    // vm_binary_op(vm, L, R, op, res_out)
    LLVMValueRef res_ptr =
        LLVMBuildAlloca(backend->builder, backend->vm_value_type, "res_ptr");
    LLVMValueRef res_void = LLVMBuildBitCast(backend->builder, res_ptr,
                                             backend->ptr_type, "res_void");

    // Bitcast L/R to void*
    LLVMValueRef L_ptr =
        LLVMBuildAlloca(backend->builder, backend->vm_value_type, "L_ptr");
    LLVMBuildStore(backend->builder, old_val, L_ptr);
    LLVMValueRef L_void =
        LLVMBuildBitCast(backend->builder, L_ptr, backend->ptr_type, "L_void");

    LLVMValueRef R_ptr =
        LLVMBuildAlloca(backend->builder, backend->vm_value_type, "R_ptr");
    LLVMBuildStore(backend->builder, right_val, R_ptr);
    LLVMValueRef R_void =
        LLVMBuildBitCast(backend->builder, R_ptr, backend->ptr_type, "R_void");

    LLVMValueRef bin_args[] = {
        LLVMConstPointerNull(backend->ptr_type), L_void, R_void,
        LLVMConstInt(backend->int32_type, node->op, 0), res_void};

    LLVMBuildCall2(backend->builder, backend->vm_binary_op_type,
                   backend->func_vm_binary_op, bin_args, 5, "");

    LLVMValueRef new_val = LLVMBuildLoad2(
        backend->builder, backend->vm_value_type, res_ptr, "compound_res");
    LLVMBuildStore(backend->builder, new_val, val_ptr);
    return new_val;
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

    // Extract types for fast path checking
    LLVMValueRef l_type =
        LLVMBuildExtractValue(backend->builder, L, 0, "l_type");
    LLVMValueRef r_type =
        LLVMBuildExtractValue(backend->builder, R, 0, "r_type");

    // Check if both are INT (type == 0)
    LLVMValueRef l_is_int =
        LLVMBuildICmp(backend->builder, LLVMIntEQ, l_type,
                      LLVMConstInt(backend->int32_type, 0, 0), "l_int");
    LLVMValueRef r_is_int =
        LLVMBuildICmp(backend->builder, LLVMIntEQ, r_type,
                      LLVMConstInt(backend->int32_type, 0, 0), "r_int");
    LLVMValueRef both_int =
        LLVMBuildAnd(backend->builder, l_is_int, r_is_int, "both_int");

    // Check if both are FLOAT (type == 1)
    LLVMValueRef l_is_float =
        LLVMBuildICmp(backend->builder, LLVMIntEQ, l_type,
                      LLVMConstInt(backend->int32_type, 1, 0), "l_float");
    LLVMValueRef r_is_float =
        LLVMBuildICmp(backend->builder, LLVMIntEQ, r_type,
                      LLVMConstInt(backend->int32_type, 1, 0), "r_float");
    LLVMValueRef both_float =
        LLVMBuildAnd(backend->builder, l_is_float, r_is_float, "both_float");

    LLVMValueRef func = backend->current_function;
    LLVMBasicBlockRef int_block = LLVMAppendBasicBlock(func, "op_int");
    LLVMBasicBlockRef float_block = LLVMAppendBasicBlock(func, "op_float");
    LLVMBasicBlockRef fallback_block =
        LLVMAppendBasicBlock(func, "op_fallback");
    LLVMBasicBlockRef merge_block = LLVMAppendBasicBlock(func, "op_merge");

    // Branch: int -> int_block, else check float
    LLVMBasicBlockRef check_float_block =
        LLVMAppendBasicBlock(func, "check_float");
    LLVMBuildCondBr(backend->builder, both_int, int_block, check_float_block);

    // Check float block
    LLVMPositionBuilderAtEnd(backend->builder, check_float_block);
    LLVMBuildCondBr(backend->builder, both_float, float_block, fallback_block);

    // --- Integer Block ---
    // VMValue struct: {i32 type, pad, i64 as}
    LLVMPositionBuilderAtEnd(backend->builder, int_block);
    LLVMValueRef l_val = LLVMBuildExtractValue(backend->builder, L, 2, "l_val");
    LLVMValueRef r_val = LLVMBuildExtractValue(backend->builder, R, 2, "r_val");
    LLVMValueRef int_res = NULL;
    int is_bool_res = 0;

    switch (node->op) {
    case TOKEN_PLUS:
      int_res = LLVMBuildAdd(backend->builder, l_val, r_val, "add");
      break;
    case TOKEN_MINUS:
      int_res = LLVMBuildSub(backend->builder, l_val, r_val, "sub");
      break;
    case TOKEN_MULTIPLY:
      int_res = LLVMBuildMul(backend->builder, l_val, r_val, "mul");
      break;
    case TOKEN_DIVIDE:
      int_res = LLVMBuildSDiv(backend->builder, l_val, r_val, "div");
      break;
    case TOKEN_EQUAL:
      int_res = LLVMBuildZExt(
          backend->builder,
          LLVMBuildICmp(backend->builder, LLVMIntEQ, l_val, r_val, "eq"),
          backend->int_type, "zext");
      is_bool_res = 1;
      break;
    case TOKEN_NOT_EQUAL:
      int_res = LLVMBuildZExt(
          backend->builder,
          LLVMBuildICmp(backend->builder, LLVMIntNE, l_val, r_val, "neq"),
          backend->int_type, "zext");
      is_bool_res = 1;
      break;
    case TOKEN_LESS:
      int_res = LLVMBuildZExt(
          backend->builder,
          LLVMBuildICmp(backend->builder, LLVMIntSLT, l_val, r_val, "lt"),
          backend->int_type, "zext");
      is_bool_res = 1;
      break;
    case TOKEN_GREATER:
      int_res = LLVMBuildZExt(
          backend->builder,
          LLVMBuildICmp(backend->builder, LLVMIntSGT, l_val, r_val, "gt"),
          backend->int_type, "zext");
      is_bool_res = 1;
      break;
    case TOKEN_LESS_EQUAL:
      int_res = LLVMBuildZExt(
          backend->builder,
          LLVMBuildICmp(backend->builder, LLVMIntSLE, l_val, r_val, "le"),
          backend->int_type, "zext");
      is_bool_res = 1;
      break;
    case TOKEN_GREATER_EQUAL:
      int_res = LLVMBuildZExt(
          backend->builder,
          LLVMBuildICmp(backend->builder, LLVMIntSGE, l_val, r_val, "ge"),
          backend->int_type, "zext");
      is_bool_res = 1;
      break;
    case TOKEN_AND:
      // Logical AND: both non-zero -> 1, else 0
      {
        LLVMValueRef l_nz =
            LLVMBuildICmp(backend->builder, LLVMIntNE, l_val,
                          LLVMConstInt(backend->int_type, 0, 0), "l_nz");
        LLVMValueRef r_nz =
            LLVMBuildICmp(backend->builder, LLVMIntNE, r_val,
                          LLVMConstInt(backend->int_type, 0, 0), "r_nz");
        LLVMValueRef and_res =
            LLVMBuildAnd(backend->builder, l_nz, r_nz, "and");
        int_res = LLVMBuildZExt(backend->builder, and_res, backend->int_type,
                                "zext_and");
        is_bool_res = 1;
      }
      break;
    case TOKEN_OR:
      // Logical OR: any non-zero -> 1, else 0
      {
        LLVMValueRef l_nz =
            LLVMBuildICmp(backend->builder, LLVMIntNE, l_val,
                          LLVMConstInt(backend->int_type, 0, 0), "l_nz");
        LLVMValueRef r_nz =
            LLVMBuildICmp(backend->builder, LLVMIntNE, r_val,
                          LLVMConstInt(backend->int_type, 0, 0), "r_nz");
        LLVMValueRef or_res = LLVMBuildOr(backend->builder, l_nz, r_nz, "or");
        int_res = LLVMBuildZExt(backend->builder, or_res, backend->int_type,
                                "zext_or");
        is_bool_res = 1;
      }
      break;
    default:
      int_res = NULL;
    }

    LLVMValueRef int_vm_res;
    if (int_res) {
      if (is_bool_res) {
        // Result is BOOL (type 2)
        int_vm_res = llvm_vm_val_bool(backend, 0); // dummy init
        // Manually build struct to avoid constant restrictions if needed,
        // but llvm_vm_val_int_val handles runtime values for INT, need one for
        // BOOL?

        LLVMValueRef s = LLVMGetUndef(backend->vm_value_type);
        s = LLVMBuildInsertValue(backend->builder, s,
                                 LLVMConstInt(backend->int32_type, 2, 0), 0,
                                 "");
        s = LLVMBuildInsertValue(backend->builder, s, int_res, 2, "");
        int_vm_res = s;
      } else {
        // Result is INT (type 0)
        int_vm_res = llvm_vm_val_int_val(backend, int_res);
      }
      LLVMBuildBr(backend->builder, merge_block);
    } else {
      // Op not supported for fast path
      LLVMBuildBr(backend->builder, fallback_block);
    }
    LLVMBasicBlockRef int_block_end = LLVMGetInsertBlock(backend->builder);

    // --- Float Block (Fast Path for float operations) ---
    LLVMPositionBuilderAtEnd(backend->builder, float_block);
    LLVMValueRef l_float_bits =
        LLVMBuildExtractValue(backend->builder, L, 2, "l_float_bits");
    LLVMValueRef r_float_bits =
        LLVMBuildExtractValue(backend->builder, R, 2, "r_float_bits");
    // Reinterpret i64 bits as double
    LLVMValueRef l_float = LLVMBuildBitCast(backend->builder, l_float_bits,
                                            LLVMDoubleType(), "l_double");
    LLVMValueRef r_float = LLVMBuildBitCast(backend->builder, r_float_bits,
                                            LLVMDoubleType(), "r_double");

    LLVMValueRef float_res = NULL;
    int float_is_bool = 0;

    switch (node->op) {
    case TOKEN_PLUS:
      float_res = LLVMBuildFAdd(backend->builder, l_float, r_float, "fadd");
      break;
    case TOKEN_MINUS:
      float_res = LLVMBuildFSub(backend->builder, l_float, r_float, "fsub");
      break;
    case TOKEN_MULTIPLY:
      float_res = LLVMBuildFMul(backend->builder, l_float, r_float, "fmul");
      break;
    case TOKEN_DIVIDE:
      float_res = LLVMBuildFDiv(backend->builder, l_float, r_float, "fdiv");
      break;
    case TOKEN_LESS:
      float_res =
          LLVMBuildFCmp(backend->builder, LLVMRealOLT, l_float, r_float, "flt");
      float_is_bool = 1;
      break;
    case TOKEN_GREATER:
      float_res =
          LLVMBuildFCmp(backend->builder, LLVMRealOGT, l_float, r_float, "fgt");
      float_is_bool = 1;
      break;
    case TOKEN_LESS_EQUAL:
      float_res =
          LLVMBuildFCmp(backend->builder, LLVMRealOLE, l_float, r_float, "fle");
      float_is_bool = 1;
      break;
    case TOKEN_GREATER_EQUAL:
      float_res =
          LLVMBuildFCmp(backend->builder, LLVMRealOGE, l_float, r_float, "fge");
      float_is_bool = 1;
      break;
    case TOKEN_EQUAL:
      float_res =
          LLVMBuildFCmp(backend->builder, LLVMRealOEQ, l_float, r_float, "feq");
      float_is_bool = 1;
      break;
    case TOKEN_NOT_EQUAL:
      float_res =
          LLVMBuildFCmp(backend->builder, LLVMRealONE, l_float, r_float, "fne");
      float_is_bool = 1;
      break;
    default:
      float_res = NULL;
    }

    LLVMValueRef float_vm_res;
    if (float_res) {
      if (float_is_bool) {
        // Result is BOOL (type 2)
        LLVMValueRef bool_ext = LLVMBuildZExt(backend->builder, float_res,
                                              backend->int_type, "bool_zext");
        LLVMValueRef s = LLVMGetUndef(backend->vm_value_type);
        s = LLVMBuildInsertValue(backend->builder, s,
                                 LLVMConstInt(backend->int32_type, 2, 0), 0,
                                 "");
        s = LLVMBuildInsertValue(backend->builder, s, bool_ext, 2, "");
        float_vm_res = s;
      } else {
        // Result is FLOAT (type 1) - convert double back to i64 bits
        LLVMValueRef res_bits = LLVMBuildBitCast(backend->builder, float_res,
                                                 backend->int_type, "res_bits");
        LLVMValueRef s = LLVMGetUndef(backend->vm_value_type);
        s = LLVMBuildInsertValue(backend->builder, s,
                                 LLVMConstInt(backend->int32_type, 1, 0), 0,
                                 "");
        s = LLVMBuildInsertValue(backend->builder, s, res_bits, 2, "");
        float_vm_res = s;
      }
      LLVMBuildBr(backend->builder, merge_block);
    } else {
      // Op not supported for float fast path
      LLVMBuildBr(backend->builder, fallback_block);
    }
    LLVMBasicBlockRef float_block_end = LLVMGetInsertBlock(backend->builder);

    // --- Fallback Block (Runtime Call) ---
    LLVMPositionBuilderAtEnd(backend->builder, fallback_block);

    LLVMValueRef fallback_res;

    // For TOKEN_PLUS, check if both are strings and use fast concat
    if (node->op == TOKEN_PLUS) {
      // Check if both are STRING (type == 4)
      LLVMValueRef l_is_str =
          LLVMBuildICmp(backend->builder, LLVMIntEQ, l_type,
                        LLVMConstInt(backend->int32_type, 4, 0), "l_str");
      LLVMValueRef r_is_str =
          LLVMBuildICmp(backend->builder, LLVMIntEQ, r_type,
                        LLVMConstInt(backend->int32_type, 4, 0), "r_str");
      LLVMValueRef both_str =
          LLVMBuildAnd(backend->builder, l_is_str, r_is_str, "both_str");

      LLVMBasicBlockRef str_concat_block =
          LLVMAppendBasicBlock(func, "str_concat");
      LLVMBasicBlockRef generic_block =
          LLVMAppendBasicBlock(func, "generic_op");
      LLVMBasicBlockRef fallback_merge =
          LLVMAppendBasicBlock(func, "fallback_merge");

      LLVMBuildCondBr(backend->builder, both_str, str_concat_block,
                      generic_block);

      // --- String Concat Fast Path ---
      LLVMPositionBuilderAtEnd(backend->builder, str_concat_block);
      LLVMValueRef str_args[] = {L, R};
      LLVMValueRef str_result = LLVMBuildCall2(
          backend->builder,
          LLVMGlobalGetValueType(backend->func_aot_string_concat_fast),
          backend->func_aot_string_concat_fast, str_args, 2, "str_concat_res");
      LLVMBuildBr(backend->builder, fallback_merge);
      LLVMBasicBlockRef str_block_end = LLVMGetInsertBlock(backend->builder);

      // --- Generic Runtime Path ---
      LLVMPositionBuilderAtEnd(backend->builder, generic_block);
      LLVMValueRef L_ptr =
          llvm_build_alloca_at_entry(backend, backend->vm_value_type, "L_ptr");
      LLVMBuildStore(backend->builder, L, L_ptr);
      LLVMValueRef L_void = LLVMBuildBitCast(backend->builder, L_ptr,
                                             backend->ptr_type, "L_void");

      LLVMValueRef R_ptr =
          llvm_build_alloca_at_entry(backend, backend->vm_value_type, "R_ptr");
      LLVMBuildStore(backend->builder, R, R_ptr);
      LLVMValueRef R_void = LLVMBuildBitCast(backend->builder, R_ptr,
                                             backend->ptr_type, "R_void");

      LLVMValueRef res_ptr = llvm_build_alloca_at_entry(
          backend, backend->vm_value_type, "res_ptr");
      LLVMValueRef res_void = LLVMBuildBitCast(backend->builder, res_ptr,
                                               backend->ptr_type, "res_void");

      LLVMValueRef args[] = {
          LLVMConstPointerNull(backend->ptr_type), L_void, R_void,
          LLVMConstInt(backend->int32_type, node->op, 0), res_void};

      LLVMBuildCall2(backend->builder, backend->vm_binary_op_type,
                     backend->func_vm_binary_op, args, 5, "");
      LLVMValueRef generic_res = LLVMBuildLoad2(
          backend->builder, backend->vm_value_type, res_ptr, "generic_res");
      LLVMBuildBr(backend->builder, fallback_merge);
      LLVMBasicBlockRef generic_block_end =
          LLVMGetInsertBlock(backend->builder);

      // --- Fallback Merge ---
      LLVMPositionBuilderAtEnd(backend->builder, fallback_merge);
      LLVMValueRef fallback_phi = LLVMBuildPhi(
          backend->builder, backend->vm_value_type, "fallback_phi");
      LLVMValueRef fb_vals[] = {str_result, generic_res};
      LLVMBasicBlockRef fb_blocks[] = {str_block_end, generic_block_end};
      LLVMAddIncoming(fallback_phi, fb_vals, fb_blocks, 2);
      fallback_res = fallback_phi;
    } else {
      // Non-PLUS operations - use generic path
      LLVMValueRef L_ptr =
          llvm_build_alloca_at_entry(backend, backend->vm_value_type, "L_ptr");
      LLVMBuildStore(backend->builder, L, L_ptr);
      LLVMValueRef L_void = LLVMBuildBitCast(backend->builder, L_ptr,
                                             backend->ptr_type, "L_void");

      LLVMValueRef R_ptr =
          llvm_build_alloca_at_entry(backend, backend->vm_value_type, "R_ptr");
      LLVMBuildStore(backend->builder, R, R_ptr);
      LLVMValueRef R_void = LLVMBuildBitCast(backend->builder, R_ptr,
                                             backend->ptr_type, "R_void");

      LLVMValueRef res_ptr = llvm_build_alloca_at_entry(
          backend, backend->vm_value_type, "res_ptr");
      LLVMValueRef res_void = LLVMBuildBitCast(backend->builder, res_ptr,
                                               backend->ptr_type, "res_void");

      LLVMValueRef args[] = {
          LLVMConstPointerNull(backend->ptr_type), L_void, R_void,
          LLVMConstInt(backend->int32_type, node->op, 0), res_void};

      LLVMBuildCall2(backend->builder, backend->vm_binary_op_type,
                     backend->func_vm_binary_op, args, 5, "");
      fallback_res = LLVMBuildLoad2(backend->builder, backend->vm_value_type,
                                    res_ptr, "fallback_res");
    }

    LLVMBuildBr(backend->builder, merge_block);
    LLVMBasicBlockRef fallback_block_end = LLVMGetInsertBlock(backend->builder);

    // --- Merge Block ---
    LLVMPositionBuilderAtEnd(backend->builder, merge_block);
    LLVMValueRef phi =
        LLVMBuildPhi(backend->builder, backend->vm_value_type, "op_res");

    // Add incoming values based on which paths were valid
    if (int_res && float_res) {
      // Both int and float fast paths valid
      LLVMValueRef incoming_vals[] = {int_vm_res, float_vm_res, fallback_res};
      LLVMBasicBlockRef incoming_blocks[] = {int_block_end, float_block_end,
                                             fallback_block_end};
      LLVMAddIncoming(phi, incoming_vals, incoming_blocks, 3);
    } else if (int_res) {
      // Only int fast path valid
      LLVMValueRef incoming_vals[] = {int_vm_res, fallback_res};
      LLVMBasicBlockRef incoming_blocks[] = {int_block_end, fallback_block_end};
      LLVMAddIncoming(phi, incoming_vals, incoming_blocks, 2);
    } else if (float_res) {
      // Only float fast path valid
      LLVMValueRef incoming_vals[] = {float_vm_res, fallback_res};
      LLVMBasicBlockRef incoming_blocks[] = {float_block_end,
                                             fallback_block_end};
      LLVMAddIncoming(phi, incoming_vals, incoming_blocks, 2);
    } else {
      // Only fallback was valid
      LLVMAddIncoming(phi, &fallback_res, &fallback_block_end, 1);
    }

    return phi;
  }

  case AST_UNARY_OP: {
    LLVMValueRef operand = codegen_expression(backend, node->left);
    if (!operand)
      return llvm_vm_val_int(backend, 0);

    // Logical NOT
    if (node->op == TOKEN_BANG) {
      LLVMValueRef truthy = llvm_build_is_truthy(backend, operand);
      LLVMValueRef inverted =
          LLVMBuildNot(backend->builder, truthy, "not_truthy");
      return llvm_vm_val_bool_val(backend, inverted);
    }

    // Unary minus for int/float
    if (node->op == TOKEN_MINUS) {
      LLVMValueRef type_val =
          LLVMBuildExtractValue(backend->builder, operand, 0, "unary_type");
      LLVMValueRef is_int = LLVMBuildICmp(
          backend->builder, LLVMIntEQ, type_val,
          LLVMConstInt(backend->int32_type, 0, 0), "unary_is_int");
      LLVMValueRef is_float = LLVMBuildICmp(
          backend->builder, LLVMIntEQ, type_val,
          LLVMConstInt(backend->int32_type, 1, 0), "unary_is_float");

      LLVMValueRef func = backend->current_function;
      if (!func)
        func = LLVMGetBasicBlockParent(LLVMGetInsertBlock(backend->builder));

      LLVMBasicBlockRef check_float =
          LLVMAppendBasicBlock(func, "unary_check_float");
      LLVMBasicBlockRef int_block = LLVMAppendBasicBlock(func, "unary_int");
      LLVMBasicBlockRef float_block = LLVMAppendBasicBlock(func, "unary_float");
      LLVMBasicBlockRef fallback_block =
          LLVMAppendBasicBlock(func, "unary_fallback");
      LLVMBasicBlockRef merge_block = LLVMAppendBasicBlock(func, "unary_merge");

      LLVMBuildCondBr(backend->builder, is_int, int_block, check_float);

      LLVMPositionBuilderAtEnd(backend->builder, check_float);
      LLVMBuildCondBr(backend->builder, is_float, float_block, fallback_block);

      LLVMPositionBuilderAtEnd(backend->builder, int_block);
      LLVMValueRef int_val = llvm_extract_vm_val_int(backend, operand);
      LLVMValueRef neg_int = LLVMBuildNeg(backend->builder, int_val, "neg_int");
      LLVMValueRef int_vm = llvm_vm_val_int_val(backend, neg_int);
      LLVMBuildBr(backend->builder, merge_block);
      LLVMBasicBlockRef int_end = LLVMGetInsertBlock(backend->builder);

      LLVMPositionBuilderAtEnd(backend->builder, float_block);
      LLVMValueRef float_bits = LLVMBuildExtractValue(backend->builder, operand,
                                                      1, "unary_float_bits");
      LLVMValueRef float_val = LLVMBuildBitCast(
          backend->builder, float_bits, backend->float_type, "unary_float");
      LLVMValueRef neg_float =
          LLVMBuildFNeg(backend->builder, float_val, "neg_float");
      LLVMValueRef float_vm = llvm_build_vm_val_float(backend, neg_float);
      LLVMBuildBr(backend->builder, merge_block);
      LLVMBasicBlockRef float_end = LLVMGetInsertBlock(backend->builder);

      LLVMPositionBuilderAtEnd(backend->builder, fallback_block);
      LLVMValueRef fallback_vm = llvm_vm_val_int(backend, 0);
      LLVMBuildBr(backend->builder, merge_block);
      LLVMBasicBlockRef fallback_end = LLVMGetInsertBlock(backend->builder);

      LLVMPositionBuilderAtEnd(backend->builder, merge_block);
      LLVMValueRef phi = LLVMBuildPhi(backend->builder, backend->vm_value_type,
                                      "unary_minus_res");
      LLVMValueRef incoming_vals[] = {int_vm, float_vm, fallback_vm};
      LLVMBasicBlockRef incoming_blocks[] = {int_end, float_end, fallback_end};
      LLVMAddIncoming(phi, incoming_vals, incoming_blocks, 3);
      return phi;
    }

    return llvm_vm_val_int(backend, 0);
  }

  // TODO: Function Call Update
  case AST_FUNCTION_CALL: {
    // Check for builtin "print" function
    if (node->name && strcmp(node->name, "print") == 0) {
      // Generate print calls for each argument on the same line
      for (int i = 0; i < node->argument_count; i++) {
        ASTNode *arg = node->arguments[i];

        // Add space between arguments (except first)
        if (i > 0) {
          LLVMValueRef space_fmt =
              LLVMBuildGlobalStringPtr(backend->builder, " ", "space_fmt");
          LLVMValueRef space_args[] = {space_fmt};
          LLVMBuildCall2(backend->builder,
                         LLVMGlobalGetValueType(backend->func_printf),
                         backend->func_printf, space_args, 1, "");
        }

        // Special case: string literal - use printf directly (no newline)
        if (arg->type == AST_STRING_LITERAL) {
          LLVMValueRef fmt =
              LLVMBuildGlobalStringPtr(backend->builder, "%s", "fmt_str");
          LLVMValueRef str = LLVMBuildGlobalStringPtr(
              backend->builder, arg->value.string_value, "str_arg");
          LLVMValueRef printf_args[] = {fmt, str};
          LLVMBuildCall2(backend->builder,
                         LLVMGlobalGetValueType(backend->func_printf),
                         backend->func_printf, printf_args, 2, "");
        } else {
          // Other types: use print_value_inline(VMValue*) - no newline
          // Need to pass pointer for ABI compatibility
          LLVMValueRef arg_val = codegen_expression(backend, arg);
          if (arg_val) {
            // Alloca, store, then pass pointer
            LLVMValueRef temp = LLVMBuildAlloca(
                backend->builder, backend->vm_value_type, "print_temp");
            LLVMBuildStore(backend->builder, arg_val, temp);
            LLVMValueRef args[] = {temp};
            LLVMBuildCall2(
                backend->builder,
                LLVMGlobalGetValueType(backend->func_print_value_inline),
                backend->func_print_value_inline, args, 1, "");
          }
        }
      }
      // Print newline at end of print statement
      LLVMBuildCall2(backend->builder,
                     LLVMGlobalGetValueType(backend->func_print_newline),
                     backend->func_print_newline, NULL, 0, "");
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

    // clock_ms() -> float
    if (node->name && strcmp(node->name, "clock_ms") == 0) {
      return LLVMBuildCall2(backend->builder,
                            LLVMGlobalGetValueType(backend->func_aot_clock_ms),
                            backend->func_aot_clock_ms, NULL, 0, "clock_res");
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

    // push(array, value) - pointer ABI
    if (node->name && strcmp(node->name, "push") == 0 &&
        node->argument_count >= 2) {
      LLVMValueRef arr = codegen_expression(backend, node->arguments[0]);
      LLVMValueRef val = codegen_expression(backend, node->arguments[1]);

      // Allocate temps and store values for pointer ABI
      LLVMValueRef arr_ptr = LLVMBuildAlloca(
          backend->builder, backend->vm_value_type, "push_arr_ptr");
      LLVMBuildStore(backend->builder, arr, arr_ptr);
      LLVMValueRef val_ptr = LLVMBuildAlloca(
          backend->builder, backend->vm_value_type, "push_val_ptr");
      LLVMBuildStore(backend->builder, val, val_ptr);

      LLVMValueRef args[] = {arr_ptr, val_ptr};
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

    // Socket Functions
    if (strcmp(node->name, "socket_server") == 0) {
      LLVMValueRef args[] = {codegen_expression(backend, node->arguments[0]),
                             codegen_expression(backend, node->arguments[1])};
      return LLVMBuildCall2(
          backend->builder,
          LLVMGlobalGetValueType(backend->func_aot_socket_server),
          backend->func_aot_socket_server, args, 2, "sock_fd");
    }
    if (strcmp(node->name, "socket_client") == 0) {
      LLVMValueRef args[] = {codegen_expression(backend, node->arguments[0]),
                             codegen_expression(backend, node->arguments[1])};
      return LLVMBuildCall2(
          backend->builder,
          LLVMGlobalGetValueType(backend->func_aot_socket_client),
          backend->func_aot_socket_client, args, 2, "sock_fd");
    }
    if (strcmp(node->name, "socket_accept") == 0) {
      LLVMValueRef args[] = {codegen_expression(backend, node->arguments[0])};
      return LLVMBuildCall2(
          backend->builder,
          LLVMGlobalGetValueType(backend->func_aot_socket_accept),
          backend->func_aot_socket_accept, args, 1, "client_fd");
    }
    if (strcmp(node->name, "socket_send") == 0) {
      LLVMValueRef args[] = {codegen_expression(backend, node->arguments[0]),
                             codegen_expression(backend, node->arguments[1])};
      return LLVMBuildCall2(
          backend->builder,
          LLVMGlobalGetValueType(backend->func_aot_socket_send),
          backend->func_aot_socket_send, args, 2, "bytes_sent");
    }
    if (strcmp(node->name, "socket_receive") == 0) {
      LLVMValueRef args[] = {codegen_expression(backend, node->arguments[0]),
                             codegen_expression(backend, node->arguments[1])};
      return LLVMBuildCall2(
          backend->builder,
          LLVMGlobalGetValueType(backend->func_aot_socket_receive),
          backend->func_aot_socket_receive, args, 2, "recv_data");
    }
    if (strcmp(node->name, "socket_close") == 0) {
      LLVMValueRef args[] = {codegen_expression(backend, node->arguments[0])};
      LLVMBuildCall2(backend->builder,
                     LLVMGlobalGetValueType(backend->func_aot_socket_close),
                     backend->func_aot_socket_close, args, 1, "");
      return llvm_vm_val_int(backend, 0);
    }

    // ====== Threading Functions ======
    // thread_create(func_name, arg) -> thread_id
    if (strcmp(node->name, "thread_create") == 0 && node->argument_count >= 2) {
      // First arg should be function name, get its pointer
      LLVMValueRef func_ptr = NULL;
      if (node->arguments[0]->type == AST_IDENTIFIER) {
        LLVMValueRef func =
            LLVMGetNamedFunction(backend->module, node->arguments[0]->name);
        if (func) {
          func_ptr = func;
        }
      }
      if (!func_ptr) {
        func_ptr = LLVMConstPointerNull(backend->ptr_type);
      }
      LLVMValueRef arg = codegen_expression(backend, node->arguments[1]);
      LLVMValueRef args[] = {func_ptr, arg};
      return LLVMBuildCall2(
          backend->builder,
          LLVMGlobalGetValueType(backend->func_aot_thread_create),
          backend->func_aot_thread_create, args, 2, "thread_id");
    }

    // thread_join(thread_id)
    if (strcmp(node->name, "thread_join") == 0 && node->argument_count >= 1) {
      LLVMValueRef args[] = {codegen_expression(backend, node->arguments[0])};
      LLVMBuildCall2(backend->builder,
                     LLVMGlobalGetValueType(backend->func_aot_thread_join),
                     backend->func_aot_thread_join, args, 1, "");
      return llvm_vm_val_int(backend, 0);
    }

    // thread_detach(thread_id)
    if (strcmp(node->name, "thread_detach") == 0 && node->argument_count >= 1) {
      LLVMValueRef args[] = {codegen_expression(backend, node->arguments[0])};
      LLVMBuildCall2(backend->builder,
                     LLVMGlobalGetValueType(backend->func_aot_thread_detach),
                     backend->func_aot_thread_detach, args, 1, "");
      return llvm_vm_val_int(backend, 0);
    }

    // mutex_create() -> mutex_ptr
    if (strcmp(node->name, "mutex_create") == 0) {
      return LLVMBuildCall2(
          backend->builder,
          LLVMGlobalGetValueType(backend->func_aot_mutex_create),
          backend->func_aot_mutex_create, NULL, 0, "mutex_ptr");
    }

    // mutex_lock(mutex)
    if (strcmp(node->name, "mutex_lock") == 0 && node->argument_count >= 1) {
      LLVMValueRef args[] = {codegen_expression(backend, node->arguments[0])};
      LLVMBuildCall2(backend->builder,
                     LLVMGlobalGetValueType(backend->func_aot_mutex_lock),
                     backend->func_aot_mutex_lock, args, 1, "");
      return llvm_vm_val_int(backend, 0);
    }

    // mutex_unlock(mutex)
    if (strcmp(node->name, "mutex_unlock") == 0 && node->argument_count >= 1) {
      LLVMValueRef args[] = {codegen_expression(backend, node->arguments[0])};
      LLVMBuildCall2(backend->builder,
                     LLVMGlobalGetValueType(backend->func_aot_mutex_unlock),
                     backend->func_aot_mutex_unlock, args, 1, "");
      return llvm_vm_val_int(backend, 0);
    }

    // mutex_destroy(mutex)
    if (strcmp(node->name, "mutex_destroy") == 0 && node->argument_count >= 1) {
      LLVMValueRef args[] = {codegen_expression(backend, node->arguments[0])};
      LLVMBuildCall2(backend->builder,
                     LLVMGlobalGetValueType(backend->func_aot_mutex_destroy),
                     backend->func_aot_mutex_destroy, args, 1, "");
      return llvm_vm_val_int(backend, 0);
    }

    // ====== HTTP Functions ======
    // http_parse_request(raw) -> object
    if (strcmp(node->name, "http_parse_request") == 0 &&
        node->argument_count >= 1) {
      LLVMValueRef args[] = {codegen_expression(backend, node->arguments[0])};
      return LLVMBuildCall2(
          backend->builder,
          LLVMGlobalGetValueType(backend->func_aot_http_parse_request),
          backend->func_aot_http_parse_request, args, 1, "parsed_req");
    }

    // http_create_response(status, content_type, body) -> string
    if (strcmp(node->name, "http_create_response") == 0 &&
        node->argument_count >= 3) {
      LLVMValueRef args[] = {codegen_expression(backend, node->arguments[0]),
                             codegen_expression(backend, node->arguments[1]),
                             codegen_expression(backend, node->arguments[2])};
      return LLVMBuildCall2(
          backend->builder,
          LLVMGlobalGetValueType(backend->func_aot_http_create_response),
          backend->func_aot_http_create_response, args, 3, "http_resp");
    }

// ====== Math Functions - Single Param ======
#define MATH1_FUNC(func_name, field)                                           \
  if (strcmp(node->name, func_name) == 0 && node->argument_count >= 1) {       \
    LLVMValueRef m1_args[] = {                                                 \
        codegen_expression(backend, node->arguments[0])};                      \
    return LLVMBuildCall2(backend->builder,                                    \
                          LLVMGlobalGetValueType(backend->field),              \
                          backend->field, m1_args, 1, func_name "_res");       \
  }

    // abs(v) -> passed by pointer with result pointer
    if (strcmp(node->name, "abs") == 0 && node->argument_count >= 1) {
      LLVMValueRef arg = codegen_expression(backend, node->arguments[0]);

      LLVMValueRef arg_temp =
          LLVMBuildAlloca(backend->builder, backend->vm_value_type, "abs_arg");
      LLVMBuildStore(backend->builder, arg, arg_temp);

      LLVMValueRef res_temp = LLVMBuildAlloca(
          backend->builder, backend->vm_value_type, "abs_res_slot");

      LLVMValueRef args[] = {res_temp, arg_temp};
      LLVMBuildCall2(backend->builder,
                     LLVMGlobalGetValueType(backend->func_aot_math_abs),
                     backend->func_aot_math_abs, args, 2, "");

      return LLVMBuildLoad2(backend->builder, backend->vm_value_type, res_temp,
                            "abs_res");
    }

    MATH1_FUNC("sqrt", func_aot_math_sqrt)
    MATH1_FUNC("floor", func_aot_math_floor)
    MATH1_FUNC("ceil", func_aot_math_ceil)
    MATH1_FUNC("round", func_aot_math_round)
    MATH1_FUNC("sin", func_aot_math_sin)
    MATH1_FUNC("cos", func_aot_math_cos)
    MATH1_FUNC("tan", func_aot_math_tan)
    MATH1_FUNC("asin", func_aot_math_asin)
    MATH1_FUNC("acos", func_aot_math_acos)
    MATH1_FUNC("atan", func_aot_math_atan)
    MATH1_FUNC("exp", func_aot_math_exp)
    MATH1_FUNC("log", func_aot_math_log)
    MATH1_FUNC("log10", func_aot_math_log10)
    MATH1_FUNC("log2", func_aot_math_log2)
    MATH1_FUNC("sinh", func_aot_math_sinh)
    MATH1_FUNC("cosh", func_aot_math_cosh)
    MATH1_FUNC("tanh", func_aot_math_tanh)
    MATH1_FUNC("cbrt", func_aot_math_cbrt)
    MATH1_FUNC("trunc", func_aot_math_trunc)

#undef MATH1_FUNC

// ====== Math Functions - Two Params ======
#define MATH2_FUNC(func_name, field)                                           \
  if (strcmp(node->name, func_name) == 0 && node->argument_count >= 2) {       \
    LLVMValueRef m2_args[] = {                                                 \
        codegen_expression(backend, node->arguments[0]),                       \
        codegen_expression(backend, node->arguments[1])};                      \
    return LLVMBuildCall2(backend->builder,                                    \
                          LLVMGlobalGetValueType(backend->field),              \
                          backend->field, m2_args, 2, func_name "_res");       \
  }

    MATH2_FUNC("pow", func_aot_math_pow)
    MATH2_FUNC("atan2", func_aot_math_atan2)
    MATH2_FUNC("hypot", func_aot_math_hypot)
    MATH2_FUNC("fmod", func_aot_math_fmod)
    MATH2_FUNC("min", func_aot_math_min)
    MATH2_FUNC("max", func_aot_math_max)
    MATH2_FUNC("randint", func_aot_math_randint)

#undef MATH2_FUNC

    // random() -> float (0.0 - 1.0)
    if (strcmp(node->name, "random") == 0) {
      return LLVMBuildCall2(
          backend->builder,
          LLVMGlobalGetValueType(backend->func_aot_math_random),
          backend->func_aot_math_random, NULL, 0, "random_res");
    }

// ====== String Functions - Single Param ======
#define STR1_FUNC(func_name, field)                                            \
  if (strcmp(node->name, func_name) == 0 && node->argument_count >= 1) {       \
    LLVMValueRef s1_args[] = {                                                 \
        codegen_expression(backend, node->arguments[0])};                      \
    return LLVMBuildCall2(backend->builder,                                    \
                          LLVMGlobalGetValueType(backend->field),              \
                          backend->field, s1_args, 1, func_name "_res");       \
  }

    STR1_FUNC("upper", func_aot_string_upper)
    STR1_FUNC("lower", func_aot_string_lower)
    STR1_FUNC("reverse", func_aot_string_reverse)
    STR1_FUNC("isEmpty", func_aot_string_is_empty)
    STR1_FUNC("capitalize", func_aot_string_capitalize)
    STR1_FUNC("isDigit", func_aot_string_is_digit)
    STR1_FUNC("isAlpha", func_aot_string_is_alpha)

#undef STR1_FUNC

// ====== String Functions - Two Params ======
#define STR2_FUNC(func_name, field)                                            \
  if (strcmp(node->name, func_name) == 0 && node->argument_count >= 2) {       \
    LLVMValueRef s2_args[] = {                                                 \
        codegen_expression(backend, node->arguments[0]),                       \
        codegen_expression(backend, node->arguments[1])};                      \
    return LLVMBuildCall2(backend->builder,                                    \
                          LLVMGlobalGetValueType(backend->field),              \
                          backend->field, s2_args, 2, func_name "_res");       \
  }

    STR2_FUNC("contains", func_aot_string_contains)
    STR2_FUNC("startsWith", func_aot_string_starts_with)
    STR2_FUNC("endsWith", func_aot_string_ends_with)
    STR2_FUNC("indexOf", func_aot_string_index_of)
    STR2_FUNC("repeat", func_aot_string_repeat)
    STR2_FUNC("count", func_aot_string_count)
    STR2_FUNC("join", func_aot_string_join)

#undef STR2_FUNC

    // substring(str, start, end) -> string
    if (strcmp(node->name, "substring") == 0 && node->argument_count >= 3) {
      LLVMValueRef args[] = {codegen_expression(backend, node->arguments[0]),
                             codegen_expression(backend, node->arguments[1]),
                             codegen_expression(backend, node->arguments[2])};
      return LLVMBuildCall2(
          backend->builder,
          LLVMGlobalGetValueType(backend->func_aot_string_substring),
          backend->func_aot_string_substring, args, 3, "substring_res");
    }

    // ====== Time Functions ======
    // timestamp() -> int (unix timestamp)
    if (strcmp(node->name, "timestamp") == 0) {
      return LLVMBuildCall2(
          backend->builder, LLVMGlobalGetValueType(backend->func_aot_timestamp),
          backend->func_aot_timestamp, NULL, 0, "timestamp_res");
    }

    // time_ms() -> int (milliseconds)
    if (strcmp(node->name, "time_ms") == 0) {
      return LLVMBuildCall2(backend->builder,
                            LLVMGlobalGetValueType(backend->func_aot_time_ms),
                            backend->func_aot_time_ms, NULL, 0, "time_ms_res");
    }

    // sleep(ms)
    if (strcmp(node->name, "sleep") == 0 && node->argument_count >= 1) {
      LLVMValueRef args[] = {codegen_expression(backend, node->arguments[0])};
      LLVMBuildCall2(backend->builder,
                     LLVMGlobalGetValueType(backend->func_aot_sleep),
                     backend->func_aot_sleep, args, 1, "");
      return llvm_vm_val_int(backend, 0);
    }

    // ====== JSON Functions ======
    // fromJson(str) -> object/array
    if (strcmp(node->name, "fromJson") == 0 && node->argument_count >= 1) {
      LLVMValueRef args[] = {codegen_expression(backend, node->arguments[0])};
      return LLVMBuildCall2(
          backend->builder, LLVMGlobalGetValueType(backend->func_aot_from_json),
          backend->func_aot_from_json, args, 1, "from_json_res");
    }

    // ====== Input Functions ======
    // input_int(prompt) -> int
    if (strcmp(node->name, "input_int") == 0) {
      LLVMValueRef prompt =
          node->argument_count >= 1
              ? codegen_expression(backend, node->arguments[0])
              : llvm_vm_val_int(backend, 0);
      LLVMValueRef args[] = {prompt};
      return LLVMBuildCall2(
          backend->builder, LLVMGlobalGetValueType(backend->func_aot_input_int),
          backend->func_aot_input_int, args, 1, "input_int_res");
    }

    // input_float(prompt) -> float
    if (strcmp(node->name, "input_float") == 0) {
      LLVMValueRef prompt =
          node->argument_count >= 1
              ? codegen_expression(backend, node->arguments[0])
              : llvm_vm_val_int(backend, 0);
      LLVMValueRef args[] = {prompt};
      return LLVMBuildCall2(
          backend->builder,
          LLVMGlobalGetValueType(backend->func_aot_input_float),
          backend->func_aot_input_float, args, 1, "input_float_res");
    }

    // ====== Range Function ======
    // range(end) -> array [0, 1, ..., end-1]
    if (strcmp(node->name, "range") == 0 && node->argument_count >= 1) {
      LLVMValueRef args[] = {codegen_expression(backend, node->arguments[0])};
      return LLVMBuildCall2(backend->builder,
                            LLVMGlobalGetValueType(backend->func_aot_range),
                            backend->func_aot_range, args, 1, "range_res");
    }

    // ====== SQLite Database Functions ======
    // db_open(path) -> db_handle
    if (strcmp(node->name, "db_open") == 0 && node->argument_count >= 1) {
      LLVMValueRef args[] = {codegen_expression(backend, node->arguments[0])};
      return LLVMBuildCall2(backend->builder,
                            LLVMGlobalGetValueType(backend->func_aot_db_open),
                            backend->func_aot_db_open, args, 1, "db_handle");
    }

    // db_close(db)
    if (strcmp(node->name, "db_close") == 0 && node->argument_count >= 1) {
      LLVMValueRef args[] = {codegen_expression(backend, node->arguments[0])};
      LLVMBuildCall2(backend->builder,
                     LLVMGlobalGetValueType(backend->func_aot_db_close),
                     backend->func_aot_db_close, args, 1, "");
      return llvm_vm_val_int(backend, 0);
    }

    // db_execute(db, sql) -> bool
    if (strcmp(node->name, "db_execute") == 0 && node->argument_count >= 2) {
      LLVMValueRef args[] = {codegen_expression(backend, node->arguments[0]),
                             codegen_expression(backend, node->arguments[1])};
      return LLVMBuildCall2(
          backend->builder,
          LLVMGlobalGetValueType(backend->func_aot_db_execute),
          backend->func_aot_db_execute, args, 2, "db_exec_res");
    }

    // db_query(db, sql) -> array of objects
    if (strcmp(node->name, "db_query") == 0 && node->argument_count >= 2) {
      LLVMValueRef args[] = {codegen_expression(backend, node->arguments[0]),
                             codegen_expression(backend, node->arguments[1])};
      return LLVMBuildCall2(
          backend->builder, LLVMGlobalGetValueType(backend->func_aot_db_query),
          backend->func_aot_db_query, args, 2, "db_query_res");
    }

    // db_last_insert_id(db) -> int64
    if (strcmp(node->name, "db_last_insert_id") == 0 &&
        node->argument_count >= 1) {
      LLVMValueRef args[] = {codegen_expression(backend, node->arguments[0])};
      return LLVMBuildCall2(
          backend->builder,
          LLVMGlobalGetValueType(backend->func_aot_db_last_insert_id),
          backend->func_aot_db_last_insert_id, args, 1, "db_last_id");
    }

    // db_error(db) -> string
    if (strcmp(node->name, "db_error") == 0 && node->argument_count >= 1) {
      LLVMValueRef args[] = {codegen_expression(backend, node->arguments[0])};
      return LLVMBuildCall2(backend->builder,
                            LLVMGlobalGetValueType(backend->func_aot_db_error),
                            backend->func_aot_db_error, args, 1, "db_err");
    }

// ====== Type Checking Functions ======
#define TYPE_CHECK_FUNC(func_name, field)                                      \
  if (strcmp(node->name, func_name) == 0 && node->argument_count >= 1) {       \
    LLVMValueRef tc_args[] = {                                                 \
        codegen_expression(backend, node->arguments[0])};                      \
    return LLVMBuildCall2(backend->builder,                                    \
                          LLVMGlobalGetValueType(backend->field),              \
                          backend->field, tc_args, 1, func_name "_res");       \
  }

    TYPE_CHECK_FUNC("typeof", func_aot_typeof)
    TYPE_CHECK_FUNC("isInt", func_aot_is_int)
    TYPE_CHECK_FUNC("isFloat", func_aot_is_float)
    TYPE_CHECK_FUNC("isString", func_aot_is_string)
    TYPE_CHECK_FUNC("isArray", func_aot_is_array)
    TYPE_CHECK_FUNC("isObject", func_aot_is_object)
    TYPE_CHECK_FUNC("isBool", func_aot_is_bool)

#undef TYPE_CHECK_FUNC

    // ====== StringBuilder Functions ======
    // StringBuilder(capacity) -> ptr (stored as int in VMValue)
    if (strcmp(node->name, "StringBuilder") == 0) {
      int capacity = 1024; // default
      if (node->argument_count >= 1) {
        LLVMValueRef cap_val = codegen_expression(backend, node->arguments[0]);
        LLVMValueRef cap_int = llvm_extract_vm_val_int(backend, cap_val);
        capacity = 0; // Will use runtime value
        LLVMValueRef cap_i32 = LLVMBuildTrunc(backend->builder, cap_int,
                                              backend->int32_type, "cap_i32");
        LLVMValueRef args[] = {cap_i32};
        LLVMValueRef sb_ptr = LLVMBuildCall2(
            backend->builder,
            LLVMGlobalGetValueType(backend->func_aot_stringbuilder_new),
            backend->func_aot_stringbuilder_new, args, 1, "sb_ptr");
        // Store pointer as int64 in VMValue
        LLVMValueRef ptr_as_int = LLVMBuildPtrToInt(
            backend->builder, sb_ptr, backend->int_type, "ptr_int");
        return llvm_vm_val_int_val(backend, ptr_as_int);
      } else {
        LLVMValueRef args[] = {LLVMConstInt(backend->int32_type, 1024, 0)};
        LLVMValueRef sb_ptr = LLVMBuildCall2(
            backend->builder,
            LLVMGlobalGetValueType(backend->func_aot_stringbuilder_new),
            backend->func_aot_stringbuilder_new, args, 1, "sb_ptr");
        LLVMValueRef ptr_as_int = LLVMBuildPtrToInt(
            backend->builder, sb_ptr, backend->int_type, "ptr_int");
        return llvm_vm_val_int_val(backend, ptr_as_int);
      }
    }

    // sb_append(sb, value) -> void
    if (strcmp(node->name, "sb_append") == 0 && node->argument_count >= 2) {
      LLVMValueRef sb_val = codegen_expression(backend, node->arguments[0]);
      LLVMValueRef val = codegen_expression(backend, node->arguments[1]);
      // Extract pointer from VMValue (stored as int64)
      LLVMValueRef ptr_int = llvm_extract_vm_val_int(backend, sb_val);
      LLVMValueRef sb_ptr = LLVMBuildIntToPtr(backend->builder, ptr_int,
                                              backend->ptr_type, "sb_ptr");
      LLVMValueRef args[] = {sb_ptr, val};
      LLVMBuildCall2(backend->builder,
                     LLVMGlobalGetValueType(
                         backend->func_aot_stringbuilder_append_vmvalue),
                     backend->func_aot_stringbuilder_append_vmvalue, args, 2,
                     "");
      return llvm_vm_val_int(backend, 0);
    }

    // sb_tostring(sb) -> VMValue string
    if (strcmp(node->name, "sb_tostring") == 0 && node->argument_count >= 1) {
      LLVMValueRef sb_val = codegen_expression(backend, node->arguments[0]);
      LLVMValueRef ptr_int = llvm_extract_vm_val_int(backend, sb_val);
      LLVMValueRef sb_ptr = LLVMBuildIntToPtr(backend->builder, ptr_int,
                                              backend->ptr_type, "sb_ptr");
      LLVMValueRef args[] = {sb_ptr};
      return LLVMBuildCall2(
          backend->builder,
          LLVMGlobalGetValueType(backend->func_aot_stringbuilder_to_string),
          backend->func_aot_stringbuilder_to_string, args, 1, "sb_str");
    }

    // sb_free(sb) -> void
    if (strcmp(node->name, "sb_free") == 0 && node->argument_count >= 1) {
      LLVMValueRef sb_val = codegen_expression(backend, node->arguments[0]);
      LLVMValueRef ptr_int = llvm_extract_vm_val_int(backend, sb_val);
      LLVMValueRef sb_ptr = LLVMBuildIntToPtr(backend->builder, ptr_int,
                                              backend->ptr_type, "sb_ptr");
      LLVMValueRef args[] = {sb_ptr};
      LLVMBuildCall2(
          backend->builder,
          LLVMGlobalGetValueType(backend->func_aot_stringbuilder_free),
          backend->func_aot_stringbuilder_free, args, 1, "");
      return llvm_vm_val_int(backend, 0);
    }

    // User-defined or Runtime function call
    LLVMValueRef func = LLVMGetNamedFunction(backend->module, node->name);
    if (func) {
      // Check if it is a registered user function (uses NEW ABI: void
      // func(ret*, args*...))
      LLVMTypeRef user_func_type = NULL;
      for (int i = 0; i < backend->function_count; i++) {
        if (strcmp(backend->functions[i].name, node->name) == 0) {
          user_func_type = backend->functions[i].type;
          break;
        }
      }

      if (user_func_type) {
        // Check if this is a native ABI function (returns i64, not void)
        LLVMTypeRef ret_type = LLVMGetReturnType(user_func_type);
        if (ret_type == backend->int_type) {
          // --- NATIVE ABI: i64 func(i64, i64, ...) ---
          int arg_count = node->argument_count;
          LLVMValueRef *args = NULL;
          if (arg_count > 0) {
            args = (LLVMValueRef *)malloc(sizeof(LLVMValueRef) * arg_count);
            for (int i = 0; i < arg_count; i++) {
              LLVMValueRef val = codegen_expression(backend, node->arguments[i]);
              // Extract i64 from VMValue
              args[i] = LLVMBuildExtractValue(backend->builder, val, 2, "arg_i64");
            }
          }
          
          LLVMValueRef result = LLVMBuildCall2(backend->builder, user_func_type, func,
                                                args, arg_count, "native_call");
          if (args) free(args);
          
          // Box result back to VMValue
          return llvm_vm_val_int_val(backend, result);
        }
        
        // --- NEW ABI (User Function) ---
        // 1. Allocate Result Slot
        LLVMValueRef res_ptr = LLVMBuildAlloca(
            backend->builder, backend->vm_value_type, "call_res_ptr");

        // 2. Prepare Args: [ResultPtr, Arg1Ptr, Arg2Ptr...]
        int call_arg_count = node->argument_count + 1;
        LLVMValueRef *args = malloc(sizeof(LLVMValueRef) * call_arg_count);
        args[0] = res_ptr;

        for (int i = 0; i < node->argument_count; i++) {
          // Evaluate argument
          LLVMValueRef val = codegen_expression(backend, node->arguments[i]);
          // Store to temp alloca to get pointer
          LLVMValueRef arg_temp = LLVMBuildAlloca(
              backend->builder, backend->vm_value_type, "arg_tmp");
          LLVMBuildStore(backend->builder, val, arg_temp);
          args[i + 1] = arg_temp;
        }

        // 3. Call Void Function
        LLVMBuildCall2(backend->builder, user_func_type, func, args,
                       call_arg_count, "");
        free(args);

        // 4. Load Result
        return LLVMBuildLoad2(backend->builder, backend->vm_value_type, res_ptr,
                              "call_res_loaded");
      }

      // --- LEGACY ABI (Runtime Function) ---
      LLVMTypeRef func_type = LLVMGlobalGetValueType(func);

      // Prepare arguments (By Value)
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
    // printf("[AOT] Declaring var: %s\n", node->name);
    LLVMValueRef init = node->right ? codegen_expression(backend, node->right)
                                    : llvm_vm_val_int(backend, 0);
    LLVMValueRef alloca =
        llvm_build_alloca_at_entry(backend, backend->vm_value_type, node->name);
    LLVMBuildStore(backend->builder, init, alloca);
    add_local(backend, node->name, alloca);
    return alloca;
  }
  case AST_ASSIGNMENT: {
    LLVMValueRef val = codegen_expression(backend, node->right);

    // Check for Array/Object Assignment: arr[i] = val OR obj["k"] = val
    if (node->left && node->left->type == AST_ARRAY_ACCESS) {
      ASTNode *access = node->left;
      LLVMValueRef target = NULL;

      // Array access can be either:
      // 1. Simple: arr[i] - name is in access->name, left is NULL
      // 2. Nested: arr[i][j] - left has previous access
      if (access->name) {
        // Simple access: get array from variable
        LLVMValueRef var_ptr = get_local(backend, access->name);
        if (var_ptr) {
          target = LLVMBuildLoad2(backend->builder, backend->vm_value_type,
                                  var_ptr, access->name);
        }
      } else if (access->left) {
        // Nested access: evaluate left expression
        target = codegen_expression(backend, access->left);
      }

      LLVMValueRef index = codegen_expression(backend, access->index);

      if (target && index) {
        // ============================================================
        // SAFE ELEMENT SET - Use runtime function for all set access
        // This handles arrays, strings, and objects correctly with type
        // checking
        // ============================================================

        // Use generic vm_set_element_ptr for all element set
        LLVMValueRef target_temp = LLVMBuildAlloca(
            backend->builder, backend->vm_value_type, "target_set_tmp");
        LLVMBuildStore(backend->builder, target, target_temp);
        LLVMValueRef index_temp = LLVMBuildAlloca(
            backend->builder, backend->vm_value_type, "index_set_tmp");
        LLVMBuildStore(backend->builder, index, index_temp);
        LLVMValueRef val_temp = LLVMBuildAlloca(
            backend->builder, backend->vm_value_type, "val_set_tmp");
        LLVMBuildStore(backend->builder, val, val_temp);

        LLVMValueRef args[] = {LLVMConstNull(backend->ptr_type), target_temp,
                               index_temp, val_temp};
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
      else {
        fprintf(stderr, "Undefined var in assignment: %s\n", node->name);
        // Print known vars
        Scope *s = backend->current_scope;
        while (s) {
          fprintf(stderr, "  Scope dump:");
          for (int i = 0; i < s->count; i++)
            fprintf(stderr, " %s", s->vars[i].name);
          fprintf(stderr, "\n");
          s = s->parent;
        }
      }
    }
    return val;
  }
  case AST_BLOCK: {
    LLVMValueRef last = NULL;
    if (node->statements) {
      for (int i = 0; i < node->statement_count; i++) {
        if (LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(backend->builder)))
          break;
        last = codegen_statement(backend, node->statements[i]);
      }
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
    if (!LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(backend->builder)))
      LLVMBuildBr(backend->builder, mergeB);
    LLVMPositionBuilderAtEnd(backend->builder, elseB);
    if (node->else_branch)
      codegen_statement(backend, node->else_branch);
    if (!LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(backend->builder)))
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
    if (!LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(backend->builder)))
      LLVMBuildBr(backend->builder, condB);
    LLVMPositionBuilderAtEnd(backend->builder, exitB);
    return NULL;
  }
  case AST_FOR: {
    // for (init; condition; increment) { body }
    // Create new scope for loop variable
    enter_scope(backend);

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

    // Exit loop scope
    exit_scope(backend);
    return NULL;
  }
  case AST_RETURN: {
    LLVMValueRef ret =
        node->return_value
            ? codegen_expression(backend, node->return_value)
            : llvm_vm_val_int(backend, 0); // Return 0/Void if no value

    // ABI Change: Store to Result Pointer (Param 0)
    LLVMValueRef res_ptr = LLVMGetParam(backend->current_function, 0);
    LLVMBuildStore(backend->builder, ret, res_ptr);
    return LLVMBuildRetVoid(backend->builder);
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

    // Pop handler on normal exit ONLY if not terminated
    if (!LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(backend->builder))) {
      LLVMBuildCall2(backend->builder,
                     LLVMGlobalGetValueType(backend->func_aot_try_pop),
                     backend->func_aot_try_pop, NULL, 0, "");
      LLVMBuildBr(backend->builder, finallyB ? finallyB : endB);
    }

    // Catch block
    LLVMPositionBuilderAtEnd(backend->builder, catchB);
    if (node->catch_var && node->catch_block) {
      // Get exception: VMValue e = aot_get_exception()
      LLVMValueRef exc = LLVMBuildCall2(
          backend->builder,
          LLVMGlobalGetValueType(backend->func_aot_get_exception),
          backend->func_aot_get_exception, NULL, 0, "exception");
      // Store to catch variable (at entry)
      LLVMValueRef alloca = llvm_build_alloca_at_entry(
          backend, backend->vm_value_type, node->catch_var);
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

// Check if function has all integer parameters (eligible for native ABI)
int is_all_int_params(ASTNode *node) {
  if (!node)
    return 0;
  // Allow zero param functions too
  for (int i = 0; i < node->param_count; i++) {
    if (!node->parameters[i] || node->parameters[i]->data_type != TYPE_INT) {
      return 0;
    }
  }
  return 1;
}

// Native function name (adds "_native" suffix)
char *get_native_func_name(const char *name) {
  size_t len = strlen(name) + 8;
  char *native_name = (char *)malloc(len);
  snprintf(native_name, len, "%s_native", name);
  return native_name;
}

// Generate a pure native function with i64 parameters and return
// This is called BEFORE codegen_func_def to create a fast path
void codegen_native_func_def(LLVMBackend *backend, ASTNode *node) {
  if (!is_all_int_params(node))
    return;

  // Check if function already exists in module (avoid duplicates)
  if (LLVMGetNamedFunction(backend->module, node->name)) {
    return;  // Already defined
  }

  int param_count = node->param_count;

  // Native ABI: i64 func(i64, i64, ...)
  LLVMTypeRef *param_types =
      (LLVMTypeRef *)malloc(sizeof(LLVMTypeRef) * param_count);
  for (int i = 0; i < param_count; i++) {
    param_types[i] = backend->int_type; // All i64
  }

  LLVMTypeRef func_type =
      LLVMFunctionType(backend->int_type, param_types, param_count, 0);

  // Use original name for function (not _native_ prefix) so calls can find it
  LLVMValueRef func = LLVMAddFunction(backend->module, node->name, func_type);
  register_function(backend, node->name, func_type);  // Register for lookups

  // Add optimization attributes
  // nounwind: function doesn't throw exceptions
  // noinline: prevent inlining to preserve function boundaries
  // optnone: CRITICAL - completely disable optimization for this function
  //          This prevents LLVM from eliminating loops via closed-form solutions
  LLVMAddAttributeAtIndex(
      func, LLVMAttributeFunctionIndex,
      LLVMCreateEnumAttribute(
          backend->context, LLVMGetEnumAttributeKindForName("nounwind", 8), 0));
  LLVMAddAttributeAtIndex(
      func, LLVMAttributeFunctionIndex,
      LLVMCreateEnumAttribute(
          backend->context, LLVMGetEnumAttributeKindForName("noinline", 8),
          0));
  LLVMAddAttributeAtIndex(
      func, LLVMAttributeFunctionIndex,
      LLVMCreateEnumAttribute(
          backend->context, LLVMGetEnumAttributeKindForName("optnone", 7),
          0));

  LLVMValueRef prev_func = backend->current_function;
  LLVMBasicBlockRef prev_block = LLVMGetInsertBlock(backend->builder);
  backend->current_function = func;

  LLVMBasicBlockRef entry = LLVMAppendBasicBlock(func, "entry");
  LLVMPositionBuilderAtEnd(backend->builder, entry);

  enter_scope(backend);

  // Add parameters as native i64 locals
  for (int i = 0; i < param_count; i++) {
    if (node->parameters[i]) {
      LLVMValueRef param = LLVMGetParam(func, i);
      LLVMValueRef alloca = LLVMBuildAlloca(backend->builder, backend->int_type,
                                            node->parameters[i]->name);
      LLVMBuildStore(backend->builder, param, alloca);

      // Add as typed local (INFERRED_INT with native alloca)
      add_local_typed(backend, node->parameters[i]->name, NULL, INFERRED_INT,
                      alloca);
    }
  }

  // Generate body using typed expression codegen
  if (node->body && node->body->type == AST_BLOCK && node->body->statements) {
    for (int i = 0; i < node->body->statement_count; i++) {
      ASTNode *stmt = node->body->statements[i];

      if (stmt->type == AST_RETURN) {
        // Use typed codegen for return value
        TypedValue ret = codegen_typed_expr(backend, stmt->return_value);
        if (ret.type == INFERRED_INT || ret.type == INFERRED_BOOL) {
          LLVMBuildRet(backend->builder, ret.value);
        } else {
          // Extract int from boxed value
          LLVMValueRef boxed = box_typed_value(backend, ret);
          LLVMValueRef int_val =
              LLVMBuildExtractValue(backend->builder, boxed, 2, "ret_int");
          LLVMBuildRet(backend->builder, int_val);
        }
      } else if (stmt->type == AST_IF) {
        // Handle if statement in native context
        TypedValue cond = codegen_typed_expr(backend, stmt->condition);
        LLVMValueRef cond_bool;
        if (cond.type == INFERRED_INT || cond.type == INFERRED_BOOL) {
          cond_bool =
              LLVMBuildICmp(backend->builder, LLVMIntNE, cond.value,
                            LLVMConstInt(backend->int_type, 0, 0), "cond");
        } else {
          LLVMValueRef boxed = box_typed_value(backend, cond);
          LLVMValueRef val =
              LLVMBuildExtractValue(backend->builder, boxed, 2, "cond_val");
          cond_bool =
              LLVMBuildICmp(backend->builder, LLVMIntNE, val,
                            LLVMConstInt(backend->int_type, 0, 0), "cond");
        }

        LLVMBasicBlockRef then_bb = LLVMAppendBasicBlock(func, "then");
        LLVMBasicBlockRef else_bb =
            stmt->else_branch ? LLVMAppendBasicBlock(func, "else") : NULL;
        LLVMBasicBlockRef merge_bb = LLVMAppendBasicBlock(func, "merge");

        LLVMBuildCondBr(backend->builder, cond_bool, then_bb,
                        else_bb ? else_bb : merge_bb);

        // Then branch
        LLVMPositionBuilderAtEnd(backend->builder, then_bb);
        if (stmt->then_branch) {
          // Recursively handle then branch (simplified: just handle return)
          if (stmt->then_branch->type == AST_RETURN) {
            TypedValue ret =
                codegen_typed_expr(backend, stmt->then_branch->return_value);
            if (ret.type == INFERRED_INT || ret.type == INFERRED_BOOL) {
              LLVMBuildRet(backend->builder, ret.value);
            } else {
              LLVMValueRef boxed = box_typed_value(backend, ret);
              LLVMValueRef int_val =
                  LLVMBuildExtractValue(backend->builder, boxed, 2, "ret_int");
              LLVMBuildRet(backend->builder, int_val);
            }
          } else if (stmt->then_branch->type == AST_BLOCK &&
                     stmt->then_branch->statements) {
            for (int j = 0; j < stmt->then_branch->statement_count; j++) {
              ASTNode *inner = stmt->then_branch->statements[j];
              if (inner->type == AST_RETURN) {
                TypedValue ret =
                    codegen_typed_expr(backend, inner->return_value);
                if (ret.type == INFERRED_INT || ret.type == INFERRED_BOOL) {
                  LLVMBuildRet(backend->builder, ret.value);
                } else {
                  LLVMValueRef boxed = box_typed_value(backend, ret);
                  LLVMValueRef int_val = LLVMBuildExtractValue(
                      backend->builder, boxed, 2, "ret_int");
                  LLVMBuildRet(backend->builder, int_val);
                }
                break;
              }
            }
          }
        }
        if (!LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(backend->builder)))
          LLVMBuildBr(backend->builder, merge_bb);

        // Else branch
        if (else_bb) {
          LLVMPositionBuilderAtEnd(backend->builder, else_bb);
          // Similar handling for else...
          if (!LLVMGetBasicBlockTerminator(
                  LLVMGetInsertBlock(backend->builder)))
            LLVMBuildBr(backend->builder, merge_bb);
        }

        LLVMPositionBuilderAtEnd(backend->builder, merge_bb);
      } else if (stmt->type == AST_VARIABLE_DECL) {
        // Native variable declaration
        LLVMValueRef alloca = LLVMBuildAlloca(backend->builder, backend->int_type, stmt->name);
        if (stmt->right) {
          TypedValue init = codegen_typed_expr(backend, stmt->right);
          if (init.type == INFERRED_INT || init.type == INFERRED_BOOL) {
            LLVMBuildStore(backend->builder, init.value, alloca);
          } else if (init.boxed) {
            LLVMValueRef int_val = LLVMBuildExtractValue(backend->builder, init.boxed, 2, "init_int");
            LLVMBuildStore(backend->builder, int_val, alloca);
          } else {
            LLVMBuildStore(backend->builder, LLVMConstInt(backend->int_type, 0, 0), alloca);
          }
        } else {
          LLVMBuildStore(backend->builder, LLVMConstInt(backend->int_type, 0, 0), alloca);
        }
        add_local_typed(backend, stmt->name, NULL, INFERRED_INT, alloca);
      } else if (stmt->type == AST_ASSIGNMENT) {
        // Native assignment
        LLVMValueRef var_ptr = get_local_native(backend, stmt->left ? stmt->left->name : stmt->name);
        if (var_ptr) {
          TypedValue val = codegen_typed_expr(backend, stmt->right);
          if (val.type == INFERRED_INT || val.type == INFERRED_BOOL) {
            LLVMBuildStore(backend->builder, val.value, var_ptr);
          } else if (val.boxed) {
            LLVMValueRef int_val = LLVMBuildExtractValue(backend->builder, val.boxed, 2, "assign_int");
            LLVMBuildStore(backend->builder, int_val, var_ptr);
          }
        }
      } else if (stmt->type == AST_FOR) {
        // Native for loop
        // Initialize
        if (stmt->init) {
          if (stmt->init->type == AST_VARIABLE_DECL) {
            LLVMValueRef alloca = LLVMBuildAlloca(backend->builder, backend->int_type, stmt->init->name);
            if (stmt->init->right) {
              TypedValue init = codegen_typed_expr(backend, stmt->init->right);
              if (init.type == INFERRED_INT) {
                LLVMBuildStore(backend->builder, init.value, alloca);
              } else {
                LLVMBuildStore(backend->builder, LLVMConstInt(backend->int_type, 0, 0), alloca);
              }
            } else {
              LLVMBuildStore(backend->builder, LLVMConstInt(backend->int_type, 0, 0), alloca);
            }
            add_local_typed(backend, stmt->init->name, NULL, INFERRED_INT, alloca);
          }
        }
        
        LLVMBasicBlockRef loop_cond = LLVMAppendBasicBlock(func, "for.cond");
        LLVMBasicBlockRef loop_body = LLVMAppendBasicBlock(func, "for.body");
        LLVMBasicBlockRef loop_inc = LLVMAppendBasicBlock(func, "for.inc");
        LLVMBasicBlockRef loop_end = LLVMAppendBasicBlock(func, "for.end");
        
        LLVMBuildBr(backend->builder, loop_cond);
        
        // Condition
        LLVMPositionBuilderAtEnd(backend->builder, loop_cond);
        TypedValue cond = codegen_typed_expr(backend, stmt->condition);
        LLVMValueRef cond_bool;
        if (cond.type == INFERRED_INT || cond.type == INFERRED_BOOL) {
          cond_bool = LLVMBuildICmp(backend->builder, LLVMIntNE, cond.value,
                                    LLVMConstInt(backend->int_type, 0, 0), "for_cond");
        } else {
          cond_bool = LLVMConstInt(backend->bool_type, 1, 0);
        }
        LLVMBuildCondBr(backend->builder, cond_bool, loop_body, loop_end);
        
        // Body
        LLVMPositionBuilderAtEnd(backend->builder, loop_body);
        if (stmt->body && stmt->body->type == AST_BLOCK) {
          for (int j = 0; j < stmt->body->statement_count; j++) {
            ASTNode *body_stmt = stmt->body->statements[j];
            if (body_stmt->type == AST_ASSIGNMENT) {
              LLVMValueRef var_ptr = get_local_native(backend, body_stmt->left ? body_stmt->left->name : body_stmt->name);
              if (var_ptr) {
                TypedValue val = codegen_typed_expr(backend, body_stmt->right);
                if (val.type == INFERRED_INT || val.type == INFERRED_BOOL) {
                  LLVMBuildStore(backend->builder, val.value, var_ptr);
                }
              }
            } else if (body_stmt->type == AST_VARIABLE_DECL) {
              // Variable declaration inside loop body
              LLVMValueRef alloca = LLVMBuildAlloca(backend->builder, backend->int_type, body_stmt->name);
              if (body_stmt->right) {
                TypedValue init = codegen_typed_expr(backend, body_stmt->right);
                if (init.type == INFERRED_INT) {
                  LLVMBuildStore(backend->builder, init.value, alloca);
                } else {
                  LLVMBuildStore(backend->builder, LLVMConstInt(backend->int_type, 0, 0), alloca);
                }
              } else {
                LLVMBuildStore(backend->builder, LLVMConstInt(backend->int_type, 0, 0), alloca);
              }
              add_local_typed(backend, body_stmt->name, NULL, INFERRED_INT, alloca);
            } else if (body_stmt->type == AST_FOR) {
              // Nested for loop - generate inline
              ASTNode *inner = body_stmt;
              
              // Initialize inner loop variable
              if (inner->init && inner->init->type == AST_VARIABLE_DECL) {
                LLVMValueRef inner_alloca = LLVMBuildAlloca(backend->builder, backend->int_type, inner->init->name);
                if (inner->init->right) {
                  TypedValue init_val = codegen_typed_expr(backend, inner->init->right);
                  if (init_val.type == INFERRED_INT) {
                    LLVMBuildStore(backend->builder, init_val.value, inner_alloca);
                  } else {
                    LLVMBuildStore(backend->builder, LLVMConstInt(backend->int_type, 0, 0), inner_alloca);
                  }
                } else {
                  LLVMBuildStore(backend->builder, LLVMConstInt(backend->int_type, 0, 0), inner_alloca);
                }
                add_local_typed(backend, inner->init->name, NULL, INFERRED_INT, inner_alloca);
              }
              
              LLVMBasicBlockRef inner_cond = LLVMAppendBasicBlock(func, "inner.cond");
              LLVMBasicBlockRef inner_body = LLVMAppendBasicBlock(func, "inner.body");
              LLVMBasicBlockRef inner_inc = LLVMAppendBasicBlock(func, "inner.inc");
              LLVMBasicBlockRef inner_end = LLVMAppendBasicBlock(func, "inner.end");
              
              LLVMBuildBr(backend->builder, inner_cond);
              
              // Inner condition
              LLVMPositionBuilderAtEnd(backend->builder, inner_cond);
              TypedValue inner_cond_val = codegen_typed_expr(backend, inner->condition);
              LLVMValueRef inner_cond_bool = LLVMBuildICmp(backend->builder, LLVMIntNE, inner_cond_val.value,
                                        LLVMConstInt(backend->int_type, 0, 0), "inner_cond");
              LLVMBuildCondBr(backend->builder, inner_cond_bool, inner_body, inner_end);
              
              // Inner body
              LLVMPositionBuilderAtEnd(backend->builder, inner_body);
              if (inner->body && inner->body->type == AST_BLOCK) {
                for (int k = 0; k < inner->body->statement_count; k++) {
                  ASTNode *inner_stmt = inner->body->statements[k];
                  if (inner_stmt->type == AST_ASSIGNMENT) {
                    LLVMValueRef var_ptr = get_local_native(backend, inner_stmt->left ? inner_stmt->left->name : inner_stmt->name);
                    if (var_ptr) {
                      TypedValue val = codegen_typed_expr(backend, inner_stmt->right);
                      if (val.type == INFERRED_INT || val.type == INFERRED_BOOL) {
                        LLVMBuildStore(backend->builder, val.value, var_ptr);
                      }
                    }
                  }
                }
              }
              LLVMBuildBr(backend->builder, inner_inc);
              
              // Inner increment
              LLVMPositionBuilderAtEnd(backend->builder, inner_inc);
              if (inner->increment && inner->increment->type == AST_ASSIGNMENT) {
                LLVMValueRef var_ptr = get_local_native(backend, inner->increment->left ? inner->increment->left->name : inner->increment->name);
                if (var_ptr) {
                  TypedValue val = codegen_typed_expr(backend, inner->increment->right);
                  if (val.type == INFERRED_INT || val.type == INFERRED_BOOL) {
                    LLVMBuildStore(backend->builder, val.value, var_ptr);
                  }
                }
              }
              LLVMBuildBr(backend->builder, inner_cond);
              
              // Continue after inner loop
              LLVMPositionBuilderAtEnd(backend->builder, inner_end);
            }
          }
        }
        LLVMBuildBr(backend->builder, loop_inc);
        
        // Increment
        LLVMPositionBuilderAtEnd(backend->builder, loop_inc);
        if (stmt->increment) {
          if (stmt->increment->type == AST_ASSIGNMENT) {
            LLVMValueRef var_ptr = get_local_native(backend, stmt->increment->left ? stmt->increment->left->name : stmt->increment->name);
            if (var_ptr) {
              TypedValue val = codegen_typed_expr(backend, stmt->increment->right);
              if (val.type == INFERRED_INT || val.type == INFERRED_BOOL) {
                LLVMBuildStore(backend->builder, val.value, var_ptr);
              }
            }
          }
        }
        LLVMBuildBr(backend->builder, loop_cond);
        
        LLVMPositionBuilderAtEnd(backend->builder, loop_end);
      }
    }
  }

  // Default return 0
  if (!LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(backend->builder)))
    LLVMBuildRet(backend->builder, LLVMConstInt(backend->int_type, 0, 0));

  exit_scope(backend);
  free(param_types);

  backend->current_function = prev_func;
  if (prev_block)
    LLVMPositionBuilderAtEnd(backend->builder, prev_block);
}

void codegen_func_def(LLVMBackend *backend, ASTNode *node) {
  // Check if function has explicit return type - use native codegen
  if (backend->use_static_typing && node->return_type != TYPE_VOID && 
      node->return_type != TYPE_UNKNOWN) {
    // Verify all parameters have types
    int all_typed = 1;
    for (int i = 0; i < node->param_count; i++) {
      if (node->parameters[i]->data_type == TYPE_UNKNOWN) {
        all_typed = 0;
        break;
      }
    }
    if (all_typed) {
      codegen_native_func_def(backend, node);
      return;
    }
  }
  
  int user_param_count = node->param_count;
  int total_params = user_param_count + 1; // +1 for Result Pointer

  // Param types: [ResultPtr, ArgPtr, ArgPtr...]
  LLVMTypeRef *param_types =
      (LLVMTypeRef *)malloc(sizeof(LLVMTypeRef) * total_params);
  for (int i = 0; i < total_params; i++)
    param_types[i] = backend->ptr_type;

  LLVMTypeRef func_type =
      LLVMFunctionType(backend->void_type, param_types, total_params, 0);

  LLVMValueRef func = LLVMAddFunction(backend->module, node->name, func_type);
  register_function(backend, node->name, func_type);

  // ============================================================
  // INLINING & OPTIMIZATION ATTRIBUTES
  // ============================================================

  // nounwind - function doesn't throw exceptions
  LLVMAddAttributeAtIndex(
      func, LLVMAttributeFunctionIndex,
      LLVMCreateEnumAttribute(
          backend->context, LLVMGetEnumAttributeKindForName("nounwind", 8), 0));

  // All user functions get inlinehint
  LLVMAddAttributeAtIndex(
      func, LLVMAttributeFunctionIndex,
      LLVMCreateEnumAttribute(backend->context,
                              LLVMGetEnumAttributeKindForName("inlinehint", 10),
                              0));

  LLVMValueRef prev_func = backend->current_function;
  LLVMBasicBlockRef prev_block = LLVMGetInsertBlock(backend->builder);
  backend->current_function = func;

  LLVMBasicBlockRef entry = LLVMAppendBasicBlock(func, "entry");
  LLVMPositionBuilderAtEnd(backend->builder, entry);

  enter_scope(backend);

  // Handle parameters
  // Param 0 is Result Pointer (used by return)
  // Params 1..N are User Arguments (pointers)
  for (int i = 0; i < user_param_count; i++) {
    if (node->parameters[i]) {
      // Get pointer to argument (Param i+1)
      LLVMValueRef arg_ptr = LLVMGetParam(func, i + 1);

      // We must load the value to ensure pass-by-value semantics if the var is
      // modified (Or we can treat it as local var pointer if we trust it won't
      // alias?
      //  To be safe and consistent with AST_VARIABLE_DECL, let's copy to local
      //  alloca)

      LLVMValueRef val =
          LLVMBuildLoad2(backend->builder, backend->vm_value_type, arg_ptr,
                         node->parameters[i]->name);

      LLVMValueRef alloca = LLVMBuildAlloca(
          backend->builder, backend->vm_value_type, node->parameters[i]->name);
      LLVMBuildStore(backend->builder, val, alloca);
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

  // Verify module
  char *verify_error = NULL;
  if (LLVMVerifyModule(backend->module, LLVMPrintMessageAction,
                       &verify_error) != 0) {
    fprintf(stderr, "Global module verification failed: %s\n", verify_error);
    LLVMDisposeMessage(verify_error);
    // Continue anyway to see if it links? No, it usually crashes.
    // return 1;
  }

  if (LLVMTargetMachineEmitToFile(machine, backend->module, filename,
                                  LLVMObjectFile, &error) != 0) {
    fprintf(stderr, "Error emitting object file: %s\n", error);
    return 1;
  }
  LLVMDisposeTargetMachine(machine);
  LLVMDisposeMessage(triple);
  return 0;
}

// Optimization Pass enabling using new LLVM Pass Manager
void llvm_backend_optimize(LLVMBackend *backend) {
  // Create pass builder options
  LLVMPassBuilderOptionsRef options = LLVMCreatePassBuilderOptions();

  // Set optimization options
  LLVMPassBuilderOptionsSetVerifyEach(options, 0);
  LLVMPassBuilderOptionsSetDebugLogging(options, 0);
  LLVMPassBuilderOptionsSetLoopInterleaving(options, 1);
  LLVMPassBuilderOptionsSetLoopVectorization(options, 1);
  LLVMPassBuilderOptionsSetSLPVectorization(options, 1);
  LLVMPassBuilderOptionsSetLoopUnrolling(options, 0);
  LLVMPassBuilderOptionsSetForgetAllSCEVInLoopUnroll(options, 0);
  LLVMPassBuilderOptionsSetMergeFunctions(options, 1);

  // O2 + selective passes - no inline/loop-unroll to preserve loop structure
  // This prevents LLVM from computing closed-form solutions for simple loops
  LLVMErrorRef error = LLVMRunPasses(
      backend->module, "default<O2>,function(sroa,gvn,instcombine)",
      NULL, options);

  if (error) {
    char *msg = LLVMGetErrorMessage(error);
    fprintf(stderr, "[AOT] Warning: Optimization failed: %s\n", msg);
    LLVMDisposeErrorMessage(msg);
  } else {
    printf("[AOT] Optimizations (O3) applied successfully.\n");
  }

  LLVMDisposePassBuilderOptions(options);
}

// ============================================================================
// STATIC TYPING / NATIVE TYPE CODEGEN
// ============================================================================

void llvm_backend_enable_static_typing(LLVMBackend *backend) {
  backend->use_static_typing = 1;
  
  // Declare native runtime functions
  // tulpar_print_int(i64) -> void
  LLVMTypeRef print_int_params[] = {backend->int_type};
  LLVMTypeRef print_int_type = LLVMFunctionType(backend->void_type, print_int_params, 1, 0);
  backend->func_tulpar_print_int = LLVMAddFunction(backend->module, "tulpar_print_int", print_int_type);
  backend->func_tulpar_println_int = LLVMAddFunction(backend->module, "tulpar_println_int", print_int_type);
  
  // tulpar_print_float(double) -> void
  LLVMTypeRef print_float_params[] = {backend->float_type};
  LLVMTypeRef print_float_type = LLVMFunctionType(backend->void_type, print_float_params, 1, 0);
  backend->func_tulpar_print_float = LLVMAddFunction(backend->module, "tulpar_print_float", print_float_type);
  backend->func_tulpar_println_float = LLVMAddFunction(backend->module, "tulpar_println_float", print_float_type);
  
  // tulpar_print_bool(i8) -> void
  LLVMTypeRef print_bool_params[] = {backend->bool_type};
  LLVMTypeRef print_bool_type = LLVMFunctionType(backend->void_type, print_bool_params, 1, 0);
  backend->func_tulpar_print_bool = LLVMAddFunction(backend->module, "tulpar_print_bool", print_bool_type);
  backend->func_tulpar_println_bool = LLVMAddFunction(backend->module, "tulpar_println_bool", print_bool_type);
  
  // tulpar_print_string(i8*) -> void
  LLVMTypeRef print_str_params[] = {backend->string_type};
  LLVMTypeRef print_str_type = LLVMFunctionType(backend->void_type, print_str_params, 1, 0);
  backend->func_tulpar_print_string = LLVMAddFunction(backend->module, "tulpar_print_string", print_str_type);
  backend->func_tulpar_println_string = LLVMAddFunction(backend->module, "tulpar_println_string", print_str_type);
  
  // tulpar_print_newline() -> void
  LLVMTypeRef print_nl_type = LLVMFunctionType(backend->void_type, NULL, 0, 0);
  backend->func_tulpar_print_newline = LLVMAddFunction(backend->module, "tulpar_print_newline", print_nl_type);
  
  // tulpar_string_concat(i8*, i8*) -> i8*
  LLVMTypeRef concat_params[] = {backend->string_type, backend->string_type};
  LLVMTypeRef concat_type = LLVMFunctionType(backend->string_type, concat_params, 2, 0);
  backend->func_tulpar_string_concat = LLVMAddFunction(backend->module, "tulpar_string_concat", concat_type);
  
  // tulpar_string_length(i8*) -> i64
  LLVMTypeRef strlen_params[] = {backend->string_type};
  LLVMTypeRef strlen_type = LLVMFunctionType(backend->int_type, strlen_params, 1, 0);
  backend->func_tulpar_string_length = LLVMAddFunction(backend->module, "tulpar_string_length", strlen_type);
  
  // tulpar_clock_ms() -> double
  LLVMTypeRef clock_type = LLVMFunctionType(backend->float_type, NULL, 0, 0);
  backend->func_tulpar_clock_ms = LLVMAddFunction(backend->module, "tulpar_clock_ms", clock_type);
}

// Convert DataType to LLVM type
LLVMTypeRef datatype_to_llvm(LLVMBackend *backend, DataType type) {
  switch (type) {
    case TYPE_INT:    return backend->int_type;
    case TYPE_FLOAT:  return backend->float_type;
    case TYPE_BOOL:   return backend->bool_type;
    case TYPE_STRING: return backend->string_type;
    case TYPE_VOID:   return backend->void_type;
    default:          return backend->vm_value_type; // Fallback for complex types
  }
}

// Convert DataType to InferredType
InferredType datatype_to_inferred(DataType type) {
  switch (type) {
    case TYPE_INT:    return INFERRED_INT;
    case TYPE_FLOAT:  return INFERRED_FLOAT;
    case TYPE_BOOL:   return INFERRED_BOOL;
    case TYPE_STRING: return INFERRED_STRING;
    case TYPE_ARRAY:
    case TYPE_ARRAY_INT:
    case TYPE_ARRAY_FLOAT:
    case TYPE_ARRAY_STR:
    case TYPE_ARRAY_BOOL:
      return INFERRED_ARRAY;
    default:          return INFERRED_UNKNOWN;
  }
}

