// L0 PLATFORM — is parcacigi ilkelleri. Job sistemi (L1) bunun ustune kurulu;
// motorun geri kalani thread'e dogrudan DOKUNMAZ, job kullanir.
#pragma once
#include <cstdint>

namespace tulpar::engine::platform {

struct Thread {
  unsigned long handle = 0; // pthread_t; L0 disina sizmasin diye opak
  bool valid = false;
};

using ThreadFn = void (*)(void *arg);

bool thread_create(Thread &t, ThreadFn fn, void *arg, const char *name);
void thread_join(Thread &t);
void thread_yield();
void thread_sleep_us(uint32_t us);
uint32_t cpu_count();

} // namespace tulpar::engine::platform
