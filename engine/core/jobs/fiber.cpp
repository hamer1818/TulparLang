#include "core/jobs/fiber.hpp"

#include <cstring>

#include "platform/fatal.hpp"

namespace tulpar::engine {

void fiber_prepare(Fiber &f, FiberFn fn, void *arg) {
  ENGINE_ASSERT(f.stack_lo != nullptr && f.stack_bytes >= 4096);
  uintptr_t top = (uintptr_t)(f.stack_lo + f.stack_bytes) & ~(uintptr_t)15;
  f.zone_depth = 0;
#if defined(__x86_64__) && defined(_WIN64)
  // WIN64 DUZENI — fiber_switch_x86_64.S'teki Win64 dalinin pop sirasinin
  // birebir aynasi. SysV'den uc farki var ve ucu de zorunlu:
  //   * callee-saved kumede rdi/rsi de var (8 GPR)
  //   * XMM6-XMM15 callee-saved (160 bayt) + 8 bayt hizalama dolgusu
  //   * TEB'in yigin alanlari (StackBase / StackLimit / DeallocationStack)
  //     fiber'in KENDI yiginini gostermeli; yoksa yigin denetimi ve SEH
  //     baska bir yigini dogru sanar.
  // Toplam cerceve: 24 (TEB) + 168 (XMM+dolgu) + 64 (8 GPR) + 8 (ret) = 264.
  const uint64_t base = (uint64_t)top;                       // yiginin USTU
  const uint64_t limit = (uint64_t)(uintptr_t)f.stack_lo;    // en dusuk adres
  uint64_t *sp = (uint64_t *)(top - 264);
  std::memset(sp, 0, 264);
  sp[0] = limit;                               // DeallocationStack
  sp[1] = limit;                               // StackLimit
  sp[2] = base;                                // StackBase
  // sp[3..22]: XMM6-15 (sifir) + dolgu — memset ile sifirlandi
  sp[24] = 0;                                  // r15
  sp[25] = 0;                                  // r14
  sp[26] = (uint64_t)(uintptr_t)arg;           // r13
  sp[27] = (uint64_t)(uintptr_t)fn;            // r12
  sp[28] = 0;                                  // rsi
  sp[29] = 0;                                  // rdi
  sp[30] = 0;                                  // rbx
  sp[31] = 0;                                  // rbp
  sp[32] = (uint64_t)(uintptr_t)&tulpar_fiber_trampoline; // ret
  f.sp = sp;
#elif defined(__x86_64__)
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
