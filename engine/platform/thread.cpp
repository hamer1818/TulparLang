#include "platform/thread.hpp"

// MinGW-w64 winpthreads'i tasidigi icin thread/mutex/sched yolu Windows'ta da
// AYNI pthread kodudur (ayri bir Win32 uyarlamasi yazmiyoruz: ikinci bir kod
// yolu = ikinci bir hata yuzeyi). Platforma ozgu kalan tek sey cekirdek
// sayisi — sysconf POSIX'e ozgu.
#include <atomic>
#include <pthread.h>
#include <sched.h>
#include <time.h>
#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace tulpar::engine::platform {

namespace {
struct StartArg {
  ThreadFn fn;
  void *arg;
  const char *name;
  std::atomic<bool> copied;
};

void *thread_entry(void *p) {
  StartArg *src = static_cast<StartArg *>(p);
  StartArg a{src->fn, src->arg, src->name, {false}};
  // StartArg cagiran tarafin yiginindaydi; kopyalandi, artik serbest.
  src->copied.store(true, std::memory_order_release);
  if (a.name) {
#if defined(__APPLE__)
    pthread_setname_np(a.name);
#else
    pthread_setname_np(pthread_self(), a.name);
#endif
  }
  a.fn(a.arg);
  return nullptr;
}
} // namespace

bool thread_create(Thread &t, ThreadFn fn, void *arg, const char *name) {
  StartArg a{fn, arg, name, {false}};
  pthread_t h;
  if (pthread_create(&h, nullptr, thread_entry, &a) != 0) return false;
  // Kopyalanana kadar bekle: `a` bu cercevede yasiyor.
  while (!a.copied.load(std::memory_order_acquire)) thread_yield();
  t.handle = (unsigned long)h;
  t.valid = true;
  return true;
}

void thread_join(Thread &t) {
  if (!t.valid) return;
  pthread_join((pthread_t)t.handle, nullptr);
  t.valid = false;
}

void thread_yield() { sched_yield(); }

void thread_sleep_us(uint32_t us) {
  struct timespec ts;
  ts.tv_sec = us / 1000000u;
  ts.tv_nsec = (long)(us % 1000000u) * 1000L;
  nanosleep(&ts, nullptr);
}

uint32_t cpu_count() {
#if defined(_WIN32)
  SYSTEM_INFO si;
  GetSystemInfo(&si);
  return si.dwNumberOfProcessors ? (uint32_t)si.dwNumberOfProcessors : 1u;
#else
  long n = sysconf(_SC_NPROCESSORS_ONLN);
  return n > 0 ? (uint32_t)n : 1u;
#endif
}

} // namespace tulpar::engine::platform
