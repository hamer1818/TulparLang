#include "platform/memory.hpp"

// L0 — ham sanal bellek. POSIX'te mmap/munmap/mprotect, Windows'ta
// VirtualAlloc/VirtualFree/VirtualProtect. Sozlesme ikisinde de ayni:
// os_reserve YAZILABILIR bellek dondurur (motorun arena'si ayirdigi ani
// kullanmaya baslar), os_protect_none erisimi tamamen kapatir (kanarya/
// tuzak sayfalari).
#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <sys/mman.h>
#include <unistd.h>
#endif

namespace tulpar::engine::platform {

size_t os_page_size() {
#if defined(_WIN32)
  SYSTEM_INFO si;
  GetSystemInfo(&si);
  return si.dwPageSize ? (size_t)si.dwPageSize : 4096;
#else
  long p = sysconf(_SC_PAGESIZE);
  return p > 0 ? (size_t)p : 4096;
#endif
}

void *os_reserve(size_t bytes) {
#if defined(_WIN32)
  // RESERVE|COMMIT birlikte: POSIX'teki MAP_ANONYMOUS eslesmesi. Windows'ta
  // yalniz MEM_RESERVE yapip sayfa sayfa commit etmek daha "dogru" gorunur
  // ama arena hemen yazmaya basliyor; iki asamali kullanim sozlesmeyi
  // platforma gore ayirir. Commit edilen sayfalar sifirlanmis gelir (POSIX
  // anonim eslesme gibi).
  return VirtualAlloc(nullptr, bytes, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
#else
  void *p = mmap(nullptr, bytes, PROT_READ | PROT_WRITE,
                 MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  return p == MAP_FAILED ? nullptr : p;
#endif
}

void os_release(void *p, size_t bytes) {
#if defined(_WIN32)
  // MEM_RELEASE boyut olarak SIFIR ister ve yalniz VirtualAlloc'un dondurdugu
  // TABAN adresle cagrilabilir — bytes bilerek kullanilmiyor.
  (void)bytes;
  if (p) VirtualFree(p, 0, MEM_RELEASE);
#else
  if (p) munmap(p, bytes);
#endif
}

bool os_protect_none(void *p, size_t bytes) {
#if defined(_WIN32)
  DWORD old = 0;
  return VirtualProtect(p, bytes, PAGE_NOACCESS, &old) != 0;
#else
  return mprotect(p, bytes, PROT_NONE) == 0;
#endif
}

} // namespace tulpar::engine::platform
