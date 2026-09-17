#include "tests/test.hpp"
#include "sim/neo_riemannian.hpp"

using namespace tulpar::engine::sim;

ENGINE_TEST(engine_test_neo_riemannian) {
  NeoRiemannianGen gen;
  gen.init(42); // Sabit tohum
  
  // Baslangic her zaman C Major (0, Major)
  CHECK(gen.current_chord().root_note == 0);
  CHECK(gen.current_chord().type == ChordType::Major);

  // Manuel donusum testleri
  Chord c_maj = {0, ChordType::Major};
  
  // P(C Maj) = C Min
  Chord c_min = apply_p(c_maj);
  CHECK(c_min.root_note == 0);
  CHECK(c_min.type == ChordType::Minor);
  
  // P(C Min) = C Maj
  Chord c_maj_back = apply_p(c_min);
  CHECK(c_maj_back.root_note == 0);
  CHECK(c_maj_back.type == ChordType::Major);

  // L(C Maj) = E Min
  Chord e_min = apply_l(c_maj);
  CHECK(e_min.root_note == 4);
  CHECK(e_min.type == ChordType::Minor);
  
  // L(E Min) = C Maj
  Chord c_maj_back2 = apply_l(e_min);
  CHECK(c_maj_back2.root_note == 0);
  CHECK(c_maj_back2.type == ChordType::Major);

  // R(C Maj) = A Min
  Chord a_min = apply_r(c_maj);
  CHECK(a_min.root_note == 9);
  CHECK(a_min.type == ChordType::Minor);

  // R(A Min) = C Maj
  Chord c_maj_back3 = apply_r(a_min);
  CHECK(c_maj_back3.root_note == 0);
  CHECK(c_maj_back3.type == ChordType::Major);

  // Determinizm Testi: Belli bir rastgele sekansla ayni akorlari mi uretiyor?
  Chord next_1 = gen.next_chord();
  Chord next_2 = gen.next_chord();
  
  // Ayni tohumla bir daha init edelim
  NeoRiemannianGen gen2;
  gen2.init(42);
  Chord next_1_b = gen2.next_chord();
  Chord next_2_b = gen2.next_chord();
  
  CHECK(next_1.root_note == next_1_b.root_note);
  CHECK(next_1.type == next_1_b.type);
  CHECK(next_2.root_note == next_2_b.root_note);
  CHECK(next_2.type == next_2_b.type);
}
