// Tulpar Debug Adapter Protocol (DAP) entry point.
//
// Plan 07 Part B — Debugger MVP, DAP server.
//
// What's wired up right now:
//   - stdio JSON-RPC framing (`Content-Length: N\r\n\r\n<json>`).
//   - `initialize` → capabilities + `initialized` event.
//   - `launch` → AOT-builds the .tpr with `--debug` (DWARF emit from
//     Plan 07 Part A) so a gdb subprocess can attach to it later.
//   - `setBreakpoints` → forwards each (path, line) to gdb MI
//     (`-break-insert`) and returns the verified Breakpoint[] reply.
//   - `configurationDone` → spawns `gdb --interpreter=mi3 -nx <binary>`
//     and sends `-exec-run`. A background reader thread parses MI
//     async records and pushes DAP events:
//       *stopped(reason=breakpoint-hit)   →  stopped(reason=breakpoint)
//       *stopped(reason=exited-normally)  →  stopped(reason=exit) + terminated
//       *stopped(reason=signal-received)  →  stopped(reason=exception)
//       ~"..." console + @"..." target    →  output(category=stdout)
//   - `threads` → single fake thread { id: 1, name: "main" }.
//   - `terminate` / `disconnect` → sends `-gdb-exit`, reaps subprocess.
//
// What's intentionally NOT wired up yet (later PRs in Plan 07 Part B):
//   - `stackTrace` / `scopes` / `variables` — return "not implemented".
//   - `continue` / `next` / `stepIn` / `stepOut` — return "not implemented".
//   - `evaluate` / `setVariable` — return "not implemented".
//   - Conditional / log / hit-count / function breakpoints.
//
// stdin/stdout are owned by this command — every diagnostic line goes
// to stderr only (LSP follows the same rule, for the same reason).
// The reader thread shares stdout with the main thread, so every
// `write_message` call goes through `g_stdout_mu`.

#include "debug_cmd.hpp"
#include "../aot/aot_pipeline.hpp"

extern "C" {
#include "../../runtime/cJSON.h"
}

#include <atomic>
#include <cctype>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <sys/stat.h>
#include <thread>
#include <unordered_map>
#include <vector>

#ifdef _WIN32
  #include <fcntl.h>
  #include <io.h>
  #include <windows.h>
  #define DUP _dup
  #define DUP2 _dup2
  #define FILENO _fileno
  #define CLOSE _close
#else
  #include <signal.h>
  #include <sys/wait.h>
  #include <unistd.h>
  #define DUP dup
  #define DUP2 dup2
  #define FILENO fileno
  #define CLOSE close
#endif

namespace tulpar {
namespace {

// Bumped on every response/event we send. Spec leaves the
// sequence-number semantics flexible — receivers correlate by
// `request_seq` field on responses, not by reusing the client's
// seq number — but we keep ours monotonically increasing for
// easier wire-log reading.
int g_next_seq = 1;

// Program path arrived via argv[2]. The `launch` request handler can
// override this with its own `program` argument, so callers using
// VS Code's `launch.json` aren't forced to also pass the file on the
// command line.
std::string g_program_path;
// Set by `launch` once an AOT build succeeds. PR 4c (gdb spawn) reads
// it to feed `gdb --interpreter=mi3 ./<built_binary>`.
std::string g_built_binary;
// True once a `launch` request has run AOT-build to completion. The
// gdb subprocess only starts inside `configurationDone`, so handlers
// that come before that point check this flag to refuse early calls.
bool g_launched = false;

// stdout is shared between the main request loop (which writes DAP
// responses synchronously) and the gdb reader thread (which writes
// DAP events asynchronously as the inferior runs). Every framed
// write_message call MUST hold this mutex or the two streams race
// and produce torn `Content-Length: N\r\n\r\n<json>` frames.
std::mutex g_stdout_mu;

// stdin / stdout must be binary on Windows or the runtime would
// translate \r\n line endings between our raw bytes and the DAP
// framing's expected exact bytes. LSP code path does the same dance.
void make_streams_binary() {
#ifdef _WIN32
  _setmode(_fileno(stdin), _O_BINARY);
  _setmode(_fileno(stdout), _O_BINARY);
#endif
}

// Read one DAP message (Content-Length: N\r\n\r\n + N bytes of JSON).
// Returns true on success, false on EOF / malformed framing — the
// main loop treats false as a clean disconnect.
bool read_message(std::string &out_body) {
  // Header parse loop: consume lines until we hit the empty separator
  // line. Only `Content-Length` is meaningful to us; everything else
  // (e.g. `Content-Type`, which the spec says SHOULD be
  // "application/vscode-jsonrpc; charset=utf-8") is read + ignored.
  long content_length = -1;
  std::string line;
  for (;;) {
    int c = std::fgetc(stdin);
    if (c == EOF) return false;
    if (c == '\r') {
      // Expect \n right after — and if the line is empty we're
      // at the header/body separator, so break out of the parse.
      int n = std::fgetc(stdin);
      if (n != '\n') return false;
      if (line.empty()) break;
      // Parse this header line.
      auto colon = line.find(':');
      if (colon != std::string::npos) {
        std::string key = line.substr(0, colon);
        std::string val = line.substr(colon + 1);
        // Strip the spaces that follow the colon.
        while (!val.empty() && (val[0] == ' ' || val[0] == '\t')) {
          val.erase(0, 1);
        }
        if (key == "Content-Length") {
          content_length = std::strtol(val.c_str(), nullptr, 10);
        }
      }
      line.clear();
      continue;
    }
    line.push_back(static_cast<char>(c));
  }
  if (content_length < 0) return false;
  if (content_length > 16 * 1024 * 1024) return false;  // 16 MiB cap

  out_body.assign(static_cast<size_t>(content_length), '\0');
  size_t got = std::fread(&out_body[0], 1, out_body.size(), stdin);
  return got == out_body.size();
}

// Write one DAP message. Caller owns + frees the cJSON object. Takes
// `g_stdout_mu` so the gdb reader thread can write events
// concurrently with the main loop's responses without tearing the
// `Content-Length: N\r\n\r\n<json>` framing.
void write_message(cJSON *msg) {
  char *json = cJSON_PrintUnformatted(msg);
  if (!json) return;
  size_t len = std::strlen(json);
  std::lock_guard<std::mutex> lk(g_stdout_mu);
  std::fprintf(stdout, "Content-Length: %zu\r\n\r\n", len);
  std::fwrite(json, 1, len, stdout);
  std::fflush(stdout);
  std::free(json);
}

// Build a DAP response envelope:
//   { "seq": N, "type": "response", "request_seq": ..., "success": ...,
//     "command": ..., "message": ..., "body": ... }
// Caller adds an optional `body` object before passing to write_message.
cJSON *make_response(cJSON *request, bool success, const char *message_or_null) {
  cJSON *resp = cJSON_CreateObject();
  cJSON_AddNumberToObject(resp, "seq", g_next_seq++);
  cJSON_AddStringToObject(resp, "type", "response");
  cJSON *req_seq = cJSON_GetObjectItem(request, "seq");
  cJSON_AddNumberToObject(resp, "request_seq",
                          cJSON_IsNumber(req_seq) ? req_seq->valuedouble : 0);
  cJSON_AddBoolToObject(resp, "success", success);
  cJSON *cmd = cJSON_GetObjectItem(request, "command");
  cJSON_AddStringToObject(resp, "command",
                          (cJSON_IsString(cmd) && cmd->valuestring)
                              ? cmd->valuestring
                              : "");
  if (message_or_null) {
    cJSON_AddStringToObject(resp, "message", message_or_null);
  }
  return resp;
}

// Build a DAP event envelope:
//   { "seq": N, "type": "event", "event": ..., "body": ... }
cJSON *make_event(const char *event_name) {
  cJSON *evt = cJSON_CreateObject();
  cJSON_AddNumberToObject(evt, "seq", g_next_seq++);
  cJSON_AddStringToObject(evt, "type", "event");
  cJSON_AddStringToObject(evt, "event", event_name);
  return evt;
}

// Send the `initialized` event right after we successfully reply to
// `initialize`. The DAP spec is explicit that the event MUST come
// after the response (clients race on it), so the caller is expected
// to write the response first.
void send_initialized_event() {
  cJSON *evt = make_event("initialized");
  cJSON_AddItemToObject(evt, "body", cJSON_CreateObject());
  write_message(evt);
  cJSON_Delete(evt);
}

// ---------------------------------------------------------------------------
// gdb/MI subprocess plumbing.
//
// We talk to gdb in MI3 mode over an anonymous bidirectional pipe.
// Every request gets a numeric token prefix (`123-break-insert ...`)
// so the reader thread can match the `123^done` / `123^error` result
// record back to the caller. Result records arrive interleaved with
// async records (`*stopped`, `=thread-exited`, ...) and stream
// records (`~"..."` console, `@"..."` target, `&"..."` log) — the
// reader keeps them apart and routes async records to DAP events.

// Unescape a gdb MI C-string literal in place. MI uses the C escape
// alphabet: \n \t \r \\ \" \a \b \f \v \xHH \0NN. We handle the
// common ones; anything we don't recognise is left literal so the
// user still sees something useful in the DEBUG CONSOLE.
std::string mi_unescape(const std::string &s) {
  std::string out;
  out.reserve(s.size());
  for (size_t i = 0; i < s.size(); ++i) {
    char c = s[i];
    if (c != '\\' || i + 1 >= s.size()) { out.push_back(c); continue; }
    char next = s[++i];
    switch (next) {
      case 'n': out.push_back('\n'); break;
      case 't': out.push_back('\t'); break;
      case 'r': out.push_back('\r'); break;
      case '\\': out.push_back('\\'); break;
      case '"': out.push_back('"'); break;
      case 'a': out.push_back('\a'); break;
      case 'b': out.push_back('\b'); break;
      case 'f': out.push_back('\f'); break;
      case 'v': out.push_back('\v'); break;
      case '0': out.push_back('\0'); break;
      default: out.push_back('\\'); out.push_back(next); break;
    }
  }
  return out;
}

// Extract the value of a `key="..."` pair from a single MI record
// line. Returns "" if the key isn't present or the value isn't a
// quoted string. Stops at the matching unescaped `"`. Sufficient for
// the fields we actually parse (`reason`, `bkpt.line`, `bkpt.file`,
// `exit-code`, etc.) — the records we care about all use the simple
// `key="value"` form, never nested tuples.
std::string mi_field(const std::string &record, const std::string &key) {
  std::string needle = key + "=\"";
  size_t pos = record.find(needle);
  if (pos == std::string::npos) return "";
  pos += needle.size();
  std::string out;
  while (pos < record.size()) {
    char c = record[pos++];
    if (c == '\\' && pos < record.size()) {
      out.push_back(c);
      out.push_back(record[pos++]);
      continue;
    }
    if (c == '"') break;
    out.push_back(c);
  }
  return mi_unescape(out);
}

// Split a MI list (`<key>=[...]`) into its top-level elements. The
// elements come back with surrounding whitespace stripped but the
// `key=` prefix preserved if present, so e.g.
//
//   stack=[frame={level="0",file="x.tpr"},frame={level="1"}]
//
// yields two strings: `frame={level="0",file="x.tpr"}` and
// `frame={level="1"}`. mi_field() works directly on either.
//
// Tracks `{`/`[` nesting and skips commas inside quoted strings so a
// value like `value="a,b"` doesn't split the parent tuple. Stops at
// the matching `]` closing the outer list. Returns an empty vector
// when the list key isn't present.
std::vector<std::string> mi_split_list(const std::string &record,
                                        const std::string &list_key) {
  std::vector<std::string> out;
  std::string needle = list_key + "=[";
  size_t start = record.find(needle);
  if (start == std::string::npos) return out;
  start += needle.size();
  int depth = 0;
  bool in_str = false;
  bool esc = false;
  size_t elem_start = start;
  for (size_t i = start; i < record.size(); ++i) {
    char c = record[i];
    if (in_str) {
      if (esc) { esc = false; continue; }
      if (c == '\\') { esc = true; continue; }
      if (c == '"') in_str = false;
      continue;
    }
    if (c == '"') { in_str = true; continue; }
    if (c == '{' || c == '[') { depth++; continue; }
    if (c == '}') { if (depth > 0) depth--; continue; }
    if (c == ']') {
      if (depth == 0) {
        if (i > elem_start) {
          std::string e = record.substr(elem_start, i - elem_start);
          // Strip leading/trailing whitespace + commas.
          size_t s = 0;
          while (s < e.size() && (e[s] == ' ' || e[s] == '\t' || e[s] == ',')) s++;
          size_t en = e.size();
          while (en > s && (e[en - 1] == ' ' || e[en - 1] == '\t')) en--;
          if (en > s) out.push_back(e.substr(s, en - s));
        }
        return out;
      }
      depth--;
      continue;
    }
    if (c == ',' && depth == 0) {
      std::string e = record.substr(elem_start, i - elem_start);
      size_t s = 0;
      while (s < e.size() && (e[s] == ' ' || e[s] == '\t')) s++;
      size_t en = e.size();
      while (en > s && (e[en - 1] == ' ' || e[en - 1] == '\t')) en--;
      if (en > s) out.push_back(e.substr(s, en - s));
      elem_start = i + 1;
    }
  }
  return out;
}

class GdbProcess {
 public:
  GdbProcess() = default;
  ~GdbProcess() { stop(); }

  // Spawn `gdb --interpreter=mi3 -nx <binary>` with bidirectional
  // pipes. Returns false (and leaves the object in an unstarted
  // state) if gdb couldn't be spawned — the caller surfaces that as
  // `success: false` on the DAP request that triggered the spawn.
  bool start(const std::string &binary);

  // Send one MI command. `prefix_token` is true for requests we want
  // to correlate (setBreakpoints, exec-run); false for fire-and-
  // forget cleanup commands (-gdb-exit). Returns the token assigned
  // (0 when prefix_token=false), so the caller can later pull the
  // matching `^done`/`^error` record via wait_for_result.
  int send_command(const std::string &cmd, bool prefix_token);

  // Block until the result record for `token` arrives, or until
  // `timeout_ms` elapses. Returns the whole record line minus the
  // token prefix (e.g. `^done,bkpt={...}`); empty string on timeout.
  std::string wait_for_result(int token, int timeout_ms);

  // Tear down the subprocess: send `-gdb-exit`, close pipes, join
  // the reader thread, reap. Safe to call even if start() failed or
  // stop() was already invoked.
  void stop();

  bool running() const { return m_running.load(); }

 private:
  void reader_loop();
  void dispatch_async_record(const std::string &kind,
                             const std::string &record);
  void emit_stopped(const std::string &reason);
  void emit_terminated();
  void emit_output(const std::string &category,
                   const std::string &content);

#ifdef _WIN32
  HANDLE m_proc = nullptr;
  HANDLE m_in_w = nullptr;  // parent → gdb stdin
  HANDLE m_out_r = nullptr; // gdb stdout → parent
#else
  pid_t m_pid = -1;
  int m_in_w = -1;
  int m_out_r = -1;
#endif

  std::atomic<bool> m_running{false};
  std::atomic<int> m_next_token{1};
  std::thread m_reader;

  // Mutex protects m_results + m_results_cv. Reader fills the map
  // when it sees a tokened `^done` / `^error`; wait_for_result drains.
  std::mutex m_results_mu;
  std::condition_variable m_results_cv;
  std::unordered_map<int, std::string> m_results;

  // Send-side mutex so concurrent writers (today: only the main
  // thread, but cheap insurance against future stepping handlers)
  // can't tear MI commands.
  std::mutex m_send_mu;
};

// Global gdb instance. Initialised by configurationDone, torn down
// by terminate/disconnect or by the main loop on EOF.
GdbProcess g_gdb;

bool GdbProcess::start(const std::string &binary) {
  if (m_running.load()) {
    std::fprintf(stderr, "[dap] gdb: start() called while already running\n");
    return false;
  }

#ifdef _WIN32
  SECURITY_ATTRIBUTES sa = {};
  sa.nLength = sizeof(sa);
  sa.bInheritHandle = TRUE;
  sa.lpSecurityDescriptor = nullptr;

  HANDLE in_r = nullptr, in_w = nullptr, out_r = nullptr, out_w = nullptr;
  if (!CreatePipe(&in_r, &in_w, &sa, 0)) {
    std::fprintf(stderr, "[dap] gdb: CreatePipe(stdin) failed\n");
    return false;
  }
  if (!SetHandleInformation(in_w, HANDLE_FLAG_INHERIT, 0)) {
    CloseHandle(in_r); CloseHandle(in_w);
    return false;
  }
  if (!CreatePipe(&out_r, &out_w, &sa, 0)) {
    CloseHandle(in_r); CloseHandle(in_w);
    return false;
  }
  if (!SetHandleInformation(out_r, HANDLE_FLAG_INHERIT, 0)) {
    CloseHandle(in_r); CloseHandle(in_w);
    CloseHandle(out_r); CloseHandle(out_w);
    return false;
  }

  STARTUPINFOW si = {};
  si.cb = sizeof(si);
  si.dwFlags = STARTF_USESTDHANDLES;
  si.hStdInput = in_r;
  si.hStdOutput = out_w;
  si.hStdError = out_w;  // fold gdb stderr into the same pipe
  PROCESS_INFORMATION pi = {};

  // Build a UTF-16 command line. Quote the binary path so spaces in
  // the AOT output directory don't break argv splitting.
  std::wstring cmd = L"gdb.exe --interpreter=mi3 -nx -q \"";
  for (char c : binary) {
    cmd.push_back(static_cast<wchar_t>(static_cast<unsigned char>(c)));
  }
  cmd.push_back(L'"');

  BOOL ok = CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, TRUE,
                           CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);
  // Always close the child-side handles in the parent — keeping them
  // open would prevent the pipes from ever signalling EOF.
  CloseHandle(in_r);
  CloseHandle(out_w);
  if (!ok) {
    std::fprintf(stderr, "[dap] gdb: CreateProcess failed (err=%lu)\n",
                 GetLastError());
    CloseHandle(in_w);
    CloseHandle(out_r);
    return false;
  }
  CloseHandle(pi.hThread);
  m_proc = pi.hProcess;
  m_in_w = in_w;
  m_out_r = out_r;
#else
  int in_pipe[2] = {-1, -1};
  int out_pipe[2] = {-1, -1};
  if (pipe(in_pipe) != 0) return false;
  if (pipe(out_pipe) != 0) {
    close(in_pipe[0]); close(in_pipe[1]);
    return false;
  }
  pid_t pid = fork();
  if (pid < 0) {
    close(in_pipe[0]); close(in_pipe[1]);
    close(out_pipe[0]); close(out_pipe[1]);
    return false;
  }
  if (pid == 0) {
    dup2(in_pipe[0], 0);
    dup2(out_pipe[1], 1);
    dup2(out_pipe[1], 2);  // fold gdb stderr → same pipe
    close(in_pipe[0]); close(in_pipe[1]);
    close(out_pipe[0]); close(out_pipe[1]);
    execlp("gdb", "gdb", "--interpreter=mi3", "-nx", "-q",
           binary.c_str(), nullptr);
    _exit(127);
  }
  close(in_pipe[0]);
  close(out_pipe[1]);
  m_pid = pid;
  m_in_w = in_pipe[1];
  m_out_r = out_pipe[0];
#endif

  m_running.store(true);
  m_reader = std::thread(&GdbProcess::reader_loop, this);
  return true;
}

int GdbProcess::send_command(const std::string &cmd, bool prefix_token) {
  if (!m_running.load()) return 0;
  std::lock_guard<std::mutex> lk(m_send_mu);
  int token = 0;
  std::string line;
  if (prefix_token) {
    token = m_next_token.fetch_add(1);
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%d", token);
    line.append(buf);
  }
  line.append(cmd);
  line.push_back('\n');
#ifdef _WIN32
  DWORD wrote = 0;
  WriteFile(m_in_w, line.data(), static_cast<DWORD>(line.size()), &wrote,
            nullptr);
#else
  ssize_t wrote = ::write(m_in_w, line.data(), line.size());
  (void)wrote;
#endif
  return token;
}

std::string GdbProcess::wait_for_result(int token, int timeout_ms) {
  std::unique_lock<std::mutex> lk(m_results_mu);
  if (!m_results_cv.wait_for(lk, std::chrono::milliseconds(timeout_ms),
                             [&]() { return m_results.count(token) > 0; })) {
    return "";
  }
  std::string out = std::move(m_results[token]);
  m_results.erase(token);
  return out;
}

void GdbProcess::stop() {
  if (!m_running.load()) {
    // start() failed or stop() already ran. Still clean up any half-
    // initialised handles in case start() bailed mid-way.
#ifdef _WIN32
    if (m_in_w)  { CloseHandle(m_in_w);  m_in_w = nullptr; }
    if (m_out_r) { CloseHandle(m_out_r); m_out_r = nullptr; }
    if (m_proc)  { CloseHandle(m_proc);  m_proc = nullptr; }
#endif
    return;
  }

  // Best-effort polite shutdown: -gdb-exit asks gdb to detach +
  // exit, which closes our output pipe and trips the reader's EOF.
  send_command("-gdb-exit", false);

#ifdef _WIN32
  // Closing the write end lets gdb see EOF on its stdin and bail out
  // if it ignored -gdb-exit for any reason.
  if (m_in_w) { CloseHandle(m_in_w); m_in_w = nullptr; }
  // Give gdb a second to exit gracefully; if it hasn't, kill it.
  // 1000 ms matches the LSP/DAP "no hang on shutdown" convention.
  DWORD wait = WaitForSingleObject(m_proc, 1000);
  if (wait != WAIT_OBJECT_0) {
    TerminateProcess(m_proc, 1);
    WaitForSingleObject(m_proc, INFINITE);
  }
  CloseHandle(m_proc); m_proc = nullptr;
  if (m_out_r) { CloseHandle(m_out_r); m_out_r = nullptr; }
#else
  if (m_in_w >= 0) { ::close(m_in_w); m_in_w = -1; }
  // Reader's read() returns 0 once gdb closes its stdout; we still
  // waitpid here to reap the zombie.
  int status = 0;
  for (int i = 0; i < 20; ++i) {
    pid_t r = waitpid(m_pid, &status, WNOHANG);
    if (r == m_pid) break;
    if (r < 0) break;
    struct timespec ts = {0, 50 * 1000 * 1000};  // 50ms
    nanosleep(&ts, nullptr);
  }
  if (waitpid(m_pid, &status, WNOHANG) == 0) {
    kill(m_pid, SIGKILL);
    waitpid(m_pid, &status, 0);
  }
  if (m_out_r >= 0) { ::close(m_out_r); m_out_r = -1; }
  m_pid = -1;
#endif
  m_running.store(false);
  if (m_reader.joinable()) m_reader.join();

  // Wake any waiters left over so they see m_running=false instead
  // of blocking forever on a result that will never arrive.
  std::lock_guard<std::mutex> lk(m_results_mu);
  m_results.clear();
  m_results_cv.notify_all();
}

void GdbProcess::reader_loop() {
  std::string line;
  for (;;) {
    char buf[4096];
#ifdef _WIN32
    DWORD got = 0;
    BOOL ok = ReadFile(m_out_r, buf, sizeof(buf), &got, nullptr);
    if (!ok || got == 0) break;
#else
    ssize_t got = ::read(m_out_r, buf, sizeof(buf));
    if (got <= 0) break;
#endif
    for (size_t i = 0; i < static_cast<size_t>(got); ++i) {
      char c = buf[i];
      if (c == '\r') continue;
      if (c == '\n') {
        if (!line.empty()) {
          // Trim trailing whitespace just in case.
          while (!line.empty() && (line.back() == ' ' || line.back() == '\t')) {
            line.pop_back();
          }
          std::fprintf(stderr, "[dap] gdb<< %s\n", line.c_str());

          // Parse the line. Possible shapes (MI3):
          //   ^done,...          result record (no token)
          //   123^done,...       result record (tokened)
          //   *stopped,...       async exec record
          //   =thread-exited,... async notify record
          //   ~"..."             console-stream
          //   @"..."             target-stream
          //   &"..."             log-stream
          //   (gdb)              prompt — ignore
          size_t p = 0;
          int token = 0;
          while (p < line.size() && std::isdigit(static_cast<unsigned char>(line[p]))) {
            token = token * 10 + (line[p] - '0');
            ++p;
          }
          if (p < line.size()) {
            char prefix = line[p];
            std::string body = line.substr(p);
            if (prefix == '^') {
              // Result record. If a token preceded it, route to the
              // waiting send_and_wait via the result map.
              if (token > 0) {
                {
                  std::lock_guard<std::mutex> lk(m_results_mu);
                  m_results[token] = body;
                }
                m_results_cv.notify_all();
              }
            } else if (prefix == '*') {
              dispatch_async_record("exec", body);
            } else if (prefix == '=') {
              dispatch_async_record("notify", body);
            } else if (prefix == '~') {
              // Console stream: gdb's own chatter. Show it under
              // category=console so the client puts it in the
              // DEBUG CONSOLE without mixing with program stdout.
              size_t q1 = body.find('"');
              size_t q2 = body.rfind('"');
              if (q1 != std::string::npos && q2 != std::string::npos && q2 > q1) {
                emit_output("console", mi_unescape(body.substr(q1 + 1, q2 - q1 - 1)));
              }
            } else if (prefix == '@') {
              // Target stream: the inferior's stdout.
              size_t q1 = body.find('"');
              size_t q2 = body.rfind('"');
              if (q1 != std::string::npos && q2 != std::string::npos && q2 > q1) {
                emit_output("stdout", mi_unescape(body.substr(q1 + 1, q2 - q1 - 1)));
              }
            }
            // log-stream `&"..."` and `(gdb)` prompt fall through —
            // logged to stderr above, nothing more to do.
          }
        }
        line.clear();
        continue;
      }
      line.push_back(c);
    }
  }
  m_running.store(false);
  // Wake any waiter on a never-coming result.
  std::lock_guard<std::mutex> lk(m_results_mu);
  m_results_cv.notify_all();
}

void GdbProcess::dispatch_async_record(const std::string &kind,
                                       const std::string &record) {
  if (kind == "exec") {
    // record starts with e.g. "stopped,reason=\"exited-normally\",..."
    if (record.compare(0, std::strlen("stopped"), "stopped") == 0) {
      std::string reason = mi_field(record, "reason");
      // Disambiguate a user-initiated pause (`-exec-interrupt` →
      // SIGINT) from a real exception (segfault → SIGSEGV, etc.) so
      // DAP `reason` lands on `pause` vs `exception`. emit_stopped
      // can't tell from `reason` alone, so we peek at signal-name
      // here and swap to a synthetic marker before forwarding.
      if (reason == "signal-received") {
        std::string sig = mi_field(record, "signal-name");
        if (sig == "SIGINT" || sig == "0") reason = "_pause";
      }
      emit_stopped(reason);
      // exited-* reasons mean the inferior is gone — also emit
      // `terminated` so the client tears down the session.
      if (reason == "exited-normally" || reason == "exited" ||
          reason == "exited-signalled") {
        emit_terminated();
      }
    }
    // *running is ignored — DAP `continued` event is only required
    // when WE initiated the resume; for `-exec-run` the client
    // already knows we're running.
  }
  // We deliberately don't translate every `=` notify record — only
  // the bits that surface to the user. =thread-group-exited would be
  // a candidate but `*stopped,reason=exited-normally` covers it.
}

void GdbProcess::emit_stopped(const std::string &mi_reason) {
  // Map gdb/MI reasons to DAP reasons. The DAP spec's `reason`
  // enum is small (step, breakpoint, exception, pause, entry,
  // goto, function breakpoint, data breakpoint, instruction
  // breakpoint, exit); anything we can't classify cleanly we send
  // as the literal MI string and let the client render it.
  std::string dap_reason = mi_reason;
  if (mi_reason == "breakpoint-hit") dap_reason = "breakpoint";
  else if (mi_reason == "end-stepping-range" ||
           mi_reason == "function-finished") dap_reason = "step";
  else if (mi_reason == "_pause") dap_reason = "pause";
  else if (mi_reason == "signal-received") dap_reason = "exception";
  else if (mi_reason == "exited-normally" || mi_reason == "exited" ||
           mi_reason == "exited-signalled") dap_reason = "exit";

  cJSON *evt = make_event("stopped");
  cJSON *body = cJSON_CreateObject();
  cJSON_AddStringToObject(body, "reason", dap_reason.c_str());
  cJSON_AddNumberToObject(body, "threadId", 1);
  cJSON_AddBoolToObject(body, "allThreadsStopped", true);
  cJSON_AddItemToObject(evt, "body", body);
  write_message(evt);
  cJSON_Delete(evt);
}

void GdbProcess::emit_terminated() {
  cJSON *evt = make_event("terminated");
  cJSON_AddItemToObject(evt, "body", cJSON_CreateObject());
  write_message(evt);
  cJSON_Delete(evt);
}

void GdbProcess::emit_output(const std::string &category,
                              const std::string &content) {
  cJSON *evt = make_event("output");
  cJSON *body = cJSON_CreateObject();
  cJSON_AddStringToObject(body, "category", category.c_str());
  cJSON_AddStringToObject(body, "output", content.c_str());
  cJSON_AddItemToObject(evt, "body", body);
  write_message(evt);
  cJSON_Delete(evt);
}

// Handle the `initialize` request. Advertises what this server CAN
// do today — which is almost nothing besides keeping the channel
// alive — and emits the `initialized` event the client waits on
// before sending `setBreakpoints` / `configurationDone`.
//
// Every capability we advertise as `false` is something a later PR
// will flip to `true` once the gdb-backed implementation lands.
void handle_initialize(cJSON *request) {
  cJSON *resp = make_response(request, /*success=*/true, nullptr);
  cJSON *body = cJSON_CreateObject();

  cJSON_AddBoolToObject(body, "supportsConfigurationDoneRequest", true);
  cJSON_AddBoolToObject(body, "supportsFunctionBreakpoints", false);
  cJSON_AddBoolToObject(body, "supportsConditionalBreakpoints", false);
  cJSON_AddBoolToObject(body, "supportsHitConditionalBreakpoints", false);
  cJSON_AddBoolToObject(body, "supportsEvaluateForHovers", false);
  cJSON_AddBoolToObject(body, "supportsStepBack", false);
  cJSON_AddBoolToObject(body, "supportsSetVariable", false);
  cJSON_AddBoolToObject(body, "supportsRestartFrame", false);
  cJSON_AddBoolToObject(body, "supportsGotoTargetsRequest", false);
  cJSON_AddBoolToObject(body, "supportsStepInTargetsRequest", false);
  cJSON_AddBoolToObject(body, "supportsCompletionsRequest", false);
  cJSON_AddBoolToObject(body, "supportsModulesRequest", false);
  cJSON_AddBoolToObject(body, "supportsRestartRequest", false);
  cJSON_AddBoolToObject(body, "supportsExceptionOptions", false);
  cJSON_AddBoolToObject(body, "supportsValueFormattingOptions", false);
  cJSON_AddBoolToObject(body, "supportsExceptionInfoRequest", false);
  cJSON_AddBoolToObject(body, "supportTerminateDebuggee", true);
  cJSON_AddBoolToObject(body, "supportsDelayedStackTraceLoading", false);
  cJSON_AddBoolToObject(body, "supportsLoadedSourcesRequest", false);
  cJSON_AddBoolToObject(body, "supportsLogPoints", false);
  cJSON_AddBoolToObject(body, "supportsTerminateThreadsRequest", false);
  cJSON_AddBoolToObject(body, "supportsSetExpression", false);
  cJSON_AddBoolToObject(body, "supportsTerminateRequest", true);
  cJSON_AddBoolToObject(body, "supportsDataBreakpoints", false);
  cJSON_AddBoolToObject(body, "supportsReadMemoryRequest", false);
  cJSON_AddBoolToObject(body, "supportsWriteMemoryRequest", false);
  cJSON_AddBoolToObject(body, "supportsDisassembleRequest", false);
  cJSON_AddBoolToObject(body, "supportsCancelRequest", false);
  cJSON_AddBoolToObject(body, "supportsBreakpointLocationsRequest", false);
  cJSON_AddBoolToObject(body, "supportsClipboardContext", false);
  cJSON_AddBoolToObject(body, "supportsSteppingGranularity", false);
  cJSON_AddBoolToObject(body, "supportsInstructionBreakpoints", false);
  cJSON_AddBoolToObject(body, "supportsExceptionFilterOptions", false);

  cJSON_AddItemToObject(resp, "body", body);
  write_message(resp);
  cJSON_Delete(resp);

  send_initialized_event();
}

// Strip the `.tpr` extension off a source path and append the
// platform's executable suffix. `aot_compile_with_filename_debug`
// derives its output basename the same way `tulpar build` does;
// matching that here lets the (eventual) gdb spawn invocation use a
// predictable path.
std::string derive_output_name(const std::string &src) {
  std::string out = src;
  // Trim trailing `.tpr` so the basename matches what the AOT
  // pipeline emits. Other extensions are left alone — the user
  // shouldn't be `launch`ing a non-.tpr file, but we don't want to
  // silently corrupt arbitrary paths if they do.
  if (out.size() > 4 &&
      out.compare(out.size() - 4, 4, ".tpr") == 0) {
    out.resize(out.size() - 4);
  }
  return out;
}

// Slurp a whole text file into a heap-allocated buffer. Caller is
// responsible for `free()`. Returns nullptr when the file can't be
// opened — the caller then surfaces a structured "failed to launch"
// response to the DAP client.
char *slurp_file(const char *path) {
  FILE *f = std::fopen(path, "rb");
  if (!f) return nullptr;
  std::fseek(f, 0, SEEK_END);
  long size = std::ftell(f);
  std::fseek(f, 0, SEEK_SET);
  if (size < 0) { std::fclose(f); return nullptr; }
  char *buf = static_cast<char *>(std::malloc(static_cast<size_t>(size) + 1));
  if (!buf) { std::fclose(f); return nullptr; }
  size_t got = std::fread(buf, 1, static_cast<size_t>(size), f);
  buf[got] = '\0';
  std::fclose(f);
  return buf;
}

// `launch` is the first request that actually does work. DAP semantics:
//   1. Read `program` (and optionally `stopOnEntry`, `args`, etc.)
//     from `arguments`.
//   2. Get the debuggee ready to run, but don't start it yet — that
//     happens after `configurationDone` so the client has a chance to
//     set breakpoints first.
//   3. Reply success once the debuggee is "loaded".
//
// PR 4b scope: do the AOT build with debug info so PR 4c has a binary
// to feed gdb. Source-file read failures, AOT codegen errors, and
// link failures all surface as `success: false` with the AOT pipeline
// result code in the message — the DAP client renders that in its
// "DEBUG CONSOLE" pane verbatim. gdb spawn / `-exec-run` /
// `stopped` event come in PR 4c.
void handle_launch(cJSON *request) {
  cJSON *args = cJSON_GetObjectItem(request, "arguments");

  // Honour `arguments.program` when the client provided one (VS Code
  // `launch.json` does). Fall back to the path we got off argv[2] at
  // startup so a hand-rolled DAP session with no launch.json still
  // works.
  std::string program = g_program_path;
  if (cJSON_IsObject(args)) {
    cJSON *p = cJSON_GetObjectItem(args, "program");
    if (cJSON_IsString(p) && p->valuestring && *p->valuestring) {
      program = p->valuestring;
    }
  }

  if (program.empty()) {
    cJSON *resp = make_response(request, /*success=*/false,
                                "launch: no `program` provided");
    cJSON_AddItemToObject(resp, "body", cJSON_CreateObject());
    write_message(resp);
    cJSON_Delete(resp);
    return;
  }

  std::fprintf(stderr, "[dap] launch: building %s with debug info...\n",
               program.c_str());

  char *source = slurp_file(program.c_str());
  if (!source) {
    std::string msg = "launch: cannot read source file '" + program + "'";
    cJSON *resp = make_response(request, /*success=*/false, msg.c_str());
    cJSON_AddItemToObject(resp, "body", cJSON_CreateObject());
    write_message(resp);
    cJSON_Delete(resp);
    return;
  }

  std::string output = derive_output_name(program);

  // AOT pipeline writes its `[AOT] ...` progress lines to stdout via
  // raw printf. That's harmless in normal `tulpar build` invocations
  // but here stdout IS the DAP wire — any byte we leak there breaks
  // the `Content-Length: N\r\n\r\n<json>` framing and the client
  // hangs trying to parse the next response. Redirect stdout to
  // stderr for the duration of the build so those progress lines end
  // up in the same channel the rest of our diagnostics use, then
  // restore stdout so write_message can resume sending DAP messages.
  std::fflush(stdout);
  int saved_stdout = DUP(FILENO(stdout));
  DUP2(FILENO(stderr), FILENO(stdout));

  AOTResult rc = aot_compile_with_filename_debug(source, output.c_str(),
                                                 program.c_str(),
                                                 /*emit_debug_info=*/1);
  std::fflush(stdout);
  DUP2(saved_stdout, FILENO(stdout));
  CLOSE(saved_stdout);
  std::free(source);

  if (rc != AOT_OK) {
    // The pipeline already streamed its own diagnostic output to
    // stderr; we just hand a short human-readable summary back to
    // the client so the DEBUG CONSOLE shows something useful.
    char msg[128];
    std::snprintf(msg, sizeof(msg),
                  "launch: AOT build failed (result=%d)", static_cast<int>(rc));
    cJSON *resp = make_response(request, /*success=*/false, msg);
    cJSON_AddItemToObject(resp, "body", cJSON_CreateObject());
    write_message(resp);
    cJSON_Delete(resp);
    return;
  }

  // AOT emits the executable next to the source basename. On Windows
  // it gets a `.exe` suffix; gdb on Windows accepts either path,
  // but we feed the platform-suffixed name so the gdb command-line
  // looks right in the [dap] log.
  std::string binary_for_gdb = output;
#ifdef _WIN32
  // Only append `.exe` if the AOT pipeline didn't already produce
  // that path itself — Tulpar's Windows AOT emits `<base>.exe`.
  if (binary_for_gdb.size() < 4 ||
      binary_for_gdb.compare(binary_for_gdb.size() - 4, 4, ".exe") != 0) {
    binary_for_gdb += ".exe";
  }
#endif
  g_built_binary = binary_for_gdb;
  std::fprintf(stderr, "[dap] launch: build OK, binary=%s\n",
               binary_for_gdb.c_str());

  // Spawn gdb in MI3 mode right now so `setBreakpoints` (which
  // arrives between `launch` and `configurationDone` per DAP) has
  // a live subprocess to forward `-break-insert` to. The reader
  // thread starts immediately and absorbs gdb's startup banner.
  if (!g_gdb.start(binary_for_gdb)) {
    cJSON *resp = make_response(request, /*success=*/false,
                                "launch: failed to spawn gdb subprocess "
                                "(is gdb installed and on PATH?)");
    cJSON_AddItemToObject(resp, "body", cJSON_CreateObject());
    write_message(resp);
    cJSON_Delete(resp);
    return;
  }

  g_launched = true;

  cJSON *resp = make_response(request, /*success=*/true, nullptr);
  cJSON_AddItemToObject(resp, "body", cJSON_CreateObject());
  write_message(resp);
  cJSON_Delete(resp);
}

// `setBreakpoints` arrives after the client receives `initialized`
// and before `configurationDone`. DAP semantics: the request REPLACES
// the entire breakpoint set for the named source — so the right
// thing is to clear gdb's existing breakpoints and re-insert. For
// PR 4c we accept the over-approximation that no breakpoints existed
// yet (every smoke run starts fresh; the same is true for a typical
// VS Code session because each F5 restarts the adapter). A later PR
// will track per-source breakpoint IDs and `-break-delete` stale ones.
//
// Per breakpoint we send:
//   <token>-break-insert -f <path>:<line>
// `-f` makes gdb defer insertion if the source isn't loaded yet
// (which it won't be — `-exec-run` hasn't fired). The `^done` reply
// carries `bkpt={...,line="N",func="...",pending="..."}` which we
// parse into a verified Breakpoint{} for the response body.
void handle_set_breakpoints(cJSON *request) {
  cJSON *args = cJSON_GetObjectItem(request, "arguments");
  cJSON *source = cJSON_IsObject(args)
                       ? cJSON_GetObjectItem(args, "source") : nullptr;
  cJSON *path = cJSON_IsObject(source)
                     ? cJSON_GetObjectItem(source, "path") : nullptr;
  cJSON *bkpts = cJSON_IsObject(args)
                      ? cJSON_GetObjectItem(args, "breakpoints") : nullptr;

  if (!cJSON_IsString(path) || !path->valuestring || !*path->valuestring) {
    cJSON *resp = make_response(request, /*success=*/false,
                                "setBreakpoints: missing source.path");
    cJSON_AddItemToObject(resp, "body", cJSON_CreateObject());
    write_message(resp);
    cJSON_Delete(resp);
    return;
  }
  if (!g_gdb.running()) {
    cJSON *resp = make_response(request, /*success=*/false,
                                "setBreakpoints: gdb subprocess not running "
                                "(launch first)");
    cJSON_AddItemToObject(resp, "body", cJSON_CreateObject());
    write_message(resp);
    cJSON_Delete(resp);
    return;
  }

  cJSON *resp = make_response(request, /*success=*/true, nullptr);
  cJSON *body = cJSON_CreateObject();
  cJSON *verified = cJSON_CreateArray();

  if (cJSON_IsArray(bkpts)) {
    cJSON *bp = nullptr;
    cJSON_ArrayForEach(bp, bkpts) {
      cJSON *line_j = cJSON_GetObjectItem(bp, "line");
      int line = cJSON_IsNumber(line_j) ? static_cast<int>(line_j->valuedouble)
                                        : 0;
      std::string cmd = "-break-insert -f \"";
      cmd.append(path->valuestring);
      cmd.push_back(':');
      char numbuf[16];
      std::snprintf(numbuf, sizeof(numbuf), "%d", line);
      cmd.append(numbuf);
      cmd.push_back('"');

      int tok = g_gdb.send_command(cmd, /*prefix_token=*/true);
      // 2-second cap per breakpoint — gdb usually replies in <50ms
      // but we shouldn't hang the DAP wire if it stalls.
      std::string result = g_gdb.wait_for_result(tok, 2000);

      cJSON *out_bp = cJSON_CreateObject();
      bool ok = result.compare(0, 5, "^done") == 0;
      cJSON_AddBoolToObject(out_bp, "verified", ok);
      if (ok) {
        // gdb may return either `line="N"` (resolved) or `pending="..."`
        // (deferred). For deferred we still report the user's
        // requested line; the client renders an unverified marker.
        std::string resolved_line = mi_field(result, "line");
        int rl = resolved_line.empty() ? line : std::atoi(resolved_line.c_str());
        cJSON_AddNumberToObject(out_bp, "line", rl);
      } else {
        cJSON_AddNumberToObject(out_bp, "line", line);
        std::string msg = mi_field(result, "msg");
        if (msg.empty()) msg = result.empty() ? "gdb timeout" : result;
        cJSON_AddStringToObject(out_bp, "message", msg.c_str());
      }
      cJSON *src_copy = cJSON_CreateObject();
      cJSON_AddStringToObject(src_copy, "path", path->valuestring);
      cJSON_AddItemToObject(out_bp, "source", src_copy);
      cJSON_AddItemToArray(verified, out_bp);
    }
  }

  cJSON_AddItemToObject(body, "breakpoints", verified);
  cJSON_AddItemToObject(resp, "body", body);
  write_message(resp);
  cJSON_Delete(resp);
}

// `configurationDone` is the client saying "I've sent all my
// setBreakpoints / setExceptionBreakpoints / setDataBreakpoints
// requests — go run the debuggee now." We answer success first
// (the spec is strict that the response precedes any `stopped`
// event that the resulting `-exec-run` produces), THEN send the
// MI command. The reader thread is already running; it'll surface
// the inferior's `*stopped` records as DAP events.
void handle_configuration_done(cJSON *request) {
  cJSON *resp = make_response(request, /*success=*/true, nullptr);
  cJSON_AddItemToObject(resp, "body", cJSON_CreateObject());
  write_message(resp);
  cJSON_Delete(resp);

  if (!g_gdb.running()) {
    // launch never succeeded — nothing to run. The client will time
    // out on the missing `stopped` event eventually, but a literal
    // hang is worse than a silent no-op here.
    std::fprintf(stderr,
                 "[dap] configurationDone with no gdb subprocess; "
                 "skipping -exec-run\n");
    return;
  }
  // -exec-run starts the inferior. We use a separate token to make
  // the wire log easier to follow, but we don't wait_for_result —
  // gdb's `^running` is followed asynchronously by `*stopped` events
  // that the reader thread translates into DAP `stopped` / `terminated`.
  g_gdb.send_command("-exec-run", /*prefix_token=*/true);
}

// `threads` is polled by every DAP client right after the first
// `stopped` event so the variables / stack views know which thread
// to query. Tulpar AOT binaries are predominantly single-threaded
// (wings spawns workers, but those won't surface to the debugger
// until the threading runtime grows DAP integration), so we report a
// single fake thread with id=1. Clients tolerate this fine when the
// stopped event also references thread 1.
void handle_threads(cJSON *request) {
  cJSON *resp = make_response(request, /*success=*/true, nullptr);
  cJSON *body = cJSON_CreateObject();
  cJSON *threads = cJSON_CreateArray();
  cJSON *t = cJSON_CreateObject();
  cJSON_AddNumberToObject(t, "id", 1);
  cJSON_AddStringToObject(t, "name", "main");
  cJSON_AddItemToArray(threads, t);
  cJSON_AddItemToObject(body, "threads", threads);
  cJSON_AddItemToObject(resp, "body", body);
  write_message(resp);
  cJSON_Delete(resp);
}

// `terminate` is "stop the debuggee but keep the adapter alive in
// case the client wants to relaunch". VS Code follows up with
// `disconnect` after a graceful terminate, so it's fine to fully
// tear down gdb here — a re-launch in the same session would have
// to spawn a fresh subprocess anyway.
void handle_terminate(cJSON *request) {
  g_gdb.stop();
  g_launched = false;
  cJSON *resp = make_response(request, /*success=*/true, nullptr);
  cJSON_AddItemToObject(resp, "body", cJSON_CreateObject());
  write_message(resp);
  cJSON_Delete(resp);
}

// `disconnect` is "we're done, please exit". DAP allows the server
// to choose whether to terminate the debuggee — we always do,
// because leaving a detached inferior behind would orphan the
// process tree.
void handle_disconnect(cJSON *request) {
  g_gdb.stop();
  g_launched = false;
  cJSON *resp = make_response(request, /*success=*/true, nullptr);
  cJSON_AddItemToObject(resp, "body", cJSON_CreateObject());
  write_message(resp);
  cJSON_Delete(resp);
}

// ---------------------------------------------------------------------------
// Stack / scopes / variables inspection.
//
// Client flow after a `stopped` event:
//   threads      →  [{ id: 1, name: "main" }]
//   stackTrace   →  StackFrame[] from `-stack-list-frames`
//   scopes       →  one "Locals" Scope per frame; variablesReference
//                   carries the frame level + 1 so variables() can
//                   recover the gdb frame index.
//   variables    →  Variable[] from `-stack-list-variables
//                   --thread 1 --frame N --simple-values`. Nested
//                   structs are flattened to their gdb-printed value
//                   (no per-field drill-down yet).
//
// Step / continue support is the next PR; what's here is purely the
// "show me what's on the stack right now" view.

// Forward a tokened MI command and return the result-record body, or
// empty string on timeout. Used by every inspection handler.
std::string gdb_query(const std::string &cmd, int timeout_ms = 2000) {
  if (!g_gdb.running()) return "";
  int tok = g_gdb.send_command(cmd, /*prefix_token=*/true);
  return g_gdb.wait_for_result(tok, timeout_ms);
}

void handle_stack_trace(cJSON *request) {
  if (!g_gdb.running()) {
    cJSON *resp = make_response(request, /*success=*/false,
                                "stackTrace: gdb subprocess not running");
    cJSON_AddItemToObject(resp, "body", cJSON_CreateObject());
    write_message(resp);
    cJSON_Delete(resp);
    return;
  }

  std::string result = gdb_query("-stack-list-frames");
  if (result.compare(0, 5, "^done") != 0) {
    // gdb hasn't loaded a stack yet (e.g. the program already exited
    // before the client asked). Return an empty frames list with
    // success=true so the client doesn't render an error toast —
    // an empty stack is a legitimate state, not a failure.
    cJSON *resp = make_response(request, /*success=*/true, nullptr);
    cJSON *body = cJSON_CreateObject();
    cJSON_AddItemToObject(body, "stackFrames", cJSON_CreateArray());
    cJSON_AddNumberToObject(body, "totalFrames", 0);
    cJSON_AddItemToObject(resp, "body", body);
    write_message(resp);
    cJSON_Delete(resp);
    return;
  }

  cJSON *resp = make_response(request, /*success=*/true, nullptr);
  cJSON *body = cJSON_CreateObject();
  cJSON *frames = cJSON_CreateArray();

  // `^done,stack=[frame={level="0",addr="...",func="main",file="hello.tpr",
  //                fullname="/abs/hello.tpr",line="3"},frame={...}]`
  auto frame_tuples = mi_split_list(result, "stack");
  int total = 0;
  for (auto &t : frame_tuples) {
    cJSON *f = cJSON_CreateObject();
    std::string level = mi_field(t, "level");
    std::string func = mi_field(t, "func");
    std::string file = mi_field(t, "file");
    std::string fullname = mi_field(t, "fullname");
    std::string line = mi_field(t, "line");

    int id = level.empty() ? total : std::atoi(level.c_str());
    cJSON_AddNumberToObject(f, "id", id);
    cJSON_AddStringToObject(f, "name", func.empty() ? "??" : func.c_str());
    cJSON_AddNumberToObject(f, "line", line.empty() ? 0 : std::atoi(line.c_str()));
    cJSON_AddNumberToObject(f, "column", 1);

    // Prefer fullname (absolute) over file (relative or basename
    // depending on gdb's path config). VS Code happily opens either
    // but absolute paths spare it a workspace search.
    cJSON *src = cJSON_CreateObject();
    const std::string &path = fullname.empty() ? file : fullname;
    if (!path.empty()) {
      cJSON_AddStringToObject(src, "path", path.c_str());
      // `name` is the display label in the breakpoint panel; the
      // basename keeps it short.
      size_t slash = path.find_last_of("/\\");
      cJSON_AddStringToObject(src, "name",
                              (slash == std::string::npos)
                                  ? path.c_str()
                                  : path.c_str() + slash + 1);
    }
    cJSON_AddItemToObject(f, "source", src);
    cJSON_AddItemToArray(frames, f);
    total++;
  }

  cJSON_AddItemToObject(body, "stackFrames", frames);
  cJSON_AddNumberToObject(body, "totalFrames", total);
  cJSON_AddItemToObject(resp, "body", body);
  write_message(resp);
  cJSON_Delete(resp);
}

// `scopes` is just a directory of variable groups for a given frame.
// We expose one group, "Locals", whose `variablesReference` encodes
// the frame index. `variablesReference = frame_id + 1` so the value
// is always > 0 (DAP reserves 0 for "no children"). The client then
// calls `variables(ref)` to actually pull the per-variable list.
void handle_scopes(cJSON *request) {
  cJSON *args = cJSON_GetObjectItem(request, "arguments");
  int frame_id = 0;
  if (cJSON_IsObject(args)) {
    cJSON *fid = cJSON_GetObjectItem(args, "frameId");
    if (cJSON_IsNumber(fid)) frame_id = static_cast<int>(fid->valuedouble);
  }

  cJSON *resp = make_response(request, /*success=*/true, nullptr);
  cJSON *body = cJSON_CreateObject();
  cJSON *scopes = cJSON_CreateArray();

  cJSON *locals = cJSON_CreateObject();
  cJSON_AddStringToObject(locals, "name", "Locals");
  cJSON_AddStringToObject(locals, "presentationHint", "locals");
  cJSON_AddNumberToObject(locals, "variablesReference", frame_id + 1);
  cJSON_AddBoolToObject(locals, "expensive", false);
  cJSON_AddItemToArray(scopes, locals);

  cJSON_AddItemToObject(body, "scopes", scopes);
  cJSON_AddItemToObject(resp, "body", body);
  write_message(resp);
  cJSON_Delete(resp);
}

void handle_variables(cJSON *request) {
  cJSON *args = cJSON_GetObjectItem(request, "arguments");
  int vref = 0;
  if (cJSON_IsObject(args)) {
    cJSON *r = cJSON_GetObjectItem(args, "variablesReference");
    if (cJSON_IsNumber(r)) vref = static_cast<int>(r->valuedouble);
  }
  if (vref <= 0 || !g_gdb.running()) {
    cJSON *resp = make_response(request, /*success=*/true, nullptr);
    cJSON *body = cJSON_CreateObject();
    cJSON_AddItemToObject(body, "variables", cJSON_CreateArray());
    cJSON_AddItemToObject(resp, "body", body);
    write_message(resp);
    cJSON_Delete(resp);
    return;
  }
  int frame_id = vref - 1;

  // `--simple-values` (1 in MI3 numeric form) asks gdb to fold each
  // variable's `value` into the listing — we get name + value + type
  // in a single round-trip instead of N round-trips of -var-create /
  // -var-evaluate-expression / -var-info-type.
  char cmdbuf[96];
  std::snprintf(cmdbuf, sizeof(cmdbuf),
                "-stack-list-variables --thread 1 --frame %d --simple-values",
                frame_id);
  std::string result = gdb_query(cmdbuf);

  cJSON *resp = make_response(request, /*success=*/true, nullptr);
  cJSON *body = cJSON_CreateObject();
  cJSON *vars = cJSON_CreateArray();

  if (result.compare(0, 5, "^done") == 0) {
    // `^done,variables=[{name="x",value="42",type="int"},{name="s",
    //   value="\"hi\"",type="ObjString *"}]`
    auto var_tuples = mi_split_list(result, "variables");
    for (auto &t : var_tuples) {
      std::string name = mi_field(t, "name");
      std::string value = mi_field(t, "value");
      std::string type = mi_field(t, "type");
      if (name.empty()) continue;
      cJSON *v = cJSON_CreateObject();
      cJSON_AddStringToObject(v, "name", name.c_str());
      cJSON_AddStringToObject(v, "value", value.c_str());
      cJSON_AddStringToObject(v, "type", type.c_str());
      // PR 4d keeps every variable as a leaf — no nested struct
      // drill-down. A later PR can switch to -var-create per entry
      // and use a child variablesReference for tuple/array types.
      cJSON_AddNumberToObject(v, "variablesReference", 0);
      cJSON_AddItemToArray(vars, v);
    }
  }
  // If the result wasn't ^done, leave the array empty. Common cause
  // is calling variables() after the inferior exited; an empty list
  // is more useful to the client than an error.

  cJSON_AddItemToObject(body, "variables", vars);
  cJSON_AddItemToObject(resp, "body", body);
  write_message(resp);
  cJSON_Delete(resp);
}

// ---------------------------------------------------------------------------
// Execution control: continue / pause / next / stepIn / stepOut.
//
// Every command is fire-and-forget at the DAP level: the response
// goes out immediately with success=true, and the subsequent
// `*stopped` async record from gdb (handled by the reader thread)
// translates into a DAP `stopped` event that drives the client's UI.
//
// MI mapping:
//   continue   →  -exec-continue            (resume all threads)
//   pause      →  -exec-interrupt           (SIGINT into the inferior)
//   next       →  -exec-next                (step over)
//   stepIn     →  -exec-step                (step into)
//   stepOut    →  -exec-finish              (run to caller, then stop)

void execution_control_reply(cJSON *request, bool include_continued_flag) {
  cJSON *resp = make_response(request, /*success=*/true, nullptr);
  cJSON *body = cJSON_CreateObject();
  if (include_continued_flag) {
    cJSON_AddBoolToObject(body, "allThreadsContinued", true);
  }
  cJSON_AddItemToObject(resp, "body", body);
  write_message(resp);
  cJSON_Delete(resp);
}

void execution_control_fail(cJSON *request, const char *msg) {
  cJSON *resp = make_response(request, /*success=*/false, msg);
  cJSON_AddItemToObject(resp, "body", cJSON_CreateObject());
  write_message(resp);
  cJSON_Delete(resp);
}

void handle_continue(cJSON *request) {
  if (!g_gdb.running()) {
    execution_control_fail(request, "continue: gdb subprocess not running");
    return;
  }
  // -exec-continue resumes all stopped threads. Tulpar AOT binaries
  // are effectively single-threaded from the debugger's POV (wings
  // worker pools don't surface to gdb yet), so the global resume is
  // the right semantic.
  g_gdb.send_command("-exec-continue", /*prefix_token=*/true);
  execution_control_reply(request, /*include_continued_flag=*/true);
}

void handle_pause(cJSON *request) {
  if (!g_gdb.running()) {
    execution_control_fail(request, "pause: gdb subprocess not running");
    return;
  }
  // -exec-interrupt sends SIGINT to the inferior. The reader thread
  // will surface the resulting *stopped record as a DAP `stopped`
  // event (mapped from `signal-received`/SIGINT → `pause`).
  g_gdb.send_command("-exec-interrupt", /*prefix_token=*/true);
  execution_control_reply(request, /*include_continued_flag=*/false);
}

void handle_next(cJSON *request) {
  if (!g_gdb.running()) {
    execution_control_fail(request, "next: gdb subprocess not running");
    return;
  }
  g_gdb.send_command("-exec-next", /*prefix_token=*/true);
  execution_control_reply(request, /*include_continued_flag=*/false);
}

void handle_step_in(cJSON *request) {
  if (!g_gdb.running()) {
    execution_control_fail(request, "stepIn: gdb subprocess not running");
    return;
  }
  g_gdb.send_command("-exec-step", /*prefix_token=*/true);
  execution_control_reply(request, /*include_continued_flag=*/false);
}

void handle_step_out(cJSON *request) {
  if (!g_gdb.running()) {
    execution_control_fail(request, "stepOut: gdb subprocess not running");
    return;
  }
  g_gdb.send_command("-exec-finish", /*prefix_token=*/true);
  execution_control_reply(request, /*include_continued_flag=*/false);
}

// For every request we haven't wired up yet, send a structured
// "not implemented" response so the client surfaces a clear error
// instead of waiting forever for a reply that never comes.
void handle_not_implemented(cJSON *request, const char *command) {
  cJSON *resp = make_response(request, /*success=*/false,
                              "Not implemented yet.");
  cJSON_AddItemToObject(resp, "body", cJSON_CreateObject());
  std::fprintf(stderr, "[dap] request '%s' rejected: not implemented yet\n",
               command ? command : "?");
  write_message(resp);
  cJSON_Delete(resp);
}

}  // namespace

int debug_cmd_main(int argc, char **argv) {
  // Usage: `tulpar debug <file.tpr>`. The file isn't opened here —
  // it's stashed for PR 4b's `launch` request handler. Missing-file
  // diagnostics come later, when the debugger actually tries to use
  // it; for now we only validate that *something* was passed so the
  // DAP scaffold has a placeholder to log against.
  if (argc < 3) {
    std::fprintf(stderr,
                 "Usage: tulpar debug <file.tpr>\n"
                 "Opens a DAP-speaking stdio server. Connect with VS Code's "
                 "\"Run and Debug\" panel (configured via the vscode-tulpar "
                 "extension) or any other DAP client.\n");
    return 2;
  }
  g_program_path = argv[2];
  std::fprintf(stderr,
               "[dap] tulpar debug adapter starting (program: %s)\n",
               g_program_path.c_str());

  make_streams_binary();

  std::string body;
  while (read_message(body)) {
    cJSON *msg = cJSON_Parse(body.c_str());
    if (!msg) {
      std::fprintf(stderr, "[dap] dropped malformed message: %s\n",
                   body.substr(0, 120).c_str());
      continue;
    }
    cJSON *type = cJSON_GetObjectItem(msg, "type");
    cJSON *command = cJSON_GetObjectItem(msg, "command");
    bool is_request =
        cJSON_IsString(type) && type->valuestring &&
        std::strcmp(type->valuestring, "request") == 0;
    const char *cmd = (cJSON_IsString(command) && command->valuestring)
                          ? command->valuestring
                          : "";

    if (!is_request) {
      // We don't expect server-bound events or responses; ignore them.
      cJSON_Delete(msg);
      continue;
    }

    if (std::strcmp(cmd, "initialize") == 0) {
      handle_initialize(msg);
    } else if (std::strcmp(cmd, "launch") == 0) {
      handle_launch(msg);
    } else if (std::strcmp(cmd, "setBreakpoints") == 0) {
      handle_set_breakpoints(msg);
    } else if (std::strcmp(cmd, "configurationDone") == 0) {
      handle_configuration_done(msg);
    } else if (std::strcmp(cmd, "threads") == 0) {
      handle_threads(msg);
    } else if (std::strcmp(cmd, "stackTrace") == 0) {
      handle_stack_trace(msg);
    } else if (std::strcmp(cmd, "scopes") == 0) {
      handle_scopes(msg);
    } else if (std::strcmp(cmd, "variables") == 0) {
      handle_variables(msg);
    } else if (std::strcmp(cmd, "continue") == 0) {
      handle_continue(msg);
    } else if (std::strcmp(cmd, "pause") == 0) {
      handle_pause(msg);
    } else if (std::strcmp(cmd, "next") == 0) {
      handle_next(msg);
    } else if (std::strcmp(cmd, "stepIn") == 0) {
      handle_step_in(msg);
    } else if (std::strcmp(cmd, "stepOut") == 0) {
      handle_step_out(msg);
    } else if (std::strcmp(cmd, "terminate") == 0) {
      handle_terminate(msg);
    } else if (std::strcmp(cmd, "disconnect") == 0) {
      handle_disconnect(msg);
      cJSON_Delete(msg);
      break;
    } else {
      handle_not_implemented(msg, cmd);
    }
    cJSON_Delete(msg);
  }

  // EOF or explicit disconnect — either way, make sure the gdb
  // subprocess doesn't outlive us. stop() is a no-op if it was
  // already reaped by handle_terminate/handle_disconnect.
  g_gdb.stop();
  std::fprintf(stderr, "[dap] adapter shutting down\n");
  return 0;
}

}  // namespace tulpar
