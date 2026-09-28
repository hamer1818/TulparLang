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

#endif
