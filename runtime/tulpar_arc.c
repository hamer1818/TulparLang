// Tulpar ARC (Automatic Reference Counting) Runtime - Implementation
// High-performance memory management with Move Semantics

#include "tulpar_arc.h"
#include "../src/vm/vm.h"
#include <stdio.h>
#include <string.h>

// ============================================================================
// Core Inline Function Implementations
// ============================================================================

void arc_retain(Obj *obj) {
  if (obj && !obj->arena_allocated) {
    obj->ref_count++;
#ifdef TULPAR_DEBUG
    arc_retain_count++;
#endif
  }
}

void arc_move(Obj **src) {
  if (src && *src) {
    (*src)->is_moved = 1;
    *src = NULL;  // Null out source
  }
}

int arc_is_valid(Obj *obj) {
  return obj != NULL && !obj->is_moved;
}

// ============================================================================
// Debug Statistics
// ============================================================================

#ifdef TULPAR_DEBUG
static int arc_alloc_count = 0;
static int arc_free_count = 0;
static int arc_retain_count = 0;
static int arc_release_count = 0;

void arc_print_stats(void) {
  printf("ARC Stats: alloc=%d, free=%d, retain=%d, release=%d, live=%d\n",
         arc_alloc_count, arc_free_count, arc_retain_count, arc_release_count,
         arc_alloc_count - arc_free_count);
}

int arc_get_live_count(void) {
  return arc_alloc_count - arc_free_count;
}
#endif

// ============================================================================
// Type-Specific Free Functions
// ============================================================================

void arc_free_string(Obj *obj) {
  if (!obj) return;
  ObjString *str = (ObjString *)obj;
  if (str->chars && !obj->arena_allocated) {
    free(str->chars);
  }
  if (!obj->arena_allocated) {
    free(str);
#ifdef TULPAR_DEBUG
    arc_free_count++;
#endif
  }
}

void arc_free_array(Obj *obj) {
  if (!obj) return;
  ObjArray *arr = (ObjArray *)obj;
  
  // Release all items in array
  if (arr->items) {
    for (int i = 0; i < arr->count; i++) {
      if (IS_OBJ(arr->items[i])) {
        arc_release(AS_OBJ(arr->items[i]));
      }
    }
    if (!obj->arena_allocated) {
      free(arr->items);
    }
  }
  
  if (!obj->arena_allocated) {
    free(arr);
#ifdef TULPAR_DEBUG
    arc_free_count++;
#endif
  }
}

void arc_free_object(Obj *obj) {
  if (!obj) return;
  ObjObject *object = (ObjObject *)obj;
  
  // Release all values
  if (object->values) {
    for (int i = 0; i < object->count; i++) {
      if (IS_OBJ(object->values[i])) {
        arc_release(AS_OBJ(object->values[i]));
      }
    }
    if (!obj->arena_allocated) {
      free(object->values);
    }
  }
  
  // Free keys (strings)
  if (object->keys) {
    for (int i = 0; i < object->count; i++) {
      if (object->keys[i]) {
        arc_release((Obj *)object->keys[i]);
      }
    }
    if (!obj->arena_allocated) {
      free(object->keys);
    }
  }
  
  if (!obj->arena_allocated) {
    free(object);
#ifdef TULPAR_DEBUG
    arc_free_count++;
#endif
  }
}

// ============================================================================
// Core ARC Functions
// ============================================================================

void arc_release(Obj *obj) {
  if (!obj) return;
  
  // Arena allocated objects are freed in bulk
  if (obj->arena_allocated) return;
  
#ifdef TULPAR_DEBUG
  arc_release_count++;
#endif
  
  obj->ref_count--;
  
  if (obj->ref_count <= 0) {
    // Free based on type
    switch (obj->type) {
      case OBJ_STRING:
        arc_free_string(obj);
        break;
      case OBJ_ARRAY:
        arc_free_array(obj);
        break;
      case OBJ_OBJECT:
        arc_free_object(obj);
        break;
      case OBJ_FUNCTION:
        // Functions are usually static, don't free
        break;
      default:
        // Unknown type - just free the base
        free(obj);
#ifdef TULPAR_DEBUG
        arc_free_count++;
#endif
        break;
    }
  }
}

// ============================================================================
// Scope Management
// ============================================================================

void arc_scope_exit(Obj **objects, int count) {
  for (int i = 0; i < count; i++) {
    if (objects[i] && !objects[i]->is_moved) {
      arc_release(objects[i]);
    }
  }
}

// ============================================================================
// VMValue Helpers for LLVM Backend
// ============================================================================

// Retain a VMValue if it contains an object
void arc_retain_vmvalue(VMValue *val) {
  if (val && IS_OBJ(*val)) {
    arc_retain(AS_OBJ(*val));
  }
}

// Release a VMValue if it contains an object
void arc_release_vmvalue(VMValue *val) {
  if (val && IS_OBJ(*val)) {
    arc_release(AS_OBJ(*val));
  }
}

// Move a VMValue - transfer ownership
void arc_move_vmvalue(VMValue *src, VMValue *dst) {
  if (!src || !dst) return;
  
  // Copy value
  *dst = *src;
  
  // Mark source as moved (don't release on scope exit)
  if (IS_OBJ(*src)) {
    AS_OBJ(*src)->is_moved = 1;
  }
  
  // Clear source
  src->type = VM_VAL_VOID;
  src->as.obj = NULL;
}
