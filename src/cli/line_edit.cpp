// Minimal cross-platform line editor for tulpar --repl.
// See line_edit.hpp for goals and limits.

#include "line_edit.hpp"
#include "../common/platform.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cctype>

#if PLATFORM_WINDOWS
#include <conio.h>
#include <io.h>
#include <windows.h>
#define ISATTY(fd) _isatty(fd)
#define FILENO _fileno
#else
#include <termios.h>
#include <unistd.h>
#define ISATTY(fd) isatty(fd)
#define FILENO fileno
#endif

namespace {

constexpr int kHistoryMax = 500;
constexpr size_t kLineMax = 4096;

}  // namespace

struct LineEditor {
  char **history;            // owned strings, history[0]=oldest
  int history_count;
  int history_capacity;
  char *history_path;        // NULL = no persist
  bool stdin_is_tty;
  bool stdout_is_tty;
};

// ---------------------------------------------------------------------------
// History storage
// ---------------------------------------------------------------------------

static void history_grow(LineEditor *ed) {
  int new_cap = ed->history_capacity == 0 ? 64 : ed->history_capacity * 2;
  if (new_cap > kHistoryMax) new_cap = kHistoryMax;
  if (new_cap == ed->history_capacity) return;
  ed->history = static_cast<char **>(
      realloc(ed->history, sizeof(char *) * new_cap));
  ed->history_capacity = new_cap;
}

static void history_append(LineEditor *ed, const char *line) {
  if (!line || !*line) return;
  // Dedupe against the most recent entry.
  if (ed->history_count > 0 &&
      strcmp(ed->history[ed->history_count - 1], line) == 0) {
    return;
  }
  if (ed->history_count >= kHistoryMax) {
    // Drop oldest.
    free(ed->history[0]);
    memmove(ed->history, ed->history + 1,
            sizeof(char *) * (kHistoryMax - 1));
    ed->history_count = kHistoryMax - 1;
  }
  if (ed->history_count >= ed->history_capacity) history_grow(ed);
  ed->history[ed->history_count++] = strdup(line);
}

static void history_load(LineEditor *ed) {
  if (!ed->history_path) return;
  FILE *f = fopen(ed->history_path, "rb");
  if (!f) return;
  char buf[kLineMax];
  while (fgets(buf, sizeof(buf), f)) {
    size_t n = strlen(buf);
    while (n > 0 && (buf[n - 1] == '\n' || buf[n - 1] == '\r')) {
      buf[--n] = '\0';
    }
    if (n > 0) history_append(ed, buf);
  }
  fclose(f);
}

static void history_save(LineEditor *ed) {
  if (!ed->history_path || ed->history_count == 0) return;
  FILE *f = fopen(ed->history_path, "wb");
  if (!f) return;
  for (int i = 0; i < ed->history_count; i++) {
    fputs(ed->history[i], f);
    fputc('\n', f);
  }
  fclose(f);
}

// ---------------------------------------------------------------------------
// Terminal I/O — platform shims
// ---------------------------------------------------------------------------

#if PLATFORM_WINDOWS
// Single-byte console read. Returns >= 0 on success. Special keys come as a
// 0x00 / 0xE0 prefix followed by a code; we collapse them into a synthetic
// negative value (see Key enum below) so the main loop can switch cleanly.
//
// We use _getch from conio.h: it does not echo and returns one keystroke at
// a time, so we can render the prompt + buffer ourselves.
static int read_key_win() {
  int c = _getch();
  if (c == 0 || c == 0xE0) {
    int c2 = _getch();
    return -c2;  // mark as escape — see Key constants below.
  }
  return c;
}
#else
// On POSIX we put the terminal in raw mode for the duration of readline()
// and restore it on exit. Each call to read_key_posix returns either a
// regular byte or a synthetic negative value for arrow keys etc.

struct PosixRaw {
  termios saved;
  bool active;
};

static void raw_enter(PosixRaw *raw) {
  raw->active = false;
  if (tcgetattr(STDIN_FILENO, &raw->saved) != 0) return;
  termios t = raw->saved;
  t.c_lflag &= ~(ICANON | ECHO);
  t.c_iflag &= ~(IXON | ICRNL);
  t.c_cc[VMIN] = 1;
  t.c_cc[VTIME] = 0;
  if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &t) != 0) return;
  raw->active = true;
}

static void raw_leave(PosixRaw *raw) {
  if (raw->active) {
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw->saved);
    raw->active = false;
  }
}

static int read_byte_posix() {
  unsigned char c;
  ssize_t n = read(STDIN_FILENO, &c, 1);
  if (n != 1) return -1000;  // EOF / error
  return c;
}

// Parse an ANSI escape sequence following an initial ESC byte. Returns a
// synthetic key code or 27 (ESC) if the sequence didn't match.
static int read_escape_posix() {
  int b1 = read_byte_posix();
  if (b1 != '[' && b1 != 'O') return 27;  // bare ESC
  int b2 = read_byte_posix();
  if (b2 == -1000) return 27;
  if (b2 >= '0' && b2 <= '9') {
    // ESC [ <digit> ... ~  — eat until '~' or letter
    int b3 = read_byte_posix();
    if (b3 == '~') {
      switch (b2) {
        case '1': return -'G';   // Home
        case '3': return -'S';   // Delete
        case '4': return -'O';   // End
        case '7': return -'G';
        case '8': return -'O';
        default: return 27;
      }
    }
    // Long sequence (e.g., "[1;5C") — drain to letter and ignore.
    while (b3 != -1000 && !(b3 >= 'A' && b3 <= 'Z') && b3 != '~') {
      b3 = read_byte_posix();
    }
    return 27;
  }
  switch (b2) {
    case 'A': return -'H';   // Up
    case 'B': return -'P';   // Down
    case 'C': return -'M';   // Right
    case 'D': return -'K';   // Left
    case 'H': return -'G';   // Home
    case 'F': return -'O';   // End
    default: return 27;
  }
}

static int read_key_posix() {
  int c = read_byte_posix();
  if (c == 27) return read_escape_posix();
  return c;
}
#endif  // PLATFORM_WINDOWS

// Synthetic key codes shared by both backends. Values match the Windows
// _getch() second-byte alphabet so the switch in editor_loop() is uniform.
constexpr int kKeyUp     = -'H';
constexpr int kKeyDown   = -'P';
constexpr int kKeyLeft   = -'K';
constexpr int kKeyRight  = -'M';
constexpr int kKeyHome   = -'G';
constexpr int kKeyEnd    = -'O';
constexpr int kKeyDelete = -'S';

// ---------------------------------------------------------------------------
// Rendering
// ---------------------------------------------------------------------------

// Repaint: \r → prompt → buffer → ESC[K (clear to EOL) → move cursor back to
// `pos` from end. We assume the prompt fits on one line and the buffer
// doesn't wrap; both true for typical REPL use.
static void repaint(const char *prompt, const char *buf, size_t len,
                    size_t pos) {
  fputc('\r', stdout);
  fputs(prompt, stdout);
  fwrite(buf, 1, len, stdout);
  fputs("\033[K", stdout);  // clear to end of line
  if (pos < len) {
    // Move cursor left (len - pos) cells.
    fprintf(stdout, "\033[%zuD", len - pos);
  }
  fflush(stdout);
}

// ---------------------------------------------------------------------------
// Editor loop
// ---------------------------------------------------------------------------

// Returns 0 on Enter, 1 on Ctrl-C, -1 on EOF.
// On success, `out_line` is malloc'd by the caller (kLineMax) and contains the
// finished line at exit; `*out_len` has its byte length (no trailing newline).
static int editor_loop(LineEditor *ed, const char *prompt, char *out_line,
                       size_t *out_len) {
  size_t pos = 0;
  size_t len = 0;
  out_line[0] = '\0';

  // History navigation state. `history_idx == ed->history_count` means we are
  // editing a fresh line (not browsing). Pressing Up moves toward 0; Down
  // moves toward history_count.
  int history_idx = ed->history_count;
  // We stash the user's in-progress line so Down past the latest history
  // entry restores it.
  char saved_line[kLineMax];
  size_t saved_len = 0;
  saved_line[0] = '\0';

  fputs(prompt, stdout);
  fflush(stdout);

  while (true) {
#if PLATFORM_WINDOWS
    int key = read_key_win();
#else
    int key = read_key_posix();
#endif

    if (key == -1000) {
      // EOF
      return -1;
    }

    if (key == 13 || key == 10) {  // Enter (CR or LF)
      fputc('\n', stdout);
      fflush(stdout);
      out_line[len] = '\0';
      *out_len = len;
      return 0;
    }
    if (key == 3) {  // Ctrl-C
      fputs("^C\n", stdout);
      fflush(stdout);
      return 1;
    }
    if (key == 4 && len == 0) {  // Ctrl-D on empty line = EOF
      return -1;
    }
    if (key == 8 || key == 127) {  // Backspace
      if (pos > 0) {
        memmove(out_line + pos - 1, out_line + pos, len - pos);
        pos--;
        len--;
        out_line[len] = '\0';
        repaint(prompt, out_line, len, pos);
      }
      continue;
    }
    if (key == kKeyDelete) {
      if (pos < len) {
        memmove(out_line + pos, out_line + pos + 1, len - pos - 1);
        len--;
        out_line[len] = '\0';
        repaint(prompt, out_line, len, pos);
      }
      continue;
    }
    if (key == kKeyLeft) {
      if (pos > 0) {
        pos--;
        repaint(prompt, out_line, len, pos);
      }
      continue;
    }
    if (key == kKeyRight) {
      if (pos < len) {
        pos++;
        repaint(prompt, out_line, len, pos);
      }
      continue;
    }
    if (key == kKeyHome || key == 1) {  // Ctrl-A
      pos = 0;
      repaint(prompt, out_line, len, pos);
      continue;
    }
    if (key == kKeyEnd || key == 5) {  // Ctrl-E
      pos = len;
      repaint(prompt, out_line, len, pos);
      continue;
    }
    if (key == 21) {  // Ctrl-U: clear line
      pos = 0;
      len = 0;
      out_line[0] = '\0';
      repaint(prompt, out_line, len, pos);
      continue;
    }
    if (key == kKeyUp) {
      if (history_idx > 0) {
        if (history_idx == ed->history_count) {
          // Save in-progress line before browsing.
          memcpy(saved_line, out_line, len);
          saved_len = len;
          saved_line[saved_len] = '\0';
        }
        history_idx--;
        const char *h = ed->history[history_idx];
        size_t hlen = strlen(h);
        if (hlen >= kLineMax) hlen = kLineMax - 1;
        memcpy(out_line, h, hlen);
        out_line[hlen] = '\0';
        len = hlen;
        pos = len;
        repaint(prompt, out_line, len, pos);
      }
      continue;
    }
    if (key == kKeyDown) {
      if (history_idx < ed->history_count) {
        history_idx++;
        if (history_idx == ed->history_count) {
          // Restore in-progress line.
          memcpy(out_line, saved_line, saved_len);
          out_line[saved_len] = '\0';
          len = saved_len;
        } else {
          const char *h = ed->history[history_idx];
          size_t hlen = strlen(h);
          if (hlen >= kLineMax) hlen = kLineMax - 1;
          memcpy(out_line, h, hlen);
          out_line[hlen] = '\0';
          len = hlen;
        }
        pos = len;
        repaint(prompt, out_line, len, pos);
      }
      continue;
    }

    // Printable ASCII (and high bytes for UTF-8 best-effort).
    if (key >= 32 && key < 0x100 && len + 1 < kLineMax) {
      if (pos < len) {
        memmove(out_line + pos + 1, out_line + pos, len - pos);
      }
      out_line[pos++] = static_cast<char>(key);
      len++;
      out_line[len] = '\0';
      repaint(prompt, out_line, len, pos);
      continue;
    }
    // Unhandled key — ignore.
  }
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

extern "C" LineEditor *line_editor_create(const char *history_path) {
  LineEditor *ed = static_cast<LineEditor *>(calloc(1, sizeof(LineEditor)));
  if (!ed) return nullptr;
  ed->stdin_is_tty = ISATTY(FILENO(stdin)) != 0;
  ed->stdout_is_tty = ISATTY(FILENO(stdout)) != 0;
  if (history_path && *history_path) {
    ed->history_path = strdup(history_path);
    history_load(ed);
  }
  return ed;
}

extern "C" char *line_editor_readline(LineEditor *ed, const char *prompt) {
  // Non-tty stdin: keep classic fgets behavior so test scripts and pipes
  // continue to work without modification.
  if (!ed || !ed->stdin_is_tty || !ed->stdout_is_tty) {
    fputs(prompt, stdout);
    fflush(stdout);
    char buf[kLineMax];
    if (!fgets(buf, sizeof(buf), stdin)) return nullptr;
    size_t n = strlen(buf);
    while (n > 0 && (buf[n - 1] == '\n' || buf[n - 1] == '\r')) {
      buf[--n] = '\0';
    }
    return strdup(buf);
  }

  char *line = static_cast<char *>(malloc(kLineMax));
  if (!line) return nullptr;
  size_t len = 0;

#if !PLATFORM_WINDOWS
  PosixRaw raw;
  raw_enter(&raw);
#endif

  int rc = editor_loop(ed, prompt, line, &len);

#if !PLATFORM_WINDOWS
  raw_leave(&raw);
#endif

  if (rc == -1) {
    free(line);
    return nullptr;  // EOF
  }
  if (rc == 1) {
    // Ctrl-C: return empty string so the REPL's outer loop reprompts.
    line[0] = '\0';
    return line;
  }
  return line;
}

extern "C" void line_editor_add_history(LineEditor *ed, const char *line) {
  if (!ed) return;
  history_append(ed, line);
}

extern "C" void line_editor_destroy(LineEditor *ed) {
  if (!ed) return;
  history_save(ed);
  for (int i = 0; i < ed->history_count; i++) free(ed->history[i]);
  free(ed->history);
  free(ed->history_path);
  free(ed);
}
