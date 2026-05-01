#ifndef LLVM_VALUES_H
#define LLVM_VALUES_H

#include "llvm_backend.hpp"
#include <llvm-c/Core.h>
#include <cstdint>

// Helpers to build VMValue structs in IR
LLVMValueRef llvm_vm_val_int(LLVMBackend *backend, int64_t value);
LLVMValueRef llvm_vm_val_bool(LLVMBackend *backend, int value);
LLVMValueRef llvm_vm_val_bool_val(LLVMBackend *backend, LLVMValueRef value);
LLVMValueRef llvm_vm_val_int_val(LLVMBackend *backend, LLVMValueRef value);
LLVMValueRef llvm_build_vm_val_float(LLVMBackend *backend,
                                     LLVMValueRef float_val);
LLVMValueRef llvm_build_vm_val_obj(LLVMBackend *backend, LLVMValueRef obj_ptr);

LLVMValueRef llvm_build_is_truthy(LLVMBackend *backend, LLVMValueRef vm_val);
LLVMValueRef llvm_extract_vm_val_int(LLVMBackend *backend, LLVMValueRef vm_val);
LLVMValueRef llvm_extract_vm_val_ptr(LLVMBackend *backend, LLVMValueRef vm_val);

// ABI-safe VMValue return conversion
LLVMValueRef llvm_convert_ret_pair_to_vmvalue(LLVMBackend *backend,
                                              LLVMValueRef ret_pair);
LLVMValueRef llvm_call_vmvalue_func(LLVMBackend *backend, LLVMValueRef func,
                                    LLVMValueRef *args, unsigned arg_count,
                                    const char *name);

// Build a function type for a runtime function that returns VMValue.
// Drop-in replacement for `LLVMFunctionType(backend->ret_pair_type, args,
// count, isvararg)`: signature is identical, only the *return ABI* changes.
// On SysV-ABI platforms (Linux, macOS) the function returns `{i64,i64}` —
// pair-of-i64 in registers. On Windows MS-x64 the C compiler emits 16-byte
// structs via a hidden `sret` pointer, so we synthesise
// `void(ptr sret, args...)` to match. Pair with `llvm_call_vmvalue_func`,
// which handles the per-platform call shape.
LLVMTypeRef llvm_make_vmvalue_func_type(LLVMBackend *backend,
                                        LLVMTypeRef *arg_types,
                                        unsigned arg_count,
                                        int is_vararg);

#endif
