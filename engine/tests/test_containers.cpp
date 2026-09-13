#include <cstdint>

#include "core/containers/array.hpp"
#include "core/containers/hash_map.hpp"
#include "core/containers/static_string.hpp"
#include "core/memory/alloc_gate.hpp"
#include "core/memory/arena.hpp"
#include "tests/test.hpp"

using namespace tulpar::engine;

ENGINE_TEST(array_fixed_capacity_never_grows) {
  SystemArena sys;
  CHECK(sys.reserve(1u << 16, "arr"));
  Array<int> a;
  CHECK(a.init(sys, 4));
  CHECK(a.push(1) && a.push(2) && a.push(3) && a.push(4));
  CHECK(!a.push(5)); // dolu: false, buyume yok
  CHECK(a.size() == 4 && a.full());
  a.pop();
  CHECK(a.size() == 3 && a.back() == 3);
  int sum = 0;
  for (int v : a) sum += v;
  CHECK(sum == 6);
  a.clear();
  CHECK(a.empty());
}

ENGINE_TEST(hash_map_insert_find_erase_backshift) {
  SystemArena sys;
  CHECK(sys.reserve(1u << 20, "hm"));
  HashMap<uint64_t, uint32_t> m;
  CHECK(m.init(sys, 2048));
  CHECK(m.capacity() == 2048 && m.limit() == 1536);
  for (uint64_t k = 0; k < 1000; k++) CHECK(m.insert(k * 7919 + 13, (uint32_t)k));
  CHECK(m.size() == 1000);
  // Her ikinci anahtari sil (geri kaydirmayi zorlar), kalanlar bulunmali.
  for (uint64_t k = 0; k < 1000; k += 2) CHECK(m.erase(k * 7919 + 13));
  CHECK(m.size() == 500);
  bool ok = true;
  for (uint64_t k = 0; k < 1000; k++) {
    uint32_t *v = m.find(k * 7919 + 13);
    if (k % 2 == 0) ok = ok && v == nullptr;
    else ok = ok && v && *v == (uint32_t)k;
  }
  CHECK(ok);
  CHECK(!m.erase(999999)); // yok
  CHECK(m.insert(5 * 7919 + 13, 777)); // uzerine yaz
  CHECK(*m.find(5 * 7919 + 13) == 777 && m.size() == 500);
  // Doluluk siniri: limit'e kadar kabul, sonra false.
  uint64_t k = 1ull << 40;
  while (m.size() < m.limit()) CHECK(m.insert(k++, 1));
  CHECK(!m.insert(k, 1));
  uint32_t seen = 0;
  m.each([&](const uint64_t &, uint32_t &) { seen++; });
  CHECK(seen == m.size());
}

ENGINE_TEST(hash_map_zero_allocations) {
  SystemArena sys;
  CHECK(sys.reserve(1u << 20, "hm0"));
  HashMap<uint64_t, uint64_t> m;
  CHECK(m.init(sys, 256));
  AllocGate::begin_frame();
  for (uint64_t k = 0; k < 150; k++) m.insert(k, k * 2);
  for (uint64_t k = 0; k < 150; k += 3) m.erase(k);
  uint64_t s = 0;
  for (uint64_t k = 0; k < 150; k++)
    if (uint64_t *v = m.find(k)) s += *v;
  CHECK(AllocGate::end_frame() == 0);
  CHECK(s > 0);
}

ENGINE_TEST(static_string_truncation_is_visible) {
  StaticString<8> s("abc");
  CHECK(s.length() == 3 && s.equals("abc"));
  s.append("defgh"); // 8 karakter istendi, 7 sigar
  CHECK(s.length() == 7 && s.truncated() && s.equals("abcdefg"));
  StaticString<32> f;
  f.appendf("x=%d y=%.1f", 3, 2.5);
  CHECK(f.equals("x=3 y=2.5") && !f.truncated());
  f.append_char('!');
  CHECK(f.equals("x=3 y=2.5!"));
  StaticString<4> t;
  t.appendf("%d", 123456);
  CHECK(t.truncated() && t.length() == 3 && t.equals("123"));
}
