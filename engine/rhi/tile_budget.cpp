#include "rhi/tile_budget.hpp"

#include <cstdio>

namespace tulpar::engine::rhi {

uint32_t format_bits(VkFormat f) {
  switch (f) {
  case VK_FORMAT_R8_UNORM: case VK_FORMAT_R8_SRGB: case VK_FORMAT_R8_UINT: case VK_FORMAT_S8_UINT: return 8;
  case VK_FORMAT_R8G8_UNORM: case VK_FORMAT_R16_UNORM: case VK_FORMAT_R16_SFLOAT: case VK_FORMAT_D16_UNORM:
  case VK_FORMAT_R5G6B5_UNORM_PACK16: case VK_FORMAT_R16_UINT: return 16;
  case VK_FORMAT_D16_UNORM_S8_UINT: return 24;
  case VK_FORMAT_R8G8B8A8_UNORM: case VK_FORMAT_R8G8B8A8_SRGB: case VK_FORMAT_B8G8R8A8_UNORM:
  case VK_FORMAT_B8G8R8A8_SRGB: case VK_FORMAT_A2B10G10R10_UNORM_PACK32: case VK_FORMAT_A2R10G10B10_UNORM_PACK32:
  case VK_FORMAT_B10G11R11_UFLOAT_PACK32: case VK_FORMAT_R16G16_SFLOAT: case VK_FORMAT_R16G16_UNORM:
  case VK_FORMAT_R32_SFLOAT: case VK_FORMAT_R32_UINT: case VK_FORMAT_D32_SFLOAT: case VK_FORMAT_D24_UNORM_S8_UINT:
  case VK_FORMAT_X8_D24_UNORM_PACK32: case VK_FORMAT_R8G8B8A8_UINT: return 32;
  case VK_FORMAT_D32_SFLOAT_S8_UINT: return 40;
  case VK_FORMAT_R16G16B16A16_SFLOAT: case VK_FORMAT_R16G16B16A16_UNORM: case VK_FORMAT_R32G32_SFLOAT:
  case VK_FORMAT_R32G32_UINT: case VK_FORMAT_R16G16B16A16_UINT: return 64;
  case VK_FORMAT_R32G32B32A32_SFLOAT: case VK_FORMAT_R32G32B32A32_UINT: return 128;
  default: return 0;
  }
}

bool format_is_depth(VkFormat f) {
  switch (f) {
  case VK_FORMAT_D16_UNORM: case VK_FORMAT_D16_UNORM_S8_UINT: case VK_FORMAT_D32_SFLOAT:
  case VK_FORMAT_D24_UNORM_S8_UINT: case VK_FORMAT_X8_D24_UNORM_PACK32: case VK_FORMAT_D32_SFLOAT_S8_UINT:
  case VK_FORMAT_S8_UINT: return true;
  default: return false;
  }
}

TileBudget tile_budget(const VkFormat *fmts, uint32_t n) {
  TileBudget b;
  for (uint32_t i = 0; i < n; i++) {
    const uint32_t bits = format_bits(fmts[i]);
    if (bits == 0) {
      std::snprintf(b.error, sizeof b.error, "bilinmeyen attachment bicimi %d (tile butcesi hesaplanamadi)", (int)fmts[i]);
      return b;
    }
    if (format_is_depth(fmts[i])) { b.depth_bits += bits; continue; }
    b.attachments++;
    b.color_bits += bits;
  }
  if (b.attachments > kTileMaxAttachments) {
    std::snprintf(b.error, sizeof b.error, "tile butcesi: %u attachment > %u (Mali birlestirmez)", b.attachments,
                  kTileMaxAttachments);
    return b;
  }
  if (b.color_bits > kTileMaxColorBits) {
    std::snprintf(b.error, sizeof b.error, "tile butcesi: %u bit/px renk > %u (Mali birlestirmez)", b.color_bits,
                  kTileMaxColorBits);
    return b;
  }
  b.ok = true;
  return b;
}

} // namespace tulpar::engine::rhi
