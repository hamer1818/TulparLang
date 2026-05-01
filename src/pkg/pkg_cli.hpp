#ifndef TULPAR_PKG_CLI_HPP
#define TULPAR_PKG_CLI_HPP

namespace tulpar {

// Entry point for `tulpar pkg <subcommand> [args]`.
// Subcommands implemented today:
//   * init [name]      — create a tulpar.toml in the cwd
//   * list             — print the parsed manifest
//   * add <name@ver>   — append a dependency line (no fetching yet)
//   * remove <name>    — remove a dependency line
//
// Returns process exit code (0 = success, 1 = expected failure,
// 2 = usage error).
int pkg_cli_main(int argc, char **argv);

}  // namespace tulpar

#endif  // TULPAR_PKG_CLI_HPP
