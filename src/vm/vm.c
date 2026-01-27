#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
typedef int socklen_t;
#else
// POSIX socket includes
#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

// Cross-platform compatibility definitions
typedef int SOCKET;
#define INVALID_SOCKET (-1)
#define SOCKET_ERROR (-1)
#define closesocket(s) close(s)
#define WSAGetLastError() errno
#endif
#include "../../lib/sqlite3/sqlite3.h"
#include "../jit/jit.h"
#include "../parser/parser.h"
#include "compiler.h"
#include "vm.h"
#include <math.h> // For clock()
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

// ============================================================================
// HELPERS
// ============================================================================

// Forward declarations for JSON serialization
static void value_to_json(VMValue v, char **buf, size_t *pos, size_t *capacity);

// External function from runtime_bindings.c
extern void print_vm_value(VMValue value);

static void ensure_capacity(char **buf, size_t *capacity, size_t needed) {
  while (*capacity < needed) {
    *capacity *= 2;
    *buf = realloc(*buf, *capacity);
  }
}

static void append_str(char **buf, size_t *pos, size_t *capacity,
                       const char *str) {
  size_t len = strlen(str);
  ensure_capacity(buf, capacity, *pos + len + 1);
  strcpy(*buf + *pos, str);
  *pos += len;
}

static void value_to_json(VMValue v, char **buf, size_t *pos,
                          size_t *capacity) {
  if (IS_INT(v)) {
    char tmp[64];
    snprintf(tmp, 64, "%lld", AS_INT(v));
    append_str(buf, pos, capacity, tmp);
  } else if (IS_FLOAT(v)) {
    char tmp[64];
    snprintf(tmp, 64, "%g", AS_FLOAT(v));
    append_str(buf, pos, capacity, tmp);
  } else if (IS_BOOL(v)) {
    append_str(buf, pos, capacity, AS_BOOL(v) ? "true" : "false");
  } else if (IS_VOID(v)) {
    append_str(buf, pos, capacity, "null");
  } else if (IS_STRING(v)) {
    append_str(buf, pos, capacity, "\"");
    append_str(buf, pos, capacity, AS_STRING(v)->chars);
    append_str(buf, pos, capacity, "\"");
  } else if (IS_OBJ(v) && AS_OBJ(v)->type == OBJ_ARRAY) {
    ObjArray *arr = (ObjArray *)AS_OBJ(v);
    append_str(buf, pos, capacity, "[");
    for (int i = 0; i < arr->count; i++) {
      if (i > 0)
        append_str(buf, pos, capacity, ", ");
      value_to_json(arr->items[i], buf, pos, capacity);
    }
    append_str(buf, pos, capacity, "]");
  } else if (IS_OBJ(v) && AS_OBJ(v)->type == OBJ_OBJECT) {
    ObjObject *obj = (ObjObject *)AS_OBJ(v);
    append_str(buf, pos, capacity, "{");
    for (int i = 0; i < obj->count; i++) {
      if (i > 0)
        append_str(buf, pos, capacity, ", ");
      append_str(buf, pos, capacity, "\"");
      append_str(buf, pos, capacity, obj->keys[i]->chars);
      append_str(buf, pos, capacity, "\": ");
      value_to_json(obj->values[i], buf, pos, capacity);
    }
    append_str(buf, pos, capacity, "}");
  } else {
    append_str(buf, pos, capacity, "null");
  }
}

static void runtime_error(VM *vm, const char *format, ...) {
  va_list args;
  va_start(args, format);
  vfprintf(stderr, format, args);
  va_end(args);
  fputs("\n", stderr);

  for (int i = vm->frame_count - 1; i >= 0; i--) {
    CallFrame *frame = &vm->frames[i];
    ObjFunction *function = frame->function;
    // Instruction pointer offset - 1 (because ip is already advanced)
    size_t instruction = frame->ip - function->chunk.code - 1;
    if (instruction < (size_t)function->chunk.code_length) { // Bounds check
      // Simple line lookup (can be optimized)
      // For now assume lines array matches instructions 1:1 roughly or use RLE
      // logic if implemented Since we used rough RLE in byteocde.c, let's just
      // create a helper later. For now just print "in function"
    }

    fprintf(stderr, "[line %d] in ", -1); // Line lookup TODO
    if (function->name == NULL) {
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
  ArenaBlock *block = (ArenaBlock *)malloc(sizeof(ArenaBlock));
  if (!block)
    return NULL;

  block->base = (char *)malloc(size);
  if (!block->base) {
    free(block);
    return NULL;
  }

  block->current = block->base;
  block->capacity = size;
  block->next = NULL;
  return block;
}

static void arena_block_destroy(ArenaBlock *block) {
  if (block) {
    free(block->base);
    free(block);
  }
}

Arena *arena_create(size_t initial_size) {
  Arena *arena = (Arena *)malloc(sizeof(Arena));
  if (!arena)
    return NULL;

  arena->first_block = arena_block_create(initial_size);
  if (!arena->first_block) {
    free(arena);
    return NULL;
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
    return NULL;

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
  arena->first_block->next = NULL;
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
  if (LIKELY(size <= ARENA_ALLOC_THRESHOLD && vm->arena != NULL)) {
    obj = (Obj *)arena_alloc(vm->arena, size);
    from_arena = 1;
  } else {
    obj = (Obj *)malloc(size);
    from_arena = 0;
  }

  if (!obj)
    return NULL;

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
  str->chars = (char *)malloc(str->capacity + 1);
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
  if (file == NULL) {
    return NULL; // File not found
  }

  fseek(file, 0L, SEEK_END);
  size_t fileSize = ftell(file);
  rewind(file);

  char *buffer = (char *)malloc(fileSize + 1);
  if (buffer == NULL) {
    fclose(file);
    return NULL; // Allocation failed
  }

  size_t bytesRead = fread(buffer, sizeof(char), fileSize, file);
  if (bytesRead < fileSize) {
    // Could not read file completely
    free(buffer);
    fclose(file);
    return NULL;
  }

  buffer[bytesRead] = '\0';
  fclose(file);
  return buffer;
}

// Helper: Parse source code into AST
static ASTNode *parse_source(char *source) {
  // 1. Lexing
  Lexer *lexer = lexer_create(source);
  Token **tokens = NULL;
  int token_count = 0;
  int token_capacity = 0;

  Token *token = lexer_next_token(lexer);
  while (token->type != TOKEN_EOF) {
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
  Parser *parser = parser_create(tokens, token_count);
  ASTNode *program = parser_parse(parser);

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
  str->chars = (char *)malloc(capacity + 1);
  str->chars[length] = '\0';
  str->hash = 0; // Not hashed yet
  return str;
}

ObjFunction *vm_new_function(VM *vm) {
  ObjFunction *func =
      (ObjFunction *)allocate_object(vm, sizeof(ObjFunction), OBJ_FUNCTION);
  func->arity = 0;
  func->upvalue_count = 0;
  func->name = NULL;
  func->call_count = 0;  // Init profiling
  func->jit_code = NULL; // No JIT code yet

  // Initialize chunk manually
  func->chunk.code = NULL;
  func->chunk.code_length = 0;
  func->chunk.code_capacity = 0;
  func->chunk.lines = NULL;
  func->chunk.line_count = 0;
  func->chunk.line_capacity = 0;
  func->chunk.constants = NULL;
  func->chunk.const_count = 0;
  func->chunk.const_capacity = 0;
  func->chunk.local_names = NULL;
  func->chunk.local_count = 0;

  return func;
}

// ============================================================================
// VM CREATION / DESTRUCTION
// ============================================================================

VM *vm_create() {
  VM *vm = (VM *)malloc(sizeof(VM));

  vm->frame_count = 0;
  vm->stack_top = vm->stack;
  vm->global_count = 0;

  // Initialize global cache
  vm->global_cache_count = 0;
  for (int i = 0; i < VM_GLOBALS_MAX; i++) {
    vm->global_cache[i] = -1; // Invalid index
  }

  vm->objects = NULL;
  vm->bytes_allocated = 0;
  vm->next_gc = 1024 * 1024; // First GC at 1MB

  // Create arena allocator for fast object allocation
  vm->arena = arena_create(ARENA_DEFAULT_SIZE);

  vm->strings = NULL;
  vm->string_count = 0;
  vm->string_capacity = 0;

  vm->legacy_interp = NULL; // Initialized to NULL

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
    // Free JIT compiled code
    if (func->jit_code) {
      jit_free_compiled(func->jit_code);
      func->jit_code = NULL;
    }
    // Free chunk contents (always malloc'd)
    if (func->chunk.code)
      free(func->chunk.code);
    if (func->chunk.lines)
      free(func->chunk.lines);
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
    vm->arena = NULL;
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
  return NULL;
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
    printf("void");
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
    } else if (IS_OBJECT(value)) {
      printf("<object>");
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
  fprintf(stderr, "Runtime Error: ");
  vfprintf(stderr, format, args);
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

#ifdef __GNUC__
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

VMResult vm_run(VM *vm, ObjFunction *function) {
  // Push the function to stack (as if it was called)
  vm_push(vm, VM_OBJ(function));

  // Create initial frame
  CallFrame *frame = &vm->frames[vm->frame_count++];
  frame->function = function;
  frame->ip = function->chunk.code;
  frame->slots = vm->stack; // First slot is function itself

  // Local registers for performance
  register uint8_t *ip = frame->ip;
  Chunk *chunk = &function->chunk;

#ifdef __GNUC__
  static void *dispatch_table[] = {
      &&OP_NOP, &&OP_POP, &&OP_DUP, &&OP_CONST_INT, &&OP_CONST_FLOAT,
      &&OP_CONST_TRUE, &&OP_CONST_FALSE, &&OP_CONST_VOID, &&OP_CONST_FUNC,
      &&OP_CONST_STR, &&OP_ADD, &&OP_SUB, &&OP_MUL, &&OP_DIV, &&OP_MOD,
      &&OP_NEG, &&OP_INC, &&OP_DEC, &&OP_EQ, &&OP_NE, &&OP_LT, &&OP_LE, &&OP_GT,
      &&OP_GE, &&OP_AND, &&OP_OR, &&OP_NOT, &&OP_LOAD_LOCAL, &&OP_STORE_LOCAL,
      &&OP_LOAD_GLOBAL, &&OP_STORE_GLOBAL, &&OP_JUMP, &&OP_JUMP_IF_FALSE,
      &&OP_JUMP_IF_TRUE, &&OP_LOOP, &&OP_CALL, &&OP_TAIL_CALL,
      &&OP_CALL_BUILTIN, &&OP_RETURN, &&OP_RETURN_VOID, &&OP_ARRAY_NEW,
      &&OP_ARRAY_PUSH, &&OP_ARRAY_GET, &&OP_ARRAY_SET, &&OP_OBJECT_NEW,
      &&OP_OBJECT_GET, &&OP_OBJECT_SET, &&OP_PRINT, &&OP_IMPORT, &&OP_TRY,
      &&OP_POP_TRY, &&OP_THROW, &&OP_HALT,
      // Optimized opcodes
      &&OP_FOR_ITER, &&OP_INC_LOCAL, &&OP_DEC_LOCAL,
      // Type-specialized opcodes
      &&OP_ADD_INT, &&OP_SUB_INT, &&OP_MUL_INT, &&OP_DIV_INT, &&OP_LT_INT,
      &&OP_LE_INT, &&OP_GT_INT, &&OP_GE_INT, &&OP_EQ_INT, &&OP_NE_INT,
      // Fast call opcodes
      &&OP_CALL_FAST, &&OP_RETURN_INT, &&OP_CALL_0, &&OP_CALL_1, &&OP_CALL_2,
      &&OP_RETURN_FAST,
      // Inline cached calls
      &&OP_CALL_1_CACHED, &&OP_CALL_2_CACHED,
      // Superinstructions
      &&OP_LOAD_LOCAL_ADD, &&OP_LOAD_LOCAL_SUB, &&OP_LOAD_LOCAL_LT,
      &&OP_DEC_JUMP_NZ, &&OP_LOAD_ADD_STORE, &&OP_INC_LOCAL_FAST,
      // Register-based opcodes
      &&OP_ADD_RRR, &&OP_SUB_RRR, &&OP_MUL_RRR, &&OP_DIV_RRR, &&OP_LT_RRR,
      &&OP_LE_RRR, &&OP_GT_RRR, &&OP_GE_RRR, &&OP_EQ_RRR, &&OP_NE_RRR,
      &&OP_MOV_RR, &&OP_MOV_RI, &&OP_ADD_RI, &&OP_SUB_RI, &&OP_LT_RI};

  DISPATCH(); // Jump to first instruction
#endif

  // Main loop
  for (;;) {
#ifndef __GNUC__
    uint8_t instruction = READ_BYTE();
    switch (instruction)
#endif
    {
      TARGET(OP_NOP) : DISPATCH();

      TARGET(OP_POP) : vm_pop(vm);
      DISPATCH();

      TARGET(OP_DUP) : vm_push(vm, vm_peek(vm, 0));
      DISPATCH();

      TARGET(OP_CONST_INT) : {
        Constant c = READ_CONSTANT();
        vm_push(vm, VM_INT(c.int_val));
        DISPATCH();
      }
      TARGET(OP_CONST_FLOAT) : {
        Constant c = READ_CONSTANT();
        vm_push(vm, VM_FLOAT(c.float_val));
        DISPATCH();
      }
      TARGET(OP_CONST_TRUE) : vm_push(vm, VM_BOOL(1));
      DISPATCH();
      TARGET(OP_CONST_FALSE) : vm_push(vm, VM_BOOL(0));
      DISPATCH();
      TARGET(OP_CONST_VOID) : vm_push(vm, VM_VOID());
      DISPATCH();

      TARGET(OP_CONST_FUNC) : {
        Constant c = READ_CONSTANT();
        if (c.type != CONST_FUNCTION) {
          runtime_error(vm, "Invalid function constant");
          return VM_RUNTIME_ERROR;
        }
        CompiledFunction *cf = c.func_val;

        ObjFunction *func = vm_new_function(vm);
        func->arity = cf->arity;
        if (cf->name) {
          func->name = vm_copy_string(vm, cf->name, strlen(cf->name));
        }

        // Deep copy chunk to avoid double-free issues
        Chunk *src = cf->chunk;
        Chunk *dest = &func->chunk;

        // Copy code
        if (src->code_length > 0) {
          dest->code = (uint8_t *)malloc(src->code_length);
          memcpy(dest->code, src->code, src->code_length);
          dest->code_length = src->code_length;
          dest->code_capacity = src->code_length;
        }

        // Copy lines
        if (src->line_count > 0) {
          dest->lines = (int *)malloc(src->line_count * sizeof(int));
          memcpy(dest->lines, src->lines, src->line_count * sizeof(int));
          dest->line_count = src->line_count;
          dest->line_capacity = src->line_count;
        }

        // Copy constants (Shallow copy of the array, but we need to be careful
        // about ownership) For simple integers/floats it's fine. For strings,
        // we duplicate.
        if (src->const_count > 0) {
          dest->constants =
              (Constant *)malloc(src->const_count * sizeof(Constant));
          for (int i = 0; i < src->const_count; i++) {
            dest->constants[i] = src->constants[i];
            if (src->constants[i].type == CONST_STRING) {
              dest->constants[i].string_val =
                  strdup(src->constants[i].string_val);
            }
            // Recursive function copying?
            // If a function contains another function constant, we just copy
            // the pointer to CompiledFunction. When that internal OP_CONST_FUNC
            // runs, it will copy THAT one. So simplistic copy is fine for
            // CONST_FUNCTION.
          }
          dest->const_count = src->const_count;
          dest->const_capacity = src->const_count;
        }

        // Copy local names
        if (src->local_count > 0) {
          dest->local_names =
              (char **)malloc(src->local_count * sizeof(char *));
          for (int i = 0; i < src->local_count; i++) {
            if (src->local_names[i])
              dest->local_names[i] = strdup(src->local_names[i]);
            else
              dest->local_names[i] = NULL;
          }
          dest->local_count = src->local_count;
        }

        vm_push(vm, VM_OBJ(func));
        DISPATCH();
      }

      TARGET(OP_CONST_STR) : {
        Constant c = READ_CONSTANT();
        ObjString *str = vm_copy_string(vm, c.string_val, strlen(c.string_val));
        vm_push(vm, VM_OBJ(str));
        DISPATCH();
      }

      // --- Arithmetic ---
      TARGET(OP_ADD) : {
        VMValue b = vm_peek(vm, 0);
        VMValue a = vm_peek(vm, 1);
        if (IS_STRING(a) && IS_STRING(b)) {
          vm_pop(vm);
          vm_pop(vm);
          ObjString *sa = AS_STRING(a);
          ObjString *sb = AS_STRING(b);
          // Concat
          ObjString *result = NULL;
          // Optimization: Check if we can reuse 'sa' buffer (In-Place)
          if (sa->capacity > sa->length) {
            int new_len = sa->length + sb->length;
            if (new_len <= sa->capacity) {
              // Fits in existing capacity! In-Place Append.
              // Incremental Hash Update O(M)
              uint32_t h = sa->hash;
              for (int i = 0; i < sb->length; i++) {
                sa->chars[sa->length + i] = sb->chars[i];
                h ^= (uint8_t)sb->chars[i];
                h *= 16777619;
              }
              sa->length = new_len;
              sa->chars[new_len] = 0;
              sa->hash = h;
              result = sa;
            } else {
              // Needs growth.
              int new_cap = (sa->capacity * 2) > new_len ? (sa->capacity * 2)
                                                         : (new_len + 16);
              sa->chars = realloc(sa->chars, new_cap + 1);
              sa->capacity = new_cap;

              // Incremental Hash
              uint32_t h = sa->hash;
              for (int i = 0; i < sb->length; i++) {
                sa->chars[sa->length + i] = sb->chars[i];
                h ^= (uint8_t)sb->chars[i];
                h *= 16777619;
              }
              sa->length = new_len;
              sa->chars[new_len] = 0;
              sa->hash = h;
              result = sa;
            }
          } else {
            // First append or immutable string
            int len = sa->length + sb->length;
            int cap = len < 64 ? 64 : len * 2;

            // Create uninterned buffer string
            result = vm_alloc_string_buffer(vm, len, cap);

            memcpy(result->chars, sa->chars, sa->length);
            memcpy(result->chars + sa->length, sb->chars, sb->length);
            result->chars[len] = 0;
            result->hash = hash_string(result->chars, len); // Initial hash O(N)
          }
          vm_push(vm, VM_OBJ(result));
        } else {
          BINARY_OP(+);
        }
        DISPATCH();
      }
      TARGET(OP_SUB) : BINARY_OP(-);
      DISPATCH();
      TARGET(OP_MUL) : BINARY_OP(*);
      DISPATCH();
      TARGET(OP_DIV) : BINARY_OP(/);
      DISPATCH();
      TARGET(OP_MOD) : {
        VMValue b = vm_pop(vm);
        VMValue a = vm_pop(vm);
        if (IS_INT(a) && IS_INT(b)) {
          if (AS_INT(b) == 0) {
            runtime_error(vm, "Modulo by zero");
            return VM_RUNTIME_ERROR;
          }
          vm_push(vm, VM_INT(AS_INT(a) % AS_INT(b)));
        } else {
          runtime_error(vm, "Modulo requires integers");
          return VM_RUNTIME_ERROR;
        }
        DISPATCH();
      }
      TARGET(OP_NEG) : {
        if (IS_INT(vm_peek(vm, 0))) {
          vm_push(vm, VM_INT(-AS_INT(vm_pop(vm))));
        } else {
          vm_push(vm, VM_FLOAT(-AS_FLOAT(vm_pop(vm))));
        }
        DISPATCH();
      }

      TARGET(OP_INC) : {
        VMValue *v = &vm->stack_top[-1];
        if (IS_INT(*v)) {
          v->as.int_val++;
        } else if (IS_FLOAT(*v)) {
          v->as.float_val++;
        } else {
          runtime_error(vm, "Operand must be a number.");
          return VM_RUNTIME_ERROR;
        }
        DISPATCH();
      }

      TARGET(OP_DEC) : {
        VMValue *v = &vm->stack_top[-1];
        if (IS_INT(*v)) {
          v->as.int_val--;
        } else if (IS_FLOAT(*v)) {
          v->as.float_val--;
        } else {
          runtime_error(vm, "Operand must be a number.");
          return VM_RUNTIME_ERROR;
        }
        DISPATCH();
      }

      // --- Comparison ---
      TARGET(OP_EQ) : {
        VMValue b = vm_pop(vm);
        VMValue a = vm_pop(vm);
        if (IS_INT(a) && IS_INT(b))
          vm_push(vm, VM_BOOL(AS_INT(a) == AS_INT(b)));
        else if (IS_FLOAT(a) || IS_FLOAT(b)) {
          double fa = IS_FLOAT(a) ? AS_FLOAT(a) : (double)AS_INT(a);
          double fb = IS_FLOAT(b) ? AS_FLOAT(b) : (double)AS_INT(b);
          vm_push(vm, VM_BOOL(fa == fb));
        } else if (IS_BOOL(a) && IS_BOOL(b))
          vm_push(vm, VM_BOOL(AS_BOOL(a) == AS_BOOL(b)));
        else if (IS_STRING(a) && IS_STRING(b)) {
          // Compare by content, not just pointer (strings may not be interned)
          ObjString *sa = AS_STRING(a);
          ObjString *sb = AS_STRING(b);
          int eq = (sa == sb) || (sa->length == sb->length &&
                                  strcmp(sa->chars, sb->chars) == 0);
          vm_push(vm, VM_BOOL(eq));
        } else
          vm_push(vm, VM_BOOL(0));
        DISPATCH();
      }
      TARGET(OP_NE) : {
        VMValue b = vm_pop(vm);
        VMValue a = vm_pop(vm);
        if (IS_INT(a) && IS_INT(b))
          vm_push(vm, VM_BOOL(AS_INT(a) != AS_INT(b)));
        else if (IS_FLOAT(a) || IS_FLOAT(b)) {
          double fa = IS_FLOAT(a) ? AS_FLOAT(a) : (double)AS_INT(a);
          double fb = IS_FLOAT(b) ? AS_FLOAT(b) : (double)AS_INT(b);
          vm_push(vm, VM_BOOL(fa != fb));
        } else if (IS_BOOL(a) && IS_BOOL(b))
          vm_push(vm, VM_BOOL(AS_BOOL(a) != AS_BOOL(b)));
        else if (IS_STRING(a) && IS_STRING(b)) {
          ObjString *sa = AS_STRING(a);
          ObjString *sb = AS_STRING(b);
          int ne = (sa != sb) && (sa->length != sb->length ||
                                  strcmp(sa->chars, sb->chars) != 0);
          vm_push(vm, VM_BOOL(ne));
        } else
          vm_push(vm, VM_BOOL(1));
        DISPATCH();
      }
      TARGET(OP_LT) : COMPARE_OP(<);
      DISPATCH();
      TARGET(OP_GT) : COMPARE_OP(>);
      DISPATCH();
      TARGET(OP_LE) : COMPARE_OP(<=);
      DISPATCH();
      TARGET(OP_GE) : COMPARE_OP(>=);
      DISPATCH();

      // --- Logic ---
      TARGET(OP_NOT) : vm_push(vm, VM_BOOL(!AS_BOOL(vm_pop(vm))));
      DISPATCH();

      TARGET(OP_AND) : {
        uint16_t offset = READ_SHORT();
        VMValue v = vm_peek(vm, 0);
        if (!AS_BOOL(v))
          ip += offset;
        else
          vm_pop(vm);
        DISPATCH();
      }

      TARGET(OP_OR) : {
        uint16_t offset = READ_SHORT();
        VMValue v = vm_peek(vm, 0);
        if (AS_BOOL(v))
          ip += offset;
        else
          vm_pop(vm);
        DISPATCH();
      }

      // --- Variables ---
      TARGET(OP_LOAD_LOCAL) : {
        uint16_t slot = READ_SHORT();
        vm_push(vm, frame->slots[slot]);
        DISPATCH();
      }
      TARGET(OP_STORE_LOCAL) : {
        uint16_t slot = READ_SHORT();
        frame->slots[slot] = vm_peek(vm, 0);
        DISPATCH();
      }
      TARGET(OP_LOAD_GLOBAL) : {
        Constant c = READ_CONSTANT(); // name string
        uint16_t cache_slot = READ_SHORT();

        VMValue val;
        // Fast path: Inline cache hit (no string allocation needed)
        if (LIKELY(cache_slot < (uint16_t)vm->global_count)) {
          val = vm->globals[cache_slot].value;
          vm_push(vm, val);
          DISPATCH();
        }

        // Slow path: Cache miss - need to look up by name
        ObjString *name =
            vm_alloc_string(vm, c.string_val, strlen(c.string_val));
        VMValue *v = vm_get_global(vm, name);
        if (UNLIKELY(!v)) {
          runtime_error(vm, "Undefined gloabl '%s'", name->chars);
          return VM_RUNTIME_ERROR;
        }
        val = *v;

        // Update cache: write back index to bytecode for next access
        for (int i = 0; i < vm->global_count; i++) {
          if (vm->globals[i].key == name) {
            *(ip - 2) = (i >> 8) & 0xFF; // HI
            *(ip - 1) = i & 0xFF;        // LO
            break;
          }
        }

        vm_push(vm, val);
        DISPATCH();
      }
      TARGET(OP_STORE_GLOBAL) : {
        Constant c = READ_CONSTANT();
        uint16_t cache_slot = READ_SHORT();

        // Fast path: Inline cache hit (no string allocation needed)
        if (LIKELY(cache_slot < (uint16_t)vm->global_count)) {
          vm->globals[cache_slot].value = vm_peek(vm, 0);
          DISPATCH();
        }

        // Slow path: Cache miss
        ObjString *name =
            vm_alloc_string(vm, c.string_val, strlen(c.string_val));
        vm_define_global(vm, name, vm_peek(vm, 0));

        // Update cache for next access
        for (int i = 0; i < vm->global_count; i++) {
          if (vm->globals[i].key == name) {
            *(ip - 2) = (i >> 8) & 0xFF;
            *(ip - 1) = i & 0xFF;
            break;
          }
        }
        DISPATCH();
      }

      // --- Jumps ---
      TARGET(OP_JUMP) : {
        uint16_t offset = READ_SHORT();
        ip += offset;
        DISPATCH();
      }
      TARGET(OP_JUMP_IF_FALSE) : {
        uint16_t offset = READ_SHORT();
        VMValue v = vm_peek(vm, 0);
        int is_false =
            (IS_BOOL(v) && !AS_BOOL(v)) || (IS_INT(v) && AS_INT(v) == 0);
        if (is_false)
          ip += offset;
        DISPATCH();
      }
      TARGET(OP_JUMP_IF_TRUE) : {
        uint16_t offset = READ_SHORT();
        VMValue v = vm_peek(vm, 0);
        int is_true =
            (IS_BOOL(v) && AS_BOOL(v)) || (IS_INT(v) && AS_INT(v) != 0);
        if (is_true)
          ip += offset;
        DISPATCH();
      }
      TARGET(OP_LOOP) : {
        uint16_t offset = READ_SHORT();
        uint8_t *loop_target = ip - offset;

        // Hot loop detection: Count iterations
        // Look up or create trace entry for this loop
        LoopTrace *trace = NULL;
        for (int i = 0; i < vm->loop_traces.count; i++) {
          if (vm->loop_traces.traces[i].loop_start == loop_target) {
            trace = &vm->loop_traces.traces[i];
            break;
          }
        }

        if (!trace && vm->loop_traces.count < MAX_LOOP_TRACES) {
          // New loop - create entry
          trace = &vm->loop_traces.traces[vm->loop_traces.count++];
          trace->loop_start = loop_target;
          trace->iteration_count = 0;
          trace->trace_recorded = 0;
          trace->native_code = NULL;
        }

        if (trace) {
          trace->iteration_count++;

          // If hot enough and has native code, use it
          // (Native code compilation would be done elsewhere)
        }

        ip = loop_target;
        DISPATCH();
      }

      // --- Functions ---
      TARGET(OP_CALL) : {
        int arg_count = READ_BYTE();
        VMValue callee = vm_peek(vm, arg_count);

        if (!IS_FUNCTION(callee)) {
          runtime_error(vm, "Can only call functions.");
          return VM_RUNTIME_ERROR;
        }

        ObjFunction *func = AS_FUNCTION(callee);
        if (arg_count != func->arity) {
          runtime_error(vm, "Expected %d arguments but got %d.", func->arity,
                        arg_count);
          return VM_RUNTIME_ERROR;
        }

        if (vm->frame_count == VM_FRAMES_MAX) {
          runtime_error(vm, "Stack overflow.");
          return VM_RUNTIME_ERROR;
        }

        // Save current frame state
        frame->ip = ip;

        // Setup new frame
        frame = &vm->frames[vm->frame_count++];
        frame->function = func;
        frame->ip = func->chunk.code;
        frame->slots = vm->stack_top - arg_count -
                       1; // -1 for function itself local slot 0

        // JIT Compilation - compile hot functions to native code
        func->call_count++;

#if JIT_ENABLED
        // Check if we should JIT compile this function
        if (func->call_count >= JIT_THRESHOLD && !func->jit_code) {
          // Try to compile
          func->jit_code = jit_compile_function(vm, func);
          if (func->jit_code && func->name) {
            // Uncomment for debug: printf("[JIT] Compiled: %s\n",
            // func->name->chars);
          }
        }

        // If JIT code exists and is valid, execute it
        if (func->jit_code && func->jit_code->valid) {
          // Execute JIT code
          JITFunction jit_func = func->jit_code->entry;
          jit_func(vm);

          // After JIT returns, pop the frame
          vm->frame_count--;
          if (vm->frame_count == 0) {
            // Program finished
            return VM_OK;
          }

          // Restore previous frame
          frame = &vm->frames[vm->frame_count - 1];
          ip = frame->ip;
          chunk = &frame->function->chunk;
          DISPATCH();
        }
#endif

        // Update cached regs (interpreter path)
        ip = frame->ip;
        chunk = &func->chunk;
        DISPATCH();
      }

      TARGET(OP_TAIL_CALL) : {
        int arg_count = READ_BYTE();
        VMValue callee = vm_peek(vm, arg_count);

        if (!IS_FUNCTION(callee)) {
          runtime_error(vm, "Can only tail call functions.");
          return VM_RUNTIME_ERROR;
        }

        ObjFunction *func = AS_FUNCTION(callee);
        if (arg_count != func->arity) {
          runtime_error(vm, "Expected %d arguments but got %d.", func->arity,
                        arg_count);
          return VM_RUNTIME_ERROR;
        }

        // Reuse Current Frame (Tail Call Optimization)
        // Copy arguments down to overwrite current frame's locals/slots
        VMValue *dest = frame->slots;
        VMValue *src = vm->stack_top - arg_count - 1;
        int count = arg_count + 1; // Function + Args

        for (int i = 0; i < count; i++) {
          dest[i] = src[i];
        }

        // Reset stack top
        vm->stack_top = frame->slots + count;

        // Update frame state
        frame->function = func;
        frame->ip = func->chunk.code;

        // Hot Path Profiling
        func->call_count++;
        if (func->call_count == 1000) {
          if (func->name) {
            printf("\n[JIT] 🔥 Hot Function Detected: %s (Execution count > "
                   "1000)\n",
                   func->name->chars);
          }
        }

        // Update loop registers
        ip = frame->ip;
        chunk = &func->chunk;
        DISPATCH();
      }

      TARGET(OP_RETURN) : {
        VMValue result = vm_pop(vm);

        vm->frame_count--;
        if (vm->frame_count == 0) {
          vm_pop(vm); // Pop script function
          return VM_OK;
        }

        // Restore previous frame
        vm->stack_top = frame->slots; // Discard locals
        vm_push(vm, result);          // Push result

        frame = &vm->frames[vm->frame_count - 1];
        ip = frame->ip;
        chunk = &frame->function->chunk;
        DISPATCH();
      }
      TARGET(OP_RETURN_VOID) : {
        VMValue result = VM_VOID();

        vm->frame_count--;
        if (vm->frame_count == 0) {
          vm_pop(vm); // Pop script function
          return VM_OK;
        }

        // Restore previous frame
        vm->stack_top = frame->slots; // Discard locals
        vm_push(vm, result);          // Push result

        frame = &vm->frames[vm->frame_count - 1];
        ip = frame->ip;
        chunk = &frame->function->chunk;
        DISPATCH();
      }

      // --- Builtins ---
      TARGET(OP_CALL_BUILTIN) : {
        int builtin_id = READ_BYTE();

        switch (builtin_id) {
        case 0: { // clock()
#ifdef _WIN32
          LARGE_INTEGER frequency;
          LARGE_INTEGER counter;
          QueryPerformanceFrequency(&frequency);
          QueryPerformanceCounter(&counter);
          double ms = (double)counter.QuadPart / frequency.QuadPart * 1000.0;
          vm_push(vm, VM_FLOAT(ms));
#else
          struct timespec ts;
          clock_gettime(CLOCK_MONOTONIC, &ts);
          double ms = (ts.tv_sec * 1000.0) + (ts.tv_nsec / 1000000.0);
          vm_push(vm, VM_FLOAT(ms));
#endif
          break;
        }
        case 1: { // length(obj)
          VMValue val = vm_pop(vm);
          if (IS_STRING(val)) {
            vm_push(vm, VM_INT(AS_STRING(val)->length));
          } else if (IS_ARRAY(val)) {
            vm_push(vm, VM_INT(AS_ARRAY(val)->count));
          } else if (IS_OBJECT(val)) {
            vm_push(vm, VM_INT(AS_OBJECT(val)->count));
          } else {
            vm_push(vm, VM_INT(0));
          }
          break;
        }
        // --- Sockets ---
        case 30: { // socket_server(host, port)
          VMValue portVal = vm_pop(vm);
          VMValue hostVal = vm_pop(vm);
// Init WSA
#ifdef _WIN32
          static int wsa_init = 0;
          if (!wsa_init) {
            WSADATA wsaData;
            WSAStartup(MAKEWORD(2, 2), &wsaData);
            wsa_init = 1;
          }
#endif

          SOCKET server_fd = socket(AF_INET, SOCK_STREAM, 0);
          if (server_fd == INVALID_SOCKET) {
            printf("Socket error: %d\n", WSAGetLastError());
            vm_push(vm, VM_INT(-1));
          } else {
            struct sockaddr_in server_addr;
            memset(&server_addr, 0, sizeof(server_addr)); // Zero init
            server_addr.sin_family = AF_INET;
            // Parse port
            int port = IS_INT(portVal) ? (int)AS_INT(portVal) : 8080;
            server_addr.sin_port = htons(port);
            // Parse host
            if (IS_STRING(hostVal)) {
              server_addr.sin_addr.s_addr =
                  inet_addr(AS_STRING(hostVal)->chars);
            } else {
              server_addr.sin_addr.s_addr = INADDR_ANY;
            }

            if (bind(server_fd, (struct sockaddr *)&server_addr,
                     sizeof(server_addr)) < 0) {
              printf("Bind error: %d\n", WSAGetLastError());
              closesocket(server_fd);
              vm_push(vm, VM_INT(-1));
            } else {
              if (listen(server_fd, 5) < 0) {
                printf("Listen error: %d\n", WSAGetLastError());
                closesocket(server_fd);
                vm_push(vm, VM_INT(-1));
              } else {
                vm_push(vm, VM_INT((long long)server_fd));
              }
            }
          }
          break;
        }
        case 31: { // socket_accept(server_fd)
          VMValue serverVal = vm_pop(vm);
          SOCKET server_fd = (SOCKET)AS_INT(serverVal);
          struct sockaddr_in client_addr;
          socklen_t addr_len = sizeof(client_addr);
          SOCKET client_fd =
              accept(server_fd, (struct sockaddr *)&client_addr, &addr_len);
          if (client_fd == INVALID_SOCKET) {
            vm_push(vm, VM_INT(-1));
          } else {
            vm_push(vm, VM_INT((long long)client_fd));
          }
          break;
        }
        case 32: { // socket_receive(client_fd, size)
          VMValue sizeVal = vm_pop(vm);
          VMValue fdVal = vm_pop(vm);
          int size = IS_INT(sizeVal) ? (int)AS_INT(sizeVal) : 1024;
          SOCKET fd = (SOCKET)AS_INT(fdVal);

          char *buffer = (char *)malloc(size + 1);
          int bytes = recv(fd, buffer, size, 0);
          if (bytes > 0) {
            buffer[bytes] = 0;
            vm_push(vm, VM_OBJ(vm_alloc_string(vm, buffer, bytes)));
          } else {
            vm_push(vm, VM_OBJ(vm_alloc_string(vm, "", 0)));
          }
          free(buffer);
          break;
        }
        case 33: { // socket_send(client_fd, data)
          VMValue dataVal = vm_pop(vm);
          VMValue fdVal = vm_pop(vm);
          SOCKET fd = (SOCKET)AS_INT(fdVal);
          if (IS_STRING(dataVal)) {
            send(fd, AS_STRING(dataVal)->chars, AS_STRING(dataVal)->length, 0);
          }
          vm_push(vm, VM_VOID());
          break;
        }
        case 34: { // socket_close(fd)
          VMValue fdVal = vm_pop(vm);
          closesocket((SOCKET)AS_INT(fdVal));
          vm_push(vm, VM_VOID());
          break;
        }

        // --- Threading (Fake) ---
        case 40: { // thread_create(func_name, arg)
          VMValue argVal = vm_pop(vm);
          VMValue nameVal = vm_pop(vm); // String name

          // To support "fake threading" we need to find the function by name
          // and CALL it. We need global lookup by name string. Since we don't
          // have a name->index map readily available in VM_H without probing
          // compiler structures... Wait! globals array stores KEYS
          // (ObjString*). We can linear scan vm->globals for the name!

          if (!IS_STRING(nameVal)) {
            runtime_error(vm, "thread_create expects function name as string.");
            return VM_RUNTIME_ERROR;
          }
          ObjString *targetName = AS_STRING(nameVal);

          // Linear scan globals
          int found = 0;
          VMValue funcVal = VM_VOID();
          for (int i = 0; i < vm->global_count; i++) {
            // Pointer compare might fail if string was allocated differently
            // (but interning handles it?) If interned, pointer compare is safe.
            // But targetName might comefrom const string.
            // We should use strcmp just in case or ensure interning.
            // vm_alloc_string interns.
            if (vm->globals[i].key == targetName ||
                strcmp(vm->globals[i].key->chars, targetName->chars) == 0) {
              funcVal = vm->globals[i].value;
              found = 1;
              break;
            }
          }

          if (!found || !IS_FUNCTION(funcVal)) {
            runtime_error(vm, "Thread function '%s' not found.",
                          targetName->chars);
            return VM_RUNTIME_ERROR;
          }

          // Call it using standard setup logic
          // We push func, push arg.
          // We can attempt to inline setup or recurse vm_run?
          // Since we are inside vm_run, we can just PUSH FRAME and DISPATCH?
          // But we need to update "ip", "frame", "chunk" local vars.
          // And we need to ensure when it returns, we handle it?
          // If we just PUSH frame, it BECOMES the top frame.
          // Execution loops.
          // When that frame returns (OP_RETURN), it pops frame.
          // Control returns to US?
          // Yes, OP_RETURN checks frame_count. If > 0, it restores previous
          // frame. So if we push, we effectively call it. Correct!

          // Setup call
          ObjFunction *func = AS_FUNCTION(funcVal);
          vm_push(vm, funcVal);
          vm_push(vm, argVal); // Argument 0
          // Call frame setup duplicated (optimize later)
          CallFrame *newFrame = &vm->frames[vm->frame_count++];
          newFrame->function = func;
          newFrame->ip = func->chunk.code;
          newFrame->slots = vm->stack_top - 1 - 1; // Func, Arg

          // Update registers
          frame = newFrame;
          ip = frame->ip;
          chunk = &func->chunk;

          // Jump to start of new function
          DISPATCH(); // Loop continues from new ip.
          break;
        }
        case 41: { // sleep(ms)
          VMValue msVal = vm_pop(vm);
          int ms = IS_INT(msVal) ? (int)AS_INT(msVal) : 0;
#ifdef _WIN32
          Sleep(ms);
#else
          usleep(ms * 1000);
#endif
          vm_push(vm, VM_VOID());
          break;
        }

        // --- Utils ---
        case 50: { // toString(val)
          VMValue v = vm_pop(vm);
          char buf[64];
          if (IS_INT(v))
            snprintf(buf, 64, "%lld", AS_INT(v));
          else if (IS_FLOAT(v))
            snprintf(buf, 64, "%g", AS_FLOAT(v));
          else if (IS_BOOL(v))
            snprintf(buf, 64, "%s", AS_BOOL(v) ? "true" : "false");
          else if (IS_STRING(v)) {
            vm_push(vm, v);
            break;
          } // already string
          else if (IS_VOID(v))
            snprintf(buf, 64, "void");
          else
            snprintf(buf, 64, "[Object]");
          // Allocation
          vm_push(vm, VM_OBJ(vm_alloc_string(vm, buf, strlen(buf))));
          break;
        }
        case 51: { // toJson(val) -> String (recursive, supports nested
                   // objects/arrays)
          VMValue v = vm_pop(vm);
          size_t capacity = 256;
          size_t pos = 0;
          char *buf = malloc(capacity);
          buf[0] = '\0';

          value_to_json(v, &buf, &pos, &capacity);

          vm_push(vm, VM_OBJ(vm_alloc_string(vm, buf, pos)));
          free(buf);
          break;
        }

        case 52: { // fromJson(str) -> Object
          // Stub: Return empty object for now
          // Real implementation requires recursive parser
          // TODO: Implement JSON parser
          ObjObject *obj = vm_allocate_object(vm);
          vm_push(vm, VM_OBJ(obj));
          break;
        }
        case 53: { // toInt(val) -> Int
          VMValue v = vm_pop(vm);
          if (IS_INT(v)) {
            vm_push(vm, v);
          } else if (IS_FLOAT(v)) {
            vm_push(vm, VM_INT((long long)AS_FLOAT(v)));
          } else if (IS_STRING(v)) {
            char *str = AS_STRING(v)->chars;
            long long result = 0;
            // Try to parse as integer
            char *endptr;
            result = strtoll(str, &endptr, 10);
            vm_push(vm, VM_INT(result));
          } else if (IS_BOOL(v)) {
            vm_push(vm, VM_INT(AS_BOOL(v) ? 1 : 0));
          } else {
            vm_push(vm, VM_INT(0));
          }
          break;
        }
        case 54: { // toFloat(val) -> Float
          VMValue v = vm_pop(vm);
          if (IS_FLOAT(v)) {
            vm_push(vm, v);
          } else if (IS_INT(v)) {
            vm_push(vm, VM_FLOAT((double)AS_INT(v)));
          } else if (IS_STRING(v)) {
            char *str = AS_STRING(v)->chars;
            double result = strtod(str, NULL);
            vm_push(vm, VM_FLOAT(result));
          } else if (IS_BOOL(v)) {
            vm_push(vm, VM_FLOAT(AS_BOOL(v) ? 1.0 : 0.0));
          } else {
            vm_push(vm, VM_FLOAT(0.0));
          }
          break;
        }
        case 55: { // toBool(val) -> Bool
          VMValue v = vm_pop(vm);
          if (IS_BOOL(v)) {
            vm_push(vm, v);
          } else if (IS_INT(v)) {
            vm_push(vm, VM_BOOL(AS_INT(v) != 0));
          } else if (IS_FLOAT(v)) {
            vm_push(vm, VM_BOOL(AS_FLOAT(v) != 0.0));
          } else if (IS_STRING(v)) {
            // Empty string is false, non-empty is true
            vm_push(vm, VM_BOOL(AS_STRING(v)->length > 0));
          } else if (IS_VOID(v)) {
            vm_push(vm, VM_BOOL(0));
          } else {
            vm_push(vm, VM_BOOL(1)); // Objects are truthy
          }
          break;
        }
        case 60: { // exit(code)
          VMValue codeVal = vm_pop(vm);
          int code = IS_INT(codeVal) ? (int)AS_INT(codeVal) : 0;
          exit(code);
          break;
        }

        case 61: { // input() -> String (reads line from stdin)
          char buffer[4096];
          if (fgets(buffer, sizeof(buffer), stdin) != NULL) {
            // Remove trailing newline if present
            size_t len = strlen(buffer);
            if (len > 0 && buffer[len - 1] == '\n') {
              buffer[len - 1] = '\0';
              len--;
            }
            // Also remove \r if present (Windows)
            if (len > 0 && buffer[len - 1] == '\r') {
              buffer[len - 1] = '\0';
              len--;
            }
            vm_push(vm, VM_OBJ(vm_alloc_string(vm, buffer, len)));
          } else {
            // EOF or error - return empty string
            vm_push(vm, VM_OBJ(vm_alloc_string(vm, "", 0)));
          }
          break;
        }

        // --- String Utils ---
        case 70: { // split(str, delim)
          VMValue delimVal = vm_pop(vm);
          VMValue strVal = vm_pop(vm);

          ObjArray *arr = vm_allocate_array(vm);
          vm_push(vm, VM_OBJ(arr)); // Keep arr rooted

          if (IS_STRING(strVal) && IS_STRING(delimVal)) {
            char *s = AS_STRING(strVal)->chars;
            char *d = AS_STRING(delimVal)->chars;
            int d_len = strlen(d);

            if (d_len == 0) {
              // Split by char
              for (int i = 0; i < (int)strlen(s); i++) {
                char tmp[2] = {s[i], 0};
                vm_array_push(vm, arr, VM_OBJ(vm_alloc_string(vm, tmp, 1)));
              }
            } else {
              // Split by delimiter
              char *start = s;
              char *found = strstr(start, d);
              while (found) {
                int len = found - start;
                vm_array_push(vm, arr, VM_OBJ(vm_alloc_string(vm, start, len)));
                start = found + d_len;
                found = strstr(start, d);
              }
              // Last part
              vm_array_push(vm, arr,
                            VM_OBJ(vm_alloc_string(vm, start, strlen(start))));
            }
          }
          // Pop arr root? No, leave it as return value.
          break;
        }
        case 71: { // replace(str, old, new)
          VMValue newVal = vm_pop(vm);
          VMValue oldVal = vm_pop(vm);
          VMValue strVal = vm_pop(vm);

          if (IS_STRING(strVal) && IS_STRING(oldVal) && IS_STRING(newVal)) {
            char *s = AS_STRING(strVal)->chars;
            char *old = AS_STRING(oldVal)->chars;
            char *new = AS_STRING(newVal)->chars;

            // Manual replace loop
            // Calculate new size
            int s_len = strlen(s);
            int old_len = strlen(old);
            int new_len = strlen(new);

            if (old_len == 0) {
              vm_push(vm, strVal); // No op
            } else {
              int count = 0;
              char *p = s;
              while ((p = strstr(p, old))) {
                count++;
                p += old_len;
              }

              int new_size = s_len + count * (new_len - old_len);
              char *res = malloc(new_size + 1);
              char *rp = res;
              p = s;
              char *found;
              while ((found = strstr(p, old))) {
                int len = found - p;
                memcpy(rp, p, len);
                rp += len;
                memcpy(rp, new, new_len);
                rp += new_len;
                p = found + old_len;
              }
              strcpy(rp, p); // Copy remainder

              vm_push(vm, VM_OBJ(vm_alloc_string(vm, res, new_size)));
              free(res);
            }
          } else {
            vm_push(vm, VM_VOID());
          }
          break;
        }
        case 72: { // substring(str, start, len) OR (str, start, end)?
          // Tulpar logic usually (start, length) or (start, end)?
          // utils.tpr uses substring(str, 1, length) -> so (start, length)?
          // But router.tpr: substring(full, 0, qmark) -> length? qmark is
          // index. So length = index. substring(full, qmark+1, length(full)).
          // length(full) is END index logic? No, length implies size. But if
          // 3rd arg is usually length. Let's assume (str, start, length). If
          // length(full_path) is passed as 3rd arg, and start is qmark+1. Then
          // len = length(full) - (time)? Wait. router.tpr: substring(req,
          // len(prefix), length(req)). This suggests (str, start, end_index)!
          // Java style?
          // Let's implement (start, end).

          VMValue endVal = vm_pop(vm);
          VMValue startVal = vm_pop(vm);
          VMValue strVal = vm_pop(vm);

          if (IS_STRING(strVal)) {
            int len = AS_STRING(strVal)->length;
            int start = IS_INT(startVal) ? (int)AS_INT(startVal) : 0;
            int end = IS_INT(endVal) ? (int)AS_INT(endVal) : len;

            if (start < 0)
              start = 0;
            if (end > len)
              end = len;
            if (start > end)
              start = end; // Empty

            int sliceLen = end - start;
            char *res = malloc(sliceLen + 1);
            memcpy(res, AS_STRING(strVal)->chars + start, sliceLen);
            res[sliceLen] = 0;

            vm_push(vm, VM_OBJ(vm_alloc_string(vm, res, sliceLen)));
            free(res);
          } else {
            vm_push(vm, VM_OBJ(vm_alloc_string(vm, "", 0)));
          }
          break;
        }
        case 73: { // indexOf(str, substr)
          VMValue subVal = vm_pop(vm);
          VMValue strVal = vm_pop(vm);
          if (IS_STRING(strVal) && IS_STRING(subVal)) {
            char *found =
                strstr(AS_STRING(strVal)->chars, AS_STRING(subVal)->chars);
            if (found) {
              vm_push(vm,
                      VM_INT((long long)(found - AS_STRING(strVal)->chars)));
            } else {
              vm_push(vm, VM_INT(-1));
            }
          } else {
            vm_push(vm, VM_INT(-1));
          }
          break;
        }
        case 74: { // startsWith(str, prefix)
          VMValue preVal = vm_pop(vm);
          VMValue strVal = vm_pop(vm);
          int res = 0;
          if (IS_STRING(strVal) && IS_STRING(preVal)) {
            ObjString *s = AS_STRING(strVal);
            ObjString *p = AS_STRING(preVal);
            if (s->length >= p->length) {
              if (memcmp(s->chars, p->chars, p->length) == 0)
                res = 1;
            }
          }
          vm_push(vm, VM_BOOL(res));
          break;
        }
        case 75: { // endsWith(str, suffix)
          VMValue sufVal = vm_pop(vm);
          VMValue strVal = vm_pop(vm);
          int res = 0;
          if (IS_STRING(strVal) && IS_STRING(sufVal)) {
            ObjString *s = AS_STRING(strVal);
            ObjString *p = AS_STRING(sufVal);
            if (s->length >= p->length) {
              if (memcmp(s->chars + (s->length - p->length), p->chars,
                         p->length) == 0)
                res = 1;
            }
          }
          vm_push(vm, VM_BOOL(res));
          break;
        }

        case 76: { // trim(str) -> removes leading/trailing whitespace
          VMValue strVal = vm_pop(vm);
          if (IS_STRING(strVal)) {
            char *s = AS_STRING(strVal)->chars;
            int len = AS_STRING(strVal)->length;

            // Find start (skip leading whitespace)
            int start = 0;
            while (start < len && (s[start] == ' ' || s[start] == '\t' ||
                                   s[start] == '\n' || s[start] == '\r')) {
              start++;
            }

            // Find end (skip trailing whitespace)
            int end = len;
            while (end > start && (s[end - 1] == ' ' || s[end - 1] == '\t' ||
                                   s[end - 1] == '\n' || s[end - 1] == '\r')) {
              end--;
            }

            int new_len = end - start;
            char *trimmed = malloc(new_len + 1);
            memcpy(trimmed, s + start, new_len);
            trimmed[new_len] = '\0';

            vm_push(vm, VM_OBJ(vm_alloc_string(vm, trimmed, new_len)));
            free(trimmed);
          } else {
            vm_push(vm, VM_OBJ(vm_alloc_string(vm, "", 0)));
          }
          break;
        }

        case 77: { // contains(haystack, needle) -> bool
          VMValue needleVal = vm_pop(vm);
          VMValue haystackVal = vm_pop(vm);
          if (IS_STRING(haystackVal) && IS_STRING(needleVal)) {
            char *haystack = AS_STRING(haystackVal)->chars;
            char *needle = AS_STRING(needleVal)->chars;
            vm_push(vm, VM_BOOL(strstr(haystack, needle) != NULL));
          } else {
            vm_push(vm, VM_BOOL(0));
          }
          break;
        }

        // --- File I/O ---
        case 80: { // write_file(filename, content) -> bool
          VMValue contentVal = vm_pop(vm);
          VMValue filenameVal = vm_pop(vm);
          if (IS_STRING(filenameVal) && IS_STRING(contentVal)) {
            FILE *f = fopen(AS_STRING(filenameVal)->chars, "w");
            if (f) {
              fputs(AS_STRING(contentVal)->chars, f);
              fclose(f);
              vm_push(vm, VM_BOOL(1));
            } else {
              vm_push(vm, VM_BOOL(0));
            }
          } else {
            vm_push(vm, VM_BOOL(0));
          }
          break;
        }

        case 81: { // read_file(filename) -> string
          VMValue filenameVal = vm_pop(vm);
          if (IS_STRING(filenameVal)) {
            FILE *f = fopen(AS_STRING(filenameVal)->chars, "r");
            if (f) {
              fseek(f, 0, SEEK_END);
              long size = ftell(f);
              fseek(f, 0, SEEK_SET);
              char *content = malloc(size + 1);
              size_t read_size = fread(content, 1, size, f);
              content[read_size] = '\0';
              fclose(f);
              vm_push(vm, VM_OBJ(vm_alloc_string(vm, content, read_size)));
              free(content);
            } else {
              vm_push(vm, VM_OBJ(vm_alloc_string(vm, "", 0)));
            }
          } else {
            vm_push(vm, VM_OBJ(vm_alloc_string(vm, "", 0)));
          }
          break;
        }

        case 82: { // append_file(filename, content) -> bool
          VMValue contentVal = vm_pop(vm);
          VMValue filenameVal = vm_pop(vm);
          if (IS_STRING(filenameVal) && IS_STRING(contentVal)) {
            FILE *f = fopen(AS_STRING(filenameVal)->chars, "a");
            if (f) {
              fputs(AS_STRING(contentVal)->chars, f);
              fclose(f);
              vm_push(vm, VM_BOOL(1));
            } else {
              vm_push(vm, VM_BOOL(0));
            }
          } else {
            vm_push(vm, VM_BOOL(0));
          }
          break;
        }

        case 83: { // file_exists(filename) -> bool
          VMValue filenameVal = vm_pop(vm);
          if (IS_STRING(filenameVal)) {
            FILE *f = fopen(AS_STRING(filenameVal)->chars, "r");
            if (f) {
              fclose(f);
              vm_push(vm, VM_BOOL(1));
            } else {
              vm_push(vm, VM_BOOL(0));
            }
          } else {
            vm_push(vm, VM_BOOL(0));
          }
          break;
        }

        // --- Socket I/O ---
        case 90: { // socket_server(host, port) -> fd
          VMValue portVal = vm_pop(vm);
          VMValue hostVal = vm_pop(vm);

          if (!IS_STRING(hostVal) || !IS_INT(portVal)) {
            vm_push(vm, VM_INT(-1));
            break;
          }

#ifdef _WIN32
          // Initialize Winsock
          static int wsa_initialized = 0;
          if (!wsa_initialized) {
            WSADATA wsaData;
            if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
              vm_push(vm, VM_INT(-1));
              break;
            }
            wsa_initialized = 1;
          }
#endif

          int server_fd = socket(AF_INET, SOCK_STREAM, 0);
          if (server_fd < 0) {
            vm_push(vm, VM_INT(-1));
            break;
          }

          // Allow address reuse
          int opt = 1;
          setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, (const char *)&opt,
                     sizeof(opt));

          struct sockaddr_in addr;
          addr.sin_family = AF_INET;
          addr.sin_port = htons((int)AS_INT(portVal));
          addr.sin_addr.s_addr = inet_addr(AS_STRING(hostVal)->chars);

          if (bind(server_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
#ifdef _WIN32
            closesocket(server_fd);
#else
            close(server_fd);
#endif
            vm_push(vm, VM_INT(-1));
            break;
          }

          if (listen(server_fd, 5) < 0) {
#ifdef _WIN32
            closesocket(server_fd);
#else
            close(server_fd);
#endif
            vm_push(vm, VM_INT(-1));
            break;
          }

          vm_push(vm, VM_INT(server_fd));
          break;
        }

        case 91: { // socket_client(host, port) -> fd
          VMValue portVal = vm_pop(vm);
          VMValue hostVal = vm_pop(vm);

          if (!IS_STRING(hostVal) || !IS_INT(portVal)) {
            vm_push(vm, VM_INT(-1));
            break;
          }

#ifdef _WIN32
          static int wsa_initialized = 0;
          if (!wsa_initialized) {
            WSADATA wsaData;
            if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
              vm_push(vm, VM_INT(-1));
              break;
            }
            wsa_initialized = 1;
          }
#endif

          int client_fd = socket(AF_INET, SOCK_STREAM, 0);
          if (client_fd < 0) {
            vm_push(vm, VM_INT(-1));
            break;
          }

          struct sockaddr_in addr;
          addr.sin_family = AF_INET;
          addr.sin_port = htons((int)AS_INT(portVal));
          addr.sin_addr.s_addr = inet_addr(AS_STRING(hostVal)->chars);

          if (connect(client_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
#ifdef _WIN32
            closesocket(client_fd);
#else
            close(client_fd);
#endif
            vm_push(vm, VM_INT(-1));
            break;
          }

          vm_push(vm, VM_INT(client_fd));
          break;
        }

        case 92: { // socket_accept(server_fd) -> client_fd
          VMValue fdVal = vm_pop(vm);
          if (!IS_INT(fdVal)) {
            vm_push(vm, VM_INT(-1));
            break;
          }

          struct sockaddr_in client_addr;
          socklen_t client_len = sizeof(client_addr);
          int client_fd = accept((int)AS_INT(fdVal),
                                 (struct sockaddr *)&client_addr, &client_len);

          vm_push(vm, VM_INT(client_fd));
          break;
        }

        case 93: { // socket_send(fd, data) -> bytes_sent
          VMValue dataVal = vm_pop(vm);
          VMValue fdVal = vm_pop(vm);

          if (!IS_INT(fdVal) || !IS_STRING(dataVal)) {
            vm_push(vm, VM_INT(-1));
            break;
          }

          int sent = send((int)AS_INT(fdVal), AS_STRING(dataVal)->chars,
                          AS_STRING(dataVal)->length, 0);
          vm_push(vm, VM_INT(sent));
          break;
        }

        case 94: { // socket_receive(fd, size) -> string
          VMValue sizeVal = vm_pop(vm);
          VMValue fdVal = vm_pop(vm);

          if (!IS_INT(fdVal) || !IS_INT(sizeVal)) {
            vm_push(vm, VM_OBJ(vm_alloc_string(vm, "", 0)));
            break;
          }

          int size = (int)AS_INT(sizeVal);
          char *buf = malloc(size + 1);
          int received = recv((int)AS_INT(fdVal), buf, size, 0);

          if (received > 0) {
            buf[received] = '\0';
            vm_push(vm, VM_OBJ(vm_alloc_string(vm, buf, received)));
          } else {
            vm_push(vm, VM_OBJ(vm_alloc_string(vm, "", 0)));
          }
          free(buf);
          break;
        }

        case 95: { // socket_close(fd)
          VMValue fdVal = vm_pop(vm);
          if (IS_INT(fdVal)) {
#ifdef _WIN32
            closesocket((int)AS_INT(fdVal));
#else
            close((int)AS_INT(fdVal));
#endif
          }
          vm_push(vm, VM_VOID());
          break;
        }

        case 96: { // socket_select(fds_array, timeout_ms) -> ready_fds_array
          VMValue timeoutVal = vm_pop(vm);
          VMValue fdsVal = vm_pop(vm);

          ObjArray *result = vm_allocate_array(vm);

          if (!IS_OBJ(fdsVal) || AS_OBJ(fdsVal)->type != OBJ_ARRAY ||
              !IS_INT(timeoutVal)) {
            vm_push(vm, VM_OBJ(result));
            break;
          }

          ObjArray *fds = (ObjArray *)AS_OBJ(fdsVal);
          int timeout_ms = (int)AS_INT(timeoutVal);

          fd_set read_fds;
          FD_ZERO(&read_fds);
          int max_fd = 0;

          for (int i = 0; i < fds->count; i++) {
            if (IS_INT(fds->items[i])) {
              int fd = (int)AS_INT(fds->items[i]);
              FD_SET(fd, &read_fds);
              if (fd > max_fd)
                max_fd = fd;
            }
          }

          struct timeval tv;
          tv.tv_sec = timeout_ms / 1000;
          tv.tv_usec = (timeout_ms % 1000) * 1000;

          int ready = select(max_fd + 1, &read_fds, NULL, NULL, &tv);

          if (ready > 0) {
            for (int i = 0; i < fds->count; i++) {
              if (IS_INT(fds->items[i])) {
                int fd = (int)AS_INT(fds->items[i]);
                if (FD_ISSET(fd, &read_fds)) {
                  vm_array_push(vm, result, VM_INT(fd));
                }
              }
            }
          }

          vm_push(vm, VM_OBJ(result));
          break;
        }

        // --- Database (SQLite) ---
        case 100: { // db_open(path) -> db handle (int)
          VMValue pathVal = vm_pop(vm);
          if (!IS_STRING(pathVal)) {
            vm_push(vm, VM_INT(0));
            break;
          }
          sqlite3 *db;
          int rc = sqlite3_open(AS_STRING(pathVal)->chars, &db);
          if (rc != SQLITE_OK) {
            sqlite3_close(db);
            vm_push(vm, VM_INT(0));
          } else {
            vm_push(vm, VM_INT((int64_t)(intptr_t)db));
          }
          break;
        }

        case 101: { // db_close(db)
          VMValue dbVal = vm_pop(vm);
          if (IS_INT(dbVal)) {
            sqlite3 *db = (sqlite3 *)(intptr_t)AS_INT(dbVal);
            if (db) {
              sqlite3_close(db);
            }
          }
          vm_push(vm, VM_VOID());
          break;
        }

        case 102: { // db_query(db, sql) -> array of rows (each row is object)
          VMValue sqlVal = vm_pop(vm);
          VMValue dbVal = vm_pop(vm);

          ObjArray *results = vm_allocate_array(vm);

          if (!IS_INT(dbVal) || !IS_STRING(sqlVal)) {
            vm_push(vm, VM_OBJ(results));
            break;
          }

          sqlite3 *db = (sqlite3 *)(intptr_t)AS_INT(dbVal);
          char *sql = AS_STRING(sqlVal)->chars;

          if (!db) {
            vm_push(vm, VM_OBJ(results));
            break;
          }

          sqlite3_stmt *stmt;
          int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);

          if (rc != SQLITE_OK) {
            // Return empty array on error
            vm_push(vm, VM_OBJ(results));
            break;
          }

          // Execute and collect results
          while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
            ObjObject *row = vm_allocate_object(vm);
            int col_count = sqlite3_column_count(stmt);

            for (int i = 0; i < col_count; i++) {
              const char *col_name = sqlite3_column_name(stmt, i);
              ObjString *key = vm_alloc_string(vm, col_name, strlen(col_name));

              VMValue val;
              int col_type = sqlite3_column_type(stmt, i);

              switch (col_type) {
              case SQLITE_INTEGER:
                val = VM_INT(sqlite3_column_int64(stmt, i));
                break;
              case SQLITE_FLOAT:
                val = VM_FLOAT(sqlite3_column_double(stmt, i));
                break;
              case SQLITE_TEXT: {
                const char *text = (const char *)sqlite3_column_text(stmt, i);
                int len = sqlite3_column_bytes(stmt, i);
                val = VM_OBJ(vm_alloc_string(vm, text, len));
                break;
              }
              case SQLITE_NULL:
              default:
                val = VM_VOID();
                break;
              }

              // Add to row object
              if (row->capacity < row->count + 1) {
                int old_cap = row->capacity;
                row->capacity = old_cap < 8 ? 8 : old_cap * 2;
                row->keys =
                    realloc(row->keys, sizeof(ObjString *) * row->capacity);
                row->values =
                    realloc(row->values, sizeof(VMValue) * row->capacity);
              }
              row->keys[row->count] = key;
              row->values[row->count] = val;
              row->count++;
            }

            vm_array_push(vm, results, VM_OBJ(row));
          }

          sqlite3_finalize(stmt);
          vm_push(vm, VM_OBJ(results));
          break;
        }

        default:
          // Math?
          if (builtin_id < 10 && builtin_id > 1) { // Unknown low ID
            vm_push(vm, VM_VOID());
          } else if (builtin_id >= 10 && builtin_id < 30) {
            // Existing math logic
            VMValue v = vm_pop(vm);
            double d = IS_INT(v) ? (double)AS_INT(v) : AS_FLOAT(v);
            double res = 0;
            switch (builtin_id) {
            case 10:
              res = sqrt(d);
              break;
            case 11:
              res = sin(d);
              break;
            case 12:
              res = cos(d);
              break;
            case 13:
              res = floor(d);
              break;
            case 14:
              res = ceil(d);
              break;
            case 15:
              res = fabs(d);
              break;
            case 16:
              res = tan(d);
              break;
            case 17:
              res = log(d);
              break;
            case 18:
              res = log10(d);
              break;
            case 19:
              res = exp(d);
              break;
            }
            vm_push(vm, VM_FLOAT(res));
          } else {
            vm_push(vm, VM_VOID());
          }
        }
        DISPATCH();
      }

      TARGET(OP_PRINT) : {
        int argc = READ_BYTE();
        for (int i = argc - 1; i >= 0; i--) {
          print_vm_value(vm_peek(vm, i));
          if (i > 0)
            printf(" ");
        }
        printf("\n");
        vm->stack_top -= argc;
        vm_push(vm, VM_VOID()); // Return void
        DISPATCH();
      }

      TARGET(OP_HALT) : { return VM_OK; }

      TARGET(OP_ARRAY_NEW) : {
        // Opcode followed by initial size (not really used if we push manually,
        // assumes empty or populated by PUSH)
        // Actually OP_ARRAY_NEW in compiler didn't take args.
        // Create empty array.
        ObjArray *array = vm_allocate_array(vm);
        vm_push(vm, VM_OBJ(array));
        DISPATCH();
      }
      TARGET(OP_ARRAY_PUSH) : {
        VMValue item = vm_pop(vm);
        VMValue arrVal = vm_peek(vm, 0); // Array stays on stack?
        // Compiler emitted: PUSH arr code... OP_ARRAY_PUSH.
        // If AST_ARRAY_LITERAL: new, [expr, push]*.
        // So arr is at Top-1 (before pop item).
        // But wait, compile_expression(literal) pushes ARRAY.
        // Then loop: expr (push val).
        // Stack: [Arr, Val].
        // OP_ARRAY_PUSH -> pop Val. Peek Arr. Push to Arr.
        // Arr remains on stack.
        // Yes.

        if (!IS_OBJ(arrVal) || AS_OBJ(arrVal)->type != OBJ_ARRAY) {
          printf("DEBUG: OP_ARRAY_PUSH failed. Type: %s, IsObj: %d\n",
                 IS_OBJ(arrVal) ? "OBJ" : "NON-OBJ", IS_OBJ(arrVal));
          if (IS_OBJ(arrVal))
            printf("ObjType: %d\n", AS_OBJ(arrVal)->type);
          runtime_error(vm, "Expected array for push.");
          return VM_RUNTIME_ERROR;
        }
        vm_array_push(vm, (ObjArray *)AS_OBJ(arrVal), item);
        DISPATCH();
      }
      TARGET(OP_ARRAY_GET) : {
        VMValue indexVal = vm_pop(vm);
        VMValue targetVal = vm_pop(vm);

        if (IS_OBJ(targetVal)) {
          if (AS_OBJ(targetVal)->type == OBJ_ARRAY) {
            if (!IS_INT(indexVal)) {
              runtime_error(vm, "Array index must be integer.");
              return VM_RUNTIME_ERROR;
            }
            int index = (int)AS_INT(indexVal);
            ObjArray *arr = (ObjArray *)AS_OBJ(targetVal);
            if (index < 0 || index >= arr->count) {
              runtime_error(vm, "Array index out of bounds.");
              return VM_RUNTIME_ERROR;
            }
            vm_push(vm, arr->items[index]);
            DISPATCH();
          } else if (AS_OBJ(targetVal)->type == OBJ_OBJECT) { // Or generic dict
            // Object get by key
            // indexVal should be string?
            if (!IS_STRING(indexVal)) {
              // Try to stringify? Or error.
              runtime_error(vm, "Object key must be string.");
              return VM_RUNTIME_ERROR;
            }
            ObjObject *obj = (ObjObject *)AS_OBJ(targetVal);
            ObjString *key = AS_STRING(indexVal);
            // Linear scan
            int found = 0;
            for (int i = 0; i < obj->count; i++) {
              if (obj->keys[i] == key ||
                  strcmp(obj->keys[i]->chars, key->chars) == 0) {
                vm_push(vm, obj->values[i]);
                found = 1;
                break;
              }
            }
            if (!found) {
              // Return void for undefined properties
              vm_push(vm, VM_VOID());
            }
            DISPATCH();
          } else if (AS_OBJ(targetVal)->type == OBJ_STRING) {
            // String character access: str[index] -> single char string
            if (!IS_INT(indexVal)) {
              runtime_error(vm, "String index must be integer.");
              return VM_RUNTIME_ERROR;
            }
            int index = (int)AS_INT(indexVal);
            ObjString *str = AS_STRING(targetVal);
            if (index < 0 || index >= str->length) {
              runtime_error(vm, "String index out of bounds.");
              return VM_RUNTIME_ERROR;
            }
            // Return single character as string
            char ch[2] = {str->chars[index], '\0'};
            vm_push(vm, VM_OBJ(vm_alloc_string(vm, ch, 1)));
            DISPATCH();
          }
        }

        runtime_error(vm,
                      "Index access only supported on Arrays/Objects/Strings.");
        return VM_RUNTIME_ERROR;
      }
      TARGET(OP_ARRAY_SET) : {
        VMValue val = vm_pop(vm);
        VMValue indexVal = vm_pop(vm);
        VMValue targetVal = vm_pop(vm);

        if (IS_OBJ(targetVal)) {
          if (AS_OBJ(targetVal)->type == OBJ_ARRAY) {
            if (!IS_INT(indexVal)) {
              runtime_error(vm, "Array index must be integer.");
              return VM_RUNTIME_ERROR;
            }
            int index = (int)AS_INT(indexVal);
            ObjArray *arr = (ObjArray *)AS_OBJ(targetVal);
            if (index < 0 || index >= arr->count) {
              runtime_error(vm, "Array index out of bounds.");
              return VM_RUNTIME_ERROR;
            }
            arr->items[index] = val;
            vm_push(vm, val); // Assignment expression result
            DISPATCH();
          } else if (AS_OBJ(targetVal)->type == OBJ_OBJECT) {
            if (!IS_STRING(indexVal)) {
              runtime_error(vm, "Object key must be string.");
              return VM_RUNTIME_ERROR;
            }
            ObjObject *obj = (ObjObject *)AS_OBJ(targetVal);
            ObjString *key = AS_STRING(indexVal);
            int found = 0;
            for (int i = 0; i < obj->count; i++) {
              if (obj->keys[i] == key ||
                  strcmp(obj->keys[i]->chars, key->chars) == 0) {
                obj->values[i] = val;
                found = 1;
                break;
              }
            }
            if (!found) {
              // Add new key
              // Reuse OP_OBJECT_SET logic or call helper?
              // Inline for now:
              if (obj->capacity < obj->count + 1) {
                int old_capacity = obj->capacity;
                obj->capacity = old_capacity < 8 ? 8 : old_capacity * 2;
                obj->keys = (ObjString **)realloc(
                    obj->keys, sizeof(ObjString *) * obj->capacity);
                obj->values = (VMValue *)realloc(
                    obj->values, sizeof(VMValue) * obj->capacity);
              }
              obj->keys[obj->count] = key;
              obj->values[obj->count] = val;
              obj->count++;
            }
            vm_push(vm, val);
            DISPATCH();
          }
        }
        runtime_error(vm, "Index assignment only supported on Arrays/Objects.");
        return VM_RUNTIME_ERROR;
      }

      TARGET(OP_OBJECT_NEW) : {
        ObjObject *obj = vm_allocate_object(vm);
        vm_push(vm, VM_OBJ(obj));
        DISPATCH();
      }
      TARGET(OP_OBJECT_GET) : {
        VMValue keyVal = vm_pop(vm);
        VMValue objVal = vm_pop(vm);

        if (!IS_OBJ(objVal) || AS_OBJ(objVal)->type != OBJ_OBJECT) {
          runtime_error(vm, "Expected object for property access.");
          return VM_RUNTIME_ERROR;
        }
        if (!IS_STRING(keyVal)) {
          runtime_error(vm, "Object key must be string.");
          return VM_RUNTIME_ERROR;
        }

        ObjObject *obj = (ObjObject *)AS_OBJ(objVal);
        ObjString *key = AS_STRING(keyVal);

        // Linear scan
        VMValue result = VM_VOID();
        int found = 0;
        for (int i = 0; i < obj->count; i++) {
          // Pointer comparison for interned strings is valid!
          if (obj->keys[i] == key ||
              strcmp(obj->keys[i]->chars, key->chars) == 0) {
            result = obj->values[i];
            found = 1;
            break;
          }
        }
        if (!found) {
          // Return void/null? Or error?
          // Usually undefined property -> null/void
          result = VM_VOID();
        }
        vm_push(vm, result);
        DISPATCH();
      }
      TARGET(OP_OBJECT_SET) : {
        VMValue val = vm_pop(vm);
        VMValue keyVal = vm_pop(vm);     // String key pushed by compiler
        VMValue objVal = vm_peek(vm, 0); // Object created/pushed before

        if (!IS_OBJ(objVal) || AS_OBJ(objVal)->type != OBJ_OBJECT) {
          runtime_error(vm, "Expected object for property set.");
          return VM_RUNTIME_ERROR;
        }
        if (!IS_STRING(keyVal)) {
          runtime_error(vm, "Object key must be string.");
          return VM_RUNTIME_ERROR;
        }

        ObjObject *obj = (ObjObject *)AS_OBJ(objVal);
        ObjString *key = AS_STRING(keyVal);

        // Update if exists
        int found = 0;
        for (int i = 0; i < obj->count; i++) {
          if (obj->keys[i] == key ||
              strcmp(obj->keys[i]->chars, key->chars) == 0) {
            obj->values[i] = val;
            found = 1;
            break;
          }
        }

        // Add new
        if (!found) {
          if (obj->capacity < obj->count + 1) {
            int old_capacity = obj->capacity;
            obj->capacity = old_capacity < 8 ? 8 : old_capacity * 2;
            obj->keys = (ObjString **)realloc(obj->keys, sizeof(ObjString *) *
                                                             obj->capacity);
            obj->values = (VMValue *)realloc(obj->values,
                                             sizeof(VMValue) * obj->capacity);
          }
          obj->keys[obj->count] = key;
          obj->values[obj->count] = val;
          obj->count++;
        }

        // vm_peek was used to get obj, so obj remains on stack for subsequent
        // SETs. BUT wait! OP_OBJECT_SET is used in literal:
        //   OP_OBJECT_NEW (pushes obj)
        //   OP_CONST_STR (key)
        //   Compile Val (val)
        //   OP_OBJECT_SET
        // The stack has: [Obj, Key, Val].
        // My implementation: pop Val, pop Key. Peek Obj.
        // Obj remains on stack. Correct.

        // Warning: Does OP_OBJECT_SET pop Val?
        // Yes.
        // Does it push anything? No.
        // So for literal: { k:v, k2:v2 }
        // Stack grows: [Obj] -> [Obj, K, V] -> SET -> [Obj] -> [Obj, K2, V2] ->
        // SET -> [Obj]. Result is [Obj]. Implementation looks correct for
        // Literal.

        DISPATCH();
      }

      TARGET(OP_IMPORT) : {
        uint16_t nameIdx = READ_SHORT();
        // Access chunk from current frame
        CallFrame *frame = &vm->frames[vm->frame_count - 1];
        char *fileName = frame->function->chunk.constants[nameIdx].string_val;

        // 1. Read file
        char *source = read_file(fileName);
        if (source == NULL) {
          printf("Import Error: Could not read file '%s'\n", fileName);
          // Push void to avoid crash if assignment expects result
          vm_push(vm, VM_VOID());
          DISPATCH();
        }

        // 2. Parse
        ASTNode *program = parse_source(source);

        if (program == NULL) {
          printf("Import Error: Failed to parse '%s'\n", fileName);
          free(source);
          vm_push(vm, VM_VOID());
          DISPATCH();
        }

        // 3. Compile
        Chunk *compiledParams = compile(program);
        // Free AST
        // ast_node_free(program); // Need to check if this function is
        // available/needed Assuming compile() doesn't consume AST in a way
        // preventing free.

        if (compiledParams == NULL) {
          printf("Import Error: Failed to compile '%s'\n", fileName);
          free(source);
          vm_push(vm, VM_VOID());
          DISPATCH();
        }

        // 4. Create Function wrapper
        ObjFunction *scriptFunc = vm_new_function(vm);

        // Take ownership of chunk content
        scriptFunc->chunk = *compiledParams;
        // IMPORTANT: compile() allocated the struct, we copied content. Free
        // struct.
        free(compiledParams);

        scriptFunc->name = vm_copy_string(vm, fileName, (int)strlen(fileName));
        scriptFunc->arity = 0;

        // 5. Execute (Call)
        // printf("DEBUG: Executing %s\n", fileName);
        // Push function to stack
        vm_push(vm, VM_OBJ(scriptFunc));

        // Save current frame IP
        frame->ip = ip;

        // Create Frame
        if (vm->frame_count == VM_FRAMES_MAX) {
          runtime_error(vm, "Stack overflow.");
          return VM_RUNTIME_ERROR;
        }

        CallFrame *newFrame = &vm->frames[vm->frame_count++];
        newFrame->function = scriptFunc;
        newFrame->ip = scriptFunc->chunk.code;
        newFrame->slots = vm->stack_top - 1; // Function starts at -1 (arity 0)

        // Update local registers
        frame = newFrame;
        ip = frame->ip;
        chunk = &scriptFunc->chunk;

        free(source);

        DISPATCH();
      }

      TARGET(OP_TRY) : {
        uint16_t offset = READ_SHORT();
        if (frame->handler_count >= 4) {
          runtime_error(vm, "Too many nested try blocks.");
          return VM_RUNTIME_ERROR;
        }
        int idx = frame->handler_count++;
        frame->catch_ips[idx] = ip + offset;
        frame->catch_stacks[idx] = vm->stack_top;
        DISPATCH();
      }

      TARGET(OP_POP_TRY) : {
        if (frame->handler_count > 0) {
          frame->handler_count--;
        }
        DISPATCH();
      }

      TARGET(OP_THROW) : {
        VMValue exception = vm_pop(vm);

        // Unwinding
        while (vm->frame_count > 0) {
          CallFrame *f = &vm->frames[vm->frame_count - 1];

          if (f->handler_count > 0) {
            // Found handler
            int idx = f->handler_count - 1;
            f->handler_count--;

            // Restore frame and stack
            vm->stack_top = f->catch_stacks[idx];
            vm_push(vm, exception);

            // Restore IP local register
            frame = f;
            ip = f->catch_ips[idx];
            chunk = &frame->function->chunk;

            DISPATCH();
          }

          // No handler in this frame, discard frame
          vm->frame_count--;
          if (vm->frame_count == 0) {
            // Unhandled exception
            printf("Unhandled Exception: ");
            print_vm_value(exception);
            printf("\n");
            return VM_RUNTIME_ERROR;
          }

          // Move to next frame (loop continues)
        }
        return VM_RUNTIME_ERROR;
      }

      // ================================================================
      // OPTIMIZED OPCODES - Superinstructions
      // ================================================================

      TARGET(OP_FOR_ITER) : {
        // Optimized for loop: slot(2) + limit_const(2) + body_end_offset(2)
        uint16_t slot = READ_SHORT();
        Constant limit_const = READ_CONSTANT();
        uint16_t end_offset = READ_SHORT();

        int64_t i = AS_INT(frame->slots[slot]);
        int64_t limit = limit_const.int_val;

        if (i < limit) {
          // Continue loop - increment and execute body
          frame->slots[slot] = VM_INT(i + 1);
          // Body follows, will loop back
        } else {
          // Exit loop
          ip += end_offset;
        }
        DISPATCH();
      }

      TARGET(OP_INC_LOCAL) : {
        // Increment local variable in-place
        uint16_t slot = READ_SHORT();
        int64_t val = AS_INT(frame->slots[slot]);
        frame->slots[slot] = VM_INT(val + 1);
        DISPATCH();
      }

      TARGET(OP_DEC_LOCAL) : {
        // Decrement local variable in-place
        uint16_t slot = READ_SHORT();
        int64_t val = AS_INT(frame->slots[slot]);
        frame->slots[slot] = VM_INT(val - 1);
        DISPATCH();
      }

      // ================================================================
      // TYPE-SPECIALIZED OPCODES - No type checking
      // ================================================================

      TARGET(OP_ADD_INT) : {
        // Fast integer add - assumes both operands are integers
        int64_t b = AS_INT(vm_pop(vm));
        int64_t a = AS_INT(vm_pop(vm));
        vm_push(vm, VM_INT(a + b));
        DISPATCH();
      }

      TARGET(OP_SUB_INT) : {
        int64_t b = AS_INT(vm_pop(vm));
        int64_t a = AS_INT(vm_pop(vm));
        vm_push(vm, VM_INT(a - b));
        DISPATCH();
      }

      TARGET(OP_MUL_INT) : {
        int64_t b = AS_INT(vm_pop(vm));
        int64_t a = AS_INT(vm_pop(vm));
        vm_push(vm, VM_INT(a * b));
        DISPATCH();
      }

      TARGET(OP_DIV_INT) : {
        int64_t b = AS_INT(vm_pop(vm));
        int64_t a = AS_INT(vm_pop(vm));
        vm_push(vm, VM_INT(a / b));
        DISPATCH();
      }

      TARGET(OP_LT_INT) : {
        int64_t b = AS_INT(vm_pop(vm));
        int64_t a = AS_INT(vm_pop(vm));
        vm_push(vm, VM_BOOL(a < b));
        DISPATCH();
      }

      TARGET(OP_LE_INT) : {
        int64_t b = AS_INT(vm_pop(vm));
        int64_t a = AS_INT(vm_pop(vm));
        vm_push(vm, VM_BOOL(a <= b));
        DISPATCH();
      }

      TARGET(OP_GT_INT) : {
        int64_t b = AS_INT(vm_pop(vm));
        int64_t a = AS_INT(vm_pop(vm));
        vm_push(vm, VM_BOOL(a > b));
        DISPATCH();
      }

      TARGET(OP_GE_INT) : {
        int64_t b = AS_INT(vm_pop(vm));
        int64_t a = AS_INT(vm_pop(vm));
        vm_push(vm, VM_BOOL(a >= b));
        DISPATCH();
      }

      TARGET(OP_EQ_INT) : {
        int64_t b = AS_INT(vm_pop(vm));
        int64_t a = AS_INT(vm_pop(vm));
        vm_push(vm, VM_BOOL(a == b));
        DISPATCH();
      }

      TARGET(OP_NE_INT) : {
        int64_t b = AS_INT(vm_pop(vm));
        int64_t a = AS_INT(vm_pop(vm));
        vm_push(vm, VM_BOOL(a != b));
        DISPATCH();
      }

      // ================================================================
      // FAST CALL OPCODES - Minimal overhead function calls
      // ================================================================

      TARGET(OP_CALL_FAST) : {
        // Ultra-fast call path - no JIT check, no type validation
        int arg_count = READ_BYTE();
        ObjFunction *func = AS_FUNCTION(vm_peek(vm, arg_count));

        // Save IP and setup new frame inline
        frame->ip = ip;
        frame = &vm->frames[vm->frame_count++];
        frame->function = func;
        frame->ip = func->chunk.code;
        frame->slots = vm->stack_top - arg_count - 1;

        // Update local registers
        ip = frame->ip;
        chunk = &func->chunk;
        DISPATCH();
      }

      TARGET(OP_RETURN_INT) : {
        // Fast return for integer values - uses accumulator
        vm->acc = vm_pop(vm);

        // Pop frame
        vm->frame_count--;
        if (vm->frame_count == 0) {
          return VM_OK;
        }

        // Restore caller frame
        frame = &vm->frames[vm->frame_count - 1];
        vm->stack_top = frame->slots + 1; // Keep function slot
        ip = frame->ip;
        chunk = &frame->function->chunk;

        // Push accumulator to caller's stack
        vm_push(vm, vm->acc);
        DISPATCH();
      }

      // ================================================================
      // ULTRA-FAST CALL OPCODES - Specialized for common arities
      // ================================================================

      TARGET(OP_CALL_0) : {
        // Ultra-fast call with 0 arguments
        ObjFunction *func = AS_FUNCTION(*(vm->stack_top - 1));

        // Save return address
        frame->ip = ip;

        // Setup new frame - minimal operations
        CallFrame *new_frame = &vm->frames[vm->frame_count++];
        new_frame->function = func;
        new_frame->ip = func->chunk.code;
        new_frame->slots = vm->stack_top - 1;
        new_frame->handler_count = 0;

        // Switch to new frame
        frame = new_frame;
        ip = new_frame->ip;
        chunk = &func->chunk;
        DISPATCH();
      }

      TARGET(OP_CALL_1) : {
        // Ultra-fast call with 1 argument (common: fibonacci(n-1))
        ObjFunction *func = AS_FUNCTION(*(vm->stack_top - 2));

        frame->ip = ip;

        CallFrame *new_frame = &vm->frames[vm->frame_count++];
        new_frame->function = func;
        new_frame->ip = func->chunk.code;
        new_frame->slots = vm->stack_top - 2;
        new_frame->handler_count = 0;

        frame = new_frame;
        ip = new_frame->ip;
        chunk = &func->chunk;
        DISPATCH();
      }

      TARGET(OP_CALL_2) : {
        // Ultra-fast call with 2 arguments
        ObjFunction *func = AS_FUNCTION(*(vm->stack_top - 3));

        frame->ip = ip;

        CallFrame *new_frame = &vm->frames[vm->frame_count++];
        new_frame->function = func;
        new_frame->ip = func->chunk.code;
        new_frame->slots = vm->stack_top - 3;
        new_frame->handler_count = 0;

        frame = new_frame;
        ip = new_frame->ip;
        chunk = &func->chunk;
        DISPATCH();
      }

      // INLINE CACHED CALLS - REAL IMPLEMENTATION! 🚀
      TARGET(OP_CALL_1_CACHED) : {
        uint16_t cache_id = READ_SHORT();
        CallSiteCache *cache = &vm->call_caches[cache_id];

        // Peek the function from stack (1 arg means function is at -2)
        VMValue callee = *(vm->stack_top - 2);

        // CACHE HIT PATH (HOT - 99.9% for recursive calls!)
        if (LIKELY(IS_FUNCTION(callee) &&
                   AS_FUNCTION(callee) == cache->cached_function)) {
          // CACHE HIT! Ultra-fast path! ⚡
          cache->cache_hit_count++;

          ObjFunction *func = cache->cached_function;

          // Direct frame setup (no type check, no lookup!)
          frame->ip = ip;
          CallFrame *new_frame = &vm->frames[vm->frame_count++];
          new_frame->function = func;
          new_frame->ip = cache->cached_entry_point; // Cached!
          new_frame->slots = vm->stack_top - 2;
          new_frame->handler_count = 0;

          frame = new_frame;
          ip = cache->cached_entry_point; // Direct jump to cached entry
          chunk = &func->chunk;

// JIT check (cached!)
#if JIT_ENABLED
          if (UNLIKELY(cache->cached_jit && cache->cached_jit->valid)) {
            // JIT code available and cached!
            cache->cached_jit->entry(vm);
            vm->frame_count--;
            DISPATCH();
          }
#endif

          DISPATCH();
        }

        // CACHE MISS PATH (COLD - only first call or polymorphic sites)
        cache->cache_miss_count++;

        // Type check
        if (UNLIKELY(!IS_FUNCTION(callee))) {
          runtime_error(vm, "Can only call functions.");
          return VM_RUNTIME_ERROR;
        }

        ObjFunction *func = AS_FUNCTION(callee);

        // UPDATE CACHE for next call!
        cache->cached_function = func;
        cache->cached_entry_point = func->chunk.code;
#if JIT_ENABLED
        cache->cached_jit = func->jit_code;
#else
        cache->cached_jit = NULL;
#endif

        // Execute call (normal path)
        frame->ip = ip;
        CallFrame *new_frame = &vm->frames[vm->frame_count++];
        new_frame->function = func;
        new_frame->ip = func->chunk.code;
        new_frame->slots = vm->stack_top - 2;
        new_frame->handler_count = 0;

        frame = new_frame;
        ip = new_frame->ip;
        chunk = &func->chunk;

// JIT check
#if JIT_ENABLED
        if (func->jit_code && func->jit_code->valid) {
          func->jit_code->entry(vm);
          vm->frame_count--;
          DISPATCH();
        }
#endif

        DISPATCH();
      }

      TARGET(OP_CALL_2_CACHED) : {
        uint16_t cache_id = READ_SHORT();
        CallSiteCache *cache = &vm->call_caches[cache_id];

        VMValue callee = *(vm->stack_top - 3); // 2 args means function at -3

        // CACHE HIT
        if (LIKELY(IS_FUNCTION(callee) &&
                   AS_FUNCTION(callee) == cache->cached_function)) {
          cache->cache_hit_count++;

          ObjFunction *func = cache->cached_function;
          frame->ip = ip;
          CallFrame *new_frame = &vm->frames[vm->frame_count++];
          new_frame->function = func;
          new_frame->ip = cache->cached_entry_point;
          new_frame->slots = vm->stack_top - 3;
          new_frame->handler_count = 0;

          frame = new_frame;
          ip = cache->cached_entry_point;
          chunk = &func->chunk;

#if JIT_ENABLED
          if (UNLIKELY(cache->cached_jit && cache->cached_jit->valid)) {
            cache->cached_jit->entry(vm);
            vm->frame_count--;
            DISPATCH();
          }
#endif

          DISPATCH();
        }

        // CACHE MISS
        cache->cache_miss_count++;

        if (UNLIKELY(!IS_FUNCTION(callee))) {
          runtime_error(vm, "Can only call functions.");
          return VM_RUNTIME_ERROR;
        }

        ObjFunction *func = AS_FUNCTION(callee);

        // Update cache
        cache->cached_function = func;
        cache->cached_entry_point = func->chunk.code;
#if JIT_ENABLED
        cache->cached_jit = func->jit_code;
#else
        cache->cached_jit = NULL;
#endif

        frame->ip = ip;
        CallFrame *new_frame = &vm->frames[vm->frame_count++];
        new_frame->function = func;
        new_frame->ip = func->chunk.code;
        new_frame->slots = vm->stack_top - 3;
        new_frame->handler_count = 0;

        frame = new_frame;
        ip = new_frame->ip;
        chunk = &func->chunk;

#if JIT_ENABLED
        if (func->jit_code && func->jit_code->valid) {
          func->jit_code->entry(vm);
          vm->frame_count--;
          DISPATCH();
        }
#endif

        DISPATCH();
      }

      TARGET(OP_RETURN_FAST) : {
        // Ultra-fast return
        VMValue result = vm_pop(vm); // Get return value

        // Discard callee's locals - reset to callee's slots base
        vm->stack_top = frame->slots;

        // Push result to caller's stack
        vm_push(vm, result);

        // Pop frame
        vm->frame_count--;
        if (UNLIKELY(vm->frame_count == 0)) {
          return VM_OK;
        }

        // Restore caller frame
        frame = &vm->frames[vm->frame_count - 1];
        ip = frame->ip;
        chunk = &frame->function->chunk;
        DISPATCH();
      }

      // ================================================================
      // SUPERINSTRUCTIONS - Combined common patterns
      // ================================================================

      TARGET(OP_LOAD_LOCAL_ADD) : {
        // Common pattern: push local + add to TOS
        uint16_t slot = READ_SHORT();
        VMValue local = frame->slots[slot];
        VMValue *sp = vm->stack_top - 1;

        if (LIKELY(IS_INT(local) && IS_INT(*sp))) {
          sp[0] = VM_INT(AS_INT(*sp) + AS_INT(local));
        } else {
          // Fallback to generic
          vm_push(vm, local);
          BINARY_OP(+);
        }
        DISPATCH();
      }

      TARGET(OP_LOAD_LOCAL_SUB) : {
        uint16_t slot = READ_SHORT();
        VMValue local = frame->slots[slot];
        VMValue *sp = vm->stack_top - 1;

        if (LIKELY(IS_INT(local) && IS_INT(*sp))) {
          sp[0] = VM_INT(AS_INT(*sp) - AS_INT(local));
        } else {
          vm_push(vm, local);
          BINARY_OP(-);
        }
        DISPATCH();
      }

      TARGET(OP_LOAD_LOCAL_LT) : {
        // Pattern: i < limit (used in for loops)
        uint16_t slot = READ_SHORT();
        VMValue local = frame->slots[slot];
        VMValue *sp = vm->stack_top - 1;

        if (LIKELY(IS_INT(local) && IS_INT(*sp))) {
          sp[0] = VM_BOOL(AS_INT(*sp) < AS_INT(local));
        } else {
          vm_push(vm, local);
          COMPARE_OP(<);
        }
        DISPATCH();
      }

      TARGET(OP_DEC_JUMP_NZ) : {
        // Countdown loop: decrement local, jump if not zero
        uint16_t slot = READ_SHORT();
        int16_t offset = (int16_t)READ_SHORT();

        int64_t val = AS_INT(frame->slots[slot]);
        val--;
        frame->slots[slot] = VM_INT(val);

        if (val != 0) {
          ip += offset;
        }
        DISPATCH();
      }

      TARGET(OP_LOAD_ADD_STORE) : {
        // Pattern: sum = sum + x (x is on stack, sum is local)
        uint16_t slot = READ_SHORT();
        VMValue x = vm_pop(vm);
        VMValue sum = frame->slots[slot];

        if (LIKELY(IS_INT(sum) && IS_INT(x))) {
          VMValue result = VM_INT(AS_INT(sum) + AS_INT(x));
          frame->slots[slot] = result;
          vm_push(vm, result);
        } else {
          // Fallback
          vm_push(vm, sum);
          vm_push(vm, x);
          BINARY_OP(+);
          // Result is on stack. Copy to slot.
          frame->slots[slot] = vm_peek(vm, 0);
          // Result stays on stack. Correct.
        }
        DISPATCH();
      }

      TARGET(OP_INC_LOCAL_FAST) : {
        // Ultra-fast increment: var = var + 1 (assumes int)
        uint16_t slot = READ_SHORT();
        frame->slots[slot].as.int_val++;
        vm_push(vm, frame->slots[slot]);
        DISPATCH();
      }

      // ================================================================
      // REGISTER-BASED OPCODES - Direct register-to-register operations
      // Format: OP dst, src1, src2 (each is a local slot index)
      // ================================================================

      TARGET(OP_ADD_RRR) : {
        // dst = src1 + src2 (all registers/slots)
        uint8_t dst = READ_BYTE();
        uint8_t src1 = READ_BYTE();
        uint8_t src2 = READ_BYTE();
        frame->slots[dst] =
            VM_INT(AS_INT(frame->slots[src1]) + AS_INT(frame->slots[src2]));
        DISPATCH();
      }

      TARGET(OP_SUB_RRR) : {
        uint8_t dst = READ_BYTE();
        uint8_t src1 = READ_BYTE();
        uint8_t src2 = READ_BYTE();
        frame->slots[dst] =
            VM_INT(AS_INT(frame->slots[src1]) - AS_INT(frame->slots[src2]));
        DISPATCH();
      }

      TARGET(OP_MUL_RRR) : {
        uint8_t dst = READ_BYTE();
        uint8_t src1 = READ_BYTE();
        uint8_t src2 = READ_BYTE();
        frame->slots[dst] =
            VM_INT(AS_INT(frame->slots[src1]) * AS_INT(frame->slots[src2]));
        DISPATCH();
      }

      TARGET(OP_DIV_RRR) : {
        uint8_t dst = READ_BYTE();
        uint8_t src1 = READ_BYTE();
        uint8_t src2 = READ_BYTE();
        frame->slots[dst] =
            VM_INT(AS_INT(frame->slots[src1]) / AS_INT(frame->slots[src2]));
        DISPATCH();
      }

      TARGET(OP_LT_RRR) : {
        uint8_t dst = READ_BYTE();
        uint8_t src1 = READ_BYTE();
        uint8_t src2 = READ_BYTE();
        frame->slots[dst] =
            VM_BOOL(AS_INT(frame->slots[src1]) < AS_INT(frame->slots[src2]));
        DISPATCH();
      }

      TARGET(OP_LE_RRR) : {
        uint8_t dst = READ_BYTE();
        uint8_t src1 = READ_BYTE();
        uint8_t src2 = READ_BYTE();
        frame->slots[dst] =
            VM_BOOL(AS_INT(frame->slots[src1]) <= AS_INT(frame->slots[src2]));
        DISPATCH();
      }

      TARGET(OP_GT_RRR) : {
        uint8_t dst = READ_BYTE();
        uint8_t src1 = READ_BYTE();
        uint8_t src2 = READ_BYTE();
        frame->slots[dst] =
            VM_BOOL(AS_INT(frame->slots[src1]) > AS_INT(frame->slots[src2]));
        DISPATCH();
      }

      TARGET(OP_GE_RRR) : {
        uint8_t dst = READ_BYTE();
        uint8_t src1 = READ_BYTE();
        uint8_t src2 = READ_BYTE();
        frame->slots[dst] =
            VM_BOOL(AS_INT(frame->slots[src1]) >= AS_INT(frame->slots[src2]));
        DISPATCH();
      }

      TARGET(OP_EQ_RRR) : {
        uint8_t dst = READ_BYTE();
        uint8_t src1 = READ_BYTE();
        uint8_t src2 = READ_BYTE();
        frame->slots[dst] =
            VM_BOOL(AS_INT(frame->slots[src1]) == AS_INT(frame->slots[src2]));
        DISPATCH();
      }

      TARGET(OP_NE_RRR) : {
        uint8_t dst = READ_BYTE();
        uint8_t src1 = READ_BYTE();
        uint8_t src2 = READ_BYTE();
        frame->slots[dst] =
            VM_BOOL(AS_INT(frame->slots[src1]) != AS_INT(frame->slots[src2]));
        DISPATCH();
      }

      TARGET(OP_MOV_RR) : {
        // dst = src (register copy)
        uint8_t dst = READ_BYTE();
        uint8_t src = READ_BYTE();
        frame->slots[dst] = frame->slots[src];
        DISPATCH();
      }

      TARGET(OP_MOV_RI) : {
        // dst = immediate (load 16-bit immediate to register)
        uint8_t dst = READ_BYTE();
        int16_t imm = (int16_t)READ_SHORT();
        frame->slots[dst] = VM_INT(imm);
        DISPATCH();
      }

      TARGET(OP_ADD_RI) : {
        // dst = dst + immediate
        uint8_t dst = READ_BYTE();
        int16_t imm = (int16_t)READ_SHORT();
        frame->slots[dst].as.int_val += imm;
        DISPATCH();
      }

      TARGET(OP_SUB_RI) : {
        // Stack version: TOS = TOS - immediate (for fibonacci n-1, n-2 pattern)
        READ_BYTE(); // Skip dst byte (not used in stack mode)
        int16_t imm = (int16_t)READ_SHORT();
        vm->stack_top[-1].as.int_val -= imm; // Modify top of stack directly
        DISPATCH();
      }

      TARGET(OP_LT_RI) : {
        // tmp = dst < immediate (result in temp register, pushes to stack)
        uint8_t reg = READ_BYTE();
        int16_t imm = (int16_t)READ_SHORT();
        vm_push(vm, VM_BOOL(AS_INT(frame->slots[reg]) < imm));
        DISPATCH();
      }

#ifndef __GNUC__
    default:
      runtime_error(vm, "Unknown opcode %d", instruction);
      return VM_RUNTIME_ERROR;
#endif
      return VM_RUNTIME_ERROR;
    }
  }
}

// ============================================================================
// ARRAY / OBJECT FUNCTIONS
// ============================================================================

ObjArray *vm_allocate_array(VM *vm) {
  ObjArray *array =
      (ObjArray *)allocate_object(vm, sizeof(ObjArray), OBJ_ARRAY);
  array->count = 0;
  array->capacity = 0;
  array->items = NULL;
  return array;
}

void vm_array_push(VM *vm, ObjArray *array, VMValue value) {
  (void)vm; // Unused for now (unless we track memory/GC)
  if (array->capacity < array->count + 1) {
    int old_capacity = array->capacity;
    array->capacity = old_capacity < 8 ? 8 : old_capacity * 2;
    array->items =
        (VMValue *)realloc(array->items, sizeof(VMValue) * array->capacity);
  }
  array->items[array->count] = value;
  array->count++;
}

ObjObject *vm_allocate_object(VM *vm) {
  ObjObject *obj =
      (ObjObject *)allocate_object(vm, sizeof(ObjObject), OBJ_OBJECT);
  obj->count = 0;
  obj->capacity = 0;
  obj->keys = NULL;
  obj->values = NULL;
  return obj;
}

// ============================================================================
// JIT HELPER FUNCTIONS
// ============================================================================

// Helper called from JIT code to execute a function call via interpreter
// Stack layout on entry: [callee, arg0, arg1, ..., argN-1]
// On return: [result]
void jit_helper_call(VM *vm, int arg_count) {
  // Get callee from stack
  VMValue callee = vm_peek(vm, arg_count);

  if (!IS_FUNCTION(callee)) {
    printf("JIT Error: Can only call functions\n");
    vm_push(vm, VM_VOID());
    return;
  }

  ObjFunction *func = AS_FUNCTION(callee);

  // Check arity
  if (arg_count != func->arity) {
    printf("JIT Error: Expected %d args but got %d\n", func->arity, arg_count);
    vm_push(vm, VM_VOID());
    return;
  }

  // Check for stack overflow
  if (vm->frame_count >= VM_FRAMES_MAX) {
    printf("JIT Error: Stack overflow\n");
    vm_push(vm, VM_VOID());
    return;
  }

  // Increment call count for JIT profiling
  func->call_count++;

  // Check if function has JIT code - if so, use it!
#if JIT_ENABLED
  if (func->jit_code && func->jit_code->valid) {
    // Setup frame for JIT execution
    CallFrame *frame = &vm->frames[vm->frame_count++];
    frame->function = func;
    frame->ip = func->chunk.code;
    frame->slots = vm->stack_top - arg_count - 1;
    frame->handler_count = 0;

    // Execute JIT code directly!
    JITFunction jit_func = func->jit_code->entry;
    jit_func(vm);

    // JIT code handles return, stack cleanup
    // Pop frame and push result
    vm->frame_count--;
    // Result should already be on stack from JIT epilogue
    return;
  }

  // Try to JIT compile if hot enough
  if (func->call_count >= JIT_THRESHOLD && !func->jit_code) {
    func->jit_code = jit_compile_function(vm, func);
    if (func->jit_code && func->jit_code->valid) {
      // Compiled! Use JIT for this call
      CallFrame *frame = &vm->frames[vm->frame_count++];
      frame->function = func;
      frame->ip = func->chunk.code;
      frame->slots = vm->stack_top - arg_count - 1;
      frame->handler_count = 0;

      JITFunction jit_func = func->jit_code->entry;
      jit_func(vm);

      vm->frame_count--;
      return;
    }
  }
#endif

  // Fallback: Setup new call frame for interpreter
  CallFrame *frame = &vm->frames[vm->frame_count++];
  frame->function = func;
  frame->ip = func->chunk.code;
  frame->slots = vm->stack_top - arg_count - 1;
  frame->handler_count = 0;

  // Run interpreter for this function
  // We need a mini-interpreter loop here
  register uint8_t *ip = frame->ip;
  Chunk *chunk = &func->chunk;

#define READ_BYTE() (*ip++)
#define READ_SHORT() (ip += 2, (uint16_t)((ip[-2]) | (ip[-1] << 8)))
#define READ_CONSTANT() (chunk->constants[READ_SHORT()])

  for (;;) {
    uint8_t instruction = READ_BYTE();

    switch (instruction) {
    case OP_CONST_INT: {
      Constant c = READ_CONSTANT();
      vm_push(vm, VM_INT(c.int_val));
      break;
    }

    case OP_CONST_TRUE:
      vm_push(vm, VM_BOOL(1));
      break;

    case OP_CONST_FALSE:
      vm_push(vm, VM_BOOL(0));
      break;

    case OP_CONST_VOID:
      vm_push(vm, VM_VOID());
      break;

    case OP_POP:
      vm_pop(vm);
      break;

    case OP_ADD: {
      VMValue b = vm_pop(vm);
      VMValue a = vm_pop(vm);
      if (IS_INT(a) && IS_INT(b)) {
        vm_push(vm, VM_INT(AS_INT(a) + AS_INT(b)));
      } else {
        vm_push(vm, VM_FLOAT(AS_FLOAT(a) + AS_FLOAT(b)));
      }
      break;
    }

    case OP_SUB: {
      VMValue b = vm_pop(vm);
      VMValue a = vm_pop(vm);
      if (IS_INT(a) && IS_INT(b)) {
        vm_push(vm, VM_INT(AS_INT(a) - AS_INT(b)));
      } else {
        vm_push(vm, VM_FLOAT(AS_FLOAT(a) - AS_FLOAT(b)));
      }
      break;
    }

    case OP_MUL: {
      VMValue b = vm_pop(vm);
      VMValue a = vm_pop(vm);
      if (IS_INT(a) && IS_INT(b)) {
        vm_push(vm, VM_INT(AS_INT(a) * AS_INT(b)));
      } else {
        vm_push(vm, VM_FLOAT(AS_FLOAT(a) * AS_FLOAT(b)));
      }
      break;
    }

    case OP_LT: {
      VMValue b = vm_pop(vm);
      VMValue a = vm_pop(vm);
      vm_push(vm, VM_BOOL(AS_INT(a) < AS_INT(b)));
      break;
    }

    case OP_LE: {
      VMValue b = vm_pop(vm);
      VMValue a = vm_pop(vm);
      vm_push(vm, VM_BOOL(AS_INT(a) <= AS_INT(b)));
      break;
    }

    case OP_GT: {
      VMValue b = vm_pop(vm);
      VMValue a = vm_pop(vm);
      vm_push(vm, VM_BOOL(AS_INT(a) > AS_INT(b)));
      break;
    }

    case OP_GE: {
      VMValue b = vm_pop(vm);
      VMValue a = vm_pop(vm);
      vm_push(vm, VM_BOOL(AS_INT(a) >= AS_INT(b)));
      break;
    }

    case OP_EQ: {
      VMValue b = vm_pop(vm);
      VMValue a = vm_pop(vm);
      vm_push(vm, VM_BOOL(AS_INT(a) == AS_INT(b)));
      break;
    }

    case OP_NE: {
      VMValue b = vm_pop(vm);
      VMValue a = vm_pop(vm);
      vm_push(vm, VM_BOOL(AS_INT(a) != AS_INT(b)));
      break;
    }

    case OP_LOAD_LOCAL: {
      uint16_t slot = READ_SHORT();
      vm_push(vm, frame->slots[slot]);
      break;
    }

    case OP_STORE_LOCAL: {
      uint16_t slot = READ_SHORT();
      frame->slots[slot] = vm_peek(vm, 0);
      break;
    }

    case OP_JUMP: {
      uint16_t offset = READ_SHORT();
      ip += offset;
      break;
    }

    case OP_JUMP_IF_FALSE: {
      uint16_t offset = READ_SHORT();
      if (!AS_BOOL(vm_peek(vm, 0))) {
        ip += offset;
      }
      break;
    }

    case OP_LOOP: {
      uint16_t offset = READ_SHORT();
      ip -= offset;
      break;
    }

    case OP_CALL:
    case OP_CALL_FAST:
    case OP_CALL_1: {
      int call_arg_count =
          (instruction == OP_CALL || instruction == OP_CALL_FAST) ? READ_BYTE()
                                                                  : 1;
      // Recursive call - use jit_helper_call
      jit_helper_call(vm, call_arg_count);
      break;
    }

    case OP_RETURN:
    case OP_RETURN_FAST: {
      VMValue result = vm_pop(vm);
      vm->frame_count--;
      vm->stack_top = frame->slots;
      vm_push(vm, result);
      return; // Exit mini-interpreter
    }

    case OP_RETURN_VOID: {
      vm->frame_count--;
      vm->stack_top = frame->slots;
      vm_push(vm, VM_VOID());
      return;
    }

    default:
      printf("JIT Helper: Unsupported opcode %d\n", instruction);
      vm->frame_count--;
      vm->stack_top = frame->slots;
      vm_push(vm, VM_VOID());
      return;
    }
  }

#undef READ_BYTE
#undef READ_SHORT
#undef READ_CONSTANT
}

// Simplified interpreter call for JIT - runs a function and returns result
VMValue jit_interpreter_call(VM *vm, ObjFunction *func, VMValue *args,
                             int arg_count) {
  // Push function and args to stack
  vm_push(vm, VM_OBJ(func));
  for (int i = 0; i < arg_count; i++) {
    vm_push(vm, args[i]);
  }

  // Call helper
  jit_helper_call(vm, arg_count);

  // Pop and return result
  return vm_pop(vm);
}
