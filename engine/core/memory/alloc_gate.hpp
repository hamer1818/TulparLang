// L1 CORE — A2 kapisi: "kare icinde allocation YOKTUR" iddiasini OLCER.
//
// Global operator new/delete sayilir (override `alloc_gate_override.cpp`de,
// yurutulebilire acikca eklenir). Bir kare `begin_frame()`/`end_frame()`
// arasinda kac ayirma oldugu raporlanir; kapi bunun 0 olmasini ister.
// Sinir: C `malloc` (ucuncu parti C kutuphaneleri) sayilmaz — o yol icin
// ayrica ENGINE_ASSERT'li arena disiplini gecerli. (Not: Faz 0)
#pragma once
#include <cstdint>

namespace tulpar::engine {

class AllocGate {
public:
  // Yasam boyu toplamlar (override her cagrida artirir).
  static uint64_t total_allocations();
  static uint64_t total_frees();
  static uint64_t total_bytes();

  // Kare penceresi.
  static void begin_frame();
  // Pencere icindeki ayirma sayisini doner ve pencereyi kapatir.
  static uint64_t end_frame();
  static bool in_frame();

  // Override'in cagirdigi ic API.
  static void on_alloc(uint64_t bytes);
  static void on_free();
};

} // namespace tulpar::engine
