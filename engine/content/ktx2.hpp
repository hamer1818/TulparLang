// L6 CONTENT — KTX2 konteyneri (Khronos, kendi okuyucu/yazicimiz: ~200 satir;
// libktx'in Basis/zstd yolu yok, supercompression 0) + ASTC (astc-encoder,
// vendored, Apache-2.0). Mobil dokunun asil yolu (plan Faz 6): sahne derleyicisi
// (engine_texpack) PNG -> ASTC mip zinciri -> .ktx2; runtime GPU ASTC LDR
// destekliyorsa bloklari oldugu gibi yukler (Mali/Adreno), yoksa CPU'da cozer
// (masaustu NVIDIA) — ayni goruntu, iki yol da PSNR ile sinanir.
#pragma once
#include <cstdint>
#include <vulkan/vulkan.h>

#include "core/memory/arena.hpp"
#include "renderer/renderer.hpp"
#include "rhi/device.hpp"

namespace tulpar::engine::content {

struct Ktx2Image {
  static constexpr uint32_t kMaxLevels = 16;
  VkFormat vk_format = VK_FORMAT_UNDEFINED;
  uint32_t width = 0, height = 0, levels = 0;
  uint32_t block_w = 1, block_h = 1; // ASTC blok; sikistirilmamis 1x1
  bool astc = false, srgb = false;
  const uint8_t *level_data[kMaxLevels] = {};
  uint32_t level_size[kMaxLevels] = {};
  char error[128] = {0};
};

// Dosyadan (Arena'ya kopyalanir). false: out->error.
bool ktx2_load(Arena &arena, const char *path, Ktx2Image *out);
bool ktx2_parse(Arena &arena, const uint8_t *bytes, size_t size, Ktx2Image *out);
// Yazici (arac): seviye verileri verilen bicimde.
bool ktx2_write(const char *path, VkFormat fmt, uint32_t w, uint32_t h, uint32_t levels, const uint8_t *const *data,
                const uint32_t *sizes, uint32_t block_w, uint32_t block_h);

// ASTC blok verisini RGBA8'e cozer (CPU; belirlenimli, her platformda ayni).
bool astc_decode_rgba(Arena &arena, const uint8_t *blocks, uint32_t size, uint32_t w, uint32_t h, uint32_t block_w, uint32_t block_h,
                      bool srgb, uint8_t **out_rgba);
// ASTC sikistirma (arac; quality: ASTCENC_PRE_* 0..100).
bool astc_encode_rgba(Arena &arena, const uint8_t *rgba, uint32_t w, uint32_t h, uint32_t block_w, uint32_t block_h, bool srgb,
                      float quality, uint8_t **out_blocks, uint32_t *out_size);

struct Ktx2UploadInfo {
  bool hardware = false; // true: bloklar GPU'ya oldugu gibi; false: CPU'da cozuldu (RGBA8)
  uint32_t levels = 0;
};
// GPU'ya yukler: ASTC LDR destegi varsa dogrudan, yoksa CPU cozumu.
renderer::TextureHandle ktx2_upload(renderer::Renderer &r, const rhi::DeviceCaps &caps, Arena &arena, const Ktx2Image &img,
                                    Ktx2UploadInfo *info);

// Iki RGBA8 goruntu arasi PSNR (dB; RGB, alfa haric). Ayni = sonsuz (999).
double rgba_psnr(const uint8_t *a, const uint8_t *b, uint32_t w, uint32_t h);

} // namespace tulpar::engine::content
