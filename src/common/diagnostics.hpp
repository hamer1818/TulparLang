#ifndef TULPAR_DIAGNOSTICS_HPP
#define TULPAR_DIAGNOSTICS_HPP

#include <string>
#include <vector>

namespace tulpar {

// Structured diagnostic record. Lines/columns are 1-based to match the
// existing parser/codegen rendering; LSP clients re-base to 0 when needed.
// `length` is the caret span (in source bytes) — 0 means "no specific
// caret token; highlight the whole line".
struct Diagnostic {
    int line;
    int column;
    int length;
    std::string severity;
    std::string message;
    std::string hint;
};

// Process-global sink. When active, renderers in parser.cpp /
// llvm_backend.cpp push records here instead of writing to stderr.
// Single-threaded compilation is the only context that ever runs this,
// so a global is fine (matches the existing parser_set_diagnostic_context
// pattern).
void diag_sink_enable();
void diag_sink_disable();
bool diag_sink_active();
void diag_sink_push(int line, int column, int length,
                    const char *severity, const char *message,
                    const char *hint);
std::vector<Diagnostic> diag_sink_drain();

}  // namespace tulpar

#endif  // TULPAR_DIAGNOSTICS_HPP
