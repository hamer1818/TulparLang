// QualityScaler: bilinen kare-suresi dizileriyle bilinen tier gecisleri +
// histerezis (gurultu tier degistirmemeli) + sinir tasmasi yok (Low altina/
// High ustune gecmeye calismaz).
#include "content/dynamic_quality.hpp"
#include "tests/test.hpp"

using namespace tulpar::engine;
using namespace tulpar::engine::test;

namespace {
constexpr float kTarget = 16.667f; // 60 FPS
} // namespace

ENGINE_TEST(quality_scaler_downgrades_after_sustained_bad_frames) {
  content::QualityScaler q(content::DeviceTier::Mid);
  content::DeviceTier last = content::DeviceTier::Mid;
  for (int i = 0; i < 10; i++) last = q.update(30.0f, kTarget); // hedefin cok ustunde, 10 kare = esik
  CHECK(last == content::DeviceTier::Low);
  CHECK(q.current() == content::DeviceTier::Low);
}

ENGINE_TEST(quality_scaler_upgrades_after_sustained_good_frames) {
  content::QualityScaler q(content::DeviceTier::Mid);
  content::DeviceTier last = content::DeviceTier::Mid;
  for (int i = 0; i < 30; i++) last = q.update(4.0f, kTarget); // hedefin cok altinda, 30 kare = esik
  CHECK(last == content::DeviceTier::High);
}

ENGINE_TEST(quality_scaler_never_goes_below_low_or_above_high) {
  content::QualityScaler lo(content::DeviceTier::Low);
  for (int i = 0; i < 100; i++) lo.update(1000.0f, kTarget); // surekli feci kotu
  CHECK(lo.current() == content::DeviceTier::Low); // taban asilmadi (uint donusumu tasmadi)

  content::QualityScaler hi(content::DeviceTier::High);
  for (int i = 0; i < 100; i++) hi.update(0.1f, kTarget); // surekli mukemmel
  CHECK(hi.current() == content::DeviceTier::High); // tavan asilmadi
}

ENGINE_TEST(quality_scaler_noisy_frames_do_not_flap) {
  content::QualityScaler q(content::DeviceTier::Mid);
  // Bir kotu, bir iyi, dongu -- hicbir sayac esige ulasamamali (her seferinde
  // sifirlaniyor), tier HICBIR ZAMAN degismemeli.
  for (int i = 0; i < 200; i++) {
    q.update(30.0f, kTarget); // kotu (bad_streak_ 1'e cikar)
    q.update(4.0f, kTarget);  // iyi (bad_streak_'i sifirlar, good_streak_ 1'e cikar)
  }
  CHECK(q.current() == content::DeviceTier::Mid);
}

ENGINE_TEST(quality_scaler_neutral_frames_reset_streak) {
  content::QualityScaler q(content::DeviceTier::Mid);
  for (int i = 0; i < 9; i++) q.update(30.0f, kTarget); // esigin BIR altinda kotu kare (9 < 10)
  q.update(kTarget, kTarget);                            // tam hedefte: NOTR, sayaci sifirlar
  content::DeviceTier last = content::DeviceTier::Mid;
  for (int i = 0; i < 9; i++) last = q.update(30.0f, kTarget); // yeniden 9 kotu kare: HALA esigin altinda
  CHECK(last == content::DeviceTier::Mid); // dusmedi -- notr kare sayaci gercekten sifirladi
}
