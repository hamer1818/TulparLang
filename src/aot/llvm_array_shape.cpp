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

// Govdedeki ELEMAN YAZMALARI diziyi KUTULAYABILIR mi?
//
// Bu, sinir denetimi elemenin YUK TASIYAN kosulu. Kutusuz bir diziye int
// OLMAYAN bir deger yazmak (`a[i] = 2.5`) diziyi KUTULUYOR: idata free
// ediliyor, items_ ayriliyor. Onbellekteki idata o an sarkiyor ve bunu
// yalniz yavas yoldaki tazeleme kurtariyor (bkz. Tuzaklar 6l). Bekcisiz
// hizli surumde tazeleme YOK — dolayisiyla KUTULAMA da olmamali.
//
// Yazmanin HANGI ISIMDEN oldugu onemsiz: `array b = a;` ikisini ayni
// diziye bagliyor, yani `b[i] = 2.5` bizim `a`mizi da kutular. O yuzden
// soru "bu diziye yaziliyor mu" degil, "govdedeki HER eleman yazmasi
// KESIN tamsayi mi".
//
// 2026-09-06'ya kadar burasi "govdede eleman yazmasi VARSA vazgec"
// diyordu; o yuzden `for (i...) { a[i] = i; }` gibi DOLDURMA dongulerinin
// tamami bekcili kaliyor ve vektorlesemiyordu.

// Bir ifadenin degeri KESIN tamsayi mi?
//
// Yalniz "evet" cevabi yuk tasiyor; "hayir" en fazla optimizasyonu
// kaciriyor. O yuzden liste beyaz: taniniamayan her dugum "hayir".
//
// bool BILEREK DISARIDA: etiketi 0 (INT) degil 2, yani `a[i] = true`
// kutusuz diziye dogrudan yazilamaz.
struct IntCtx {
  const char *ivar;  // dongu degiskeni: int oldugu dongu BICIMINDEN belli
};

static bool expr_is_int(ASTNode_C *n, IntCtx *ic) {
  if (!n) return false;
  switch (n->type) {
  case AST_INT_LITERAL:
    return true;
  case AST_IDENTIFIER:
    // TEK kabul edilen ad DONGU DEGISKENI. Int oldugu dongunun BICIMINDEN
    // belli: init bir int sabiti, artim int sabiti ekliyor ve govde onu
    // yeniden baglamiyor — ucu de tulpar_loop_index_proven'in on kosulu.
    //
    // BASKA HICBIR AD KABUL EDILMIYOR, cunku turunu bilmenin yolu yok:
    // `int k` yazan bir yerel bile kutulu bir VMValue yuvasinda duruyor ve
    // icine calisma zamaninda float girebilir (parametreye cagiran float
    // gecebilir). Bir sure codegen'e "bu ad native i64 yuvasinda mi" diye
    // soran bir geri cagri vardi; OLCULDU (2026-09-06) ve pratikte HIC
    // "evet" demiyor — yalniz dar bicimli "native fonksiyon" yayicisinda
    // native yuva olusuyor. Sinanamayan bir kanit yolu tasimaktansa
    // kaldirildi.
    //
    // Genisletmenin dogru yolu bu degil: `a[i] = k` gibi dongu-DEGISMEZI
    // bir adin etiketi de dongu degismezidir, yani `tag(k) == INT` sinavi
    // dongu BASINA, surumleme kosuluna (`count_slot != 0` yanina)
    // eklenebilir. Kiyaslamalarin hicbiri buna bagli olmadigi icin
    // yapilmadi.
    return n->name && ic->ivar && strcmp(n->name, ic->ivar) == 0;
  case AST_UNARY_OP:
    return n->op == TOKEN_MINUS && expr_is_int(n->left, ic);
  case AST_BINARY_OP:
    switch (n->op) {
    // Tulpar'da int/int TAMSAYI bolme (`7 / 2 == 3`), yani `/` de int
    // koruyor. Karsilastirmalar bool uretiyor: listede yoklar.
    case TOKEN_PLUS:
    case TOKEN_MINUS:
    case TOKEN_MULTIPLY:
    case TOKEN_DIVIDE:
    case TOKEN_MODULO:
      return expr_is_int(n->left, ic) && expr_is_int(n->right, ic);
    default:
      return false;
    }
  default:
    return false;
  }
}

struct WriteCtx {
  IntCtx ic;
  bool unsafe;
};

static bool visit_elem_write_ok(ASTNode_C *n, void *p) {
  WriteCtx *w = (WriteCtx *)p;
  if (!n->left || n->left->type != AST_ARRAY_ACCESS) return true;
  switch (n->type) {
  case AST_INCREMENT:
  case AST_DECREMENT:
    // Kutusuz dizide eleman zaten int; int +-1 yine int.
    return true;
  case AST_ASSIGNMENT:
    if (expr_is_int(n->right, &w->ic)) return true;
    break;
  case AST_COMPOUND_ASSIGN:
    // `a[i] op= x`: sol taraf (kutusuz dizide) int, op int koruyor ve x
    // int ise sonuc int.
    switch (n->op) {
    case TOKEN_PLUS_EQUAL:
    case TOKEN_MINUS_EQUAL:
    case TOKEN_MULTIPLY_EQUAL:
    case TOKEN_DIVIDE_EQUAL:
    case TOKEN_MODULO_EQUAL:
      if (expr_is_int(n->right, &w->ic)) return true;
      break;
    default:
      break;
    }
    break;
  default:
    return true;   // eleman yazmasi degil
  }
  w->unsafe = true;
  return false;
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
// KUTULAYABILEN eleman yazmasi olan govde reddediliyor: bkz.
// visit_elem_write_ok.
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

  // HER eleman yazmasi KESIN tamsayi olmali — yoksa kutulama riski.
  WriteCtx wc{IntCtx{ivar}, false};
  walk_all(body, visit_elem_write_ok, &wc);
  walk_all(cond, visit_elem_write_ok, &wc);
  walk_all(incr, visit_elem_write_ok, &wc);
  if (wc.unsafe) return 0;

  if (ivar_out) *ivar_out = ivar;
  return 1;
}

// `while (v <= UB) { ... ; v = v + STEP; }` bicimi mi?
//
// NEDEN AYRI BIR KANIT: `for` bicimindeki kanit tamamen SOZDIZIMSEL —
// baslangic bir int sabiti, artim bir int sabiti, ust sinir `len(a)`.
// Elek'in ic dongusu ucunu de saglamiyor:
//
//     int k = i * i;                        // baslangic: ifade
//     while (k <= n) { f[k] = 1; k = k + i; }   // sinir n, adim i
//
// Ama uc buyuklugun de DONGU DEGISMEZI oldugu goruluyor. O zaman kanit
// sozdiziminden degil, dongu BASINDA BIR KEZ yapilan sinavdan gelebilir:
//
//     v >= 0  &&  STEP > 0  &&  UB < count      ->   0 <= v <= UB < count
//
// Ucu de dongu disinda, bir kez. Bu fonksiyon yalniz BICIMI dogruluyor ve
// isimleri disari veriyor; sinavi codegen uretiyor (surumleme kosulu).
//
// v'nin govdedeki TEK atamasi son ifade olmali. Ortada olsaydi, ondan
// SONRAKI erisimler artmis v ile UB'yi asabilirdi:
//     while (k <= n) { k = k + i; f[k] = 1; }   // f[n+i] okur — REDDEDILIR
static bool stmt_is_step(ASTNode_C *st, const char *ivar,
                         const char **step_name) {
  // `v = v + STEP`  (STEP bir AD; sabit adimli bicim zaten `for` kanitinda)
  if (!st || st->type != AST_ASSIGNMENT || !st->name || st->left) return false;
  if (strcmp(st->name, ivar) != 0) return false;
  ASTNode_C *r = st->right;
  if (!r || r->type != AST_BINARY_OP || r->op != TOKEN_PLUS) return false;
  ASTNode_C *a = r->left, *b = r->right;
  if (!a || !b || a->type != AST_IDENTIFIER || !a->name ||
      strcmp(a->name, ivar) != 0)
    return false;
  if (b->type != AST_IDENTIFIER || !b->name) return false;
  *step_name = b->name;
  return true;
}

extern "C" int tulpar_while_index_proven(ASTNode_C *cond, ASTNode_C *body,
                                         const char **ivar_out,
                                         const char **ub_out,
                                         const char **step_out,
                                         int *inclusive_out) {
  if (!cond || !body) return 0;

  // kosul: `v <= UB` ya da `v < UB`, ikisi de AD.
  if (cond->type != AST_BINARY_OP) return 0;
  int incl;
  if (cond->op == TOKEN_LESS_EQUAL) incl = 1;
  else if (cond->op == TOKEN_LESS) incl = 0;
  else return 0;
  ASTNode_C *l = cond->left, *r = cond->right;
  if (!l || l->type != AST_IDENTIFIER || !l->name) return 0;
  if (!r || r->type != AST_IDENTIFIER || !r->name) return 0;
  const char *ivar = l->name, *ub = r->name;
  if (strcmp(ivar, ub) == 0) return 0;

  // govde bir blok olmali ve SON ifadesi adim olmali.
  if (body->type != AST_BLOCK || body->statement_count < 1 ||
      !body->statements)
    return 0;
  const char *step = nullptr;
  if (!stmt_is_step(body->statements[body->statement_count - 1], ivar, &step))
    return 0;
  if (strcmp(step, ivar) == 0) return 0;

  // v BASKA hicbir yerde atanmamali. Son ifadeden ONCEKI her ifade taraniyor.
  //
  // Not: "adim SON ifade olmali" kurali boylece IKI yerden birden geliyor —
  // yukaridaki `stmt_is_step(son ifade)` ve bu tarama. Enjeksiyonla
  // dogrulandi (2026-09-06): yalniz birini kaldirmak testleri kizartmiyor,
  // cunku digeri ayni bicimi zaten reddediyor; IKISI birden kalkinca
  // `while (k <= ub) { k = k + step; a[k] = 3; }` bekcisiz uretiliyor ve
  // paket cokuyor. Yani bu bir gevseklik degil, kasitli cift kilit.
  for (int i = 0; i < body->statement_count - 1; i++)
    if (tulpar_loop_rebinds_name(nullptr, body->statements[i], nullptr, ivar))
      return 0;
  // Son ifadenin ICINDE (sagda) v'ye baska atama olamaz — `v = v + STEP`
  // bicimi zaten bunu disliyor.

  // UB ve STEP dongu boyunca DEGISMEMELI, yoksa bir kezlik sinav bayatlar.
  if (tulpar_loop_rebinds_name(cond, body, nullptr, ub)) return 0;
  if (tulpar_loop_rebinds_name(cond, body, nullptr, step)) return 0;

  // Kutulayabilen eleman yazmasi olmamali (for kanitiyla ayni kural).
  WriteCtx wc{IntCtx{ivar}, false};
  walk_all(body, visit_elem_write_ok, &wc);
  walk_all(cond, visit_elem_write_ok, &wc);
  if (wc.unsafe) return 0;

  if (ivar_out) *ivar_out = ivar;
  if (ub_out) *ub_out = ub;
  if (step_out) *step_out = step;
  if (inclusive_out) *inclusive_out = incl;
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
