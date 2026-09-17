// content/morph.hpp: blend_morph_targets'in tanimi zaten "base + agirlikli
// delta toplami" oldugu icin testler dogrudan elle hesaplanmis toplama
// sonuclarini dogrular -- gizli/karmasik bir algoritma yok, asil risk
// indeksleme (satir-major duz dizi) hatasidir, testler tam da bunu hedefler.
#include "content/morph.hpp"
#include "core/math/vec.hpp"
#include "tests/test.hpp"

using namespace tulpar::engine;
using namespace tulpar::engine::content;

namespace {
// 2 vertex, 2 hedef ortak kurulum:
//   base   = [(0,0,0), (1,1,1)]
//   hedef0 = [(1,0,0), (0,1,0)]
//   hedef1 = [(0,0,2), (0,0,-1)]
const Vec3 kBase[2] = {{0, 0, 0}, {1, 1, 1}};
const Vec3 kDeltas[4] = {
    {1, 0, 0}, {0, 1, 0}, // hedef 0 (vertex 0, vertex 1)
    {0, 0, 2}, {0, 0, -1}, // hedef 1 (vertex 0, vertex 1)
};
} // namespace

ENGINE_TEST(morph_single_target_full_weight_adds_delta_exactly) {
  const float w[2] = {1.0f, 0.0f};
  Vec3 out[2];
  blend_morph_targets(kBase, 2, kDeltas, w, 2, out);
  CHECK(nearly_equal(out[0], Vec3{1, 0, 0}, 1e-6f));
  CHECK(nearly_equal(out[1], Vec3{1, 2, 1}, 1e-6f));
}

ENGINE_TEST(morph_other_target_full_weight) {
  const float w[2] = {0.0f, 1.0f};
  Vec3 out[2];
  blend_morph_targets(kBase, 2, kDeltas, w, 2, out);
  CHECK(nearly_equal(out[0], Vec3{0, 0, 2}, 1e-6f));
  CHECK(nearly_equal(out[1], Vec3{1, 1, 0}, 1e-6f));
}

ENGINE_TEST(morph_two_targets_half_weight_each_combines_additively) {
  const float w[2] = {0.5f, 0.5f};
  Vec3 out[2];
  blend_morph_targets(kBase, 2, kDeltas, w, 2, out);
  // vertex0: (0,0,0) + 0.5*(1,0,0) + 0.5*(0,0,2) = (0.5, 0, 1)
  // vertex1: (1,1,1) + 0.5*(0,1,0) + 0.5*(0,0,-1) = (1, 1.5, 0.5)
  CHECK(nearly_equal(out[0], Vec3{0.5f, 0, 1}, 1e-6f));
  CHECK(nearly_equal(out[1], Vec3{1, 1.5f, 0.5f}, 1e-6f));
}

ENGINE_TEST(morph_zero_weights_leaves_base_unchanged) {
  const float w[2] = {0.0f, 0.0f};
  Vec3 out[2];
  blend_morph_targets(kBase, 2, kDeltas, w, 2, out);
  CHECK(nearly_equal(out[0], kBase[0], 1e-6f));
  CHECK(nearly_equal(out[1], kBase[1], 1e-6f));
}

ENGINE_TEST(morph_in_place_aliasing_matches_out_of_place_result) {
  // base_positions == out_positions AYNI dizi olsa bile sonuc DEGISMEMELI
  // (baslikta belgelenen yerinde-guncelleme garantisi).
  Vec3 buf[2] = {kBase[0], kBase[1]};
  const float w[2] = {0.5f, 0.5f};
  blend_morph_targets(buf, 2, kDeltas, w, 2, buf); // AYNI dizi hem base hem out
  CHECK(nearly_equal(buf[0], Vec3{0.5f, 0, 1}, 1e-6f));
  CHECK(nearly_equal(buf[1], Vec3{1, 1.5f, 0.5f}, 1e-6f));
}
