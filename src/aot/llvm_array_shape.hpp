#ifndef TULPAR_LLVM_ARRAY_SHAPE_HPP
#define TULPAR_LLVM_ARRAY_SHAPE_HPP

#include "../parser/parser.hpp"

// Bir cagri adinin dizi seklini degistiremedigini soyleyen karar. Codegen
// veriyor, cunku karar iki seye birden bagli: adin elle dogrulanmis bir
// yerlesik olmasi VE kullanicinin ayni adi tanimlamamis olmasi.
typedef int (*TulparPureCallFn)(const char *name, void *ctx);

extern "C" int tulpar_loop_shape_stable(ASTNode_C *cond, ASTNode_C *body,
                                        ASTNode_C *incr, TulparPureCallFn pure,
                                        void *ctx);
extern "C" int tulpar_loop_rebinds_name(ASTNode_C *cond, ASTNode_C *body,
                                        ASTNode_C *incr, const char *name);
extern "C" int tulpar_collect_indexed_names(ASTNode_C *cond, ASTNode_C *body,
                                            const char **out, int max);

// Kanitin CALISMA ZAMANINDA sinanacak kismi (K215): hizli surumdeki eleman
// yazmalarinin i32'ye sigmasi icin gerekenler. Codegen ikisini de SURUM
// KOSULUNA ekler:
//   inv[0..n_inv)  dongu-degismezi adlar: `tag == INT && deger i32'ye sigar`;
//   count_limit    > 0 ise `count <= count_limit` (dongu degiskeni en fazla
//                  count_limit-1 — `a[i] = i * 2` gibi ifadeler icin), 0 sinirsiz.
#define TULPAR_WP_MAX_INV 4
struct TulparWriteProof {
  const char *inv[TULPAR_WP_MAX_INV];
  int n_inv;
  long long count_limit;
  // for kosulunun ust siniri `len(a)` degilse (yalniz for kaniti doldurur):
  //   bound_name   `i < n` / `i <= n` — codegen `n` int ve n (<|<=) count(a)
  //                 sinar (bound_incl: `<=`);
  //   bound_len_of `i < len(b)` — codegen b kutusuz, bos degil ve
  //                 count(b) <= count(a) sinar.
  const char *bound_name = nullptr;
  const char *bound_len_of = nullptr;
  int bound_incl = 0;
  // K201 (yalniz for kaniti): hizli surumde int kabul edilen `X[i]` okumalari
  // — codegen her X'in de kanitli oldugunu dogrular, degilse kaniti geri
  // alir; ve i32'ye sigdigi KANITLANAMAYAN ama govdenin ILK deyimi olan yazma
  // (`a[i] = a[i] + b[i]`): hizli surumde sigma sinavi, sigmazsa ayni turu
  // genel surumde bastan kosmaya gecis (deopt).
  const char *arr[TULPAR_WP_MAX_INV];
  int n_arr = 0;
  ASTNode_C *deopt_write = nullptr;
};

// `a[i]` sinir denetimi elenebilir mi? Bkz. tanimdaki kanit. `wp` NULL
// olabilir: o zaman calisma zamani sinavi gerektiren kanit reddedilir.
extern "C" int tulpar_loop_index_proven(ASTNode_C *init, ASTNode_C *cond,
                                        ASTNode_C *body, ASTNode_C *incr,
                                        const char *array_name,
                                        const char **ivar_out,
                                        TulparWriteProof *wp);

// `while (v <= UB) { ...; v = v + STEP; }` — BICIM dogrulamasi. Kanitin
// sayisal kismini (v>=0, STEP>0, UB<count) codegen dongu basinda SINIYOR.
// `step_out` NULL doner ve `step_const_out` dolar: adim bir int sabiti.
extern "C" int tulpar_while_index_proven(ASTNode_C *cond, ASTNode_C *body,
                                         const char **ivar_out,
                                         const char **ub_out,
                                         const char **step_out,
                                         long long *step_const_out,
                                         int *inclusive_out,
                                         TulparWriteProof *wp);

extern "C" int tulpar_loop_uses_len(ASTNode_C *cond, ASTNode_C *body,
                                    ASTNode_C *incr, const char *name);

// PERFORMANS IPUCU (K167): kanit NEDEN kurulamadi? Kanit fonksiyonlarinin
// adimlarini ayni sirayla yeniden yuruyup ilk dusen adimin kodunu verir
// (0 = kanitli). `detail_out` sinirin/adimin ADINI tasir (yoksa NULL).
// Karar degil yalniz TANI: kanitin kendisi yukaridaki fonksiyonlarda kalir.
enum TulparLoopWhy {
  TLW_OK = 0,
  TLW_INIT,          // for: sayac `int i = <sabit >= 0>` ile baslamiyor
  TLW_COND_OP,       // kosul `i < ...` biciminde degil
  TLW_BOUND_NAME,    // for: sinir bir AD (`i < n`), `len(a)` degil
  TLW_BOUND_OTHER,   // for: sinir baska dizinin uzunlugu / baska ifade
  TLW_INCR,          // for: artim `i++` / `i = i + K` (K > 0) degil
  TLW_REBIND,        // sayac govdede yeniden ataniyor
  TLW_WRITE,         // govdede int OLMAYAN (kutulayabilen) eleman yazmasi
  TLW_W_BOUND,       // while: sinir bir AD degil (`i < len(a)`)
  TLW_W_STEP,        // while: son deyim `v = v + ADIM` degil
  TLW_W_UB_REBIND,   // while: sinir ya da adim dongude degisiyor
};
extern "C" int tulpar_loop_index_why(ASTNode_C *init, ASTNode_C *cond, ASTNode_C *body,
                                     ASTNode_C *incr, const char *array_name,
                                     const char **detail_out);
extern "C" int tulpar_while_index_why(ASTNode_C *cond, ASTNode_C *body,
                                      const char **detail_out);
// Govde `<dizi>[<ad>]` erisimi iceriyor mu (ipucu yalniz o zaman anlamli).
extern "C" int tulpar_body_indexes_by(ASTNode_C *body, const char *array_name,
                                      const char *ivar);

// K064: `var q = mk(..)` bildirimindeki `q` fonksiyon `fn` icinde KACIYOR mu?
// 0 yalniz `q`nun her gecisi `q.<alan>` OKUMASIYSA (alan `fields`ta); ciplak
// kullanim, alan yazmasi, yeniden atama/bildirim, kapanis icinde gecis -> 1.
// Kacmayan `q` tipli yerel (yigin) olabilir: deger ve referans anlambilimi
// yalniz okumada ayni (bkz. .cpp).
extern "C" int tulpar_struct_var_escapes(ASTNode_C *fn, ASTNode_C *decl,
                                         const char *const *fields, int nfields);

// FLOAT DIZI DONGU SURUMU (2026-10-01): en icteki `for` dongusunde float
// dizi erisimlerinin afin indeks + kesin float yazma kaniti (bkz. .cpp).
// Plan yalniz BICIMI dogrular; sayisal kisim (B + j araligi, double depo,
// degismez adlarin FLOAT etiketi) codegen'de dongu basinda sinanir.
#define TULPAR_FV_MAX_ACC 64
#define TULPAR_FV_MAX_ARR 16
#define TULPAR_FV_MAX_INV 16
struct TulparFloatLoopPlan {
  const char *ivar;                          // dongu degiskeni j
  ASTNode_C *ub;                             // ust sinir (dongu-degismezi int)
  int incl;                                  // `j <= UB`
  const char *arr[TULPAR_FV_MAX_ARR];        // erisilen dizi adlari
  int n_arr;
  ASTNode_C *acc[TULPAR_FV_MAX_ACC];         // AST_ARRAY_ACCESS dugumleri
  ASTNode_C *acc_base[TULPAR_FV_MAX_ACC];    // indeksin degismez kismi (NULL = 0)
  int acc_has_j[TULPAR_FV_MAX_ACC];          // indeks j iceriyor mu
  int acc_arr[TULPAR_FV_MAX_ACC];            // arr[] icindeki sira
  const char *inv_float[TULPAR_FV_MAX_INV];  // dongu basinda FLOAT sinanacak adlar
  int n_acc;
  int n_inv;
  const char *why;                           // reddedildiyse sebep (teshis)
  // INT kipi (tulpar_int_loop_plan): inv_float adlari INT sinanir; tek eleman
  // yazmasi (varsa) deopt_write — govdenin ilk bildirim-disi deyimi.
  int is_int;
  ASTNode_C *deopt_write;
};
extern "C" int tulpar_float_loop_plan(ASTNode_C *init, ASTNode_C *cond,
                                      ASTNode_C *body, ASTNode_C *incr,
                                      TulparPureCallFn pure, void *ctx,
                                      TulparFloatLoopPlan *p);
// INT DIZI DONGU SURUMU: ayni plan `int[]` icin (bkz. .cpp).
extern "C" int tulpar_int_loop_plan(ASTNode_C *init, ASTNode_C *cond,
                                    ASTNode_C *body, ASTNode_C *incr,
                                    TulparPureCallFn pure, void *ctx,
                                    TulparFloatLoopPlan *p);
// FLOAT DIZI IC ICE SURUM (2026-10-01): dis dongu O'nun govdesi yalniz
// (planlanabilir) en ic `for` dongulerinden ve dizi erisimi icermeyen
// deyimlerden olusuyorsa, ic dongulerin dongu basi sinavlari O'nun girisine
// TASINABILIR: O'nun degiskeni i [I0, UBo) icinde, ic dongunun baslangici
// J0 = i + c (ya da O'da degismez), j'siz erisimin tabani i + c (ya da
// degismez). Sinav O basinda BIR KEZ; hizli O govdesinde ic donguler
// sinavsiz/genel kopyasiz uretilir (bkz. llvm_backend.cpp fvn_try_version).
#define TULPAR_FVN_MAX_INNER 4
struct TulparFloatNestPlan {
  const char *ivar;                                  // dis dongu degiskeni i
  ASTNode_C *ub;                                     // UBo (O'da degismez int)
  int incl;                                          // `i <= UBo`
  int n_inner;
  ASTNode_C *inner[TULPAR_FVN_MAX_INNER];            // ic AST_FOR dugumleri
  TulparFloatLoopPlan plan[TULPAR_FVN_MAX_INNER];    // her birinin plani
  int j0_dep[TULPAR_FVN_MAX_INNER];                  // J0 = i + j0_c mi
  long long j0_c[TULPAR_FVN_MAX_INNER];
  int b_dep[TULPAR_FVN_MAX_INNER][TULPAR_FV_MAX_ACC];        // taban = i + b_c mi
  long long b_c[TULPAR_FVN_MAX_INNER][TULPAR_FV_MAX_ACC];
  const char *why;
};
extern "C" int tulpar_float_nest_plan(ASTNode_C *init, ASTNode_C *cond,
                                      ASTNode_C *body, ASTNode_C *incr,
                                      TulparPureCallFn pure, void *ctx,
                                      TulparFloatNestPlan *np);

// Programdaki `float[]` bildirim/parametre adlari (surum karari icin ipucu).
extern "C" void tulpar_collect_float_array_decls(ASTNode_C *root,
                                                 void (*cb)(const char *, void *),
                                                 void *ctx);

// INT YEREL GOLGE SURUMU (2026-10-01): fonksiyon icindeki kutulu `int`
// yerellerin dongu boyunca native i64 golgesi (bkz. .cpp). Plan yalniz BICIMI
// dogrular; dis adlarin INT etiketi ve dizilerin kutusuz int deposu codegen'de
// dongu basinda sinanir.
#define TULPAR_IV_MAX_NAMES 32
#define TULPAR_IV_MAX_ARR 8
#define TULPAR_IV_MAX_NODES 256
#define TULPAR_IV_MAX_LOOP_NODES 1500   // daha buyuk dongu kopyalanmaz
enum TulparIvClass {
  TIV_NONE = 0,   // dongu basinda gorunmuyor
  TIV_NATIVE,     // native int yuvasi (her zaman INT)
  TIV_CAND,       // bu fonksiyonun kutulu `int` yereli/parametresi (golge adayi)
  TIV_ARR,        // `int[]` ipuclu kutulu degisken (depo sinavi adayi)
  TIV_BOXED,      // baska kutulu deger (bir diziye takma ad olabilir)
  TIV_STRUCT,     // tipli struct / struct dizisi / native float: takma ad olamaz
};
typedef int (*TulparIvClassFn)(const char *name, void *ctx);
struct TulparIntLocalPlan {
  const char *cand[TULPAR_IV_MAX_NAMES];   // dongu basinda golgelenecek adlar
  int n_cand;
  const char *arr[TULPAR_IV_MAX_ARR];      // deposu sinanacak diziler
  int n_arr;
  ASTNode_C *acc[TULPAR_IV_MAX_NODES];     // kesin-INT okumalar
  int n_acc;
  ASTNode_C *decl[TULPAR_IV_MAX_NODES];    // native yuvaya inecek bildirimler
  int n_decl;
  ASTNode_C *ewr[TULPAR_IV_MAX_NODES];     // sag tarafi kesin INT eleman yazmalari
  int n_ewr;
  int n_nodes;                             // dongunun dugum sayisi (teshis)
  const char *why;                         // reddedildiyse sebep (teshis)
};
extern "C" int tulpar_int_local_plan(ASTNode_C *loop, ASTNode_C *fn_body,
                                     TulparPureCallFn pure, TulparIvClassFn cls,
                                     void *ctx, TulparIntLocalPlan *p);
// Programdaki `int[]` bildirim/parametre adlari (V icin ipucu).
extern "C" void tulpar_collect_int_array_decls(ASTNode_C *root,
                                               void (*cb)(const char *, void *),
                                               void *ctx);

// STRUCT DIZISI DONGU SURUMU (2026-10-02): `for (i = E; i < UB; i += K)`
// govdesindeki `A[i]` erisimleri dongu basinda tek sinavla kanitlanir (bkz.
// .cpp). Plan yalniz BICIMI dogrular; i >= 0, UB' <= count(A) ve A'nin struct
// dizisi oldugu codegen'de dongu basinda sinanir (sv_try_version).
#define TULPAR_SV_MAX_ARR 8
struct TulparSarrLoopPlan {
  const char *ivar;                      // dongu degiskeni i
  ASTNode_C *ub;                         // ust sinir dugumu (sabit / ad / len(X))
  int incl;                              // `i <= UB`
  const char *arr[TULPAR_SV_MAX_ARR];    // `A[i]` bicimde erisilen adlar
  int n_arr;
  int n_acc;                             // `A[i]` erisim sayisi (teshis)
  int n_nodes;                           // govdenin dugum sayisi (teshis)
  const char *why;                       // reddedildiyse sebep (teshis)
};
extern "C" int tulpar_sarr_loop_plan(ASTNode_C *init, ASTNode_C *cond,
                                     ASTNode_C *body, ASTNode_C *incr,
                                     TulparPureCallFn pure, void *ctx,
                                     TulparSarrLoopPlan *p);
// Ayni kanit `while (i < UB) { ...; i = i + K; }` icin (artim son deyim).
extern "C" int tulpar_sarr_while_plan(ASTNode_C *cond, ASTNode_C *body, TulparPureCallFn pure,
                                      void *ctx, TulparSarrLoopPlan *p);

// Butun cocuk alanlarini (walk_all ile AYNI liste) on-sirayla gezer; `visit`
// 0 dondururse gezinti durur ve 0 doner.
extern "C" int tulpar_ast_walk(ASTNode_C *n, int (*visit)(ASTNode_C *, void *),
                               void *ctx);

#endif
