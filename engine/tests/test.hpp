// Minimal test cercevesi: STL yok, ayirma yok (kayit sabit diziye).
#pragma once
#include <cstdio>

namespace tulpar::engine::test {

using TestFn = void (*)();
struct Case {
  const char *name;
  TestFn fn;
};
struct Registry {
  static constexpr int kMax = 256;
  static Case cases[kMax];
  static int count;
  static int failures;      // mevcut testteki CHECK basarisizliklari
  static int failures_total;
};
struct Registrar {
  Registrar(const char *name, TestFn fn) {
    if (Registry::count < Registry::kMax) Registry::cases[Registry::count++] = Case{name, fn};
  }
};

// `new`/`delete` cifti derleyicice ELENEBILIR (C++14 allocation elision,
// GCC/Clang -O2+ yapar). Ayirma sayan testlerde isaretci KACMALI: bu engel
// derleyiciye "p gozlemlendi" der. Olculdu 2026-09-14: engelsiz
// `new int(1); delete` sayaci artirmadi ve kapi testi yanlis dustu.
inline void escape(const void *p) { __asm__ volatile("" : : "r"(p) : "memory"); }

} // namespace tulpar::engine::test

#define ENGINE_TEST(name)                                                       \
  static void name();                                                           \
  static ::tulpar::engine::test::Registrar _reg_##name(#name, name);            \
  static void name()

#define CHECK(cond)                                                             \
  do {                                                                          \
    if (!(cond)) {                                                              \
      std::printf("    FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);           \
      ::tulpar::engine::test::Registry::failures++;                             \
    }                                                                           \
  } while (0)
