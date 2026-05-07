#ifndef TULPAR_LSP_DOCUMENT_INDEX_HPP
#define TULPAR_LSP_DOCUMENT_INDEX_HPP

#include "../parser/parser.hpp"
#include <string>
#include <vector>

namespace tulpar {

// Lightweight per-document symbol model used by the LSP server. Built once
// per parse pass and consulted on hover / completion / definition. Kept
// intentionally small: each query (currently hover + completion) only
// needs function declarations + their signatures + leading comments.
// Variables and types will join here as the LSP gains hover/completion
// support for them.

struct IndexParam {
    std::string name;
    std::string type;  // pretty-printed, e.g. "int", "str", "array<int>"
};

struct IndexFunction {
    std::string name;
    std::vector<IndexParam> params;
    std::string return_type;     // empty if unspecified
    int line;                     // 1-based declaration line
    int end_line;                 // 1-based last line covered by body (scope range)
    int column;                   // 1-based identifier column (best-effort)
    std::string leading_comment;  // joined `// ...` lines immediately above
};

// Variable bindings collected from `AST_VARIABLE_DECL` nodes — both
// top-level (`scope_function == ""`) and function-local (`scope_function`
// = enclosing function name). Used for hover ("what's this variable's
// type?") and scope-aware completion ("what locals are in the function
// the cursor is in?"). Function parameters are also recorded with their
// declaring function's name as scope so they show up in both.
struct IndexVariable {
    std::string name;
    std::string type;             // pretty-printed via render_type
    int line;                     // 1-based declaration line
    int column;                   // 1-based name column (best-effort)
    std::string scope_function;   // empty = top-level / global
};

// Every AST_FUNCTION_CALL site we found while walking the program.
// Used by `textDocument/references` and `textDocument/rename` to point
// the editor at every place a symbol is invoked. Variable usages are
// not collected yet — only call sites.
struct IndexCallSite {
    std::string name;
    int line;
    int column;  // 1-based, best-effort (looked up by string match on the line)
};

struct DocumentIndex {
    std::vector<IndexFunction> functions;
    std::vector<IndexCallSite> call_sites;
    std::vector<IndexVariable> variables;
};

// Walk the program AST and fill `out`. Caller owns both AST and source
// lifetime; we only borrow.
void document_index_build(const ASTNode_C *ast, const char *source,
                          DocumentIndex &out);

// Pretty-print a Tulpar DataType + optional custom-type name as the user
// would write it in source code. Exposed because the hover handler also
// renders parameter types from outside the index.
std::string render_type(int data_type, const char *custom_type);

}  // namespace tulpar

#endif  // TULPAR_LSP_DOCUMENT_INDEX_HPP
