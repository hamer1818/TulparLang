#include "core/memory/alloc_gate.hpp"
#include "tests/test.hpp"

using namespace tulpar::engine;
namespace test = tulpar::engine::test;

ENGINE_TEST(alloc_gate_counts_global_new) {
  uint64_t before = AllocGate::total_allocations();
  int *p = new int(1);
  test::escape(p);
  CHECK(AllocGate::total_allocations() == before + 1);
  uint64_t frees = AllocGate::total_frees();
  delete p;
  CHECK(AllocGate::total_frees() == frees + 1);
}

// POZITIF KONTROL: pencere icindeki ayirma sayiliyor mu?
ENGINE_TEST(alloc_gate_frame_window_positive_control) {
  AllocGate::begin_frame();
  CHECK(AllocGate::in_frame());
  CHECK(AllocGate::end_frame() == 0);
  CHECK(!AllocGate::in_frame());
  AllocGate::begin_frame();
  char *p = new char[8];
  test::escape(p);
  delete[] p;
  CHECK(AllocGate::end_frame() == 1);
}
