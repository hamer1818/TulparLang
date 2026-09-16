#include "content/dynamic_quality.hpp"

namespace tulpar::engine::content {

DeviceTier QualityScaler::update(float frame_ms, float target_ms) {
  const float bad_limit = target_ms * kBadFactor;
  const float good_limit = target_ms * kGoodFactor;
  if (frame_ms > bad_limit) {
    bad_streak_++;
    good_streak_ = 0;
    if (bad_streak_ >= kBadThreshold && tier_ != DeviceTier::Low) {
      tier_ = static_cast<DeviceTier>(static_cast<uint8_t>(tier_) - 1);
      bad_streak_ = 0;
    }
  } else if (frame_ms < good_limit) {
    good_streak_++;
    bad_streak_ = 0;
    if (good_streak_ >= kGoodThreshold && tier_ != DeviceTier::High) {
      tier_ = static_cast<DeviceTier>(static_cast<uint8_t>(tier_) + 1);
      good_streak_ = 0;
    }
  } else {
    // Notr bolge: gurultu tier degistirmesin diye HER IKI sayac da sifirlanir.
    bad_streak_ = 0;
    good_streak_ = 0;
  }
  return tier_;
}

} // namespace tulpar::engine::content
