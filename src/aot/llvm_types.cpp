#include "llvm_types.hpp"
#include "../vm/obj_layout.h"   // Obj basligi boyu / tur genisligi (tek kaynak)
#include <llvm-c/Core.h>
#include <cstring>

LLVMTypeRef llvm_obj_type_ty(LLVMBackend *backend) {
  return LLVMIntTypeInContext(backend->context, 8 * TULPAR_OBJ_TYPE_SIZE);
}

LLVMValueRef llvm_load_obj_type(LLVMBackend *backend, LLVMValueRef objp,
                                const char *name) {
  // Tur alani @0: GEP gerekmez, isaretcinin kendisinden yuklenir.
  return LLVMBuildLoad2(backend->builder, llvm_obj_type_ty(backend), objp, name);
}

LLVMValueRef llvm_obj_type_const(LLVMBackend *backend, unsigned kind) {
  return LLVMConstInt(llvm_obj_type_ty(backend), kind, 0);
}

static LLVMMetadataRef tbaa_str(LLVMContextRef c, const char *s) {
  return LLVMMDStringInContext2(c, s, strlen(s));
}

// TBAA agacini kur. Iki yaprak: "header" (ObjArray alanlari + VMValue
// kuresel yuvalari) ve "elem" (dizi eleman deposu). Ikisi AYRI bellek:
// eleman deposu her zaman ayri bir malloc/arena blogu, asla basligin
// baytlari degil — yani bu ayrim dogru.
static void llvm_init_tbaa(LLVMBackend *backend) {
  LLVMContextRef c = backend->context;
  LLVMMetadataRef root_ops[] = {tbaa_str(c, "Tulpar TBAA")};
  LLVMMetadataRef root = LLVMMDNodeInContext2(c, root_ops, 1);
  LLVMMetadataRef ch_ops[] = {tbaa_str(c, "omnipotent char"), root};
  LLVMMetadataRef ch = LLVMMDNodeInContext2(c, ch_ops, 2);
  LLVMMetadataRef h_ops[] = {tbaa_str(c, "tulpar.header"), ch};
  LLVMMetadataRef h = LLVMMDNodeInContext2(c, h_ops, 2);
  LLVMMetadataRef e_ops[] = {tbaa_str(c, "tulpar.elem"), ch};
  LLVMMetadataRef e = LLVMMDNodeInContext2(c, e_ops, 2);
  LLVMMetadataRef zero = LLVMValueAsMetadata(
      LLVMConstInt(LLVMInt64TypeInContext(c), 0, 0));
  LLVMMetadataRef ht[] = {h, h, zero};
  LLVMMetadataRef et[] = {e, e, zero};
  backend->tbaa_header = LLVMMDNodeInContext2(c, ht, 3);
  backend->tbaa_elem = LLVMMDNodeInContext2(c, et, 3);
  backend->tbaa_kind = LLVMGetMDKindIDInContext(c, "tbaa", 4);
}

void llvm_tbaa_tag(LLVMBackend *backend, LLVMValueRef inst, int is_elem) {
  if (!inst || !backend->tbaa_kind) return;
  LLVMSetMetadata(inst, backend->tbaa_kind,
                  LLVMMetadataAsValue(backend->context,
                                      is_elem ? backend->tbaa_elem
                                              : backend->tbaa_header));
}

void llvm_init_types(LLVMBackend *backend) {
  LLVMContextRef ctx = backend->context;
  llvm_init_tbaa(backend);

  // Basic types
  backend->ptr_type = LLVMPointerType(LLVMInt8TypeInContext(ctx), 0);

  // --- Create Named Structs (Opaque first for recursion) ---

  // struct Obj
  backend->obj_type = LLVMStructCreateNamed(ctx, "struct.Obj");

  // struct VMValue
  backend->vm_value_type = LLVMStructCreateNamed(ctx, "struct.VMValue");

  // struct ObjString
  backend->obj_string_type = LLVMStructCreateNamed(ctx, "struct.ObjString");

  // struct ObjArray — dizi erisiminin SATIR ICI hizli yolu bu tip uzerinden
  // GEP yapiyor. Obj basligi 2026-10-02'den beri her hedefte 8 bayt
  // (src/vm/obj_layout.h); alanlarin geri kalani isaretci boyuna bagli:
  //   64-bit: count@8 capacity@12 items_@16 idata@24 elem_bits@32, sizeof 40
  //   wasm32: count@8 capacity@12 items_@16 idata@20 elem_bits@24, sizeof 28
  // (Eskiden baslik 64-bit'te 32, wasm32'de 20 bayttı ve dolgu hedefe gore
  // secilen elle yazilmis bir sayiydi — 28 / 16.) Basligi basindaki tur
  // alani + opak dolgu olarak modelliyoruz: OBJ_ARRAY denetimi icin yalnizca
  // o alan lazim. Ofset kilidi runtime_bindings.cpp static_assert'lerinde.
  const unsigned obj_header_pad = TULPAR_OBJ_HEADER_SIZE - TULPAR_OBJ_TYPE_SIZE;
  backend->obj_array_type = LLVMStructCreateNamed(ctx, "struct.ObjArray");
  LLVMTypeRef obj_arr_elements[] = {
      llvm_obj_type_ty(backend),                                // obj.type   @0
      LLVMArrayType(LLVMInt8TypeInContext(ctx), obj_header_pad), // baslik kalani
      LLVMInt32TypeInContext(ctx),                              // count
      LLVMInt32TypeInContext(ctx),                              // capacity
      LLVMPointerType(LLVMInt8TypeInContext(ctx), 0),           // items_
      LLVMPointerType(LLVMInt8TypeInContext(ctx), 0),           // idata
      LLVMInt32TypeInContext(ctx)                               // elem_bits
  };
  LLVMStructSetBody(backend->obj_array_type, obj_arr_elements, 7, 0);

  // struct ObjStructArray (P1.1) — `d[i].x` satir ici hizli yolu bunun
  // `count` ve `data` alanlarini GEP ile okuyor. ObjArray ile AYNI gerekce ve
  // ayni baslik dolgusu; duzen kilidi runtime_bindings.cpp'deki
  // static_assert'lerde.
  backend->obj_sarr_type = LLVMStructCreateNamed(ctx, "struct.ObjStructArray");
  LLVMTypeRef obj_sarr_elements[] = {
      llvm_obj_type_ty(backend),                                 // obj.type    @0
      LLVMArrayType(LLVMInt8TypeInContext(ctx), obj_header_pad),  // baslik kalani
      LLVMPointerType(LLVMInt8TypeInContext(ctx), 0),             // type_name
      LLVMPointerType(LLVMInt8TypeInContext(ctx), 0),             // field_names
      LLVMPointerType(LLVMInt8TypeInContext(ctx), 0),             // field_types
      LLVMInt32TypeInContext(ctx),                                // field_count
      LLVMInt32TypeInContext(ctx),                                // count
      LLVMInt32TypeInContext(ctx),                                // capacity
      LLVMInt32TypeInContext(ctx),                                // elem_size (K037; 64-bit'te dolgu boslugu)
      LLVMPointerType(LLVMInt8TypeInContext(ctx), 0)              // data
  };
  LLVMStructSetBody(backend->obj_sarr_type, obj_sarr_elements, 10, 0);

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
      // DOLGU `i32`, `[4 x i8]` DEGIL. Fark sadece yazim degil: dizi bicimi
      // LLVM'e "dort ayri bayt" diyor ve uretilen kodda her VMValue kopyasi
      // dort `movzbl` + dort bayt store'a aciliyordu — dilin HER YERINDE.
      // Elek kiyaslamasinin ic dongusunden okundu (2026-09-03):
      //     mov    (%r12),%edx        ; etiket
      //     movzbl 0x4(%r12),%esi     ┐
      //     movzbl 0x5(%r12),%edi     │ dolgu, tek tek
      //     movzbl 0x6(%r12),%r8d     │
      //     movzbl 0x7(%r12),%r9d     ┘
      //     mov    0x8(%r12),%rax     ; yuk
      // Duz i32 ile LLVM iki skaler goruyor. Olculdu (A/B, ayni makinede,
      // 9 tekrar x 2 tur): sieve en iyi 24.6/27.5 -> 21.9/22.0 ms.
      //
      // ⚠️ SART: dolgu SABITI struct'in kendisinden turetilmeli
      // (llvm_values.cpp'deki `llvm_vm_val_padding_zero`). Elle `[4 x i8]`
      // yazan bir sabit bu alana verilirse `LLVMConstNamedStruct` tip
      // uyusmazligini SESSIZCE `undef`e ceviriyor — hata vermiyor — ve
      // global'lere `{ i32 1, i32 undef, i64 ... }` yaziliyor. Tam olarak bu
      // olmustu: scene3d'nin 10 carpisma/fizik testi, hangi testin once
      // kostuguna gore degisen degerler yuzunden dusuyordu.
      LLVMInt32TypeInContext(ctx),  // dolgu (offset 8'i zorlamak icin)
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
  // struct Obj {                (src/vm/obj_layout.h, 2026-10-02: 8 bayt)
  //   ObjType type;             // offset 0 (uint8_t)
  //   uint8_t arena_allocated;  // offset 1
  //   uint8_t is_moved;         // offset 2
  //   uint8_t struct_tag;       // offset 3 (OBJ_OBJECT: kutulu struct adi)
  //   int32_t ref_count;        // offset 4
  // }
  LLVMTypeRef obj_elements[] = {
      llvm_obj_type_ty(backend),             // type (enum, i8)
      LLVMInt8TypeInContext(ctx),            // arena_allocated
      LLVMInt8TypeInContext(ctx),            // is_moved
      LLVMInt8TypeInContext(ctx),            // struct_tag
      LLVMInt32TypeInContext(ctx),           // ref_count
  };
  LLVMStructSetBody(backend->obj_type, obj_elements, 5, 0);

  // --- Define ObjString Body ---
  // struct ObjString {
  //   Obj obj;          // 8
  //   int length;       // @8
  //   uint32_t hash;    // @12
  //   char chars[];     // @16 — nesnenin ICINDE (2026-10-02), isaretci degil
  // }                   // sizeof 16, her hedefte
  // Codegen dizginin karakterlerine dogrudan dokunmuyor (tip yalniz isaretci
  // olarak kullaniliyor); boy runtime static_assert'leriyle kilitli.
  LLVMTypeRef str_elements[] = {
      backend->obj_type,           // obj header
      LLVMInt32TypeInContext(ctx), // length
      LLVMInt32TypeInContext(ctx), // hash
  };
  LLVMStructSetBody(backend->obj_string_type, str_elements, 3, 0);
}
