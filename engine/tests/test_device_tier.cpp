// Cihaz-sinifi siniflandirma: bilinen GPU adlari + RAM esikleri, pozitif/
// negatif kontrol (bilinmeyen GPU, sifir RAM, buyuk/kucuk harf farki).
#include "content/device_tier.hpp"
#include "tests/test.hpp"

using namespace tulpar::engine;
using namespace tulpar::engine::test;

ENGINE_TEST(device_tier_classifies_known_low_gpu) {
  // DURUM.md'de gercekten olculen cihaz: Huawei P20 Pro (Mali-G72).
  CHECK(content::device_tier_classify("Mali-G72", 4096) == content::DeviceTier::Low);
  CHECK(content::device_tier_classify("ARM Mali-G52 MC2", 0) == content::DeviceTier::Low);
}

ENGINE_TEST(device_tier_classifies_known_high_gpu) {
  CHECK(content::device_tier_classify("Mali-G925", 8192) == content::DeviceTier::High);
  CHECK(content::device_tier_classify("Qualcomm Adreno 750", 6144) == content::DeviceTier::High);
}

ENGINE_TEST(device_tier_is_case_insensitive) {
  CHECK(content::device_tier_classify("mali-g72", 4096) == content::DeviceTier::Low);
  CHECK(content::device_tier_classify("MALI-G925", 8192) == content::DeviceTier::High);
}

ENGINE_TEST(device_tier_falls_back_to_ram) {
  // Bilinmeyen GPU adi: yalniz RAM'e gore karar.
  CHECK(content::device_tier_classify("UnknownGPU X1", 0) == content::DeviceTier::Mid);       // bilinmiyor -> guvenli orta
  CHECK(content::device_tier_classify("UnknownGPU X1", 2048) == content::DeviceTier::Low);    // az RAM
  CHECK(content::device_tier_classify("UnknownGPU X1", 4096) == content::DeviceTier::Mid);    // orta RAM
  CHECK(content::device_tier_classify("UnknownGPU X1", 8192) == content::DeviceTier::High);   // cok RAM
}

ENGINE_TEST(device_tier_handles_null_gpu_name) {
  // Negatif kontrol: nullptr GPU adi cokmemeli, RAM'e dusmeli.
  CHECK(content::device_tier_classify(nullptr, 8192) == content::DeviceTier::High);
  CHECK(content::device_tier_classify(nullptr, 0) == content::DeviceTier::Mid);
}

ENGINE_TEST(device_tier_profiles_scale_up_monotonically) {
  const content::TierProfile &lo = content::device_tier_profile(content::DeviceTier::Low);
  const content::TierProfile &mid = content::device_tier_profile(content::DeviceTier::Mid);
  const content::TierProfile &hi = content::device_tier_profile(content::DeviceTier::High);
  CHECK(lo.shadow_map_size <= mid.shadow_map_size && mid.shadow_map_size <= hi.shadow_map_size);
  CHECK(lo.max_point_lights <= mid.max_point_lights && mid.max_point_lights <= hi.max_point_lights);
  CHECK(lo.msaa_samples <= hi.msaa_samples);
  CHECK(!lo.enable_fog); // en dusuk segmentte analitik sis bile kapali (Is 6'nin bedeli sifir degil)
}
