#include "llvm_backend.hpp"
#include "llvm_types.hpp"
#include "../common/platform.h"
#include <llvm-c/Core.h>
#include <cstdint>
#include <cstdlib>

// VMValue Construction Helpers
// VMValue struct layout: {i32 type, [4 x i8] pad, i64 as}
//
// C runtime VMValueType enum (from vm.h):
//   VM_VAL_INT   = 0
//   VM_VAL_FLOAT = 1
//   VM_VAL_BOOL  = 2
//   VM_VAL_VOID  = 3
//   VM_VAL_OBJ   = 4

// Create tagged INT value: {type: 0 (VM_VAL_INT), pad, as: i64}
LLVMValueRef llvm_vm_val_int(LLVMBackend *backend, int64_t value) {
  LLVMValueRef padding =
      LLVMConstNull(LLVMArrayType(LLVMInt8TypeInContext(backend->context), 4));
  LLVMValueRef fields[] = {
      LLVMConstInt(backend->int32_type, 0, 0),  // VM_VAL_INT = 0
      padding,                                  // Padding
      LLVMConstInt(backend->int_type, value, 0) // as
  };
  return LLVMConstNamedStruct(backend->vm_value_type, fields, 3);
}

// Create tagged BOOL value: {type: 2 (VM_VAL_BOOL), pad, as: i64}
LLVMValueRef llvm_vm_val_bool(LLVMBackend *backend, int value) {
  LLVMValueRef padding =
      LLVMConstNull(LLVMArrayType(LLVMInt8TypeInContext(backend->context), 4));
  LLVMValueRef fields[] = {
      LLVMConstInt(backend->int32_type, 2, 0), // VM_VAL_BOOL = 2
      padding, LLVMConstInt(backend->int_type, value ? 1 : 0, 0)}; // as
  return LLVMConstNamedStruct(backend->vm_value_type, fields, 3);
}

// Create tagged BOOL value from runtime i1/i64
LLVMValueRef llvm_vm_val_bool_val(LLVMBackend *backend, LLVMValueRef value) {
  LLVMValueRef as_i64 =
      LLVMBuildZExt(backend->builder, value, backend->int_type, "bool_zext");

  LLVMValueRef s = LLVMGetUndef(backend->vm_value_type);
  s = LLVMBuildInsertValue(backend->builder, s,
                           LLVMConstInt(backend->int32_type, 2, 0), 0, ""); // VM_VAL_BOOL = 2
  s = LLVMBuildInsertValue(backend->builder, s, as_i64, 2, "");
  return s;
}

// Create tagged INT value from runtime LLVMValueRef (not constant)
LLVMValueRef llvm_vm_val_int_val(LLVMBackend *backend, LLVMValueRef value) {
  LLVMValueRef s = LLVMGetUndef(backend->vm_value_type);
  s = LLVMBuildInsertValue(backend->builder, s,
                           LLVMConstInt(backend->int32_type, 0, 0), 0,
                           ""); // VM_VAL_INT = 0
  s = LLVMBuildInsertValue(backend->builder, s, value, 2, ""); // as (index 2)
  return s;
}

// Create tagged FLOAT value: {type: 1 (VM_VAL_FLOAT), as: cast(double -> i64)}
LLVMValueRef llvm_build_vm_val_float(LLVMBackend *backend,
                                     LLVMValueRef float_val) {
  LLVMValueRef cast_val = LLVMBuildBitCast(backend->builder, float_val,
                                           backend->int_type, "float_bits");

  LLVMValueRef s = LLVMGetUndef(backend->vm_value_type);
  s = LLVMBuildInsertValue(backend->builder, s,
                           LLVMConstInt(backend->int32_type, 1, 0), 0,
                           ""); // VM_VAL_FLOAT = 1
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
  // Also works for OBJ (non-nullptr pointer)
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

// ============================================================================
// ABI-safe VMValue return conversion
// ============================================================================
// LLVM uses sret for {i32, [4xi8], i64} but returns {i64, i64} in registers.
// This helper converts a {i64, i64} return value to a VMValue struct.
// Use this after calling any C function that returns VMValue.

LLVMValueRef llvm_convert_ret_pair_to_vmvalue(LLVMBackend *backend,
                                              LLVMValueRef ret_pair) {
  // Extract the two i64 values from the return pair
  LLVMValueRef lo =
      LLVMBuildExtractValue(backend->builder, ret_pair, 0, "ret_lo");
  LLVMValueRef hi =
      LLVMBuildExtractValue(backend->builder, ret_pair, 1, "ret_hi");

  // Store pair into memory, then load as VMValue (type punning through
  // memory). Entry-hoisted: this helper is called once per VMValue-returning
  // runtime call, and on a SysV path that's basically every binary op /
  // get / set — cumulative per-iteration block-allocas overflow the stack
  // in long loops.
  LLVMValueRef temp =
      llvm_build_alloca_at_entry(backend, backend->ret_pair_type, "ret_tmp");
  LLVMBuildStore(backend->builder, ret_pair, temp);

  // Load as VMValue from the same memory location
  return LLVMBuildLoad2(backend->builder, backend->vm_value_type, temp,
                        "vmval_ret");
}

// Helper: Call a C function that returns VMValue, handling per-platform ABI.
// On SysV (Linux/macOS) the function is declared as `{i64,i64}(args)` and we
// extract the pair back into a VMValue via memory.
// On Windows the function is declared as `void(ptr sret, args)` — we
// allocate the output, prepend the sret pointer, call, then load. VMValue
// args are 16 bytes, which Win64 ABI passes by hidden pointer; we mirror that
// in IR with `byval(%vm_value_type)` so LLVM matches what gcc/clang produce
// for the C-side declaration `aot_foo(VMValue v)`. Without byval LLVM lowers
// the struct into register pairs (SysV-style) and the args land garbled.
LLVMValueRef llvm_call_vmvalue_func(LLVMBackend *backend, LLVMValueRef func,
                                    LLVMValueRef *args, unsigned arg_count,
                                    const char *name) {
#if PLATFORM_WINDOWS
  // sret slot + per-VMValue-arg byval slots are allocated for every
  // VMValue-returning runtime call on Windows. On a hot loop (e.g. bubble
  // sort: arr[j] > arr[j+1]) that's 3 allocas per inner-iter × 250K iters
  // ≈ 12 MB of cumulative stack growth → STATUS_STACK_BUFFER_OVERRUN.
  // Hoisting them to the function's entry block keeps the per-call cost
  // static at codegen time.
  LLVMValueRef out = llvm_build_alloca_at_entry(backend,
                                                backend->vm_value_type,
                                                "sret_out");
  unsigned total = arg_count + 1;
  LLVMValueRef *all = static_cast<LLVMValueRef*>(
      malloc(sizeof(LLVMValueRef) * total));
  all[0] = out;
  // Materialise each VMValue arg into a stack slot and pass its pointer.
  for (unsigned i = 0; i < arg_count; i++) {
    LLVMTypeRef arg_ty = LLVMTypeOf(args[i]);
    if (arg_ty == backend->vm_value_type) {
      LLVMValueRef slot = llvm_build_alloca_at_entry(
          backend, backend->vm_value_type, "vmarg_slot");
      LLVMBuildStore(backend->builder, args[i], slot);
      all[i + 1] = slot;
    } else {
      all[i + 1] = args[i];
    }
  }
  LLVMValueRef call = LLVMBuildCall2(
      backend->builder, LLVMGlobalGetValueType(func), func, all, total, "");
  // Tag the byval arg slots so LLVM emits the Win64 hidden-pointer copy.
  unsigned byval_kind =
      LLVMGetEnumAttributeKindForName("byval", 5);
  for (unsigned i = 0; i < arg_count; i++) {
    LLVMTypeRef arg_ty = LLVMTypeOf(args[i]);
    if (arg_ty == backend->vm_value_type) {
      LLVMAttributeRef byval = LLVMCreateTypeAttribute(
          backend->context, byval_kind, backend->vm_value_type);
      LLVMAddCallSiteAttribute(call, i + 2 /* +1 for sret, +1 for 1-based */,
                               byval);
    }
  }
  free(all);
  return LLVMBuildLoad2(backend->builder, backend->vm_value_type, out, name);
#else
  // Coerce each VMValue arg to {i64, i64} via memory aliasing — the
  // function type was declared with `ret_pair_type` for VMValue args
  // (see llvm_make_vmvalue_func_type SysV branch). The bitcast through
  // memory is the standard C "type-pun via pointer cast" pattern;
  // alloca + store + load lets LLVM keep it as a no-op when the
  // optimizer can prove both halves stay in registers.
  LLVMValueRef *coerced = static_cast<LLVMValueRef*>(
      malloc(sizeof(LLVMValueRef) * arg_count));
  for (unsigned i = 0; i < arg_count; i++) {
    LLVMTypeRef arg_ty = LLVMTypeOf(args[i]);
    if (arg_ty == backend->vm_value_type) {
      LLVMValueRef slot = llvm_build_alloca_at_entry(
          backend, backend->vm_value_type, "vmarg_coerce_slot");
      LLVMBuildStore(backend->builder, args[i], slot);
      coerced[i] = LLVMBuildLoad2(backend->builder, backend->ret_pair_type,
                                  slot, "vmarg_as_pair");
    } else {
      coerced[i] = args[i];
    }
  }
  LLVMValueRef ret_pair = LLVMBuildCall2(
      backend->builder, LLVMGlobalGetValueType(func), func, coerced,
      arg_count, name);
  free(coerced);
  return llvm_convert_ret_pair_to_vmvalue(backend, ret_pair);
#endif
}

// Helper: build a function type for a runtime function that returns VMValue.
LLVMTypeRef llvm_make_vmvalue_func_type(LLVMBackend *backend,
                                        LLVMTypeRef *arg_types,
                                        unsigned arg_count,
                                        int is_vararg) {
#if PLATFORM_WINDOWS
  // void(ptr sret, ptr byval(VMValue) args...)  — see llvm_call_vmvalue_func
  // for why VMValue args become pointers on Win64.
  unsigned total = arg_count + 1;
  LLVMTypeRef *all = static_cast<LLVMTypeRef*>(
      malloc(sizeof(LLVMTypeRef) * total));
  all[0] = backend->ptr_type;
  for (unsigned i = 0; i < arg_count; i++) {
    if (arg_types[i] == backend->vm_value_type) {
      all[i + 1] = backend->ptr_type;
    } else {
      all[i + 1] = arg_types[i];
    }
  }
  LLVMTypeRef ft =
      LLVMFunctionType(backend->void_type, all, total, is_vararg);
  free(all);
  return ft;
#else
  // SysV (Linux/macOS): for VMValue args, declare them as the coerced
  // {i64, i64} pair (`ret_pair_type`) — same shape we use for the return.
  // Why: passing {i32, [4xi8], i64} as a struct value works for one arg
  // (lands in rdi:rsi) but a second arg silently corrupts somewhere in
  // LLVM's SysV ABI lowering — `socket_server("127.0.0.1", port)` reaches
  // the C runtime with garbage VMValues and segfaults the very first
  // bind() / accept(). Coercing to the explicit `{i64, i64}` matches what
  // GCC emits for a 16-byte INTEGER+INTEGER struct argument and sidesteps
  // the lowering bug.
  LLVMTypeRef *all = static_cast<LLVMTypeRef*>(
      malloc(sizeof(LLVMTypeRef) * arg_count));
  for (unsigned i = 0; i < arg_count; i++) {
    if (arg_types[i] == backend->vm_value_type) {
      all[i] = backend->ret_pair_type;
    } else {
      all[i] = arg_types[i];
    }
  }
  LLVMTypeRef ft = LLVMFunctionType(backend->ret_pair_type, all, arg_count,
                                    is_vararg);
  free(all);
  return ft;
#endif
}
