#ifndef TULPAR_THREAD_LINT_HPP
#define TULPAR_THREAD_LINT_HPP

#include "../parser/ast_nodes.hpp"
#include <string>

namespace tulpar {

// Paylasilan-global lint'i.
//
// Kapanis cumlesi: "Global yazimi, baska thread'de OKUNUYORSA uyari verir."
//
// Hedef sekil OLCULMUS bir kusur (Concurrency.md, Bulgu 2): bir global thread
// iscisinde yaziliyor, ana thread bir bekleme dongusunde okuyor, ve yazma
// OKUYANA HIC GORUNMUYOR — optimize edici okumayi dongunun disina cikariyor.
// Program sessizce sonsuza kadar doner. Dilin bellek modeli yok, yani bu
// "hata" degil TANIMSIZ; tanimsizin bedeli de tam bu.
//
// `warning_mode` true ise `[typecheck]` satiri, degilse `Type Error:` basar.
// Bulgu sayisini dondurur.
int thread_lint_run(const ASTNode *program, const std::string &source_path,
                    bool warning_mode);

} // namespace tulpar

#endif
