// Tulpar Debug Adapter Protocol (DAP) entry point.
//
// Plan 07 PR 4 — Debugger MVP, Part B (scaffold).
//
// This first PR establishes the DAP plumbing:
//   - stdio JSON-RPC framing read + write (`Content-Length: N\r\n\r\n<json>`),
//   - sequence-number bookkeeping for response correlation,
//   - an `initialize` → capabilities advertisement,
//   - a `disconnect` → clean exit,
//   - "not implemented yet" responses for every other DAP request so a
//     client sees a clearly-shaped error instead of a hang.
//
// What's deliberately NOT here yet:
//   - No gdb subprocess. `launch`, `setBreakpoints`, `stackTrace`,
//     `variables`, `continue`, `next`, `stepIn`, `stepOut`, etc. all
//     reply `success: false` with `message: "not implemented yet"`.
//     Those wire up in PR 4b on top of `gdb --interpreter=mi3`.
//   - No source-file validation. We accept the program path argument
//     and stash it so PR 4b's `launch` can read it back, but we don't
//     try to AOT-build the .tpr here — the debug build happens inside
//     `launch` request handling, which doesn't exist yet.
//   - No threads/scopes/variables view. Capabilities reflect that.
//
// stdin/stdout are owned by this command — every diagnostic line goes
// to stderr only (LSP follows the same rule, for the same reason).

#include "debug_cmd.hpp"
#include "../aot/aot_pipeline.hpp"

extern "C" {
#include "../../runtime/cJSON.h"
}

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <sys/stat.h>

#ifdef _WIN32
  #include <fcntl.h>
  #include <io.h>
  #define DUP _dup
  #define DUP2 _dup2
  #define FILENO _fileno
  #define CLOSE _close
#else
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
// True once a `launch` request has run AOT-build to completion. PR 4c
// will use this as the gate for forwarding execution-control requests
// (`continue` / `next` / `stepIn`) to the underlying debugger.
bool g_launched = false;

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

// Write one DAP message. Caller owns + frees the cJSON object.
void write_message(cJSON *msg) {
  char *json = cJSON_PrintUnformatted(msg);
  if (!json) return;
  size_t len = std::strlen(json);
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

  // Stash the path the gdb spawn (PR 4c) will hand to
  // `--interpreter=mi3`. AOT emits the executable next to the build
  // dir (or with .exe on Windows); we don't need to verify that file
  // exists right now because the spawn step will surface that error
  // naturally.
  g_built_binary = output;
  g_launched = true;
  std::fprintf(stderr, "[dap] launch: build OK, binary=%s\n",
               output.c_str());

  cJSON *resp = make_response(request, /*success=*/true, nullptr);
  cJSON_AddItemToObject(resp, "body", cJSON_CreateObject());
  write_message(resp);
  cJSON_Delete(resp);
}

// `configurationDone` is the client saying "I've sent all my
// setBreakpoints / setExceptionBreakpoints / setDataBreakpoints
// requests — go run the debuggee now." PR 4b accepts it but doesn't
// actually start anything; PR 4c will spawn gdb here and forward
// `-exec-run`.
void handle_configuration_done(cJSON *request) {
  cJSON *resp = make_response(request, /*success=*/true, nullptr);
  cJSON_AddItemToObject(resp, "body", cJSON_CreateObject());
  write_message(resp);
  cJSON_Delete(resp);
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

// `terminate` (graceful shutdown, vs. `disconnect` which is
// hard-stop) — PR 4c will use this to send `kill` to the gdb
// subprocess. For now there's nothing to terminate beyond the
// adapter itself, so we acknowledge and let the loop continue;
// VS Code follows up with `disconnect` after a graceful terminate.
void handle_terminate(cJSON *request) {
  cJSON *resp = make_response(request, /*success=*/true, nullptr);
  cJSON_AddItemToObject(resp, "body", cJSON_CreateObject());
  write_message(resp);
  cJSON_Delete(resp);
  g_launched = false;
}

// `disconnect` is the client's "we're done, please exit". DAP allows
// the server to choose whether to terminate the debuggee — we don't
// have one yet, so just acknowledge and let the main loop fall out.
void handle_disconnect(cJSON *request) {
  cJSON *resp = make_response(request, /*success=*/true, nullptr);
  cJSON_AddItemToObject(resp, "body", cJSON_CreateObject());
  write_message(resp);
  cJSON_Delete(resp);
}

// For every request we haven't wired up yet, send a structured
// "not implemented" response so the client surfaces a clear error
// instead of waiting forever for a reply that never comes.
void handle_not_implemented(cJSON *request, const char *command) {
  cJSON *resp = make_response(request, /*success=*/false,
                              "Not implemented yet — PR 4b will add this.");
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
    } else if (std::strcmp(cmd, "configurationDone") == 0) {
      handle_configuration_done(msg);
    } else if (std::strcmp(cmd, "threads") == 0) {
      handle_threads(msg);
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

  std::fprintf(stderr, "[dap] adapter shutting down\n");
  return 0;
}

}  // namespace tulpar
