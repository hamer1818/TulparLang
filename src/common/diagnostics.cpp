#include "diagnostics.hpp"

namespace tulpar {

namespace {
bool g_active = false;
std::vector<Diagnostic> g_records;
}  // namespace

void diag_sink_enable() {
    g_active = true;
    g_records.clear();
}

void diag_sink_disable() {
    g_active = false;
    g_records.clear();
}

bool diag_sink_active() { return g_active; }

void diag_sink_push(int line, int column, int length,
                    const char *severity, const char *message,
                    const char *hint) {
    if (!g_active) return;
    Diagnostic d;
    d.line = line;
    d.column = column;
    d.length = length;
    d.severity = severity ? severity : "error";
    d.message = message ? message : "";
    d.hint = hint ? hint : "";
    g_records.push_back(std::move(d));
}

std::vector<Diagnostic> diag_sink_drain() {
    std::vector<Diagnostic> out;
    out.swap(g_records);
    return out;
}

}  // namespace tulpar
