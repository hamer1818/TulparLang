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

// ---- Eleman yazmasi guvenligi: TAMSAYI + i32'ye SIGMA (K215, 2026-09-28) ----
//
// Hizli surum 32-BIT depoya gore DALSIZ uretiliyor (surum kosulu `is32`).
// O surumde bir eleman yazmasi diziyi GENISLETIRSE (i32'ye sigmayan deger:
// calisma zamani depoyu 64-bit'e cevirir) sonraki kanitli erisimler hala
// 32-bit adresliyor. Iki olculmus sessiz bozulma (2026-09-28):
//   * kanitli yazma `a[i] = i + 2147483647` degeri i32'ye KIRPIYORDU
//     (array_fill(3,0) -> `2147483647 -2147483648 -2147483647`);
//   * bekcili yazma (`a[i] += 2000000000`, ya da takma ad `b[0] = i + 3e9`)
//     diziyi genisletiyor, ayni turdaki kanitli `s = s + a[i]` okumasi 32-bit
//     adresle cop okuyordu (toplam 12e9 yerine 1410065408).
// O yuzden kural "tamsayi" degil "tamsayi VE i32'ye SIGAR": her yazilan
// ifadenin DEGER ARALIGI hesaplaniyor. Sigdigi kanitlanamayan yazma (ve
// `+=`/`-=`/`*=`/`<<=`/`++`/`--` — sonuc elemanin kendisine bagli) dongunun
// kanitini dusurur; genel (bekcili) surum her durumu dogru isliyor.
//
// Aralik double ile tutuluyor: yalniz i32 sinirlarina gore karsilastirma
// yapiliyor, 2^53'e kadar tam; daha buyuk uclar zaten "sigmaz".
//
// Dongu degiskeni: 0 <= i <= ivar_hi (for: i < len(a) <= count <= INT32_MAX;
// while: i <= UB < count). ivar_hi kucultulerek (`i * 2` gibi) sigmayan bir
// ifade icin EN BUYUK izinli ust sinir aranir; codegen onu surum kosuluna
// `count <= sinir + 1` olarak ekler (ivar_max_out).
//
// DONGU-DEGISMEZI AD (K215): dongude yeniden baglanmayan bir ad (`a[i] = k`)
// da kabul; turunu derleme zamani bilmiyor, o yuzden ad `inv` listesine
// yaziliyor ve codegen dongu basinda `tag(k) == INT && k i32'ye sigar`
// sinavini SURUM KOSULUNA ekliyor. Aralik [INT32_MIN, INT32_MAX].
//
// bool BILEREK DISARIDA: etiketi 0 (INT) degil 2, yani `a[i] = true`
// kutusuz diziye dogrudan yazilamaz.
struct IntCtx {
  const char *ivar;       // dongu degiskeni: int oldugu dongu BICIMINDEN belli
  double ivar_hi;         // dongu degiskeninin ust siniri (aralik hesabi icin)
  ASTNode_C *cond, *body, *incr;   // degismezlik sinavi icin (nullptr = kapali)
  const char **inv;       // kabul edilen dongu-degismezi adlar (codegen sinar)
  int *n_inv;
  int max_inv;
  // K201: `X[i]` OKUMASI (i dongu degiskeni) int kabul edilir — yalniz X de
  // kanitli (32-bit kutusuz) olursa dogru; ad `arr` listesine yazilir ve
  // codegen X kanitli degilse kaniti geri alir. NULL = kapali.
  const char **arr;
  int *n_arr;
  int max_arr;
};

static const double kI32Min = -2147483648.0;
static const double kI32Max = 2147483647.0;

struct IRange {
  double lo, hi;
};

static bool fits_i32(IRange r) { return r.lo >= kI32Min && r.hi <= kI32Max; }

static bool note_arr_read(IntCtx *ic, const char *name) {
  if (!ic->arr || !ic->n_arr || !name) return false;
  for (int k = 0; k < *ic->n_arr; k++)
    if (strcmp(ic->arr[k], name) == 0) return true;
  if (*ic->n_arr >= ic->max_arr) return false;
  ic->arr[(*ic->n_arr)++] = name;
  return true;
}

static bool note_invariant(IntCtx *ic, const char *name) {
  if (!ic->inv || !ic->n_inv) return false;
  if (tulpar_loop_rebinds_name(ic->cond, ic->body, ic->incr, name)) return false;
  for (int k = 0; k < *ic->n_inv; k++)
    if (strcmp(ic->inv[k], name) == 0) return true;
  if (*ic->n_inv >= ic->max_inv) return false;
  ic->inv[(*ic->n_inv)++] = name;
  return true;
}

// Deger KESIN tamsayi mi, ve araligi ne? Yalniz "evet" yuk tasiyor; "hayir"
// en fazla optimizasyonu kaciriyor. Beyaz liste: taniniamayan her dugum hayir.
static bool expr_int_range(ASTNode_C *n, IntCtx *ic, IRange *r) {
  if (!n) return false;
  switch (n->type) {
  case AST_INT_LITERAL:
    r->lo = r->hi = (double)n->value.int_value;
    return true;
  case AST_IDENTIFIER:
    if (!n->name) return false;
    // Dongu degiskeni: int oldugu ve araligi dongunun BICIMINDEN belli (init
    // bir int sabiti >= 0, artim pozitif int sabiti, govde onu yeniden
    // baglamiyor — ucu de kanitin on kosulu).
    //
    // `int k` yazan bir yerel bile kutulu bir VMValue yuvasinda duruyor ve
    // icine calisma zamaninda float girebilir; derleme zamaninda turunu
    // bilmenin yolu yok. Dongu-DEGISMEZI adin etiketi ise dongu degismezi:
    // sinav dongu BASINA, surum kosuluna gidiyor (note_invariant).
    if (ic->ivar && strcmp(n->name, ic->ivar) == 0) {
      r->lo = 0.0;
      r->hi = ic->ivar_hi;
      return true;
    }
    if (note_invariant(ic, n->name)) {
      r->lo = kI32Min;
      r->hi = kI32Max;
      return true;
    }
    return false;
  case AST_ARRAY_ACCESS: {
    // `X[i]` (K201): kanitli 32-bit kutusuz dizinin elemani i32 araliginda.
    const char *base = n->name ? n->name
                       : (n->left && n->left->type == AST_IDENTIFIER) ? n->left->name
                                                                      : nullptr;
    if (!base || !n->index || n->index->type != AST_IDENTIFIER || !n->index->name ||
        !ic->ivar || strcmp(n->index->name, ic->ivar) != 0)
      return false;
    if (!note_arr_read(ic, base)) return false;
    r->lo = kI32Min;
    r->hi = kI32Max;
    return true;
  }
  case AST_UNARY_OP: {
    IRange a;
    if (!expr_int_range(n->left, ic, &a)) return false;
    if (n->op == TOKEN_MINUS) { r->lo = -a.hi; r->hi = -a.lo; return true; }
    if (n->op == TOKEN_BIT_NOT) { r->lo = -a.hi - 1.0; r->hi = -a.lo - 1.0; return true; }
    return false;
  }
  case AST_BINARY_OP: {
    IRange a, b;
    switch (n->op) {
    // Tulpar'da int/int TAMSAYI bolme (`7 / 2 == 3`), yani `/` de int
    // koruyor. Karsilastirmalar bool uretiyor: listede yoklar.
    case TOKEN_PLUS:
      if (!expr_int_range(n->left, ic, &a) || !expr_int_range(n->right, ic, &b)) return false;
      r->lo = a.lo + b.lo; r->hi = a.hi + b.hi;
      return true;
    case TOKEN_MINUS:
      if (!expr_int_range(n->left, ic, &a) || !expr_int_range(n->right, ic, &b)) return false;
      r->lo = a.lo - b.hi; r->hi = a.hi - b.lo;
      return true;
    case TOKEN_MULTIPLY: {
      if (!expr_int_range(n->left, ic, &a) || !expr_int_range(n->right, ic, &b)) return false;
      double p[4] = {a.lo * b.lo, a.lo * b.hi, a.hi * b.lo, a.hi * b.hi};
      r->lo = r->hi = p[0];
      for (double v : p) { if (v < r->lo) r->lo = v; if (v > r->hi) r->hi = v; }
      return true;
    }
    case TOKEN_DIVIDE: {
      // |a / b| <= |a| (b != 0); b araligi 0'i iceriyorsa sifira bolme —
      // sonuc bilinmiyor, "sigmaz" say.
      if (!expr_int_range(n->left, ic, &a) || !expr_int_range(n->right, ic, &b)) return false;
      const bool has0 = b.lo <= 0.0 && b.hi >= 0.0;
      double m = a.lo < 0 ? -a.lo : a.lo;
      if ((a.hi < 0 ? -a.hi : a.hi) > m) m = a.hi < 0 ? -a.hi : a.hi;
      r->lo = has0 ? -1e300 : -m;
      r->hi = has0 ? 1e300 : m;
      return true;
    }
    case TOKEN_MODULO: {
      // |a % b| < |b| ve <= |a|; b 0'i iceriyorsa bilinmiyor.
      if (!expr_int_range(n->left, ic, &a) || !expr_int_range(n->right, ic, &b)) return false;
      const bool has0 = b.lo <= 0.0 && b.hi >= 0.0;
      double mb = (b.lo < 0 ? -b.lo : b.lo);
      if ((b.hi < 0 ? -b.hi : b.hi) > mb) mb = b.hi < 0 ? -b.hi : b.hi;
      double ma = a.lo < 0 ? -a.lo : a.lo;
      if ((a.hi < 0 ? -a.hi : a.hi) > ma) ma = a.hi < 0 ? -a.hi : a.hi;
      double m = (mb - 1.0 < ma) ? mb - 1.0 : ma;
      r->lo = has0 ? -1e300 : (a.lo < 0 ? -m : 0.0);
      r->hi = has0 ? 1e300 : (a.hi > 0 ? m : 0.0);
      return true;
    }
    // BIT ISLECLERI (2026-09-16): iki taraf da tamsayi olarak KANITLIYSA
    // sonuc da tamsayidir. Iki taraf i32'ye sigiyorsa `&`/`|`/`^` sonucu da
    // sigar (ikiye tumleyen); negatif olmayan bir maskeyle `&` [0, maske].
    case TOKEN_BIT_AND:
      if (!expr_int_range(n->left, ic, &a) || !expr_int_range(n->right, ic, &b)) return false;
      if (a.lo >= 0 && b.lo >= 0) { r->lo = 0; r->hi = a.hi < b.hi ? a.hi : b.hi; return true; }
      if (a.lo >= 0) { r->lo = 0; r->hi = a.hi; return true; }
      if (b.lo >= 0) { r->lo = 0; r->hi = b.hi; return true; }
      if (fits_i32(a) && fits_i32(b)) { r->lo = kI32Min; r->hi = kI32Max; return true; }
      r->lo = -1e300; r->hi = 1e300;
      return true;
    case TOKEN_PIPE:
    case TOKEN_BIT_XOR:
      if (!expr_int_range(n->left, ic, &a) || !expr_int_range(n->right, ic, &b)) return false;
      if (fits_i32(a) && fits_i32(b)) {
        // Iki taraf negatif degilse sonuc da negatif degil; ust sinir iki
        // ucun en buyugunun bit genisligini asmaz.
        if (a.lo >= 0 && b.lo >= 0) {
          double m = a.hi > b.hi ? a.hi : b.hi, p = 1.0;
          while (p <= m) p *= 2.0;
          r->lo = 0; r->hi = p - 1.0;
        } else {
          r->lo = kI32Min; r->hi = kI32Max;
        }
        return true;
      }
      r->lo = -1e300; r->hi = 1e300;
      return true;
    case TOKEN_SHIFT_LEFT:
      if (!expr_int_range(n->left, ic, &a) || !expr_int_range(n->right, ic, &b)) return false;
      if (b.lo == b.hi && b.lo >= 0 && b.lo <= 62) {
        double f = 1.0;
        for (int k = 0; k < (int)b.lo; k++) f *= 2.0;
        r->lo = a.lo * f; r->hi = a.hi * f;
        return true;
      }
      r->lo = -1e300; r->hi = 1e300;
      return true;
    case TOKEN_SHIFT_RIGHT:
      // Aritmetik kaydirma: sonuc x ile 0 arasinda (kaydirma >= 0 ise).
      if (!expr_int_range(n->left, ic, &a) || !expr_int_range(n->right, ic, &b)) return false;
      if (b.lo >= 0) {
        r->lo = a.lo < 0 ? a.lo : 0.0; r->hi = a.hi > 0 ? a.hi : 0.0;
        return true;
      }
      r->lo = -1e300; r->hi = 1e300;
      return true;
    default:
      return false;
    }
  }
  default:
    return false;
  }
}

struct WriteCtx {
  IntCtx ic;
  bool unsafe;
  bool need_limit;   // bir yazma ancak ivar_hi kucultulurse sigiyor
  // K201: govdenin ILK deyimi `A[i] = e` ise ve e sigmiyorsa hizli surumde
  // calisma zamani sinavi + GENEL SURUME GECIS (deopt) ile kabul edilir: o
  // deyimden once hicbir etki olmadigi icin turu genel surumde bastan kosmak
  // esdeger. Kullanilirsa deopt_used.
  ASTNode_C *deopt_node;
  bool deopt_used;
};

// `X[...] = e` / `X[...] op= e` hedefinin taban adi. Deopt'lu yazmanin hedefi
// de KANITLI olmali (arr listesine girer): yoksa yazma hizli surumde bekcili
// yoldan gecip diziyi genisletebilir — takma adli kanitli bir diziyi bozardi.
static const char *write_target_base(ASTNode_C *n) {
  ASTNode_C *t = n ? n->left : nullptr;
  if (!t || t->type != AST_ARRAY_ACCESS) return nullptr;
  if (t->name) return t->name;
  return (t->left && t->left->type == AST_IDENTIFIER) ? t->left->name : nullptr;
}

static bool visit_elem_write_ok(ASTNode_C *n, void *p) {
  WriteCtx *w = (WriteCtx *)p;
  if (!n->left || n->left->type != AST_ARRAY_ACCESS) return true;
  IRange rg;
  switch (n->type) {
  case AST_INCREMENT:
  case AST_DECREMENT:
    // Sonuc ELEMANIN kendisine bagli: INT32_MAX'ta `++` i32'den tasar ve
    // diziyi genisletir (bkz. yukaridaki not). Kanit dusuyor.
    break;
  case AST_ASSIGNMENT:
    if (expr_int_range(n->right, &w->ic, &rg)) {
      if (fits_i32(rg)) return true;
      if (n == w->deopt_node && note_arr_read(&w->ic, write_target_base(n))) {
        w->deopt_used = true;
        return true;
      }
      // Dongu degiskeni iceren ifade daha kucuk bir ust sinirla sigabilir
      // (`i * 2`): cagiran sinir arayacak.
      w->need_limit = true;
      return true;
    }
    break;
  case AST_COMPOUND_ASSIGN:
    // `a[i] op= x`: sol taraf i32'de (kutusuz 32-bit dizi). Sonucu elemandan
    // BAGIMSIZ olarak i32'de kalan islecler guvenli: `&= | ^=` (iki i32'nin
    // bit islemi i32), `>>=` (x ile 0 arasi), `%=` (|sonuc| <= |eleman|), `/=`
    // bolen 0 ve -1'i icermiyorsa (INT32_MIN / -1 = 2^31 tasar). `+= -= *=
    // <<=` tasabilir: kanit dusuyor.
    if (!expr_int_range(n->right, &w->ic, &rg)) break;
    switch (n->op) {
    case TOKEN_BIT_AND_EQUAL:
    case TOKEN_BIT_OR_EQUAL:
    case TOKEN_BIT_XOR_EQUAL:
      if (fits_i32(rg)) return true;
      break;
    case TOKEN_SHIFT_RIGHT_EQUAL:
      if (rg.lo >= 0) return true;
      break;
    case TOKEN_MODULO_EQUAL:
      if (!(rg.lo <= 0.0 && rg.hi >= 0.0)) return true;
      break;
    case TOKEN_DIVIDE_EQUAL:
      if (!(rg.lo <= 0.0 && rg.hi >= -1.0)) return true;
      break;
    case TOKEN_PLUS_EQUAL:
    case TOKEN_MINUS_EQUAL:
    case TOKEN_MULTIPLY_EQUAL:
      // `a[i] += e` (K201 devami): ilk deyimse `a[i] = a[i] + e` ile ayni —
      // hizli surumde sigma sinavi + genel surume gecis.
      if (n == w->deopt_node && note_arr_read(&w->ic, write_target_base(n))) {
        w->deopt_used = true;
        return true;
      }
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

// Govdedeki yazmalar ivar_hi ile guvenli mi? need_limit ise sigmayan yazma
// icin EN BUYUK izinli ust siniri ikili aramayla bulur (aralik ivar_hi'de
// monoton: buyuyen ust sinir araligi yalniz genisletir). Donus: -1 kanit yok,
// 0 sinirsiz, >0 ivar <= donus-1 (codegen `count <= donus` sinar).
static long long elem_writes_limit(ASTNode_C *cond, ASTNode_C *body, ASTNode_C *incr,
                                   const char *ivar, const char **inv, int *n_inv,
                                   int max_inv, const char **arr, int *n_arr, int max_arr,
                                   ASTNode_C *deopt_node, bool *deopt_used) {
  auto run = [&](double hi, bool *need) -> bool {
    int saved = n_inv ? *n_inv : 0;
    int saved_arr = n_arr ? *n_arr : 0;
    WriteCtx wc{IntCtx{ivar, hi, cond, body, incr, inv, n_inv, max_inv, arr, n_arr, max_arr},
                false, false, deopt_node, false};
    walk_all(body, visit_elem_write_ok, &wc);
    walk_all(cond, visit_elem_write_ok, &wc);
    walk_all(incr, visit_elem_write_ok, &wc);
    if (need) *need = wc.need_limit;
    if (wc.unsafe || wc.need_limit) {
      if (n_inv) *n_inv = saved;   // basarisiz denemenin adlari sayilmasin
      if (n_arr) *n_arr = saved_arr;
      return false;
    }
    if (deopt_used) *deopt_used = wc.deopt_used;
    return true;
  };
  bool need = false;
  if (run(kI32Max - 1.0, &need)) return 0;
  if (!need) return -1;   // guvensiz yazma: sinir kurtarmaz
  // En buyuk guvenli ust sinir: [0, INT32_MAX-1] icinde ikili arama.
  long long lo = 0, hi = 2147483646LL, best = -1;
  while (lo <= hi) {
    long long mid = lo + (hi - lo) / 2;
    int saved = n_inv ? *n_inv : 0;
    int saved_arr = n_arr ? *n_arr : 0;
    if (run((double)mid, nullptr)) {
      best = mid;
      lo = mid + 1;
      if (n_inv) *n_inv = saved;   // son (en iyi) calistirma asagida
      if (n_arr) *n_arr = saved_arr;
    } else {
      hi = mid - 1;
    }
  }
  // Cok kucuk sinir (`a[i] = i + 2147483647` -> count <= 1) hizli surumu
  // pratikte hic acmaz; ikinci bir govde uretmeye degmez.
  if (best + 1 < 1024) return -1;
  // Adlari en iyi sinirla bir kez daha topla.
  if (!run((double)best, nullptr)) return -1;
  return best + 1;   // count <= best + 1  <=>  ivar <= best
}

// Kanit fonksiyonlarinin ortak yazma kurali. `wp` NULL ise dongu-degismezi
// ad ve sayim siniri KABUL EDILMEZ (cagiran onlari sinayamaz).
static bool write_proof(ASTNode_C *cond, ASTNode_C *body, ASTNode_C *incr, const char *ivar,
                        TulparWriteProof *wp, bool for_loop = false) {
  int n_local = 0;
  const char *local_inv[TULPAR_WP_MAX_INV];
  const char **inv = wp ? wp->inv : local_inv;
  int *n_inv = wp ? &wp->n_inv : &n_local;
  if (wp) { wp->n_inv = 0; wp->count_limit = 0; wp->n_arr = 0; wp->deopt_write = nullptr; }
  // K201 (yalniz for, yalniz cagiran sinayabiliyorsa): dizi okumasi ve ilk
  // deyimde deopt'lu yazma.
  const bool k201 = for_loop && wp;
  ASTNode_C *first = nullptr;
  if (k201 && body && body->type == AST_BLOCK && body->statement_count >= 1 &&
      body->statements) {
    ASTNode_C *s0 = body->statements[0];
    const bool arith_compound =
        s0 && s0->type == AST_COMPOUND_ASSIGN &&
        (s0->op == TOKEN_PLUS_EQUAL || s0->op == TOKEN_MINUS_EQUAL ||
         s0->op == TOKEN_MULTIPLY_EQUAL);
    if (s0 && (s0->type == AST_ASSIGNMENT || arith_compound) && s0->left &&
        s0->left->type == AST_ARRAY_ACCESS && s0->left->index &&
        s0->left->index->type == AST_IDENTIFIER && s0->left->index->name &&
        strcmp(s0->left->index->name, ivar) == 0)
      first = s0;
  }
  bool deopt_used = false;
  // Once DEOPT'SUZ: sayim siniriyla kanitlanabiliyorsa (`a[i] = i * 2`) o yol
  // tercih edilir — dongude sinav yok, vektorlesebilir. Olculdu: deopt'lu
  // surum ayni govdede 20 -> 60 ms geriliyordu (20M int x 10 tur).
  long long lim = elem_writes_limit(cond, body, incr, ivar, wp ? inv : nullptr,
                                    wp ? n_inv : nullptr, TULPAR_WP_MAX_INV,
                                    k201 ? wp->arr : nullptr, k201 ? &wp->n_arr : nullptr,
                                    TULPAR_WP_MAX_INV, nullptr, nullptr);
  if (lim < 0 && first) {
    if (wp) { wp->n_inv = 0; wp->n_arr = 0; }
    lim = elem_writes_limit(cond, body, incr, ivar, wp ? inv : nullptr, wp ? n_inv : nullptr,
                            TULPAR_WP_MAX_INV, k201 ? wp->arr : nullptr,
                            k201 ? &wp->n_arr : nullptr, TULPAR_WP_MAX_INV, first,
                            &deopt_used);
  }
  if (lim < 0) return false;
  if (lim > 0 && !wp) return false;
  if (wp) wp->count_limit = lim;
  if (wp && deopt_used) wp->deopt_write = first;
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
// KUTULAYABILEN eleman yazmasi olan govde reddediliyor: bkz.
// visit_elem_write_ok.
extern "C" int tulpar_loop_index_proven(ASTNode_C *init, ASTNode_C *cond,
                                        ASTNode_C *body, ASTNode_C *incr,
                                        const char *array_name,
                                        const char **ivar_out,
                                        TulparWriteProof *wp) {
  if (!init || !cond || !incr || !array_name) return 0;

  // init: `int i = C;`  (C >= 0)
  if (init->type != AST_VARIABLE_DECL || !init->name) return 0;
  ASTNode_C *iv = init->right;
  if (!iv || iv->type != AST_INT_LITERAL || iv->value.int_value < 0) return 0;
  const char *ivar = init->name;

  // cond: `i < len(a)` — ya da (2026-09-28) sinir DONGU BASINDA sinanabilen
  // bir sey: `i < n` / `i <= n` (dongude yeniden baglanmayan ad) ya da
  // `i < len(b)` (b baska bir dizi). Ucu de ayni kanit: sinir <= count(a)
  // dongu basinda BIR KEZ sinanir ve surum kosuluna girer (codegen,
  // TulparWriteProof::bound_*). Olculdu (20M int, 5 tur toplama, bu makine):
  // `i < n` 256 ms -> kanitli yol; eskiden yalniz `i < len(a)` kanitlaniyordu
  // ve `i < n` sessizce bekcili kaliyordu (K167 ipucu bunu soyluyordu).
  if (cond->type != AST_BINARY_OP ||
      (cond->op != TOKEN_LESS && cond->op != TOKEN_LESS_EQUAL))
    return 0;
  ASTNode_C *lhs = cond->left, *rhs = cond->right;
  if (!lhs || lhs->type != AST_IDENTIFIER || !lhs->name ||
      strcmp(lhs->name, ivar) != 0)
    return 0;
  const char *bound_name = nullptr, *bound_len_of = nullptr;
  const bool incl = cond->op == TOKEN_LESS_EQUAL;
  if (rhs && rhs->type == AST_FUNCTION_CALL && rhs->name &&
      (strcmp(rhs->name, "len") == 0 || strcmp(rhs->name, "length") == 0) &&
      rhs->argument_count == 1 && rhs->arguments && rhs->arguments[0] &&
      rhs->arguments[0]->type == AST_IDENTIFIER && rhs->arguments[0]->name && !incl) {
    if (strcmp(rhs->arguments[0]->name, array_name) != 0) {
      bound_len_of = rhs->arguments[0]->name;
      // Sinir dizisi de dongude yeniden baglanmamali (sekli zaten kanitli
      // olacak: codegen onu sekil onbelleginde bulamazsa kaniti geri alir).
      if (tulpar_loop_rebinds_name(cond, body, incr, bound_len_of)) return 0;
    }
  } else if (rhs && rhs->type == AST_IDENTIFIER && rhs->name && strcmp(rhs->name, ivar) != 0) {
    bound_name = rhs->name;
    if (tulpar_loop_rebinds_name(cond, body, incr, bound_name)) return 0;
  } else {
    return 0;
  }
  // Calisma zamani sinavi gereken sinir, cagiran sinayamiyorsa (wp NULL) yok.
  if ((bound_name || bound_len_of) && !wp) return 0;

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

  // HER eleman yazmasi KESIN tamsayi VE i32'ye sigmali — yoksa kutulama ya
  // da genisletme riski (bkz. elem_writes_limit, K215).
  if (!write_proof(cond, body, incr, ivar, wp, /*for_loop=*/true)) return 0;
  if (wp) {
    wp->bound_name = bound_name;
    wp->bound_len_of = bound_len_of;
    wp->bound_incl = incl ? 1 : 0;
  }

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
                         const char **step_name, long long *step_const) {
  // `v = v + STEP` — STEP ya bir AD ya da POZITIF bir int sabiti.
  //
  // Sabit adim once REDDEDILIYORDU ("sabit adimli bicim zaten `for`
  // kanitinda" diye), ama `for` kaniti yalniz `for` dongulerine bakiyor.
  // `while (i <= n) { ...; i = i + 1; }` her iki kanitin da disinda kaliyordu
  // — elek'in DIS dongusu tam bu bicimde ve hic surumlenmiyordu.
  // Sabit adim ustelik daha kolay: `STEP > 0` sinavi derleme zamaninda
  // katlaniyor, dongu basina hicbir sey binmiyor.
  if (!st || st->type != AST_ASSIGNMENT || !st->name || st->left) return false;
  if (strcmp(st->name, ivar) != 0) return false;
  ASTNode_C *r = st->right;
  if (!r || r->type != AST_BINARY_OP || r->op != TOKEN_PLUS) return false;
  ASTNode_C *a = r->left, *b = r->right;
  if (!a || !b || a->type != AST_IDENTIFIER || !a->name ||
      strcmp(a->name, ivar) != 0)
    return false;
  if (b->type == AST_IDENTIFIER && b->name) {
    *step_name = b->name;
    return true;
  }
  if (b->type == AST_INT_LITERAL && b->value.int_value > 0) {
    *step_name = nullptr;
    *step_const = b->value.int_value;
    return true;
  }
  return false;
}

extern "C" int tulpar_while_index_proven(ASTNode_C *cond, ASTNode_C *body,
                                         const char **ivar_out,
                                         const char **ub_out,
                                         const char **step_out,
                                         long long *step_const_out,
                                         int *inclusive_out,
                                         TulparWriteProof *wp) {
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
  long long step_const = 0;
  if (!stmt_is_step(body->statements[body->statement_count - 1], ivar, &step,
                    &step_const))
    return 0;
  if (step && strcmp(step, ivar) == 0) return 0;

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
  // Sabit adimda yeniden baglanma sorusu anlamsiz (sabitin adi yok).
  if (step && tulpar_loop_rebinds_name(cond, body, nullptr, step)) return 0;

  // Kutulayabilen eleman yazmasi olmamali (for kanitiyla ayni kural).
  if (!write_proof(cond, body, nullptr, ivar, wp)) return 0;

  if (ivar_out) *ivar_out = ivar;
  if (ub_out) *ub_out = ub;
  if (step_out) *step_out = step;
  if (step_const_out) *step_const_out = step_const;
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

// ---- K167 (2026-09-28): kanit neden kurulamadi (performans ipucu) ----------
//
// Kanit kurulamayinca erisim SESSIZCE bekcili yola dusuyordu; `i < n` ile
// `i < len(a)` arasindaki fark birkac kat ama hicbir sey soylemiyordu
// (Tuzaklar: "acik bir is kalemi"). Asagidakiler kanit fonksiyonlarinin
// adimlarini AYNI SIRAYLA yuruyup ilk dusen adimi adlandirir. Kanit karari
// burada VERILMIYOR — codegen tulpar_*_index_proven'e bakiyor; bunlar yalniz
// o "hayir" dediginde cagriliyor.

extern "C" int tulpar_loop_index_why(ASTNode_C *init, ASTNode_C *cond, ASTNode_C *body,
                                     ASTNode_C *incr, const char *array_name,
                                     const char **detail_out) {
  if (detail_out) *detail_out = nullptr;
  if (!init || init->type != AST_VARIABLE_DECL || !init->name || !init->right ||
      init->right->type != AST_INT_LITERAL || init->right->value.int_value < 0)
    return TLW_INIT;
  const char *ivar = init->name;
  if (!cond || cond->type != AST_BINARY_OP ||
      (cond->op != TOKEN_LESS && cond->op != TOKEN_LESS_EQUAL) || !cond->left ||
      cond->left->type != AST_IDENTIFIER || !cond->left->name ||
      strcmp(cond->left->name, ivar) != 0)
    return TLW_COND_OP;
  ASTNode_C *rhs = cond->right;
  const bool is_len = rhs && rhs->type == AST_FUNCTION_CALL && rhs->name &&
                      (strcmp(rhs->name, "len") == 0 || strcmp(rhs->name, "length") == 0) &&
                      rhs->argument_count == 1 && rhs->arguments && rhs->arguments[0] &&
                      rhs->arguments[0]->type == AST_IDENTIFIER && rhs->arguments[0]->name;
  // Sinir: `len(<dizi>)` ya da dongude degismeyen bir AD (`i < n`, `i <= n`) —
  // ikisi de dongu basinda sinaniyor (kanit bunlari kabul ediyor).
  if (is_len) {
    if (cond->op == TOKEN_LESS_EQUAL) return TLW_COND_OP;
    if (strcmp(rhs->arguments[0]->name, array_name) != 0 &&
        tulpar_loop_rebinds_name(cond, body, incr, rhs->arguments[0]->name)) {
      if (detail_out) *detail_out = rhs->arguments[0]->name;
      return TLW_W_UB_REBIND;
    }
  } else if (rhs && rhs->type == AST_IDENTIFIER && rhs->name && strcmp(rhs->name, ivar) != 0) {
    if (tulpar_loop_rebinds_name(cond, body, incr, rhs->name)) {
      if (detail_out) *detail_out = rhs->name;
      return TLW_W_UB_REBIND;
    }
  } else {
    return TLW_BOUND_OTHER;
  }
  bool incr_ok = false;
  if (incr && incr->type == AST_INCREMENT && incr->name && !incr->left &&
      strcmp(incr->name, ivar) == 0) {
    incr_ok = true;
  } else if (incr && incr->type == AST_ASSIGNMENT && incr->name && !incr->left &&
             strcmp(incr->name, ivar) == 0 && incr->right &&
             incr->right->type == AST_BINARY_OP && incr->right->op == TOKEN_PLUS) {
    ASTNode_C *a = incr->right->left, *b = incr->right->right;
    incr_ok = a && b && a->type == AST_IDENTIFIER && a->name && strcmp(a->name, ivar) == 0 &&
              b->type == AST_INT_LITERAL && b->value.int_value > 0;
  }
  if (!incr_ok) return TLW_INCR;
  if (tulpar_loop_rebinds_name(nullptr, body, nullptr, ivar)) {
    if (detail_out) *detail_out = ivar;
    return TLW_REBIND;
  }
  TulparWriteProof wpw;
  if (!write_proof(cond, body, incr, ivar, &wpw, /*for_loop=*/true)) return TLW_WRITE;
  return TLW_OK;
}

extern "C" int tulpar_while_index_why(ASTNode_C *cond, ASTNode_C *body,
                                      const char **detail_out) {
  if (detail_out) *detail_out = nullptr;
  if (!cond || cond->type != AST_BINARY_OP ||
      (cond->op != TOKEN_LESS && cond->op != TOKEN_LESS_EQUAL) || !cond->left ||
      cond->left->type != AST_IDENTIFIER || !cond->left->name)
    return TLW_COND_OP;
  const char *ivar = cond->left->name;
  ASTNode_C *r = cond->right;
  if (!r || r->type != AST_IDENTIFIER || !r->name || strcmp(r->name, ivar) == 0)
    return TLW_W_BOUND;
  const char *ub = r->name;
  if (!body || body->type != AST_BLOCK || body->statement_count < 1 || !body->statements)
    return TLW_W_STEP;
  const char *step = nullptr;
  long long step_const = 0;
  if (!stmt_is_step(body->statements[body->statement_count - 1], ivar, &step, &step_const) ||
      (step && strcmp(step, ivar) == 0)) {
    if (detail_out) *detail_out = ivar;
    return TLW_W_STEP;
  }
  for (int i = 0; i < body->statement_count - 1; i++)
    if (tulpar_loop_rebinds_name(nullptr, body->statements[i], nullptr, ivar)) {
      if (detail_out) *detail_out = ivar;
      return TLW_REBIND;
    }
  if (tulpar_loop_rebinds_name(cond, body, nullptr, ub)) {
    if (detail_out) *detail_out = ub;
    return TLW_W_UB_REBIND;
  }
  if (step && tulpar_loop_rebinds_name(cond, body, nullptr, step)) {
    if (detail_out) *detail_out = step;
    return TLW_W_UB_REBIND;
  }
  TulparWriteProof wpw;
  if (!write_proof(cond, body, nullptr, ivar, &wpw)) return TLW_WRITE;
  return TLW_OK;
}

struct IndexedByCtx {
  const char *arr;
  const char *ivar;
  bool found;
};

static bool visit_indexed_by(ASTNode_C *n, void *p) {
  IndexedByCtx *c = (IndexedByCtx *)p;
  if (n->type == AST_ARRAY_ACCESS && n->left && n->left->type == AST_IDENTIFIER &&
      n->left->name && strcmp(n->left->name, c->arr) == 0 && n->index &&
      n->index->type == AST_IDENTIFIER && n->index->name &&
      strcmp(n->index->name, c->ivar) == 0) {
    c->found = true;
    return false;
  }
  return true;
}

extern "C" int tulpar_body_indexes_by(ASTNode_C *body, const char *array_name,
                                      const char *ivar) {
  if (!body || !array_name || !ivar) return 0;
  IndexedByCtx c{array_name, ivar, false};
  walk_all(body, visit_indexed_by, &c);
  return c.found ? 1 : 0;
}

// ---- K064: struct DEGERI tutan `var`in kacis analizi (2026-09-29) ----------
//
// `var q = mk(..)` (mk kutusuz bir struct dondurur) GENEL baglam sayiliyor ve
// deger string anahtarli bir nesneye KUTULANIYORDU: her bildirimde bir nesne
// ayirmasi + alan basina bir strcmp'li yazma, her `q.x` bir ad aramasi. Ayni
// dongu `P q = mk(..)` ile yazilinca yigindaki alloca'ya iniyor. Olculdu
// (2026-09-29, Ryzen 7 9800X3D): 5M tur `var q = mk(i); s += q.x + q.y`
// 444 ms, `P q = ...` ~0 ms (katlaniyor).
//
// Kutulu nesne REFERANS anlambilimli, tipli yerel DEGER (kopya) — `q`
// yalniz ALAN OKUMASIYLA kullaniliyorsa ikisi ayirt edilemez. Kural (dar,
// bilerek): fonksiyon govdesinde `q`nun HER gecisi `q.<alan>` okumasi olmali
// (alan struct'ta var). Asagidakilerin HERHANGI biri "kacar" -> kutulu kalir:
//   * `q`nun ciplak kullanimi (arguman, atama sag tarafi, return, print,
//     toJson, match, metot alicisi, dizi/nesne literali...) — takma ad olusur
//     ya da cikti bicimi degisir (print(q) kutuluda "<object>");
//   * `q.x = ..` / `q.x += ..` / `q.x++` — kutulu nesnenin alani dinamik
//     (int alana 1.5 yazilabilir), tipli alan donusturur;
//   * `q = ..` yeniden atama, ayni adla ikinci bildirim / parametre / for-in
//     degiskeni / catch baglamasi (golgeleme);
//   * bir kapanisin (lambda) icinde herhangi bir gecis.
// 1: kacar (kutulu kal), 0: kacmaz (tipli yerel olabilir).
namespace {
struct EscCtx {
  const char *name;
  ASTNode_C *decl;
  const char *const *fields;
  int nfields;
  bool esc;
};

static bool esc_is_base(ASTNode_C *acc, const char *name) {
  if (!acc || acc->type != AST_ARRAY_ACCESS) return false;
  if (acc->left) return acc->left->type == AST_IDENTIFIER && acc->left->name &&
                        strcmp(acc->left->name, name) == 0;
  return acc->name && strcmp(acc->name, name) == 0;
}

static bool esc_field_ok(ASTNode_C *acc, const EscCtx &c) {
  if (!acc->index || acc->index->type != AST_STRING_LITERAL ||
      !acc->index->value.string_value)
    return false;
  for (int i = 0; i < c.nfields; i++)
    if (c.fields[i] && strcmp(c.fields[i], acc->index->value.string_value) == 0) return true;
  return false;
}

static void esc_children(ASTNode_C *n, EscCtx &c, int in_lambda);

static void esc_walk(ASTNode_C *n, EscCtx &c, int in_lambda) {
  if (!n || c.esc) return;
  const char *nm = c.name;
  switch (n->type) {
  case AST_ARRAY_ACCESS:
    if (esc_is_base(n, nm)) {
      // `q.<alan>` OKUMASI: izinli (lambda icinde degilse). Hedef konumu
      // (yazma) atama dugumlerinde ayrica yakalaniyor.
      if (in_lambda || !esc_field_ok(n, c)) { c.esc = true; return; }
      return;
    }
    break;
  case AST_ASSIGNMENT:
  case AST_COMPOUND_ASSIGN:
  case AST_INCREMENT:
  case AST_DECREMENT:
    if ((n->name && strcmp(n->name, nm) == 0) || esc_is_base(n->left, nm) ||
        (n->left && n->left->type == AST_IDENTIFIER && n->left->name &&
         strcmp(n->left->name, nm) == 0)) {
      c.esc = true;
      return;
    }
    break;
  case AST_VARIABLE_DECL:
    if (n != c.decl && n->name && strcmp(n->name, nm) == 0) { c.esc = true; return; }
    break;
  case AST_LAMBDA:
    in_lambda++;
    break;
  default:
    if (n->name && strcmp(n->name, nm) == 0) {  // IDENTIFIER, cagri adi, for-in, catch...
      c.esc = true;
      return;
    }
    break;
  }
  esc_children(n, c, in_lambda);
}

static void esc_children(ASTNode_C *n, EscCtx &c, int in_lambda) {
  ASTNode_C *singles[] = {n->left,       n->right,        n->body,
                          n->receiver,   n->callee,       n->condition,
                          n->then_branch, n->else_branch, n->init,
                          n->increment,  n->iterable,     n->return_value,
                          n->index,      n->try_block,    n->catch_block,
                          n->finally_block, n->throw_expr};
  for (ASTNode_C *ch : singles) esc_walk(ch, c, in_lambda);
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
      for (int i = 0; i < l.count; i++) esc_walk(l.arr[i], c, in_lambda);
}
}  // namespace

extern "C" int tulpar_struct_var_escapes(ASTNode_C *fn, ASTNode_C *decl,
                              const char *const *fields, int nfields) {
  if (!fn || !decl || !decl->name) return 1;
  EscCtx c{decl->name, decl, fields, nfields, false};
  // Kokun kendisi (fonksiyon ya da bildirimin ICINDE bulundugu lambda)
  // "kapanis icinde" sayilmaz — yalniz govdesindeki ic ice lambdalar.
  esc_children(fn, c, 0);
  return c.esc ? 1 : 0;
}
