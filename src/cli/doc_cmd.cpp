// `tulpar doc <file.tpr>` — implementation. See doc_cmd.hpp for the
// command shape; this file does the actual parse + index walk + markdown
// emit.
//
// The hot path of this command is just `aot_check_and_index` (the same
// helper the LSP runs on every keystroke), so the doc generator is by
// design consistent with what hovering over an identifier in VS Code
// shows. New parser features that flow into the LSP's hover panel
// auto-show in `tulpar doc` output too — no second walker to keep in
// sync.

#include "doc_cmd.hpp"

#include "../aot/aot_pipeline.hpp"
#include "../lsp/document_index.hpp"

#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>

namespace tulpar {
namespace {

// Render one function signature as `name(p1: T1, p2: T2): RetT`. Empty
// return-type field renders as `name(...)` with no `: RetT` suffix —
// matches the convention the LSP's hover panel uses for inferred
// returns. `render_type` is the same helper the LSP signature handler
// formats with, so the two surfaces stay in lockstep.
std::string format_signature(const IndexFunction &fn) {
    std::string s = fn.name;
    s.push_back('(');
    bool first = true;
    for (const auto &p : fn.params) {
        if (!first) s.append(", ");
        first = false;
        s.append(p.name);
        if (!p.type.empty()) {
            s.append(": ");
            s.append(p.type);
        }
    }
    s.push_back(')');
    if (!fn.return_type.empty()) {
        s.append(": ");
        s.append(fn.return_type);
    }
    return s;
}

// Internal-by-convention: Tulpar uses a leading underscore for symbols
// users aren't expected to touch (`_request`, `_wings_serve_*`,
// `_wings_max_body_bytes`). `tulpar doc` hides those by default so the
// rendered reference focuses on the public API. `--include-internal`
// flips the filter.
bool is_internal(const std::string &name) {
    return !name.empty() && name[0] == '_';
}

// Pull the basename of a path for the document title. Same rule
// `derive_output_name` in the DAP adapter uses, minus the .tpr trim.
std::string basename_of(const std::string &path) {
    size_t slash = path.find_last_of("/\\");
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

}  // namespace

int doc_cmd_main(int argc, char **argv) {
    // argv layout: argv[0]="tulpar", argv[1]="doc", argv[2..]=args.
    const char *path = nullptr;
    bool include_internal = false;
    for (int i = 2; i < argc; i++) {
        if (std::strcmp(argv[i], "--include-internal") == 0) {
            include_internal = true;
            continue;
        }
        if (argv[i][0] == '-') {
            std::fprintf(stderr, "tulpar doc: unknown flag '%s'\n", argv[i]);
            return 2;
        }
        if (path) {
            std::fprintf(stderr,
                         "tulpar doc: only one file at a time "
                         "(got '%s' and '%s')\n",
                         path, argv[i]);
            return 2;
        }
        path = argv[i];
    }
    if (!path) {
        std::fprintf(stderr,
                     "Usage: tulpar doc <file.tpr> [--include-internal]\n");
        return 2;
    }

    std::ifstream in(path, std::ios::binary);
    if (!in) {
        std::fprintf(stderr, "tulpar doc: cannot open '%s'\n", path);
        return 2;
    }
    std::stringstream ss;
    ss << in.rdbuf();
    std::string source = ss.str();
    in.close();

    DocumentIndex idx;
    AOTResult rc = aot_check_and_index(source.c_str(), path, &idx);
    if (rc != AOT_OK) {
        std::fprintf(stderr,
                     "tulpar doc: parse/codegen errors prevented indexing "
                     "(see diagnostics above)\n");
        return 1;
    }

    // ----- Header -----
    std::printf("# %s\n\n", basename_of(path).c_str());

    // ----- Functions -----
    bool any_fn = false;
    for (const auto &fn : idx.functions) {
        if (!include_internal && is_internal(fn.name)) continue;
        any_fn = true;
        break;
    }
    if (any_fn) {
        std::printf("## Functions\n\n");
        for (const auto &fn : idx.functions) {
            if (!include_internal && is_internal(fn.name)) continue;
            std::printf("### `%s`\n\n", format_signature(fn).c_str());
            if (!fn.leading_comment.empty()) {
                // The comment is already joined with `\n` between lines
                // by document_index_build. Emit verbatim so any inline
                // markdown the author wrote (lists, code blocks,
                // emphasis) round-trips. Make sure we always end with
                // exactly one blank line so adjacent functions don't
                // run together.
                std::printf("%s", fn.leading_comment.c_str());
                if (fn.leading_comment.back() != '\n') std::printf("\n");
                std::printf("\n");
            } else {
                std::printf("_No documentation._\n\n");
            }
        }
    }

    // ----- Globals -----
    // Top-level variables only (scope_function empty). Locals don't
    // belong in a reference doc — they're implementation detail.
    bool any_global = false;
    for (const auto &v : idx.variables) {
        if (!v.scope_function.empty()) continue;
        if (!include_internal && is_internal(v.name)) continue;
        any_global = true;
        break;
    }
    if (any_global) {
        std::printf("## Globals\n\n");
        for (const auto &v : idx.variables) {
            if (!v.scope_function.empty()) continue;
            if (!include_internal && is_internal(v.name)) continue;
            if (v.type.empty()) {
                std::printf("- `%s`\n", v.name.c_str());
            } else {
                std::printf("- `%s: %s`\n", v.name.c_str(), v.type.c_str());
            }
        }
        std::printf("\n");
    }

    if (!any_fn && !any_global) {
        std::printf("_Empty source — no functions or top-level globals._\n");
    }

    return 0;
}

}  // namespace tulpar
