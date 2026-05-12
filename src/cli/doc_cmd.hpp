// `tulpar doc <file.tpr>` — emit a markdown reference for every function
// (and top-level variable) declared in the source file. The doc body for
// each entry is the leading `//` comment block right above its
// declaration, identical to what the LSP's `hover` surfaces in the
// editor.
//
// Usage:
//   tulpar doc path/to/script.tpr             # write to stdout
//   tulpar doc path/to/script.tpr > REFERENCE.md
//
// Output shape:
//   # <filename>
//
//   ## Functions
//
//   ### `name(p1: T1, p2: T2): RetT`
//
//   <leading comment block>
//
//   ## Globals
//
//   - `name: type` — leading comment
//
// Exit codes:
//   0 — emitted markdown to stdout.
//   1 — parse/codegen failures (same diagnostic the LSP/check path prints
//        is also surfaced).
//   2 — usage / I/O error.

#ifndef TULPAR_DOC_CMD_HPP
#define TULPAR_DOC_CMD_HPP

namespace tulpar {

int doc_cmd_main(int argc, char **argv);

}  // namespace tulpar

#endif  // TULPAR_DOC_CMD_HPP
