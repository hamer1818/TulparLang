// Tulpar LLVM Backend - Core Interface

#ifndef TULPAR_LLVM_BACKEND_H
#define TULPAR_LLVM_BACKEND_H

#include "../parser/parser.h"
#include <llvm-c/Core.h>
#include <llvm-c/Target.h>
#include <llvm-c/TargetMachine.h>

// Known type enumeration for type inference
typedef enum {
  INFERRED_UNKNOWN = 0, // Runtime type, use VMValue
  INFERRED_INT,         // Known int64
  INFERRED_FLOAT,       // Known double
  INFERRED_BOOL,        // Known bool
  INFERRED_STRING,      // Known string (still boxed)
  INFERRED_ARRAY        // Known array (still boxed)
} InferredType;

// Scope definition with type tracking
typedef struct {
  char *name;
  LLVMValueRef value;        // Alloca or direct value
  InferredType known_type;   // Known type for unboxing
  LLVMValueRef native_value; // Unboxed native value (i64/double) when known
} LocalVar;

typedef struct Scope {
  LocalVar vars[256];
  int count;
  struct Scope *parent;
} Scope;

typedef struct {
  char *name;
  LLVMTypeRef type;
  InferredType return_type;  // Known return type
  InferredType *param_types; // Known parameter types
  int param_count;
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
  LLVMValueRef func_print_value; // Helper: print_value(VMValue) with newline
  LLVMValueRef
      func_print_value_inline; // Helper: print_value_inline(VMValue) no newline
  LLVMValueRef func_print_newline; // Helper: print_newline()
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

  // Fast Array Access (value-based, no alloca needed)
  LLVMValueRef func_aot_array_get_fast;
  LLVMValueRef func_aot_array_set_fast;

  // RAW Pointer Array Access (Maximum Performance - no bounds check)
  LLVMValueRef func_aot_array_get_raw;      // Get ObjArray* from VMValue
  LLVMValueRef func_aot_array_get_raw_fast; // Direct access via ObjArray*
  LLVMValueRef func_aot_array_set_raw_fast; // Direct set via ObjArray*

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

  // Time functions
  LLVMValueRef func_aot_clock_ms;

  // Socket Functions
  LLVMValueRef func_aot_socket_server;
  LLVMValueRef func_aot_socket_client;
  LLVMValueRef func_aot_socket_accept;
  LLVMValueRef func_aot_socket_send;
  LLVMValueRef func_aot_socket_receive;
  LLVMValueRef func_aot_socket_close;

  // Fast String Operations
  LLVMValueRef func_aot_string_concat_fast;

  // StringBuilder Functions
  LLVMValueRef func_aot_stringbuilder_new;
  LLVMValueRef func_aot_stringbuilder_append;
  LLVMValueRef func_aot_stringbuilder_append_vmvalue;
  LLVMValueRef func_aot_stringbuilder_to_string;
  LLVMValueRef func_aot_stringbuilder_free;

  // Threading Functions
  LLVMValueRef func_aot_thread_create;
  LLVMValueRef func_aot_thread_join;
  LLVMValueRef func_aot_thread_detach;
  LLVMValueRef func_aot_mutex_create;
  LLVMValueRef func_aot_mutex_lock;
  LLVMValueRef func_aot_mutex_unlock;
  LLVMValueRef func_aot_mutex_destroy;

  // HTTP Functions
  LLVMValueRef func_aot_http_parse_request;
  LLVMValueRef func_aot_http_create_response;

  // Math Functions - Single param
  LLVMValueRef func_aot_math_abs;
  LLVMValueRef func_aot_math_sqrt;
  LLVMValueRef func_aot_math_floor;
  LLVMValueRef func_aot_math_ceil;
  LLVMValueRef func_aot_math_round;
  LLVMValueRef func_aot_math_sin;
  LLVMValueRef func_aot_math_cos;
  LLVMValueRef func_aot_math_tan;
  LLVMValueRef func_aot_math_asin;
  LLVMValueRef func_aot_math_acos;
  LLVMValueRef func_aot_math_atan;
  LLVMValueRef func_aot_math_exp;
  LLVMValueRef func_aot_math_log;
  LLVMValueRef func_aot_math_log10;
  LLVMValueRef func_aot_math_log2;
  LLVMValueRef func_aot_math_sinh;
  LLVMValueRef func_aot_math_cosh;
  LLVMValueRef func_aot_math_tanh;
  LLVMValueRef func_aot_math_cbrt;
  LLVMValueRef func_aot_math_trunc;

  // Math Functions - Two params
  LLVMValueRef func_aot_math_pow;
  LLVMValueRef func_aot_math_atan2;
  LLVMValueRef func_aot_math_hypot;
  LLVMValueRef func_aot_math_fmod;
  LLVMValueRef func_aot_math_min;
  LLVMValueRef func_aot_math_max;
  LLVMValueRef func_aot_math_randint;

  // Math Functions - No param
  LLVMValueRef func_aot_math_random;

  // String Functions - Single param
  LLVMValueRef func_aot_string_upper;
  LLVMValueRef func_aot_string_lower;
  LLVMValueRef func_aot_string_reverse;
  LLVMValueRef func_aot_string_is_empty;
  LLVMValueRef func_aot_string_capitalize;
  LLVMValueRef func_aot_string_is_digit;
  LLVMValueRef func_aot_string_is_alpha;

  // String Functions - Two params
  LLVMValueRef func_aot_string_contains;
  LLVMValueRef func_aot_string_starts_with;
  LLVMValueRef func_aot_string_ends_with;
  LLVMValueRef func_aot_string_index_of;
  LLVMValueRef func_aot_string_repeat;
  LLVMValueRef func_aot_string_count;
  LLVMValueRef func_aot_string_join;

  // String Functions - Three params
  LLVMValueRef func_aot_string_substring;

  // Time Functions
  LLVMValueRef func_aot_timestamp;
  LLVMValueRef func_aot_time_ms;
  LLVMValueRef func_aot_sleep;

  // JSON Functions
  LLVMValueRef func_aot_from_json;

  // Input Functions
  LLVMValueRef func_aot_input_int;
  LLVMValueRef func_aot_input_float;

  // Range Function
  LLVMValueRef func_aot_range;

  // SQLite Database Functions
  LLVMValueRef func_aot_db_open;
  LLVMValueRef func_aot_db_close;
  LLVMValueRef func_aot_db_execute;
  LLVMValueRef func_aot_db_query;
  LLVMValueRef func_aot_db_last_insert_id;
  LLVMValueRef func_aot_db_error;

  // Type Checking Functions
  LLVMValueRef func_aot_typeof;
  LLVMValueRef func_aot_is_int;
  LLVMValueRef func_aot_is_float;
  LLVMValueRef func_aot_is_string;
  LLVMValueRef func_aot_is_array;
  LLVMValueRef func_aot_is_object;
  LLVMValueRef func_aot_is_bool;

  // ============================================================================
  // Native Type Runtime Functions (No VMValue boxing)
  // ============================================================================
  LLVMValueRef func_tulpar_print_int;
  LLVMValueRef func_tulpar_print_float;
  LLVMValueRef func_tulpar_print_bool;
  LLVMValueRef func_tulpar_print_string;
  LLVMValueRef func_tulpar_println_int;
  LLVMValueRef func_tulpar_println_float;
  LLVMValueRef func_tulpar_println_bool;
  LLVMValueRef func_tulpar_println_string;
  LLVMValueRef func_tulpar_print_newline;
  
  LLVMValueRef func_tulpar_string_concat;
  LLVMValueRef func_tulpar_string_length;
  LLVMValueRef func_tulpar_clock_ms;
  
  // Static typing mode flag
  int use_static_typing;  // 1 = use native types, 0 = use VMValue

} LLVMBackend;

LLVMBackend *llvm_backend_create(const char *module_name);
void llvm_backend_destroy(LLVMBackend *backend);
void llvm_backend_compile(LLVMBackend *backend, ASTNode *node);
void llvm_backend_optimize(LLVMBackend *backend);
int llvm_backend_emit_object(LLVMBackend *backend, const char *filename);
int llvm_backend_emit_ir_file(LLVMBackend *backend, const char *filename);

// Enable static typing mode for native performance
void llvm_backend_enable_static_typing(LLVMBackend *backend);

// Helper functions for testing
void enter_scope(LLVMBackend *backend);
void exit_scope(LLVMBackend *backend);
void add_local(LLVMBackend *backend, const char *name, LLVMValueRef val);
void add_local_typed(LLVMBackend *backend, const char *name, LLVMValueRef val, 
                     InferredType type, LLVMValueRef native_val);
LLVMValueRef get_local(LLVMBackend *backend, const char *name);
InferredType get_local_type(LLVMBackend *backend, const char *name);
LLVMValueRef codegen_expression(LLVMBackend *backend, ASTNode *node);
LLVMValueRef codegen_statement(LLVMBackend *backend, ASTNode *node);

// Native type codegen (static typing)
LLVMValueRef codegen_native_expr(LLVMBackend *backend, ASTNode *node, DataType expected_type);
void codegen_native_func_def(LLVMBackend *backend, ASTNode *node);

// DataType to LLVM type conversion
LLVMTypeRef datatype_to_llvm(LLVMBackend *backend, DataType type);
InferredType datatype_to_inferred(DataType type);

#endif
