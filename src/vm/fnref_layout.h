// AOTFnRef (fonksiyon referansi havuz kaydi) DUZENI — TEK KAYNAK.
//
// Codegen'in satir ici call() yolu (llvm_backend.cpp "call(f, ...) SATIR ICI
// HIZLI YOL") kaydin alanlarina SABIT ofsetle GEP yapiyor; runtime
// (runtime_bindings.cpp) ayni sayilari static_assert ile kilitliyor. Eskiden
// sayilar iki dosyada ayri ayri yaziliydi ve runtime'in kilidi yalniz KENDI
// tarafini goruyordu: 2026-10-01'de ObjString 56 -> 48 bayt olunca codegen'in
// `nfp` ofseti eski 72'de birakilinca hicbir sey kirmizi olmadi — 72 bir
// sonraki kaydin basina (tip alani 0) dusuyor, `nfp` null okunuyor ve yerel
// int yolu sessizce hic tutmuyordu (dogru ama yavas). Artik iki taraf da bu
// basligi okuyor.
//
// Yalniz 64-bit hedefler: web (wasm32) bu yolu kullanmiyor.
#ifndef TULPAR_FNREF_LAYOUT_H
#define TULPAR_FNREF_LAYOUT_H

// 2026-10-02: Obj basligi 32 -> 8 bayt (obj_layout.h), ObjString 48 -> 24;
// kayit 72 -> 48 bayt.
#define AOT_FNREF_FP_OFF 24     // void (*fp)(VMValue *)  — kutulu giris
#define AOT_FNREF_ARITY_OFF 32  // int arity
#define AOT_FNREF_NFP_OFF 40    // void *nfp               — ciplak yerel int giris
#define AOT_FNREF_SIZE 48       // kayit boyu
#define AOT_FNREF_COUNT 1024    // havuz kayit sayisi

#endif
