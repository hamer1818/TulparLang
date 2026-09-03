#include "llvm_types.hpp"
#include <llvm-c/Core.h>
#include <cstring>

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
  // GEP yapiyor. C tarafindaki duzen (olculdu):
  //   Obj obj;      // 32 bayt basik
  //   int count;    // @32
  //   int capacity; // @36
  //   VMValue *items_;   // @40  (NULL ise dizi KUTULANMAMIS)
  //   long long *idata;  // @48  (NULL ise dizi kutulu) -> sizeof 56
  // Basligi opak 28 baytlik dolgu + basindaki i32 (obj.type) olarak
  // modelliyoruz: OBJ_ARRAY denetimi icin yalnizca o alan lazim.
  // Duzen degisirse runtime_bindings.cpp'deki static_assert'ler derlemeyi
  // kirar — sessizce yanlis ofsete GEP yapilmasin diye.
  backend->obj_array_type = LLVMStructCreateNamed(ctx, "struct.ObjArray");
  LLVMTypeRef obj_arr_elements[] = {
      LLVMInt32TypeInContext(ctx),                    // obj.type   @0
      LLVMArrayType(LLVMInt8TypeInContext(ctx), 28),  // baslik kalani
      LLVMInt32TypeInContext(ctx),                    // count      @32
      LLVMInt32TypeInContext(ctx),                    // capacity   @36
      LLVMPointerType(LLVMInt8TypeInContext(ctx), 0), // items_     @40
      LLVMPointerType(LLVMInt8TypeInContext(ctx), 0)  // idata      @48 -> sizeof 56
  };
  LLVMStructSetBody(backend->obj_array_type, obj_arr_elements, 6, 0);

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
      backend->ptr_type,           // chars static_cast<char*>
      LLVMInt32TypeInContext(ctx), // hash
      LLVMArrayType(LLVMInt8TypeInContext(ctx), 4)
  };
  LLVMStructSetBody(backend->obj_string_type, str_elements, 6, 0);
}
