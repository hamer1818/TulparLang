// L1 CORE — Fiber: kendi yigini olan, ISBIRLIKLI olarak degistirilen yurutme
// baglami. Job sistemi (Naughty Dog modeli) beklemeleri thread bloklamadan
// fiber degistirerek yapar. Gecis asm'de (x86_64 SysV + AArch64 AAPCS64).
//
// ⚠ Bir fiber baska bir THREAD'de devam edebilir. Bir gecisin iki yaninda
// `thread_local` adresini onbellekleme — `JobSystem::current_tls()` gibi
// noinline bir getter uzerinden her seferinde yeniden oku.
#pragma once
#include <cstddef>
#include <cstdint>

namespace tulpar::engine {

extern "C" void tulpar_fiber_switch(void **save_sp, void *load_sp);
extern "C" void tulpar_fiber_trampoline();

using FiberFn = void (*)(void *arg);

struct Fiber {
  void *sp = nullptr;         // calismiyorken kayitli yigin isaretcisi
  uint8_t *stack_lo = nullptr; // en dusuk adres (altinda bekci sayfa)
  size_t stack_bytes = 0;
  uint32_t index = 0;
  // Profiler: acik bolge yigini fiber'la birlikte gocer (thread'le degil).
  uint32_t zone_stack[32];
  uint32_t zone_depth = 0;
};

// Ilk gecis fn(arg) cagiracak sekilde yigini hazirlar. fn ASLA donmemeli:
// bitince scheduler'a geri gecmeli (job sistemi bunu yapar).
void fiber_prepare(Fiber &f, FiberFn fn, void *arg);

// Mevcut baglami *save_sp'ye kaydedip `to`ya gecer; `to` geri gecince doner.
inline void fiber_switch_to(void **save_sp, const Fiber &to) {
  tulpar_fiber_switch(save_sp, to.sp);
}

} // namespace tulpar::engine
