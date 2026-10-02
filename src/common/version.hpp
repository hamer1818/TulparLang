#ifndef TULPAR_COMMON_VERSION_HPP
#define TULPAR_COMMON_VERSION_HPP

// Single source of truth for the version string surfaced by
// `tulpar --version` and used by `tulpar update --check` to compare
// against the latest GitHub release tag.
//
// The CMake build generates `tulpar_version.h` on every build from the git
// tag (cmake/TulparVersion.cmake): release CI passes the tag itself
// (e.g. "v3.37.16"), other builds get `git describe` ("v3.37.16-4-gabc1234",
// "-dirty" when modified) and a tree without git/tags gets "0.0.0-dev" —
// never a hand-written number, which is what used to drift ("3.13.1-dev"
// while v3.37.x was out). A dev build only matches a release when it IS
// that tagged, clean commit.
#if defined(TULPAR_HAVE_VERSION_HEADER)
#include "tulpar_version.h"
#endif
#ifndef TULPAR_VERSION_STRING
#define TULPAR_VERSION_STRING "0.0.0-dev"
#endif

namespace tulpar {

inline constexpr const char *kVersion = TULPAR_VERSION_STRING;

}  // namespace tulpar

#endif  // TULPAR_COMMON_VERSION_HPP
