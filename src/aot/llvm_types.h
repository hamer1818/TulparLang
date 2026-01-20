#ifndef LLVM_TYPES_H
#define LLVM_TYPES_H

#include "llvm_backend.h"

// Initialize VM struct types (VMValue, Obj, ObjString)
void llvm_init_types(LLVMBackend *backend);

// Helper to get offset of fields (used for GEP)
// Note: These should match C runtime offsets
#define OFFSET_VMVALUE_TYPE 0
#define OFFSET_VMVALUE_AS 1

#define OFFSET_OBJ_TYPE 0
#define OFFSET_OBJ_NEXT 1
#define OFFSET_OBJ_ARENA 2

#define OFFSET_OBJSTRING_OBJ 0
#define OFFSET_OBJSTRING_LENGTH 1
#define OFFSET_OBJSTRING_CAPACITY 2
#define OFFSET_OBJSTRING_REF 3
#define OFFSET_OBJSTRING_CHARS 4
#define OFFSET_OBJSTRING_HASH 5

#endif
