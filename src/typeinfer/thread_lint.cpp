// Paylasilan-global lint'i — "yazan thread, okuyan thread" tespiti.
//
// NEDEN BU LINT VAR. Olculdu (docs/mindmap/Concurrency.md, Bulgu 2):
//
//     int fin = 0;
//     func w(int id) { fin = 1; return 0; }
//     thread_create(w, 0);
//     while (fin == 0 && s < 200000000) { s = s + 1; }   // ASLA cikmaz
//
// Isci gercekten kosuyor ve `fin`e yaziyor; ana thread'in dongusu HIC
// gormuyor, cunku optimize edici global okumasini dongunun disina cikarip
// yazmacta tutuyor. Bu bir derleyici hatasi DEGIL: dilin bellek modeli yok
// (`volatile`/atomik/ordering kavrami yok), yani davranis TANIMSIZ. Tanimsizin
// bedeli, sessizce sonsuza kadar donen bir programdir.
//
// NEDEN "YAZMA TARAFI" TEK BASINA YETMEZDI. Bu lint bir tur ertelendi ve
// gerekcesi yaziliydi: hangi global'in yazildigini bulmak birkac saatlik is,
// ama tek basina YANLIS MESAJ uretir — "bu global yazMLIYOR" bir kusur degil,
// programin olagan hali. Dogru cumle OKUMA tarafini gerektiriyor: *"bu global
// baska bir thread'de yaziliyor ve burada senkronizasyonsuz okunuyor"*. Bu
// dosya o okuma tarafidir.
//
// KAPSAM (v1) BILEREK DAR. Yalniz OLCULMUS sekil: thread iscisinde yazilan
// bir global'in ana akista senkronizasyonsuz OKUNMASI. Iki isci arasindaki
// paylasim (wings'in bilerek kilitsiz sayaclari gibi) uyari URETMEZ — o
// desen belgelenmis ve kasitli. Kapsami genisletmek ayri bir karar ve ayri
// bir korpus olcumu ister.

#include "thread_lint.hpp"
#include "../common/localization.hpp"

#include <cstdio>
#include <cstdlib>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace tulpar {
namespace {

template <typename T> const T *node_as(const ASTNode *n) {
  return n ? std::get_if<T>(&n->value) : nullptr;
}

// Bir fonksiyonun (ya da ust duzeyin) dogrudan etkileri.
struct FnFacts {
  std::set<std::string> reads;
  std::set<std::string> writes;
  std::set<std::string> calls;
  std::set<std::string> locals;   // parametreler + yerel bildirimler
  std::map<std::string, int> read_line; // ilk okuma satiri
};

const char *const TOPLEVEL = "@toplevel";

struct Collector {
  std::set<std::string> globals;
  std::map<std::string, FnFacts> fns;
  std::set<std::string> thread_roots;
  std::set<std::string> known_functions;
  FnFacts *cur = nullptr;
  // KILIT KAPSAMI — fonksiyon degil, DEYIM duzeyinde.
  //
  // Ilk surum mutex'i fonksiyon bazinda tutuyordu ("bu fonksiyon kilit
  // kullaniyor") ve bir isci bir global'i kilitleyip otekini kilitlemeden
  // yazdiginda IKISINI birden susturuyordu — yani dogru is yapan kod, yanindaki
  // hatayi gizliyordu. Simdi `mutex_lock`/`mutex_unlock` bir derinlik sayaci
  // suruyor ve yalniz derinlik 0 iken yapilan erisimler KORUMASIZ sayiliyor.
  //
  // ⚠ Yaklasiktir: deyim sirasina bakar, dallanmayi/erken donusu izlemez, ve
  // hangi mutex'in hangi global'i korudugunu BILMEZ (dilde o bag yok). Yanlis
  // NEGATIF uretebilir; yanlis pozitif uretmemesi tercih edildi (#25: gurultulu
  // lint kapatilir, kapatilan lint yoktur).
  int lock_depth = 0;
  // ⚠ UST DUZEYDE BILDIRIM "YEREL" YAPMAZ.
  //
  // Ilk surumde VariableDecl kosulsuz `locals`e ekliyordu; ust duzeyde bu,
  // global'in KENDI BILDIRIMININ onu yerel isaretlemesi demekti ve sonraki
  // butun okumalari susturuyordu. Sonuc: lint Bulgu 2'nin tam sekli uzerinde
  // bile SESSIZ kaldi — yani hicbir sey olcmeden yesil gorundu.
  bool at_toplevel = false;

  void note_read(const std::string &name, int line) {
    if (!cur || cur->locals.count(name) || !globals.count(name)) return;
    if (known_functions.count(name)) return;  // fonksiyon referansi, deger degil
    if (lock_depth > 0) return;               // kilit altinda: korumali
    cur->reads.insert(name);
    if (!cur->read_line.count(name)) cur->read_line[name] = line;
  }
  void note_write(const std::string &name) {
    if (!cur || cur->locals.count(name) || !globals.count(name)) return;
    if (lock_depth > 0) return;               // kilit altinda: korumali
    cur->writes.insert(name);
  }

  // Atama hedefi: `g = ...` dogrudan yazma; `g[i] = ...` icerik mutasyonu,
  // o da yazmadir (paylasim acisindan ayni tehlike).
  void target_write(const ASTNode *t) {
    if (!t) return;
    if (const auto *id = node_as<Identifier>(t)) { note_write(id->name); return; }
    if (const auto *ac = node_as<ArrayAccess>(t)) {
      const ASTNode *base = ac->object.get();
      while (base) {
        if (const auto *bid = node_as<Identifier>(base)) { note_write(bid->name); break; }
        if (const auto *inner = node_as<ArrayAccess>(base)) { base = inner->object.get(); continue; }
        break;
      }
      walk(ac->index.get());
      return;
    }
    walk(t);
  }

  // ⚠ GEZICI HER COCUK ALANINI GEZMEK ZORUNDA.
  // Atlanan bir dal, orada duran bir yazmayi/okumayi gormez ve lint SESSIZCE
  // eksik calisir — yani yesil kalir ama bir seyi olcmez. Alan eklemek
  // serbest, atlamak degil; tests/thread_lint_walker_audit.py bunu mekanik
  // olarak denetliyor (ast_nodes.hpp'den turetiyor, elle liste tutmuyor).
  void walk(const ASTNode *n) {
    if (!n) return;

    if (const auto *x = node_as<Identifier>(n)) { note_read(x->name, x->loc.line); return; }
    if (const auto *x = node_as<BinaryOp>(n))   { walk(x->left.get()); walk(x->right.get()); return; }
    if (const auto *x = node_as<TernaryOp>(n))  { walk(x->condition.get()); walk(x->then_branch.get()); walk(x->else_branch.get()); return; }
    if (const auto *x = node_as<UnaryOp>(n))    { walk(x->operand.get()); return; }
    if (const auto *x = node_as<ArrayLiteral>(n)) { for (auto &e : x->elements) walk(e.get()); return; }
    if (const auto *x = node_as<ObjectLiteral>(n)) { for (auto &f : x->fields) walk(f.second.get()); return; }
    if (const auto *x = node_as<ArrayAccess>(n)) { walk(x->object.get()); walk(x->index.get()); return; }

    if (const auto *x = node_as<FunctionCall>(n)) {
      if (!x->name.empty()) {
        cur->calls.insert(x->name);
        if (x->name == "mutex_lock") lock_depth++;
        if (x->name == "mutex_unlock" && lock_depth > 0) lock_depth--;
        // thread_create(isci, arg): ilk arguman BIR AD, deger degil.
        if (x->name == "thread_create" && !x->arguments.empty()) {
          if (const auto *root = node_as<Identifier>(x->arguments[0].get()))
            thread_roots.insert(root->name);
        }
      }
      for (size_t i = 0; i < x->arguments.size(); i++) {
        if (x->name == "thread_create" && i == 0) continue; // ad, okuma degil
        walk(x->arguments[i].get());
      }
      walk(x->receiver.get());
      walk(x->callee.get());
      return;
    }

    if (const auto *x = node_as<VariableDecl>(n)) {
      walk(x->initializer.get());
      // Yalniz FONKSIYON govdesinde yerel dogar. Ust duzeyde bu bildirim
      // global'in kendisidir (yukaridaki nota bak).
      if (cur && !at_toplevel) cur->locals.insert(x->name);
      return;
    }
    // ⚠ ATAMA DUGUMLERI IKI BICIMLIDIR: basit hedef `name` (DIZGI alani),
    // karmasik hedef `target` (dugum alani). Ikisi ayni anda dolu olmaz.
    //
    // Ilk surum yalniz `target`a bakiyordu ve `fin = 1;` gibi EN YAYGIN
    // yazimi hic gormuyordu — lint Bulgu 2'nin tam sekli uzerinde sessiz
    // kaldi. Ve bunu bir alan-denetimi YAKALAYAMAZDI: `name` bir ASTNode
    // alani degil, dolayisiyla "her cocuk alani gezildi mi" sorusu ona
    // deger vermiyor. Ders: envanter COCUK DUGUMLERI degil, ANLAMSAL
    // HEDEFLERI kapsamali.
    if (const auto *x = node_as<Assignment>(n)) {
      walk(x->value.get());
      if (!x->name.empty()) note_write(x->name); else target_write(x->target.get());
      return;
    }
    if (const auto *x = node_as<CompoundAssign>(n)) {
      walk(x->value.get());
      // `g += e` hem okur hem yazar.
      if (!x->name.empty()) { note_read(x->name, x->loc.line); note_write(x->name); }
      else { walk(x->target.get()); target_write(x->target.get()); }
      return;
    }
    if (const auto *x = node_as<IncrementOp>(n)) {
      if (!x->name.empty()) { note_read(x->name, x->loc.line); note_write(x->name); }
      else { walk(x->target.get()); target_write(x->target.get()); }
      return;
    }
    if (const auto *x = node_as<DecrementOp>(n)) {
      if (!x->name.empty()) { note_read(x->name, x->loc.line); note_write(x->name); }
      else { walk(x->target.get()); target_write(x->target.get()); }
      return;
    }
    if (const auto *x = node_as<LambdaExpr>(n))     { walk(x->body.get()); return; }
    // MatchArm variant uyesi DEGIL (MatchExpr::arms icinde duz struct), bu
    // yuzden ayri bir dali yok — kollari burada geziliyor.
    if (const auto *x = node_as<MatchExpr>(n))      { walk(x->subject.get()); for (auto &a : x->arms) { walk(a.pattern.get()); walk(a.body.get()); } return; }
    if (const auto *x = node_as<IfStatement>(n))    { walk(x->condition.get()); walk(x->then_branch.get()); walk(x->else_branch.get()); return; }
    if (const auto *x = node_as<WhileLoop>(n))      { walk(x->condition.get()); walk(x->body.get()); return; }
    if (const auto *x = node_as<ForLoop>(n))        { walk(x->init.get()); walk(x->condition.get()); walk(x->increment.get()); walk(x->body.get()); return; }
    if (const auto *x = node_as<ForInLoop>(n))      { walk(x->iterable.get()); walk(x->body.get()); return; }
    if (const auto *x = node_as<ReturnStatement>(n)){ walk(x->value.get()); return; }
    if (const auto *x = node_as<Block>(n))          { for (auto &s : x->statements) walk(s.get()); return; }
    if (const auto *x = node_as<Program>(n))        { for (auto &s : x->statements) walk(s.get()); return; }
    if (const auto *x = node_as<TryCatch>(n))       { walk(x->try_block.get()); walk(x->catch_block.get()); walk(x->finally_block.get()); return; }
    if (const auto *x = node_as<ThrowStatement>(n)) { walk(x->expression.get()); return; }
    if (const auto *x = node_as<TypeDecl>(n))       { for (auto &d : x->field_defaults) walk(d.get()); return; }
    if (const auto *x = node_as<FunctionDecl>(n))   { walk(x->body.get()); return; }
    // Yaprak dugumler (literaller, break/continue/import) cocuk tasimaz.
  }
};

void closure(const std::map<std::string, FnFacts> &fns, const std::string &root,
             std::set<std::string> &out) {
  if (out.count(root)) return;
  out.insert(root);
  auto it = fns.find(root);
  if (it == fns.end()) return;
  for (const auto &c : it->second.calls) closure(fns, c, out);
}

} // namespace

int thread_lint_run(const ASTNode *program, const std::string &source_path,
                    bool warning_mode) {
  const auto *prog = program ? std::get_if<Program>(&program->value) : nullptr;
  if (!prog) return 0;

  Collector col;
  // 1. Ust duzey global'ler ve fonksiyon adlari (ON-GECIS — lint de sirasiz
  //    calismali; P23'un dersi burada da gecerli).
  for (const auto &st : prog->statements) {
    if (const auto *v = std::get_if<VariableDecl>(&st->value)) col.globals.insert(v->name);
    if (const auto *f = std::get_if<FunctionDecl>(&st->value)) col.known_functions.insert(f->name);
  }
  if (col.globals.empty()) return 0;

  // 2. Her fonksiyonun ve ust duzeyin dogrudan etkileri.
  for (const auto &st : prog->statements) {
    if (const auto *f = std::get_if<FunctionDecl>(&st->value)) {
      FnFacts &facts = col.fns[f->name];
      col.cur = &facts;
      col.lock_depth = 0;   // her fonksiyon kendi kapsamiyla baslar
      for (const auto &p : f->parameters) facts.locals.insert(p.name);
      col.walk(f->body.get());
    }
  }
  {
    FnFacts &top = col.fns[TOPLEVEL];
    col.cur = &top;
    col.at_toplevel = true;
    col.lock_depth = 0;
    for (const auto &st : prog->statements) {
      if (std::get_if<FunctionDecl>(&st->value)) continue;   // govdesi ayri gezildi
      col.walk(st.get());
    }
  }
  col.at_toplevel = false;
  col.cur = nullptr;
  if (getenv("TULPAR_LINT_DEBUG")) {
    fprintf(stderr, "[lint] globals=%zu fns=%zu roots=%zu\n", col.globals.size(),
            col.fns.size(), col.thread_roots.size());
    for (const auto &g : col.globals) fprintf(stderr, "[lint]   global %s\n", g.c_str());
    for (const auto &r : col.thread_roots) fprintf(stderr, "[lint]   root %s\n", r.c_str());
    for (const auto &kv : col.fns) {
      fprintf(stderr, "[lint]   fn %s: reads=", kv.first.c_str());
      for (const auto &x : kv.second.reads) fprintf(stderr, "%s ", x.c_str());
      fprintf(stderr, "| writes=");
      for (const auto &x : kv.second.writes) fprintf(stderr, "%s ", x.c_str());
      fprintf(stderr, "| calls=");
      for (const auto &x : kv.second.calls) fprintf(stderr, "%s ", x.c_str());
      fprintf(stderr, "\n");
    }
  }
  if (col.thread_roots.empty()) return 0;   // thread yoksa paylasim da yok

  // 3. Erisilebilirlik: thread tarafi ve ana akis.
  std::set<std::string> in_thread, in_main;
  for (const auto &r : col.thread_roots) closure(col.fns, r, in_thread);
  closure(col.fns, TOPLEVEL, in_main);

  auto agg = [&](const std::set<std::string> &scope, bool want_writes) {
    std::set<std::string> out;
    for (const auto &fn : scope) {
      auto it = col.fns.find(fn);
      if (it == col.fns.end()) continue;
      const auto &src = want_writes ? it->second.writes : it->second.reads;
      out.insert(src.begin(), src.end());
    }
    return out;
  };
  std::set<std::string> thread_writes = agg(in_thread, true);
  std::set<std::string> main_reads = agg(in_main, false);

  // (Susturma artik toplama sirasinda yapiliyor: kilit altindaki erisimler
  //  hic kaydedilmiyor. Asagidaki yorum tarihsel gerekce olarak duruyor.)
  // SUSTURMA GLOBAL BAZINDA, DOSYA BAZINDA DEGIL.
  //
  // Ilk surum "dosyada bir yerde mutex varsa hic uyarma" diyordu; bu, bir
  // global'i dogru koruyan bir dosyadaki IKINCI, korunmasiz global'i
  // gizlerdi — yani dogru is yapmak, lint'i kor eder hale gelirdi.
  // Simdi: yalniz O GLOBAL'e dokunan bir fonksiyon mutex kullaniyorsa
  // susturuluyor.
  //
  // ⚠ Kesinlik siniri (belgelenmis): hangi mutex'in hangi global'i
  // korudugu BILINMIYOR — dilde o bag yok. Yani "ayni fonksiyonda mutex
  // var" bir YAKINLIK sezgisidir, kanit degil. Yanlis negatif uretebilir
  // (yanlis mutex'le korunan global). Yanlis pozitif uretmemesi tercih
  // edildi: gurultulu bir lint kapatilir, ve kapatilan lint yoktur (#25).

  int found = 0;
  for (const auto &g : thread_writes) {
    if (!main_reads.count(g)) continue;
    // Okumanin gectigi ilk yeri bul — tani OKUMA satirini gostermeli, cunku
    // duzeltmesi gereken yer orasi.
    int line = 0; std::string where;
    for (const auto &fn : in_main) {
      auto it = col.fns.find(fn);
      if (it == col.fns.end()) continue;
      auto rl = it->second.read_line.find(g);
      if (rl != it->second.read_line.end()) { line = rl->second; where = fn; break; }
    }
    std::string writer;
    for (const auto &r : col.thread_roots) {
      auto it = col.fns.find(r);
      if (it != col.fns.end() && it->second.writes.count(g)) { writer = r; break; }
    }
    if (writer.empty()) writer = *col.thread_roots.begin();

    const char *fmt =
        warning_mode
            ? tulpar::i18n::tr_en(
                  "[typecheck] %s:%d: '%s' thread iscisi '%s' icinde YAZILIYOR "
                  "ama burada senkronizasyonsuz OKUNUYOR - dilin bellek modeli "
                  "yok, yazma bu okumaya hic gorunmeyebilir (bekleme donguleri "
                  "sonsuza kadar doner). `mutex_*` ile koru ya da sonucu "
                  "`thread_join` ile al\n",
                  "[typecheck] %s:%d: '%s' is WRITTEN in thread worker '%s' but "
                  "READ here without synchronisation - the language has no "
                  "memory model, so the write may never become visible to this "
                  "read (spin-waits loop forever). Guard it with `mutex_*` or "
                  "take the result via `thread_join`\n")
            : tulpar::i18n::tr_en(
                  "Tip Hatasi: '%s' thread iscisi '%s' icinde YAZILIYOR ama "
                  "senkronizasyonsuz OKUNUYOR (satir %d, %s)\n",
                  "Type Error: '%s' is WRITTEN in thread worker '%s' but READ "
                  "without synchronisation at line %d (%s)\n");
    if (warning_mode)
      fprintf(stderr, fmt, source_path.empty() ? "<kaynak>" : source_path.c_str(),
              line, g.c_str(), writer.c_str());
    else
      fprintf(stderr, fmt, g.c_str(), writer.c_str(), line,
              where.empty() ? TOPLEVEL : where.c_str());
    found++;
  }
  return found;
}

} // namespace tulpar
