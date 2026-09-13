#include "platform/time.hpp"

#include <time.h>

namespace tulpar::engine::platform {

uint64_t now_ns() {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

} // namespace tulpar::engine::platform
