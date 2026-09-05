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

// `a[i]` sinir denetimi elenebilir mi? Bkz. tanimdaki kanit.
extern "C" int tulpar_loop_index_proven(ASTNode_C *init, ASTNode_C *cond,
                                        ASTNode_C *body, ASTNode_C *incr,
                                        const char *array_name,
                                        const char **ivar_out);

extern "C" int tulpar_loop_uses_len(ASTNode_C *cond, ASTNode_C *body,
                                    ASTNode_C *incr, const char *name);

#endif
