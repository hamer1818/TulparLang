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
  //   int type;      // offset 0  (4 bytes)
  //   union as;      // offset 8  (8 bytes, aligned to 8 on x86-64)
  // }
  // Total: 16 bytes with alignment padding
  // We model 'as' as i64 (largest member)
  // Note: LLVM will add implicit padding for alignment when isPacked=0
  LLVMTypeRef vm_val_elements[] = {
      LLVMInt32TypeInContext(ctx), // type
      LLVMArrayType(LLVMInt8TypeInContext(ctx),
                    4),           // Explicit Padding to force offset 8
      LLVMInt64TypeInContext(ctx) // as
  };
  LLVMStructSetBody(backend->vm_value_type, vm_val_elements, 3, 0);

  // --- ABI-safe return type for VMValue ---
  // LLVM uses sret for {i32, [4xi8], i64} but returns {i64, i64} in RAX:RDX
  // This matches GCC's ABI for returning VMValue structs
  LLVMTypeRef ret_pair_elements[] = {
      LLVMInt64TypeInContext(ctx), // first 8 bytes (type + padding)
      LLVMInt64TypeInContext(ctx)  // second 8 bytes (as union)
  };
  backend->ret_pair_type = LLVMStructTypeInContext(ctx, ret_pair_elements, 2, 0);

  // --- Define Obj Body ---
  // struct Obj {
  //   ObjType type;             // offset 0 (i32)
  //   padding 4 bytes           // offset 4
  //   struct Obj *next;         // offset 8 (ptr)
  //   uint8_t arena_allocated;  // offset 16
  //   padding 3 bytes           // offset 17
  //   int32_t ref_count;        // offset 20
  //   uint8_t is_moved;         // offset 24
  //   padding 7 bytes           // offset 25
  // }
  LLVMTypeRef obj_elements[] = {
      LLVMInt32TypeInContext(ctx),           // type (enum)
      LLVMArrayType(LLVMInt8TypeInContext(ctx), 4),
      LLVMPointerType(backend->obj_type, 0), // next
      LLVMInt8TypeInContext(ctx),            // arena_allocated
      LLVMArrayType(LLVMInt8TypeInContext(ctx), 3),
      LLVMInt32TypeInContext(ctx),           // ref_count
      LLVMInt8TypeInContext(ctx),            // is_moved
      LLVMArrayType(LLVMInt8TypeInContext(ctx), 7)
  };
  LLVMStructSetBody(backend->obj_type, obj_elements, 8, 0);

  // --- Define ObjString Body ---
  // struct ObjString {
  //   Obj obj;
  //   int length;
  //   int capacity;
  //   char *chars;
  //   uint32_t hash;
  //   padding 4 bytes
  // }
  LLVMTypeRef str_elements[] = {
      backend->obj_type,           // obj header
      LLVMInt32TypeInContext(ctx), // length
      LLVMInt32TypeInContext(ctx), // capacity
      backend->ptr_type,           // chars (char*)
      LLVMInt32TypeInContext(ctx), // hash
      LLVMArrayType(LLVMInt8TypeInContext(ctx), 4)
  };
  LLVMStructSetBody(backend->obj_string_type, str_elements, 6, 0);
}
