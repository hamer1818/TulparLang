// BOLUMLU (PARALEL) NESNE URETIMI — llvm_bolum.cpp.
//
// Optimize edilmis TEK modul (O3 butun programda kostu; satir ici acma
// kaybolmaz) fonksiyon SIRASI korunarak K bitisik bolume ayrilir, her bolum
// kendi LLVMContext'inde, kendi is parcaciginda nesneye yazilir; nesneler
// sirayla linklenir. Ayrinti ve olcum: llvm_bolum.cpp basi.

#ifndef TULPAR_LLVM_BOLUM_H
#define TULPAR_LLVM_BOLUM_H

#include <llvm-c/Core.h>
#include <llvm-c/TargetMachine.h>
#include <string>
#include <vector>

// Bolum sayisi karari: TULPAR_AOT_BOLUM=<n> zorlar (1 = bolme, eski tek
// nesne yolu); yoksa modulun komut sayisindan (makineden BAGIMSIZ —
// cekirdek sayisi yalniz is parcacigi sayisini etkiler, nesneleri degil).
int tulpar_bolum_sayisi(LLVMModuleRef mod);

// `mod`u `bolum` parcaya ayirip yazar. Basarida 0 doner ve `nesneler`
// link sirasiyla doldurulur (ilk eleman `dosya`). Modul YERINDE degisir
// (bolumler arasi kullanilan yerel semboller disa acilir) — cagiran
// bundan sonra modulu yalniz yok etmeli. Hata: 1 ve `hata` dolu.
int tulpar_bolumlu_emit(LLVMModuleRef mod, const char *triple,
                        LLVMRelocMode reloc, const char *dosya, int bolum,
                        std::vector<std::string> &nesneler,
                        std::string &hata);

#endif
