#include <cstdint>

#include "core/memory/arena.hpp"
#include "core/memory/pool.hpp"
#include "tests/test.hpp"

using namespace tulpar::engine;

ENGINE_TEST(arena_alignment_marks_and_stats) {
  SystemArena sys;
  CHECK(sys.reserve(1u << 20, "t"));
  void *a = sys.alloc(1, 16);
  void *b = sys.alloc(1, 64);
  void *c = sys.alloc(1, 256);
  CHECK(((uintptr_t)a & 15) == 0);
  CHECK(((uintptr_t)b & 63) == 0);
  CHECK(((uintptr_t)c & 255) == 0);
  CHECK(sys.contains(a) && sys.contains(c));
  size_t m = sys.mark();
  CHECK(sys.alloc(1000) != nullptr);
  CHECK(sys.used() > m);
  sys.reset_to(m);
  CHECK(sys.used() == m);
  CHECK(sys.check());
  CHECK(sys.stats().alloc_count == 4);
  CHECK(sys.stats().peak >= m + 1000);
  CHECK(sys.stats().reset_count == 1);
  sys.release();
  CHECK(sys.capacity() == 0);
}

ENGINE_TEST(arena_overflow_is_counted_not_silent) {
  SystemArena sys;
  CHECK(sys.reserve(64 * 1024, "small"));
  Arena sub;
  CHECK(sys.carve(sub, 1024, "sub", OverflowPolicy::ReturnNull));
  CHECK(sub.alloc(2048) == nullptr);
  CHECK(sub.stats().overflow_count == 1);
  CHECK(sub.alloc(256) != nullptr); // tasma sonrasi arena hala kullanilabilir
  CHECK(sub.stats().overflow_count == 1);
  CHECK(sub.used() <= sub.capacity());
}

ENGINE_TEST(arena_zeroed_and_typed) {
  SystemArena sys;
  CHECK(sys.reserve(1u << 16, "z"));
  uint32_t *p = sys.alloc_array_zeroed<uint32_t>(64);
  bool all_zero = p != nullptr;
  for (int i = 0; p && i < 64; i++) all_zero = all_zero && p[i] == 0;
  CHECK(all_zero);
  struct alignas(64) Big { char x[64]; };
  Big *q = sys.alloc_array<Big>(3);
  CHECK(q != nullptr && ((uintptr_t)q & 63) == 0);
}

#if defined(ENGINE_MEM_CANARY)
// POZITIF KONTROL: kanarya gercekten yakaliyor mu? (tasma enjeksiyonu)
ENGINE_TEST(arena_canary_catches_overrun) {
  SystemArena sys;
  CHECK(sys.reserve(1u << 16, "c"));
  Arena a;
  CHECK(sys.carve(a, 8192, "a", OverflowPolicy::ReturnNull));
  uint8_t *p = static_cast<uint8_t *>(a.alloc(32));
  uint8_t *q = static_cast<uint8_t *>(a.alloc(16));
  CHECK(p && q);
  CHECK(a.check());
  p[32] = 0xFF; // bir bayt tasma
  CHECK(!a.check());
  p[32] = 0; // kanaryayi geri yaz? Hayir — degeri bilmiyoruz; yalniz durum
  // testin devami icin reset (kanaryalar gecersiz sayilir)
  a.reset();
  CHECK(a.check());
}
#endif

namespace {
struct Thing {
  int v;
  explicit Thing(int x) : v(x) {}
};
} // namespace

ENGINE_TEST(pool_handles_are_generation_tagged) {
  SystemArena sys;
  CHECK(sys.reserve(1u << 20, "p"));
  Pool<Thing> pool;
  CHECK(pool.init(sys, 4, "things"));
  Handle h = pool.create(7);
  CHECK(h.valid());
  CHECK(pool.get(h) && pool.get(h)->v == 7);
  pool.destroy(h);
  CHECK(pool.get(h) == nullptr); // bayat
  Handle h2 = pool.create(9);
  CHECK(h2.index() == h.index()); // ayni slot
  CHECK(h2 != h);                 // farkli nesil
  CHECK(pool.get(h) == nullptr);  // eski handle HALA gecersiz
  CHECK(pool.get(h2) && pool.get(h2)->v == 9);
  Handle a = pool.create(1), b = pool.create(2), c = pool.create(3);
  CHECK(a.valid() && b.valid() && c.valid());
  CHECK(!pool.create(4).valid()); // dolu: gecersiz handle, fatal degil
  CHECK(pool.live_count() == 4);
  int sum = 0;
  pool.each([&](Handle, Thing &t) { sum += t.v; });
  CHECK(sum == 9 + 1 + 2 + 3);
  CHECK(!Handle::invalid().valid());
}
