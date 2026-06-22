// Tulpar LLVM Backend - Core Interface

#ifndef TULPAR_LLVM_BACKEND_H
#define TULPAR_LLVM_BACKEND_H

#include "../parser/parser.hpp"
#include <llvm-c/Core.h>
#include <llvm-c/DebugInfo.h>
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
  // When this local is a user-defined struct (`struct Point p;`) lowered to a
  // native LLVM struct type, this points at the registered StructTypeEntry's
  // name so field access can resolve fields against the layout. NULL for
  // regular VMValue locals (the historical default path).
  char *struct_type_name;
  int is_captured;
  int slot_index;
  LLVMValueRef env_ptr;
  struct ASTNode_C *declaring_function;
} LocalVar;

typedef struct FuncStackNode {
  struct ASTNode_C *node;
  struct FuncStackNode *parent;
} FuncStackNode;

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
  // Per-parameter struct type names (size matches param_count). Non-NULL slot
  // means the matching parameter is declared as a user struct (e.g.
  // `Point p`); the call-site codegen passes a struct pointer through that
  // slot instead of boxing the rhs into a VMValue. NULL slots (the common
  // case) keep the existing VMValue-pointer ABI.
  char **param_struct_names;
  // Non-NULL when the function returns a user struct (`func make(): Point`).
  // Caller allocates a typed-struct alloca and passes its pointer as
  // the result_ptr; callee writes into it via memcpy on return.
  char *return_struct_name;
  // 1 when declared `async`: a call site spawns it as a coroutine and yields
  // a promise instead of calling it directly. See codegen FUNCTION_CALL.
  int is_async;
} FunctionEntry;

// User-declared struct (`struct Point { int x; int y; }`) tracked by the
// AOT backend so field accesses can lower to a single getelementptr + load
// (typed path) instead of the generic vm_get_element runtime call (boxed
// path). The boxed path stays as a fallback when the local isn't tagged
// with a struct_type_name, so adding entries here is opt-in: only the new
// VAR_DECL TYPE_CUSTOM branch produces a typed-struct local.
typedef struct {
  char *name;            // e.g. "Point"
  LLVMTypeRef llvm_type; // %struct.Point = type { i64, i64 }
  char **field_names;    // declaration-ordered (matches LLVM struct order)
  DataType *field_types;
  int field_count;
} StructTypeEntry;

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
  LLVMTypeRef vm_value_type;   // struct VMValue {i32, [4xi8], i64}
  LLVMTypeRef ret_pair_type;   // {i64, i64} - ABI-safe return type for VMValue
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
  struct ASTNode_C *current_function_node;
  LLVMValueRef current_env_ptr;
  LLVMValueRef current_parent_env;
  Scope *current_scope;
  FuncStackNode *func_stack;

  FunctionEntry functions[128];
  int function_count;

  StructTypeEntry struct_types[64];
  int struct_type_count;

  // Import tracking
  char *imported_files[128];
  int imported_count;

  // Plan 02 PR3 multi-file paket: when processing an import statement
  // from inside a multi-file bundled package, sibling imports (`import
  // "util"` from `tulpar_modules/foo/main.tpr`) need to find the
  // bundle's own directory before falling back to the cwd-rooted
  // probes. Holds the path of the directory currently being recursed
  // into (e.g. `tulpar_modules/foo`); empty string when the import
  // came from cwd. Saved/restored across nested imports.
  char current_import_dir[256];

  // AOT Builtin Functions
  LLVMValueRef func_aot_to_string;
  LLVMValueRef func_aot_to_int;
  LLVMValueRef func_aot_to_float;
  LLVMValueRef func_aot_len;
  LLVMValueRef func_aot_array_push;
  LLVMValueRef func_aot_array_pop;
  // Plan 04 v2 heap promotion — typed structs lifted onto the heap so
  // they can survive past their stack scope (push, return-as-array,
  // store into json object). Field codegen on a struct VMValue uses
  // the get/set helpers; field index is resolved at compile time from
  // the struct registry, so the helper is just an indexed slot access.
  LLVMValueRef func_aot_struct_alloc;
  LLVMValueRef func_aot_struct_alloc_from_fields;
  LLVMValueRef func_aot_struct_get_field;
  LLVMValueRef func_aot_struct_set_field;
  LLVMValueRef func_aot_struct_type_is; // (VMValue* v, ptr name) -> i64
  LLVMValueRef func_aot_struct_unpack_to;
  LLVMValueRef func_aot_to_json;
  LLVMValueRef func_aot_runtime_init;
  LLVMValueRef func_aot_input;
  LLVMValueRef func_aot_env;
  LLVMValueRef func_aot_arena_save;
  LLVMValueRef func_aot_arena_restore;
  LLVMValueRef func_aot_arena_drop;
  LLVMValueRef func_aot_now_iso8601;
  LLVMValueRef func_aot_format_iso8601;
  LLVMValueRef func_aot_parse_iso8601;
  LLVMValueRef func_aot_regex_match;
  LLVMValueRef func_aot_regex_search;
  LLVMValueRef func_aot_regex_capture;
  LLVMValueRef func_aot_regex_replace;
  LLVMValueRef func_aot_weekday;
  LLVMValueRef func_aot_date_add_seconds;
  LLVMValueRef func_aot_file_glob;
  LLVMValueRef func_aot_csv_parse;
  LLVMValueRef func_aot_csv_emit;
  LLVMValueRef func_aot_keys;
  LLVMValueRef func_aot_object_clone;
  LLVMValueRef func_aot_persist; // persist(value) -> deep malloc'd copy (survives arena_restore)
  LLVMValueRef func_aot_http_request;
  LLVMValueRef func_aot_http_request_async; // (method,url,body) -> promise
  LLVMValueRef func_aot_trim;
  LLVMValueRef func_aot_replace;
  LLVMValueRef func_aot_split;
  LLVMValueRef func_aot_array_slice; // (VMValue* arr, i64 start) -> VMValue

  LLVMValueRef func_aot_read_file;
  LLVMValueRef func_aot_write_file;
  LLVMValueRef func_aot_append_file;
  LLVMValueRef func_aot_file_exists;
  LLVMValueRef func_aot_sha256;

  // Exception Handling
  LLVMValueRef func_aot_try_push;
  LLVMValueRef func_aot_try_pop;
  LLVMValueRef func_aot_throw;
  LLVMValueRef func_aot_get_exception;
  LLVMValueRef func_setjmp;
  // llvm.frameaddress.p0 — Windows only, feeds _setjmpex's 2nd arg.
  LLVMValueRef func_frameaddress;

  // Time functions
  LLVMValueRef func_aot_clock_ms;

  // Socket Functions
  LLVMValueRef func_aot_socket_server;
  LLVMValueRef func_aot_socket_client;
  LLVMValueRef func_aot_socket_accept;
  LLVMValueRef func_aot_socket_send;
  LLVMValueRef func_aot_socket_receive;
  LLVMValueRef func_aot_socket_close;
  LLVMValueRef func_aot_socket_set_nonblocking;
  LLVMValueRef func_aot_socket_poll;

  // TLS server primitives — power lib/wings_tls.tpr
  LLVMValueRef func_aot_tls_init;
  LLVMValueRef func_aot_tls_accept;
  LLVMValueRef func_aot_tls_recv;
  LLVMValueRef func_aot_tls_send;
  LLVMValueRef func_aot_tls_close;
  LLVMValueRef func_aot_tls_ctx_free;

  // Dynamic Call (Wings support)
  LLVMValueRef func_aot_call_dynamic;
  LLVMValueRef func_aot_call_dynamic_1; // call(name, arg) — 1-arg dynamic dispatch
  LLVMValueRef func_aot_create_closure;
  LLVMValueRef func_aot_call_closure;

  // Async runtime (runtime/tulpar_async.cpp)
  LLVMValueRef func_aot_async_spawn;   // (ptr fn, ptr args, i32 argc) -> ptr
  LLVMValueRef func_aot_await;         // (VMValue) -> VMValue
  LLVMValueRef func_aot_sleep_async;   // (i64 ms) -> ptr
  LLVMValueRef func_aot_gather;        // (ptr args, i32 argc) -> ptr
  LLVMValueRef func_aot_event_loop_run;// () -> void

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
  LLVMValueRef func_aot_http_create_response_full;
  LLVMValueRef func_aot_http_create_response_keepalive;
  LLVMValueRef func_aot_http_should_keepalive;
  LLVMValueRef func_aot_http_recv_request;
  LLVMValueRef func_aot_http_status_text;
  LLVMValueRef func_aot_path_match;
  LLVMValueRef func_aot_parse_query;
  LLVMValueRef func_aot_parse_cookies;
  LLVMValueRef func_aot_parse_multipart;
  // Wings-level fused response builder — folds the envelope check +
  // toJson + http_create_response_keepalive sequence into one native
  // call (one Tulpar function dispatch instead of three).
  LLVMValueRef func_aot_wings_build_response;
  // Native route lookup (exact + pattern) — replaces the per-request
  // Tulpar `_find_route_with_params` loop with a single C call.
  LLVMValueRef func_aot_wings_find_route;
  // Permanent-storage copy of a string (escapes the per-request arena).
  // Wings response cache uses this to pin cached wire bytes past the
  // serve loop's `arena_restore`.
  LLVMValueRef func_aot_string_pin;
  // Logical CPU count (sysconf / GetSystemInfo). Wings uses this to
  // default `listen_pool`'s worker count to a host-matched value.
  LLVMValueRef func_aot_cpu_count;

  // Crypto / encoding utilities — sha1 + base64. Self-contained C++
  // implementations (no OpenSSL needed); used by the WebSocket upgrade
  // handshake (`wings_ws_accept_key`) but also exposed directly for
  // signed cookies, JWT HMAC, ETag generation, etc.
  LLVMValueRef func_aot_sha1;
  LLVMValueRef func_aot_sha1_hex;
  LLVMValueRef func_aot_base64_encode;
  LLVMValueRef func_aot_base64_decode;
  // base64(sha1(client_key + RFC 6455 GUID)) — the only piece of the
  // WebSocket handshake that benefits from a native; rest is pure
  // string work that's fine in Tulpar source.
  LLVMValueRef func_aot_wings_ws_accept_key;
  // WebSocket frame I/O (RFC 6455 §5.2). Send is 3-arg
  // (fd, opcode, payload) → int; recv is 1-arg (fd) → object
  // {ok, opcode, fin, payload} / {ok=0, error}. Closes the upgrade
  // loop so a wings keep-alive socket can pump frames entirely in
  // Tulpar source without raw-byte bit-twiddling.
  LLVMValueRef func_aot_wings_ws_send_frame;
  LLVMValueRef func_aot_wings_ws_recv_frame;
  // Per-thread current-request fd. Wings dispatchers set this on
  // every request; streaming handlers read it back to write to the
  // socket directly and signal `{"_stream": 1}` to skip the
  // dispatcher's response build. Used as the SSE / WS-upgrade
  // sidechannel that doesn't go through `_request[k] = …` (cookies
  // miscompile turf).
  LLVMValueRef func_aot_wings_set_current_fd;
  LLVMValueRef func_aot_wings_current_fd;

  // Process control
  LLVMValueRef func_aot_exit;

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
  LLVMValueRef func_aot_math_mod;
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
  int use_static_typing; // 1 = use native types, 0 = use VMValue

  // ABI tracking
  int current_function_is_void_abi;
  // Set to the registered struct name (managed pointer into struct_types[])
  // while emitting a function whose declared return type is a user struct.
  // Tells AST_RETURN to memcpy the source struct into the result pointer
  // (param 0) instead of building a VMValue store. NULL during regular
  // VMValue-returning functions.
  const char *current_function_returns_struct;
  // Hint slot for VAR_DECL `Point p = make_point();`: VAR_DECL allocates the
  // typed-struct local for `p` first, then sets this slot before calling
  // codegen_expression on the rhs. AST_FUNCTION_CALL, when the callee
  // returns a matching struct, reuses the supplied alloca as the call's
  // result_ptr — that avoids an extra alloca + copy and lets the function
  // write its struct return straight into `p`. Cleared back to NULL after
  // the rhs codegen returns so it doesn't leak across statements.
  LLVMValueRef pending_struct_result_ptr;
  const char *pending_struct_result_name;

  // Loop control-flow stack: tracks the {continue-target, break-target}
  // basic blocks of every `while`/`for` we're currently inside, so AST_BREAK
  // and AST_CONTINUE can branch to the right block. Capped at 32 (deeper
  // loop nesting is exotic; if we ever hit it we'd error out).
  struct LoopContext {
    LLVMBasicBlockRef continue_block;  // where `continue;` jumps to
    LLVMBasicBlockRef break_block;     // where `break;` jumps to
  } loop_stack[32];
  int loop_depth;
  int lambda_count;
  int match_count;

  // Quiet mode - suppress [AOT] messages during compilation
  int quiet;

  // Codegen error flag — set by any site that prints an "HATA" diagnostic
  // (undefined identifier, undefined function, etc). aot_compile reads this
  // after llvm_backend_compile() and turns it into AOT_ERROR_CODEGEN so a
  // broken IR never silently produces a working-looking exe.
  int had_error;

  // Plan 07 PR 1 scaffold: `tulpar build --debug` sets this flag. Today
  // it forwards `-g` to clang at link time and pins the optimiser at
  // -O0 in the pipeline so subsequent debugger sessions don't see
  // inlined frames. PR 2 (this commit) lights up the LLVMDIBuilder
  // path below — compile unit + file metadata land in the IR when the
  // flag is on. Per-function subprogram metadata and per-statement
  // debug locations follow in PR 3.
  int emit_debug_info;

  // Plan 07 PR 2: LLVMDIBuilder bundle. NULL when `emit_debug_info`
  // is off (the no-op default — same path as before this PR). When
  // on, `llvm_backend_init_debug_info` populates all three slots, and
  // `llvm_backend_finalize_debug_info` must be called before the
  // first IR-consumer (`emit_ir_file` / `emit_object`) so the
  // metadata graph is closed and verifier-clean.
  LLVMDIBuilderRef di_builder;
  LLVMMetadataRef di_file;
  LLVMMetadataRef di_compile_unit;
  // Plan 07 PR 3e: cached `DIBasicType` for the int64 primitive.
  // Created once in `init_debug_info` so every local-int declaration
  // can reuse it (LLVM dedups identical types itself, but the cache
  // avoids re-walking the API per declaration). NULL when --debug
  // is off.
  LLVMMetadataRef di_int_type;
  // Plan 07 PR 3f: cached opaque `DIBasicType` for the 16-byte
  // `VMValue` carrier. Boxed locals (`json`, `str`, `array`, plus any
  // typed `VMValue` parameter) get this type for `info locals` /
  // `print <name>` to surface the variable existence and storage
  // address. Decoding the tag + payload back to a Tulpar-level type
  // is the gdb pretty-printer's job — out of scope for this PR.
  LLVMMetadataRef di_vmvalue_type;

  // Original source text (NUL-terminated). When non-null, codegen errors
  // include a source-line excerpt + caret next to the diagnostic message,
  // Rust-style. Owned by aot_compile, lives for the duration of codegen.
  const char *source_text;
  // Optional source filename used in the diagnostic prefix
  // ("HATA (lib/router.tpr:245): ..."). May be null for stdin/embedded input.
  const char *source_filename;
  void *capture_data;
} LLVMBackend;

LLVMBackend *llvm_backend_create(const char *module_name);
void llvm_backend_destroy(LLVMBackend *backend);
void llvm_backend_compile(LLVMBackend *backend, ASTNode_C *node);
void llvm_backend_optimize(LLVMBackend *backend);
int llvm_backend_emit_object(LLVMBackend *backend, const char *filename);
int llvm_backend_emit_ir_file(LLVMBackend *backend, const char *filename);

// Plan 07 PR 2: open / close the LLVMDIBuilder bundle. Call init once
// after the backend is created but before codegen runs, then finalize
// once after `llvm_backend_compile` returns and before the first IR
// consumer (`emit_object` / `emit_ir_file`). Both are no-ops when
// `backend->emit_debug_info == 0`. `source_filename` is optional;
// when NULL, the compile unit uses "<stdin>" as the source path so
// downstream debuggers still see *something*.
void llvm_backend_init_debug_info(LLVMBackend *backend,
                                  const char *source_filename);
void llvm_backend_finalize_debug_info(LLVMBackend *backend);

// Plan 07 PR 3b: attach a `DISubprogram` to a user function and
// position the builder on its declaration line. Call this right
// after the entry basic block exists and before codegen walks the
// body — the returned subprogram is also stashed as
// `LLVMGetSubprogram(func)` so subsequent debug locations can scope
// against it. No-op + returns NULL when emit_debug_info is off.
// `decl_line` should be the parser-recorded line of the function
// declaration; 0 is accepted (debugger will report "no source line").
LLVMMetadataRef llvm_backend_emit_subprogram_for_function(
    LLVMBackend *backend, LLVMValueRef func, const char *name, int decl_line);

// Plan 07 PR 3e: emit `DILocalVariable` + `llvm.dbg.declare` for an
// int64 local. Hook this in right after a local `alloca` for a
// declared int variable so gdb's `info locals` / `print x` can read
// it back. No-op when --debug is off, when no DISubprogram is bound
// to the current function, or when alloca/name is null. `line`
// should be the parser-recorded declaration line.
void llvm_backend_emit_local_int_declare(LLVMBackend *backend,
                                         const char *name,
                                         LLVMValueRef alloca, int line);

// Plan 07 PR 3f: same shape as the int variant, but for a 16-byte
// boxed `VMValue` local (the `add_local(...)` path: json / str /
// array / generic VMValue declarations and function parameters).
// Surfaces variable existence and address to the debugger; a future
// gdb pretty-printer can decode the tag + payload into a Tulpar-
// level value display.
void llvm_backend_emit_local_vmvalue_declare(LLVMBackend *backend,
                                             const char *name,
                                             LLVMValueRef alloca, int line);

// Plan 07 PR 3g: attach a `DIGlobalVariableExpression` to a
// top-level global. Use `is_vmvalue=0` for native int globals
// (i64), `is_vmvalue=1` for boxed VMValue globals. Hook this in
// right after `LLVMAddGlobal` in the Pass 0.1 / module-import
// pre-scan. No-op when --debug is off.
void llvm_backend_emit_global_declare(LLVMBackend *backend, const char *name,
                                      LLVMValueRef global_var, int line,
                                      int is_vmvalue);

// Enable static typing mode for native performance
void llvm_backend_enable_static_typing(LLVMBackend *backend);

// Helper functions for testing
void enter_scope(LLVMBackend *backend);
void exit_scope(LLVMBackend *backend);
void add_local(LLVMBackend *backend, const char *name, LLVMValueRef val);

// Build an alloca pinned to the function's entry block. Use this any time
// the alloca site is inside a loop body — block-local LLVMBuildAlloca grows
// the stack frame on every iteration and overflows in long-running loops.
LLVMValueRef llvm_build_alloca_at_entry(LLVMBackend *backend, LLVMTypeRef type,
                                        const char *name);
void add_local_typed(LLVMBackend *backend, const char *name, LLVMValueRef val,
                     InferredType type, LLVMValueRef native_val);
LLVMValueRef get_local(LLVMBackend *backend, const char *name);
LocalVar *get_local_var(LLVMBackend *backend, const char *name);
void add_local_captured(LLVMBackend *backend, const char *name, LLVMValueRef env_ptr, int slot_index, struct ASTNode_C *decl_func);
InferredType get_local_type(LLVMBackend *backend, const char *name);
LLVMValueRef codegen_expression(LLVMBackend *backend, ASTNode_C *node);
LLVMValueRef codegen_statement(LLVMBackend *backend, ASTNode_C *node);

// Native type codegen (static typing)
LLVMValueRef codegen_native_expr(LLVMBackend *backend, ASTNode_C *node,
                                 DataType expected_type);
void codegen_native_func_def(LLVMBackend *backend, ASTNode_C *node);

// DataType to LLVM type conversion
LLVMTypeRef datatype_to_llvm(LLVMBackend *backend, DataType type);
InferredType datatype_to_inferred(DataType type);

// User-defined struct tracking for the typed (unboxed) struct lower path.
// `register_struct_type` builds the LLVM struct layout from a TypeDecl AST
// node and adds it to backend->struct_types. Returns the entry pointer
// (or NULL if the table is full / inputs are invalid).
struct ASTNode_C;
StructTypeEntry *register_struct_type(LLVMBackend *backend, struct ASTNode_C *type_decl);
StructTypeEntry *find_struct_type(LLVMBackend *backend, const char *name);
// Returns the field index in declaration order, or -1 if the field isn't
// part of the struct.
int struct_type_field_index(StructTypeEntry *st, const char *field_name);
// `add_local_struct` adds a typed-struct local: `value` is an alloca to the
// LLVM struct type, `struct_name` is the registered StructTypeEntry name.
void add_local_struct(LLVMBackend *backend, const char *name, LLVMValueRef alloca_ptr,
                      const char *struct_name);
// Returns the struct_type_name of the local (or NULL if it's a regular
// VMValue local). Walks the scope chain like get_local.
const char *get_local_struct_type(LLVMBackend *backend, const char *name);
// Predicate: a struct is "trivially unboxable" when every field is a plain
// int or bool. Strings/arrays/nested structs hit the fallback boxed path
// in this PR; later PRs will widen the predicate.
int struct_is_trivially_unboxable(StructTypeEntry *st);

#endif
