#include "../lexer/lexer.h"
#include "vm.h"
#define TYPE_BOOL_BOOL ((VM_VAL_BOOL << 4) | VM_VAL_BOOL)
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Helper for AOT string allocation (no GC)
static ObjString *aot_allocate_string(const char *chars, int length) {
  ObjString *str = (ObjString *)malloc(sizeof(ObjString));
  if (!str)
    return NULL;
  // Base Obj init
  str->obj.type = OBJ_STRING;
  str->obj.arena_allocated = 0;
  str->obj.next = NULL;

  str->length = length;

  // Simple FNV-1a hash
  uint32_t hash = 2166136261u;
  for (int i = 0; i < length; i++) {
    hash ^= (uint8_t)chars[i];
    hash *= 16777619;
  }
  str->hash = hash;

  str->chars = (char *)malloc(length + 1);
  if (str->chars) {
    memcpy(str->chars, chars, length);
    str->chars[length] = '\0';
  }
  return str;
}

// Wrapper for AOT string literals (matches vm_alloc_string signature)
ObjString *vm_alloc_string_aot(void *vm, const char *chars, int length) {
  return aot_allocate_string(chars, length);
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
    return VM_INT(0); // TODO: Return error or throw
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

// Generic Element Access (Array or Object)
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

// Alias for LLVM AOT
void print_value(VMValue value) {
  print_vm_value(value);
  printf("\n");
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

// push(array, value) - wrapper for AOT
void aot_array_push(VMValue arr_val, VMValue item) {
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
// JSON Serialization
// ============================================================================

typedef struct {
  char *data;
  size_t len;
  size_t cap;
} JSBuilder;

static void js_init(JSBuilder *b) {
  b->cap = 256;
  b->len = 0;
  b->data = (char *)malloc(b->cap);
  if (b->data)
    b->data[0] = '\0';
}

static void js_append(JSBuilder *b, const char *str) {
  if (!b->data)
    return;
  size_t len = strlen(str);
  if (b->len + len + 1 >= b->cap) {
    b->cap = (b->cap + len + 256) * 2;
    b->data = (char *)realloc(b->data, b->cap);
  }
  if (b->data) {
    memcpy(b->data + b->len, str, len);
    b->len += len;
    b->data[b->len] = '\0';
  }
}

static void js_escape_string(JSBuilder *b, const char *str, int len) {
  js_append(b, "\"");
  for (int i = 0; i < len; i++) {
    unsigned char c = (unsigned char)str[i];
    if (c == '"')
      js_append(b, "\\\"");
    else if (c == '\\')
      js_append(b, "\\\\");
    else if (c == '\b')
      js_append(b, "\\b");
    else if (c == '\f')
      js_append(b, "\\f");
    else if (c == '\n')
      js_append(b, "\\n");
    else if (c == '\r')
      js_append(b, "\\r");
    else if (c == '\t')
      js_append(b, "\\t");
    else if (c < 32) {
      char hex[7];
      snprintf(hex, sizeof(hex), "\\u%04x", c);
      js_append(b, hex);
    } else {
      char tmp[2] = {c, '\0'};
      js_append(b, tmp);
    }
  }
  js_append(b, "\"");
}

static void js_serialize(JSBuilder *b, VMValue v, int depth) {
  if (depth > 20) {
    js_append(b, "\"<max_depth>\"");
    return;
  }

  if (IS_INT(v)) {
    char tmp[64];
    snprintf(tmp, sizeof(tmp), "%lld", AS_INT(v));
    js_append(b, tmp);
  } else if (IS_FLOAT(v)) {
    char tmp[64];
    snprintf(tmp, sizeof(tmp), "%g", AS_FLOAT(v));
    js_append(b, tmp);
  } else if (IS_BOOL(v)) {
    js_append(b, AS_BOOL(v) ? "true" : "false");
  } else if (IS_STRING(v)) {
    ObjString *s = AS_STRING(v);
    js_escape_string(b, s->chars, s->length);
  } else if (IS_ARRAY(v)) {
    ObjArray *arr = AS_ARRAY(v);
    js_append(b, "[");
    for (int i = 0; i < arr->count; i++) {
      if (i > 0)
        js_append(b, ",");
      js_serialize(b, arr->items[i], depth + 1);
    }
    js_append(b, "]");
  } else if (IS_OBJECT(v)) {
    ObjObject *obj = AS_OBJECT(v);
    js_append(b, "{");
    for (int i = 0; i < obj->count; i++) {
      if (i > 0)
        js_append(b, ",");
      js_escape_string(b, obj->keys[i]->chars, obj->keys[i]->length);
      js_append(b, ":");
      js_serialize(b, obj->values[i], depth + 1);
    }
    js_append(b, "}");
  } else {
    js_append(b, "null");
  }
}

// toJson(VMValue) -> VMValue (String)
VMValue aot_to_json(VMValue value) {
  JSBuilder b;
  js_init(&b);
  if (!b.data)
    return VM_INT(0); // Alloc failed

  js_serialize(&b, value, 0);

  ObjString *res = aot_allocate_string(b.data, b.len);
  free(b.data);
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
    fprintf(stderr, "Uncaught exception\n");
    exit(1);
  }
  eh_exception = exception;
  eh_depth--;
  longjmp(eh_stack[eh_depth], 1);
}

VMValue aot_get_exception(void) { return eh_exception; }
