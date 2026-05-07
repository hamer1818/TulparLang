#include "formatter.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#if defined(_WIN32)
  #include <io.h>
  #include <fcntl.h>
#endif

namespace tulpar {

namespace {

bool is_blank(const std::string &line) {
    for (char c : line) {
        if (c != ' ' && c != '\t') return false;
    }
    return true;
}

std::string rstrip(const std::string &s) {
    size_t end = s.size();
    while (end > 0 && (s[end - 1] == ' ' || s[end - 1] == '\t' ||
                       s[end - 1] == '\r')) {
        end--;
    }
    return s.substr(0, end);
}

std::string lstrip(const std::string &s) {
    size_t i = 0;
    while (i < s.size() && (s[i] == ' ' || s[i] == '\t')) i++;
    return s.substr(i);
}

// Count net `{` minus `}` for a single source line, ignoring braces that
// occur inside line comments or string literals. We don't try to handle
// every edge case (escaped quotes inside backticks, e.g.) — Tulpar's
// surface syntax doesn't have backticks/raw strings, so a small two-state
// machine is sufficient.
int line_brace_delta(const std::string &line, int *open_in_line) {
    int delta = 0;
    int opens = 0;
    bool in_str = false;
    bool in_line_comment = false;
    char quote = 0;
    for (size_t i = 0; i < line.size(); i++) {
        char c = line[i];
        if (in_line_comment) break;
        if (in_str) {
            if (c == '\\' && i + 1 < line.size()) { i++; continue; }
            if (c == quote) in_str = false;
            continue;
        }
        if (c == '"' || c == '\'') { in_str = true; quote = c; continue; }
        if (c == '/' && i + 1 < line.size() && line[i + 1] == '/') {
            in_line_comment = true;
            break;
        }
        if (c == '{') { delta++; opens++; }
        else if (c == '}') { delta--; }
    }
    if (open_in_line) *open_in_line = opens;
    return delta;
}

// Returns true if the trimmed line begins with `}` (used to dedent
// closing-brace lines so they line up with their opener instead of the
// line above).
bool starts_with_close_brace(const std::string &trimmed) {
    return !trimmed.empty() && trimmed[0] == '}';
}

// ---------------------------------------------------------------------------
// Token-pass: normalise inter-token whitespace.
//
// Run *after* the line-by-line indent pass on each non-comment line:
//   * exactly one space after `,` and `;`, none before
//   * exactly one space around binary operators (`+`, `-`, `*`, `/`,
//     `==`, `!=`, `<`, `<=`, `>`, `>=`, `&&`, `||`, `=`, `+=`, `-=`)
//     when both sides are non-whitespace
//   * collapse runs of internal whitespace to a single space
//   * leading whitespace (indent) is preserved
//   * inside string literals and `//` line comments, nothing changes
//
// Unary `-` (e.g. `-1`, `return -x`) is intentionally left alone — the
// formatter shouldn't add a space between `-` and a numeric literal /
// identifier. We detect "looks-unary" when the previous non-space char
// is one of `( , = + - * / < > ! { [` or the line is empty so far.
// ---------------------------------------------------------------------------

bool char_implies_unary(char c) {
    // After any of these, a `-` or `+` is unary, not binary.
    return c == '\0' || c == '(' || c == '[' || c == '{' || c == ',' ||
           c == ';' || c == ':' || c == '=' || c == '<' || c == '>' ||
           c == '!' || c == '+' || c == '-' || c == '*' || c == '/' ||
           c == '%' || c == '&' || c == '|' || c == '?' || c == '~';
}

// Apply the token spacing rules to one already-stripped & pre-indented
// content line. The leading indent (everything up to the first non-
// space char) is passed in `indent` and re-prepended unchanged.
std::string normalise_line_spacing(const std::string &content) {
    std::string out;
    out.reserve(content.size() + 4);
    bool in_str = false;
    char quote = 0;

    auto last_emitted = [&]() -> char {
        return out.empty() ? '\0' : out.back();
    };

    for (size_t i = 0; i < content.size(); i++) {
        char c = content[i];

        // String state machine. We pass through every byte verbatim,
        // including embedded escapes; only the closing quote flips the
        // state back.
        if (in_str) {
            out.push_back(c);
            if (c == '\\' && i + 1 < content.size()) {
                out.push_back(content[i + 1]);
                i++;
                continue;
            }
            if (c == quote) in_str = false;
            continue;
        }
        if (c == '"' || c == '\'') {
            in_str = true; quote = c;
            out.push_back(c);
            continue;
        }
        // Line comment — pass through.
        if (c == '/' && i + 1 < content.size() && content[i + 1] == '/') {
            // Make sure exactly one space separates code from `//` if
            // there's preceding code on this line (gofmt-style).
            if (!out.empty() && out.back() != ' ') out.push_back(' ');
            out.append(content.substr(i));
            return out;
        }

        // Collapse runs of inline whitespace to a single space — but
        // suppress the space entirely when it would land directly
        // after an opener (`(` `[`) or directly before a closer
        // (peeked one char ahead).
        if (c == ' ' || c == '\t') {
            char prev = out.empty() ? '\0' : out.back();
            char next_nonspace = '\0';
            for (size_t k = i + 1; k < content.size(); k++) {
                if (content[k] != ' ' && content[k] != '\t') {
                    next_nonspace = content[k];
                    break;
                }
            }
            if (prev == '(' || prev == '[' ||
                next_nonspace == ')' || next_nonspace == ']' ||
                next_nonspace == ',' || next_nonspace == ';') {
                continue;  // swallow
            }
            if (prev != ' ' && prev != '\0') out.push_back(' ');
            continue;
        }

        // `:` — used for return types (`func f(): int`) and object
        // literal keys (`{"k": v}`). One space after, none before.
        if (c == ':') {
            // Skip `::` (not a Tulpar token today, but be defensive).
            if (i + 1 < content.size() && content[i + 1] == ':') {
                out.push_back(c);
                out.push_back(content[i + 1]);
                i++;
                continue;
            }
            if (!out.empty() && out.back() == ' ') out.pop_back();
            out.push_back(':');
            if (i + 1 < content.size() &&
                content[i + 1] != ' ' && content[i + 1] != '\t') {
                out.push_back(' ');
            }
            continue;
        }

        // `,` and `;` — never preceded by space, always followed by one
        // (unless they end the line — handled implicitly because the
        // outer loop strips trailing whitespace later).
        if (c == ',' || c == ';') {
            if (!out.empty() && out.back() == ' ') out.pop_back();
            out.push_back(c);
            if (i + 1 < content.size() &&
                content[i + 1] != ' ' && content[i + 1] != '\t' &&
                content[i + 1] != ')' && content[i + 1] != ']' &&
                content[i + 1] != '}') {
                out.push_back(' ');
            }
            continue;
        }

        // `)` followed by `{` — gofmt-style, ensure space.
        if (c == '{' && !out.empty() && out.back() == ')') {
            out.push_back(' ');
            out.push_back('{');
            continue;
        }
        // Identifier directly followed by `{` — same gofmt rule applied
        // to return types (`func f(): int{`), struct heads (`Point p{`),
        // etc. Without this, the keyword-spacing pass leaves `int{` and
        // similar token glue intact.
        if (c == '{' && !out.empty()) {
            char prev = out.back();
            bool is_word_char = (prev >= 'a' && prev <= 'z') ||
                                (prev >= 'A' && prev <= 'Z') ||
                                (prev >= '0' && prev <= '9') ||
                                prev == '_';
            if (is_word_char) {
                out.push_back(' ');
                out.push_back('{');
                continue;
            }
        }

        // Generic type opener: `array<int>`, `array<json>`, etc. Tulpar
        // tokenizes these as single TOKEN_ARRAY_INT-style tokens but the
        // formatter walks the source character-by-character, so without
        // a lookahead `<` falls through to the binary-operator path and
        // gets spaced into `array < int >`. Detect the closed shape
        // `<TYPE>` and emit it verbatim.
        if (c == '<') {
            static const char *const kGenericInner[] = {
                "int", "float", "str", "bool", "json", "array"
            };
            // Walk past optional whitespace after `<`, match a type
            // keyword, walk past optional whitespace, require `>`.
            size_t p = i + 1;
            while (p < content.size() &&
                   (content[p] == ' ' || content[p] == '\t')) p++;
            int matched = 0;
            for (auto kw : kGenericInner) {
                size_t kw_len = std::strlen(kw);
                if (p + kw_len > content.size()) continue;
                if (std::memcmp(&content[p], kw, kw_len) != 0) continue;
                char after = (p + kw_len < content.size())
                                 ? content[p + kw_len] : '\0';
                bool word_break = !((after >= 'a' && after <= 'z') ||
                                    (after >= 'A' && after <= 'Z') ||
                                    (after >= '0' && after <= '9') ||
                                    after == '_');
                if (!word_break) continue;
                matched = (int)kw_len;
                break;
            }
            if (matched) {
                size_t q = p + matched;
                while (q < content.size() &&
                       (content[q] == ' ' || content[q] == '\t')) q++;
                if (q < content.size() && content[q] == '>') {
                    // Confirmed generic. The whitespace pass above may
                    // already have queued a space before `array`; we
                    // want exactly one space between the previous token
                    // and `array`, but no space between `array` and `<`,
                    // nor between `<` and the type, etc. The `array`
                    // text is already in `out`; trim a trailing space
                    // we may have just emitted.
                    while (!out.empty() && out.back() == ' ') out.pop_back();
                    out.push_back('<');
                    out.append(&content[p], (size_t)matched);
                    out.push_back('>');
                    i = q;  // skip past the `>` (loop increments)
                    continue;
                }
            }
            // Not a generic — fall through to binary-op handling.
        }

        // Multi-char operators: detect `==`, `!=`, `<=`, `>=`, `&&`,
        // `||`, `+=`, `-=`, `*=`, `/=` and `=` standalone.
        auto two = [&](char a, char b) {
            return c == a && i + 1 < content.size() && content[i + 1] == b;
        };

        // Skip member-access dot — no spaces around it ever.
        if (c == '.') { out.push_back(c); continue; }

        // Detect operators that take padding.
        bool is_op = false;
        size_t op_len = 1;
        if (two('=', '=') || two('!', '=') || two('<', '=') ||
            two('>', '=') || two('&', '&') || two('|', '|') ||
            two('+', '=') || two('-', '=') || two('*', '=') ||
            two('/', '=')) {
            is_op = true; op_len = 2;
        } else if (c == '+' || c == '*' || c == '/' || c == '%' ||
                   c == '<' || c == '>' || c == '=') {
            is_op = true;
        } else if (c == '-') {
            // Unary vs binary — peek at last emitted non-space char.
            char prev = last_emitted();
            // Walk backwards over a space if present so we look at the
            // real previous token.
            if (prev == ' ' && out.size() >= 2) prev = out[out.size() - 2];
            is_op = !char_implies_unary(prev);
        } else if (c == '!') {
            // Standalone `!` is unary; only paired form `!=` is binary
            // and that's handled above.
            is_op = false;
        }

        if (is_op) {
            // Ensure exactly one space before (unless line empty).
            if (!out.empty() && out.back() != ' ') out.push_back(' ');
            for (size_t k = 0; k < op_len; k++) out.push_back(content[i + k]);
            i += op_len - 1;
            // Ensure one space after, unless the next char is space or
            // `(` (e.g. `return -x` already-handled by unary path).
            if (i + 1 < content.size() &&
                content[i + 1] != ' ' && content[i + 1] != '\t') {
                out.push_back(' ');
            }
            continue;
        }

        out.push_back(c);
    }

    // rstrip the line — token rules above sometimes leave a trailing
    // space (e.g. after `,` when the source had `a , `).
    while (!out.empty() && (out.back() == ' ' || out.back() == '\t')) {
        out.pop_back();
    }

    // ---- Keyword spacing pass --------------------------------------------
    //
    // The token-level pass above doesn't know about control-flow
    // keywords. Without this fixup, `if(x)` and `}else{` survive. Walk
    // the produced line one more time, respecting string state, and:
    //
    //   * Insert a space before `else`/`catch`/`finally` when preceded
    //     by `}` with no space.
    //   * Insert a space between `if`/`while`/`for`/`catch`/`else if`
    //     and the following `(` or `{` (when there is none).
    //
    // Keywords are matched only when surrounded by word boundaries so
    // an identifier like `else_branch` isn't touched.
    {
        auto is_word = [](char c) {
            return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                   (c >= '0' && c <= '9') || c == '_';
        };
        std::string fixed;
        fixed.reserve(out.size() + 4);
        bool in_str2 = false;
        char quote2 = 0;

        for (size_t i = 0; i < out.size(); i++) {
            char c = out[i];
            if (in_str2) {
                fixed.push_back(c);
                if (c == '\\' && i + 1 < out.size()) {
                    fixed.push_back(out[i + 1]);
                    i++;
                    continue;
                }
                if (c == quote2) in_str2 = false;
                continue;
            }
            if (c == '"' || c == '\'') {
                in_str2 = true; quote2 = c;
                fixed.push_back(c);
                continue;
            }
            // Line comment — pass through verbatim.
            if (c == '/' && i + 1 < out.size() && out[i + 1] == '/') {
                fixed.append(out.substr(i));
                break;
            }

            // Try to recognise an identifier starting at i and decide
            // if it's a keyword we want to space-pad.
            char prev_nonspace = '\0';
            for (size_t k = fixed.size(); k > 0; k--) {
                if (fixed[k - 1] != ' ') { prev_nonspace = fixed[k - 1]; break; }
            }
            if (is_word(c) && (i == 0 || !is_word(out[i - 1]))) {
                size_t end = i;
                while (end < out.size() && is_word(out[end])) end++;
                std::string word = out.substr(i, end - i);

                bool is_pre_paren = (word == "if" || word == "while" ||
                                     word == "for" || word == "catch");
                bool is_pre_brace = (word == "else" || word == "try" ||
                                     word == "finally" || word == "do");
                bool needs_after = is_pre_paren || is_pre_brace;

                // Space before keyword if attached to `}` (e.g. `}else`).
                if ((word == "else" || word == "catch" || word == "finally") &&
                    prev_nonspace == '}' && !fixed.empty() &&
                    fixed.back() == '}') {
                    fixed.push_back(' ');
                }

                fixed.append(word);
                i = end - 1;

                if (needs_after && end < out.size() &&
                    (out[end] == '(' || out[end] == '{')) {
                    fixed.push_back(' ');
                }
                continue;
            }

            fixed.push_back(c);
        }
        out = std::move(fixed);
    }

    return out;
}

}  // namespace

std::string fmt_source(const std::string &source, int indent_width) {
    if (indent_width < 0) indent_width = 0;

    // Split on newlines, preserving blank lines. We treat \r\n and \n
    // identically; the output uses \n only.
    std::vector<std::string> lines;
    {
        std::string cur;
        for (char c : source) {
            if (c == '\n') { lines.push_back(std::move(cur)); cur.clear(); }
            else { cur.push_back(c); }
        }
        if (!cur.empty()) lines.push_back(std::move(cur));
    }

    std::string out;
    out.reserve(source.size());
    int depth = 0;
    int consecutive_blank = 0;
    for (const auto &raw : lines) {
        std::string trimmed = lstrip(rstrip(raw));
        if (trimmed.empty()) {
            // Blank line: emit just the newline, no indent. We collapse
            // runs of two or more blank lines to a single one — gofmt /
            // black / rustfmt all do this and it keeps diffs clean.
            consecutive_blank++;
            if (consecutive_blank <= 1) out.push_back('\n');
            continue;
        }
        consecutive_blank = 0;
        int print_depth = depth;
        if (starts_with_close_brace(trimmed) && print_depth > 0) {
            // The closing brace itself lines up with the opener.
            print_depth--;
        }
        // Token-pass: normalise inter-token whitespace on the trimmed
        // line. This is the second `tulpar fmt` pass; the first pass
        // (above) handled indentation + blank-line collapse.
        std::string normalised = normalise_line_spacing(trimmed);
        for (int i = 0; i < print_depth * indent_width; i++) out.push_back(' ');
        out.append(normalised);
        out.push_back('\n');

        int delta = line_brace_delta(trimmed, nullptr);
        depth += delta;
        if (depth < 0) depth = 0;  // tolerate unbalanced input
    }

    // Ensure exactly one trailing newline (we already appended `\n` after
    // every non-empty line; any leading blank-only padding stays).
    while (out.size() >= 2 && out[out.size() - 1] == '\n' &&
           out[out.size() - 2] == '\n') {
        out.pop_back();
    }
    if (out.empty() || out.back() != '\n') out.push_back('\n');

    return out;
}

int fmt_cli_main(int argc, char **argv) {
    // argv layout when called from main(): argv[0]="tulpar", argv[1]="fmt",
    // and the rest are the formatter's own args.
    bool write_in_place = false;
    const char *path = nullptr;

    for (int i = 2; i < argc; i++) {
        if (std::strcmp(argv[i], "--write") == 0 ||
            std::strcmp(argv[i], "-w") == 0) {
            write_in_place = true;
        } else if (argv[i][0] == '-') {
            std::fprintf(stderr, "tulpar fmt: unknown flag '%s'\n", argv[i]);
            return 2;
        } else if (!path) {
            path = argv[i];
        } else {
            std::fprintf(stderr, "tulpar fmt: only one file at a time (got '%s' and '%s')\n",
                         path, argv[i]);
            return 2;
        }
    }

    if (!path) {
        std::fprintf(stderr, "Usage: tulpar fmt <file.tpr> [--write]\n");
        return 2;
    }

    std::ifstream in(path, std::ios::binary);
    if (!in) {
        std::fprintf(stderr, "tulpar fmt: cannot open '%s'\n", path);
        return 1;
    }
    std::stringstream ss;
    ss << in.rdbuf();
    std::string source = ss.str();
    in.close();

    std::string formatted = fmt_source(source, 4);

    if (write_in_place) {
        if (formatted == source) return 0;  // already formatted
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        if (!out) {
            std::fprintf(stderr, "tulpar fmt: cannot write '%s'\n", path);
            return 1;
        }
        out.write(formatted.data(), (std::streamsize)formatted.size());
        return 0;
    }

#if defined(_WIN32)
    // Windows' CRT translates `\n` to `\r\n` on stdout text mode. Switch
    // to binary so piping the formatter into other tools (or comparing
    // it against a "golden" file) gets clean LF — matches gofmt.
    _setmode(_fileno(stdout), _O_BINARY);
#endif
    std::fwrite(formatted.data(), 1, formatted.size(), stdout);
    return 0;
}

}  // namespace tulpar
