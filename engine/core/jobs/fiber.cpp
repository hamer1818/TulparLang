#include "core/jobs/fiber.hpp"

#include <cstring>

#include "platform/fatal.hpp"

namespace tulpar::engine {

void fiber_prepare(Fiber &f, FiberFn fn, void *arg) {
  ENGINE_ASSERT(f.stack_lo != nullptr && f.stack_bytes >= 4096);
  uintptr_t top = (uintptr_t)(f.stack_lo + f.stack_bytes) & ~(uintptr_t)15;
  f.zone_depth = 0;
#if defined(__x86_64__)
  // pop sirasi: r15 r14 r13 r12 rbx rbp, sonra ret.
  uint64_t *sp = (uint64_t *)(top - 64);
  sp[0] = 0;                                   // r15
  sp[1] = 0;                                   // r14
  sp[2] = (uint64_t)(uintptr_t)arg;            // r13
  sp[3] = (uint64_t)(uintptr_t)fn;             // r12
  sp[4] = 0;                                   // rbx
  sp[5] = 0;                                   // rbp
  sp[6] = (uint64_t)(uintptr_t)&tulpar_fiber_trampoline; // ret
  sp[7] = 0;
  f.sp = sp;
#elif defined(__aarch64__)
  uint64_t *sp = (uint64_t *)(top - 160);
  std::memset(sp, 0, 160);
  sp[0] = (uint64_t)(uintptr_t)fn;             // x19
  sp[1] = (uint64_t)(uintptr_t)arg;            // x20
  sp[11] = (uint64_t)(uintptr_t)&tulpar_fiber_trampoline; // x30
  f.sp = sp;
#else
#error "fiber: desteklenmeyen mimari (x86_64 ve aarch64 var)"
#endif
}

} // namespace tulpar::engine
