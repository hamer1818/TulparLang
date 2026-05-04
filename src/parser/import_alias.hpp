// Helper used by both AOT and VM import handlers to implement
// `import "name" as alias;` namespacing. After parsing the imported
// module's program, the codegen path calls `apply_import_alias` to
// rewrite the AST: every top-level FunctionDecl gets its name prefixed
// with `<alias>__`, and every FunctionCall that targets one of those
// (locally-defined) functions is rewritten in lockstep so internal
// references inside the module continue to resolve.
//
// Calls to functions NOT defined in this module (built-ins, user
// helpers in the importer, anything else) are left untouched, so
// `print(...)`, `len(...)`, `assert(...)` etc. still work.
//
// Pass `alias == nullptr` or empty to no-op (preserves the historical
// "imported names land in global scope unchanged" behaviour).

#ifndef TULPAR_IMPORT_ALIAS_H
#define TULPAR_IMPORT_ALIAS_H

#include "parser.hpp"

void apply_import_alias(ASTNode_C *program, const char *alias);

#endif  // TULPAR_IMPORT_ALIAS_H
