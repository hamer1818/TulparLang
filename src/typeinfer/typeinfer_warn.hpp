// Shared "type inference as warnings" pre-pass used by `tulpar build`,
// `tulpar` (default AOT run), and `tulpar --vm` so the existing typeinfer
// module surfaces problems at build time instead of staying invisible
// behind the `tulpar typecheck` opt-in subcommand.
//
// The function parses the source through the C++ Parser (the same path
// `tulpar typecheck` uses), runs `typeinfer_program`, and prints
// `[typecheck] <path>: <message>` lines to stderr. It never propagates
// errors — if the pre-pass parse fails, we silently skip so the actual
// compile path produces the canonical diagnostic. Callers should still
// proceed to compile/run regardless of the return value; the count is
// informational only.
//
// Disabled when the env var `TULPAR_NO_TYPECHECK=1` is set, or when the
// CLI driver passed `--no-typecheck`.

#ifndef TULPAR_TYPEINFER_WARN_H
#define TULPAR_TYPEINFER_WARN_H

namespace tulpar {

// Run the pre-pass. Returns the number of `[typecheck]` lines emitted
// (0 when clean, when parse fails, or when disabled by env/flag).
// `source_filename` is optional but recommended — it's folded into the
// printed prefix so editors can jump to the right file.
int typeinfer_emit_warnings(const char *source, const char *source_filename);

}  // namespace tulpar

#endif  // TULPAR_TYPEINFER_WARN_H
