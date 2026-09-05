// Dongu-degismezi DIZI SEKLI onbellegi — cozumleme tarafi.
//
// Neden: `while (k <= n) { f[k] = 1; k = k + i; }` gibi bir dongude her
// erisim dizinin basligini YENIDEN okuyor (obj.type, count, idata). LLVM
// bunlari dongu disina tasiyamiyor, cunku govdede yavas yol cagrisi duruyor
// ve o cagri her seyi ezebilir. Olculdu (2026-09-03, elek N=5M): denetimleri
// tamamen kaldirmak 12,9 -> 9,1 ms; en pahalisi sinir denetimi (+2,4 ms) ve
// pahali olan denetimin KENDISI degil, `count`un BELLEKTEN okunmasi.
//
// Cozum: dizinin seklini dongu basinda BIR KEZ okuyup yerelde tutmak — Go'nun
// dilim basligini yazmacta tutmasiyla ayni fikir. Bunun guvenli olmasi icin
// dongu govdesinin sekli degistiremedigini kanitlamak gerekiyor.
//
// KANIT MUHAFAZAKAR: govdede/kosulda/artirmada beyaz listede olmayan TEK bir
// dugum varsa vazgeciyoruz. Beyaz liste bilincli: kara liste yanlis tarafa
// hata yapar (taniniamayan yeni bir dugum turu sessizce "guvenli" sayilir),
// beyaz liste ise en fazla optimizasyonu kacirir.
//
// Fonksiyon cagrisi kural olarak yeterli sebep: `push`/`pop`/`insert` ve her
// kullanici fonksiyonu bir cagri. TEK ISTISNA, cagrilan adin sekil
// degistiremedigi ELLE dogrulanmis bir yerlesik olmasi — bunu burasi degil,
// codegen'deki `shape_pure_call` karari veriyor (kullanici ayni adi
// tanimlamis olabilir; o zaman guvenli sayilmamali). Bu istisna sart:
// Tulpar'in en yaygin dongu kalibi `for (int i = 0; i < len(a); ...)` ve
// `len` bir cagri oldugu icin kanit her seferinde dusuyordu.
// Eleman yazmasi (`f[k] = 1`) sekli degistirmez.
#include "../parser/parser.hpp"
#include "llvm_array_shape.hpp"
#include <cstring>

// Govdede gorulmesine izin verilen dugumler. Buraya bir tur eklemeden once
// sorulacak soru: bu dugum bir DIZININ count/idata alanini degistirebilir mi?
static bool shape_safe_kind(ASTNodeType t) {
  switch (t) {
  case AST_INT_LITERAL:
  case AST_FLOAT_LITERAL:
  case AST_STRING_LITERAL:
  case AST_BOOL_LITERAL:
  case AST_NULL_LITERAL:
  case AST_IDENTIFIER:
  case AST_BINARY_OP:
  case AST_UNARY_OP:
  case AST_TERNARY:
  case AST_ARRAY_ACCESS:
  case AST_VARIABLE_DECL:
  case AST_ASSIGNMENT:
  case AST_COMPOUND_ASSIGN:
  case AST_INCREMENT:
  case AST_DECREMENT:
  case AST_IF:
  case AST_WHILE:
  case AST_FOR:
  case AST_BREAK:
  case AST_CONTINUE:
  case AST_BLOCK:
    return true;
  default:
    // AST_AWAIT, AST_LAMBDA, AST_MATCH, AST_TRY_CATCH,
    // AST_THROW, AST_RETURN, AST_FOR_IN, AST_ARRAY_LITERAL, ... hepsi ya
    // cagri icerir ya da yeni kap uretir: hicbiri gerekli degil, hepsi risk.
    // (AST_FUNCTION_CALL yukarida ayrica ele aliniyor.)
    return false;
  }
}

// ASTNode_C'nin TUM cocuk alanlari. Liste struct'tan mekanik olarak cikarildi
// (`grep -oE "struct ASTNode_C \*+[a-z_]+"`); alan eklenirse buraya da
// eklenmeli, yoksa gezici bir alt agaci atlar ve kanit delinir.
// Tamlik denetimi: tests/ast_child_fields_audit.py
static bool walk_all(ASTNode_C *n, bool (*visit)(ASTNode_C *, void *),
                     void *ctx) {
  if (!n) return true;
  if (!visit(n, ctx)) return false;
  ASTNode_C *singles[] = {n->left,       n->right,        n->body,
                          n->receiver,   n->callee,       n->condition,
                          n->then_branch, n->else_branch, n->init,
                          n->increment,  n->iterable,     n->return_value,
                          n->index,      n->try_block,    n->catch_block,
                          n->finally_block, n->throw_expr};
  for (ASTNode_C *c : singles)
    if (!walk_all(c, visit, ctx)) return false;
  struct { ASTNode_C **arr; int count; } lists[] = {
      {n->field_types_nodes, n->field_count},
      {n->field_defaults, n->field_count},
      {n->parameters, n->param_count},
      {n->statements, n->statement_count},
      {n->arguments, n->argument_count},
      {n->elements, n->element_count},
      {n->object_values, n->object_count}};
  for (auto &l : lists)
    if (l.arr)
      for (int i = 0; i < l.count; i++)
        if (!walk_all(l.arr[i], visit, ctx)) return false;
  return true;
}

struct KindCtx {
  TulparPureCallFn pure;
  void *ctx;
};

static bool visit_kind_ok(ASTNode_C *n, void *p) {
  KindCtx *k = (KindCtx *)p;
  if (n->type == AST_FUNCTION_CALL) {
    // Adsiz cagri (bir degerde tutulan kapanis) asla guvenli sayilmaz.
    if (!n->name || !k->pure) return false;
    return k->pure(n->name, k->ctx) != 0;
  }
  return shape_safe_kind(n->type);
}

// Dongunun sekli degistiremedigi kanitlanabiliyor mu?
extern "C" int tulpar_loop_shape_stable(ASTNode_C *cond, ASTNode_C *body,
                                        ASTNode_C *incr, TulparPureCallFn pure,
                                        void *ctx) {
  KindCtx k{pure, ctx};
  return walk_all(cond, visit_kind_ok, &k) &&
         walk_all(body, visit_kind_ok, &k) &&
         walk_all(incr, visit_kind_ok, &k);
}

struct NameCtx {
  const char *name;
  bool assigned;
};

static bool visit_assign_to(ASTNode_C *n, void *p) {
  NameCtx *c = (NameCtx *)p;
  // `f = ...` (dizinin KENDISI baska bir diziye baglaniyor) onbellegi bayat
  // birakir. `f[i] = ...` degil — orada hedef bir AST_ARRAY_ACCESS.
  if ((n->type == AST_ASSIGNMENT || n->type == AST_COMPOUND_ASSIGN ||
       n->type == AST_VARIABLE_DECL) &&
      n->name && strcmp(n->name, c->name) == 0 &&
      !(n->left && n->left->type == AST_ARRAY_ACCESS)) {
    c->assigned = true;
    return false;
  }
  return true;
}

// Dongu icinde `name` degiskeninin kendisi yeniden baglaniyor mu?
extern "C" int tulpar_loop_rebinds_name(ASTNode_C *cond, ASTNode_C *body,
                                        ASTNode_C *incr, const char *name) {
  NameCtx c{name, false};
  walk_all(cond, visit_assign_to, &c);
  walk_all(body, visit_assign_to, &c);
  walk_all(incr, visit_assign_to, &c);
  return c.assigned ? 1 : 0;
}

struct CollectCtx {
  const char **out;
  int max;
  int n;
};

static bool visit_indexed(ASTNode_C *n, void *p) {
  CollectCtx *c = (CollectCtx *)p;
  // `X[...]` — parser tabani ya node->name'e ya da node->left'e koyuyor.
  if (n->type == AST_ARRAY_ACCESS) {
    const char *base = nullptr;
    if (n->name) base = n->name;
    else if (n->left && n->left->type == AST_IDENTIFIER) base = n->left->name;
    if (base) {
      for (int i = 0; i < c->n; i++)
        if (strcmp(c->out[i], base) == 0) return true;
      if (c->n < c->max) c->out[c->n++] = base;
    }
  }
  return true;
}

// Dongude `X[...]` bicimde indekslenen degisken adlari.
extern "C" int tulpar_collect_indexed_names(ASTNode_C *cond, ASTNode_C *body,
                                            const char **out, int max) {
  CollectCtx c{out, max, 0};
  walk_all(cond, visit_indexed, &c);
  walk_all(body, visit_indexed, &c);
  return c.n;
}

struct LenCtx {
  const char *name;
  bool used;
};

static bool visit_len_of(ASTNode_C *n, void *p) {
  LenCtx *c = (LenCtx *)p;
  if (n->type == AST_FUNCTION_CALL && n->name &&
      (strcmp(n->name, "len") == 0 || strcmp(n->name, "length") == 0) &&
      n->argument_count >= 1 && n->arguments && n->arguments[0] &&
      n->arguments[0]->type == AST_IDENTIFIER && n->arguments[0]->name &&
      strcmp(n->arguments[0]->name, c->name) == 0) {
    c->used = true;
    return false;
  }
  return true;
}

// Govdede ELEMAN YAZMASI var mi? (`a[i] = ...`, `a[i] += ...`, `a[i]++`)
//
// Bu, sinir denetimi elemenin YUK TASIYAN kosulu. Kutusuz bir diziye int
// OLMAYAN bir deger yazmak (`a[i] = 2.5`) diziyi KUTULUYOR: idata free
// ediliyor, items_ ayriliyor. Onbellekteki idata o an sarkiyor ve bugun
// bunu yalniz yavas yoldaki tazeleme kurtariyor (bkz. Tuzaklar 6l).
// Bekcisiz hizli surumde tazeleme YOK, yani yazma da olmamali.
//
// Yazma HANGI ISIMDEN oldugu onemsiz: `array b = a;` ikisini ayni diziye
// bagliyor, yani `b[i] = 2.5` bizim `a`mizi da kutular. O yuzden soru
// "bu diziye yaziliyor mu" degil, "govdede HERHANGI bir eleman yazmasi
// var mi".
static bool visit_any_elem_write(ASTNode_C *n, void *p) {
  bool *found = (bool *)p;
  if ((n->type == AST_ASSIGNMENT || n->type == AST_COMPOUND_ASSIGN ||
       n->type == AST_INCREMENT || n->type == AST_DECREMENT) &&
      n->left && n->left->type == AST_ARRAY_ACCESS) {
    *found = true;
    return false;
  }
  return true;
}

// `for (int i = C; i < len(a); i = i + K)` bicimi mi, ve `a[i]` icin SINIR
// DENETIMI GEREKSIZ mi?
//
// Kanit: dizi kutusuzken count == len (ikisi de gercek eleman sayisi).
// Kosul `i < len(a)` ustteki siniri, `i` C >= 0'dan baslayip yalniz K > 0
// ile artiyor olmasi alttaki siniri veriyor. Sekil kaniti zaten uzunlugun
// dongu boyunca degismedigini soyluyor. Yani 0 <= i < count.
//
// "Dizi kutusuz" kismi CAGIRAN tarafta, dongu basinda bir kez sinaniyor
// (count_slot != 0) ve dongu SURUMLENIYOR — LLVM'in kendi unswitch'i bu
// isi yapamiyor, olculdu (Performance.md).
//
// ELEMAN YAZMASI OLAN govde reddediliyor: bkz. visit_any_elem_write.
extern "C" int tulpar_loop_index_proven(ASTNode_C *init, ASTNode_C *cond,
                                        ASTNode_C *body, ASTNode_C *incr,
                                        const char *array_name,
                                        const char **ivar_out) {
  if (!init || !cond || !incr || !array_name) return 0;

  // init: `int i = C;`  (C >= 0)
  if (init->type != AST_VARIABLE_DECL || !init->name) return 0;
  ASTNode_C *iv = init->right;
  if (!iv || iv->type != AST_INT_LITERAL || iv->value.int_value < 0) return 0;
  const char *ivar = init->name;

  // cond: `i < len(a)`
  if (cond->type != AST_BINARY_OP || cond->op != TOKEN_LESS) return 0;
  ASTNode_C *lhs = cond->left, *rhs = cond->right;
  if (!lhs || lhs->type != AST_IDENTIFIER || !lhs->name ||
      strcmp(lhs->name, ivar) != 0)
    return 0;
  if (!rhs || rhs->type != AST_FUNCTION_CALL || !rhs->name) return 0;
  if (strcmp(rhs->name, "len") != 0 && strcmp(rhs->name, "length") != 0)
    return 0;
  if (rhs->argument_count != 1 || !rhs->arguments || !rhs->arguments[0] ||
      rhs->arguments[0]->type != AST_IDENTIFIER || !rhs->arguments[0]->name ||
      strcmp(rhs->arguments[0]->name, array_name) != 0)
    return 0;

  // incr: `i = i + K` (K > 0) ya da `i++`
  bool incr_ok = false;
  if (incr->type == AST_INCREMENT && incr->name && !incr->left &&
      strcmp(incr->name, ivar) == 0) {
    incr_ok = true;
  } else if (incr->type == AST_ASSIGNMENT && incr->name && !incr->left &&
             strcmp(incr->name, ivar) == 0 && incr->right &&
             incr->right->type == AST_BINARY_OP &&
             incr->right->op == TOKEN_PLUS) {
    ASTNode_C *a = incr->right->left, *b = incr->right->right;
    if (a && b && a->type == AST_IDENTIFIER && a->name &&
        strcmp(a->name, ivar) == 0 && b->type == AST_INT_LITERAL &&
        b->value.int_value > 0)
      incr_ok = true;
  }
  if (!incr_ok) return 0;

  // `i` govdede baska yerde ATANMAMALI (kosul/artim disinda).
  if (tulpar_loop_rebinds_name(nullptr, body, nullptr, ivar)) return 0;

  // Govdede HIC eleman yazmasi olmamali — kutulama riski.
  bool wrote = false;
  walk_all(body, visit_any_elem_write, &wrote);
  walk_all(cond, visit_any_elem_write, &wrote);
  walk_all(incr, visit_any_elem_write, &wrote);
  if (wrote) return 0;

  if (ivar_out) *ivar_out = ivar;
  return 1;
}

// Dongu `len(<ad>)` cagiriyor mu? Cagiriyorsa uzunlugu dongu basinda BIR KEZ
// hesaplayip yuvayi her zaman gecerli kiliyoruz; o zaman kullanim yerinde ne
// dal ne cagri kaliyor. Cagirmiyorsa bos yere bir aot_len cagrisi eklemenin
// anlami yok (ic ice dongude her girise bir cagri demek olurdu).
extern "C" int tulpar_loop_uses_len(ASTNode_C *cond, ASTNode_C *body,
                                    ASTNode_C *incr, const char *name) {
  LenCtx c{name, false};
  walk_all(cond, visit_len_of, &c);
  walk_all(body, visit_len_of, &c);
  walk_all(incr, visit_len_of, &c);
  return c.used ? 1 : 0;
}
