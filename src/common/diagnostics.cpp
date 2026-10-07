#include "diagnostics.hpp"

namespace tulpar {

namespace {
bool g_active = false;
std::vector<Diagnostic> g_records;
std::string g_file;
bool g_quiet = false;
int g_anchor_line = 0;
}  // namespace

void diag_set_file(const char *file) { g_file = file ? file : ""; }
const char *diag_file() { return g_file.c_str(); }
void diag_set_quiet(bool quiet) { g_quiet = quiet; }
bool diag_quiet() { return g_quiet; }
void diag_set_anchor_line(int line) { g_anchor_line = line; }
int diag_anchor_line() { return g_anchor_line; }

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
                    const char *hint, const char *file) {
    if (!g_active) return;
    Diagnostic d;
    d.line = line;
    d.column = column;
    d.length = length;
    d.severity = severity ? severity : "error";
    d.message = message ? message : "";
    d.hint = hint ? hint : "";
    d.file = file ? file : g_file;
    d.anchor_line = g_anchor_line;
    g_records.push_back(std::move(d));
}

std::vector<Diagnostic> diag_sink_drain() {
    std::vector<Diagnostic> out;
    out.swap(g_records);
    return out;
}

}  // namespace tulpar
