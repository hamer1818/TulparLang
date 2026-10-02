#ifndef LLVM_TYPES_H
#define LLVM_TYPES_H

#include "llvm_backend.hpp"

// Initialize VM struct types (VMValue, Obj, ObjString)
void llvm_init_types(LLVMBackend *backend);

// Helper to get offset of fields (used for GEP)
// Note: These should match C runtime offsets
#define OFFSET_VMVALUE_TYPE 0
#define OFFSET_VMVALUE_AS 1

// (Obj / ObjString alan ofsetleri burada eskiden elle yaziliydi ve hicbir
// yer okumuyordu; Obj duzeni artik src/vm/obj_layout.h'de.)

#endif

// TBAA etiketi: is_elem=1 dizi eleman deposu, 0 baslik/VMValue yuvasi.
void llvm_tbaa_tag(LLVMBackend *backend, LLVMValueRef inst, int is_elem);

// Nesne basliginin TUR alani (Obj::type, @0). Genislik obj_layout.h'den:
// her tur sinavi bu iki yardimcidan gecer — kendi `i32` yuklemesini yazan
// bir yer basligin komsu baytlarini (arena_allocated, is_moved) da okur ve
// sinav sessizce tutmaz (yavas yol; dogru ama hizli yol kaybolur).
LLVMTypeRef llvm_obj_type_ty(LLVMBackend *backend);
LLVMValueRef llvm_load_obj_type(LLVMBackend *backend, LLVMValueRef objp,
                                const char *name);
LLVMValueRef llvm_obj_type_const(LLVMBackend *backend, unsigned kind);
