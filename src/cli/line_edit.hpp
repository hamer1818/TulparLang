// Minimal cross-platform line editor for the REPL.
//
// Goals:
//   - Arrow-key history (up/down)
//   - In-line editing (left/right/home/end/backspace/delete)
//   - Persistent history file
//   - Falls back to fgets() when stdin is not a TTY (test scripts, pipes)
//
// Non-goals (deferred):
//   - Tab completion
//   - Multi-line editing (handled by the REPL accumulator above us)
//   - Wide-char/UTF-8 column accounting (best-effort byte-wise editing)

#ifndef TULPAR_LINE_EDIT_H
#define TULPAR_LINE_EDIT_H

#ifdef __cplusplus
extern "C" {
#endif

struct LineEditor;

// `history_path` may be NULL to disable persistence. If non-NULL and the file
// exists, history is loaded from it; on destroy the latest history is written
// back (best-effort).
struct LineEditor *line_editor_create(const char *history_path);

// Returns malloc'd line WITHOUT trailing newline, or NULL on EOF / read error.
// Caller frees with free().
char *line_editor_readline(struct LineEditor *ed, const char *prompt);

// Adds a line to the in-memory history (deduped against the most recent entry).
// Empty lines are ignored.
void line_editor_add_history(struct LineEditor *ed, const char *line);

void line_editor_destroy(struct LineEditor *ed);

#ifdef __cplusplus
}
#endif

#endif // TULPAR_LINE_EDIT_H
