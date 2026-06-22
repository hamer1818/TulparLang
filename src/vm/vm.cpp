#include "../common/platform.h"
#include "../common/platform_threads.h"
#ifndef TULPAR_WASM_BUILD
#include "../common/platform_sockets.h"
#include <errno.h>
#include <ctime>

#if !PLATFORM_WINDOWS
#include <sys/select.h>
#endif
#endif // TULPAR_WASM_BUILD
#ifndef TULPAR_WASM_BUILD
#include "../../lib/sqlite3/sqlite3.h"
#endif
#include "../embedded_libs.h"
#include "../parser/parser.hpp"
#include "../parser/import_alias.hpp"
#include "../common/localization.hpp"
#include "vm.hpp"
#include "../pkg/sha256.hpp"
#include <cctype>
#include <cmath> // For clock()
#include <stdarg.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>

// ============================================================================
// HELPERS
// ============================================================================

// External function from runtime_bindings.cpp (C ABI)
extern "C" void print_vm_value(VMValue value);

// Shared AOT runtime entry points. Defined in runtime_bindings.cpp inside
// its `extern "C" {` block — match that linkage here so the VM opcodes
// below resolve to the same implementations `tulpar build` uses.
extern "C" VMValue aot_from_json(VMValue jsonStr);
extern "C" VMValue aot_to_json(VMValue value);
extern "C" VMValue aot_keys(VMValue objVal);
extern "C" VMValue aot_array_pop(VMValue arr_val);
extern "C" VMValue aot_parse_cookies(VMValue strVal);
extern "C" VMValue aot_parse_query(VMValue strVal);
extern "C" VMValue aot_http_parse_request(VMValue rawRequest);
extern "C" VMValue aot_socket_peer_ip(VMValue fdVal);
extern "C" VMValue aot_http_status_text(VMValue statusVal);
extern "C" VMValue aot_http_should_keepalive(VMValue requestVal);
extern "C" VMValue aot_path_match(VMValue patternVal, VMValue pathVal);
// aot_math_abs's pointer-out signature stands out from the rest — wrap
// it inline in case 15 (and case 105 for round) so callers see the
// regular VMValue → VMValue shape every other math builtin uses.
extern "C" void aot_math_abs(VMValue *result, VMValue *v_ptr);
extern "C" VMValue aot_math_sqrt(VMValue v);
extern "C" VMValue aot_math_sin(VMValue v);
extern "C" VMValue aot_math_cos(VMValue v);
extern "C" VMValue aot_math_tan(VMValue v);
extern "C" VMValue aot_math_floor(VMValue v);
extern "C" VMValue aot_math_ceil(VMValue v);
extern "C" VMValue aot_math_round(VMValue v);
extern "C" VMValue aot_math_log(VMValue v);
extern "C" VMValue aot_math_log10(VMValue v);
extern "C" VMValue aot_math_exp(VMValue v);
extern "C" VMValue aot_math_min(VMValue a, VMValue b);
extern "C" VMValue aot_math_max(VMValue a, VMValue b);
extern "C" VMValue aot_math_mod(VMValue a, VMValue b);
extern "C" VMValue aot_math_random(void);
extern "C" VMValue aot_arena_save(void);
extern "C" VMValue aot_arena_restore(VMValue idxVal);
extern "C" VMValue aot_string_pin(VMValue strVal);
extern "C" VMValue aot_cpu_count(void);
extern "C" VMValue aot_timestamp(void);
extern "C" VMValue aot_time_ms(void);
extern "C" VMValue aot_input_int(VMValue promptVal);
extern "C" VMValue aot_input_float(VMValue promptVal);
extern "C" VMValue aot_http_create_response(VMValue statusVal,
                                            VMValue contentTypeVal,
                                            VMValue bodyVal);
extern "C" VMValue aot_http_create_response_full(VMValue statusVal,
                                                 VMValue contentTypeVal,
                                                 VMValue bodyVal,
                                                 VMValue headersVal);
extern "C" VMValue aot_http_create_response_keepalive(VMValue statusVal,
                                                      VMValue contentTypeVal,
                                                      VMValue bodyVal,
                                                      VMValue headersVal,
                                                      VMValue keepaliveVal);
extern "C" VMValue aot_csv_parse(VMValue strVal);
extern "C" VMValue aot_csv_emit(VMValue rowsVal);
extern "C" VMValue aot_date_add_seconds(VMValue baseVal, VMValue deltaVal);
extern "C" VMValue aot_db_execute(VMValue dbVal, VMValue sqlVal);
extern "C" VMValue aot_db_error(VMValue dbVal);
extern "C" VMValue aot_db_last_insert_id(VMValue dbVal);
extern "C" VMValue aot_object_clone(VMValue val);
extern "C" VMValue aot_math_pow(VMValue base, VMValue exp);
extern "C" VMValue aot_format_iso8601(VMValue secsVal);
extern "C" VMValue aot_parse_iso8601(VMValue strVal);
extern "C" VMValue aot_weekday(VMValue secsVal);
extern "C" VMValue aot_string_repeat(VMValue str, VMValue countVal);
extern "C" VMValue aot_now_iso8601(void);
extern "C" VMValue aot_regex_match(VMValue patVal, VMValue strVal);
extern "C" VMValue aot_regex_search(VMValue patVal, VMValue strVal);
extern "C" VMValue aot_regex_capture(VMValue patVal, VMValue strVal);
extern "C" VMValue aot_regex_replace(VMValue patVal, VMValue strVal,
                                     VMValue replVal);

static void runtime_error(VM *vm, const char *format, ...) {
  va_list args;
  va_start(args, format);
  vfprintf(stderr, tulpar::i18n::tr_for_en(format), args);
  va_end(args);
  fputs("\n", stderr);

  for (int i = vm->frame_count - 1; i >= 0; i--) {
    CallFrame *frame = &vm->frames[i];
    ObjFunction *function = frame->function;
    // `ip` points one past the byte being executed (dispatcher pre-advances
    // before running the handler), so the offset of the faulting byte is
    // `ip - code - 1`. Clamp into range in case the trap is at offset 0 of
    // an empty/synthetic chunk.
    size_t instruction = (size_t)(frame->ip - function->chunk.code);
    if (instruction > 0)
      instruction--;
    int line = chunk_line_at(&function->chunk, instruction);

    fprintf(stderr, "[line %d] in ", line);
    if (function->name == nullptr) {
      fprintf(stderr, "script\n");
    } else {
      fprintf(stderr, "%s()\n", function->name->chars);
    }
  }

  // Scan stack removed logic (reset stack)
  vm->stack_top = vm->stack;
  vm->frame_count = 0;
}

// ============================================================================
// ARENA ALLOCATOR IMPLEMENTATION
// ============================================================================

static ArenaBlock *arena_block_create(size_t size) {
  ArenaBlock *block = static_cast<ArenaBlock*>(malloc(sizeof(ArenaBlock)));
  if (!block)
    return nullptr;

  block->base = static_cast<char*>(malloc(size));
  if (!block->base) {
    free(block);
    return nullptr;
  }

  block->current = block->base;
  block->capacity = size;
  block->next = nullptr;
  return block;
}

static void arena_block_destroy(ArenaBlock *block) {
  if (block) {
    free(block->base);
    free(block);
  }
}

Arena *arena_create(size_t initial_size) {
  Arena *arena = static_cast<Arena*>(malloc(sizeof(Arena)));
  if (!arena)
    return nullptr;

  arena->first_block = arena_block_create(initial_size);
  if (!arena->first_block) {
    free(arena);
    return nullptr;
  }

  arena->current_block = arena->first_block;
  arena->total_allocated = 0;
  return arena;
}

void arena_destroy(Arena *arena) {
  if (!arena)
    return;

  ArenaBlock *block = arena->first_block;
  while (block) {
    ArenaBlock *next = block->next;
    arena_block_destroy(block);
    block = next;
  }
  free(arena);
}

void *arena_alloc(Arena *arena, size_t size) {
  // Align size to ARENA_ALIGNMENT
  size = (size + ARENA_ALIGNMENT - 1) & ~(ARENA_ALIGNMENT - 1);

  ArenaBlock *block = arena->current_block;
  size_t used = block->current - block->base;
  size_t remaining = block->capacity - used;

  // Check if current block has space
  if (LIKELY(size <= remaining)) {
    void *ptr = block->current;
    block->current += size;
    arena->total_allocated += size;
    return ptr;
  }

  // Need new block - allocate at least as much as requested or default size
  size_t new_size = size > ARENA_DEFAULT_SIZE ? size : ARENA_DEFAULT_SIZE;
  ArenaBlock *new_block = arena_block_create(new_size);
  if (!new_block)
    return nullptr;

  // Link to chain
  block->next = new_block;
  arena->current_block = new_block;

  void *ptr = new_block->current;
  new_block->current += size;
  arena->total_allocated += size;
  return ptr;
}

void arena_reset(Arena *arena) {
  if (!arena)
    return;

  // Keep first block, free others
  ArenaBlock *block = arena->first_block->next;
  while (block) {
    ArenaBlock *next = block->next;
    arena_block_destroy(block);
    block = next;
  }

  arena->first_block->current = arena->first_block->base;
  arena->first_block->next = nullptr;
  arena->current_block = arena->first_block;
  arena->total_allocated = 0;
}

// ============================================================================
// OBJECT ALLOCATION (Arena-based for small objects)
// ============================================================================

// Threshold for arena vs malloc (objects larger than this use malloc)
#define ARENA_ALLOC_THRESHOLD 256

static Obj *allocate_object(VM *vm, size_t size, ObjType type) {
  Obj *obj;
  uint8_t from_arena = 0;

  // Use arena for small objects, malloc for large ones
  if (LIKELY(size <= ARENA_ALLOC_THRESHOLD && vm->arena != nullptr)) {
    obj = (Obj *)arena_alloc(vm->arena, size);
    from_arena = 1;
  } else {
    obj = static_cast<Obj*>(malloc(size));
    from_arena = 0;
  }

  if (!obj)
    return nullptr;

  obj->type = type;
  obj->arena_allocated = from_arena;
  obj->next = vm->objects;
  vm->objects = obj;
  vm->bytes_allocated += size;
  return obj;
}

// String hashing (FNV-1a)
static uint32_t hash_string(const char *key, int length) {
  uint32_t hash = 2166136261u;
  for (int i = 0; i < length; i++) {
    hash ^= (uint8_t)key[i];
    hash *= 16777619;
  }
  return hash;
}

ObjString *vm_alloc_string(VM *vm, const char *chars, int length) {
  uint32_t hash = hash_string(chars, length);

  // Check string interning table
  for (int i = 0; i < vm->string_count; i++) {
    ObjString *s = vm->strings[i];
    if (s->length == length && s->hash == hash &&
        memcmp(s->chars, chars, length) == 0) {
      return s; // Return interned string
    }
  }

  // Allocate new string
  ObjString *str =
      (ObjString *)allocate_object(vm, sizeof(ObjString), OBJ_STRING);
  str->length = length;

  // OPTIMIZATION: Pre-allocate capacity for string concatenation
  // Empty strings and small strings get extra capacity for future appends
  if (length == 0) {
    str->capacity = 256; // Empty string gets 256 bytes (common in loops!)
  } else if (length < 64) {
    str->capacity = 128; // Small strings get 128 bytes
  } else {
    str->capacity = length * 2; // Larger strings: 2x growth strategy
  }

  str->obj.ref_count = 1; // Start with 1 reference (The caller/stack)
  str->chars = static_cast<char*>(malloc(str->capacity + 1));
  memcpy(str->chars, chars, length);
  str->chars[length] = '\0';
  str->hash = hash;

  // Add to interning table
  if (vm->string_count >= vm->string_capacity) {
    vm->string_capacity =
        vm->string_capacity == 0 ? 64 : vm->string_capacity * 2;
    vm->strings = (ObjString **)realloc(vm->strings, vm->string_capacity *
                                                         sizeof(ObjString *));
  }
  vm->strings[vm->string_count++] = str;

  return str;
}

ObjString *vm_take_string(VM *vm, char *chars, int length) {
  uint32_t hash = hash_string(chars, length);

  // Check string interning table
  for (int i = 0; i < vm->string_count; i++) {
    ObjString *s = vm->strings[i];
    if (s->length == length && s->hash == hash &&
        memcmp(s->chars, chars, length) == 0) {
      // Found match, free the passed buffer
      free(chars);
      return s;
    }
  }

  // Allocate new string object wrapper
  ObjString *str =
      (ObjString *)allocate_object(vm, sizeof(ObjString), OBJ_STRING);
  str->length = length;
  str->capacity = length;
  str->obj.ref_count = 1;
  str->chars = chars; // Take ownership directly!
  str->chars[length] = '\0';
  str->hash = hash;

  // Add to interning table
  if (vm->string_count >= vm->string_capacity) {
    vm->string_capacity =
        vm->string_capacity == 0 ? 64 : vm->string_capacity * 2;
    vm->strings = (ObjString **)realloc(vm->strings, vm->string_capacity *
                                                         sizeof(ObjString *));
  }
  vm->strings[vm->string_count++] = str;

  return str;
}

static char *read_file(const char *path) {
  FILE *file = fopen(path, "rb");
  if (file == nullptr) {
    return nullptr; // File not found
  }

  fseek(file, 0L, SEEK_END);
  size_t fileSize = ftell(file);
  rewind(file);

  char *buffer = static_cast<char*>(malloc(fileSize + 1));
  if (buffer == nullptr) {
    fclose(file);
    return nullptr; // Allocation failed
  }

  size_t bytesRead = fread(buffer, sizeof(char), fileSize, file);
  if (bytesRead < fileSize) {
    // Could not read file completely
    free(buffer);
    fclose(file);
    return nullptr;
  }

  buffer[bytesRead] = '\0';
  fclose(file);
  return buffer;
}

// Helper: Parse source code into AST
static ASTNode_C *parse_source(char *source) {
  // 1. Lexing
  Lexer *lexer = lexer_create(source);
  Token **tokens = nullptr;
  int token_count = 0;
  int token_capacity = 0;

  Token *token = lexer_next_token(lexer);
  while (token->type() != TOKEN_EOF) {
    if (token_count >= token_capacity) {
      token_capacity = token_capacity < 8 ? 8 : token_capacity * 2;
      tokens = (Token **)realloc(tokens, sizeof(Token *) * token_capacity);
    }
    tokens[token_count++] = token;
    token = lexer_next_token(lexer);
  }
  // Add EOF token
  if (token_count >= token_capacity) {
    token_capacity = token_capacity < 8 ? 8 : token_capacity * 2;
    tokens = (Token **)realloc(tokens, sizeof(Token *) * token_capacity);
  }
  tokens[token_count++] = token; // EOF

  lexer_free(lexer);

  // 2. Parsing
  Parser_C *parser = parser_create(tokens, token_count);
  ASTNode_C *program = parser_parse(parser);

  // Cleanup tokens and parser
  parser_free(parser);
  // Note: parser_free does not free tokens array content, usually?
  // Check parser checks. Tulpar parser takes ownership?
  // Standard Tulpar implementation: parser_free frees parser struct.
  // We should free tokens.
  for (int i = 0; i < token_count; i++) {
    token_free(tokens[i]);
  }
  free(tokens);

  return program;
}

ObjString *vm_copy_string(VM *vm, const char *chars, int length) {
  return vm_alloc_string(vm, chars, length);
}

ObjString *vm_alloc_string_buffer(VM *vm, int length, int capacity) {
  ObjString *str =
      (ObjString *)allocate_object(vm, sizeof(ObjString), OBJ_STRING);
  str->length = length;
  str->capacity = capacity;
  str->obj.ref_count = 1;
  str->chars = static_cast<char*>(malloc(capacity + 1));
  str->chars[length] = '\0';
  str->hash = 0; // Not hashed yet
  return str;
}

ObjFunction *vm_new_function(VM *vm) {
  ObjFunction *func =
      (ObjFunction *)allocate_object(vm, sizeof(ObjFunction), OBJ_FUNCTION);
  func->arity = 0;
  func->upvalue_count = 0;
  func->name = nullptr;
  func->call_count = 0;  // Init profiling

  // Initialize chunk manually
  func->chunk.code = nullptr;
  func->chunk.code_length = 0;
  func->chunk.code_capacity = 0;
  func->chunk.lines = nullptr;
  func->chunk.line_offsets = nullptr;
  func->chunk.line_count = 0;
  func->chunk.line_capacity = 0;
  func->chunk.constants = nullptr;
  func->chunk.const_count = 0;
  func->chunk.const_capacity = 0;
  func->chunk.local_names = nullptr;
  func->chunk.local_count = 0;

  return func;
}

// ============================================================================
// VM CREATION / DESTRUCTION
// ============================================================================

VM *vm_create() {
  // Initialize sockets on Windows (first time only)
#ifndef TULPAR_WASM_BUILD
  static int sockets_initialized = 0;
  if (!sockets_initialized) {
    tulpar_socket_init();
    sockets_initialized = 1;
  }
#endif

  VM *vm = static_cast<VM*>(malloc(sizeof(VM)));

  vm->frame_count = 0;
  vm->stack_top = vm->stack;
  vm->global_count = 0;
  // Top-level user script lives in cwd → no import_dir. Slot is
  // populated when OP_IMPORT pushes a frame for a freshly-loaded module.
  vm->import_dirs[0][0] = '\0';

  // Initialize global cache
  vm->global_cache_count = 0;
  for (int i = 0; i < VM_GLOBALS_MAX; i++) {
    vm->global_cache[i] = -1; // Invalid index
  }

  vm->objects = nullptr;
  vm->bytes_allocated = 0;
  vm->next_gc = 1024 * 1024; // First GC at 1MB

  // Create arena allocator for fast object allocation
  vm->arena = arena_create(ARENA_DEFAULT_SIZE);

  vm->strings = nullptr;
  vm->string_count = 0;
  vm->string_capacity = 0;

  return vm;
}

static void free_object(Obj *obj) {
  int from_arena = obj->arena_allocated;

  switch (obj->type) {
  case OBJ_STRING: {
    ObjString *str = (ObjString *)obj;
    // chars is always malloc'd, free it
    if (str->chars)
      free(str->chars);
    // Only free the struct if not from arena
    if (!from_arena)
      free(str);
    break;
  }
  case OBJ_FUNCTION: {
    ObjFunction *func = (ObjFunction *)obj;
    // Free chunk contents (always malloc'd)
    if (func->chunk.code)
      free(func->chunk.code);
    if (func->chunk.lines)
      free(func->chunk.lines);
    if (func->chunk.line_offsets)
      free(func->chunk.line_offsets);
    if (func->chunk.constants) {
      free(func->chunk.constants);
    }
    if (func->chunk.local_names) {
      for (int i = 0; i < func->chunk.local_count; i++) {
        if (func->chunk.local_names[i])
          free(func->chunk.local_names[i]);
      }
      free(func->chunk.local_names);
    }
    if (!from_arena)
      free(func);
    break;
  }
  case OBJ_ARRAY: {
    ObjArray *arr = (ObjArray *)obj;
    // items is always malloc'd
    if (arr->items)
      free(arr->items);
    if (!from_arena)
      free(arr);
    break;
  }
  case OBJ_OBJECT: {
    ObjObject *o = (ObjObject *)obj;
    // keys and values are always malloc'd
    if (o->keys)
      free(o->keys);
    if (o->values)
      free(o->values);
    if (!from_arena)
      free(o);
    break;
  }
  case OBJ_STRUCT: {
    // Phase 1 ObjStruct: trivially-unboxable only, fields live inline
    // via the flexible array member. No owned pointers to chase, so
    // freeing the object itself is enough — the arena claims arena
    // allocations on its own.
    if (!from_arena)
      free(obj);
    break;
  }
  case OBJ_CLOSURE: {
    if (!from_arena)
      free(obj);
    break;
  }
  default:
    if (!from_arena)
      free(obj);
    break;
  }
}

void vm_free(VM *vm) {
  if (!vm)
    return;

  // Free all objects (only those allocated with malloc, not arena)
  // Note: Arena objects are freed when arena is destroyed
  Obj *obj = vm->objects;
  while (obj) {
    Obj *next = obj->next;
    // Only free if it's a large object (allocated with malloc)
    // For now we still free all - arena handles its own memory
    free_object(obj);
    obj = next;
  }

  // Free arena allocator
  if (vm->arena) {
    arena_destroy(vm->arena);
    vm->arena = nullptr;
  }

  // Free string table
  if (vm->strings)
    free(vm->strings);

  free(vm);
}

// ============================================================================
// STACK OPERATIONS
// ============================================================================

// ============================================================================
// GLOBALS
// ============================================================================

void vm_define_global(VM *vm, ObjString *name, VMValue value) {
  // Check if already exists
  for (int i = 0; i < vm->global_count; i++) {
    if (vm->globals[i].key == name) {
      vm->globals[i].value = value;
      return;
    }
  }

  // Add new global
  if (vm->global_count < VM_GLOBALS_MAX) {
    vm->globals[vm->global_count].key = name;
    vm->globals[vm->global_count].value = value;
    vm->global_count++;
  }
}

VMValue *vm_get_global(VM *vm, ObjString *name) {
  for (int i = 0; i < vm->global_count; i++) {
    if (vm->globals[i].key == name) {
      return &vm->globals[i].value;
    }
  }
  return nullptr;
}

// ============================================================================
// VALUE PRINTING
// ============================================================================

void vm_print_value(VMValue value) {
  switch (value.type) {
  case VM_VAL_INT:
    printf("%lld", AS_INT(value));
    break;
  case VM_VAL_FLOAT:
    printf("%g", AS_FLOAT(value));
    break;
  case VM_VAL_BOOL:
    printf(AS_BOOL(value) ? "true" : "false");
    break;
  case VM_VAL_VOID:
    // The `null` literal and a value-less (void) result share this tag;
    // "null" reads better than "void" now that `null` is user-facing.
    printf("null");
    break;
  case VM_VAL_OBJ:
    if (IS_STRING(value)) {
      printf("%s", AS_STRING(value)->chars);
    } else if (IS_FUNCTION(value)) {
      ObjFunction *func = AS_FUNCTION(value);
      if (func->name) {
        printf("<fn %s>", func->name->chars);
      } else {
        printf("<script>");
      }
    } else if (IS_ARRAY(value)) {
      // Avoid infinite recursion for now, simple type print
      printf("<array>");
    } else if (IS_CLOSURE(value)) {
      printf("<closure>");
    } else if (IS_OBJECT(value)) {
      printf("<object>");
    } else if (IS_STRUCT(value)) {
      // Heap-promoted typed struct (Plan 04 v2). Phase 1: trivially-
      // unboxable only — every field is i64 (int/bool). We don't have
      // field names on the heap struct itself; the runtime schema
      // registry isn't wired up yet, so we print `Type { 1, 2, 3 }`
      // by-position. AOT-side print path knows the schema and uses
      // named fields for stack-typed locals (`Type { x: 1, y: 2 }`).
      ObjStruct *s = AS_STRUCT(value);
      printf("%s { ", s->type_name ? s->type_name : "struct");
      for (int i = 0; i < s->field_count; i++) {
        if (i > 0) printf(", ");
        printf("%lld", (long long)s->fields[i]);
      }
      printf(" }");
    } else {
      printf("<obj>");
    }
    break;
  }
}

void vm_runtime_error(VM *vm, const char *format, ...) {
  (void)vm; // Unused parameter
  va_list args;
  va_start(args, format);
  fprintf(stderr, "%s", tulpar::i18n::tr_for_en("Runtime Error: "));
  vfprintf(stderr, tulpar::i18n::tr_for_en(format), args);
  va_end(args);
  fprintf(stderr, "\n");
}

// ============================================================================
// VM EXECUTION
// ============================================================================

// Helper macros for dispatch
#undef READ_BYTE
#undef READ_SHORT
#undef READ_CONSTANT

#if defined(__GNUC__) && !defined(__clang__)
#define READ_BYTE() (*ip++)
#define READ_SHORT() (ip += 2, (uint16_t)((ip[-2]) | (ip[-1] << 8)))
#define READ_CONSTANT() (chunk->constants[READ_SHORT()])

#define TARGET(op) op
#define DISPATCH() goto *dispatch_table[*ip++]

#else
#define READ_BYTE() (*ip++)
#define READ_SHORT() (ip += 2, (uint16_t)((ip[-2]) | (ip[-1] << 8)))
#define READ_CONSTANT() (chunk->constants[READ_SHORT()])

#define TARGET(op) case op
#define DISPATCH() break
#endif

// Floating point conversions if needed (implicit casts)
// Fast path binary operation using TYPE_PAIR for quick dispatch
// TOS CACHING: Direct stack pointer manipulation to avoid function call
// overhead
#define BINARY_OP(op)                                                          \
  do {                                                                         \
    VMValue *sp = vm->stack_top - 2;                                           \
    VMValue a = sp[0];                                                         \
    VMValue b = sp[1];                                                         \
    uint8_t type_pair = TYPE_PAIR(a, b);                                       \
    switch (type_pair) {                                                       \
    case TYPE_INT_INT:                                                         \
      sp[0] = VM_INT(AS_INT(a) op AS_INT(b));                                  \
      break;                                                                   \
    case TYPE_FLOAT_FLOAT:                                                     \
      sp[0] = VM_FLOAT(AS_FLOAT(a) op AS_FLOAT(b));                            \
      break;                                                                   \
    case TYPE_INT_FLOAT:                                                       \
      sp[0] = VM_FLOAT((double)AS_INT(a) op AS_FLOAT(b));                      \
      break;                                                                   \
    case TYPE_FLOAT_INT:                                                       \
      sp[0] = VM_FLOAT(AS_FLOAT(a) op(double) AS_INT(b));                      \
      break;                                                                   \
    default:                                                                   \
      sp[0] = VM_FLOAT(0.0); /* Error fallback */                              \
      break;                                                                   \
    }                                                                          \
    vm->stack_top = sp + 1;                                                    \
  } while (0)

// Fast path comparison operation using TYPE_PAIR
// TOS CACHING: Direct stack pointer manipulation for comparisons
#define COMPARE_OP(op)                                                         \
  do {                                                                         \
    VMValue *sp = vm->stack_top - 2;                                           \
    VMValue a = sp[0];                                                         \
    VMValue b = sp[1];                                                         \
    uint8_t type_pair = TYPE_PAIR(a, b);                                       \
    switch (type_pair) {                                                       \
    case TYPE_INT_INT:                                                         \
      sp[0] = VM_BOOL(AS_INT(a) op AS_INT(b));                                 \
      break;                                                                   \
    case TYPE_FLOAT_FLOAT:                                                     \
      sp[0] = VM_BOOL(AS_FLOAT(a) op AS_FLOAT(b));                             \
      break;                                                                   \
    case TYPE_INT_FLOAT:                                                       \
      sp[0] = VM_BOOL((double)AS_INT(a) op AS_FLOAT(b));                       \
      break;                                                                   \
    case TYPE_FLOAT_INT:                                                       \
      sp[0] = VM_BOOL(AS_FLOAT(a) op(double) AS_INT(b));                       \
      break;                                                                   \
    default:                                                                   \
      sp[0] = VM_BOOL(0); /* Error fallback */                                 \
      break;                                                                   \
    }                                                                          \
    vm->stack_top = sp + 1;                                                    \
  } while (0)

// ============================================================================
// vm_run (the bytecode interpreter) was REMOVED on 2026-06-15: Tulpar is
// AOT-only (see CLAUDE.md "AOT-ONLY"). This file is retained ONLY for the
// shared runtime helpers below (arena, object/string allocators) that the
// AOT runtime and runtime_bindings.cpp link against. Do NOT reintroduce a
// bytecode interpreter here.
// ============================================================================

// ============================================================================
// ARRAY / OBJECT FUNCTIONS
// ============================================================================

ObjArray *vm_allocate_array(VM *vm) {
  ObjArray *array =
      (ObjArray *)allocate_object(vm, sizeof(ObjArray), OBJ_ARRAY);
  array->count = 0;
  array->capacity = 0;
  array->items = nullptr;
  return array;
}

void vm_array_push(VM *vm, ObjArray *array, VMValue value) {
  (void)vm; // Unused for now (unless we track memory/GC)
  if (array->capacity < array->count + 1) {
    int old_capacity = array->capacity;
    array->capacity = old_capacity < 8 ? 8 : old_capacity * 2;
    array->items =
        static_cast<VMValue*>(realloc(array->items, sizeof(VMValue) * array->capacity));
  }
  array->items[array->count] = value;
  array->count++;
}

ObjObject *vm_allocate_object(VM *vm) {
  ObjObject *obj =
      (ObjObject *)allocate_object(vm, sizeof(ObjObject), OBJ_OBJECT);
  obj->count = 0;
  obj->capacity = 0;
  obj->keys = nullptr;
  obj->values = nullptr;
  return obj;
}

