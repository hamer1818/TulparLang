// sim/lag_compensation.hpp: bu testler tekniginin TEK gerekce nedenini somut
// olarak kanitlar -- ayni atis, hedefin GUNCEL konumuna karsi ISKALAR ama
// atici gordugu (gecmis) konuma karsi (rewind) ISABET EDER. Kanitlanmazsa
// "lag compensation" iddiasi anlamsizdir (sim/rollback.hpp'nin testlerindeki
// ayni ilke: sayiyla kanitla, iddia etme).
#include "core/math/vec.hpp"
#include "sim/lag_compensation.hpp"
#include "tests/test.hpp"

using namespace tulpar::engine;
using namespace tulpar::engine::sim;

ENGINE_TEST(lag_compensation_rewind_interpolates_between_samples) {
  LagCompensator lc(1.0);
  lc.record(0, 0.0, Vec3{0, 0, 0}, 1.0f);
  lc.record(0, 0.1, Vec3{5, 0, 0}, 1.0f);
  lc.record(0, 0.2, Vec3{10, 0, 0}, 1.0f);

  HitboxSample hb;
  CHECK(lc.rewind(0, 0.2, 0.05, &hb)); // 0.0 ve 0.1 arasi tam ortasi
  CHECK(nearly_equal(hb.center, Vec3{2.5f, 0, 0}, 1e-4f));

  // -0.5: en eski ornekten (t=0.0) ONCESI, ama max_rewind_s(1.0) SINIRI
  // ICINDE (now_s-render_time_s=0.7<=1.0) -- reddedilme DEGIL, kilitlenme testi.
  CHECK(lc.rewind(0, 0.2, -0.5, &hb));
  CHECK(nearly_equal(hb.center, Vec3{0, 0, 0}, 1e-4f));

  CHECK(lc.rewind(0, 0.2, 100.0, &hb)); // sinirin ustunde: en yeni ornege kilitlenir
  CHECK(nearly_equal(hb.center, Vec3{10, 0, 0}, 1e-4f));

  CHECK(!lc.rewind(1, 0.2, 0.05, &hb)); // hic record() edilmemis track
}

ENGINE_TEST(lag_compensation_raycast_hits_rewound_but_misses_current) {
  // Hedef +X yonunde hareket ediyor: t=0 x=0, t=0.1 x=5, t=0.2 x=10 (50 birim/sn).
  LagCompensator lc(1.0);
  lc.record(3, 0.0, Vec3{0, 0, 0}, 1.0f);
  lc.record(3, 0.1, Vec3{5, 0, 0}, 1.0f);
  lc.record(3, 0.2, Vec3{10, 0, 0}, 1.0f);

  // Atici, hedefi x=0'dayken (t=0.0) gordugunde ates etti: dogrudan yukari.
  Ray shot{{0, -5, 0}, {0, 1, 0}};

  // GUNCEL (rewind YOK, render_time_s == now_s) konuma karsi test: hedef artik
  // x=10'da, atis ISKALAR -- bu, lag compensation'siz sunucunun yapacagi
  // (HAKSIZ) sey.
  uint32_t track;
  float t;
  CHECK(!lc.raycast(shot, 0.2, 0.2, &track, &t));

  // Ayni atis, aticinin GORDUGU zamana (t=0.0) SARILARAK test edilirse: ISABET.
  CHECK(lc.raycast(shot, 0.2, 0.0, &track, &t));
  CHECK(track == 3u);
  CHECK(nearly_equal(t, 4.0f, 1e-3f)); // y=-5'ten kurenin alt yuzeyine (y=-1) 4 birim
}

ENGINE_TEST(lag_compensation_rejects_excessive_rewind) {
  LagCompensator lc(0.5); // en fazla 0.5s geriye sarilabilir (istismar siniri)
  lc.record(0, 0.0, Vec3{0, 0, 0}, 1.0f);
  lc.record(0, 1.0, Vec3{0, 0, 0}, 1.0f);

  HitboxSample hb;
  CHECK(lc.rewind(0, 1.0, 0.6, &hb));  // 1.0-0.6=0.4s geri: sinir icinde
  CHECK(!lc.rewind(0, 1.0, 0.4, &hb)); // 1.0-0.4=0.6s geri: siniri asiyor, reddedilir

  Ray any{{0, -5, 0}, {0, 1, 0}};
  CHECK(!lc.raycast(any, 1.0, 0.4, nullptr, nullptr)); // asiri eski istek: TUM track'ler reddedilir
}

ENGINE_TEST(lag_compensation_clear_track_deactivates) {
  LagCompensator lc(1.0);
  lc.record(5, 0.0, Vec3{0, 0, 0}, 1.0f);
  lc.record(5, 0.1, Vec3{0, 0, 0}, 1.0f);
  HitboxSample hb;
  CHECK(lc.rewind(5, 0.1, 0.05, &hb));

  lc.clear_track(5);
  CHECK(!lc.rewind(5, 0.1, 0.05, &hb));
}
