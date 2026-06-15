#ifndef VM_H
#define VM_H

// Note: interpreter.h removed to avoid winsock conflicts
// Forward declare what we need
#include "bytecode.hpp"

// ============================================================================
// TULPAR VIRTUAL MACHINE - FAZ 3 OPTİMİZASYON
// Stack-based bytecode interpreter with Arena Allocator
// ============================================================================

#define VM_STACK_MAX 16384
#define VM_FRAMES_MAX 4096
#define VM_GLOBALS_MAX 1024

// ============================================================================
// ARENA ALLOCATOR - Fast bump allocation for VM objects
// ============================================================================

#define ARENA_DEFAULT_SIZE (1024 * 1024) // 1MB default arena
#define ARENA_ALIGNMENT 8

typedef struct ArenaBlock {
  char *base;
  char *current;
  size_t capacity;
  struct ArenaBlock *next; // For overflow blocks
} ArenaBlock;

typedef struct {
  ArenaBlock *current_block;
  ArenaBlock *first_block;
  size_t total_allocated;
} Arena;

// Arena functions (implemented in vm.c)
Arena *arena_create(size_t initial_size);
void arena_destroy(Arena *arena);
void *arena_alloc(Arena *arena, size_t size);
void arena_reset(Arena *arena); // Reset without freeing

// ============================================================================
// DEBUG/RELEASE MODE
// ============================================================================

#ifdef TULPAR_DEBUG
#define VM_BOUNDS_CHECK(cond, msg)                                             \
  do {                                                                         \
    if (!(cond)) {                                                             \
      fprintf(stderr, "VM Error: %s\n", msg);                                  \
      abort();                                                                 \
    }                                                                          \
  } while (0)
#define VM_ASSERT(cond)                                                        \
  do {                                                                         \
    if (!(cond)) {                                                             \
      fprintf(stderr, "VM Assert failed: %s:%d\n", __FILE__, __LINE__);        \
      abort();                                                                 \
    }                                                                          \
  } while (0)
#else
#define VM_BOUNDS_CHECK(cond, msg) ((void)0)
#define VM_ASSERT(cond) ((void)0)
#endif

// ============================================================================
// FAST PATH TYPE CHECKING - Combined type tags for quick dispatch
// ============================================================================

// Combine two types into single value for switch
#define TYPE_PAIR(a, b) (((uint8_t)(a).type << 4) | (uint8_t)(b).type)

// Common type pairs (for fast path in arithmetic)
#define TYPE_INT_INT ((VM_VAL_INT << 4) | VM_VAL_INT)         // 0x00
#define TYPE_INT_FLOAT ((VM_VAL_INT << 4) | VM_VAL_FLOAT)     // 0x01
#define TYPE_FLOAT_INT ((VM_VAL_FLOAT << 4) | VM_VAL_INT)     // 0x10
#define TYPE_FLOAT_FLOAT ((VM_VAL_FLOAT << 4) | VM_VAL_FLOAT) // 0x11

// Branch prediction hints
#ifdef __GNUC__
#define LIKELY(x) __builtin_expect(!!(x), 1)
#define UNLIKELY(x) __builtin_expect(!!(x), 0)
#else
#define LIKELY(x) (x)
#define UNLIKELY(x) (x)
#endif

// ============================================================================
// VM VALUE - Lightweight, stack-allocated values
// ============================================================================

typedef enum {
  VM_VAL_INT,
  VM_VAL_FLOAT,
  VM_VAL_BOOL,
  VM_VAL_VOID,
  VM_VAL_OBJ // Heap-allocated (strings, arrays, objects)
} VMValueType;

// Object types for heap allocation
typedef enum { OBJ_STRING, OBJ_ARRAY, OBJ_OBJECT, OBJ_FUNCTION, OBJ_STRUCT, OBJ_CLOSURE, OBJ_PROMISE } ObjType;

// Base object header - ARC enabled
typedef struct Obj {
  ObjType type;
  struct Obj *next;        // For GC linked list
  uint8_t arena_allocated; // 1 if allocated from arena, 0 if malloc
  int32_t ref_count;       // ARC reference count
  uint8_t is_moved;        // Move semantics: 1 if ownership transferred
} Obj;

// String object
typedef struct {
  Obj obj;
  int length;
  int capacity;
  char *chars;
  uint32_t hash;
} ObjString;

// Function object
typedef struct ObjFunction {
  Obj obj;
  int arity;
  int upvalue_count;
  Chunk chunk;
  ObjString *name;
  int call_count; // Profiling counter
} ObjFunction;

// Closure Object
typedef struct {
  Obj obj;
  void *func_ptr;
  struct ObjArray *env;
  int arity;
} ObjClosure;

// VM Value - 16 bytes, stack allocated
typedef struct {
  VMValueType type;
  union {
    long long int_val;
    double float_val;
    int bool_val;
    Obj *obj;
  } as;
} VMValue;

static inline VMValue vm_make_int(long long v) {
  VMValue value;
  value.type = VM_VAL_INT;
  value.as.int_val = v;
  return value;
}

static inline VMValue vm_make_float(double v) {
  VMValue value;
  value.type = VM_VAL_FLOAT;
  value.as.float_val = v;
  return value;
}

static inline VMValue vm_make_bool(int v) {
  // Zero the whole `as` slot first — `as.bool_val = v` only writes one
  // byte and leaves the remaining 7 bytes uninitialised. After SysV ABI
  // splits VMValue into {i64, i64} for return, those uninitialised bytes
  // become part of the returned `as` field and `is_truthy` (which tests
  // `as != 0`) reports stale garbage as "truthy" even for VM_BOOL(0).
  VMValue value;
  value.type = VM_VAL_BOOL;
  value.as.int_val = 0;
  value.as.bool_val = v ? 1 : 0;
  return value;
}

static inline VMValue vm_make_void() {
  VMValue value;
  value.type = VM_VAL_VOID;
  value.as.int_val = 0;
  return value;
}

static inline VMValue vm_make_obj(void *v) {
  VMValue value;
  value.type = VM_VAL_OBJ;
  value.as.obj = (Obj *)v;
  return value;
}

// Value creation macros
#define VM_INT(v) vm_make_int((v))
#define VM_FLOAT(v) vm_make_float((v))
#define VM_BOOL(v) vm_make_bool((v))
#define VM_VOID() vm_make_void()
#define VM_OBJ(v) vm_make_obj((v))

// Value accessors
#define AS_INT(v) ((v).as.int_val)
#define AS_FLOAT(v) ((v).as.float_val)
#define AS_BOOL(v) ((v).as.bool_val)
#define AS_OBJ(v) ((v).as.obj)
#define AS_STRING(v) ((ObjString *)AS_OBJ(v))

// Type checks
#define IS_INT(v) ((v).type == VM_VAL_INT)
#define IS_FLOAT(v) ((v).type == VM_VAL_FLOAT)
#define IS_BOOL(v) ((v).type == VM_VAL_BOOL)
#define IS_VOID(v) ((v).type == VM_VAL_VOID)
#define IS_OBJ(v) ((v).type == VM_VAL_OBJ)
#define IS_STRING(v) (IS_OBJ(v) && AS_OBJ(v)->type == OBJ_STRING)
#define IS_FUNCTION(v) (IS_OBJ(v) && AS_OBJ(v)->type == OBJ_FUNCTION)
#define IS_NUMBER(v) (IS_INT(v) || IS_FLOAT(v))

#define AS_FUNCTION(v) ((ObjFunction *)AS_OBJ(v))
#define IS_ARRAY(v) (IS_OBJ(v) && AS_OBJ(v)->type == OBJ_ARRAY)
#define IS_OBJECT(v) (IS_OBJ(v) && AS_OBJ(v)->type == OBJ_OBJECT)
#define IS_CLOSURE(v) (IS_OBJ(v) && AS_OBJ(v)->type == OBJ_CLOSURE)
#define AS_CLOSURE(v) ((ObjClosure *)AS_OBJ(v))
#define IS_PROMISE(v) (IS_OBJ(v) && AS_OBJ(v)->type == OBJ_PROMISE)
#define AS_PROMISE(v) ((ObjPromise *)AS_OBJ(v))
#define AS_ARRAY(v) ((ObjArray *)AS_OBJ(v))
#define AS_OBJECT(v) ((ObjObject *)AS_OBJ(v))

// Array Object (Requires VMValue)
typedef struct ObjArray {
  Obj obj;
  int count;
  int capacity;
  VMValue *items;
} ObjArray;

// Object (Map/Dictionary) Object (Requires VMValue)
typedef struct {
  Obj obj;
  int count;
  int capacity;
  ObjString **keys;
  VMValue *values;
} ObjObject;

// Promise Object — the value an async function produces and `await` consumes.
// Settled by the event-loop scheduler in runtime/tulpar_async.cpp. `value`
// carries the fulfilment value (state==1) or the rejection payload (state==2).
// `waiters` is an engine-private `Task**` vector of coroutines blocked in
// `await` on this promise; settling the promise moves them to the ready queue.
// Promises are created with arena_allocated=1 so ARC never reclaims them while
// the scheduler still holds raw pointers; the engine owns their lifetime.
typedef struct ObjPromise {
  Obj obj;
  int state; // 0 = pending, 1 = fulfilled, 2 = rejected
  VMValue value;
  void *waiters; // Task** (engine-private)
  int nwaiters;
  int cap_waiters;
} ObjPromise;

// Heap-allocated struct (Plan 04 v2 — heap promotion).
//
// Layout matches the stack-alloca path used by typed-struct VAR_DECLs:
// each field is a 64-bit slot, GEP-addressable by index. The compile-time
// struct registry (StructTypeEntry in llvm_backend.cpp) supplies the
// field names; ObjStruct only holds the `type_name` pointer (interned in
// the AOT module) so runtime print/diagnostics can recover the type.
//
// Phase 1 only handles trivially-unboxable structs (every field is int
// or bool — i.e. fits in i64). String/array/nested-struct fields require
// GC descent and are deferred to phase 2; the codegen path bails to the
// boxed json fallback when the struct can't take the typed path.
//
// `fields` is a flexible array member: total allocation is
// `sizeof(ObjStruct) + field_count * sizeof(int64_t)`.
typedef struct {
  Obj obj;
  const char *type_name;   // interned by the AOT module; lifetime ≥ struct
  int field_count;         // mirror of StructTypeEntry::field_count
  int64_t fields[1];       // flexible array — index 0..field_count-1 valid
} ObjStruct;

#define IS_STRUCT(v) (IS_OBJ(v) && AS_OBJ(v)->type == OBJ_STRUCT)
#define AS_STRUCT(v) ((ObjStruct *)AS_OBJ(v))

// ============================================================================
// CALL FRAME
// ============================================================================

#define VM_MAX_HANDLERS 16
typedef struct {
  // Try block start/end ip not needed if we push offset or use ranges
  // For stack unwinding we need stack depth
  int stack_depth;
  uint8_t *catch_ip;
  VMValue *stack_top; // To restore exact stack pointer
} VMExceptionHandler;

// Minimal call frame - optimized for speed
typedef struct {
  ObjFunction *function; // Function reference (for debugging/GC)
  uint8_t *ip;           // Instruction pointer / return address
  VMValue *slots;        // Local variables base pointer
  // Minimal exception handling (moved from 16 handlers to 4)
  uint8_t *catch_ips[4]; // Up to 4 nested try blocks
  VMValue *catch_stacks[4];
  int handler_count;
} CallFrame; // ~80 bytes - down from 400+ bytes!

// Exception handler stack - separate from hot call path
typedef struct {
  int frame_index; // Which frame this handler belongs to
  int stack_depth;
  uint8_t *catch_ip;
  VMValue *stack_top;
} VMExceptionEntry;

#define VM_MAX_EXCEPTIONS 64

// Forward declaration for inline caching
// Call site inline cache for function calls (MAJOR OPTIMIZATION!)
struct CallSiteCache {
  ObjFunction *cached_function;       // Cached target function
  uint8_t *cached_entry_point;        // Cached bytecode entry point
  uint32_t cache_hit_count;           // Statistics: cache hits
  uint32_t cache_miss_count;          // Statistics: cache misses
};
typedef struct CallSiteCache CallSiteCache;

// ============================================================================
// VIRTUAL MACHINE
// ============================================================================

typedef struct VM {
  // Call stack - minimal frames for speed
  CallFrame frames[VM_FRAMES_MAX];
  int frame_count;

  // Per-frame "where did this module's source live?" — populated when
  // OP_IMPORT pushes a new frame for a freshly-loaded `.tpr` and probed
  // by nested OP_IMPORTs to resolve bundle-local sibling imports
  // (e.g. tulpar_modules/multipkg/multipkg.tpr doing `import "greetings"`
  // finds tulpar_modules/multipkg/greetings.tpr before the cwd-rooted
  // candidates would either miss it or grab a wrong unrelated package).
  // Empty string for the top-level user script (lives in cwd) and for
  // the embedded-libs path. Mirrors `current_import_dir` tracking in
  // src/aot/llvm_backend.cpp's AST_IMPORT handler.
  char import_dirs[VM_FRAMES_MAX][256];

  // Value stack
  VMValue stack[VM_STACK_MAX];
  VMValue *stack_top;

  // Accumulator register - holds return value, reduces stack ops
  VMValue acc;

  // Exception handler stack - separate from hot call path
  VMExceptionEntry exceptions[VM_MAX_EXCEPTIONS];
  int exception_count;

  // Global variables (hash table)
  struct {
    ObjString *key;
    VMValue value;
  } globals[VM_GLOBALS_MAX];
  int global_count;

  // Global variable cache (for inline caching)
  int global_cache[VM_GLOBALS_MAX]; // Maps cache_slot -> global_index
  int global_cache_count;

  // Object allocation tracking (for GC)
  Obj *objects;
  size_t bytes_allocated;
  size_t next_gc;

  // Arena allocator for fast object allocation
  Arena *arena;

  // String interning table
  ObjString **strings;
  int string_count;
  int string_capacity;

  // OPTIMIZATION: Inline caching for function calls
  CallSiteCache call_caches[256]; // Cache for OP_CALL_1_CACHED, etc.
  int call_cache_count;

} VM;

// ============================================================================
// VM RESULT
// ============================================================================

typedef enum { VM_OK, VM_COMPILE_ERROR, VM_RUNTIME_ERROR } VMResult;

// ============================================================================
// VM FUNCTIONS
// ============================================================================

#ifdef __cplusplus
extern "C" {
#endif

// Create and initialize VM
VM *vm_create();

// Free VM and all resources
void vm_free(VM *vm);

// vm_run (bytecode interpreter) removed 2026-06-15 — Tulpar is AOT-only.

// Object allocation
ObjString *vm_alloc_string(VM *vm, const char *chars, int length);
ObjString *vm_take_string(VM *vm, char *chars, int length);
ObjString *vm_copy_string(VM *vm, const char *chars, int length);
ObjString *vm_alloc_string_buffer(VM *vm, int length, int capacity); // New

// Global variables
void vm_define_global(VM *vm, ObjString *name, VMValue value);
VMValue *vm_get_global(VM *vm, ObjString *name);

// Print value (for debugging)
void vm_print_value(VMValue value);

// Runtime error
void vm_runtime_error(VM *vm, const char *format, ...);

// New function creation
ObjFunction *vm_new_function(VM *vm);

// Array functions
ObjArray *vm_allocate_array(VM *vm);
void vm_array_push(VM *vm, ObjArray *array, VMValue value);

// Object functions
ObjObject *vm_allocate_object(VM *vm);

#ifdef __cplusplus
}
#endif

// Stack operations
static inline void vm_push(VM *vm, VMValue value) {
  *vm->stack_top = value;
  vm->stack_top++;
}

static inline VMValue vm_pop(VM *vm) {
  vm->stack_top--;
  return *vm->stack_top;
}

static inline VMValue vm_peek(VM *vm, int distance) {
  return vm->stack_top[-1 - distance];
}

// ============================================================================
// INLINE CACHING (Faz 3)
// ============================================================================

// Inline Cache for property/global access
typedef struct {
  ObjString *cached_key; // Cached property/variable name
  int cached_offset;     // Cached slot offset for fast lookup
  int cache_hits;        // Statistics
  int cache_misses;      // Statistics
} InlineCache;

// Type Feedback for speculative optimization
#define TYPE_FEEDBACK_SAMPLES 4
typedef struct {
  uint8_t observed_types[TYPE_FEEDBACK_SAMPLES]; // Last N observed types
  int sample_count;                              // Number of samples collected
  uint8_t dominant_type;                         // Most common type
} TypeFeedback;

// Maximum inline caches per chunk
#define MAX_INLINE_CACHES 64

#endif // VM_H
