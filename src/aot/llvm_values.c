#include "llvm_backend.h"
#include "llvm_types.h"
#include <llvm-c/Core.h>
#include <stdint.h>

// VMValue Construction Helpers
// VMValue struct layout: {i32 type, i64 as} with implicit padding

// Create tagged INT value: {type: 0 (VM_VAL_INT), as: i64}
// Create tagged INT value: {type: 0 (VM_VAL_INT), pad, as: i64}
LLVMValueRef llvm_vm_val_int(LLVMBackend *backend, int64_t value) {
  LLVMValueRef padding =
      LLVMConstNull(LLVMArrayType(LLVMInt8TypeInContext(backend->context), 4));
  LLVMValueRef fields[] = {
      LLVMConstInt(backend->int32_type, 0, 0),  // VM_VAL_INT
      padding,                                  // Padding
      LLVMConstInt(backend->int_type, value, 0) // as
  };
  return LLVMConstNamedStruct(backend->vm_value_type, fields, 3);
}

// Create tagged BOOL value: {type: 2 (VM_VAL_BOOL), as: i64}
// Create tagged BOOL value: {type: 2 (VM_VAL_BOOL), pad, as: i64}
LLVMValueRef llvm_vm_val_bool(LLVMBackend *backend, int value) {
  LLVMValueRef padding =
      LLVMConstNull(LLVMArrayType(LLVMInt8TypeInContext(backend->context), 4));
  LLVMValueRef fields[] = {
      LLVMConstInt(backend->int32_type, 2, 0), // VM_VAL_BOOL
      padding, LLVMConstInt(backend->int_type, value ? 1 : 0, 0)}; // as
  return LLVMConstNamedStruct(backend->vm_value_type, fields, 3);
}

// Create tagged BOOL value from runtime i1/i64
LLVMValueRef llvm_vm_val_bool_val(LLVMBackend *backend, LLVMValueRef value) {
  LLVMValueRef as_i64 =
      LLVMBuildZExt(backend->builder, value, backend->int_type, "bool_zext");

  LLVMValueRef s = LLVMGetUndef(backend->vm_value_type);
  s = LLVMBuildInsertValue(backend->builder, s,
                           LLVMConstInt(backend->int32_type, 2, 0), 0, "");
  // Padding initialized? Default undef is fine for runtime logic, but let's be
  // safe if copied Actually InsertValue(val, 2) is enough, padding stays undef
  s = LLVMBuildInsertValue(backend->builder, s, as_i64, 2, "");
  return s;
}

// Create tagged INT value from runtime LLVMValueRef (not constant)
LLVMValueRef llvm_vm_val_int_val(LLVMBackend *backend, LLVMValueRef value) {
  LLVMValueRef s = LLVMGetUndef(backend->vm_value_type);
  s = LLVMBuildInsertValue(backend->builder, s,
                           LLVMConstInt(backend->int32_type, 0, 0), 0,
                           ""); // type = VM_VAL_INT
  s = LLVMBuildInsertValue(backend->builder, s, value, 2, ""); // as (index 2)
  return s;
}

// Create tagged FLOAT value: {type: 1 (VM_VAL_FLOAT), as: cast(double -> i64)}
// Note: We need a runtime bitcast since LLVM ConstStruct fields must match
// types
LLVMValueRef llvm_build_vm_val_float(LLVMBackend *backend,
                                     LLVMValueRef float_val) {
  LLVMValueRef cast_val = LLVMBuildBitCast(backend->builder, float_val,
                                           backend->int_type, "float_bits");

  LLVMValueRef s = LLVMGetUndef(backend->vm_value_type);
  s = LLVMBuildInsertValue(backend->builder, s,
                           LLVMConstInt(backend->int32_type, 1, 0), 0,
                           ""); // type
  s = LLVMBuildInsertValue(backend->builder, s, cast_val, 2,
                           ""); // as (index 2)
  return s;
}

// Create tagged OBJ value: {type: 4 (VM_VAL_OBJ), as: ptrtoint(ptr)}
LLVMValueRef llvm_build_vm_val_obj(LLVMBackend *backend, LLVMValueRef obj_ptr) {
  LLVMValueRef ptr_as_int = LLVMBuildPtrToInt(backend->builder, obj_ptr,
                                              backend->int_type, "ptr_int");

  LLVMValueRef s = LLVMGetUndef(backend->vm_value_type);
  s = LLVMBuildInsertValue(backend->builder, s,
                           LLVMConstInt(backend->int32_type, 4, 0), 0,
                           ""); // type
  s = LLVMBuildInsertValue(backend->builder, s, ptr_as_int, 2,
                           ""); // as (index 2)
  return s;
}

// Check if a VMValue is truthy
// Returns i1 (boolean)
// VMValue struct: {i32 type, i64 as}
// Simplified version: Just check if payload is non-zero
LLVMValueRef llvm_build_is_truthy(LLVMBackend *backend, LLVMValueRef vm_val) {
  // Extract payload (index 2) - this is i64 (as field)
  LLVMValueRef payload =
      LLVMBuildExtractValue(backend->builder, vm_val, 2, "payload");

  // Simple check: payload != 0
  // This works for INT, BOOL (directly), and FLOAT (non-zero bits)
  // Also works for OBJ (non-null pointer)
  return LLVMBuildICmp(backend->builder, LLVMIntNE, payload,
                       LLVMConstInt(backend->int_type, 0, 0), "is_truthy");
}

// Extract int64 from VMValue (safe if type is INT or BOOL)
// VMValue struct: {i32 type, i64 as}
LLVMValueRef llvm_extract_vm_val_int(LLVMBackend *backend,
                                     LLVMValueRef vm_val) {
  return LLVMBuildExtractValue(backend->builder, vm_val, 2, "extract_int");
}

// Extract pointer from VMValue (assumes it holds a pointer/obj)
LLVMValueRef llvm_extract_vm_val_ptr(LLVMBackend *backend,
                                     LLVMValueRef vm_val) {
  LLVMValueRef as_int =
      LLVMBuildExtractValue(backend->builder, vm_val, 2, "extract_ptr_int");
  return LLVMBuildIntToPtr(backend->builder, as_int, backend->ptr_type,
                           "extract_ptr"); // Return i8*
}
