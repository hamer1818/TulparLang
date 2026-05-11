// Tulpar Debug Adapter Protocol (DAP) entry point.
//
// Plan 07 PR 4 — Debugger MVP, Part B.
//
// `tulpar debug <file.tpr>` opens a DAP-speaking stdio server so a
// DAP client (VS Code's "Run and Debug" panel, or any other DAP
// front-end) can drive the program. This first PR ships the
// scaffolding: stdio JSON-RPC framing, an `initialize`/`disconnect`
// handshake, and a minimal capabilities advertisement. The gdb
// subprocess that actually backs `setBreakpoints` / `stackTrace` /
// `variables` / `continue` / `next` lands in subsequent PRs.
//
// Wire protocol summary (DAP, the same shape LSP uses):
//   Content-Length: <N>\r\n
//   \r\n
//   { "seq": ..., "type": "request"|"response"|"event",
//     "command": ..., "body": { ... } }
//
// References:
//   - https://microsoft.github.io/debug-adapter-protocol/
//   - https://microsoft.github.io/debug-adapter-protocol/specification

#ifndef TULPAR_DEBUG_CMD_H
#define TULPAR_DEBUG_CMD_H

namespace tulpar {

// argv[0] = "tulpar", argv[1] = "debug", argv[2] = <file.tpr>, ...
// Owns stdin/stdout for JSON-RPC, so it must dispatch before any
// banner / REPL output (same constraint `--lsp` carries).
int debug_cmd_main(int argc, char **argv);

}  // namespace tulpar

#endif  // TULPAR_DEBUG_CMD_H
