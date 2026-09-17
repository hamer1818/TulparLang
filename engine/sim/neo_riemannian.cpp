#include "sim/neo_riemannian.hpp"

namespace tulpar::engine::sim {

Chord apply_p(const Chord& chord) {
  Chord result = chord;
  result.type = (chord.type == ChordType::Major) ? ChordType::Minor : ChordType::Major;
  return result;
}

Chord apply_l(const Chord& chord) {
  Chord result;
  if (chord.type == ChordType::Major) {
    // Kok yarim ses asagi, Minor
    result.root_note = (chord.root_note + 4) % 12; // Majör kökün +4 (major 3rd) notası Minörün kökü olur (C Maj -> E Min, C=0, E=4)
    result.type = ChordType::Minor;
  } else {
    // Kok +8 (minor 6th) yukari, Major
    result.root_note = (chord.root_note + 8) % 12; // Minör kökün +8 (minor 6th) notası Majörün kökü olur (A Min -> F Maj, A=9, F=5)
    result.type = ChordType::Major;
  }
  return result;
}

Chord apply_r(const Chord& chord) {
  Chord result;
  if (chord.type == ChordType::Major) {
    // Kok 3 yarim ses asagi, Minor (C Maj -> A Min)
    result.root_note = (chord.root_note + 9) % 12;
    result.type = ChordType::Minor;
  } else {
    // Kok 3 yarim ses yukari, Major (A Min -> C Maj)
    result.root_note = (chord.root_note + 3) % 12;
    result.type = ChordType::Major;
  }
  return result;
}

void NeoRiemannianGen::init(uint32_t seed) {
  state_ = seed;
  current_ = {0, ChordType::Major}; // C Maj
}

uint32_t NeoRiemannianGen::next_rand() {
  // Basit LCG RNG
  state_ = state_ * 1664525 + 1013904223;
  return state_;
}

Chord NeoRiemannianGen::next_chord() {
  uint32_t r = next_rand() % 3;
  
  if (r == 0) {
    current_ = apply_p(current_);
  } else if (r == 1) {
    current_ = apply_l(current_);
  } else {
    current_ = apply_r(current_);
  }
  
  return current_;
}

} // namespace tulpar::engine::sim
