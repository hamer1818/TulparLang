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
// Fonksiyon cagrisi tek basina yeterli sebep: `push`/`pop`/`insert` ve her
// kullanici fonksiyonu bir cagri, yani cagriyi yasaklamak sekil degistiren
// her yolu kapatiyor. Eleman yazmasi (`f[k] = 1`) sekli degistirmez.
#include "../parser/parser.hpp"
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
    // AST_FUNCTION_CALL, AST_AWAIT, AST_LAMBDA, AST_MATCH, AST_TRY_CATCH,
    // AST_THROW, AST_RETURN, AST_FOR_IN, AST_ARRAY_LITERAL, ... hepsi ya
    // cagri icerir ya da yeni kap uretir: hicbiri gerekli degil, hepsi risk.
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

static bool visit_kind_ok(ASTNode_C *n, void *) {
  return shape_safe_kind(n->type);
}

// Dongunun sekli degistiremedigi kanitlanabiliyor mu?
extern "C" int tulpar_loop_shape_stable(ASTNode_C *cond, ASTNode_C *body,
                                        ASTNode_C *incr) {
  return walk_all(cond, visit_kind_ok, nullptr) &&
         walk_all(body, visit_kind_ok, nullptr) &&
         walk_all(incr, visit_kind_ok, nullptr);
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
