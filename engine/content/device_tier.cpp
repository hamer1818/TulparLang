#include "content/device_tier.hpp"

#include <cstring>

namespace tulpar::engine::content {

namespace {
constexpr TierProfile kProfiles[3] = {
    /* Low  */ {512, 8, 0.0f, false, 1},
    /* Mid  */ {1024, 16, 0.0f, true, 1},
    /* High */ {2048, 32, 0.3f, true, 4},
};

// Basit ASCII case-insensitive alt-dizgi arama (GPU adlari ASCII).
bool contains_ci(const char *hay, const char *needle) {
  if (!hay || !needle) return false;
  const size_t hn = std::strlen(hay), nn = std::strlen(needle);
  if (nn == 0 || nn > hn) return false;
  for (size_t i = 0; i + nn <= hn; i++) {
    size_t j = 0;
    for (; j < nn; j++) {
      char a = hay[i + j], b = needle[j];
      if (a >= 'A' && a <= 'Z') a = char(a + 32);
      if (b >= 'A' && b <= 'Z') b = char(b + 32);
      if (a != b) break;
    }
    if (j == nn) return true;
  }
  return false;
}
} // namespace

DeviceTier device_tier_classify(const char *gpu_name, uint32_t ram_mb) {
  // DURUM.md'de olculen gercek dusuk segment (Mali-G72, Huawei P20 Pro) dahil.
  static const char *kLowGpus[] = {"Mali-G31", "Mali-G51", "Mali-G52", "Mali-G57",
                                   "Mali-G68", "Mali-G72", "Adreno 5", "Adreno 6", "PowerVR GE"};
  static const char *kHighGpus[] = {"Mali-G715", "Mali-G720", "Mali-G725", "Mali-G925",
                                    "Mali-G2-Ultra", "Adreno 7", "Adreno 8", "Xclipse"};
  for (const char *g : kHighGpus)
    if (contains_ci(gpu_name, g)) return DeviceTier::High;
  for (const char *g : kLowGpus)
    if (contains_ci(gpu_name, g)) return DeviceTier::Low;
  if (ram_mb != 0 && ram_mb < 3072) return DeviceTier::Low;
  if (ram_mb >= 6144) return DeviceTier::High;
  return DeviceTier::Mid;
}

const TierProfile &device_tier_profile(DeviceTier t) { return kProfiles[static_cast<uint8_t>(t)]; }

} // namespace tulpar::engine::content
