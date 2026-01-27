#include "../../runtime/cJSON.h"
#include "../lexer/lexer.h"
#include "vm.h"

#define TYPE_BOOL_BOOL ((VM_VAL_BOOL << 4) | VM_VAL_BOOL)
#include <ctype.h>
#include <setjmp.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Exception Handling Globals (Removed duplicates)

// ============================================================
// AOT STRING ARENA - Separate high-performance allocator for AOT mode
// Pre-allocates large blocks, avoids malloc overhead
// ============================================================

#define AOT_ARENA_BLOCK_SIZE (1024 * 1024) // 1MB blocks
#define AOT_ARENA_ALIGNMENT 8

typedef struct AOTArenaBlock {
  char *memory;
  size_t size;
  size_t used;
  struct AOTArenaBlock *next;
} AOTArenaBlock;

typedef struct {
  AOTArenaBlock *current;
  AOTArenaBlock *head;
  size_t total_allocated;
} AOTArena;

// Global arena for AOT string allocations
static AOTArena *g_aot_string_arena = NULL;

static AOTArenaBlock *aot_arena_new_block(size_t min_size) {
  size_t size =
      min_size > AOT_ARENA_BLOCK_SIZE ? min_size : AOT_ARENA_BLOCK_SIZE;
  AOTArenaBlock *block = (AOTArenaBlock *)malloc(sizeof(AOTArenaBlock));
  if (!block)
    return NULL;

  block->memory = (char *)malloc(size);
  if (!block->memory) {
    free(block);
    return NULL;
  }
  block->size = size;
  block->used = 0;
  block->next = NULL;
  return block;
}

static void aot_arena_init(void) {
  if (g_aot_string_arena)
    return;

  g_aot_string_arena = (AOTArena *)malloc(sizeof(AOTArena));
  if (!g_aot_string_arena)
    return;

  g_aot_string_arena->head = aot_arena_new_block(AOT_ARENA_BLOCK_SIZE);
  g_aot_string_arena->current = g_aot_string_arena->head;
  g_aot_string_arena->total_allocated = 0;
}

// Fast arena allocation - no free needed until arena reset
static void *aot_arena_alloc(size_t size) {
  if (!g_aot_string_arena)
    aot_arena_init();
  if (!g_aot_string_arena || !g_aot_string_arena->current)
    return malloc(size);

  // Align size
  size = (size + AOT_ARENA_ALIGNMENT - 1) & ~(AOT_ARENA_ALIGNMENT - 1);

  AOTArenaBlock *block = g_aot_string_arena->current;

  // Check if current block has space
  if (block->used + size > block->size) {
    // Need new block
    AOTArenaBlock *new_block = aot_arena_new_block(size);
    if (!new_block)
      return malloc(size); // Fallback

    block->next = new_block;
    g_aot_string_arena->current = new_block;
    block = new_block;
  }

  void *ptr = block->memory + block->used;
  block->used += size;
  g_aot_string_arena->total_allocated += size;

  return ptr;
}

// Reset arena (reuse memory without freeing)
void aot_arena_reset(void) {
  if (!g_aot_string_arena)
    return;

  // Just reset used counters, keep memory
  AOTArenaBlock *block = g_aot_string_arena->head;
  while (block) {
    block->used = 0;
    block = block->next;
  }
  g_aot_string_arena->current = g_aot_string_arena->head;
  g_aot_string_arena->total_allocated = 0;
}

// Free all arena memory
void aot_arena_destroy(void) {
  if (!g_aot_string_arena)
    return;

  AOTArenaBlock *block = g_aot_string_arena->head;
  while (block) {
    AOTArenaBlock *next = block->next;
    free(block->memory);
    free(block);
    block = next;
  }
  free(g_aot_string_arena);
  g_aot_string_arena = NULL;
}

// ============================================================
// FAST STRING ALLOCATION using AOT Arena
// ============================================================

// Helper for AOT string allocation (no GC) - NOW USES ARENA
static ObjString *aot_allocate_string(const char *chars, int length) {
  // Single arena allocation for struct + chars together (cache friendly)
  size_t total_size = sizeof(ObjString) + length + 1;
  char *block = (char *)aot_arena_alloc(total_size);
  if (!block)
    return NULL;

  ObjString *str = (ObjString *)block;
  str->obj.type = OBJ_STRING;
  str->obj.arena_allocated = 1; // Mark as arena allocated
  str->obj.next = NULL;
  str->length = length;

  // Chars immediately follow struct
  str->chars = block + sizeof(ObjString);
  memcpy(str->chars, chars, length);
  str->chars[length] = '\0';

  // Lazy hash - compute only when needed
  str->hash = 0;

  return str;
}

// Wrapper for AOT string literals (matches vm_alloc_string signature)
ObjString *vm_alloc_string_aot(void *vm, const char *chars, int length) {
  return aot_allocate_string(chars, length);
}

// Wrapper for AOT print (takes pointer to VMValue, calls vm_print_value which
// takes value)
#include <stddef.h>
void aot_print_value(VMValue *v) { vm_print_value(*v); }

// ============================================================
// FAST STRING CONCATENATION (for AOT optimization)
// Uses Arena allocator for O(1) allocation overhead
// ============================================================

// Fast string append - ultra-optimized version with Arena
VMValue aot_string_concat_fast(VMValue a, VMValue b) {
  if (!IS_STRING(a) || !IS_STRING(b)) {
    return VM_INT(0);
  }

  ObjString *s1 = AS_STRING(a);
  ObjString *s2 = AS_STRING(b);

  int len1 = s1->length;
  int len2 = s2->length;
  int total_len = len1 + len2;

  // Single AOT ARENA allocation for both struct and chars
  size_t alloc_size = sizeof(ObjString) + total_len + 1;
  char *block = (char *)aot_arena_alloc(alloc_size);
  if (!block)
    return VM_INT(0);

  ObjString *result = (ObjString *)block;
  result->obj.type = OBJ_STRING;
  result->obj.arena_allocated = 1;
  result->obj.next = NULL;
  result->length = total_len;
  result->chars = block + sizeof(ObjString);

  // Direct memory copy
  memcpy(result->chars, s1->chars, len1);
  memcpy(result->chars + len1, s2->chars, len2);
  result->chars[total_len] = '\0';

  // Lazy hash
  result->hash = 0;

  return VM_OBJ((Obj *)result);
}

// Create string from C string literal (fast path)
VMValue aot_string_from_cstr(const char *cstr) {
  int len = strlen(cstr);
  ObjString *str = aot_allocate_string(cstr, len);
  return VM_OBJ((Obj *)str);
}

// Pre-allocated string builder for loops
typedef struct {
  char *buffer;
  int length;
  int capacity;
} StringBuilder;

StringBuilder *aot_stringbuilder_new(int initial_capacity) {
  StringBuilder *sb = (StringBuilder *)malloc(sizeof(StringBuilder));
  sb->capacity = initial_capacity > 64 ? initial_capacity : 64;
  sb->buffer = (char *)malloc(sb->capacity);
  sb->buffer[0] = '\0';
  sb->length = 0;
  return sb;
}

void aot_stringbuilder_append(StringBuilder *sb, const char *str, int len) {
  if (sb->length + len >= sb->capacity) {
    // Double capacity
    sb->capacity = (sb->length + len + 1) * 2;
    sb->buffer = (char *)realloc(sb->buffer, sb->capacity);
  }
  memcpy(sb->buffer + sb->length, str, len);
  sb->length += len;
  sb->buffer[sb->length] = '\0';
}

// Append VMValue to StringBuilder (handles any type)
void aot_stringbuilder_append_vmvalue(StringBuilder *sb, VMValue val) {
  if (IS_STRING(val)) {
    ObjString *str = AS_STRING(val);
    aot_stringbuilder_append(sb, str->chars, str->length);
  } else if (IS_INT(val)) {
    char buf[32];
    int len = snprintf(buf, sizeof(buf), "%lld", (long long)AS_INT(val));
    aot_stringbuilder_append(sb, buf, len);
  } else if (IS_FLOAT(val)) {
    char buf[64];
    int len = snprintf(buf, sizeof(buf), "%g", AS_FLOAT(val));
    aot_stringbuilder_append(sb, buf, len);
  } else if (IS_BOOL(val)) {
    const char *s = AS_BOOL(val) ? "true" : "false";
    aot_stringbuilder_append(sb, s, strlen(s));
  }
}

VMValue aot_stringbuilder_to_string(StringBuilder *sb) {
  ObjString *str = aot_allocate_string(sb->buffer, sb->length);
  return VM_OBJ((Obj *)str);
}

void aot_stringbuilder_free(StringBuilder *sb) {
  free(sb->buffer);
  free(sb);
}

// Helper for binary operations with mixed types
// Corresponds to BINARY_OP macro in vm.c
void vm_binary_op(VM *vm, VMValue *a_ptr, VMValue *b_ptr, int op_token,
                  VMValue *result) {
  VMValue a = *a_ptr;
  VMValue b = *b_ptr;

  // Use the same Type Pair logic as VM
  uint8_t type_pair = TYPE_PAIR(a, b);

  switch (op_token) {
  case TOKEN_PLUS: // +
    switch (type_pair) {
    case TYPE_INT_INT:
      *result = VM_INT(AS_INT(a) + AS_INT(b));
      return;
    case TYPE_FLOAT_FLOAT:
      *result = VM_FLOAT(AS_FLOAT(a) + AS_FLOAT(b));
      return;
    case TYPE_INT_FLOAT:
      *result = VM_FLOAT((double)AS_INT(a) + AS_FLOAT(b));
      return;
    case TYPE_FLOAT_INT:
      *result = VM_FLOAT(AS_FLOAT(a) + (double)AS_INT(b));
      return;
    default: {
      // String Concatenation
      if (IS_STRING(a) && IS_STRING(b)) {
        ObjString *s1 = AS_STRING(a);
        ObjString *s2 = AS_STRING(b);

        int total_len = s1->length + s2->length;
        char *buf = (char *)malloc(total_len + 1);
        memcpy(buf, s1->chars, s1->length);
        memcpy(buf + s1->length, s2->chars, s2->length);
        buf[total_len] = '\0';

        if (vm) {
          ObjString *res_str = vm_alloc_string(vm, buf, total_len);
          *result = VM_OBJ((Obj *)res_str);
        } else {
          // AOT mode
          ObjString *res_str = aot_allocate_string(buf, total_len);
          *result = VM_OBJ((Obj *)res_str);
        }
        free(buf);
        return;
      }
      *result = VM_INT(0);
      return;
    }
    }

  case TOKEN_MINUS: // -
    switch (type_pair) {
    case TYPE_INT_INT:
      *result = VM_INT(AS_INT(a) - AS_INT(b));
      return;
    case TYPE_FLOAT_FLOAT:
      *result = VM_FLOAT(AS_FLOAT(a) - AS_FLOAT(b));
      return;
    case TYPE_INT_FLOAT:
      *result = VM_FLOAT((double)AS_INT(a) - AS_FLOAT(b));
      return;
    case TYPE_FLOAT_INT:
      *result = VM_FLOAT(AS_FLOAT(a) - (double)AS_INT(b));
      return;
    default:
      *result = VM_INT(0);
      return;
    }

  case TOKEN_MULTIPLY: // *
    switch (type_pair) {
    case TYPE_INT_INT:
      *result = VM_INT(AS_INT(a) * AS_INT(b));
      return;
    case TYPE_FLOAT_FLOAT:
      *result = VM_FLOAT(AS_FLOAT(a) * AS_FLOAT(b));
      return;
    case TYPE_INT_FLOAT:
      *result = VM_FLOAT((double)AS_INT(a) * AS_FLOAT(b));
      return;
    case TYPE_FLOAT_INT:
      *result = VM_FLOAT(AS_FLOAT(a) * (double)AS_INT(b));
      return;
    default:
      *result = VM_INT(0);
      return;
    }

  case TOKEN_DIVIDE: // /
    switch (type_pair) {
    case TYPE_INT_INT:
      if (AS_INT(b) == 0) {
        printf("Runtime Error: Division by zero\n");
        *result = VM_INT(0);
        return;
      }
      *result = VM_INT(AS_INT(a) / AS_INT(b));
      return;
    case TYPE_FLOAT_FLOAT:
      *result = VM_FLOAT(AS_FLOAT(a) / AS_FLOAT(b));
      return;
    case TYPE_INT_FLOAT:
      *result = VM_FLOAT((double)AS_INT(a) / AS_FLOAT(b));
      return;
    case TYPE_FLOAT_INT:
      *result = VM_FLOAT(AS_FLOAT(a) / (double)AS_INT(b));
      return;
    default:
      *result = VM_INT(0);
      return;
    }

  case TOKEN_LESS: // <
    switch (type_pair) {
    case TYPE_INT_INT:
      *result = VM_BOOL(AS_INT(a) < AS_INT(b));
      return;
    case TYPE_FLOAT_FLOAT:
      *result = VM_BOOL(AS_FLOAT(a) < AS_FLOAT(b));
      return;
    case TYPE_INT_FLOAT:
      *result = VM_BOOL((double)AS_INT(a) < AS_FLOAT(b));
      return;
    case TYPE_FLOAT_INT:
      *result = VM_BOOL(AS_FLOAT(a) < (double)AS_INT(b));
      return;
    default:
      *result = VM_BOOL(0);
      return;
    }

  case TOKEN_GREATER: // >
    switch (type_pair) {
    case TYPE_INT_INT:
      *result = VM_BOOL(AS_INT(a) > AS_INT(b));
      return;
    case TYPE_FLOAT_FLOAT:
      *result = VM_BOOL(AS_FLOAT(a) > AS_FLOAT(b));
      return;
    case TYPE_INT_FLOAT:
      *result = VM_BOOL((double)AS_INT(a) > AS_FLOAT(b));
      return;
    case TYPE_FLOAT_INT:
      *result = VM_BOOL(AS_FLOAT(a) > (double)AS_INT(b));
      return;
    default:
      *result = VM_BOOL(0);
      return;
    }

  case TOKEN_LESS_EQUAL: // <=
    switch (type_pair) {
    case TYPE_INT_INT:
      *result = VM_BOOL(AS_INT(a) <= AS_INT(b));
      return;
    case TYPE_FLOAT_FLOAT:
      *result = VM_BOOL(AS_FLOAT(a) <= AS_FLOAT(b));
      return;
    case TYPE_INT_FLOAT:
      *result = VM_BOOL((double)AS_INT(a) <= AS_FLOAT(b));
      return;
    case TYPE_FLOAT_INT:
      *result = VM_BOOL(AS_FLOAT(a) <= (double)AS_INT(b));
      return;
    default:
      *result = VM_BOOL(0);
      return;
    }

  case TOKEN_GREATER_EQUAL: // >=
    switch (type_pair) {
    case TYPE_INT_INT:
      *result = VM_BOOL(AS_INT(a) >= AS_INT(b));
      return;
    case TYPE_FLOAT_FLOAT:
      *result = VM_BOOL(AS_FLOAT(a) >= AS_FLOAT(b));
      return;
    case TYPE_INT_FLOAT:
      *result = VM_BOOL((double)AS_INT(a) >= AS_FLOAT(b));
      return;
    case TYPE_FLOAT_INT:
      *result = VM_BOOL(AS_FLOAT(a) >= (double)AS_INT(b));
      return;
    default:
      *result = VM_BOOL(0);
      return;
    }

  case TOKEN_EQUAL: // ==
    switch (type_pair) {
    case TYPE_INT_INT:
      *result = VM_BOOL(AS_INT(a) == AS_INT(b));
      return;
    case TYPE_FLOAT_FLOAT:
      *result = VM_BOOL(AS_FLOAT(a) == AS_FLOAT(b));
      return;
    case TYPE_BOOL_BOOL:
      *result = VM_BOOL(AS_BOOL(a) == AS_BOOL(b));
      return;
    case TYPE_INT_FLOAT:
      *result = VM_BOOL((double)AS_INT(a) == AS_FLOAT(b));
      return;
    case TYPE_FLOAT_INT:
      *result = VM_BOOL(AS_FLOAT(a) == (double)AS_INT(b));
      return;
    default: {
      if (IS_STRING(a) && IS_STRING(b)) {
        ObjString *s1 = AS_STRING(a);
        ObjString *s2 = AS_STRING(b);
        *result = VM_BOOL(strcmp(s1->chars, s2->chars) == 0);
        return;
      }
      *result = VM_BOOL(0);
      return;
    }
    }

  case TOKEN_NOT_EQUAL: // !=
  {
    bool eq = false;
    switch (type_pair) {
    case TYPE_INT_INT:
      eq = AS_INT(a) == AS_INT(b);
      break;
    case TYPE_FLOAT_FLOAT:
      eq = AS_FLOAT(a) == AS_FLOAT(b);
      break;
    case TYPE_BOOL_BOOL:
      eq = AS_BOOL(a) == AS_BOOL(b);
      break;
    case TYPE_INT_FLOAT:
      eq = (double)AS_INT(a) == AS_FLOAT(b);
      break;
    case TYPE_FLOAT_INT:
      eq = AS_FLOAT(a) == (double)AS_INT(b);
      break;
    default: {
      if (IS_STRING(a) && IS_STRING(b)) {
        ObjString *s1 = AS_STRING(a);
        ObjString *s2 = AS_STRING(b);
        eq = strcmp(s1->chars, s2->chars) == 0;
      } else {
        eq = false;
      }
      break;
    }
    }
    *result = VM_BOOL(!eq);
    return;
  }

  default:
    *result = VM_INT(0);
    return;
  }
}

// Array Wrappers
void vm_array_push_wrapper(VM *vm, ObjArray *array, VMValue value) {
  if (!array)
    return;
  vm_array_push(vm, array, value);
}

VMValue vm_array_get(ObjArray *array, int index) {
  if (!array || index < 0 || index >= array->count) {
    printf("Runtime Error: Array index out of bounds\n");
    return VM_INT(0);
  }
  return array->items[index];
}

void vm_array_set(ObjArray *array, int index, VMValue value) {
  if (!array || index < 0 || index >= array->count) {
    printf("Runtime Error: Array index out of bounds\n");
    return;
  }
  array->items[index] = value;
}

// ============================================================
// VALUE-BASED ARRAY ACCESS (no alloca needed, stack-safe)
// These functions take/return VMValue directly for efficiency
// ============================================================

// Enable/disable bounds checking for performance testing
#ifndef TULPAR_UNSAFE_ARRAYS
#define TULPAR_UNSAFE_ARRAYS 0 // Set to 1 to disable bounds check
#endif

// Fast array get - takes VMValue array and int index, returns VMValue
VMValue aot_array_get_fast(VMValue arr_val, int64_t index) {
#if TULPAR_UNSAFE_ARRAYS
  // UNSAFE MODE: No type check, no bounds check - MAXIMUM SPEED
  ObjArray *arr = AS_ARRAY(arr_val);
  return arr->items[index];
#else
  // SAFE MODE: Full checks
  if (!IS_ARRAY(arr_val)) {
    return VM_INT(0);
  }
  ObjArray *arr = AS_ARRAY(arr_val);
  if (index < 0 || index >= arr->count) {
    return VM_INT(0);
  }
  return arr->items[index];
#endif
}

// Fast array set - takes VMValue array, int index, VMValue value
void aot_array_set_fast(VMValue arr_val, int64_t index, VMValue value) {
#if TULPAR_UNSAFE_ARRAYS
  // UNSAFE MODE: No type check, no bounds check
  ObjArray *arr = AS_ARRAY(arr_val);
  arr->items[index] = value;
#else
  // SAFE MODE: Full checks
  if (!IS_ARRAY(arr_val)) {
    return;
  }
  ObjArray *arr = AS_ARRAY(arr_val);
  if (index < 0 || index >= arr->count) {
    return;
  }
  arr->items[index] = value;
#endif
}

// ============================================================
// UNSAFE ARRAY ACCESS - NO BOUNDS CHECK (for benchmarking)
// WARNING: Use only when you're 100% sure indices are valid!
// ============================================================

// Unsafe array get - NO type check, NO bounds check
// Assumes arr_val is definitely an array and index is in range
__attribute__((always_inline)) static inline VMValue
aot_array_get_unsafe(VMValue arr_val, int64_t index) {
  ObjArray *arr = AS_ARRAY(arr_val); // Direct cast, no check
  return arr->items[index];
}

// Unsafe array set - NO type check, NO bounds check
__attribute__((always_inline)) static inline void
aot_array_set_unsafe(VMValue arr_val, int64_t index, VMValue value) {
  ObjArray *arr = AS_ARRAY(arr_val); // Direct cast, no check
  arr->items[index] = value;
}

// ============================================================
// RAW POINTER ARRAY ACCESS - Maximum performance
// Works directly with ObjArray pointer, skips VMValue wrapper
// ============================================================

// Get raw array pointer from VMValue (one-time conversion)
ObjArray *aot_array_get_raw(VMValue arr_val) {
  if (!IS_ARRAY(arr_val))
    return NULL;
  return AS_ARRAY(arr_val);
}

// Direct array element access via raw pointer - FASTEST!
__attribute__((always_inline)) static inline VMValue
aot_raw_get(ObjArray *arr, int64_t index) {
  return arr->items[index];
}

__attribute__((always_inline)) static inline void
aot_raw_set(ObjArray *arr, int64_t index, VMValue value) {
  arr->items[index] = value;
}

// Public versions for LLVM to call (non-inline)
VMValue aot_array_get_raw_fast(ObjArray *arr, int64_t index) {
  return arr->items[index];
}

void aot_array_set_raw_fast(ObjArray *arr, int64_t index, VMValue value) {
  arr->items[index] = value;
}

// Object Wrappers
void vm_object_set(VM *vm, ObjObject *obj, char *key, VMValue value) {
  if (!obj || !key)
    return;

  // Create string object for key
  int len = strlen(key);
  ObjString *keyObj = vm_alloc_string(vm, key, len);

  // Check if key exists
  for (int i = 0; i < obj->count; i++) {
    if (strcmp(obj->keys[i]->chars, key) == 0) {
      obj->values[i] = value;
      return;
    }
  }

  // Resize if needed
  if (obj->count >= obj->capacity) {
    obj->capacity = obj->capacity < 8 ? 8 : obj->capacity * 2;
    obj->keys =
        (ObjString **)realloc(obj->keys, sizeof(ObjString *) * obj->capacity);
    obj->values =
        (VMValue *)realloc(obj->values, sizeof(VMValue) * obj->capacity);
  }

  obj->keys[obj->count] = keyObj;
  obj->values[obj->count] = value;
  obj->count++;
}

VMValue vm_object_get(ObjObject *obj, char *key) {
  if (!obj || !key)
    return VM_INT(0);

  for (int i = 0; i < obj->count; i++) {
    if (strcmp(obj->keys[i]->chars, key) == 0) {
      return obj->values[i];
    }
  }
  return VM_INT(0); // Undefined property
}

// Forward declarations
VMValue vm_get_element(VMValue target, VMValue index);
void vm_set_element(VM *vm, VMValue target, VMValue index, VMValue value);

// Pointer-based version for ABI compatibility with LLVM
VMValue vm_get_element_ptr(VMValue *target, VMValue *index) {
  if (!target || !index)
    return VM_INT(0);
  return vm_get_element(*target, *index);
}

// Pointer-based version for ABI compatibility with LLVM
void vm_set_element_ptr(VM *vm, VMValue *target, VMValue *index,
                        VMValue *value) {
  if (!target || !index || !value) {
    printf("Runtime Error: NULL pointer in set element\n");
    return;
  }
  vm_set_element(vm, *target, *index, *value);
}

VMValue vm_get_element(VMValue target, VMValue index) {
  if (IS_ARRAY(target)) {
    if (IS_INT(index)) {
      return vm_array_get(AS_ARRAY(target), (int)AS_INT(index));
    }
  } else if (IS_OBJECT(target)) {
    if (IS_STRING(index)) {
      return vm_object_get(AS_OBJECT(target), AS_STRING(index)->chars);
    }
  } else if (IS_STRING(target)) {
    if (IS_INT(index)) {
      ObjString *str = AS_STRING(target);
      int idx = (int)AS_INT(index);
      if (idx < 0 || idx >= str->length)
        return VM_OBJ(aot_allocate_string("", 0));
      return VM_OBJ(aot_allocate_string(&str->chars[idx], 1));
    }
  }
  printf("Runtime Error: Invalid index or target for get access\n");
  return VM_INT(0);
}

void vm_set_element(VM *vm, VMValue target, VMValue index, VMValue value) {
  if (IS_ARRAY(target)) {
    if (IS_INT(index)) {
      vm_array_set(AS_ARRAY(target), (int)AS_INT(index), value);
      return;
    }
  } else if (IS_OBJECT(target)) {
    if (IS_STRING(index)) {
      vm_object_set(vm, AS_OBJECT(target), AS_STRING(index)->chars, value);
      return;
    }
  }
  printf("Runtime Error: Invalid index or target for set access\n");
}

// Print a VMValue (used by OP_PRINT in VM)
void print_vm_value(VMValue value) {
  switch (value.type) {
  case VM_VAL_VOID:
    printf("void");
    break;
  case VM_VAL_BOOL:
    printf("%s", AS_BOOL(value) ? "true" : "false");
    break;
  case VM_VAL_INT:
    printf("%lld", AS_INT(value));
    break;
  case VM_VAL_FLOAT:
    printf("%g", AS_FLOAT(value));
    break;
  case VM_VAL_OBJ:
    if (IS_STRING(value)) {
      printf("%s", AS_STRING(value)->chars);
    } else if (IS_ARRAY(value)) {
      ObjArray *arr = AS_ARRAY(value);
      printf("[");
      for (int i = 0; i < arr->count; i++) {
        if (i > 0)
          printf(", ");
        print_vm_value(arr->items[i]);
      }
      printf("]");
    } else {
      printf("<object>");
    }
    break;
  default:
    printf("<unknown>");
    break;
  }
}

// Alias for LLVM AOT - prints with newline (takes pointer for ABI
// compatibility)
void print_value(VMValue *value_ptr) {
  if (value_ptr) {
    print_vm_value(*value_ptr);
  }
  printf("\n");
}

// Print without newline - used for inline printing (takes pointer for ABI
// compatibility)
void print_value_inline(VMValue *value_ptr) {
  if (value_ptr) {
    print_vm_value(*value_ptr);
  }
}

// Print newline only
void print_newline(void) {
  printf("\n");
  fflush(stdout);
}

// ============================================================================
// AOT Builtin Functions
// ============================================================================

// toString(VMValue) -> VMValue (String Object)
static char aot_string_buffer[1024];

VMValue aot_to_string(VMValue value) {
  switch (value.type) {
  case VM_VAL_INT:
    snprintf(aot_string_buffer, sizeof(aot_string_buffer), "%lld",
             AS_INT(value));
    break;
  case VM_VAL_FLOAT:
    snprintf(aot_string_buffer, sizeof(aot_string_buffer), "%g",
             AS_FLOAT(value));
    break;
  case VM_VAL_BOOL:
    snprintf(aot_string_buffer, sizeof(aot_string_buffer), "%s",
             AS_BOOL(value) ? "true" : "false");
    break;
  case VM_VAL_OBJ:
    if (IS_STRING(value)) {
      return value;
    }
    snprintf(aot_string_buffer, sizeof(aot_string_buffer), "<object>");
    break;
  default:
    snprintf(aot_string_buffer, sizeof(aot_string_buffer), "null");
    break;
  }

  ObjString *str =
      aot_allocate_string(aot_string_buffer, strlen(aot_string_buffer));
  return VM_OBJ((Obj *)str);
}

// toInt(VMValue) -> int64
int64_t aot_to_int(VMValue value) {
  switch (value.type) {
  case VM_VAL_INT:
    return AS_INT(value);
  case VM_VAL_FLOAT:
    return (int64_t)AS_FLOAT(value);
  case VM_VAL_BOOL:
    return AS_BOOL(value) ? 1 : 0;
  case VM_VAL_OBJ:
    if (IS_STRING(value)) {
      return atoll(AS_STRING(value)->chars);
    }
    return 0;
  default:
    return 0;
  }
}

// toFloat(VMValue) -> double
double aot_to_float(VMValue value) {
  switch (value.type) {
  case VM_VAL_INT:
    return (double)AS_INT(value);
  case VM_VAL_FLOAT:
    return AS_FLOAT(value);
  case VM_VAL_BOOL:
    return AS_BOOL(value) ? 1.0 : 0.0;
  case VM_VAL_OBJ:
    if (IS_STRING(value)) {
      return atof(AS_STRING(value)->chars);
    }
    return 0.0;
  default:
    return 0.0;
  }
}

// len(VMValue) -> int
int64_t aot_len(VMValue value) {
  if (IS_STRING(value)) {
    return AS_STRING(value)->length;
  } else if (IS_ARRAY(value)) {
    return AS_ARRAY(value)->count;
  }
  return 0;
}

// push(array, value) - wrapper for AOT (pointer ABI for Windows)
void aot_array_push(VMValue *arr_ptr, VMValue *item_ptr) {
  VMValue arr_val = *arr_ptr;
  VMValue item = *item_ptr;
  if (IS_ARRAY(arr_val)) {
    ObjArray *arr = AS_ARRAY(arr_val);
    // Inline push without VM
    if (arr->count >= arr->capacity) {
      int new_cap = arr->capacity < 8 ? 8 : arr->capacity * 2;
      arr->items = realloc(arr->items, sizeof(VMValue) * new_cap);
      arr->capacity = new_cap;
    }
    arr->items[arr->count++] = item;
  }
}

// pop(array) -> VMValue
VMValue aot_array_pop(VMValue arr_val) {
  if (IS_ARRAY(arr_val) && AS_ARRAY(arr_val)->count > 0) {
    ObjArray *arr = AS_ARRAY(arr_val);
    return arr->items[--arr->count];
  }
  return VM_INT(0);
}

// ============================================================================
// JSON Serialization - Optimized for Performance
// ============================================================================

typedef struct {
  char *data;
  size_t len;
  size_t cap;
} JSBuilder;

// Pre-allocated buffer for small JSON (avoids malloc for simple cases)
static __thread char js_small_buffer[4096];
static __thread int js_small_buffer_in_use = 0;

static void js_init(JSBuilder *b) {
  // Use thread-local buffer if available (avoids malloc)
  if (!js_small_buffer_in_use) {
    js_small_buffer_in_use = 1;
    b->data = js_small_buffer;
    b->cap = sizeof(js_small_buffer);
    b->len = 0;
    b->data[0] = '\0';
  } else {
    b->cap = 1024;
    b->len = 0;
    b->data = (char *)malloc(b->cap);
    if (b->data)
      b->data[0] = '\0';
  }
}

static void js_free(JSBuilder *b) {
  if (b->data == js_small_buffer) {
    js_small_buffer_in_use = 0;
  } else if (b->data) {
    free(b->data);
  }
  b->data = NULL;
}

// Ensure capacity - inline for speed
static inline void js_ensure(JSBuilder *b, size_t extra) {
  if (b->len + extra + 1 >= b->cap) {
    size_t new_cap = (b->cap + extra + 256) * 2;
    if (b->data == js_small_buffer) {
      // Migrate from small buffer to heap
      char *new_data = (char *)malloc(new_cap);
      if (new_data) {
        memcpy(new_data, b->data, b->len + 1);
        b->data = new_data;
        b->cap = new_cap;
      }
      js_small_buffer_in_use = 0;
    } else {
      b->data = (char *)realloc(b->data, new_cap);
      b->cap = new_cap;
    }
  }
}

// Append single character - ultra fast path
static inline void js_append_char(JSBuilder *b, char c) {
  js_ensure(b, 1);
  if (b->data) {
    b->data[b->len++] = c;
    b->data[b->len] = '\0';
  }
}

// Append known-length string - no strlen needed
static inline void js_append_n(JSBuilder *b, const char *str, size_t len) {
  js_ensure(b, len);
  if (b->data) {
    memcpy(b->data + b->len, str, len);
    b->len += len;
    b->data[b->len] = '\0';
  }
}

// Append null-terminated string
static void js_append(JSBuilder *b, const char *str) {
  js_append_n(b, str, strlen(str));
}

// Quick check if string needs escaping (common case: no escaping needed)
static inline int js_needs_escape(const char *str, int len) {
  for (int i = 0; i < len; i++) {
    unsigned char c = (unsigned char)str[i];
    if (c < 32 || c == '"' || c == '\\')
      return 1;
  }
  return 0;
}

// Fast path for simple strings (no escaping needed)
static inline void js_append_simple_string(JSBuilder *b, const char *str,
                                           int len) {
  js_ensure(b, len + 2);
  if (b->data) {
    b->data[b->len++] = '"';
    memcpy(b->data + b->len, str, len);
    b->len += len;
    b->data[b->len++] = '"';
    b->data[b->len] = '\0';
  }
}

// Optimized string escaping - batch copy non-escape chars
static void js_escape_string(JSBuilder *b, const char *str, int len) {
  // Fast path: most strings don't need escaping (especially keys)
  if (!js_needs_escape(str, len)) {
    js_append_simple_string(b, str, len);
    return;
  }

  js_append_char(b, '"');

  // Fast path: find runs of non-escape characters
  int start = 0;
  for (int i = 0; i < len; i++) {
    unsigned char c = (unsigned char)str[i];
    const char *escape = NULL;
    int escape_len = 0;

    // Check if char needs escaping
    switch (c) {
    case '"':
      escape = "\\\"";
      escape_len = 2;
      break;
    case '\\':
      escape = "\\\\";
      escape_len = 2;
      break;
    case '\b':
      escape = "\\b";
      escape_len = 2;
      break;
    case '\f':
      escape = "\\f";
      escape_len = 2;
      break;
    case '\n':
      escape = "\\n";
      escape_len = 2;
      break;
    case '\r':
      escape = "\\r";
      escape_len = 2;
      break;
    case '\t':
      escape = "\\t";
      escape_len = 2;
      break;
    default:
      if (c < 32) {
        // Flush pending chars first
        if (i > start) {
          js_append_n(b, str + start, i - start);
        }
        char hex[7];
        snprintf(hex, sizeof(hex), "\\u%04x", c);
        js_append_n(b, hex, 6);
        start = i + 1;
      }
      continue; // No escape needed for this char
    }

    // Flush pending non-escaped chars
    if (i > start) {
      js_append_n(b, str + start, i - start);
    }
    js_append_n(b, escape, escape_len);
    start = i + 1;
  }

  // Flush remaining chars
  if (len > start) {
    js_append_n(b, str + start, len - start);
  }

  js_append_char(b, '"');
}

// Fast integer to string conversion (faster than snprintf)
static inline int js_int_to_str(char *buf, long long val) {
  if (val == 0) {
    buf[0] = '0';
    buf[1] = '\0';
    return 1;
  }

  char tmp[24];
  int i = 0;
  int neg = 0;
  unsigned long long uval;

  if (val < 0) {
    neg = 1;
    uval = (unsigned long long)(-val);
  } else {
    uval = (unsigned long long)val;
  }

  while (uval > 0) {
    tmp[i++] = '0' + (uval % 10);
    uval /= 10;
  }

  int len = 0;
  if (neg)
    buf[len++] = '-';
  while (i > 0)
    buf[len++] = tmp[--i];
  buf[len] = '\0';
  return len;
}

static void js_serialize(JSBuilder *b, VMValue v, int depth) {
  if (depth > 20) {
    js_append_n(b, "\"<max_depth>\"", 13);
    return;
  }

  if (IS_INT(v)) {
    char tmp[24];
    int len = js_int_to_str(tmp, AS_INT(v));
    js_append_n(b, tmp, len);
  } else if (IS_FLOAT(v)) {
    char tmp[64];
    int len = snprintf(tmp, sizeof(tmp), "%g", AS_FLOAT(v));
    js_append_n(b, tmp, len);
  } else if (IS_BOOL(v)) {
    if (AS_BOOL(v)) {
      js_append_n(b, "true", 4);
    } else {
      js_append_n(b, "false", 5);
    }
  } else if (IS_STRING(v)) {
    ObjString *s = AS_STRING(v);
    js_escape_string(b, s->chars, s->length);
  } else if (IS_ARRAY(v)) {
    ObjArray *arr = AS_ARRAY(v);
    js_append_char(b, '[');
    for (int i = 0; i < arr->count; i++) {
      if (i > 0)
        js_append_char(b, ',');
      js_serialize(b, arr->items[i], depth + 1);
    }
    js_append_char(b, ']');
  } else if (IS_OBJECT(v)) {
    ObjObject *obj = AS_OBJECT(v);
    js_append_char(b, '{');
    for (int i = 0; i < obj->count; i++) {
      if (i > 0)
        js_append_char(b, ',');
      js_escape_string(b, obj->keys[i]->chars, obj->keys[i]->length);
      js_append_char(b, ':');
      js_serialize(b, obj->values[i], depth + 1);
    }
    js_append_char(b, '}');
  } else {
    js_append_n(b, "null", 4);
  }
}

// ============================================================
// cJSON-based Fast JSON Serializer
// ============================================================

// Convert VMValue to cJSON object
static cJSON *vmvalue_to_cjson(VMValue v, int depth) {
  if (depth > 20)
    return cJSON_CreateString("<max_depth>");

  if (IS_INT(v)) {
    return cJSON_CreateNumber((double)AS_INT(v));
  } else if (IS_FLOAT(v)) {
    return cJSON_CreateNumber(AS_FLOAT(v));
  } else if (IS_BOOL(v)) {
    return cJSON_CreateBool(AS_BOOL(v));
  } else if (IS_STRING(v)) {
    ObjString *s = AS_STRING(v);
    return cJSON_CreateString(s->chars);
  } else if (IS_ARRAY(v)) {
    ObjArray *arr = AS_ARRAY(v);
    cJSON *json_arr = cJSON_CreateArray();
    for (int i = 0; i < arr->count; i++) {
      cJSON_AddItemToArray(json_arr,
                           vmvalue_to_cjson(arr->items[i], depth + 1));
    }
    return json_arr;
  } else if (IS_OBJECT(v)) {
    ObjObject *obj = AS_OBJECT(v);
    cJSON *json_obj = cJSON_CreateObject();
    for (int i = 0; i < obj->count; i++) {
      cJSON_AddItemToObject(json_obj, obj->keys[i]->chars,
                            vmvalue_to_cjson(obj->values[i], depth + 1));
    }
    return json_obj;
  }
  return cJSON_CreateNull();
}

// toJson using cJSON (faster than manual implementation)
VMValue aot_to_json_cjson(VMValue value) {
  cJSON *json = vmvalue_to_cjson(value, 0);
  if (!json)
    return VM_INT(0);

  // Use unformatted print for speed (no pretty printing)
  char *str = cJSON_PrintUnformatted(json);
  cJSON_Delete(json);

  if (!str)
    return VM_INT(0);

  int len = strlen(str);
  ObjString *res = aot_allocate_string(str, len);
  free(str); // cJSON allocates with malloc

  return VM_OBJ((Obj *)res);
}

// toJson(VMValue) -> VMValue (String)
// Uses our optimized JSBuilder implementation
VMValue aot_to_json(VMValue value) {
  JSBuilder b;
  js_init(&b);
  if (!b.data)
    return VM_INT(0); // Alloc failed

  js_serialize(&b, value, 0);

  ObjString *res = aot_allocate_string(b.data, b.len);
  js_free(&b);
  return VM_OBJ((Obj *)res);
}

// AOT Wrappers for Allocations
ObjArray *vm_allocate_array_aot_wrapper(void *vm) {
  if (vm)
    return vm_allocate_array(vm);
  ObjArray *arr = (ObjArray *)malloc(sizeof(ObjArray));
  arr->obj.type = OBJ_ARRAY;
  arr->obj.arena_allocated = 0;
  arr->obj.next = NULL;
  arr->count = 0;
  arr->capacity = 0;
  arr->items = NULL;
  return arr;
}

ObjObject *vm_allocate_object_aot_wrapper(void *vm) {
  if (vm)
    return vm_allocate_object(vm);
  ObjObject *obj = (ObjObject *)malloc(sizeof(ObjObject));
  obj->obj.type = OBJ_OBJECT;
  obj->obj.arena_allocated = 0;
  obj->obj.next = NULL;
  obj->count = 0;
  obj->capacity = 0;
  obj->keys = NULL;
  obj->values = NULL;
  return obj;
}

void vm_array_push_aot_wrapper(void *vm, ObjArray *array, VMValue value) {
  if (vm) {
    // Since we don't have vm_array_push_wrapper symbol available (it's
    // vm_array_push) We call vm_array_push. Assuming vm_array_push is available
    // (linked from vm.c).
    vm_array_push(vm, array, value);
    return;
  }
  if (array->count >= array->capacity) {
    int new_cap = array->capacity < 8 ? 8 : array->capacity * 2;
    array->items = (VMValue *)realloc(array->items, sizeof(VMValue) * new_cap);
    array->capacity = new_cap;
  }
  array->items[array->count++] = value;
}

void vm_object_set_aot_wrapper(void *vm, ObjObject *obj, char *key,
                               VMValue value) {
  if (vm) {
    vm_object_set(vm, obj, key, value);
    return;
  }
  // AOT Logic
  if (obj->count >= obj->capacity) {
    int new_cap = obj->capacity < 8 ? 8 : obj->capacity * 2;
    obj->keys = (ObjString **)realloc(obj->keys, sizeof(ObjString *) * new_cap);
    obj->values = (VMValue *)realloc(obj->values, sizeof(VMValue) * new_cap);
    obj->capacity = new_cap;
  }

  // Create string object for key.
  // aot_allocate_string copies the chars.
  obj->keys[obj->count] = aot_allocate_string(key, strlen(key));
  obj->values[obj->count] = value;
  obj->count++;
}

// --- AOT Builtin Logic for Input and Strings ---

// AOT Input: Reads a line from stdin
VMValue aot_input() {
  char buffer[1024];
  if (fgets(buffer, sizeof(buffer), stdin)) {
    size_t len = strlen(buffer);
    if (len > 0 && buffer[len - 1] == '\n') {
      buffer[len - 1] = '\0';
      len--;
    }
    ObjString *str = aot_allocate_string(buffer, (int)len);
    return VM_OBJ((Obj *)str);
  }
  ObjString *empty = aot_allocate_string("", 0);
  return VM_OBJ((Obj *)empty);
}

// AOT Trim: Removes whitespace from start/end
VMValue aot_trim(VMValue val) {
  if (!IS_STRING(val))
    return val;
  ObjString *s = AS_STRING(val);

  int start = 0;
  while (start < s->length &&
         (s->chars[start] == ' ' || s->chars[start] == '\t' ||
          s->chars[start] == '\n' || s->chars[start] == '\r')) {
    start++;
  }

  int end = s->length - 1;
  while (end > start && (s->chars[end] == ' ' || s->chars[end] == '\t' ||
                         s->chars[end] == '\n' || s->chars[end] == '\r')) {
    end--;
  }

  int new_len = end - start + 1;
  if (new_len < 0)
    new_len = 0;

  char *buf = (char *)malloc(new_len + 1);
  if (new_len > 0) {
    memcpy(buf, s->chars + start, new_len);
  }
  buf[new_len] = '\0';

  ObjString *res = aot_allocate_string(buf, new_len);
  free(buf);
  return VM_OBJ((Obj *)res);
}

// AOT Replace
VMValue aot_replace(VMValue strVal, VMValue oldVal, VMValue newVal) {
  if (!IS_STRING(strVal) || !IS_STRING(oldVal) || !IS_STRING(newVal))
    return strVal;

  ObjString *s = AS_STRING(strVal);
  ObjString *oldS = AS_STRING(oldVal);
  ObjString *newS = AS_STRING(newVal);

  if (oldS->length == 0)
    return strVal;

  int count = 0;
  const char *tmp = s->chars;
  while ((tmp = strstr(tmp, oldS->chars))) {
    count++;
    tmp += oldS->length;
  }

  int new_len = s->length + count * (newS->length - oldS->length);
  char *result = (char *)malloc(new_len + 1);

  char *ptr = result;
  const char *src = s->chars;
  const char *found;

  while ((found = strstr(src, oldS->chars))) {
    int segment_len = (int)(found - src);
    memcpy(ptr, src, segment_len);
    ptr += segment_len;

    memcpy(ptr, newS->chars, newS->length);
    ptr += newS->length;

    src = found + oldS->length;
  }
  strcpy(ptr, src);

  ObjString *resObj = aot_allocate_string(result, new_len);
  free(result);
  return VM_OBJ((Obj *)resObj);
}

// AOT Split
VMValue aot_split(VMValue strVal, VMValue delVal) {
  if (!IS_STRING(strVal) || !IS_STRING(delVal)) {
    ObjArray *arr = vm_allocate_array_aot_wrapper(NULL);
    return VM_OBJ((Obj *)arr);
  }

  ObjString *s = AS_STRING(strVal);
  ObjString *d = AS_STRING(delVal);

  ObjArray *arr = vm_allocate_array_aot_wrapper(NULL);

  if (s->length == 0)
    return VM_OBJ((Obj *)arr);

  if (d->length == 0) {
    for (int i = 0; i < s->length; i++) {
      char single[2] = {s->chars[i], '\0'};
      ObjString *seg = aot_allocate_string(single, 1);
      vm_array_push_aot_wrapper(NULL, arr, VM_OBJ((Obj *)seg));
    }
  } else {
    const char *start = s->chars;
    const char *p;
    while ((p = strstr(start, d->chars)) != NULL) {
      int len = (int)(p - start);
      char *sub = (char *)malloc(len + 1);
      strncpy(sub, start, len);
      sub[len] = '\0';

      ObjString *seg = aot_allocate_string(sub, len);
      vm_array_push_aot_wrapper(NULL, arr, VM_OBJ((Obj *)seg));
      free(sub);

      start = p + d->length;
    }
    ObjString *seg = aot_allocate_string(start, strlen(start));
    vm_array_push_aot_wrapper(NULL, arr, VM_OBJ((Obj *)seg));
  }

  return VM_OBJ((Obj *)arr);
}
// ============================================================================
// File I/O Builtins
// ============================================================================

VMValue aot_read_file(VMValue path_val) {
  if (!IS_STRING(path_val))
    return VM_VOID();
  const char *path = AS_STRING(path_val)->chars;

  FILE *f = fopen(path, "rb");
  if (!f)
    return VM_VOID();

  fseek(f, 0, SEEK_END);
  long fsize = ftell(f);
  fseek(f, 0, SEEK_SET);

  char *string = (char *)malloc(fsize + 1);
  if (string) {
    size_t result = fread(string, 1, fsize, f);
    string[fsize] = 0;
    if (result != (size_t)fsize) {
      // Read failed or partial
    }
  }
  fclose(f);

  if (!string)
    return VM_VOID();

  // Create ObjString (copies char*)
  ObjString *ostr = vm_alloc_string_aot(NULL, string, (int)fsize);
  free(string);

  return VM_OBJ(ostr);
}

VMValue aot_write_file(VMValue path_val, VMValue content_val) {
  if (!IS_STRING(path_val) || !IS_STRING(content_val))
    return VM_VOID();
  const char *path = AS_STRING(path_val)->chars;
  const char *content = AS_STRING(content_val)->chars;
  int len = AS_STRING(content_val)->length;

  FILE *f = fopen(path, "wb");
  if (f) {
    fwrite(content, 1, len, f);
    fclose(f);
  }
  return VM_VOID();
}

VMValue aot_append_file(VMValue path_val, VMValue content_val) {
  if (!IS_STRING(path_val) || !IS_STRING(content_val))
    return VM_VOID();
  const char *path = AS_STRING(path_val)->chars;
  const char *content = AS_STRING(content_val)->chars;
  int len = AS_STRING(content_val)->length;

  FILE *f = fopen(path, "ab");
  if (f) {
    fwrite(content, 1, len, f);
    fclose(f);
  }
  return VM_VOID();
}

VMValue aot_file_exists(VMValue path_val) {
  if (!IS_STRING(path_val))
    return VM_BOOL(false);
  const char *path = AS_STRING(path_val)->chars;
  FILE *f = fopen(path, "r");
  if (f) {
    fclose(f);
    return VM_BOOL(true);
  }
  return VM_BOOL(false);
}

// ============================================================================
// Exception Handling Runtime (setjmp/longjmp based)
// ============================================================================
#include <setjmp.h>

#define EH_STACK_MAX 64
static jmp_buf eh_stack[EH_STACK_MAX];
static int eh_depth = 0;
static VMValue eh_exception;

// Returns pointer to jmp_buf for setjmp
jmp_buf *aot_try_push(void) {
  if (eh_depth >= EH_STACK_MAX) {
    fprintf(stderr, "Exception handler stack overflow\n");
    return NULL;
  }
  return &eh_stack[eh_depth++];
}

void aot_try_pop(void) {
  if (eh_depth > 0)
    eh_depth--;
}

void aot_throw(VMValue exception) {
  if (eh_depth == 0) {
    fprintf(stderr, "Uncaught Exception: ");
    if (IS_STRING(exception)) {
      fprintf(stderr, "%s\n", AS_STRING(exception)->chars);
    } else if (IS_OBJECT(exception)) {
      // Try to print "message" property if exists?
      // Simple fallback
      fprintf(stderr, "<object type=%d>\n", AS_OBJ(exception)->type);
    } else {
      fprintf(stderr, "<value type=%d>\n", exception.type);
    }
    exit(1);
  }
  eh_exception = exception;
  eh_depth--;
  longjmp(eh_stack[eh_depth], 1);
}

VMValue aot_get_exception(void) { return eh_exception; }

// ============================================
// Time Functions
// ============================================
#include <sys/time.h>
#include <time.h>

VMValue aot_clock_ms(void) {
  struct timeval tv;
  gettimeofday(&tv, NULL);
  double ms = tv.tv_sec * 1000.0 + tv.tv_usec / 1000.0;
  // Return relative time from program start
  static double start_time = 0;
  if (start_time == 0) {
    start_time = ms;
  }
  return VM_FLOAT(ms - start_time);
}

// ============================================================================
// Socket Functions (AOT)
// ============================================================================
#ifdef _WIN32
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>

#pragma comment(lib, "Ws2_32.lib")
static int wsa_initialized = 0;
static int ensure_wsa(void) {
  if (wsa_initialized)
    return 1;
  WSADATA wsa;
  if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0)
    return 0;
  wsa_initialized = 1;
  return 1;
}
#define tulpar_socket SOCKET
#define tulpar_close closesocket
#define tulpar_send send
#define tulpar_recv recv
#define tulpar_invalid_socket INVALID_SOCKET
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
static int ensure_wsa(void) { return 1; }
#define tulpar_socket int
#define tulpar_close close
#define tulpar_send send
#define tulpar_recv recv
#define tulpar_invalid_socket -1
#endif

VMValue aot_socket_server(VMValue hostVal, VMValue portVal) {
  if (!IS_STRING(hostVal) || !IS_INT(portVal))
    return VM_INT(-1);

  int port = (int)AS_INT(portVal);

#ifdef _WIN32
  if (!ensure_wsa())
    return VM_INT(-1);
#endif

  tulpar_socket server_fd = socket(AF_INET, SOCK_STREAM, 0);
  if (server_fd == tulpar_invalid_socket)
    return VM_INT(-1);

  int opt = 1;
  setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, (const char *)&opt,
             sizeof(opt));

  struct sockaddr_in address;
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = INADDR_ANY;
  address.sin_port = htons((uint16_t)port);

  if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
    tulpar_close(server_fd);
    return VM_INT(-1);
  }

  if (listen(server_fd, 3) < 0) {
    tulpar_close(server_fd);
    return VM_INT(-1);
  }
  return VM_INT(server_fd);
}

VMValue aot_socket_client(VMValue hostVal, VMValue portVal) {
  if (!IS_STRING(hostVal) || !IS_INT(portVal))
    return VM_INT(-1);

  const char *host = AS_STRING(hostVal)->chars;
  int port = (int)AS_INT(portVal);

  if (!ensure_wsa())
    return VM_INT(-1);

  tulpar_socket sock = socket(AF_INET, SOCK_STREAM, 0);
  if (sock == tulpar_invalid_socket)
    return VM_INT(-1);

  struct sockaddr_in serv_addr;
  serv_addr.sin_family = AF_INET;
  serv_addr.sin_port = htons((uint16_t)port);

  if (inet_pton(AF_INET, host, &serv_addr.sin_addr) <= 0) {
    tulpar_close(sock);
    return VM_INT(-1);
  }

  if (connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
    tulpar_close(sock);
    return VM_INT(-1);
  }
  return VM_INT(sock);
}

VMValue aot_socket_accept(VMValue serverFdVal) {
  if (!IS_INT(serverFdVal))
    return VM_INT(-1);
  tulpar_socket server_fd = (tulpar_socket)AS_INT(serverFdVal);

  struct sockaddr_in address;
  socklen_t addrlen = sizeof(address);
  tulpar_socket new_socket =
      accept(server_fd, (struct sockaddr *)&address, &addrlen);
  return VM_INT((int64_t)new_socket);
}

VMValue aot_socket_send(VMValue fdVal, VMValue dataVal) {
  if (!IS_INT(fdVal) || !IS_STRING(dataVal))
    return VM_INT(-1);
  tulpar_socket fd = (tulpar_socket)AS_INT(fdVal);
  ObjString *s = AS_STRING(dataVal);

  ssize_t sent = (ssize_t)tulpar_send(fd, s->chars, s->length, 0);
  return VM_INT((int64_t)sent);
}

VMValue aot_socket_receive(VMValue fdVal, VMValue sizeVal) {
  if (!IS_INT(fdVal) || !IS_INT(sizeVal))
    return VM_OBJ((Obj *)aot_allocate_string("", 0));
  tulpar_socket fd = (tulpar_socket)AS_INT(fdVal);
  int size = (int)AS_INT(sizeVal);

  char *buffer = (char *)malloc(size + 1);
  if (!buffer)
    return VM_OBJ((Obj *)aot_allocate_string("", 0));

  ssize_t valread = (ssize_t)tulpar_recv(fd, buffer, size, 0);
  if (valread < 0)
    valread = 0;
  buffer[valread] = 0;

  ObjString *res = aot_allocate_string(buffer, valread);
  free(buffer);
  return VM_OBJ((Obj *)res);
}

void aot_socket_close(VMValue fdVal) {
  if (IS_INT(fdVal)) {
    tulpar_close((tulpar_socket)AS_INT(fdVal));
  }
}
// ============================================================================
// Threading Functions (AOT) - POSIX pthread
// ============================================================================
#include <pthread.h>

// Thread argument structure
typedef struct {
  void *func_ptr; // Function pointer to call
  VMValue arg;    // Argument to pass
} AOTThreadArgs;

// Thread entry point wrapper
static void *aot_thread_entry(void *arg) {
  AOTThreadArgs *targs = (AOTThreadArgs *)arg;

  // Call the function pointer with the argument
  // The function signature is: VMValue (*func)(VMValue)
  typedef VMValue (*ThreadFunc)(VMValue);
  ThreadFunc func = (ThreadFunc)targs->func_ptr;

  if (func) {
    func(targs->arg);
  }

  free(targs);
  return NULL;
}

// thread_create(func_ptr, arg) -> thread_id
VMValue aot_thread_create(void *func_ptr, VMValue arg) {
  pthread_t thread;

  AOTThreadArgs *targs = (AOTThreadArgs *)malloc(sizeof(AOTThreadArgs));
  if (!targs)
    return VM_INT(-1);

  targs->func_ptr = func_ptr;
  targs->arg = arg;

  int result = pthread_create(&thread, NULL, aot_thread_entry, targs);
  if (result != 0) {
    free(targs);
    return VM_INT(-1);
  }

  // Return thread ID as int64 (pthread_t is typically unsigned long)
  return VM_INT((int64_t)thread);
}

// thread_join(thread_id) -> void
void aot_thread_join(VMValue threadVal) {
  if (!IS_INT(threadVal))
    return;

  pthread_t thread = (pthread_t)AS_INT(threadVal);
  pthread_join(thread, NULL);
}

// thread_detach(thread_id) -> void
void aot_thread_detach(VMValue threadVal) {
  if (!IS_INT(threadVal))
    return;

  pthread_t thread = (pthread_t)AS_INT(threadVal);
  pthread_detach(thread);
}

// ============================================================================
// Mutex Functions (AOT) - POSIX pthread_mutex
// ============================================================================

// mutex_create() -> mutex_ptr (as int64)
VMValue aot_mutex_create(void) {
  pthread_mutex_t *mutex = (pthread_mutex_t *)malloc(sizeof(pthread_mutex_t));
  if (!mutex)
    return VM_INT(0);

  pthread_mutex_init(mutex, NULL);

  // Return mutex pointer as int64
  return VM_INT((int64_t)(uintptr_t)mutex);
}

// mutex_lock(mutex_ptr) -> void
void aot_mutex_lock(VMValue mutexVal) {
  if (!IS_INT(mutexVal))
    return;

  pthread_mutex_t *mutex = (pthread_mutex_t *)(uintptr_t)AS_INT(mutexVal);
  if (mutex) {
    pthread_mutex_lock(mutex);
  }
}

// mutex_unlock(mutex_ptr) -> void
void aot_mutex_unlock(VMValue mutexVal) {
  if (!IS_INT(mutexVal))
    return;

  pthread_mutex_t *mutex = (pthread_mutex_t *)(uintptr_t)AS_INT(mutexVal);
  if (mutex) {
    pthread_mutex_unlock(mutex);
  }
}

// mutex_destroy(mutex_ptr) -> void
void aot_mutex_destroy(VMValue mutexVal) {
  if (!IS_INT(mutexVal))
    return;

  pthread_mutex_t *mutex = (pthread_mutex_t *)(uintptr_t)AS_INT(mutexVal);
  if (mutex) {
    pthread_mutex_destroy(mutex);
    free(mutex);
  }
}

// ============================================================================
// HTTP Parsing Functions (AOT)
// ============================================================================

// Parse HTTP request: returns JSON object with method, path, headers, body
VMValue aot_http_parse_request(VMValue rawRequest) {
  if (!IS_STRING(rawRequest)) {
    return VM_INT(0);
  }

  ObjString *req = AS_STRING(rawRequest);
  const char *data = req->chars;
  int len = req->length;

  // Create result object
  ObjObject *result = (ObjObject *)aot_arena_alloc(sizeof(ObjObject));
  result->obj.type = OBJ_OBJECT;
  result->obj.arena_allocated = 1;
  result->obj.next = NULL;
  result->capacity = 8;
  result->count = 0;
  result->keys =
      (ObjString **)aot_arena_alloc(sizeof(ObjString *) * result->capacity);
  result->values =
      (VMValue *)aot_arena_alloc(sizeof(VMValue) * result->capacity);

  // Parse first line: METHOD PATH HTTP/1.1
  int line_end = 0;
  while (line_end < len && data[line_end] != '\r' && data[line_end] != '\n') {
    line_end++;
  }

  // Find method
  int method_end = 0;
  while (method_end < line_end && data[method_end] != ' ') {
    method_end++;
  }
  ObjString *method = aot_allocate_string(data, method_end);
  result->keys[result->count] = aot_allocate_string("method", 6);
  result->values[result->count] = VM_OBJ((Obj *)method);
  result->count++;

  // Find path
  int path_start = method_end + 1;
  int path_end = path_start;
  while (path_end < line_end && data[path_end] != ' ' &&
         data[path_end] != '?') {
    path_end++;
  }
  ObjString *path =
      aot_allocate_string(data + path_start, path_end - path_start);
  result->keys[result->count] = aot_allocate_string("path", 4);
  result->values[result->count] = VM_OBJ((Obj *)path);
  result->count++;

  // Find query string (if any)
  int query_start = path_end;
  if (query_start < line_end && data[query_start] == '?') {
    query_start++;
    int query_end = query_start;
    while (query_end < line_end && data[query_end] != ' ') {
      query_end++;
    }
    ObjString *query =
        aot_allocate_string(data + query_start, query_end - query_start);
    result->keys[result->count] = aot_allocate_string("query", 5);
    result->values[result->count] = VM_OBJ((Obj *)query);
    result->count++;
  }

  // Find body (after \r\n\r\n)
  const char *body_marker = "\r\n\r\n";
  const char *body_start = strstr(data, body_marker);
  if (body_start) {
    body_start += 4;
    int body_len = len - (body_start - data);
    if (body_len > 0) {
      ObjString *body = aot_allocate_string(body_start, body_len);
      result->keys[result->count] = aot_allocate_string("body", 4);
      result->values[result->count] = VM_OBJ((Obj *)body);
      result->count++;
    }
  }

  return VM_OBJ((Obj *)result);
}

// Create HTTP response string
VMValue aot_http_create_response(VMValue statusVal, VMValue contentTypeVal,
                                 VMValue bodyVal) {
  int status = 200;
  if (IS_INT(statusVal)) {
    status = (int)AS_INT(statusVal);
  }

  const char *content_type = "text/plain";
  if (IS_STRING(contentTypeVal)) {
    content_type = AS_STRING(contentTypeVal)->chars;
  }

  const char *body = "";
  int body_len = 0;
  if (IS_STRING(bodyVal)) {
    body = AS_STRING(bodyVal)->chars;
    body_len = AS_STRING(bodyVal)->length;
  }

  // Status text
  const char *status_text = "OK";
  switch (status) {
  case 200:
    status_text = "OK";
    break;
  case 201:
    status_text = "Created";
    break;
  case 204:
    status_text = "No Content";
    break;
  case 400:
    status_text = "Bad Request";
    break;
  case 401:
    status_text = "Unauthorized";
    break;
  case 403:
    status_text = "Forbidden";
    break;
  case 404:
    status_text = "Not Found";
    break;
  case 405:
    status_text = "Method Not Allowed";
    break;
  case 500:
    status_text = "Internal Server Error";
    break;
  }

  // Build response
  char header[512];
  int header_len = snprintf(header, sizeof(header),
                            "HTTP/1.1 %d %s\r\n"
                            "Content-Type: %s\r\n"
                            "Content-Length: %d\r\n"
                            "Connection: close\r\n"
                            "\r\n",
                            status, status_text, content_type, body_len);

  // Allocate full response
  int total_len = header_len + body_len;
  char *response = (char *)aot_arena_alloc(total_len + 1);
  memcpy(response, header, header_len);
  memcpy(response + header_len, body, body_len);
  response[total_len] = '\0';

  return VM_OBJ((Obj *)aot_allocate_string(response, total_len));
}

// ============================================================================
// Math Functions (AOT) - Using libm
// ============================================================================
#include <math.h>

void aot_math_abs(VMValue *result, VMValue *v_ptr) {
  VMValue v = *v_ptr;
  // fprintf(stderr, "DEBUG: abs input type=%d val=%lld\n", v.type,
  // v.as.int_val);
  if (IS_INT(v)) {
    int64_t val = AS_INT(v);
    // fprintf(stderr, "DEBUG: abs input int=%lld\n", val);
    *result = VM_INT(llabs(val));
    return;
  }
  if (IS_FLOAT(v)) {
    *result = VM_FLOAT(fabs(AS_FLOAT(v)));
    return;
  }
  *result = VM_INT(0);
}

VMValue aot_math_sqrt(VMValue v) {
  double val = IS_INT(v) ? (double)AS_INT(v) : AS_FLOAT(v);
  return VM_FLOAT(sqrt(val));
}

VMValue aot_math_pow(VMValue base, VMValue exp) {
  double b = IS_INT(base) ? (double)AS_INT(base) : AS_FLOAT(base);
  double e = IS_INT(exp) ? (double)AS_INT(exp) : AS_FLOAT(exp);
  return VM_FLOAT(pow(b, e));
}

VMValue aot_math_floor(VMValue v) {
  double val = IS_INT(v) ? (double)AS_INT(v) : AS_FLOAT(v);
  return VM_FLOAT(floor(val));
}

VMValue aot_math_ceil(VMValue v) {
  double val = IS_INT(v) ? (double)AS_INT(v) : AS_FLOAT(v);
  return VM_FLOAT(ceil(val));
}

VMValue aot_math_round(VMValue v) {
  double val = IS_INT(v) ? (double)AS_INT(v) : AS_FLOAT(v);
  return VM_FLOAT(round(val));
}

VMValue aot_math_sin(VMValue v) {
  double val = IS_INT(v) ? (double)AS_INT(v) : AS_FLOAT(v);
  return VM_FLOAT(sin(val));
}

VMValue aot_math_cos(VMValue v) {
  double val = IS_INT(v) ? (double)AS_INT(v) : AS_FLOAT(v);
  return VM_FLOAT(cos(val));
}

VMValue aot_math_tan(VMValue v) {
  double val = IS_INT(v) ? (double)AS_INT(v) : AS_FLOAT(v);
  return VM_FLOAT(tan(val));
}

VMValue aot_math_asin(VMValue v) {
  double val = IS_INT(v) ? (double)AS_INT(v) : AS_FLOAT(v);
  return VM_FLOAT(asin(val));
}

VMValue aot_math_acos(VMValue v) {
  double val = IS_INT(v) ? (double)AS_INT(v) : AS_FLOAT(v);
  return VM_FLOAT(acos(val));
}

VMValue aot_math_atan(VMValue v) {
  double val = IS_INT(v) ? (double)AS_INT(v) : AS_FLOAT(v);
  return VM_FLOAT(atan(val));
}

VMValue aot_math_atan2(VMValue y, VMValue x) {
  double yv = IS_INT(y) ? (double)AS_INT(y) : AS_FLOAT(y);
  double xv = IS_INT(x) ? (double)AS_INT(x) : AS_FLOAT(x);
  return VM_FLOAT(atan2(yv, xv));
}

VMValue aot_math_exp(VMValue v) {
  double val = IS_INT(v) ? (double)AS_INT(v) : AS_FLOAT(v);
  return VM_FLOAT(exp(val));
}

VMValue aot_math_log(VMValue v) {
  double val = IS_INT(v) ? (double)AS_INT(v) : AS_FLOAT(v);
  return VM_FLOAT(log(val));
}

VMValue aot_math_log10(VMValue v) {
  double val = IS_INT(v) ? (double)AS_INT(v) : AS_FLOAT(v);
  return VM_FLOAT(log10(val));
}

VMValue aot_math_log2(VMValue v) {
  double val = IS_INT(v) ? (double)AS_INT(v) : AS_FLOAT(v);
  return VM_FLOAT(log2(val));
}

VMValue aot_math_sinh(VMValue v) {
  double val = IS_INT(v) ? (double)AS_INT(v) : AS_FLOAT(v);
  return VM_FLOAT(sinh(val));
}

VMValue aot_math_cosh(VMValue v) {
  double val = IS_INT(v) ? (double)AS_INT(v) : AS_FLOAT(v);
  return VM_FLOAT(cosh(val));
}

VMValue aot_math_tanh(VMValue v) {
  double val = IS_INT(v) ? (double)AS_INT(v) : AS_FLOAT(v);
  return VM_FLOAT(tanh(val));
}

VMValue aot_math_cbrt(VMValue v) {
  double val = IS_INT(v) ? (double)AS_INT(v) : AS_FLOAT(v);
  return VM_FLOAT(cbrt(val));
}

VMValue aot_math_hypot(VMValue x, VMValue y) {
  double xv = IS_INT(x) ? (double)AS_INT(x) : AS_FLOAT(x);
  double yv = IS_INT(y) ? (double)AS_INT(y) : AS_FLOAT(y);
  return VM_FLOAT(hypot(xv, yv));
}

VMValue aot_math_trunc(VMValue v) {
  double val = IS_INT(v) ? (double)AS_INT(v) : AS_FLOAT(v);
  return VM_FLOAT(trunc(val));
}

VMValue aot_math_fmod(VMValue x, VMValue y) {
  double xv = IS_INT(x) ? (double)AS_INT(x) : AS_FLOAT(x);
  double yv = IS_INT(y) ? (double)AS_INT(y) : AS_FLOAT(y);
  return VM_FLOAT(fmod(xv, yv));
}

// Random number generator (seeded on first call)
static int aot_random_seeded = 0;
VMValue aot_math_random(void) {
  if (!aot_random_seeded) {
    srand((unsigned int)time(NULL));
    aot_random_seeded = 1;
  }
  return VM_FLOAT((double)rand() / RAND_MAX);
}

VMValue aot_math_randint(VMValue minVal, VMValue maxVal) {
  if (!aot_random_seeded) {
    srand((unsigned int)time(NULL));
    aot_random_seeded = 1;
  }
  int64_t min = IS_INT(minVal) ? AS_INT(minVal) : (int64_t)AS_FLOAT(minVal);
  int64_t max = IS_INT(maxVal) ? AS_INT(maxVal) : (int64_t)AS_FLOAT(maxVal);
  return VM_INT(min + rand() % (max - min + 1));
}

VMValue aot_math_min(VMValue a, VMValue b) {
  double av = IS_INT(a) ? (double)AS_INT(a) : AS_FLOAT(a);
  double bv = IS_INT(b) ? (double)AS_INT(b) : AS_FLOAT(b);
  return VM_FLOAT(av < bv ? av : bv);
}

VMValue aot_math_max(VMValue a, VMValue b) {
  double av = IS_INT(a) ? (double)AS_INT(a) : AS_FLOAT(a);
  double bv = IS_INT(b) ? (double)AS_INT(b) : AS_FLOAT(b);
  return VM_FLOAT(av > bv ? av : bv);
}

// ============================================================================
// String Functions (AOT) - Extended
// ============================================================================

VMValue aot_string_upper(VMValue v) {
  if (!IS_STRING(v))
    return v;
  ObjString *s = AS_STRING(v);
  char *result = (char *)aot_arena_alloc(s->length + 1);
  for (int i = 0; i < s->length; i++) {
    result[i] = toupper((unsigned char)s->chars[i]);
  }
  result[s->length] = '\0';
  return VM_OBJ((Obj *)aot_allocate_string(result, s->length));
}

VMValue aot_string_lower(VMValue v) {
  if (!IS_STRING(v))
    return v;
  ObjString *s = AS_STRING(v);
  char *result = (char *)aot_arena_alloc(s->length + 1);
  for (int i = 0; i < s->length; i++) {
    result[i] = tolower((unsigned char)s->chars[i]);
  }
  result[s->length] = '\0';
  return VM_OBJ((Obj *)aot_allocate_string(result, s->length));
}

VMValue aot_string_contains(VMValue haystack, VMValue needle) {
  if (!IS_STRING(haystack) || !IS_STRING(needle))
    return VM_BOOL(0);
  ObjString *h = AS_STRING(haystack);
  ObjString *n = AS_STRING(needle);
  return VM_BOOL(strstr(h->chars, n->chars) != NULL);
}

VMValue aot_string_starts_with(VMValue str, VMValue prefix) {
  if (!IS_STRING(str) || !IS_STRING(prefix))
    return VM_BOOL(0);
  ObjString *s = AS_STRING(str);
  ObjString *p = AS_STRING(prefix);
  if (p->length > s->length)
    return VM_BOOL(0);
  return VM_BOOL(strncmp(s->chars, p->chars, p->length) == 0);
}

VMValue aot_string_ends_with(VMValue str, VMValue suffix) {
  if (!IS_STRING(str) || !IS_STRING(suffix))
    return VM_BOOL(0);
  ObjString *s = AS_STRING(str);
  ObjString *x = AS_STRING(suffix);
  if (x->length > s->length)
    return VM_BOOL(0);
  return VM_BOOL(strcmp(s->chars + s->length - x->length, x->chars) == 0);
}

VMValue aot_string_index_of(VMValue haystack, VMValue needle) {
  if (!IS_STRING(haystack) || !IS_STRING(needle))
    return VM_INT(-1);
  ObjString *h = AS_STRING(haystack);
  ObjString *n = AS_STRING(needle);
  const char *pos = strstr(h->chars, n->chars);
  if (pos == NULL)
    return VM_INT(-1);
  return VM_INT(pos - h->chars);
}

VMValue aot_string_substring(VMValue str, VMValue startVal, VMValue endVal) {
  if (!IS_STRING(str))
    return str;
  ObjString *s = AS_STRING(str);
  int start = IS_INT(startVal) ? (int)AS_INT(startVal) : 0;
  int end = IS_INT(endVal) ? (int)AS_INT(endVal) : s->length;

  if (start < 0)
    start = 0;
  if (end > s->length)
    end = s->length;
  if (start >= end)
    return VM_OBJ((Obj *)aot_allocate_string("", 0));

  int len = end - start;
  return VM_OBJ((Obj *)aot_allocate_string(s->chars + start, len));
}

VMValue aot_string_repeat(VMValue str, VMValue countVal) {
  if (!IS_STRING(str) || !IS_INT(countVal))
    return str;
  ObjString *s = AS_STRING(str);
  int count = (int)AS_INT(countVal);
  if (count <= 0)
    return VM_OBJ((Obj *)aot_allocate_string("", 0));

  int total_len = s->length * count;
  char *result = (char *)aot_arena_alloc(total_len + 1);
  for (int i = 0; i < count; i++) {
    memcpy(result + i * s->length, s->chars, s->length);
  }
  result[total_len] = '\0';
  return VM_OBJ((Obj *)aot_allocate_string(result, total_len));
}

VMValue aot_string_reverse(VMValue str) {
  if (!IS_STRING(str))
    return str;
  ObjString *s = AS_STRING(str);
  char *result = (char *)aot_arena_alloc(s->length + 1);
  for (int i = 0; i < s->length; i++) {
    result[i] = s->chars[s->length - 1 - i];
  }
  result[s->length] = '\0';
  return VM_OBJ((Obj *)aot_allocate_string(result, s->length));
}

VMValue aot_string_is_empty(VMValue str) {
  if (!IS_STRING(str))
    return VM_BOOL(1);
  return VM_BOOL(AS_STRING(str)->length == 0);
}

VMValue aot_string_count(VMValue haystack, VMValue needle) {
  if (!IS_STRING(haystack) || !IS_STRING(needle))
    return VM_INT(0);
  ObjString *h = AS_STRING(haystack);
  ObjString *n = AS_STRING(needle);
  if (n->length == 0)
    return VM_INT(0);

  int count = 0;
  const char *pos = h->chars;
  while ((pos = strstr(pos, n->chars)) != NULL) {
    count++;
    pos += n->length;
  }
  return VM_INT(count);
}

VMValue aot_string_capitalize(VMValue str) {
  if (!IS_STRING(str))
    return str;
  ObjString *s = AS_STRING(str);
  if (s->length == 0)
    return str;

  char *result = (char *)aot_arena_alloc(s->length + 1);
  result[0] = toupper((unsigned char)s->chars[0]);
  for (int i = 1; i < s->length; i++) {
    result[i] = tolower((unsigned char)s->chars[i]);
  }
  result[s->length] = '\0';
  return VM_OBJ((Obj *)aot_allocate_string(result, s->length));
}

VMValue aot_string_is_digit(VMValue str) {
  if (!IS_STRING(str))
    return VM_BOOL(0);
  ObjString *s = AS_STRING(str);
  if (s->length == 0)
    return VM_BOOL(0);
  for (int i = 0; i < s->length; i++) {
    if (!isdigit((unsigned char)s->chars[i]))
      return VM_BOOL(0);
  }
  return VM_BOOL(1);
}

VMValue aot_string_is_alpha(VMValue str) {
  if (!IS_STRING(str))
    return VM_BOOL(0);
  ObjString *s = AS_STRING(str);
  if (s->length == 0)
    return VM_BOOL(0);
  for (int i = 0; i < s->length; i++) {
    if (!isalpha((unsigned char)s->chars[i]))
      return VM_BOOL(0);
  }
  return VM_BOOL(1);
}

VMValue aot_string_join(VMValue sep, VMValue arr) {
  if (!IS_STRING(sep) || !IS_ARRAY(arr))
    return VM_OBJ((Obj *)aot_allocate_string("", 0));
  ObjString *separator = AS_STRING(sep);
  ObjArray *array = AS_ARRAY(arr);

  if (array->count == 0)
    return VM_OBJ((Obj *)aot_allocate_string("", 0));

  // Calculate total length
  int total_len = 0;
  for (int i = 0; i < array->count; i++) {
    if (IS_STRING(array->items[i])) {
      total_len += AS_STRING(array->items[i])->length;
    }
    if (i > 0)
      total_len += separator->length;
  }

  char *result = (char *)aot_arena_alloc(total_len + 1);
  int pos = 0;
  for (int i = 0; i < array->count; i++) {
    if (i > 0) {
      memcpy(result + pos, separator->chars, separator->length);
      pos += separator->length;
    }
    if (IS_STRING(array->items[i])) {
      ObjString *s = AS_STRING(array->items[i]);
      memcpy(result + pos, s->chars, s->length);
      pos += s->length;
    }
  }
  result[total_len] = '\0';
  return VM_OBJ((Obj *)aot_allocate_string(result, total_len));
}

// ============================================================================
// Time Functions (AOT) - Extended
// ============================================================================

VMValue aot_timestamp(void) { return VM_INT((int64_t)time(NULL)); }

VMValue aot_time_ms(void) {
  struct timeval tv;
  gettimeofday(&tv, NULL);
  return VM_INT((int64_t)(tv.tv_sec * 1000 + tv.tv_usec / 1000));
}

void aot_sleep(VMValue msVal) {
  if (!IS_INT(msVal))
    return;
  int64_t ms = AS_INT(msVal);
#ifdef _WIN32
  Sleep((DWORD)ms);
#else
  usleep((useconds_t)(ms * 1000));
#endif
}

// ============================================================================
// JSON Deserialization (AOT) - fromJson
// ============================================================================

// Forward declaration for recursive parsing
static VMValue parse_json_value(const char **p, const char *end);

static void skip_whitespace(const char **p, const char *end) {
  while (*p < end && isspace(**p))
    (*p)++;
}

static VMValue parse_json_string(const char **p, const char *end) {
  if (**p != '"')
    return VM_INT(0);
  (*p)++; // skip opening quote

  const char *start = *p;
  int len = 0;

  // Find end of string (handle escapes later)
  while (*p < end && **p != '"') {
    if (**p == '\\' && *p + 1 < end) {
      (*p) += 2; // skip escape sequence
      len += 1;  // escaped char counts as 1
    } else {
      (*p)++;
      len++;
    }
  }

  // Build unescaped string
  char *result = (char *)aot_arena_alloc(len + 1);
  const char *src = start;
  int i = 0;
  while (src < *p) {
    if (*src == '\\' && src + 1 < *p) {
      src++;
      switch (*src) {
      case 'n':
        result[i++] = '\n';
        break;
      case 't':
        result[i++] = '\t';
        break;
      case 'r':
        result[i++] = '\r';
        break;
      case '"':
        result[i++] = '"';
        break;
      case '\\':
        result[i++] = '\\';
        break;
      default:
        result[i++] = *src;
        break;
      }
      src++;
    } else {
      result[i++] = *src++;
    }
  }
  result[i] = '\0';

  if (*p < end)
    (*p)++; // skip closing quote

  return VM_OBJ((Obj *)aot_allocate_string(result, i));
}

static VMValue parse_json_number(const char **p, const char *end) {
  const char *start = *p;
  int is_float = 0;

  if (**p == '-')
    (*p)++;
  while (*p < end && isdigit(**p))
    (*p)++;

  if (*p < end && **p == '.') {
    is_float = 1;
    (*p)++;
    while (*p < end && isdigit(**p))
      (*p)++;
  }

  if (*p < end && (**p == 'e' || **p == 'E')) {
    is_float = 1;
    (*p)++;
    if (*p < end && (**p == '+' || **p == '-'))
      (*p)++;
    while (*p < end && isdigit(**p))
      (*p)++;
  }

  char buf[64];
  int len = *p - start;
  if (len >= 64)
    len = 63;
  strncpy(buf, start, len);
  buf[len] = '\0';

  if (is_float) {
    return VM_FLOAT(atof(buf));
  }
  return VM_INT(atoll(buf));
}

static VMValue parse_json_array(const char **p, const char *end) {
  if (**p != '[')
    return VM_INT(0);
  (*p)++; // skip [

  ObjArray *arr = (ObjArray *)aot_arena_alloc(sizeof(ObjArray));
  arr->obj.type = OBJ_ARRAY;
  arr->obj.arena_allocated = 1;
  arr->obj.next = NULL;
  arr->capacity = 8;
  arr->count = 0;
  arr->items = (VMValue *)aot_arena_alloc(sizeof(VMValue) * arr->capacity);

  skip_whitespace(p, end);

  while (*p < end && **p != ']') {
    VMValue val = parse_json_value(p, end);

    // Grow array if needed
    if (arr->count >= arr->capacity) {
      int new_cap = arr->capacity * 2;
      VMValue *new_items =
          (VMValue *)aot_arena_alloc(sizeof(VMValue) * new_cap);
      memcpy(new_items, arr->items, sizeof(VMValue) * arr->count);
      arr->items = new_items;
      arr->capacity = new_cap;
    }
    arr->items[arr->count++] = val;

    skip_whitespace(p, end);
    if (*p < end && **p == ',') {
      (*p)++;
      skip_whitespace(p, end);
    }
  }

  if (*p < end)
    (*p)++; // skip ]

  return VM_OBJ((Obj *)arr);
}

static VMValue parse_json_object(const char **p, const char *end) {
  if (**p != '{')
    return VM_INT(0);
  (*p)++; // skip {

  ObjObject *obj = (ObjObject *)aot_arena_alloc(sizeof(ObjObject));
  obj->obj.type = OBJ_OBJECT;
  obj->obj.arena_allocated = 1;
  obj->obj.next = NULL;
  obj->capacity = 8;
  obj->count = 0;
  obj->keys =
      (ObjString **)aot_arena_alloc(sizeof(ObjString *) * obj->capacity);
  obj->values = (VMValue *)aot_arena_alloc(sizeof(VMValue) * obj->capacity);

  skip_whitespace(p, end);

  while (*p < end && **p != '}') {
    // Parse key
    VMValue key_val = parse_json_string(p, end);
    if (!IS_STRING(key_val))
      break;

    skip_whitespace(p, end);
    if (*p < end && **p == ':')
      (*p)++;
    skip_whitespace(p, end);

    // Parse value
    VMValue val = parse_json_value(p, end);

    // Grow object if needed
    if (obj->count >= obj->capacity) {
      int new_cap = obj->capacity * 2;
      ObjString **new_keys =
          (ObjString **)aot_arena_alloc(sizeof(ObjString *) * new_cap);
      VMValue *new_vals = (VMValue *)aot_arena_alloc(sizeof(VMValue) * new_cap);
      memcpy(new_keys, obj->keys, sizeof(ObjString *) * obj->count);
      memcpy(new_vals, obj->values, sizeof(VMValue) * obj->count);
      obj->keys = new_keys;
      obj->values = new_vals;
      obj->capacity = new_cap;
    }

    obj->keys[obj->count] = AS_STRING(key_val);
    obj->values[obj->count] = val;
    obj->count++;

    skip_whitespace(p, end);
    if (*p < end && **p == ',') {
      (*p)++;
      skip_whitespace(p, end);
    }
  }

  if (*p < end)
    (*p)++; // skip }

  return VM_OBJ((Obj *)obj);
}

static VMValue parse_json_value(const char **p, const char *end) {
  skip_whitespace(p, end);
  if (*p >= end)
    return VM_INT(0);

  char c = **p;

  if (c == '"') {
    return parse_json_string(p, end);
  } else if (c == '[') {
    return parse_json_array(p, end);
  } else if (c == '{') {
    return parse_json_object(p, end);
  } else if (c == 't' && strncmp(*p, "true", 4) == 0) {
    *p += 4;
    return VM_BOOL(1);
  } else if (c == 'f' && strncmp(*p, "false", 5) == 0) {
    *p += 5;
    return VM_BOOL(0);
  } else if (c == 'n' && strncmp(*p, "null", 4) == 0) {
    *p += 4;
    return VM_INT(0);
  } else if (c == '-' || isdigit(c)) {
    return parse_json_number(p, end);
  }

  return VM_INT(0);
}

VMValue aot_from_json(VMValue jsonStr) {
  if (!IS_STRING(jsonStr))
    return VM_INT(0);

  ObjString *s = AS_STRING(jsonStr);
  const char *p = s->chars;
  const char *end = s->chars + s->length;

  return parse_json_value(&p, end);
}

// ============================================================================
// Input Functions (AOT) - Extended
// ============================================================================

VMValue aot_input_int(VMValue promptVal) {
  if (IS_STRING(promptVal)) {
    printf("%s", AS_STRING(promptVal)->chars);
    fflush(stdout);
  }

  char buf[64];
  if (fgets(buf, sizeof(buf), stdin)) {
    return VM_INT(atoll(buf));
  }
  return VM_INT(0);
}

VMValue aot_input_float(VMValue promptVal) {
  if (IS_STRING(promptVal)) {
    printf("%s", AS_STRING(promptVal)->chars);
    fflush(stdout);
  }

  char buf[64];
  if (fgets(buf, sizeof(buf), stdin)) {
    return VM_FLOAT(atof(buf));
  }
  return VM_FLOAT(0.0);
}

// ============================================================================
// Range Function (AOT)
// ============================================================================

VMValue aot_range(VMValue endVal) {
  int64_t end = IS_INT(endVal) ? AS_INT(endVal) : 0;
  if (end <= 0) {
    ObjArray *arr = (ObjArray *)aot_arena_alloc(sizeof(ObjArray));
    arr->obj.type = OBJ_ARRAY;
    arr->obj.arena_allocated = 1;
    arr->obj.next = NULL;
    arr->capacity = 0;
    arr->count = 0;
    arr->items = NULL;
    return VM_OBJ((Obj *)arr);
  }

  ObjArray *arr = (ObjArray *)aot_arena_alloc(sizeof(ObjArray));
  arr->obj.type = OBJ_ARRAY;
  arr->obj.arena_allocated = 1;
  arr->obj.next = NULL;
  arr->capacity = (int)end;
  arr->count = (int)end;
  arr->items = (VMValue *)aot_arena_alloc(sizeof(VMValue) * end);

  for (int64_t i = 0; i < end; i++) {
    arr->items[i] = VM_INT(i);
  }

  return VM_OBJ((Obj *)arr);
}

// ============================================================================
// SQLite Database Functions (AOT)
// ============================================================================
#include "../../lib/sqlite3/sqlite3.h"

// db_open(path) -> db_handle (int64)
VMValue aot_db_open(VMValue pathVal) {
  if (!IS_STRING(pathVal))
    return VM_INT(0);

  ObjString *path = AS_STRING(pathVal);
  sqlite3 *db = NULL;

  int rc = sqlite3_open(path->chars, &db);
  if (rc != SQLITE_OK) {
    if (db)
      sqlite3_close(db);
    return VM_INT(0);
  }

  // Return db pointer as int64
  return VM_INT((int64_t)(uintptr_t)db);
}

// db_close(db_handle) -> void
void aot_db_close(VMValue dbVal) {
  if (!IS_INT(dbVal))
    return;

  sqlite3 *db = (sqlite3 *)(uintptr_t)AS_INT(dbVal);
  if (db) {
    sqlite3_close(db);
  }
}

// db_execute(db_handle, sql) -> bool (success)
VMValue aot_db_execute(VMValue dbVal, VMValue sqlVal) {
  if (!IS_INT(dbVal) || !IS_STRING(sqlVal))
    return VM_BOOL(0);

  sqlite3 *db = (sqlite3 *)(uintptr_t)AS_INT(dbVal);
  ObjString *sql = AS_STRING(sqlVal);

  if (!db)
    return VM_BOOL(0);

  char *errMsg = NULL;
  int rc = sqlite3_exec(db, sql->chars, NULL, NULL, &errMsg);

  if (errMsg) {
    sqlite3_free(errMsg);
  }

  return VM_BOOL(rc == SQLITE_OK);
}

// db_query(db_handle, sql) -> array of objects (rows)
VMValue aot_db_query(VMValue dbVal, VMValue sqlVal) {
  if (!IS_INT(dbVal) || !IS_STRING(sqlVal)) {
    // Return empty array
    ObjArray *arr = (ObjArray *)aot_arena_alloc(sizeof(ObjArray));
    arr->obj.type = OBJ_ARRAY;
    arr->obj.arena_allocated = 1;
    arr->obj.next = NULL;
    arr->capacity = 0;
    arr->count = 0;
    arr->items = NULL;
    return VM_OBJ((Obj *)arr);
  }

  sqlite3 *db = (sqlite3 *)(uintptr_t)AS_INT(dbVal);
  ObjString *sql = AS_STRING(sqlVal);

  // Prepare result array
  ObjArray *result = (ObjArray *)aot_arena_alloc(sizeof(ObjArray));
  result->obj.type = OBJ_ARRAY;
  result->obj.arena_allocated = 1;
  result->obj.next = NULL;
  result->capacity = 16;
  result->count = 0;
  result->items =
      (VMValue *)aot_arena_alloc(sizeof(VMValue) * result->capacity);

  if (!db)
    return VM_OBJ((Obj *)result);

  sqlite3_stmt *stmt = NULL;
  int rc = sqlite3_prepare_v2(db, sql->chars, -1, &stmt, NULL);

  if (rc != SQLITE_OK || !stmt) {
    return VM_OBJ((Obj *)result);
  }

  int col_count = sqlite3_column_count(stmt);

  // Fetch rows
  while (sqlite3_step(stmt) == SQLITE_ROW) {
    // Create object for this row
    ObjObject *row = (ObjObject *)aot_arena_alloc(sizeof(ObjObject));
    row->obj.type = OBJ_OBJECT;
    row->obj.arena_allocated = 1;
    row->obj.next = NULL;
    row->capacity = col_count;
    row->count = 0;
    row->keys = (ObjString **)aot_arena_alloc(sizeof(ObjString *) * col_count);
    row->values = (VMValue *)aot_arena_alloc(sizeof(VMValue) * col_count);

    for (int i = 0; i < col_count; i++) {
      const char *col_name = sqlite3_column_name(stmt, i);
      row->keys[row->count] = aot_allocate_string(col_name, strlen(col_name));

      // Get value based on type
      int col_type = sqlite3_column_type(stmt, i);
      VMValue val;

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
        val = VM_OBJ((Obj *)aot_allocate_string(text, len));
        break;
      }
      case SQLITE_BLOB:
      case SQLITE_NULL:
      default:
        val = VM_INT(0); // null
        break;
      }

      row->values[row->count] = val;
      row->count++;
    }

    // Grow result array if needed
    if (result->count >= result->capacity) {
      int new_cap = result->capacity * 2;
      VMValue *new_items =
          (VMValue *)aot_arena_alloc(sizeof(VMValue) * new_cap);
      memcpy(new_items, result->items, sizeof(VMValue) * result->count);
      result->items = new_items;
      result->capacity = new_cap;
    }

    result->items[result->count++] = VM_OBJ((Obj *)row);
  }

  sqlite3_finalize(stmt);

  return VM_OBJ((Obj *)result);
}

// db_last_insert_id(db_handle) -> int64
VMValue aot_db_last_insert_id(VMValue dbVal) {
  if (!IS_INT(dbVal))
    return VM_INT(0);

  sqlite3 *db = (sqlite3 *)(uintptr_t)AS_INT(dbVal);
  if (!db)
    return VM_INT(0);

  return VM_INT(sqlite3_last_insert_rowid(db));
}

// db_error(db_handle) -> string
VMValue aot_db_error(VMValue dbVal) {
  if (!IS_INT(dbVal))
    return VM_OBJ((Obj *)aot_allocate_string("Invalid handle", 14));

  sqlite3 *db = (sqlite3 *)(uintptr_t)AS_INT(dbVal);
  if (!db)
    return VM_OBJ((Obj *)aot_allocate_string("No database", 11));

  const char *err = sqlite3_errmsg(db);
  return VM_OBJ((Obj *)aot_allocate_string(err, strlen(err)));
}

// ============================================================================
// Type Checking Functions (AOT)
// ============================================================================

VMValue aot_typeof(VMValue v) {
  const char *type_name;
  int len;

  switch (v.type) {
  case VM_VAL_INT:
    type_name = "int";
    len = 3;
    break;
  case VM_VAL_FLOAT:
    type_name = "float";
    len = 5;
    break;
  case VM_VAL_BOOL:
    type_name = "bool";
    len = 4;
    break;
  case VM_VAL_OBJ:
    if (IS_STRING(v)) {
      type_name = "string";
      len = 6;
    } else if (IS_ARRAY(v)) {
      type_name = "array";
      len = 5;
    } else if (IS_OBJECT(v)) {
      type_name = "object";
      len = 6;
    } else {
      type_name = "object";
      len = 6;
    }
    break;
  default:
    type_name = "null";
    len = 4;
  }

  return VM_OBJ((Obj *)aot_allocate_string(type_name, len));
}

VMValue aot_is_int(VMValue v) { return VM_BOOL(IS_INT(v)); }

VMValue aot_is_float(VMValue v) { return VM_BOOL(IS_FLOAT(v)); }

VMValue aot_is_string(VMValue v) { return VM_BOOL(IS_STRING(v)); }

VMValue aot_is_array(VMValue v) { return VM_BOOL(IS_ARRAY(v)); }

VMValue aot_is_object(VMValue v) { return VM_BOOL(IS_OBJECT(v)); }

VMValue aot_is_bool(VMValue v) { return VM_BOOL(IS_BOOL(v)); }
