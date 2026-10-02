// Obj (ortak nesne basligi) DUZENI — TEK KAYNAK.
//
// Codegen ObjArray / ObjStructArray tiplerini basligi opak bir blok olarak
// modelleyip (`{ iN type, [HEADER - TYPE x i8] }`) alanlara GEP yapiyor ve
// tur sinavlarinda `obj.type`i dogrudan yukluyor (llvm_types.cpp
// `llvm_load_obj_type`). Runtime (vm.hpp `Obj`) ayni sayilari
// runtime_bindings.cpp'de static_assert ile kilitliyor; iki taraf da bu
// basligi okuyor. fnref_layout.h ile ayni ders: sayi iki dosyada ayri
// yazilinca biri kayip oteki kalinca hicbir kapi kirmizi olmuyordu.
//
// 2026-10-02: baslik 32 (wasm32'de 20) -> 8 bayt, her hedefte AYNI: tur tek
// bayt, `next` (olu VM'in nesne listesi) kaldirildi. Bkz. vm.hpp `Obj`.
#ifndef TULPAR_OBJ_LAYOUT_H
#define TULPAR_OBJ_LAYOUT_H

#define TULPAR_OBJ_HEADER_SIZE 8  // sizeof(Obj) — 64-bit ve wasm32
#define TULPAR_OBJ_TYPE_SIZE 1    // sizeof(Obj::type) — codegen bu genislikte yukler
// offsetof(ObjString, chars) == sizeof(ObjString): karakterler basligin hemen
// arkasinda (vm.hpp). Yerel eklenti cagrisi (K303) `str` parametresini bu
// ofsetle KOPYASIZ gecirir; runtime_bindings.cpp static_assert ile kilitli.
#define TULPAR_OBJSTRING_CHARS_OFFSET 16

#endif
