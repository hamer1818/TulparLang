#include "platform/memory.hpp"

#include <sys/mman.h>
#include <unistd.h>

namespace tulpar::engine::platform {

size_t os_page_size() {
  long p = sysconf(_SC_PAGESIZE);
  return p > 0 ? (size_t)p : 4096;
}

void *os_reserve(size_t bytes) {
  void *p = mmap(nullptr, bytes, PROT_READ | PROT_WRITE,
                 MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  return p == MAP_FAILED ? nullptr : p;
}

void os_release(void *p, size_t bytes) {
  if (p) munmap(p, bytes);
}

bool os_protect_none(void *p, size_t bytes) {
  return mprotect(p, bytes, PROT_NONE) == 0;
}

} // namespace tulpar::engine::platform
