// platform/frame_pacer.hpp: target_fps=100 (target_frame_time=0.01s TAM
// sayi) secilerek TAM cikarma sonuclari elle hesaplanip dogrulandi.
#include "core/math/vec.hpp"
#include "platform/frame_pacer.hpp"
#include "tests/test.hpp"

using namespace tulpar::engine;
using namespace tulpar::engine::platform;

ENGINE_TEST(frame_pacer_computes_exact_wait_and_counts_missed_frames) {
  FramePacer p(100.0); // target_frame_time_s = 1/100 = 0.01 TAM
  CHECK(nearly_equal(p.target_frame_time_s(), 0.01, 1e-6));

  double r0 = p.begin_frame(0.0);
  CHECK(r0 == 0.0); // ilk kare: referans yok, bekleme HESAPLANAMAZ

  double r1 = p.begin_frame(0.005); // gecen sure 0.005 < hedef 0.01 -> HIZLI kare
  CHECK(nearly_equal(r1, 0.005, 1e-6));
  CHECK(p.missed_frames() == 0);

  double r2 = p.begin_frame(0.025); // gecen sure 0.025-0.005=0.02 > hedef -> YAVAS kare
  CHECK(nearly_equal(r2, -0.01, 1e-6));
  CHECK(p.missed_frames() == 1);
}

ENGINE_TEST(frame_pacer_disabled_when_target_fps_non_positive) {
  FramePacer p(0.0); // sinirsiz kare hizi modu
  CHECK(p.begin_frame(0.0) == 0.0);
  CHECK(p.begin_frame(5.0) == 0.0);  // ne kadar zaman gecerse gecsin
  CHECK(p.begin_frame(100.0) == 0.0);
  CHECK(p.missed_frames() == 0); // hicbir kare "gecikmis" SAYILMAZ
}
