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

// Resolve a qualified call `<recv>.<name>(args)` against the live
// function table, mutating the AST node in place so the existing
// FUNCTION_CALL codegen path can handle it without further branching.
//
// Both AOT and VM populate `node->receiver` with the head identifier
// when the parser sees `<recv>.<name>(args)` (see `parse_postfix`).
// This helper picks one of two dispatch shapes:
//
//   1. **Alias path** — `<recv>__<name>` exists in the function
//      table (i.e. `import "..." as <recv>` already mangled the
//      target). Rewrites `node->name` to the mangled form and
//      clears `node->receiver`. After this, codegen treats the call
//      as a plain user-function call.
//   2. **Method path** — alias lookup fails. Receiver is prepended
//      to `node->arguments` (becoming the first positional arg) and
//      `node->receiver` is cleared. After this, codegen resolves
//      `<name>(<recv>, args...)` against builtins and user
//      functions normally.
//
// The "function exists" check is supplied by the caller via the
// `function_exists` callback so AOT and VM can each query their
// own table without leaking module/backend types into this header.
//
// `node` may be null (no-op). Returns true if mutation happened.
typedef int (*tulpar_function_exists_fn)(const char *raw_name, void *ctx);
int resolve_qualified_call(ASTNode_C *node,
                           tulpar_function_exists_fn function_exists,
                           void *ctx);

#endif  // TULPAR_IMPORT_ALIAS_H
