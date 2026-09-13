#include "platform/fatal.hpp"

#include <cstdarg>
#include <cstdio>
#include <cstdlib>

namespace tulpar::engine::platform {

void fatal(const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  std::fputs("[engine] FATAL: ", stderr);
  std::vfprintf(stderr, fmt, ap);
  std::fputc('\n', stderr);
  va_end(ap);
  std::fflush(stderr);
  std::abort();
}

} // namespace tulpar::engine::platform
