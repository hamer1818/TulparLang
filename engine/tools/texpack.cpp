// engine_texpack — sahne derleyicisinin doku dilimi: PNG/JPG -> ASTC mip zinciri -> .ktx2
//   engine_texpack in.png out.ktx2 [--block 4x4|5x5|6x6|8x8] [--quality 0..100] [--linear]
// Mip'ler sRGB-dogru kutu filtreyle (dogrusal uzayda ortalama). Sonunda cozup PSNR basar (olcum).
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <stb_image.h>

#include "content/ktx2.hpp"
#include "core/memory/arena.hpp"

using namespace tulpar::engine;

namespace {
float srgb_to_lin(uint8_t c) { const float x = c / 255.0f; return x <= 0.04045f ? x / 12.92f : std::pow((x + 0.055f) / 1.055f, 2.4f); }
uint8_t lin_to_srgb(float x) {
  x = x < 0 ? 0 : x > 1 ? 1 : x;
  const float s = x <= 0.0031308f ? x * 12.92f : 1.055f * std::pow(x, 1.0f / 2.4f) - 0.055f;
  return (uint8_t)(s * 255.0f + 0.5f);
}
} // namespace

int main(int argc, char **argv) {
  if (argc < 3) { std::fprintf(stderr, "kullanim: engine_texpack in.png out.ktx2 [--block 4x4] [--quality 60] [--linear]\n"); return 2; }
  const char *in = argv[1], *out = argv[2];
  uint32_t bw = 4, bh = 4;
  float quality = 60.0f;
  bool srgb = true;
  for (int i = 3; i < argc; i++) {
    if (!std::strcmp(argv[i], "--block") && i + 1 < argc) { unsigned x = 4, y = 4; if (std::sscanf(argv[++i], "%ux%u", &x, &y) == 2) { bw = x; bh = y; } }
    else if (!std::strcmp(argv[i], "--quality") && i + 1 < argc) quality = (float)std::atof(argv[++i]);
    else if (!std::strcmp(argv[i], "--linear")) srgb = false;
  }
  static SystemArena sys;
  if (!sys.reserve(256u << 20, "texpack")) { std::fprintf(stderr, "arena\n"); return 1; }
  FILE *f = std::fopen(in, "rb");
  if (!f) { std::fprintf(stderr, "acilamadi: %s\n", in); return 1; }
  std::fseek(f, 0, SEEK_END); const long sz = std::ftell(f); std::fseek(f, 0, SEEK_SET);
  uint8_t *file = sys.alloc_array<uint8_t>((uint32_t)(sz > 0 ? sz : 1));
  const size_t got = sz > 0 ? std::fread(file, 1, (size_t)sz, f) : 0;
  std::fclose(f);
  int w = 0, h = 0, comp = 0;
  stbi_uc *rgba0 = stbi_load_from_memory(file, (int)got, &w, &h, &comp, 4);
  if (!rgba0 || w <= 0 || h <= 0) { std::fprintf(stderr, "goruntu cozulemedi: %s\n", in); return 1; }
  // Mip zinciri (kutu filtre, dogrusal uzay).
  const uint8_t *levels[content::Ktx2Image::kMaxLevels];
  uint32_t lw[content::Ktx2Image::kMaxLevels], lh[content::Ktx2Image::kMaxLevels];
  uint32_t nlev = 0;
  levels[0] = rgba0; lw[0] = (uint32_t)w; lh[0] = (uint32_t)h; nlev = 1;
  while ((lw[nlev - 1] > 1 || lh[nlev - 1] > 1) && nlev < content::Ktx2Image::kMaxLevels) {
    const uint32_t pw = lw[nlev - 1], ph = lh[nlev - 1];
    const uint32_t nw = pw > 1 ? pw / 2 : 1, nh = ph > 1 ? ph / 2 : 1;
    uint8_t *dst = sys.alloc_array<uint8_t>(nw * nh * 4);
    const uint8_t *src = levels[nlev - 1];
    for (uint32_t y = 0; y < nh; y++)
      for (uint32_t x = 0; x < nw; x++) {
        float acc[4] = {0, 0, 0, 0};
        int cnt = 0;
        for (uint32_t dy = 0; dy < 2; dy++)
          for (uint32_t dx = 0; dx < 2; dx++) {
            const uint32_t sx = x * 2 + dx < pw ? x * 2 + dx : pw - 1, sy = y * 2 + dy < ph ? y * 2 + dy : ph - 1;
            const uint8_t *p = src + (sy * pw + sx) * 4;
            for (int c = 0; c < 3; c++) acc[c] += srgb ? srgb_to_lin(p[c]) : p[c] / 255.0f;
            acc[3] += p[3] / 255.0f;
            cnt++;
          }
        uint8_t *o = dst + (y * nw + x) * 4;
        for (int c = 0; c < 3; c++) o[c] = srgb ? lin_to_srgb(acc[c] / (float)cnt) : (uint8_t)(acc[c] / (float)cnt * 255.0f + 0.5f);
        o[3] = (uint8_t)(acc[3] / (float)cnt * 255.0f + 0.5f);
      }
    levels[nlev] = dst; lw[nlev] = nw; lh[nlev] = nh; nlev++;
  }
  const uint8_t *blocks[content::Ktx2Image::kMaxLevels];
  uint32_t sizes[content::Ktx2Image::kMaxLevels];
  uint32_t total = 0;
  for (uint32_t i = 0; i < nlev; i++) {
    uint8_t *b = nullptr; uint32_t n = 0;
    if (!content::astc_encode_rgba(sys, levels[i], lw[i], lh[i], bw, bh, srgb, quality, &b, &n)) { std::fprintf(stderr, "ASTC sikistirma (seviye %u)\n", i); return 1; }
    blocks[i] = b; sizes[i] = n; total += n;
  }
  VkFormat fmt = VK_FORMAT_ASTC_4x4_SRGB_BLOCK;
  if (bw == 4) fmt = srgb ? VK_FORMAT_ASTC_4x4_SRGB_BLOCK : VK_FORMAT_ASTC_4x4_UNORM_BLOCK;
  else if (bw == 5) fmt = srgb ? VK_FORMAT_ASTC_5x5_SRGB_BLOCK : VK_FORMAT_ASTC_5x5_UNORM_BLOCK;
  else if (bw == 6) fmt = srgb ? VK_FORMAT_ASTC_6x6_SRGB_BLOCK : VK_FORMAT_ASTC_6x6_UNORM_BLOCK;
  else if (bw == 8) fmt = srgb ? VK_FORMAT_ASTC_8x8_SRGB_BLOCK : VK_FORMAT_ASTC_8x8_UNORM_BLOCK;
  else { std::fprintf(stderr, "desteklenmeyen blok %ux%u\n", bw, bh); return 1; }
  if (!content::ktx2_write(out, fmt, (uint32_t)w, (uint32_t)h, nlev, blocks, sizes, bw, bh)) { std::fprintf(stderr, "yazilamadi: %s\n", out); return 1; }
  // Olcum: seviye 0 coz, PSNR.
  uint8_t *dec = nullptr;
  double psnr = 0;
  if (content::astc_decode_rgba(sys, blocks[0], sizes[0], (uint32_t)w, (uint32_t)h, bw, bh, srgb, &dec)) psnr = content::rgba_psnr(rgba0, dec, (uint32_t)w, (uint32_t)h);
  std::printf("%s: %dx%d, %u seviye, ASTC %ux%u %s, kalite %.0f -> %u bayt (RGBA8 %u bayt, %.1fx), seviye0 PSNR %.1f dB\n", out, w, h, nlev, bw, bh,
              srgb ? "sRGB" : "dogrusal", quality, total, (uint32_t)(w * h * 4), (double)(w * h * 4) / (double)(sizes[0] ? sizes[0] : 1), psnr);
  stbi_image_free(rgba0);
  return 0;
}
