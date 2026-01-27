// Tulpar ARC (Automatic Reference Counting) Runtime
// High-performance memory management with Move Semantics

#ifndef TULPAR_ARC_H
#define TULPAR_ARC_H

#include <stdint.h>
#include <stdlib.h>

// Forward declarations from vm.h
typedef struct Obj Obj;

// ============================================================================
// ARC Core Functions
// ============================================================================

// Increment reference count
// Call when: assigning to a new variable, passing as parameter (non-move)
void arc_retain(Obj *obj);

// Decrement reference count and free if zero
// Call when: variable goes out of scope, reassigning variable
void arc_release(Obj *obj);

// Move ownership - transfer without incrementing ref_count
// Call when: move(x) expression, last use optimization
void arc_move(Obj **src);

// Check if object is still valid (not moved)
int arc_is_valid(Obj *obj);

// ============================================================================
// Type-Specific Free Functions
// ============================================================================

void arc_free_string(Obj *obj);
void arc_free_array(Obj *obj);
void arc_free_object(Obj *obj);

// ============================================================================
// Scope Management Helpers
// ============================================================================

// Called at end of scope to release all locals
void arc_scope_exit(Obj **objects, int count);

// ============================================================================
// Debug Functions
// ============================================================================

#ifdef TULPAR_DEBUG
void arc_print_stats(void);
int arc_get_live_count(void);
#endif

#endif // TULPAR_ARC_H
