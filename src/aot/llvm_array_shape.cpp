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

// ===========================================================================
// FLOAT DIZI DONGU SURUMU (2026-10-01) — cozumleme tarafi.
//
// Neden: `matmul` C'nin 26, `nbody` 11 kati yavasti (2026-09-29). Sebep
// olculdu: kanitli erisim yalniz TAMSAYI dizide ve yalniz EN DIS dongude
// vardi; float dizi her erisimde genel yoldan geciyordu (etiket + nesne turu
// + sinir + kutu denetimi, 16 baytlik VMValue yuklemesi) ve sonuc dinamik
// etiketli oldugu icin her aritmetik islem tur dallanmasi uretiyordu.
//
// Bu plan EN ICTEKI `for` dongusu icin (govdesinde baska dongu YOK — ic ice
// surum katlanarak buyumesin; bkz. Performance.md "Ic ice dongu
// surumlemesi") su kaniti kurar:
//
//   * govdedeki HER `X[E]` erisiminde E afin: `j`, `B`, `B + j`, `j + B`
//     (j dongu degiskeni, B dongu-degismezi int ifadesi: tamsayi sabiti,
//     dongude yeniden baglanmayan ad, ve bunlarin + - * bilesimi — bolme
//     YOK, yani B hata uretemez);
//   * HER eleman yazmasi (`X[E] = v`) KESIN FLOAT: v float sabiti, float
//     dizi okumasi, govdede `float` bildirilmis ve yalniz float alan yerel,
//     dongu-degismezi ad (etiketi codegen dongu BASINDA sinar), ya da
//     bunlarin + - * / bilesimi / tekli eksi / sqrt(...).
//
// Sayisal kisim (0 <= B + j < count, dizilerin double depoda olmasi,
// degismez adlarin etiketi) DERLEME ZAMANINDA bilinemez: codegen onu dongu
// basinda BIR KEZ sinar ve donguyu surumler. Hizli surumde erisim tek
// GEP+load/store; genel surum bugunku bekcili yol — sinir disi erisim orada
// hala HATA verir (tests/float_dizi.sh).
//
// KANIT MUHAFAZAKAR: taninmayan her dugum "hayir". Kosul ve artimda dizi
// erisimi de reddediliyor: son kosul sinavi j == UB ile kosar ve kanit
// yalniz govdedeki j icin [C, UB) araligini veriyor.
// ===========================================================================
namespace {

// Dongude `name` adina (eleman degil, adin KENDISINE) yazan dugum sayisi.
// tulpar_loop_rebinds_name'den GENIS: `n++` / `n--` de sayiliyor.
struct FvAssignCtx {
  const char *name;
  int count;
};
static bool fv_visit_assign(ASTNode_C *n, void *p) {
  FvAssignCtx *c = (FvAssignCtx *)p;
  if ((n->type == AST_ASSIGNMENT || n->type == AST_COMPOUND_ASSIGN ||
       n->type == AST_VARIABLE_DECL || n->type == AST_INCREMENT ||
       n->type == AST_DECREMENT) &&
      n->name && strcmp(n->name, c->name) == 0 &&
      !(n->left && n->left->type == AST_ARRAY_ACCESS))
    c->count++;
  return true;
}
static int fv_assign_count(ASTNode_C *cond, ASTNode_C *body, ASTNode_C *incr,
                           const char *name) {
  FvAssignCtx c{name, 0};
  walk_all(cond, fv_visit_assign, &c);
  walk_all(body, fv_visit_assign, &c);
  walk_all(incr, fv_visit_assign, &c);
  return c.count;
}

struct FvNameUse {
  const char *name;
  bool used;
};
static bool fv_visit_use(ASTNode_C *n, void *p) {
  FvNameUse *c = (FvNameUse *)p;
  if (n->name && strcmp(n->name, c->name) == 0) { c->used = true; return false; }
  return true;
}

struct FvCtx {
  ASTNode_C *cond, *body, *incr;
  const char *ivar;
  TulparFloatLoopPlan *p;
  const char *flocal[TULPAR_FV_MAX_INV];   // govdede float (int kipinde: int) yereller
  int n_flocal;
  bool bad;
  // INT kipi (tulpar_int_loop_plan): eleman yazmalari kesin INT, en fazla
  // BIR tane ve govdenin ilk bildirim-disi deyimi (bkz. o fonksiyonun notu).
  bool int_mode;
  int n_writes;
};

static bool fv_reject(FvCtx *c, const char *why) {
  if (!c->bad) { c->bad = true; c->p->why = why; }
  return false;
}

static bool fv_invariant(FvCtx *c, const char *name) {
  return name && strcmp(name, c->ivar) != 0 &&
         fv_assign_count(c->cond, c->body, c->incr, name) == 0;
}

// Dongu-degismezi INT ifadesi (B / UB): sabit, degismez ad, + - * — bolme ve
// cagri YOK (B hicbir kosulda hata uretmemeli). `len_ok`: UB icin `len(X)`.
static bool fv_inv_int(FvCtx *c, ASTNode_C *n, bool len_ok) {
  if (!n) return false;
  switch (n->type) {
  case AST_INT_LITERAL:
    return true;
  case AST_IDENTIFIER:
    return fv_invariant(c, n->name);
  case AST_UNARY_OP:
    return n->op == TOKEN_MINUS && fv_inv_int(c, n->left, false);
  case AST_BINARY_OP:
    if (n->op != TOKEN_PLUS && n->op != TOKEN_MINUS && n->op != TOKEN_MULTIPLY) return false;
    return fv_inv_int(c, n->left, false) && fv_inv_int(c, n->right, false);
  case AST_FUNCTION_CALL:
    return len_ok && n->name && !n->receiver &&
           (strcmp(n->name, "len") == 0 || strcmp(n->name, "length") == 0) &&
           n->argument_count == 1 && n->arguments && n->arguments[0] &&
           n->arguments[0]->type == AST_IDENTIFIER && fv_invariant(c, n->arguments[0]->name);
  default:
    return false;
  }
}

static const char *fv_base(ASTNode_C *acc) {
  if (acc->name) return acc->name;
  if (acc->left && acc->left->type == AST_IDENTIFIER) return acc->left->name;
  return nullptr;
}

static bool fv_is_ivar(FvCtx *c, ASTNode_C *n) {
  return n && n->type == AST_IDENTIFIER && n->name && strcmp(n->name, c->ivar) == 0;
}

// Erisimi plana yaz (ya da reddet).
static bool fv_add_access(FvCtx *c, ASTNode_C *acc) {
  TulparFloatLoopPlan *p = c->p;
  for (int k = 0; k < p->n_acc; k++)
    if (p->acc[k] == acc) return true;
  const char *base = fv_base(acc);
  if (!base) return fv_reject(c, "dizi tabani bir ad degil");
  if (!fv_invariant(c, base)) return fv_reject(c, "dizi dongude yeniden baglaniyor");
  ASTNode_C *ix = acc->index;
  ASTNode_C *b = nullptr;
  int has_j = 0;
  if (fv_is_ivar(c, ix)) {
    has_j = 1;
  } else if (ix && ix->type == AST_BINARY_OP && ix->op == TOKEN_PLUS &&
             (fv_is_ivar(c, ix->left) || fv_is_ivar(c, ix->right))) {
    has_j = 1;
    b = fv_is_ivar(c, ix->left) ? ix->right : ix->left;
    if (!fv_inv_int(c, b, false)) return fv_reject(c, "indeks afin degil");
  } else if (fv_inv_int(c, ix, false)) {
    b = ix;
  } else {
    return fv_reject(c, "indeks afin degil");
  }
  int ai = -1;
  for (int k = 0; k < p->n_arr; k++)
    if (strcmp(p->arr[k], base) == 0) ai = k;
  if (ai < 0) {
    if (p->n_arr >= TULPAR_FV_MAX_ARR) return fv_reject(c, "dizi sayisi tavani");
    ai = p->n_arr;
    p->arr[p->n_arr++] = base;
  }
  if (p->n_acc >= TULPAR_FV_MAX_ACC) return fv_reject(c, "erisim sayisi tavani");
  p->acc[p->n_acc] = acc;
  p->acc_base[p->n_acc] = b;
  p->acc_has_j[p->n_acc] = has_j;
  p->acc_arr[p->n_acc] = ai;
  p->n_acc++;
  return true;
}

static bool fv_flocal(FvCtx *c, const char *name) {
  for (int k = 0; k < c->n_flocal; k++)
    if (strcmp(c->flocal[k], name) == 0) return true;
  return false;
}

// KESIN FLOAT ifade mi? (Bkz. ust not.) Degismez ad listeye eklenir; codegen
// etiketini dongu basinda sinar.
static bool fv_float_expr(FvCtx *c, ASTNode_C *n) {
  if (!n) return false;
  switch (n->type) {
  case AST_FLOAT_LITERAL:
    return true;
  case AST_IDENTIFIER: {
    if (!n->name) return false;
    if (fv_flocal(c, n->name)) return true;
    if (!fv_invariant(c, n->name)) return false;
    TulparFloatLoopPlan *p = c->p;
    for (int k = 0; k < p->n_inv; k++)
      if (strcmp(p->inv_float[k], n->name) == 0) return true;
    if (p->n_inv >= TULPAR_FV_MAX_INV) return false;
    p->inv_float[p->n_inv++] = n->name;
    return true;
  }
  case AST_ARRAY_ACCESS:
    // Plandaki her dizi hizli surumde double depoda: elemani KESIN float.
    return fv_add_access(c, n);
  case AST_UNARY_OP:
    return n->op == TOKEN_MINUS && fv_float_expr(c, n->left);
  case AST_BINARY_OP:
    if (n->op != TOKEN_PLUS && n->op != TOKEN_MINUS && n->op != TOKEN_MULTIPLY &&
        n->op != TOKEN_DIVIDE)
      return false;
    return fv_float_expr(c, n->left) && fv_float_expr(c, n->right);
  case AST_FUNCTION_CALL:
    // sqrt her zaman FLOAT dondurur (runtime aot_math_sqrt; codegen'in satir
    // ici hali ayni kurali uyguluyor). Kullanici `sqrt` tanimladiysa dongu
    // zaten sekil-kararli sayilmaz (shape_pure_call).
    return n->name && !n->receiver && strcmp(n->name, "sqrt") == 0 &&
           n->argument_count == 1 && n->arguments && fv_float_expr(c, n->arguments[0]);
  default:
    return false;
  }
}

// KESIN INT ifade mi? (int kipi.) fv_float_expr'in ikizi: int sabiti, govdede
// `int` bildirilmis yerel, dongu-degismezi ad (codegen etiketini dongu
// basinda INT diye sinar), plandaki dizi okumasi (32-bit int depo — dongu
// basinda sinanir), tekli eksi, `+ - *`. Bolme YOK (sifira bolme hatasi).
static bool fv_int_expr(FvCtx *c, ASTNode_C *n) {
  if (!n) return false;
  switch (n->type) {
  case AST_INT_LITERAL:
    return true;
  case AST_IDENTIFIER: {
    if (!n->name) return false;
    if (fv_flocal(c, n->name)) return true;
    if (!fv_invariant(c, n->name)) return false;
    TulparFloatLoopPlan *p = c->p;
    for (int k = 0; k < p->n_inv; k++)
      if (strcmp(p->inv_float[k], n->name) == 0) return true;
    if (p->n_inv >= TULPAR_FV_MAX_INV) return false;
    p->inv_float[p->n_inv++] = n->name;
    return true;
  }
  case AST_ARRAY_ACCESS:
    return fv_add_access(c, n);
  case AST_UNARY_OP:
    return n->op == TOKEN_MINUS && fv_int_expr(c, n->left);
  case AST_BINARY_OP:
    if (n->op != TOKEN_PLUS && n->op != TOKEN_MINUS && n->op != TOKEN_MULTIPLY) return false;
    return fv_int_expr(c, n->left) && fv_int_expr(c, n->right);
  default:
    return false;
  }
}

static bool fv_visit(ASTNode_C *n, void *p) {
  FvCtx *c = (FvCtx *)p;
  if (c->bad) return false;
  switch (n->type) {
  case AST_FOR:
  case AST_WHILE:
  case AST_FOR_IN:
    return fv_reject(c, "ic ice dongu (yalniz en icteki dongu surumlenir)");
  case AST_ARRAY_ACCESS:
    return fv_add_access(c, n);
  case AST_ASSIGNMENT:
    if (n->left && n->left->type == AST_ARRAY_ACCESS) {
      if (!fv_add_access(c, n->left)) return false;
      if (c->int_mode) {
        c->n_writes++;
        if (!fv_int_expr(c, n->right))
          return fv_reject(c, "eleman yazmasi kesin int degil");
      } else if (!fv_float_expr(c, n->right)) {
        return fv_reject(c, "eleman yazmasi kesin float degil");
      }
    }
    return true;
  case AST_COMPOUND_ASSIGN:
  case AST_INCREMENT:
  case AST_DECREMENT:
    if (n->left && n->left->type == AST_ARRAY_ACCESS)
      return fv_reject(c, "bilesik eleman yazmasi");
    return true;
  default:
    return true;
  }
}

static bool fv_has_access(ASTNode_C *n, void *p) {
  if (n->type == AST_ARRAY_ACCESS) { *(bool *)p = true; return false; }
  return true;
}

}  // namespace

static int fv_plan(ASTNode_C *init, ASTNode_C *cond, ASTNode_C *body, ASTNode_C *incr,
                   TulparPureCallFn pure, void *ctx, TulparFloatLoopPlan *p, bool int_mode) {
  if (!p) return 0;
  memset(p, 0, sizeof(*p));
  p->is_int = int_mode ? 1 : 0;
  p->why = "bicim";
  if (!init || !cond || !incr || !body) return 0;
  // init: `int j = <ifade>` — degeri codegen dongu basinda OKUR (C).
  if (init->type != AST_VARIABLE_DECL || !init->name || !init->right) {
    p->why = "init `int j = ...` degil";
    return 0;
  }
  const char *ivar = init->name;
  FvCtx c{cond, body, incr, ivar, p, {}, 0, false, int_mode, 0};
  // cond: `j < UB` / `j <= UB`
  if (cond->type != AST_BINARY_OP ||
      (cond->op != TOKEN_LESS && cond->op != TOKEN_LESS_EQUAL) ||
      !fv_is_ivar(&c, cond->left)) {
    p->why = "kosul `j < UB` degil";
    return 0;
  }
  if (!fv_inv_int(&c, cond->right, true)) {
    p->why = "ust sinir dongu-degismezi int degil";
    return 0;
  }
  // incr: `j++` / `j = j + K` (0 < K <= 2^31: j + K i64'te tasamaz)
  bool incr_ok = false;
  if (incr->type == AST_INCREMENT && incr->name && !incr->left &&
      strcmp(incr->name, ivar) == 0) {
    incr_ok = true;
  } else if (incr->type == AST_ASSIGNMENT && incr->name && !incr->left &&
             strcmp(incr->name, ivar) == 0 && incr->right &&
             incr->right->type == AST_BINARY_OP && incr->right->op == TOKEN_PLUS &&
             fv_is_ivar(&c, incr->right->left) && incr->right->right &&
             incr->right->right->type == AST_INT_LITERAL &&
             incr->right->right->value.int_value > 0 &&
             incr->right->right->value.int_value <= 2147483648LL) {
    incr_ok = true;
  }
  if (!incr_ok) { p->why = "artim `j++` / `j = j + K` degil"; return 0; }
  // j yalniz artimda degisir.
  if (fv_assign_count(nullptr, body, nullptr, ivar) != 0) {
    p->why = "dongu degiskeni govdede ataniyor";
    return 0;
  }
  // Kosul/artimda dizi erisimi yok (son kosul sinavi j == UB ile kosar).
  bool acc_ci = false;
  walk_all(cond, fv_has_access, &acc_ci);
  walk_all(incr, fv_has_access, &acc_ci);
  if (acc_ci) { p->why = "kosulda/artimda dizi erisimi"; return 0; }
  // Govde sekli degistiremez (push/pop/kullanici cagrisi yok).
  if (!tulpar_loop_shape_stable(cond, body, incr, pure, ctx)) {
    p->why = "govde dizi seklini degistirebilir (cagri/desteklenmeyen dugum)";
    return 0;
  }
  // Govde deyimleri SIRAYLA: ust seviyedeki `float x = <kesin float>`
  // bildirimi, SONRAKI deyimlerde x'i kesin float yapar — x dongude baska
  // hicbir yerde atanmiyor/bildirilmiyor ve kosulda/artimda gecmiyorsa.
  // (Bildirimden ONCEKI bir `x` dis kapsamdakidir; o zaman x dongude
  // yeniden baglandigi icin degismez de sayilmaz -> kesin float degil.)
  ASTNode_C *one[1] = {body};
  ASTNode_C **stmts = one;
  int ns = 1;
  if (body->type == AST_BLOCK) { stmts = body->statements; ns = body->statement_count; }
  bool past_decls = false;   // int kipi: bildirim-disi bir deyim goruldu mu
  for (int k = 0; k < ns && !c.bad; k++) {
    ASTNode_C *s = stmts ? stmts[k] : nullptr;
    if (!s) continue;
    walk_all(s, fv_visit, &c);
    if (c.bad) break;
    if (int_mode && s->type != AST_VARIABLE_DECL) {
      if (!past_decls && s->type == AST_ASSIGNMENT && s->left &&
          s->left->type == AST_ARRAY_ACCESS)
        p->deopt_write = s;
      past_decls = true;
    }
    if (s->type == AST_VARIABLE_DECL && s->name &&
        s->data_type == (int_mode ? TYPE_INT : TYPE_FLOAT) &&
        s->right && strcmp(s->name, ivar) != 0 &&
        fv_assign_count(cond, body, incr, s->name) == 1 &&
        c.n_flocal < TULPAR_FV_MAX_INV) {
      FvNameUse u1{s->name, false}, u2{s->name, false};
      walk_all(cond, fv_visit_use, &u1);
      walk_all(incr, fv_visit_use, &u2);
      // Baslatici kesin float mi? fv_float_expr degismez adlari listeye
      // ekleyebilir; basarisiz denemenin eklediklerini geri al. (Erisimler
      // fv_visit'te zaten kaydedildi.)
      int saved_inv = p->n_inv;
      if (!u1.used && !u2.used &&
          (int_mode ? fv_int_expr(&c, s->right) : fv_float_expr(&c, s->right)))
        c.flocal[c.n_flocal++] = s->name;
      else
        p->n_inv = saved_inv;
    }
  }
  if (c.bad) return 0;
  if (p->n_acc == 0) { p->why = "dizi erisimi yok"; return 0; }
  if (int_mode && c.n_writes > 1) { p->why = "birden cok eleman yazmasi"; return 0; }
  if (int_mode && c.n_writes == 1 && !p->deopt_write) {
    p->why = "eleman yazmasi govdenin ilk bildirim-disi deyimi degil";
    return 0;
  }
  p->ivar = ivar;
  p->ub = cond->right;
  p->incl = cond->op == TOKEN_LESS_EQUAL ? 1 : 0;
  p->why = nullptr;
  return 1;
}

extern "C" int tulpar_float_loop_plan(ASTNode_C *init, ASTNode_C *cond,
                                      ASTNode_C *body, ASTNode_C *incr,
                                      TulparPureCallFn pure, void *ctx,
                                      TulparFloatLoopPlan *p) {
  return fv_plan(init, cond, body, incr, pure, ctx, p, false);
}

// INT DIZI DONGU SURUMU (2026-10-01): float planinin `int[]` ikizi. Bicim
// ayni (en icteki `for`, `X[j]` / `X[B]` / `X[B + j]`); farklar:
//   * eleman yazmasi KESIN INT (fv_int_expr) — hizli govde 32-bit depoya ham
//     i32 yaziyor;
//   * en fazla BIR eleman yazmasi ve govdenin ILK bildirim-disi deyimi.
//     Sebep: deger i32'ye sigmazsa (dizi genislemeli) hizli govde yazmayi
//     YAPMADAN genel surumun kosuluna atliyor ve tur orada BASTAN kosuyor.
//     Bu yalniz yazmadan once yan etkisiz deyim (bildirim, saf okuma) varsa
//     dogru — K201'in "ilk deyim" kurali.
// Sayisal kisim (sinir, 32-bit int depo, degismez adlarin INT etiketi)
// codegen'de dongu basinda sinanir.
extern "C" int tulpar_int_loop_plan(ASTNode_C *init, ASTNode_C *cond,
                                    ASTNode_C *body, ASTNode_C *incr,
                                    TulparPureCallFn pure, void *ctx,
                                    TulparFloatLoopPlan *p) {
  return fv_plan(init, cond, body, incr, pure, ctx, p, true);
}

// ---------------------------------------------------------------------------
// FLOAT DIZI IC ICE SURUM — cozumleme (2026-10-01).
//
// Neden: nbody'nin `advance`'i 5 cisimde i dongusu icinde 0-4 turluk bir j
// dongusu kosuyor. En ic dongu surumu (yukarida) HER j-dongusu girisinde yedi
// dizinin deposunu/sinirini ve ~20 erisimin araligini sinar; bu sinav turdan
// pahali ve genel kopya (cagrili) i dongusunun icinde kaldigi icin LLVM
// basliklari i dongusunun disina tasiyamiyor. Olculdu (Ryzen 7 9800X3D,
// 2026-10-01): sinav sabit `true`ya cekilince 187 -> 115 ms (C 115).
//
// Kanit: O = `for (int i = I0; i < UBo; i++ | i = i + K)`, i govdede
// atanmiyor, O sekil-kararli, kosul/artimda dizi erisimi yok; govdenin UST
// SEVIYE deyimleri ya tulpar_float_loop_plan'in kabul ettigi bir `for`
// (en icteki) ya da hicbir dizi erisimi/dongu icermeyen deyim. Her ic dongu
// icin: J0 = `i`, `i + c`, `c + i`, `i - c` (c int sabiti) ya da O'da
// degismez int ifadesi; UB O'da degismez; j'li erisimin tabani O'da
// degismez; j'siz erisimin tabani `i + c` bicimi ya da O'da degismez; dizi
// adlari ve degismez float adlari O'da yeniden baglanmiyor. Sayisal kisim
// (i ve j araliklarinin uc noktalari) codegen'de O basinda sinanir.
// ---------------------------------------------------------------------------
namespace {
// `i`, `i + c`, `c + i`, `i - c` -> true ve *c.
static bool fvn_i_plus_c(ASTNode_C *n, const char *ivar, long long *c) {
  auto is_i = [&](ASTNode_C *x) {
    return x && x->type == AST_IDENTIFIER && x->name && strcmp(x->name, ivar) == 0;
  };
  auto is_lit = [](ASTNode_C *x) { return x && x->type == AST_INT_LITERAL; };
  if (is_i(n)) { *c = 0; return true; }
  if (!n || n->type != AST_BINARY_OP) return false;
  if (n->op == TOKEN_PLUS && is_i(n->left) && is_lit(n->right)) {
    *c = n->right->value.int_value; return true;
  }
  if (n->op == TOKEN_PLUS && is_lit(n->left) && is_i(n->right)) {
    *c = n->left->value.int_value; return true;
  }
  if (n->op == TOKEN_MINUS && is_i(n->left) && is_lit(n->right)) {
    *c = -n->right->value.int_value; return true;
  }
  return false;
}
static bool fvn_has_loop_or_access(ASTNode_C *n, void *p) {
  if (n->type == AST_ARRAY_ACCESS || n->type == AST_FOR || n->type == AST_WHILE ||
      n->type == AST_FOR_IN) {
    *(bool *)p = true;
    return false;
  }
  return true;
}
}  // namespace

extern "C" int tulpar_float_nest_plan(ASTNode_C *init, ASTNode_C *cond,
                                      ASTNode_C *body, ASTNode_C *incr,
                                      TulparPureCallFn pure, void *ctx,
                                      TulparFloatNestPlan *np) {
  if (!np) return 0;
  memset(np, 0, sizeof(*np));
  np->why = "bicim";
  if (!init || !cond || !incr || !body) return 0;
  if (init->type != AST_VARIABLE_DECL || !init->name || !init->right) {
    np->why = "init `int i = ...` degil";
    return 0;
  }
  const char *ivar = init->name;
  TulparFloatLoopPlan scratch;
  memset(&scratch, 0, sizeof(scratch));
  FvCtx co{cond, body, incr, ivar, &scratch, {}, 0, false};   // O kapsami
  if (cond->type != AST_BINARY_OP ||
      (cond->op != TOKEN_LESS && cond->op != TOKEN_LESS_EQUAL) ||
      !fv_is_ivar(&co, cond->left) || !fv_inv_int(&co, cond->right, true)) {
    np->why = "dis kosul `i < UB` (UB degismez) degil";
    return 0;
  }
  bool incr_ok = false;
  if (incr->type == AST_INCREMENT && incr->name && !incr->left &&
      strcmp(incr->name, ivar) == 0) {
    incr_ok = true;
  } else if (incr->type == AST_ASSIGNMENT && incr->name && !incr->left &&
             strcmp(incr->name, ivar) == 0 && incr->right &&
             incr->right->type == AST_BINARY_OP && incr->right->op == TOKEN_PLUS &&
             fv_is_ivar(&co, incr->right->left) && incr->right->right &&
             incr->right->right->type == AST_INT_LITERAL &&
             incr->right->right->value.int_value > 0 &&
             incr->right->right->value.int_value <= 2147483648LL) {
    incr_ok = true;
  }
  if (!incr_ok) { np->why = "dis artim `i++` / `i = i + K` degil"; return 0; }
  if (fv_assign_count(nullptr, body, nullptr, ivar) != 0) {
    np->why = "dis dongu degiskeni govdede ataniyor";
    return 0;
  }
  bool acc_ci = false;
  walk_all(cond, fv_has_access, &acc_ci);
  walk_all(incr, fv_has_access, &acc_ci);
  if (acc_ci) { np->why = "dis kosulda/artimda dizi erisimi"; return 0; }
  if (!tulpar_loop_shape_stable(cond, body, incr, pure, ctx)) {
    np->why = "dis govde dizi seklini degistirebilir";
    return 0;
  }
  ASTNode_C *one[1] = {body};
  ASTNode_C **stmts = one;
  int ns = 1;
  if (body->type == AST_BLOCK) { stmts = body->statements; ns = body->statement_count; }
  for (int k = 0; k < ns; k++) {
    ASTNode_C *s = stmts ? stmts[k] : nullptr;
    if (!s) continue;
    if (s->type == AST_FOR) {
      if (np->n_inner >= TULPAR_FVN_MAX_INNER) { np->why = "ic dongu tavani"; return 0; }
      int m = np->n_inner;
      TulparFloatLoopPlan *p = &np->plan[m];
      if (!tulpar_float_loop_plan(s->init, s->condition, s->body, s->increment, pure, ctx, p)) {
        np->why = "ic dongu planlanamiyor";
        return 0;
      }
      // J0
      long long c = 0;
      if (fvn_i_plus_c(s->init->right, ivar, &c)) {
        np->j0_dep[m] = 1;
        np->j0_c[m] = c;
      } else if (!fv_inv_int(&co, s->init->right, false)) {
        np->why = "ic baslangic i + c ya da degismez degil";
        return 0;
      }
      if (!fv_inv_int(&co, p->ub, true)) { np->why = "ic ust sinir dista degismez degil"; return 0; }
      for (int a = 0; a < p->n_arr; a++)
        if (!fv_invariant(&co, p->arr[a])) { np->why = "dizi dista yeniden baglaniyor"; return 0; }
      for (int a = 0; a < p->n_inv; a++)
        if (!fv_invariant(&co, p->inv_float[a])) { np->why = "degismez float dista ataniyor"; return 0; }
      for (int a = 0; a < p->n_acc; a++) {
        ASTNode_C *b = p->acc_base[a];
        if (!b) continue;
        if (!p->acc_has_j[a] && fvn_i_plus_c(b, ivar, &c)) {
          np->b_dep[m][a] = 1;
          np->b_c[m][a] = c;
        } else if (!fv_inv_int(&co, b, false)) {
          np->why = "erisim tabani dista degismez / i + c degil";
          return 0;
        }
      }
      np->inner[m] = s;
      np->n_inner++;
    } else {
      bool bad = false;
      walk_all(s, fvn_has_loop_or_access, &bad);
      if (bad) { np->why = "dis govdede ic dongu disinda dizi erisimi/dongu"; return 0; }
    }
  }
  if (np->n_inner == 0) { np->why = "ic dongu yok"; return 0; }
  np->ivar = ivar;
  np->ub = cond->right;
  np->incl = cond->op == TOKEN_LESS_EQUAL ? 1 : 0;
  np->why = nullptr;
  return 1;
}

// Programdaki `float[]` bildirimlerinin adlari (parametreler dahil). Surum
// karari icin yalniz IPUCU: dogruluk dongu basindaki calisma zamani
// sinavindan gelir. Ipucu sart, cunku float OLMAYAN dizilerde bir govde
// kopyasi daha yalniz kod buyutur (Performance.md: kosmayan kopya bile dis
// donguyu yavaslatabiliyor) — int dongulerine dokunmamak icin.
struct FvHintCtx {
  void (*cb)(const char *, void *);
  void *ctx;
};
static bool fv_visit_hint(ASTNode_C *n, void *p) {
  FvHintCtx *h = (FvHintCtx *)p;
  if (n->name && n->data_type == TYPE_ARRAY_FLOAT &&
      n->type != AST_FUNCTION_DECL && n->type != AST_FUNCTION_CALL)
    h->cb(n->name, h->ctx);
  return true;
}
extern "C" void tulpar_collect_float_array_decls(ASTNode_C *root,
                                                 void (*cb)(const char *, void *),
                                                 void *ctx) {
  FvHintCtx h{cb, ctx};
  walk_all(root, fv_visit_hint, &h);
}

// Genel gezici (ust duzey degiskenlerin main-yereline terfisi, 2026-10-01).
// walk_all'in kendisi: cocuk alan listesi TEK yerde kalsin, denetim
// (tests/ast_child_fields_audit.py) onu sinamaya devam etsin.
namespace {
struct WalkAdapt {
  int (*visit)(ASTNode_C *, void *);
  void *ctx;
};
bool walk_adapt(ASTNode_C *n, void *p) {
  WalkAdapt *w = (WalkAdapt *)p;
  return w->visit(n, w->ctx) != 0;
}
}  // namespace

extern "C" int tulpar_ast_walk(ASTNode_C *n, int (*visit)(ASTNode_C *, void *),
                               void *ctx) {
  WalkAdapt w{visit, ctx};
  return walk_all(n, walk_adapt, &w) ? 1 : 0;
}

// ===========================================================================
// INT YEREL GOLGE SURUMU (2026-10-01) — kanitin BICIM kismi.
//
// Neden: bir fonksiyonun ICINDE `int i = lo;` kutulu bir VMValue yuvasidir
// (etiket + yuk). `int` bildirimi degeri ZORLAMIYOR: dizgi gelirse dizgi kalir
// (`int k = d["x"]` -> "abc", `k + 1` -> "abc1"; olculdu) — yani tip bilgisi
// statik bir garanti degil. Sonuc: dongudeki her `i + 1`, `a[i] < p`, `i <= j`
// iki etiket okumasi + tur dallanmasi + `vm_binary_op` geri dusus cagrisi
// uretiyor ve cagrinin VARLIGI LLVM'in degerleri yazmacta tutmasini
// engelliyor. Ayni qsort ust duzeyde (main yerelleri native) 85 ms, fonksiyon
// icinde 125 ms (2026-10-01, Ryzen 7 9800X3D).
//
// Kanit (EN DIS uygun dongu icin, bir kez):
//   * Aday adlar (P): dongu basinda TIV_CAND (bu fonksiyonun kutulu `int`
//     yereli/parametresi) ya da dongude YENI bildirilen `int` yereller.
//   * P'deki bir adin dongudeki HER baglanmasi kesin INT uretir: bildirim
//     `int x = E` / `int x;`, atama `x = E`, `x += E` / `-=` / `*=`, `x++`.
//     E "kesin INT" (iv_int_closed): int sabiti, P'deki ad, native int ad,
//     tekli eksi, `+ - *`, ve V'deki bir dizinin kesin-INT indeksli okumasi.
//   * V: `int[]` bildirilmis, dongude yeniden baglanmayan diziler — dongu
//     sekil-kararliysa (kullanici cagrisi yok) VE dongudeki her eleman
//     yazmasi kesin INT ise (takma ad: baska bir ad ayni diziyi gosterebilir,
//     o yuzden YALNIZ V'ye degil butun yazmalara bakiliyor). O zaman dongu
//     basinda kutusuz int depodaki bir dizi dongu boyunca oyle kalir ve her
//     okumasi INT'tir (sinir disi: istisna; yumusak kipte VM_INT(0)).
// Sabit nokta: bir adin kaniti duserse ona dayanan digerleri de yeniden
// sinanir; bir eleman yazmasi duserse V bosalir.
//
// Sayisal kisim calisma zamaninda: codegen dongu basinda P'nin dis adlarinin
// etiketini INT, okunan V dizilerinin deposunu kutusuz int diye sinar; tutarsa
// HIZLI kopya (adlar native i64 golgede, cikista kutulu yuvaya geri yazilir),
// tutmazsa bugunku kod (SOGUK kopya). Kanit muhafazakar: taninmayan dugum
// (kapanis, try, match, await, for-in) butun donguyu reddeder.
//
// KAPSAM TUZAGI: `while`/blok kapsam ACMAZ (yalniz fonksiyon, `for`, main) —
// dongu govdesindeki `int t` donguden SONRA da gorunur. Hizli kopyada t native
// bir yuvada yasiyor, soguk kopyada kutulu; donguden sonraki bir okuma hangisini
// gorecegini bilemez. O yuzden dongude bildirilen ad ya bir `for` kapsaminin
// icinde olmali ya da fonksiyonda dongu disinda HIC gecmemeli.
// ===========================================================================
namespace {

struct IvCtx {
  TulparIvClassFn cls;
  void *ctx;
  TulparIntLocalPlan *p;
  ASTNode_C *loop;
  ASTNode_C *fn_body;
  const char *nm[TULPAR_IV_MAX_NAMES];
  int ncls[TULPAR_IV_MAX_NAMES];
  bool declared[TULPAR_IV_MAX_NAMES];
  bool bound[TULPAR_IV_MAX_NAMES];
  bool in_p[TULPAR_IV_MAX_NAMES];
  signed char outside[TULPAR_IV_MAX_NAMES];   // iv_used_outside onbellegi (-1 bilinmiyor)
  int n;
  bool v_on;
  bool changed;
  bool bad;
  // `for` kapsaminda kalan bildirimler (donguyle birlikte olur).
  ASTNode_C *scoped[TULPAR_IV_MAX_NODES];
  int n_scoped;
};

static bool iv_reject(IvCtx *c, const char *why) {
  if (!c->bad) { c->bad = true; c->p->why = why; }
  return false;
}

static int iv_find(IvCtx *c, const char *name) {
  if (!name) return -1;
  for (int i = 0; i < c->n; i++)
    if (strcmp(c->nm[i], name) == 0) return i;
  return -1;
}

static int iv_add(IvCtx *c, const char *name) {
  int k = iv_find(c, name);
  if (k >= 0 || !name) return k;
  if (c->n >= TULPAR_IV_MAX_NAMES) { iv_reject(c, "ad tavani"); return -1; }
  c->nm[c->n] = name;
  c->ncls[c->n] = TIV_NONE;
  c->declared[c->n] = c->bound[c->n] = c->in_p[c->n] = false;
  c->outside[c->n] = -1;
  return c->n++;
}

static const char *iv_base(ASTNode_C *acc) {
  if (!acc || acc->type != AST_ARRAY_ACCESS) return nullptr;
  if (acc->name) return acc->name;
  if (acc->left && acc->left->type == AST_IDENTIFIER) return acc->left->name;
  return nullptr;
}

// Atamanin hedef ADI (eleman yazmasi degilse). Parser adi ya dugumun kendisine
// ya da sol cocuga (AST_IDENTIFIER) koyuyor.
static const char *iv_target_name(ASTNode_C *n) {
  if (n->left && n->left->type == AST_ARRAY_ACCESS) return nullptr;
  if (n->name) return n->name;
  if (n->left && n->left->type == AST_IDENTIFIER) return n->left->name;
  return nullptr;
}

static bool iv_is_elem_write(ASTNode_C *n) {
  return (n->type == AST_ASSIGNMENT || n->type == AST_COMPOUND_ASSIGN ||
          n->type == AST_INCREMENT || n->type == AST_DECREMENT) &&
         n->left && n->left->type == AST_ARRAY_ACCESS;
}

static bool iv_collect(ASTNode_C *n, void *pv) {
  IvCtx *c = (IvCtx *)pv;
  switch (n->type) {
  case AST_LAMBDA:
  case AST_TRY_CATCH:
  case AST_MATCH:
  case AST_AWAIT:
  case AST_FOR_IN:
  case AST_FUNCTION_DECL:
  case AST_TYPE_DECL:
  case AST_IMPORT:
    return iv_reject(c, "desteklenmeyen dugum (kapanis/try/match/await/for-in)");
  case AST_IDENTIFIER:
    iv_add(c, n->name);
    break;
  case AST_VARIABLE_DECL: {
    int k = iv_add(c, n->name);
    if (k >= 0) c->declared[k] = c->bound[k] = true;
    break;
  }
  case AST_ASSIGNMENT:
  case AST_COMPOUND_ASSIGN:
  case AST_INCREMENT:
  case AST_DECREMENT: {
    int k = iv_add(c, iv_target_name(n));
    if (k >= 0) c->bound[k] = true;
    break;
  }
  case AST_ARRAY_ACCESS:
    iv_add(c, iv_base(n));
    break;
  default:
    break;
  }
  return !c->bad;
}

// `for` dugumunun altindaki bildirimler o `for`un kapsaminda kalir.
static bool iv_mark_scoped(ASTNode_C *n, void *pv) {
  IvCtx *c = (IvCtx *)pv;
  if (n->type == AST_VARIABLE_DECL) {
    for (int i = 0; i < c->n_scoped; i++)
      if (c->scoped[i] == n) return true;
    if (c->n_scoped >= TULPAR_IV_MAX_NODES) return iv_reject(c, "bildirim tavani");
    c->scoped[c->n_scoped++] = n;
  }
  return true;
}
static bool iv_find_fors(ASTNode_C *n, void *pv) {
  IvCtx *c = (IvCtx *)pv;
  if (n->type == AST_FOR) {
    walk_all(n->init, iv_mark_scoped, c);
    walk_all(n->condition, iv_mark_scoped, c);
    walk_all(n->increment, iv_mark_scoped, c);
    walk_all(n->body, iv_mark_scoped, c);
  }
  return !c->bad;
}
static bool iv_is_scoped(IvCtx *c, ASTNode_C *decl) {
  for (int i = 0; i < c->n_scoped; i++)
    if (c->scoped[i] == decl) return true;
  return false;
}

struct IvCount {
  const char *name;
  int count;
};
static bool iv_count_name(ASTNode_C *n, void *pv) {
  IvCount *c = (IvCount *)pv;
  if (n->name && strcmp(n->name, c->name) == 0) c->count++;
  return true;
}
// Ad fonksiyonda dongu DISINDA geciyor mu? (toplam - dongu ici; fonksiyon
// govdesi bilinmiyorsa "evet" — muhafazakar.)
static bool iv_used_outside(IvCtx *c, const char *name) {
  if (!c->fn_body) return true;
  int k = iv_find(c, name);
  if (k >= 0 && c->outside[k] >= 0) return c->outside[k] != 0;
  IvCount all{name, 0}, in{name, 0};
  walk_all(c->fn_body, iv_count_name, &all);
  walk_all(c->loop, iv_count_name, &in);
  bool out = all.count != in.count;
  if (k >= 0) c->outside[k] = out ? 1 : 0;
  return out;
}

// Dongunun dugum sayisi (kod buyumesi siniri icin).
static bool iv_count_node(ASTNode_C *, void *pv) {
  ++*(int *)pv;
  return true;
}

static bool iv_int_closed(IvCtx *c, ASTNode_C *e);

// V'deki bir dizinin kesin-INT indeksli okumasi.
static bool iv_acc_closed(IvCtx *c, ASTNode_C *e) {
  if (!c->v_on || !e || e->type != AST_ARRAY_ACCESS) return false;
  if (!e->index || e->index->type == AST_STRING_LITERAL) return false;
  // `a[i][j]`: taban bir ad degil.
  if (e->left && e->left->type != AST_IDENTIFIER) return false;
  int k = iv_find(c, iv_base(e));
  if (k < 0 || c->ncls[k] != TIV_ARR || c->bound[k]) return false;
  return iv_int_closed(c, e->index);
}

static bool iv_int_closed(IvCtx *c, ASTNode_C *e) {
  if (!e) return false;
  switch (e->type) {
  case AST_INT_LITERAL:
    return true;
  case AST_IDENTIFIER: {
    int k = iv_find(c, e->name);
    if (k < 0) return false;
    if (c->in_p[k]) return true;
    return c->ncls[k] == TIV_NATIVE && !c->declared[k];
  }
  case AST_UNARY_OP:
    return e->op == TOKEN_MINUS && iv_int_closed(c, e->left);
  case AST_BINARY_OP:
    return (e->op == TOKEN_PLUS || e->op == TOKEN_MINUS || e->op == TOKEN_MULTIPLY) &&
           iv_int_closed(c, e->left) && iv_int_closed(c, e->right);
  case AST_ARRAY_ACCESS:
    return iv_acc_closed(c, e);
  default:
    return false;
  }
}

static bool iv_compound_ok(int op) {
  return op == TOKEN_PLUS_EQUAL || op == TOKEN_MINUS_EQUAL || op == TOKEN_MULTIPLY_EQUAL;
}

static void iv_drop(IvCtx *c, int k) {
  if (k >= 0 && c->in_p[k]) { c->in_p[k] = false; c->changed = true; }
}

// Sabit nokta adimi: P'deki adlarin baglanmalari ve eleman yazmalari.
static bool iv_check(ASTNode_C *n, void *pv) {
  IvCtx *c = (IvCtx *)pv;
  if (iv_is_elem_write(n)) {
    if (!c->v_on) return true;
    ASTNode_C *t = n->left;
    if (t->index && t->index->type == AST_STRING_LITERAL) return true;   // dizide hata
    int k = (t->left && t->left->type != AST_IDENTIFIER) ? -1 : iv_find(c, iv_base(t));
    if (k >= 0 && (c->ncls[k] == TIV_STRUCT || c->ncls[k] == TIV_NATIVE)) return true;
    bool ok = n->type == AST_INCREMENT || n->type == AST_DECREMENT ||
              (n->type == AST_ASSIGNMENT && iv_int_closed(c, n->right)) ||
              (n->type == AST_COMPOUND_ASSIGN && iv_compound_ok(n->op) &&
               iv_int_closed(c, n->right));
    if (!ok) { c->v_on = false; c->changed = true; }
    return true;
  }
  switch (n->type) {
  case AST_VARIABLE_DECL: {
    int k = iv_find(c, n->name);
    if (k < 0 || !c->in_p[k]) return true;
    bool ok = n->data_type == TYPE_INT && (!n->right || iv_int_closed(c, n->right)) &&
              (iv_is_scoped(c, n) || !iv_used_outside(c, n->name));
    if (!ok) iv_drop(c, k);
    return true;
  }
  case AST_ASSIGNMENT: {
    int k = iv_find(c, iv_target_name(n));
    if (k >= 0 && c->in_p[k] && !iv_int_closed(c, n->right)) iv_drop(c, k);
    return true;
  }
  case AST_COMPOUND_ASSIGN: {
    int k = iv_find(c, iv_target_name(n));
    if (k >= 0 && c->in_p[k] && !(iv_compound_ok(n->op) && iv_int_closed(c, n->right)))
      iv_drop(c, k);
    return true;
  }
  default:
    return true;
  }
}

static bool iv_record(ASTNode_C *n, void *pv) {
  IvCtx *c = (IvCtx *)pv;
  TulparIntLocalPlan *p = c->p;
  if (n->type == AST_ARRAY_ACCESS && iv_acc_closed(c, n)) {
    if (p->n_acc >= TULPAR_IV_MAX_NODES) return iv_reject(c, "okuma tavani");
    p->acc[p->n_acc++] = n;
    const char *b = iv_base(n);
    bool have = false;
    for (int i = 0; i < p->n_arr; i++) have = have || strcmp(p->arr[i], b) == 0;
    if (!have) {
      if (p->n_arr >= TULPAR_IV_MAX_ARR) return iv_reject(c, "dizi tavani");
      p->arr[p->n_arr++] = b;
    }
  } else if (n->type == AST_VARIABLE_DECL) {
    int k = iv_find(c, n->name);
    if (k >= 0 && c->in_p[k]) {
      if (p->n_decl >= TULPAR_IV_MAX_NODES) return iv_reject(c, "bildirim tavani");
      p->decl[p->n_decl++] = n;
    }
  } else if (n->type == AST_ASSIGNMENT && iv_is_elem_write(n) &&
             iv_int_closed(c, n->right)) {
    if (p->n_ewr >= TULPAR_IV_MAX_NODES) return iv_reject(c, "yazma tavani");
    p->ewr[p->n_ewr++] = n;
  }
  return !c->bad;
}

}  // namespace

extern "C" int tulpar_int_local_plan(ASTNode_C *loop, ASTNode_C *fn_body,
                                     TulparPureCallFn pure, TulparIvClassFn cls,
                                     void *ctx, TulparIntLocalPlan *p) {
  if (!p) return 0;
  memset(p, 0, sizeof(*p));
  p->why = "bicim";
  if (!loop || !cls || (loop->type != AST_WHILE && loop->type != AST_FOR)) return 0;
  IvCtx *c = new IvCtx();
  c->cls = cls;
  c->ctx = ctx;
  c->p = p;
  c->loop = loop;
  c->fn_body = fn_body;
  int ok = 0;
  ASTNode_C *init = loop->type == AST_FOR ? loop->init : nullptr;
  ASTNode_C *incr = loop->type == AST_FOR ? loop->increment : nullptr;
  ASTNode_C *parts[] = {init, loop->condition, incr, loop->body};
  // KOD BUYUMESI SINIRI: surum donguyu IKI kez uretiyor (hizli + soguk).
  // Buyuk bir oyun dongusunu kopyalamak derleme suresini ve ikiliyi
  // buyutur; sicak tamsayi dongulerinin hepsi bu sinirin cok altinda
  // (qsort'un bolme dongusu 56, int matmul'un dis dongusu 66 dugum).
  int nodes = 0;
  walk_all(loop, iv_count_node, &nodes);
  p->n_nodes = nodes;
  if (nodes > TULPAR_IV_MAX_LOOP_NODES) iv_reject(c, "dongu cok buyuk (kod buyumesi siniri)");
  for (ASTNode_C *part : parts) walk_all(part, iv_collect, c);
  if (!c->bad) {
    if (loop->type == AST_FOR) iv_find_fors(loop, c);
    for (ASTNode_C *part : parts) walk_all(part, iv_find_fors, c);
  }
  if (!c->bad) {
    for (int k = 0; k < c->n; k++) {
      c->ncls[k] = cls(c->nm[k], ctx);
      c->in_p[k] = (c->ncls[k] == TIV_CAND && !c->declared[k]) ||
                   (c->ncls[k] == TIV_NONE && c->declared[k]);
    }
    // V yalniz sekil-kararli dongude (kullanici cagrisi diziye yazabilir).
    c->v_on = tulpar_loop_shape_stable(init, nullptr, nullptr, pure, ctx) &&
              tulpar_loop_shape_stable(loop->condition, loop->body, incr, pure, ctx);
    do {
      c->changed = false;
      for (ASTNode_C *part : parts) walk_all(part, iv_check, c);
    } while (c->changed);
    for (ASTNode_C *part : parts) walk_all(part, iv_record, c);
  }
  if (!c->bad) {
    for (int k = 0; k < c->n; k++) {
      if (!c->in_p[k] || c->ncls[k] != TIV_CAND) continue;
      if (p->n_cand >= TULPAR_IV_MAX_NAMES) { iv_reject(c, "aday tavani"); break; }
      p->cand[p->n_cand++] = c->nm[k];
    }
  }
  if (!c->bad) {
    if (p->n_cand + p->n_decl == 0) {
      p->why = "golgelenecek int yerel yok";
    } else if (p->n_acc == 0) {
      // Kesin-INT dizi okumasi olmayan dongu (sayac + cagri, float dongusu):
      // kazanc tur basina bir etiket sinavi, bedeli donguyu ikiye katlamak.
      // Olculdu (2026-10-01): bu kosul yokken scene3d_editor'un derlemesi
      // 10,4 -> 11,6 s ve ikilisi %2,4 buyudu.
      p->why = "kesin-INT dizi okumasi yok (kopya kazanc getirmez)";
    } else {
      p->why = nullptr;
      ok = 1;
    }
  }
  delete c;
  return ok;
}

// `int[]` bildirim/parametre adlari (V icin ipucu; dogruluk depo sinavindan).
struct IvHintCtx {
  void (*cb)(const char *, void *);
  void *ctx;
};
static bool iv_visit_hint(ASTNode_C *n, void *p) {
  IvHintCtx *h = (IvHintCtx *)p;
  if (n->name && n->data_type == TYPE_ARRAY_INT &&
      n->type != AST_FUNCTION_DECL && n->type != AST_FUNCTION_CALL)
    h->cb(n->name, h->ctx);
  return true;
}
extern "C" void tulpar_collect_int_array_decls(ASTNode_C *root,
                                               void (*cb)(const char *, void *),
                                               void *ctx) {
  IvHintCtx h{cb, ctx};
  walk_all(root, iv_visit_hint, &h);
}

// ===========================================================================
// STRUCT DIZISI DONGU SURUMU (2026-10-02) — kanitin BICIM kismi.
//
// Neden: `ps[i].x` erisimi SarrCacheScope sayesinde basligi dongu basinda bir
// kez okuyor ama HER erisimde `i <u count` sinavi ve yavas yol (runtime
// cagrisi, hata tanisi) kopyasi kaliyor. particles'in ic dongusunde tur basina
// 10 alan erisimi; LLVM sinavlarin cogunu birlestiriyor ama 3 dal ve dongu
// sinirinin (`n` global) her turda bellekten okunmasi kaliyor — yavas yoldaki
// cagri her seyi yazabilir sayiliyor.
//
// Kanit: `for (<i> = E; i < UB; i = i + K)` (ya da `<=`, `i++`, `i += K`;
// K > 0 int sabiti) ve
//   * i govdede HIC atanmiyor / artirilmiyor / yeniden bildirilmiyor;
//   * UB bir int sabiti, dongude atanmayan bir AD ya da `len(X)` (X dongude
//     yeniden baglanmiyor);
//   * dongu sekli degistiremiyor (tulpar_loop_shape_stable — cagri yalniz
//     elle dogrulanmis yerlesik; push/pop/kullanici fonksiyonu yok).
// O zaman govdede i, [i0, UB') araliginda (UB' = `<=` ise UB + 1): i0 >= 0 ve
// UB' <= count(A) dongu basinda sinanirsa her `A[i]` sinir icinde. Sayisal
// kisim codegen'de (sv_try_version). Plan yalniz `A[i]` (indeks CIPLAK i)
// erisimlerinin dizi adlarini toplar; struct dizisi olup olmadiklarina codegen
// karar verir (sekil onbellegi).
struct SvCtx {
  const char *ivar;
  TulparSarrLoopPlan *p;
  int nodes;
};

// Sert yeniden baglama denetimi: tulpar_loop_rebinds_name `x++`/`x--`i
// saymiyor; burada sayiyoruz (bir atama bicimi kacarsa kanit delinir).
struct SvRebindCtx {
  const char *name;
  bool hit;
};
static bool sv_visit_rebind(ASTNode_C *n, void *p) {
  SvRebindCtx *c = (SvRebindCtx *)p;
  if ((n->type == AST_ASSIGNMENT || n->type == AST_COMPOUND_ASSIGN ||
       n->type == AST_VARIABLE_DECL || n->type == AST_INCREMENT ||
       n->type == AST_DECREMENT) &&
      n->name && strcmp(n->name, c->name) == 0) {
    c->hit = true;
    return false;
  }
  return true;
}
static bool sv_rebinds(ASTNode_C *n, const char *name) {
  SvRebindCtx c{name, false};
  walk_all(n, sv_visit_rebind, &c);
  return c.hit;
}

static bool sv_visit_acc(ASTNode_C *n, void *p) {
  SvCtx *c = (SvCtx *)p;
  c->nodes++;
  if (n->type == AST_ARRAY_ACCESS && n->index && n->index->type == AST_IDENTIFIER &&
      n->index->name && strcmp(n->index->name, c->ivar) == 0) {
    const char *base = nullptr;
    if (n->name) base = n->name;
    else if (n->left && n->left->type == AST_IDENTIFIER) base = n->left->name;
    if (base) {
      TulparSarrLoopPlan *pl = c->p;
      bool seen = false;
      for (int k = 0; k < pl->n_arr; k++)
        if (strcmp(pl->arr[k], base) == 0) seen = true;
      if (!seen && pl->n_arr < TULPAR_SV_MAX_ARR) pl->arr[pl->n_arr++] = base;
      pl->n_acc++;
    }
  }
  return true;
}

// `for` ve `while` bicimlerinin ORTAK kaniti: kosul `i < UB` / `i <= UB`, UB
// sabit / dongude atanmayan ad / len(X); artim `incr`; `i` `rebind[]`
// dugumlerinde (artim HARIC govde) hic baglanmiyor; govde sekli kararli.
static int sv_plan_core(const char *ivar, ASTNode_C *cond, ASTNode_C *body, ASTNode_C *incr,
                        ASTNode_C *const *rebind, int n_rebind, TulparPureCallFn pure,
                        void *ctx, TulparSarrLoopPlan *p) {
  if (!ivar || !cond || !incr || !body) return 0;
  if (cond->type != AST_BINARY_OP || (cond->op != TOKEN_LESS && cond->op != TOKEN_LESS_EQUAL))
    return 0;
  ASTNode_C *lhs = cond->left, *rhs = cond->right;
  if (!lhs || lhs->type != AST_IDENTIFIER || !lhs->name || strcmp(lhs->name, ivar) != 0 || !rhs)
    return 0;
  if (rhs->type == AST_INT_LITERAL) {
    // sabit sinir
  } else if (rhs->type == AST_IDENTIFIER && rhs->name && strcmp(rhs->name, ivar) != 0) {
    if (sv_rebinds(body, rhs->name) || sv_rebinds(incr, rhs->name)) {
      p->why = "sinir adi dongude ataniyor";
      return 0;
    }
  } else if (rhs->type == AST_FUNCTION_CALL && rhs->name && !rhs->receiver &&
             (strcmp(rhs->name, "len") == 0 || strcmp(rhs->name, "length") == 0) &&
             rhs->argument_count == 1 && rhs->arguments && rhs->arguments[0] &&
             rhs->arguments[0]->type == AST_IDENTIFIER && rhs->arguments[0]->name &&
             pure && pure(rhs->name, ctx)) {
    if (sv_rebinds(body, rhs->arguments[0]->name) || sv_rebinds(incr, rhs->arguments[0]->name)) {
      p->why = "sinir dizisi dongude yeniden baglaniyor";
      return 0;
    }
  } else {
    p->why = "sinir sabit / ad / len(X) degil";
    return 0;
  }
  // incr: `i++`, `i = i + K`, `i += K` (K > 0 int sabiti)
  bool incr_ok = false;
  if (incr->type == AST_INCREMENT && incr->name && !incr->left && strcmp(incr->name, ivar) == 0) {
    incr_ok = true;
  } else if (incr->type == AST_ASSIGNMENT && incr->name && !incr->left &&
             strcmp(incr->name, ivar) == 0 && incr->right &&
             incr->right->type == AST_BINARY_OP && incr->right->op == TOKEN_PLUS) {
    ASTNode_C *a = incr->right->left, *b = incr->right->right;
    incr_ok = a && b && a->type == AST_IDENTIFIER && a->name && strcmp(a->name, ivar) == 0 &&
              b->type == AST_INT_LITERAL && b->value.int_value > 0;
  } else if (incr->type == AST_COMPOUND_ASSIGN && incr->name && !incr->left &&
             strcmp(incr->name, ivar) == 0 && incr->op == TOKEN_PLUS_EQUAL && incr->right &&
             incr->right->type == AST_INT_LITERAL && incr->right->value.int_value > 0) {
    incr_ok = true;
  }
  if (!incr_ok) {
    p->why = "artim i++ / i = i + K / i += K degil";
    return 0;
  }
  for (int r = 0; r < n_rebind; r++)
    if (sv_rebinds(rebind[r], ivar)) {
      p->why = "dongu degiskeni govdede ataniyor";
      return 0;
    }
  if (!tulpar_loop_shape_stable(cond, body, incr, pure, ctx)) {
    p->why = "govde sekli degistirebilir (cagri / yeni kap)";
    return 0;
  }
  SvCtx c{ivar, p, 0};
  walk_all(body, sv_visit_acc, &c);
  p->n_nodes = c.nodes;
  if (p->n_arr == 0) {
    p->why = "govdede `X[i]` erisimi yok";
    return 0;
  }
  if (c.nodes > TULPAR_IV_MAX_LOOP_NODES) {
    p->why = "dongu cok buyuk (kod buyumesi siniri)";
    return 0;
  }
  p->ivar = ivar;
  p->ub = rhs;
  p->incl = cond->op == TOKEN_LESS_EQUAL ? 1 : 0;
  p->why = nullptr;
  return 1;
}

extern "C" int tulpar_sarr_loop_plan(ASTNode_C *init, ASTNode_C *cond,
                                     ASTNode_C *body, ASTNode_C *incr,
                                     TulparPureCallFn pure, void *ctx,
                                     TulparSarrLoopPlan *p) {
  memset(p, 0, sizeof(*p));
  p->why = "bicim";
  if (!init || !cond || !incr || !body) return 0;
  // init: `int i = E` ya da `i = E` (E dongu basinda bir kez; codegen i'nin
  // INT ve >= 0 oldugunu sinar).
  if ((init->type != AST_VARIABLE_DECL && init->type != AST_ASSIGNMENT) || !init->name ||
      init->left)
    return 0;
  ASTNode_C *rb[] = {body};
  return sv_plan_core(init->name, cond, body, incr, rb, 1, pure, ctx, p);
}

// `while (i < UB) { ...; i = i + K; }` (2026-10-06): artim govdenin SON deyimi,
// oncesinde `i` hic baglanmiyor. Govde (artim dahil) `while` olarak uretilir
// — `continue` artimi atlar ve `i` degismez: aralik kaniti yine gecerli (i
// yalniz artiyor ya da duruyor, her erisim `i < UB` iken). Baslangic degeri
// dongu basinda sinanir (i INT ve >= 0), `for` ile ayni.
extern "C" int tulpar_sarr_while_plan(ASTNode_C *cond, ASTNode_C *body, TulparPureCallFn pure,
                                      void *ctx, TulparSarrLoopPlan *p) {
  memset(p, 0, sizeof(*p));
  p->why = "bicim";
  if (!cond || !body || body->type != AST_BLOCK || body->statement_count < 1 || !body->statements)
    return 0;
  if (cond->type != AST_BINARY_OP || !cond->left || cond->left->type != AST_IDENTIFIER ||
      !cond->left->name)
    return 0;
  ASTNode_C *incr = body->statements[body->statement_count - 1];
  if (!incr) return 0;
  return sv_plan_core(cond->left->name, cond, body, incr, body->statements,
                      body->statement_count - 1, pure, ctx, p);
}
