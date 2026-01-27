#ifndef VM_H
#define VM_H

// Note: interpreter.h removed to avoid winsock conflicts
// Forward declare what we need
#include "bytecode.h"

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
typedef enum { OBJ_STRING, OBJ_ARRAY, OBJ_OBJECT, OBJ_FUNCTION } ObjType;

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

// Forward declare JIT types
struct JITCompiledFunc;

// Function object
typedef struct ObjFunction {
  Obj obj;
  int arity;
  int upvalue_count;
  Chunk chunk;
  ObjString *name;
  int call_count; // Profiling counter
  struct JITCompiledFunc
      *jit_code; // JIT compiled native code (NULL if not compiled)
} ObjFunction;

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

// Value creation macros
#define VM_INT(v) ((VMValue){VM_VAL_INT, {.int_val = (v)}})
#define VM_FLOAT(v) ((VMValue){VM_VAL_FLOAT, {.float_val = (v)}})
#define VM_BOOL(v) ((VMValue){VM_VAL_BOOL, {.bool_val = (v)}})
#define VM_VOID() ((VMValue){VM_VAL_VOID, {.int_val = 0}})
#define VM_OBJ(v) ((VMValue){VM_VAL_OBJ, {.obj = (Obj *)(v)}})

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
#define AS_ARRAY(v) ((ObjArray *)AS_OBJ(v))
#define AS_OBJECT(v) ((ObjObject *)AS_OBJ(v))

// Array Object (Requires VMValue)
typedef struct {
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

// ============================================================================
// TRACING JIT (Faz 4) - Must be before VM struct
// ============================================================================

// Loop trace for JIT compilation
#define TRACE_THRESHOLD 100  // Iterations before tracing starts
#define MAX_TRACE_LENGTH 256 // Maximum bytecode ops in a trace
#define MAX_LOOP_TRACES 64

typedef struct {
  uint8_t *loop_start;             // Pointer to OP_LOOP bytecode
  int iteration_count;             // How many times this loop ran
  int trace_recorded;              // 1 if trace was recorded
  uint8_t trace[MAX_TRACE_LENGTH]; // Recorded bytecode trace
  int trace_length;                // Length of recorded trace
  void *native_code;               // Compiled native code (if any)
} LoopTrace;

typedef struct {
  LoopTrace traces[MAX_LOOP_TRACES];
  int count;
} LoopTraceTable;

// Forward declaration for inline caching
// Call site inline cache for function calls (MAJOR OPTIMIZATION!)
struct CallSiteCache {
  ObjFunction *cached_function;       // Cached target function
  uint8_t *cached_entry_point;        // Cached bytecode entry point
  struct JITCompiledFunc *cached_jit; // Cached JIT code (if available)
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

  // Built-in functions (void* to avoid include conflicts)
  void *legacy_interp; // Fallback to legacy interpreter

  // Loop tracing for JIT (Faz 4)
  LoopTraceTable loop_traces;

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

// Create and initialize VM
VM *vm_create();

// Free VM and all resources
void vm_free(VM *vm);

// Run bytecode chunk
// Run bytecode function
VMResult vm_run(VM *vm, ObjFunction *function);

// Stack operations
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

// ============================================================================
// JIT HELPER FUNCTIONS - Called from JIT-compiled code
// ============================================================================

// Execute a function call from JIT code - falls back to interpreter
// Args: vm, callee function, arg_count
// Returns: result pushed to stack
void jit_helper_call(VM *vm, int arg_count);

// Execute interpreter for a single function and return
// Used when JIT encounters OP_CALL
VMValue jit_interpreter_call(VM *vm, ObjFunction *func, VMValue *args,
                             int arg_count);

#endif // VM_H
