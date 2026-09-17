// L6 CONTENT — Cihaz-sinifi profil tablosu (420 madde listesi #6/#395:
// "Cihaz-sinifi bake"). Ayni sahnenin dusuk/orta/yuksek segment icin farkli
// kalite ayarlariyla YUKLEME ANINDA (runtime degil) calismasi -- NetEase
// Messiah'in "cihaza gore kararli FPS" felsefesiyle ayni yonde (VIZYON.md
// S0). Ilk dilim: GPU adi + toplam RAM'den TEK SEFER tier secilir; "quality
// settings" kavrami runtime'da degismez (VIZYON.md S2.5'teki bahisle ayni).
#pragma once
#include <cstdint>

namespace tulpar::engine::content {

enum class DeviceTier : uint8_t { Low = 0, Mid = 1, High = 2 };

struct TierProfile {
  uint32_t shadow_map_size;  // rhi golge gecisi, kare (px)
  uint32_t max_point_lights; // kumeleme ust siniri
  float exposure_ev;         // Renderer::set_exposure_ev baslangic degeri
  bool enable_fog;           // dusuk segmentte analitik sis bile kapatilabilir
  uint32_t msaa_samples;     // 1 = kapali (MSAA hala ertelendi, profil simdiden tasir)
};

// Bilinen GPU adi alt-dizgileri (buyuk/kucuk harf farketmez) + toplam RAM
// (MB, bilinmiyorsa 0) -> tier. Bilinmeyen GPU + ram_mb==0 => Mid (guvenli
// orta karar); bilinmeyen GPU + ram_mb>=6144 => High (iyimser varsayilan).
DeviceTier device_tier_classify(const char *gpu_name, uint32_t ram_mb);

const TierProfile &device_tier_profile(DeviceTier t);

} // namespace tulpar::engine::content
