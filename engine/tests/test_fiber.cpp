#include <cstdint>

#include "core/jobs/fiber.hpp"
#include "platform/memory.hpp"
#include "tests/test.hpp"

using namespace tulpar::engine;

namespace {
void *g_main_sp = nullptr;
Fiber g_f;
int g_trace[8];
int g_n = 0;

void fiber_body(void *arg) {
  g_trace[g_n++] = (int)(intptr_t)arg;
  tulpar_fiber_switch(&g_f.sp, g_main_sp); // ana baglama don
  g_trace[g_n++] = 99;                      // yeniden girildi
  tulpar_fiber_switch(&g_f.sp, g_main_sp);
  for (;;) {} // ulasilmaz: fiber bir daha calistirilmaz
}
} // namespace

ENGINE_TEST(fiber_switch_roundtrip_preserves_state) {
  const size_t kStack = 64 * 1024;
  uint8_t *stack = static_cast<uint8_t *>(platform::os_reserve(kStack));
  CHECK(stack != nullptr);
  g_f.stack_lo = stack;
  g_f.stack_bytes = kStack;
  fiber_prepare(g_f, fiber_body, (void *)42);
  g_n = 0;
  // Callee-saved yazmaclarin korundugunu sinamak icin gecis boyunca yasayan
  // yerel degerler (derleyici bunlari rbx/r12.. ya da x19.. de tutabilir).
  uint64_t a = 0x1111, b = 0x2222, c = 0x3333, d = 0x4444, e = 0x5555, f = 0x6666;
  tulpar_fiber_switch(&g_main_sp, g_f.sp);
  CHECK(g_n == 1 && g_trace[0] == 42);
  a += b; c += d; e += f;
  tulpar_fiber_switch(&g_main_sp, g_f.sp);
  CHECK(g_n == 2 && g_trace[1] == 99);
  CHECK(a == 0x3333 && c == 0x7777 && e == 0xBBBB && b == 0x2222 && d == 0x4444 && f == 0x6666);
  platform::os_release(stack, kStack);
}
