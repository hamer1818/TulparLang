#ifndef TULPAR_CLI_UPDATE_CMD_HPP
#define TULPAR_CLI_UPDATE_CMD_HPP

namespace tulpar {

// Entry point for `tulpar update [--check]`.
//
//   --check : query the latest GitHub release tag, compare with the
//             embedded version, print whether an upgrade is available.
//             Never modifies anything on disk. Exit 0 if up-to-date,
//             exit 0 if newer is available (status reported via stdout).
//   (no flag): same check, then if newer, re-invoke the install script
//             from GitHub (PowerShell on Windows, bash on POSIX) which
//             handles the rename-then-replace dance for the running .exe.
//
// argv layout: [0]="tulpar", [1]="update", [2..]=passthrough flags.
int update_cmd_main(int argc, char **argv);

}  // namespace tulpar

#endif  // TULPAR_CLI_UPDATE_CMD_HPP
