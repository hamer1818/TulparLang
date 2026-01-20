
#include "vm.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Minimal VM stubs for LLVM testing

// Arena Stubs
Arena *arena_create(size_t initial_size) { return NULL; }
void arena_destroy(Arena *arena) {}
void *arena_alloc(Arena *arena, size_t size) { return malloc(size); }
void arena_reset(Arena *arena) {}

// VM Stubs
VM *vm_create() { return NULL; }
void vm_free(VM *vm) {}

// String Stubs
ObjString *vm_alloc_string(VM *vm, const char *chars, int length) {
  ObjString *str = (ObjString *)malloc(sizeof(ObjString));
  str->obj.type = OBJ_STRING;
  str->length = length;
  str->chars = (char *)malloc(length + 1);
  memcpy(str->chars, chars, length);
  str->chars[length] = '\0';
  return str;
}

// Print Stubs
void vm_print_value(VMValue value) {
  if (IS_INT(value))
    printf("%lld", AS_INT(value));
  else if (IS_FLOAT(value))
    printf("%g", AS_FLOAT(value));
  else if (IS_BOOL(value))
    printf(AS_BOOL(value) ? "true" : "false");
  else if (IS_STRING(value))
    printf("%s", AS_STRING(value)->chars);
  else
    printf("unknown");
}

// Array Stubs
ObjArray *vm_allocate_array(VM *vm) {
  ObjArray *arr = (ObjArray *)malloc(sizeof(ObjArray));
  arr->obj.type = OBJ_ARRAY;
  arr->count = 0;
  arr->capacity = 4;
  arr->items = (VMValue *)malloc(sizeof(VMValue) * 4);
  return arr;
}

void vm_array_push(VM *vm, ObjArray *array, VMValue value) {
  if (array->count >= array->capacity) {
    array->capacity *= 2;
    array->items =
        (VMValue *)realloc(array->items, sizeof(VMValue) * array->capacity);
  }
  array->items[array->count++] = value;
}

// Object Stubs
ObjObject *vm_allocate_object(VM *vm) {
  ObjObject *obj = (ObjObject *)malloc(sizeof(ObjObject));
  obj->obj.type = OBJ_OBJECT;
  obj->count = 0;
  obj->capacity = 4;
  obj->keys = (ObjString **)malloc(sizeof(ObjString *) * 4);
  obj->values = (VMValue *)malloc(sizeof(VMValue) * 4);
  return obj;
}
