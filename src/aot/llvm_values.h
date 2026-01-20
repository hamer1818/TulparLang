#ifndef LLVM_VALUES_H
#define LLVM_VALUES_H

#include "llvm_backend.h"
#include <llvm-c/Core.h>
#include <stdint.h>

// Helpers to build VMValue structs in IR
LLVMValueRef llvm_vm_val_int(LLVMBackend *backend, int64_t value);
LLVMValueRef llvm_vm_val_bool(LLVMBackend *backend, int value);
LLVMValueRef llvm_vm_val_int_val(LLVMBackend *backend, LLVMValueRef value);
LLVMValueRef llvm_build_vm_val_float(LLVMBackend *backend,
                                     LLVMValueRef float_val);
LLVMValueRef llvm_build_vm_val_obj(LLVMBackend *backend, LLVMValueRef obj_ptr);

LLVMValueRef llvm_build_is_truthy(LLVMBackend *backend, LLVMValueRef vm_val);
LLVMValueRef llvm_extract_vm_val_int(LLVMBackend *backend, LLVMValueRef vm_val);
LLVMValueRef llvm_extract_vm_val_ptr(LLVMBackend *backend, LLVMValueRef vm_val);

#endif
