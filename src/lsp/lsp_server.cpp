// Tulpar Language Server (LSP) — stdio JSON-RPC implementation.
//
// Stage 1 scope:
//   * initialize / initialized / shutdown / exit
//   * textDocument/didOpen, didChange, didSave, didClose
//   * publishDiagnostics (driven by aot_check_only + diagnostic sink)
//
// Hover, completion, go-to-def, rename are tracked separately and will
// land on top of this skeleton without protocol changes.

#include "lsp_server.hpp"

#include "../common/diagnostics.hpp"
#include "../common/localization.hpp"
#include "../aot/aot_pipeline.hpp"
#include "builtins.hpp"
#include "document_index.hpp"
#include "../../runtime/cJSON.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>

#if defined(_WIN32)
  #include <io.h>
  #include <fcntl.h>
#endif

namespace tulpar {

namespace {

// ---------------------------------------------------------------------------
// Stdio framing helpers
// ---------------------------------------------------------------------------

// Read a single LSP message from stdin. Returns the JSON body as a
// std::string (without the Content-Length header). Returns empty string
// on EOF or malformed framing.
std::string read_message() {
    long long content_length = -1;
    std::string header_line;
    header_line.reserve(64);

    // Read headers line-by-line until we hit a blank line.
    for (;;) {
        header_line.clear();
        int c;
        while ((c = std::fgetc(stdin)) != EOF) {
            if (c == '\r') {
                int next = std::fgetc(stdin);
                if (next == '\n') break;
                if (next == EOF) break;
                header_line.push_back((char)c);
                header_line.push_back((char)next);
                continue;
            }
            if (c == '\n') break;
            header_line.push_back((char)c);
        }
        if (c == EOF) return {};
        if (header_line.empty()) break;  // end of headers

        // Parse "Content-Length: N"
        const char *cl = "Content-Length:";
        size_t cl_len = std::strlen(cl);
        if (header_line.size() >= cl_len &&
            std::strncmp(header_line.c_str(), cl, cl_len) == 0) {
            const char *p = header_line.c_str() + cl_len;
            while (*p == ' ' || *p == '\t') p++;
            content_length = std::strtoll(p, nullptr, 10);
        }
        // Other headers (Content-Type, etc.) are ignored.
    }

    if (content_length <= 0) return {};

    std::string body;
    body.resize((size_t)content_length);
    size_t got = std::fread(body.data(), 1, (size_t)content_length, stdin);
    if (got != (size_t)content_length) return {};
    return body;
}

// Write an LSP message to stdout (Content-Length framing + JSON body).
// Builds the full payload in a single buffer and writes it in one pass —
// fprintf + fwrite have separately observable byte streams under MSVC's
// CRT, and on large bodies the framing header could be flushed before
// the body without `fflush(NULL)` between them. A single fwrite + flush
// is both simpler and more robust.
void write_message(const std::string &body) {
    char header[64];
    int hlen = std::snprintf(header, sizeof(header),
                             "Content-Length: %zu\r\n\r\n", body.size());
    if (hlen < 0) return;

    std::string out;
    out.reserve((size_t)hlen + body.size());
    out.append(header, (size_t)hlen);
    out.append(body);

    size_t total = out.size();
    size_t written = 0;
    while (written < total) {
        size_t n = std::fwrite(out.data() + written, 1, total - written, stdout);
        if (n == 0) break;  // pipe closed
        written += n;
    }
    std::fflush(stdout);

    if (const char *dbg = std::getenv("TULPAR_LSP_DEBUG")) {
        if (*dbg && *dbg != '0') {
            std::fprintf(stderr, "[lsp] write_message: total=%zu written=%zu\n",
                         total, written);
        }
    }
}

void send_json(cJSON *msg) {
    char *s = cJSON_PrintUnformatted(msg);
    if (!s) return;
    size_t cstr_len = std::strlen(s);
    std::string body(s, cstr_len);
    if (const char *dbg = std::getenv("TULPAR_LSP_DEBUG")) {
        if (*dbg && *dbg != '0') {
            std::fprintf(stderr, "[lsp] send: cstr_len=%zu body.size=%zu\n",
                         cstr_len, body.size());
        }
    }
    write_message(body);
    free(s);
}

// ---------------------------------------------------------------------------
// URI helpers
// ---------------------------------------------------------------------------

// Decode `%XX` percent-escapes in place. LSP URIs are the only place
// these occur in our wire format.
std::string percent_decode(const std::string &in) {
    std::string out;
    out.reserve(in.size());
    for (size_t i = 0; i < in.size(); i++) {
        if (in[i] == '%' && i + 2 < in.size()) {
            auto hex = [](char c) -> int {
                if (c >= '0' && c <= '9') return c - '0';
                if (c >= 'a' && c <= 'f') return 10 + c - 'a';
                if (c >= 'A' && c <= 'F') return 10 + c - 'A';
                return -1;
            };
            int hi = hex(in[i + 1]);
            int lo = hex(in[i + 2]);
            if (hi >= 0 && lo >= 0) {
                out.push_back((char)((hi << 4) | lo));
                i += 2;
                continue;
            }
        }
        out.push_back(in[i]);
    }
    return out;
}

// Convert a `file://` URI to a native filesystem path. Anything else
// (including untitled buffers) returns an empty string, signalling
// "no on-disk path" — diagnostics still flow but the `--> path:line`
// header will use the URI as-is.
std::string uri_to_path(const std::string &uri) {
    const char *prefix = "file://";
    size_t plen = std::strlen(prefix);
    if (uri.size() < plen || uri.compare(0, plen, prefix) != 0) return {};
    std::string rest = percent_decode(uri.substr(plen));
#if defined(_WIN32)
    // Windows: file:///d:/foo  → /d:/foo  → d:/foo
    if (rest.size() >= 3 && rest[0] == '/' && rest[2] == ':') {
        rest.erase(0, 1);
    }
#endif
    return rest;
}

// ---------------------------------------------------------------------------
// Document state + check/publish
// ---------------------------------------------------------------------------

struct DocumentEntry {
    std::string text;
    DocumentIndex index;
};

struct DocumentStore {
    std::unordered_map<std::string, DocumentEntry> docs;
};

void publish_diagnostics(const std::string &uri,
                         const std::vector<Diagnostic> &diags) {
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "jsonrpc", "2.0");
    cJSON_AddStringToObject(root, "method", "textDocument/publishDiagnostics");
    cJSON *params = cJSON_AddObjectToObject(root, "params");
    cJSON_AddStringToObject(params, "uri", uri.c_str());

    cJSON *arr = cJSON_AddArrayToObject(params, "diagnostics");
    for (const auto &d : diags) {
        cJSON *item = cJSON_CreateObject();

        // LSP positions are 0-based; ours are 1-based.
        int line0 = d.line > 0 ? d.line - 1 : 0;
        int col0 = d.column > 0 ? d.column - 1 : 0;
        int span = d.length > 0 ? d.length : 1;

        cJSON *range = cJSON_AddObjectToObject(item, "range");
        cJSON *start = cJSON_AddObjectToObject(range, "start");
        cJSON_AddNumberToObject(start, "line", line0);
        cJSON_AddNumberToObject(start, "character", col0);
        cJSON *end = cJSON_AddObjectToObject(range, "end");
        cJSON_AddNumberToObject(end, "line", line0);
        cJSON_AddNumberToObject(end, "character", col0 + span);

        // Severity: 1=Error 2=Warning 3=Information 4=Hint. We only emit
        // errors today; "warning"-tagged sinks would round-trip here.
        int severity = 1;
        if (d.severity == "warning") severity = 2;
        else if (d.severity == "info") severity = 3;
        else if (d.severity == "hint") severity = 4;
        cJSON_AddNumberToObject(item, "severity", severity);
        cJSON_AddStringToObject(item, "source", "tulpar");

        std::string msg = d.message;
        if (!d.hint.empty()) {
            msg += "\n";
            msg += tulpar::i18n::tr_en("ipucu", "hint");
            msg += ": ";
            msg += d.hint;
        }
        cJSON_AddStringToObject(item, "message", msg.c_str());

        cJSON_AddItemToArray(arr, item);
    }

    send_json(root);
    cJSON_Delete(root);
}

void check_and_publish(const std::string &uri, DocumentEntry &entry) {
    std::string path = uri_to_path(uri);
    const char *source_filename = path.empty() ? nullptr : path.c_str();

    DocumentIndex fresh_index;
    diag_sink_enable();
    aot_check_and_index(entry.text.c_str(), source_filename, &fresh_index);
    auto records = diag_sink_drain();
    diag_sink_disable();

    // Replace the cached index even when codegen errored — partial info is
    // more useful for hover/completion than stale info from before the
    // edit. Functions that *did* parse will still have correct signatures.
    entry.index = std::move(fresh_index);

    publish_diagnostics(uri, records);
}

// ---------------------------------------------------------------------------
// Message handlers
// ---------------------------------------------------------------------------

void send_response(cJSON *id, cJSON *result_or_null) {
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "jsonrpc", "2.0");
    if (id) {
        cJSON_AddItemToObject(root, "id", cJSON_Duplicate(id, 1));
    } else {
        cJSON_AddNullToObject(root, "id");
    }
    if (result_or_null) {
        cJSON_AddItemToObject(root, "result", result_or_null);
    } else {
        cJSON_AddNullToObject(root, "result");
    }
    send_json(root);
    cJSON_Delete(root);
}

cJSON *build_initialize_result() {
    cJSON *result = cJSON_CreateObject();
    cJSON *caps = cJSON_AddObjectToObject(result, "capabilities");
    // Incremental document sync (change=2). Clients send each edit
    // as `(range, text)` rather than the entire document on every
    // keystroke — sub-100ms re-parse stays sub-100ms but the wire
    // bandwidth drops linearly with file size. `handle_did_change`
    // accepts BOTH shapes (a change event with `range` is
    // incremental; without is full-replace), so clients that don't
    // negotiate incremental sync still work as before.
    cJSON *sync = cJSON_AddObjectToObject(caps, "textDocumentSync");
    cJSON_AddBoolToObject(sync, "openClose", 1);
    cJSON_AddNumberToObject(sync, "change", 2);  // 2 = incremental
    cJSON *save = cJSON_AddObjectToObject(sync, "save");
    cJSON_AddBoolToObject(save, "includeText", 0);

    cJSON_AddBoolToObject(caps, "hoverProvider", 1);
    cJSON_AddBoolToObject(caps, "definitionProvider", 1);
    cJSON_AddBoolToObject(caps, "referencesProvider", 1);
    cJSON_AddBoolToObject(caps, "renameProvider", 1);

    cJSON *cp = cJSON_AddObjectToObject(caps, "completionProvider");
    cJSON *triggers = cJSON_AddArrayToObject(cp, "triggerCharacters");
    cJSON_AddItemToArray(triggers, cJSON_CreateString("."));

    // Signature help: editor pops the parameter hint window when the user
    // types `(` or `,` inside an arg list (or moves the cursor there).
    // `retriggerCharacters` keeps the popup updated as the user moves
    // between params via `,`.
    cJSON *sh = cJSON_AddObjectToObject(caps, "signatureHelpProvider");
    cJSON *sh_trig = cJSON_AddArrayToObject(sh, "triggerCharacters");
    cJSON_AddItemToArray(sh_trig, cJSON_CreateString("("));
    cJSON_AddItemToArray(sh_trig, cJSON_CreateString(","));
    cJSON *sh_retrig = cJSON_AddArrayToObject(sh, "retriggerCharacters");
    cJSON_AddItemToArray(sh_retrig, cJSON_CreateString(","));

    cJSON *info = cJSON_AddObjectToObject(result, "serverInfo");
    cJSON_AddStringToObject(info, "name", "tulpar-lsp");
    cJSON_AddStringToObject(info, "version", "0.2.0");
    return result;
}

void handle_did_open(DocumentStore &docs, cJSON *params) {
    cJSON *td = cJSON_GetObjectItem(params, "textDocument");
    if (!td) return;
    const char *uri = cJSON_GetStringValue(cJSON_GetObjectItem(td, "uri"));
    const char *text = cJSON_GetStringValue(cJSON_GetObjectItem(td, "text"));
    if (!uri || !text) return;
    auto &entry = docs.docs[uri];
    entry.text = text;
    check_and_publish(uri, entry);
}

// Forward decl — the definition lives further down with the rest of
// the position helpers, but didChange needs it for incremental sync.
size_t line_col_to_offset(const std::string &src, int line, int character);

void handle_did_change(DocumentStore &docs, cJSON *params) {
    cJSON *td = cJSON_GetObjectItem(params, "textDocument");
    if (!td) return;
    const char *uri = cJSON_GetStringValue(cJSON_GetObjectItem(td, "uri"));
    if (!uri) return;
    cJSON *changes = cJSON_GetObjectItem(params, "contentChanges");
    if (!cJSON_IsArray(changes)) return;
    int n = cJSON_GetArraySize(changes);
    if (n <= 0) return;

    auto &entry = docs.docs[uri];

    // The LSP spec lets each TextDocumentContentChangeEvent be EITHER
    // a full replace (just a `text` field) OR an incremental edit
    // (`range` + `text`). With change=2 advertised in capabilities,
    // clients send the cheaper incremental form for typical edits
    // (single-char insertions, line-range diffs) and fall back to
    // full replace only on big paste / undo operations. We accept
    // both shapes per event in order — the LSP spec says the changes
    // array is applied head-to-tail.
    for (int i = 0; i < n; i++) {
        cJSON *ev = cJSON_GetArrayItem(changes, i);
        if (!ev) continue;
        const char *text = cJSON_GetStringValue(cJSON_GetObjectItem(ev, "text"));
        if (!text) continue;
        cJSON *range = cJSON_GetObjectItem(ev, "range");
        if (!cJSON_IsObject(range)) {
            // No range => full-document replace. Drop everything we
            // accumulated so far and start from this text. The spec
            // says full-replace events MUST be the only event in
            // their batch, but we don't enforce that — taking the
            // last-wins interpretation is robust against sloppy
            // clients.
            entry.text = text;
            continue;
        }
        // Incremental: replace src[start_off .. end_off) with text.
        cJSON *start = cJSON_GetObjectItem(range, "start");
        cJSON *end_j = cJSON_GetObjectItem(range, "end");
        if (!cJSON_IsObject(start) || !cJSON_IsObject(end_j)) continue;
        int sl = (int)cJSON_GetNumberValue(cJSON_GetObjectItem(start, "line"));
        int sc = (int)cJSON_GetNumberValue(cJSON_GetObjectItem(start, "character"));
        int el = (int)cJSON_GetNumberValue(cJSON_GetObjectItem(end_j, "line"));
        int ec = (int)cJSON_GetNumberValue(cJSON_GetObjectItem(end_j, "character"));
        size_t s_off = line_col_to_offset(entry.text, sl, sc);
        size_t e_off = line_col_to_offset(entry.text, el, ec);
        if (e_off < s_off) e_off = s_off;
        if (e_off > entry.text.size()) e_off = entry.text.size();
        entry.text.replace(s_off, e_off - s_off, text);
    }

    check_and_publish(uri, entry);
}

void handle_did_save(DocumentStore &docs, cJSON *params) {
    cJSON *td = cJSON_GetObjectItem(params, "textDocument");
    if (!td) return;
    const char *uri = cJSON_GetStringValue(cJSON_GetObjectItem(td, "uri"));
    if (!uri) return;
    auto it = docs.docs.find(uri);
    if (it == docs.docs.end()) return;
    check_and_publish(uri, it->second);
}

void handle_did_close(DocumentStore &docs, cJSON *params) {
    cJSON *td = cJSON_GetObjectItem(params, "textDocument");
    if (!td) return;
    const char *uri = cJSON_GetStringValue(cJSON_GetObjectItem(td, "uri"));
    if (!uri) return;
    docs.docs.erase(uri);
    // Clear diagnostics for the closed file so stale errors don't linger
    // in the editor's Problems panel.
    publish_diagnostics(uri, {});
}

// ---------------------------------------------------------------------------
// Position helpers (used by hover + definition)
// ---------------------------------------------------------------------------

bool is_ident_char(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
           (c >= '0' && c <= '9') || c == '_';
}

// Extract the identifier surrounding the given LSP position (line,
// character — both 0-based) from the source text. Returns empty string
// if the cursor isn't on an identifier.
std::string word_at_position(const std::string &source, int line, int character) {
    int cur_line = 0;
    size_t i = 0;
    while (i < source.size() && cur_line < line) {
        if (source[i] == '\n') cur_line++;
        i++;
    }
    if (cur_line < line) return {};

    size_t line_start = i;
    size_t line_end = line_start;
    while (line_end < source.size() && source[line_end] != '\n' &&
           source[line_end] != '\r') {
        line_end++;
    }

    int col = character;
    if ((size_t)col > line_end - line_start) col = (int)(line_end - line_start);

    // Walk left to start of identifier
    int start = col;
    while (start > 0 && is_ident_char(source[line_start + start - 1])) start--;
    int end = col;
    while ((size_t)end < line_end - line_start &&
           is_ident_char(source[line_start + end])) end++;

    if (start >= end) return {};
    return source.substr(line_start + start, end - start);
}

// Locate the enclosing call site for the cursor and report which
// parameter slot the cursor is in. Walks the source backwards from the
// cursor offset, counting paren depth and commas at depth 0 below the
// outermost unmatched `(`. Returns false when the cursor isn't inside
// a call.
//
// `out_func_offset` is the byte offset of the first char of the
// function name token preceding the open `(` — the caller uses it to
// pull out the identifier with `is_ident_char`. `out_active_param`
// counts commas (so the param right after `(` is index 0).
//
// String literals are tracked one quote at a time so a `(` or `,`
// inside `"foo, bar"` doesn't fool us. Strings contain `"` only when
// preceded by a `\`; we approximate with a 1-char lookback (good
// enough for LSP heuristics — no need to fully tokenise for a
// signature-help hint).
bool find_enclosing_call(const std::string &src, size_t cursor_offset,
                         size_t *out_func_offset, int *out_active_param) {
    int depth = 0;
    int commas = 0;
    bool in_string = false;
    char string_quote = 0;
    // First pass — find the unmatched `(`. We count from cursor backwards
    // because we need to know whether each comma we cross belongs to the
    // outermost call (depth==0 at time of crossing).
    if (cursor_offset > src.size()) cursor_offset = src.size();
    long long i = (long long)cursor_offset - 1;
    long long open_paren = -1;
    while (i >= 0) {
        char c = src[(size_t)i];
        if (in_string) {
            if (c == string_quote && (i == 0 || src[(size_t)i - 1] != '\\')) {
                in_string = false;
            }
            i--;
            continue;
        }
        if (c == '"' || c == '\'') {
            in_string = true;
            string_quote = c;
            i--;
            continue;
        }
        if (c == ')' || c == ']' || c == '}') {
            depth++;
        } else if (c == '(' || c == '[' || c == '{') {
            if (depth == 0) {
                if (c == '(') {
                    open_paren = i;
                    break;
                }
                // `[` or `{` at depth 0 means we're inside an array/object
                // literal, not a call. No signature to show.
                return false;
            }
            depth--;
        } else if (c == ',' && depth == 0) {
            commas++;
        } else if (c == ';' && depth == 0) {
            // Statement boundary — we walked too far; no enclosing call.
            return false;
        }
        i--;
    }
    if (open_paren < 0) return false;

    // Walk left of the `(` skipping whitespace, then collect the identifier.
    long long j = open_paren - 1;
    while (j >= 0 && (src[(size_t)j] == ' ' || src[(size_t)j] == '\t')) j--;
    if (j < 0 || !is_ident_char(src[(size_t)j])) return false;
    long long name_end = j + 1;
    while (j >= 0 && is_ident_char(src[(size_t)j])) j--;
    long long name_start = j + 1;
    if (name_start >= name_end) return false;

    *out_func_offset = (size_t)name_start;
    *out_active_param = commas;
    return true;
}

// Convert a byte offset in the source into the (line, col) that the
// LSP-level word_at_position helper expects. Both 0-based — LSP coords.
void offset_to_line_col(const std::string &src, size_t offset, int *line,
                        int *col) {
    int l = 0;
    int c = 0;
    for (size_t i = 0; i < offset && i < src.size(); i++) {
        if (src[i] == '\n') {
            l++;
            c = 0;
        } else {
            c++;
        }
    }
    *line = l;
    *col = c;
}

// Translate (line, character) — both 0-based, LSP convention — to a
// linear byte offset in `src`. Tabs count as one character (LSP treats
// them as one code unit unless the client negotiates otherwise, which
// no real editor does).
size_t line_col_to_offset(const std::string &src, int line, int character) {
    size_t i = 0;
    int cur_line = 0;
    while (i < src.size() && cur_line < line) {
        if (src[i] == '\n') cur_line++;
        i++;
    }
    int cur_col = 0;
    while (i < src.size() && cur_col < character && src[i] != '\n') {
        i++;
        cur_col++;
    }
    return i;
}

// Render a function signature in the form
//    func name(p1: t1, p2: t2): ret
// for hover panels. Tulpar's actual syntax is `func name(p1, p2): ret { ... }`
// (parameter types are inferred), but the inferred types are extra context
// the user usually wants — so we surface them.
std::string format_signature(const IndexFunction &fn) {
    std::string out = "func ";
    out += fn.name;
    out += "(";
    for (size_t i = 0; i < fn.params.size(); i++) {
        if (i) out += ", ";
        out += fn.params[i].name;
        if (!fn.params[i].type.empty()) {
            out += ": ";
            out += fn.params[i].type;
        }
    }
    out += ")";
    if (!fn.return_type.empty() && fn.return_type != "void") {
        out += ": ";
        out += fn.return_type;
    }
    return out;
}

// ---------------------------------------------------------------------------
// hover / completion / definition
// ---------------------------------------------------------------------------

cJSON *build_hover_for_variable(const IndexVariable &v) {
    cJSON *result = cJSON_CreateObject();
    cJSON *contents = cJSON_AddObjectToObject(result, "contents");
    cJSON_AddStringToObject(contents, "kind", "markdown");
    // Show type if we have one; fall back to `<unknown>` so users see
    // the LSP recognised the symbol even before typeinfer runs.
    std::string type_str = v.type.empty() ? "<unknown>" : v.type;
    std::string md = "```tulpar\n";
    md += type_str;
    md += " ";
    md += v.name;
    md += "\n```";
    if (!v.scope_function.empty()) {
        md += "\n\n*(local in `";
        md += v.scope_function;
        md += "`, line ";
        md += std::to_string(v.line);
        md += ")*";
    } else {
        md += "\n\n*(top-level, line ";
        md += std::to_string(v.line);
        md += ")*";
    }
    cJSON_AddStringToObject(contents, "value", md.c_str());
    return result;
}

// Find the function whose body contains `line_0based` (1-based on the
// IndexFunction side). Returns the function name, or "" if the cursor is
// not inside any function (top-level scope). We pick the innermost match
// by preferring later start lines among overlapping ranges; in practice
// Tulpar doesn't allow nested functions so any match is the unique one.
std::string enclosing_function_at_line(const DocumentIndex &idx,
                                       int line_0based) {
    int line_1based = line_0based + 1;
    std::string best;
    int best_start = -1;
    for (const auto &fn : idx.functions) {
        if (line_1based >= fn.line && line_1based <= fn.end_line) {
            if (fn.line > best_start) {
                best_start = fn.line;
                best = fn.name;
            }
        }
    }
    return best;
}

cJSON *build_hover_for_user_function(const IndexFunction &fn) {
    cJSON *result = cJSON_CreateObject();
    cJSON *contents = cJSON_AddObjectToObject(result, "contents");
    cJSON_AddStringToObject(contents, "kind", "markdown");

    std::string md = "```tulpar\n";
    md += format_signature(fn);
    md += "\n```";
    if (!fn.leading_comment.empty()) {
        md += "\n\n";
        md += fn.leading_comment;
    }
    cJSON_AddStringToObject(contents, "value", md.c_str());
    return result;
}

cJSON *build_hover_for_builtin(const BuiltinEntry *b) {
    cJSON *result = cJSON_CreateObject();
    cJSON *contents = cJSON_AddObjectToObject(result, "contents");
    cJSON_AddStringToObject(contents, "kind", "markdown");
    std::string md = "```tulpar\n";
    md += b->signature;
    md += "\n```";
    if (b->doc && *b->doc) {
        md += "\n\n";
        md += b->doc;
    }
    md += "\n\n*(builtin)*";
    cJSON_AddStringToObject(contents, "value", md.c_str());
    return result;
}

void handle_hover(DocumentStore &docs, cJSON *id, cJSON *params) {
    if (!id) return;
    cJSON *td = cJSON_GetObjectItem(params, "textDocument");
    cJSON *pos = cJSON_GetObjectItem(params, "position");
    if (!td || !pos) { send_response(id, nullptr); return; }
    const char *uri = cJSON_GetStringValue(cJSON_GetObjectItem(td, "uri"));
    if (!uri) { send_response(id, nullptr); return; }
    auto it = docs.docs.find(uri);
    if (it == docs.docs.end()) { send_response(id, nullptr); return; }

    int line = (int)cJSON_GetNumberValue(cJSON_GetObjectItem(pos, "line"));
    int ch   = (int)cJSON_GetNumberValue(cJSON_GetObjectItem(pos, "character"));

    std::string word = word_at_position(it->second.text, line, ch);
    if (word.empty()) { send_response(id, nullptr); return; }

    // Try user functions first; locally defined symbols shadow builtins
    // (matching Tulpar's actual lookup order).
    for (const auto &fn : it->second.index.functions) {
        if (fn.name == word) {
            send_response(id, build_hover_for_user_function(fn));
            return;
        }
    }

    // Variables: prefer the binding visible at the cursor's enclosing
    // scope. Same name can exist in multiple functions (think `int i`
    // inside many loops); the LSP should hover the one the user is
    // actually looking at. Top-level vars match anywhere outside a
    // function. Last-wins on ties so a local rebinding shadows the
    // outer with the same name (declaration order matches scope walk).
    std::string scope = enclosing_function_at_line(it->second.index, line);
    const IndexVariable *best = nullptr;
    for (const auto &v : it->second.index.variables) {
        if (v.name != word) continue;
        if (v.scope_function.empty() || v.scope_function == scope) {
            best = &v;  // last-wins shadowing
        }
    }
    if (best) {
        send_response(id, build_hover_for_variable(*best));
        return;
    }

    if (auto *b = builtin_lookup(word.c_str())) {
        send_response(id, build_hover_for_builtin(b));
        return;
    }

    send_response(id, nullptr);
}

void handle_completion(DocumentStore &docs, cJSON *id, cJSON *params) {
    if (!id) return;
    cJSON *td = cJSON_GetObjectItem(params, "textDocument");
    cJSON *pos = cJSON_GetObjectItem(params, "position");
    if (!td) { send_response(id, nullptr); return; }
    const char *uri = cJSON_GetStringValue(cJSON_GetObjectItem(td, "uri"));
    auto it = docs.docs.find(uri ? uri : "");

    // Cursor scope determines which locals are visible. Without a
    // position (some clients omit it on the initial trigger), we fall
    // back to top-level scope and only show globals + functions +
    // builtins + keywords.
    int cursor_line = pos ? (int)cJSON_GetNumberValue(
                                cJSON_GetObjectItem(pos, "line"))
                          : -1;
    std::string scope;
    if (it != docs.docs.end() && cursor_line >= 0) {
        scope = enclosing_function_at_line(it->second.index, cursor_line);
    }

    cJSON *result = cJSON_CreateObject();
    cJSON_AddBoolToObject(result, "isIncomplete", 0);
    cJSON *items = cJSON_AddArrayToObject(result, "items");

    // 1) User-defined functions in the current document.
    if (it != docs.docs.end()) {
        for (const auto &fn : it->second.index.functions) {
            cJSON *item = cJSON_CreateObject();
            cJSON_AddStringToObject(item, "label", fn.name.c_str());
            cJSON_AddNumberToObject(item, "kind", 3);  // Function
            cJSON_AddStringToObject(item, "detail", format_signature(fn).c_str());
            if (!fn.leading_comment.empty()) {
                cJSON *doc = cJSON_AddObjectToObject(item, "documentation");
                cJSON_AddStringToObject(doc, "kind", "markdown");
                cJSON_AddStringToObject(doc, "value", fn.leading_comment.c_str());
            }
            cJSON_AddItemToArray(items, item);
        }

        // 1b) Variables visible in the current scope: top-level globals
        // are always offered; locals are only offered when the cursor
        // is inside their declaring function. Same-name shadowing means
        // the same label can appear twice (e.g. global `total` + a
        // function-local `total`); IDEs deduplicate by label.
        for (const auto &v : it->second.index.variables) {
            if (!v.scope_function.empty() && v.scope_function != scope) {
                continue;
            }
            cJSON *item = cJSON_CreateObject();
            cJSON_AddStringToObject(item, "label", v.name.c_str());
            // CompletionItemKind: 6 = Variable, but parameters and
            // top-level constants ride the same lane — we don't track
            // the distinction yet.
            cJSON_AddNumberToObject(item, "kind", 6);
            std::string detail = v.type.empty() ? std::string("<unknown>") : v.type;
            detail += " ";
            detail += v.name;
            cJSON_AddStringToObject(item, "detail", detail.c_str());
            cJSON_AddItemToArray(items, item);
        }
    }

    // 2) Builtins.
    size_t n = 0;
    const BuiltinEntry *table = builtin_table(&n);
    for (size_t i = 0; i < n; i++) {
        cJSON *item = cJSON_CreateObject();
        cJSON_AddStringToObject(item, "label", table[i].name);
        cJSON_AddNumberToObject(item, "kind", 3);  // Function
        cJSON_AddStringToObject(item, "detail", table[i].signature);
        if (table[i].doc && *table[i].doc) {
            cJSON *doc = cJSON_AddObjectToObject(item, "documentation");
            cJSON_AddStringToObject(doc, "kind", "markdown");
            cJSON_AddStringToObject(doc, "value", table[i].doc);
        }
        cJSON_AddItemToArray(items, item);
    }

    // 3) Keywords. Small enough to enumerate inline.
    static const char *kKeywords[] = {
        "func", "return", "if", "else", "while", "for", "in", "break",
        "continue", "import", "type", "var", "true", "false", "null",
        "try", "catch", "finally", "throw",
        "int", "float", "str", "bool", "json", "void",
    };
    for (auto kw : kKeywords) {
        cJSON *item = cJSON_CreateObject();
        cJSON_AddStringToObject(item, "label", kw);
        cJSON_AddNumberToObject(item, "kind", 14);  // Keyword
        cJSON_AddItemToArray(items, item);
    }

    send_response(id, result);
}

// Build a Location object pointing at a single (line, column) span of
// `name_len` characters. Convenience helper for references / rename.
cJSON *build_location_obj(const char *uri, int line_1based, int col_1based,
                          int name_len) {
    cJSON *loc = cJSON_CreateObject();
    cJSON_AddStringToObject(loc, "uri", uri);
    cJSON *range = cJSON_AddObjectToObject(loc, "range");
    int line0 = line_1based > 0 ? line_1based - 1 : 0;
    int col0  = col_1based  > 0 ? col_1based  - 1 : 0;
    cJSON *start = cJSON_AddObjectToObject(range, "start");
    cJSON_AddNumberToObject(start, "line", line0);
    cJSON_AddNumberToObject(start, "character", col0);
    cJSON *end = cJSON_AddObjectToObject(range, "end");
    cJSON_AddNumberToObject(end, "line", line0);
    cJSON_AddNumberToObject(end, "character", col0 + name_len);
    return loc;
}

// textDocument/references — return every call site for the symbol the
// cursor is on, plus optionally the declaration itself
// (`context.includeDeclaration`).
void handle_references(DocumentStore &docs, cJSON *id, cJSON *params) {
    if (!id) return;
    cJSON *td = cJSON_GetObjectItem(params, "textDocument");
    cJSON *pos = cJSON_GetObjectItem(params, "position");
    if (!td || !pos) { send_response(id, cJSON_CreateArray()); return; }
    const char *uri = cJSON_GetStringValue(cJSON_GetObjectItem(td, "uri"));
    if (!uri) { send_response(id, cJSON_CreateArray()); return; }
    auto it = docs.docs.find(uri);
    if (it == docs.docs.end()) { send_response(id, cJSON_CreateArray()); return; }

    int line = (int)cJSON_GetNumberValue(cJSON_GetObjectItem(pos, "line"));
    int ch   = (int)cJSON_GetNumberValue(cJSON_GetObjectItem(pos, "character"));
    std::string word = word_at_position(it->second.text, line, ch);
    if (word.empty()) { send_response(id, cJSON_CreateArray()); return; }

    bool include_decl = true;
    if (cJSON *ctx = cJSON_GetObjectItem(params, "context")) {
        cJSON *flag = cJSON_GetObjectItem(ctx, "includeDeclaration");
        if (cJSON_IsBool(flag)) include_decl = cJSON_IsTrue(flag);
    }

    cJSON *arr = cJSON_CreateArray();
    int name_len = (int)word.size();

    if (include_decl) {
        for (const auto &fn : it->second.index.functions) {
            if (fn.name == word) {
                cJSON_AddItemToArray(arr, build_location_obj(uri, fn.line,
                                                              fn.column, name_len));
            }
        }
    }
    for (const auto &cs : it->second.index.call_sites) {
        if (cs.name == word) {
            cJSON_AddItemToArray(arr, build_location_obj(uri, cs.line,
                                                          cs.column, name_len));
        }
    }
    send_response(id, arr);
}

// textDocument/rename — turn a position + new name into a WorkspaceEdit
// that swaps the symbol at every reference + the declaration. The
// editor then applies the edits as a single undoable atom.
void handle_rename(DocumentStore &docs, cJSON *id, cJSON *params) {
    if (!id) return;
    cJSON *td = cJSON_GetObjectItem(params, "textDocument");
    cJSON *pos = cJSON_GetObjectItem(params, "position");
    if (!td || !pos) { send_response(id, nullptr); return; }
    const char *uri = cJSON_GetStringValue(cJSON_GetObjectItem(td, "uri"));
    const char *new_name = cJSON_GetStringValue(
        cJSON_GetObjectItem(params, "newName"));
    if (!uri || !new_name || !*new_name) { send_response(id, nullptr); return; }
    auto it = docs.docs.find(uri);
    if (it == docs.docs.end()) { send_response(id, nullptr); return; }

    int line = (int)cJSON_GetNumberValue(cJSON_GetObjectItem(pos, "line"));
    int ch   = (int)cJSON_GetNumberValue(cJSON_GetObjectItem(pos, "character"));
    std::string old_name = word_at_position(it->second.text, line, ch);
    if (old_name.empty()) { send_response(id, nullptr); return; }

    cJSON *edit = cJSON_CreateObject();
    cJSON *changes = cJSON_AddObjectToObject(edit, "changes");
    cJSON *file_edits = cJSON_AddArrayToObject(changes, uri);
    int old_len = (int)old_name.size();

    auto add_edit = [&](int line_1, int col_1) {
        cJSON *te = cJSON_CreateObject();
        cJSON *range = cJSON_AddObjectToObject(te, "range");
        int line0 = line_1 > 0 ? line_1 - 1 : 0;
        int col0  = col_1  > 0 ? col_1  - 1 : 0;
        cJSON *start = cJSON_AddObjectToObject(range, "start");
        cJSON_AddNumberToObject(start, "line", line0);
        cJSON_AddNumberToObject(start, "character", col0);
        cJSON *end = cJSON_AddObjectToObject(range, "end");
        cJSON_AddNumberToObject(end, "line", line0);
        cJSON_AddNumberToObject(end, "character", col0 + old_len);
        cJSON_AddStringToObject(te, "newText", new_name);
        cJSON_AddItemToArray(file_edits, te);
    };

    for (const auto &fn : it->second.index.functions) {
        if (fn.name == old_name) add_edit(fn.line, fn.column);
    }
    for (const auto &cs : it->second.index.call_sites) {
        if (cs.name == old_name) add_edit(cs.line, cs.column);
    }
    send_response(id, edit);
}

// Split a rendered signature label like
//   "name(p1: type, p2: type, ...): ret"
// into per-parameter [start, end) byte ranges over the label string.
// LSP wants the param label expressed as offsets into the SignatureInfo
// label so the editor can bold/underline the active param verbatim.
//
// Top-level commas only — depth tracking lets a nested type like
// `array<int, str>` (hypothetical) keep its inner comma intact. We
// stop at the matching `)` so a return-type annotation past `):` is
// never mistaken for a final parameter.
struct ParamSpan { size_t start; size_t end; };
std::vector<ParamSpan> split_signature_params(const std::string &sig) {
    std::vector<ParamSpan> out;
    size_t open = sig.find('(');
    if (open == std::string::npos) return out;
    size_t i = open + 1;
    size_t param_start = i;
    int depth = 0;
    auto trim_span = [&](size_t s, size_t e) {
        while (s < e && (sig[s] == ' ' || sig[s] == '\t')) s++;
        while (e > s && (sig[e - 1] == ' ' || sig[e - 1] == '\t')) e--;
        if (e > s) out.push_back({s, e});
    };
    while (i < sig.size()) {
        char c = sig[i];
        if (c == '(' || c == '[' || c == '<') depth++;
        else if (c == ')' || c == ']' || c == '>') {
            if (depth == 0) {  // closing paren of the call
                trim_span(param_start, i);
                return out;
            }
            depth--;
        } else if (c == ',' && depth == 0) {
            trim_span(param_start, i);
            param_start = i + 1;
        }
        i++;
    }
    return out;
}

cJSON *build_signature_info_for_user(const IndexFunction &fn) {
    cJSON *sig = cJSON_CreateObject();
    std::string label = format_signature(fn);
    cJSON_AddStringToObject(sig, "label", label.c_str());
    if (!fn.leading_comment.empty()) {
        cJSON *doc = cJSON_AddObjectToObject(sig, "documentation");
        cJSON_AddStringToObject(doc, "kind", "markdown");
        cJSON_AddStringToObject(doc, "value", fn.leading_comment.c_str());
    }
    auto spans = split_signature_params(label);
    cJSON *params = cJSON_AddArrayToObject(sig, "parameters");
    for (const auto &sp : spans) {
        cJSON *p = cJSON_CreateObject();
        // [start, end] offsets — the editor highlights this exact slice.
        cJSON *range = cJSON_AddArrayToObject(p, "label");
        cJSON_AddItemToArray(range, cJSON_CreateNumber((double)sp.start));
        cJSON_AddItemToArray(range, cJSON_CreateNumber((double)sp.end));
        cJSON_AddItemToArray(params, p);
    }
    return sig;
}

cJSON *build_signature_info_for_builtin(const BuiltinEntry *b) {
    cJSON *sig = cJSON_CreateObject();
    cJSON_AddStringToObject(sig, "label", b->signature);
    if (b->doc && *b->doc) {
        cJSON *doc = cJSON_AddObjectToObject(sig, "documentation");
        cJSON_AddStringToObject(doc, "kind", "markdown");
        cJSON_AddStringToObject(doc, "value", b->doc);
    }
    auto spans = split_signature_params(b->signature);
    cJSON *params = cJSON_AddArrayToObject(sig, "parameters");
    for (const auto &sp : spans) {
        cJSON *p = cJSON_CreateObject();
        cJSON *range = cJSON_AddArrayToObject(p, "label");
        cJSON_AddItemToArray(range, cJSON_CreateNumber((double)sp.start));
        cJSON_AddItemToArray(range, cJSON_CreateNumber((double)sp.end));
        cJSON_AddItemToArray(params, p);
    }
    return sig;
}

void handle_signature_help(DocumentStore &docs, cJSON *id, cJSON *params) {
    if (!id) return;
    cJSON *td = cJSON_GetObjectItem(params, "textDocument");
    cJSON *pos = cJSON_GetObjectItem(params, "position");
    if (!td || !pos) { send_response(id, nullptr); return; }
    const char *uri = cJSON_GetStringValue(cJSON_GetObjectItem(td, "uri"));
    if (!uri) { send_response(id, nullptr); return; }
    auto it = docs.docs.find(uri);
    if (it == docs.docs.end()) { send_response(id, nullptr); return; }

    int line = (int)cJSON_GetNumberValue(cJSON_GetObjectItem(pos, "line"));
    int ch   = (int)cJSON_GetNumberValue(cJSON_GetObjectItem(pos, "character"));

    const std::string &src = it->second.text;
    size_t cursor_off = line_col_to_offset(src, line, ch);

    size_t func_off = 0;
    int active = 0;
    if (!find_enclosing_call(src, cursor_off, &func_off, &active)) {
        send_response(id, nullptr);
        return;
    }

    // Pull the identifier out of the source at func_off.
    size_t end = func_off;
    while (end < src.size() && is_ident_char(src[end])) end++;
    if (end <= func_off) { send_response(id, nullptr); return; }
    std::string name(src, func_off, end - func_off);

    // User functions shadow builtins (matches Tulpar's lookup order).
    cJSON *sig_info = nullptr;
    for (const auto &fn : it->second.index.functions) {
        if (fn.name == name) {
            sig_info = build_signature_info_for_user(fn);
            // Clamp active param to the actual arity so the highlight
            // doesn't fall off the end when the user types extra commas.
            int arity = (int)fn.params.size();
            if (arity > 0 && active >= arity) active = arity - 1;
            break;
        }
    }
    if (!sig_info) {
        if (auto *b = builtin_lookup(name.c_str())) {
            sig_info = build_signature_info_for_builtin(b);
            auto spans = split_signature_params(b->signature);
            int arity = (int)spans.size();
            if (arity > 0 && active >= arity) active = arity - 1;
        }
    }

    if (!sig_info) { send_response(id, nullptr); return; }

    cJSON *result = cJSON_CreateObject();
    cJSON *sigs = cJSON_AddArrayToObject(result, "signatures");
    cJSON_AddItemToArray(sigs, sig_info);
    cJSON_AddNumberToObject(result, "activeSignature", 0);
    cJSON_AddNumberToObject(result, "activeParameter", active);
    send_response(id, result);
}

void handle_definition(DocumentStore &docs, cJSON *id, cJSON *params) {
    if (!id) return;
    cJSON *td = cJSON_GetObjectItem(params, "textDocument");
    cJSON *pos = cJSON_GetObjectItem(params, "position");
    if (!td || !pos) { send_response(id, nullptr); return; }
    const char *uri = cJSON_GetStringValue(cJSON_GetObjectItem(td, "uri"));
    if (!uri) { send_response(id, nullptr); return; }
    auto it = docs.docs.find(uri);
    if (it == docs.docs.end()) { send_response(id, nullptr); return; }

    int line = (int)cJSON_GetNumberValue(cJSON_GetObjectItem(pos, "line"));
    int ch   = (int)cJSON_GetNumberValue(cJSON_GetObjectItem(pos, "character"));
    std::string word = word_at_position(it->second.text, line, ch);
    if (word.empty()) { send_response(id, nullptr); return; }

    for (const auto &fn : it->second.index.functions) {
        if (fn.name != word) continue;
        // Build a Location { uri, range }
        cJSON *loc = cJSON_CreateObject();
        cJSON_AddStringToObject(loc, "uri", uri);
        cJSON *range = cJSON_AddObjectToObject(loc, "range");
        int line0 = fn.line > 0 ? fn.line - 1 : 0;
        int col0 = fn.column > 0 ? fn.column - 1 : 0;
        cJSON *start = cJSON_AddObjectToObject(range, "start");
        cJSON_AddNumberToObject(start, "line", line0);
        cJSON_AddNumberToObject(start, "character", col0);
        cJSON *end = cJSON_AddObjectToObject(range, "end");
        cJSON_AddNumberToObject(end, "line", line0);
        cJSON_AddNumberToObject(end, "character", col0 + (int)fn.name.size());
        send_response(id, loc);
        return;
    }
    send_response(id, nullptr);
}

}  // namespace

int lsp_run_server() {
#if defined(_WIN32)
    // Without binary mode Windows mangles \r\n in both directions, which
    // breaks the Content-Length framing. _setmode also resets any existing
    // buffer, which is why we set it before installing the unbuffered
    // policy below.
    _setmode(_fileno(stdin), _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);
#endif
    // Force unbuffered stdout. With block buffering, the CRT may flush a
    // partial 4KB chunk and stall holding the rest until the next fflush,
    // which interacts badly with LSP clients reading message-by-message.
    std::setvbuf(stdout, nullptr, _IONBF, 0);

    DocumentStore docs;
    bool shutting_down = false;

    for (;;) {
        std::string body = read_message();
        if (body.empty()) break;  // EOF

        cJSON *msg = cJSON_Parse(body.c_str());
        if (!msg) continue;

        cJSON *id = cJSON_GetObjectItem(msg, "id");
        const char *method =
            cJSON_GetStringValue(cJSON_GetObjectItem(msg, "method"));
        cJSON *params = cJSON_GetObjectItem(msg, "params");

        if (!method) {
            cJSON_Delete(msg);
            continue;
        }

        if (std::strcmp(method, "initialize") == 0) {
            send_response(id, build_initialize_result());
        } else if (std::strcmp(method, "initialized") == 0) {
            // notification, no response
        } else if (std::strcmp(method, "shutdown") == 0) {
            shutting_down = true;
            send_response(id, nullptr);
        } else if (std::strcmp(method, "exit") == 0) {
            cJSON_Delete(msg);
            return shutting_down ? 0 : 1;
        } else if (std::strcmp(method, "textDocument/didOpen") == 0) {
            handle_did_open(docs, params);
        } else if (std::strcmp(method, "textDocument/didChange") == 0) {
            handle_did_change(docs, params);
        } else if (std::strcmp(method, "textDocument/didSave") == 0) {
            handle_did_save(docs, params);
        } else if (std::strcmp(method, "textDocument/didClose") == 0) {
            handle_did_close(docs, params);
        } else if (std::strcmp(method, "textDocument/hover") == 0) {
            handle_hover(docs, id, params);
        } else if (std::strcmp(method, "textDocument/completion") == 0) {
            handle_completion(docs, id, params);
        } else if (std::strcmp(method, "textDocument/signatureHelp") == 0) {
            handle_signature_help(docs, id, params);
        } else if (std::strcmp(method, "textDocument/definition") == 0) {
            handle_definition(docs, id, params);
        } else if (std::strcmp(method, "textDocument/references") == 0) {
            handle_references(docs, id, params);
        } else if (std::strcmp(method, "textDocument/rename") == 0) {
            handle_rename(docs, id, params);
        } else if (id) {
            // Unknown request — respond with a method-not-found error so
            // the client doesn't hang waiting for us. Notifications get
            // ignored silently per spec.
            cJSON *root = cJSON_CreateObject();
            cJSON_AddStringToObject(root, "jsonrpc", "2.0");
            cJSON_AddItemToObject(root, "id", cJSON_Duplicate(id, 1));
            cJSON *err = cJSON_AddObjectToObject(root, "error");
            cJSON_AddNumberToObject(err, "code", -32601);
            cJSON_AddStringToObject(err, "message", "Method not found");
            send_json(root);
            cJSON_Delete(root);
        }

        cJSON_Delete(msg);
    }

    return 0;
}

}  // namespace tulpar
