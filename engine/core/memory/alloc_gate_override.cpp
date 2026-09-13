// Global operator new/delete override — YURUTULEBILIRE acikca eklenir
// (engine_tests, ileride oyun ikilisi). Kutuphaneye konmadi: hangi ikilinin
// sayildigi CMake'te gorunur olsun.
#include <cstdlib>
#include <new>

#include "core/memory/alloc_gate.hpp"

using tulpar::engine::AllocGate;

namespace {
void *counted_alloc(std::size_t n) {
  AllocGate::on_alloc(n);
  void *p = std::malloc(n ? n : 1);
  if (!p) std::abort(); // exception yok (motor kurali)
  return p;
}
void counted_free(void *p) noexcept {
  if (p) AllocGate::on_free();
  std::free(p);
}
} // namespace

void *operator new(std::size_t n) { return counted_alloc(n); }
void *operator new[](std::size_t n) { return counted_alloc(n); }
void *operator new(std::size_t n, const std::nothrow_t &) noexcept { return counted_alloc(n); }
void *operator new[](std::size_t n, const std::nothrow_t &) noexcept { return counted_alloc(n); }
void operator delete(void *p) noexcept { counted_free(p); }
void operator delete[](void *p) noexcept { counted_free(p); }
void operator delete(void *p, std::size_t) noexcept { counted_free(p); }
void operator delete[](void *p, std::size_t) noexcept { counted_free(p); }
