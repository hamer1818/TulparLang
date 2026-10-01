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

#define AOT_FNREF_FP_OFF 48     // void (*fp)(VMValue *)  — kutulu giris
#define AOT_FNREF_ARITY_OFF 56  // int arity
#define AOT_FNREF_NFP_OFF 64    // void *nfp               — ciplak yerel int giris
#define AOT_FNREF_SIZE 72       // kayit boyu
#define AOT_FNREF_COUNT 1024    // havuz kayit sayisi

#endif
