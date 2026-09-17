// L4 SIMULATION — Neo-Riemannian Muzik Uretici.
// DEVAM_PLANI.md Faz 4 (Devrimsel) madde D12.
// 
// Deterministik prosedurel muzik uretimi. Muzigin rassal "gurultu" (noise)
// olmamasi, muzikal kurallara uymasi icin Neo-Riemannian teorisi (P, L, R donusumleri)
// kullanilarak akor gecisleri uretilir. Determinizm, ayni tohumla ayni muzigi
// sonsuza dek bit-es (bit-exact) ayni uretmeyi saglar.
#pragma once
#include <cstdint>

namespace tulpar::engine::sim {

enum class ChordType : uint8_t {
  Major,
  Minor
};

struct Chord {
  uint8_t root_note; // 0 = C, 1 = C#, ..., 11 = B
  ChordType type;
};

// P (Parallel) Donusumu: Major <-> Minor, ayni kok.
// Ornek: C Major -> C Minor
Chord apply_p(const Chord& chord);

// L (Leading-Tone) Donusumu:
// Major: Kok yarim ses asagi, tur Minor'e doner (C Maj -> E Min)
// Minor: 5'li (kok+7) yarim ses yukari, tur Major'e doner (A Min -> F Maj)
Chord apply_l(const Chord& chord);

// R (Relative) Donusumu:
// Major: 5'li (kok+7) 2 tam ses yukari (veya kok-3), tur Minor (C Maj -> A Min)
// Minor: Kok 3 yarim ses yukari, tur Major (A Min -> C Maj)
Chord apply_r(const Chord& chord);

class NeoRiemannianGen {
 public:
  void init(uint32_t seed);
  
  // Sifirdan bir sonraki akoru uretir (RNG kullanir). 
  // Deterministik bir dizilim olusturur.
  Chord next_chord();
  
  const Chord& current_chord() const { return current_; }

 private:
  // LCG tabanli basit ve hizli deterministik RNG
  uint32_t next_rand();
  
  uint32_t state_ = 0;
  Chord current_ = {0, ChordType::Major}; // C Major baslangic
};

} // namespace tulpar::engine::sim
