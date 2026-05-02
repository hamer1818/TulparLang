#ifndef TULPAR_COMMON_VERSION_HPP
#define TULPAR_COMMON_VERSION_HPP

// Single source of truth for the version string surfaced by
// `tulpar --version` and used by `tulpar update --check` to compare
// against the latest GitHub release tag.
//
// The CMake build wires TULPAR_VERSION_STRING via -D; release CI passes
// the actual git/release tag (e.g. "v2.1.0.42"). Local dev builds fall
// back to "<project_version>-dev" so they never accidentally match a
// real release.
#ifndef TULPAR_VERSION_STRING
#define TULPAR_VERSION_STRING "0.0.0-dev"
#endif

namespace tulpar {

inline constexpr const char *kVersion = TULPAR_VERSION_STRING;

}  // namespace tulpar

#endif  // TULPAR_COMMON_VERSION_HPP
