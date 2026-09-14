// Tulpar Engine — L2 RHI: Mali tile butcesi (tasarim kisiti, kodla zorlanir).
//
// Arm/Khronos Vulkan-Samples (subpass birlestirme kosullari, olculmus):
//   - zincirdeki benzersiz renk + girdi attachment sayisi <= 8
//   - tile renk deposu <= 128 bit/piksel (derinlik ayri tile bellegi)
// Asilirsa surucu subpass'lari BIRLESTIRMEZ: her gecis tile'i belege yazar,
// TBDR'nin butun kazanci gider. Bunu tasarimdan sonra fark etmek butun gecis
// zincirini yeniden kurmak demek; o yuzden gecis YARATILIRKEN denetlenir ve
// asim init hatasidir (layer_check.py'nin GPU esi). Bilinmeyen bicim = 0 bit
// degil, HATA: sessiz gecis yok.
#pragma once
#include <cstdint>
#include <vulkan/vulkan.h>

namespace tulpar::engine::rhi {

constexpr uint32_t kTileMaxAttachments = 8;
constexpr uint32_t kTileMaxColorBits = 128;

struct TileBudget {
  uint32_t attachments = 0;    // renk + girdi (derinlik haric)
  uint32_t color_bits = 0;     // bit / piksel
  uint32_t depth_bits = 0;     // bilgi (butceye girmez)
  bool ok = false;
  char error[96] = {0};
};

// Bilinen bicimin piksel basina biti; bilinmeyen 0.
uint32_t format_bits(VkFormat f);
bool format_is_depth(VkFormat f);

// fmts: gecisin BUTUN attachment bicimleri (derinlik dahil; ayrilir).
TileBudget tile_budget(const VkFormat *fmts, uint32_t n);

} // namespace tulpar::engine::rhi
