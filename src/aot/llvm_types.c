#include "llvm_types.h"
#include <llvm-c/Core.h>

void llvm_init_types(LLVMBackend *backend) {
  LLVMContextRef ctx = backend->context;

  // Basic types
  backend->ptr_type = LLVMPointerType(LLVMInt8TypeInContext(ctx), 0);

  // --- Create Named Structs (Opaque first for recursion) ---

  // struct Obj
  backend->obj_type = LLVMStructCreateNamed(ctx, "struct.Obj");

  // struct VMValue
  backend->vm_value_type = LLVMStructCreateNamed(ctx, "struct.VMValue");

  // struct ObjString
  backend->obj_string_type = LLVMStructCreateNamed(ctx, "struct.ObjString");

  // --- Define VMValue Body ---
  // struct VMValue {
  //   int type;      // offset 0
  //   union as;      // offset 8 (aligned to 8)
  // }
  // We model 'as' as i64 (largest member)
  LLVMTypeRef vm_val_elements[] = {
      LLVMInt32TypeInContext(ctx), // type
      LLVMInt64TypeInContext(ctx)  // as (holds int/float bits or pointer)
  };
  LLVMStructSetBody(backend->vm_value_type, vm_val_elements, 2, 0);

  // --- Define Obj Body ---
  // struct Obj {
  //   ObjType type;         // offset 0
  //   struct Obj *next;     // offset 8 (aligned)
  //   uint8_t allocated;    // offset 16
  // }
  LLVMTypeRef obj_elements[] = {
      LLVMInt32TypeInContext(ctx),           // type (enum)
      LLVMPointerType(backend->obj_type, 0), // next
      LLVMInt8TypeInContext(ctx)             // arena_allocated
  };
  LLVMStructSetBody(backend->obj_type, obj_elements, 3, 0);

  // --- Define ObjString Body ---
  // struct ObjString {
  //   Obj obj;
  //   int length;
  //   int capacity;
  //   int ref_count;
  //   char *chars;
  //   uint32_t hash;
  // }
  LLVMTypeRef str_elements[] = {
      backend->obj_type,           // obj header
      LLVMInt32TypeInContext(ctx), // length
      LLVMInt32TypeInContext(ctx), // capacity
      LLVMInt32TypeInContext(ctx), // ref_count
      backend->ptr_type,           // chars (char*)
      LLVMInt32TypeInContext(ctx)  // hash
  };
  LLVMStructSetBody(backend->obj_string_type, str_elements, 6, 0);
}
