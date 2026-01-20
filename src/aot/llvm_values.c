#include "llvm_backend.h"
#include "llvm_types.h"
#include <llvm-c/Core.h>
#include <stdint.h>

// VMValue Construction Helpers

// Create tagged INT value: {type: 0 (VM_VAL_INT), as: i64}
LLVMValueRef llvm_vm_val_int(LLVMBackend *backend, int64_t value) {
  LLVMValueRef fields[] = {
      LLVMConstInt(backend->int32_type, 0, 0), // VM_VAL_INT
      LLVMConstInt(backend->int_type, value, 0)};
  return LLVMConstNamedStruct(backend->vm_value_type, fields, 2);
}

// Create tagged BOOL value: {type: 2 (VM_VAL_BOOL), as: i64}
LLVMValueRef llvm_vm_val_bool(LLVMBackend *backend, int value) {
  LLVMValueRef fields[] = {
      LLVMConstInt(backend->int32_type, 2, 0), // VM_VAL_BOOL
      LLVMConstInt(backend->int_type, value ? 1 : 0, 0)};
  return LLVMConstNamedStruct(backend->vm_value_type, fields, 2);
}

// Create tagged INT value from runtime LLVMValueRef (not constant)
LLVMValueRef llvm_vm_val_int_val(LLVMBackend *backend, LLVMValueRef value) {
  LLVMValueRef s = LLVMGetUndef(backend->vm_value_type);
  s = LLVMBuildInsertValue(backend->builder, s,
                           LLVMConstInt(backend->int32_type, 0, 0), 0,
                           ""); // type = VM_VAL_INT
  s = LLVMBuildInsertValue(backend->builder, s, value, 1, ""); // value
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
                           "");                                   // type
  s = LLVMBuildInsertValue(backend->builder, s, cast_val, 1, ""); // as
  return s;
}

// Create tagged OBJ value: {type: 4 (VM_VAL_OBJ), as: ptrtoint(ptr)}
LLVMValueRef llvm_build_vm_val_obj(LLVMBackend *backend, LLVMValueRef obj_ptr) {
  LLVMValueRef ptr_as_int = LLVMBuildPtrToInt(backend->builder, obj_ptr,
                                              backend->int_type, "ptr_int");

  LLVMValueRef s = LLVMGetUndef(backend->vm_value_type);
  s = LLVMBuildInsertValue(backend->builder, s,
                           LLVMConstInt(backend->int32_type, 4, 0), 0,
                           "");                                     // type
  s = LLVMBuildInsertValue(backend->builder, s, ptr_as_int, 1, ""); // as
  return s;
}

// Check if a VMValue is truthy
// Returns i1 (boolean)
LLVMValueRef llvm_build_is_truthy(LLVMBackend *backend, LLVMValueRef vm_val) {
  // Extract type (index 0)
  LLVMValueRef type =
      LLVMBuildExtractValue(backend->builder, vm_val, 0, "type");

  // Extract payload (index 1) - this is i64
  LLVMValueRef payload =
      LLVMBuildExtractValue(backend->builder, vm_val, 1, "payload");

  // Create blocks for switch
  LLVMValueRef func = backend->current_function;
  LLVMBasicBlockRef entry_block = LLVMGetInsertBlock(backend->builder);
  LLVMBasicBlockRef bool_block = LLVMAppendBasicBlock(func, "is_bool");
  LLVMBasicBlockRef int_block = LLVMAppendBasicBlock(func, "is_int");
  LLVMBasicBlockRef float_block = LLVMAppendBasicBlock(func, "is_float");
  LLVMBasicBlockRef default_block = LLVMAppendBasicBlock(func, "is_default");
  LLVMBasicBlockRef merge_block = LLVMAppendBasicBlock(func, "is_merge");

  LLVMValueRef switch_inst =
      LLVMBuildSwitch(backend->builder, type, default_block, 3);

  LLVMAddCase(switch_inst, LLVMConstInt(backend->int32_type, 2, 0),
              bool_block); // BOOL
  LLVMAddCase(switch_inst, LLVMConstInt(backend->int32_type, 0, 0),
              int_block); // INT
  LLVMAddCase(switch_inst, LLVMConstInt(backend->int32_type, 1, 0),
              float_block); // FLOAT

  // BOOL Block
  LLVMPositionBuilderAtEnd(backend->builder, bool_block);
  LLVMValueRef bool_res =
      LLVMBuildICmp(backend->builder, LLVMIntNE, payload,
                    LLVMConstInt(backend->int_type, 0, 0), "bool_val");
  LLVMBuildBr(backend->builder, merge_block);
  bool_block = LLVMGetInsertBlock(backend->builder);

  // INT Block
  LLVMPositionBuilderAtEnd(backend->builder, int_block);
  LLVMValueRef int_res =
      LLVMBuildICmp(backend->builder, LLVMIntNE, payload,
                    LLVMConstInt(backend->int_type, 0, 0), "int_val");
  LLVMBuildBr(backend->builder, merge_block);
  int_block = LLVMGetInsertBlock(backend->builder);

  // FLOAT Block
  LLVMPositionBuilderAtEnd(backend->builder, float_block);
  LLVMValueRef float_val = LLVMBuildBitCast(backend->builder, payload,
                                            backend->float_type, "as_double");
  LLVMValueRef float_res =
      LLVMBuildFCmp(backend->builder, LLVMRealONE, float_val,
                    LLVMConstReal(backend->float_type, 0.0), "float_val");
  LLVMBuildBr(backend->builder, merge_block);
  float_block = LLVMGetInsertBlock(backend->builder);

  // Default Block
  LLVMPositionBuilderAtEnd(backend->builder, default_block);
  LLVMValueRef def_res =
      LLVMBuildICmp(backend->builder, LLVMIntNE, payload,
                    LLVMConstInt(backend->int_type, 0, 0), "def_val");
  LLVMBuildBr(backend->builder, merge_block);
  default_block = LLVMGetInsertBlock(backend->builder);

  // Merge
  LLVMPositionBuilderAtEnd(backend->builder, merge_block);
  LLVMValueRef phi =
      LLVMBuildPhi(backend->builder, backend->bool_type, "is_truthy");
  LLVMValueRef incoming_vals[] = {bool_res, int_res, float_res, def_res};
  LLVMBasicBlockRef incoming_blocks[] = {bool_block, int_block, float_block,
                                         default_block};
  LLVMAddIncoming(phi, incoming_vals, incoming_blocks, 4);

  return phi;
}

// Extract int64 from VMValue (safe if type is INT or BOOL)
LLVMValueRef llvm_extract_vm_val_int(LLVMBackend *backend,
                                     LLVMValueRef vm_val) {
  return LLVMBuildExtractValue(backend->builder, vm_val, 1, "extract_int");
}

// Extract pointer from VMValue (assumes it holds a pointer/obj)
LLVMValueRef llvm_extract_vm_val_ptr(LLVMBackend *backend,
                                     LLVMValueRef vm_val) {
  LLVMValueRef as_int =
      LLVMBuildExtractValue(backend->builder, vm_val, 1, "extract_ptr_int");
  return LLVMBuildIntToPtr(backend->builder, as_int, backend->ptr_type,
                           "extract_ptr"); // Return i8*
}
