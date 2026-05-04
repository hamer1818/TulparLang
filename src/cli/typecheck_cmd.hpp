// `tulpar typecheck <file.tpr>` — runs the static type-inference pass and
// prints any errors it finds. Exit code is 0 when clean, 1 on issues, 2 on
// usage/IO errors. The pass itself is informational today (regular `tulpar
// build` / `tulpar --vm` runs do NOT invoke it yet); this subcommand exposes
// the existing typeinfer module as a tool authors can run manually or wire
// into editor / CI checks.

#ifndef TULPAR_TYPECHECK_CMD_H
#define TULPAR_TYPECHECK_CMD_H

namespace tulpar {

int typecheck_cmd_main(int argc, char **argv);

} // namespace tulpar

#endif
