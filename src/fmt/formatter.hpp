#ifndef TULPAR_FMT_FORMATTER_HPP
#define TULPAR_FMT_FORMATTER_HPP

#include <string>

namespace tulpar {

// Reformat a Tulpar source string in place. The current pass is
// deliberately conservative — it only normalises whitespace, never the
// token stream itself, so it won't break code that mixes Turkish/English
// keywords or uses non-trivial control flow:
//
//   * trailing whitespace stripped
//   * indentation re-derived from `{` / `}` nesting depth
//   * exactly one trailing newline at EOF
//   * idempotent: format(format(s)) == format(s)
//
// Inter-token spacing (operators, commas, parens) is left alone — that
// would require a full token-stream rewrite and is a separate pass.
std::string fmt_source(const std::string &source, int indent_width = 4);

// CLI entry: `tulpar fmt <path> [--write]`. Returns process exit code.
int fmt_cli_main(int argc, char **argv);

}  // namespace tulpar

#endif  // TULPAR_FMT_FORMATTER_HPP
