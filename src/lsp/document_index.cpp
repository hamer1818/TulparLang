#include "document_index.hpp"

#include <cstring>

namespace tulpar {

std::string render_type(int data_type, const char *custom_type) {
    switch (data_type) {
        case TYPE_INT: return "int";
        case TYPE_FLOAT: return "float";
        case TYPE_STRING: return "str";
        case TYPE_BOOL: return "bool";
        case TYPE_VOID: return "void";
        case TYPE_ARRAY: return "array";
        case TYPE_ARRAY_INT: return "array<int>";
        case TYPE_ARRAY_FLOAT: return "array<float>";
        case TYPE_ARRAY_STR: return "array<str>";
        case TYPE_ARRAY_BOOL: return "array<bool>";
        case TYPE_ARRAY_JSON: return "array<json>";
        case TYPE_JSON: return "json";
        case TYPE_CUSTOM:
            return custom_type ? std::string(custom_type) : std::string();
        default:
            // Unknown / not yet inferred — fall back to the custom-type
            // hint if the parser captured one (covers `json` parameters,
            // which are recorded as TYPE_UNKNOWN + custom_type="json").
            return custom_type ? std::string(custom_type) : std::string();
    }
}

namespace {

// Scan upwards from `decl_line` (1-based) and collect a contiguous run
// of `//` comment lines, preserving order. Returns the joined doc string
// with one trailing newline stripped. An empty string means "no leading
// comment block" — the LSP renders nothing in that case.
std::string extract_leading_comment(const char *source, int decl_line) {
    if (!source || decl_line < 2) return {};

    // First, build an index of line starts (1-based → byte offset).
    std::vector<size_t> line_starts;
    line_starts.push_back(0);
    for (size_t i = 0; source[i]; i++) {
        if (source[i] == '\n') line_starts.push_back(i + 1);
    }
    if ((int)line_starts.size() < decl_line) return {};

    auto line_text = [&](int n_1based) -> std::string {
        if (n_1based < 1 || n_1based > (int)line_starts.size()) return {};
        size_t start = line_starts[n_1based - 1];
        size_t end = start;
        while (source[end] && source[end] != '\n') end++;
        if (end > start && source[end - 1] == '\r') end--;
        return std::string(source + start, source + end);
    };

    auto trim_left = [](const std::string &s) {
        size_t i = 0;
        while (i < s.size() && (s[i] == ' ' || s[i] == '\t')) i++;
        return s.substr(i);
    };

    std::vector<std::string> lines;
    for (int ln = decl_line - 1; ln >= 1; ln--) {
        std::string raw = line_text(ln);
        std::string stripped = trim_left(raw);
        if (stripped.empty()) {
            // Blank line breaks the comment block (matches gofmt/rust-doc
            // conventions — preserves the intent that "this comment goes
            // with this declaration").
            break;
        }
        if (stripped.size() >= 2 && stripped[0] == '/' && stripped[1] == '/') {
            std::string body = stripped.substr(2);
            // Drop a single leading space so "// foo" renders as "foo".
            if (!body.empty() && body[0] == ' ') body.erase(0, 1);
            lines.push_back(body);
            continue;
        }
        // Non-comment, non-blank line: stop scanning.
        break;
    }

    if (lines.empty()) return {};
    std::string joined;
    for (auto it = lines.rbegin(); it != lines.rend(); ++it) {
        joined += *it;
        joined += '\n';
    }
    if (!joined.empty() && joined.back() == '\n') joined.pop_back();
    return joined;
}

// Best-effort: find the column (1-based) of the function name on its
// declaration line so the LSP can surface a precise hover range.
int locate_name_column(const char *source, int line_1based,
                       const char *name) {
    if (!source || !name || !*name) return 1;
    int cur_line = 1;
    const char *p = source;
    const char *line_start = source;
    while (*p && cur_line < line_1based) {
        if (*p == '\n') { cur_line++; line_start = p + 1; }
        p++;
    }
    const char *line_end = line_start;
    while (*line_end && *line_end != '\n' && *line_end != '\r') line_end++;
    int line_len = (int)(line_end - line_start);
    int name_len = (int)std::strlen(name);
    for (int i = 0; i + name_len <= line_len; i++) {
        if (std::memcmp(line_start + i, name, name_len) == 0) {
            // Require word boundary so `func print() {` doesn't match
            // inside `printout`.
            char before = (i == 0) ? ' ' : line_start[i - 1];
            char after = line_start[i + name_len];
            auto isword = [](char c) {
                return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                       (c >= '0' && c <= '9') || c == '_';
            };
            if (!isword(before) && !isword(after)) {
                return i + 1;
            }
        }
    }
    return 1;
}

// Walk the subtree and return the maximum line number we see anywhere
// inside it. Used to derive a function's body range so the LSP can
// answer "is the cursor inside this function?" without re-parsing.
// Lines are 1-based; nodes with no line info default to 0 and don't
// contribute to the max.
int max_line_in_subtree(const ASTNode_C *node) {
    if (!node) return 0;
    int m = node->line;
    auto bump = [&m](int v) { if (v > m) m = v; };
    bump(max_line_in_subtree(node->left));
    bump(max_line_in_subtree(node->right));
    bump(max_line_in_subtree(node->condition));
    bump(max_line_in_subtree(node->then_branch));
    bump(max_line_in_subtree(node->else_branch));
    bump(max_line_in_subtree(node->init));
    bump(max_line_in_subtree(node->increment));
    bump(max_line_in_subtree(node->iterable));
    bump(max_line_in_subtree(node->body));
    bump(max_line_in_subtree(node->return_value));
    bump(max_line_in_subtree(node->try_block));
    bump(max_line_in_subtree(node->catch_block));
    bump(max_line_in_subtree(node->finally_block));
    bump(max_line_in_subtree(node->throw_expr));
    bump(max_line_in_subtree(node->index));
    for (int i = 0; i < node->statement_count; i++)
        bump(max_line_in_subtree(node->statements[i]));
    for (int i = 0; i < node->element_count; i++)
        bump(max_line_in_subtree(node->elements[i]));
    for (int i = 0; i < node->object_count; i++)
        bump(max_line_in_subtree(node->object_values[i]));
    for (int i = 0; i < node->argument_count; i++)
        bump(max_line_in_subtree(node->arguments[i]));
    for (int i = 0; i < node->param_count; i++)
        bump(max_line_in_subtree(node->parameters[i]));
    return m;
}

// Walk the subtree and collect every AST_VARIABLE_DECL we find. The
// `scope_function` argument is propagated unchanged — the caller
// chooses "" for top-level or the function name for body walks; nested
// functions don't exist in Tulpar so we never need to override.
void collect_var_decls(const ASTNode_C *node, const char *source,
                       const std::string &scope_function,
                       std::vector<IndexVariable> &out) {
    if (!node) return;
    if (node->type == AST_VARIABLE_DECL && node->name) {
        IndexVariable v;
        v.name = node->name;
        v.type = render_type(node->data_type, node->return_custom_type);
        v.line = node->line;
        v.column = locate_name_column(source, node->line, node->name);
        v.scope_function = scope_function;
        out.push_back(std::move(v));
        // Initializer expression can also contain decls (e.g. inside
        // a lambda or block expression in the future). Walk it too.
        if (node->right) collect_var_decls(node->right, source, scope_function, out);
        return;
    }
    if (node->left)  collect_var_decls(node->left, source, scope_function, out);
    if (node->right) collect_var_decls(node->right, source, scope_function, out);
    if (node->condition)   collect_var_decls(node->condition, source, scope_function, out);
    if (node->then_branch) collect_var_decls(node->then_branch, source, scope_function, out);
    if (node->else_branch) collect_var_decls(node->else_branch, source, scope_function, out);
    if (node->init)      collect_var_decls(node->init, source, scope_function, out);
    if (node->increment) collect_var_decls(node->increment, source, scope_function, out);
    if (node->iterable)  collect_var_decls(node->iterable, source, scope_function, out);
    if (node->body)        collect_var_decls(node->body, source, scope_function, out);
    if (node->return_value) collect_var_decls(node->return_value, source, scope_function, out);
    if (node->try_block)    collect_var_decls(node->try_block, source, scope_function, out);
    if (node->catch_block)  collect_var_decls(node->catch_block, source, scope_function, out);
    if (node->finally_block)collect_var_decls(node->finally_block, source, scope_function, out);
    for (int i = 0; i < node->statement_count; i++)
        collect_var_decls(node->statements[i], source, scope_function, out);
    for (int i = 0; i < node->element_count; i++)
        collect_var_decls(node->elements[i], source, scope_function, out);
    for (int i = 0; i < node->object_count; i++)
        collect_var_decls(node->object_values[i], source, scope_function, out);
    for (int i = 0; i < node->argument_count; i++)
        collect_var_decls(node->arguments[i], source, scope_function, out);
}

// Recursive walker that collects every AST_FUNCTION_CALL anywhere in
// the tree — function bodies, conditional branches, loop bodies, try
// blocks, etc. We don't include calls that are themselves the named
// callee of another call (those are handled by the same visitor when
// we descend into `arguments`). Best-effort column resolution: locate
// the identifier on its declared line by string match.
void collect_call_sites(const ASTNode_C *node, const char *source,
                        std::vector<IndexCallSite> &out) {
    if (!node) return;
    if (node->type == AST_FUNCTION_CALL && node->name) {
        IndexCallSite s;
        s.name = node->name;
        s.line = node->line;
        s.column = locate_name_column(source, node->line, node->name);
        out.push_back(std::move(s));
        for (int i = 0; i < node->argument_count; i++) {
            collect_call_sites(node->arguments[i], source, out);
        }
        return;
    }
    if (node->left)  collect_call_sites(node->left, source, out);
    if (node->right) collect_call_sites(node->right, source, out);
    if (node->condition)   collect_call_sites(node->condition, source, out);
    if (node->then_branch) collect_call_sites(node->then_branch, source, out);
    if (node->else_branch) collect_call_sites(node->else_branch, source, out);
    if (node->init)      collect_call_sites(node->init, source, out);
    if (node->increment) collect_call_sites(node->increment, source, out);
    if (node->iterable)  collect_call_sites(node->iterable, source, out);
    if (node->body)        collect_call_sites(node->body, source, out);
    if (node->return_value) collect_call_sites(node->return_value, source, out);
    if (node->try_block)    collect_call_sites(node->try_block, source, out);
    if (node->catch_block)  collect_call_sites(node->catch_block, source, out);
    if (node->finally_block)collect_call_sites(node->finally_block, source, out);
    if (node->throw_expr)   collect_call_sites(node->throw_expr, source, out);
    for (int i = 0; i < node->statement_count; i++) {
        collect_call_sites(node->statements[i], source, out);
    }
    for (int i = 0; i < node->element_count; i++) {
        collect_call_sites(node->elements[i], source, out);
    }
    for (int i = 0; i < node->object_count; i++) {
        collect_call_sites(node->object_values[i], source, out);
    }
    for (int i = 0; i < node->argument_count; i++) {
        collect_call_sites(node->arguments[i], source, out);
    }
}

}  // namespace

void document_index_build(const ASTNode_C *ast, const char *source,
                          DocumentIndex &out) {
    if (!ast || ast->type != AST_PROGRAM) return;

    for (int i = 0; i < ast->statement_count; i++) {
        const ASTNode_C *stmt = ast->statements[i];
        if (!stmt || stmt->type != AST_FUNCTION_DECL) continue;

        IndexFunction fn;
        fn.name = stmt->name ? stmt->name : "";
        if (fn.name.empty()) continue;

        for (int p = 0; p < stmt->param_count; p++) {
            const ASTNode_C *param = stmt->parameters[p];
            if (!param) continue;
            IndexParam ip;
            ip.name = param->name ? param->name : "";
            // Parameters store custom-type hints in `return_custom_type`
            // (an artefact of the AST converter — see parser.cpp:1039).
            ip.type = render_type(param->data_type, param->return_custom_type);
            fn.params.push_back(std::move(ip));
        }

        fn.return_type = render_type(stmt->return_type, stmt->return_custom_type);
        fn.line = stmt->line;
        // end_line = max line we see anywhere in the body. If body is
        // missing (forward-declared func — rare), fall back to decl line
        // so containment checks degenerate to "decl line only".
        fn.end_line = stmt->body ? max_line_in_subtree(stmt->body) : stmt->line;
        if (fn.end_line < fn.line) fn.end_line = fn.line;
        fn.column = locate_name_column(source, stmt->line, fn.name.c_str());
        fn.leading_comment = extract_leading_comment(source, stmt->line);

        // Function parameters are bindings too — surface them in the
        // index so hover on `n` inside `func fib(int n)` resolves.
        for (int p = 0; p < stmt->param_count; p++) {
            const ASTNode_C *param = stmt->parameters[p];
            if (!param || !param->name) continue;
            IndexVariable pv;
            pv.name = param->name;
            pv.type = render_type(param->data_type, param->return_custom_type);
            pv.line = param->line ? param->line : stmt->line;
            pv.column = locate_name_column(source, pv.line, param->name);
            pv.scope_function = fn.name;
            out.variables.push_back(std::move(pv));
        }

        out.functions.push_back(std::move(fn));

        // Walk into the function body to collect call sites + var decls.
        if (stmt->body) {
            collect_call_sites(stmt->body, source, out.call_sites);
            collect_var_decls(stmt->body, source,
                              stmt->name ? stmt->name : "", out.variables);
        }
    }

    // Top-level statements (script-style code outside any function) are
    // also a valid place to find calls + var decls — `print(greet("Hamza"))`,
    // `int total = 0;`, etc. Scope name = "" marks them global.
    for (int i = 0; i < ast->statement_count; i++) {
        const ASTNode_C *stmt = ast->statements[i];
        if (!stmt || stmt->type == AST_FUNCTION_DECL) continue;
        collect_call_sites(stmt, source, out.call_sites);
        collect_var_decls(stmt, source, "", out.variables);
    }
}

}  // namespace tulpar
