// L2 RHI — Faz 1 "ilk piksel": pencere ACMADAN, subpass zincirli (depth
// prepass -> renk) bir render pass ile ucgen cizip pikselleri geri okur.
// Kapilar burada olculur: piksel dogrulugu, GPU zaman damgasi, PSO cache
// yukleme/kaydetme, VK_EXT_subpass_merge_feedback (varsa) birlesme durumu.
// Depth transient (STORE_OP_DONT_CARE, mumkunse LAZILY_ALLOCATED): TBDR'da
// tile'da kalir, DRAM'e inmez — plan §1.2 "zorunlu" maddesinin ilk kullanimi.
#pragma once
#include <cstdint>

#include "core/memory/arena.hpp"
#include "rhi/device.hpp"

namespace tulpar::engine::rhi {

struct OffscreenConfig {
  uint32_t width = 256;
  uint32_t height = 256;
  uint8_t clear[4] = {10, 20, 30, 255};
  const char *pso_cache_path = nullptr; // varsa yukle, sonunda yaz
};

struct OffscreenResult {
  bool ok = false;
  char error[512] = {0};
  uint8_t *pixels = nullptr; // RGBA8, arenadan, width*height*4
  uint64_t gpu_ns = 0;       // render pass suresi (zaman damgasi), 0 = yok
  // Subpass birlesme geri bildirimi (VK_EXT_subpass_merge_feedback):
  bool merge_feedback_available = false;
  uint32_t post_merge_subpass_count = 0; // 1 = iki subpass birlesti
  int32_t subpass_merge_status[2] = {-1, -1}; // VkSubpassMergeStatusEXT
  // PSO cache:
  bool pso_cache_loaded = false; // dosya vardi ve baslik cihazla eslesti
  uint64_t pso_cache_bytes = 0;  // yazilan
  uint32_t memory_allocations = 0; // vkAllocateMemory sayisi (cihaz toplam)
};

bool render_triangle_offscreen(Device &dev, Arena &arena, const OffscreenConfig &cfg,
                               OffscreenResult *out);

// Basit PPM (P6) yazici — insan gozu icin; test artefakti.
bool write_ppm(const char *path, const uint8_t *rgba, uint32_t w, uint32_t h);

} // namespace tulpar::engine::rhi
