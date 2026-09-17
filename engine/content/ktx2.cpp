#include "content/ktx2.hpp"

#include <cmath>
#include <cstdio>
#include <cstring>

#include <astcenc.h>

namespace tulpar::engine::content {

namespace {
const uint8_t kKtx2Id[12] = {0xAB, 0x4B, 0x54, 0x58, 0x20, 0x32, 0x30, 0xBB, 0x0D, 0x0A, 0x1A, 0x0A};
uint32_t rd32(const uint8_t *p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }
uint64_t rd64(const uint8_t *p) { return (uint64_t)rd32(p) | ((uint64_t)rd32(p + 4) << 32); }
void wr32(uint8_t *p, uint32_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24); }
void wr64(uint8_t *p, uint64_t v) { wr32(p, (uint32_t)v); wr32(p + 4, (uint32_t)(v >> 32)); }
bool astc_format_info(VkFormat f, uint32_t *bw, uint32_t *bh, bool *srgb) {
  switch (f) {
  case VK_FORMAT_ASTC_4x4_SRGB_BLOCK: *bw = 4; *bh = 4; *srgb = true; return true;
  case VK_FORMAT_ASTC_4x4_UNORM_BLOCK: *bw = 4; *bh = 4; *srgb = false; return true;
  case VK_FORMAT_ASTC_5x5_SRGB_BLOCK: *bw = 5; *bh = 5; *srgb = true; return true;
  case VK_FORMAT_ASTC_5x5_UNORM_BLOCK: *bw = 5; *bh = 5; *srgb = false; return true;
  case VK_FORMAT_ASTC_6x6_SRGB_BLOCK: *bw = 6; *bh = 6; *srgb = true; return true;
  case VK_FORMAT_ASTC_6x6_UNORM_BLOCK: *bw = 6; *bh = 6; *srgb = false; return true;
  case VK_FORMAT_ASTC_8x8_SRGB_BLOCK: *bw = 8; *bh = 8; *srgb = true; return true;
  case VK_FORMAT_ASTC_8x8_UNORM_BLOCK: *bw = 8; *bh = 8; *srgb = false; return true;
  default: return false;
  }
}
} // namespace

namespace {
// Tek ayristirici: arena verilirse seviyeler KOPYALANIR, verilmezse
// isaretciler kaynagin icine bakar (kopyasiz yol).
bool ktx2_parse_common(Arena *arena, const uint8_t *b, size_t n, Ktx2Image *out) {
  *out = Ktx2Image{};
  if (n < 80 || std::memcmp(b, kKtx2Id, 12) != 0) { std::snprintf(out->error, sizeof out->error, "KTX2 kimligi yok"); return false; }
  out->vk_format = (VkFormat)rd32(b + 12);
  const uint32_t type_size = rd32(b + 16);
  out->width = rd32(b + 20);
  out->height = rd32(b + 24);
  const uint32_t depth = rd32(b + 28), layers = rd32(b + 32), faces = rd32(b + 36);
  out->levels = rd32(b + 40);
  const uint32_t supercomp = rd32(b + 44);
  (void)type_size;
  if (depth > 1 || layers > 1 || faces != 1) { std::snprintf(out->error, sizeof out->error, "yalniz 2B tek yuz (depth %u, layer %u, face %u)", depth, layers, faces); return false; }
  if (supercomp != 0) { std::snprintf(out->error, sizeof out->error, "supercompression %u desteklenmiyor (Basis/zstd yok)", supercomp); return false; }
  if (out->levels == 0 || out->levels > Ktx2Image::kMaxLevels || out->width == 0 || out->height == 0) {
    std::snprintf(out->error, sizeof out->error, "gecersiz boyut/seviye (%ux%u, %u)", out->width, out->height, out->levels); return false;
  }
  uint32_t bw = 1, bh = 1; bool srgb = false;
  if (astc_format_info(out->vk_format, &bw, &bh, &srgb)) { out->astc = true; out->block_w = bw; out->block_h = bh; out->srgb = srgb; }
  else if (out->vk_format == VK_FORMAT_R8G8B8A8_SRGB) { out->srgb = true; }
  else if (out->vk_format == VK_FORMAT_R8G8B8A8_UNORM) { out->srgb = false; }
  else { std::snprintf(out->error, sizeof out->error, "desteklenmeyen vkFormat %u", (unsigned)out->vk_format); return false; }
  // KTX2 key/value bolumu: "TULPAR_normalXY" = "1" -> iki kanalli normal
  // haritasi. Bolum yoksa (ya da anahtar yoksa) eski davranis: RGB duzeni.
  {
    const uint32_t kvd_off = rd32(b + 56), kvd_len = rd32(b + 60);
    if (kvd_off && kvd_len && (size_t)kvd_off + kvd_len <= n) {
      const uint8_t *p = b + kvd_off;
      size_t left = kvd_len;
      while (left >= 4) {
        const uint32_t klen = rd32(p);
        if (klen == 0 || (size_t)klen + 4 > left) break;
        const char *kv = (const char *)(p + 4);
        size_t klen_s = 0; // strnlen POSIX; standart C++ degil
        while (klen_s < klen && kv[klen_s]) klen_s++;
        if (klen_s < klen && klen_s == 15 && std::memcmp(kv, "TULPAR_normalXY", 15) == 0 && kv[klen_s + 1] == '1')
          out->normal_xy = true;
        const uint32_t adv = (4u + klen + 3u) & ~3u; // 4 bayt hizali
        if (adv > left) break;
        p += adv;
        left -= adv;
      }
    }
  }
  const size_t index_off = 80;
  if (n < index_off + out->levels * 24) { std::snprintf(out->error, sizeof out->error, "seviye indeksi kesik"); return false; }
  for (uint32_t i = 0; i < out->levels; i++) {
    const uint8_t *e = b + index_off + i * 24;
    const uint64_t off = rd64(e), len = rd64(e + 8);
    if (off + len > n || len == 0 || len > 0xFFFFFFFFu) { std::snprintf(out->error, sizeof out->error, "seviye %u dosya disinda", i); return false; }
    if (arena) {
      uint8_t *copy = arena->alloc_array<uint8_t>((uint32_t)len);
      if (!copy) { std::snprintf(out->error, sizeof out->error, "arena dolu"); return false; }
      std::memcpy(copy, b + off, (size_t)len);
      out->level_data[i] = copy;
    } else {
      out->level_data[i] = b + off; // kopyasiz: kaynagin icine isaretci
    }
    out->level_size[i] = (uint32_t)len;
  }
  return true;
}
} // namespace

bool ktx2_parse(Arena &arena, const uint8_t *b, size_t n, Ktx2Image *out) { return ktx2_parse_common(&arena, b, n, out); }
bool ktx2_parse_inplace(const uint8_t *b, size_t n, Ktx2Image *out) { return ktx2_parse_common(nullptr, b, n, out); }

bool ktx2_load(Arena &arena, const char *path, Ktx2Image *out) {
  *out = Ktx2Image{};
  FILE *f = std::fopen(path, "rb");
  if (!f) { std::snprintf(out->error, sizeof out->error, "acilamadi: %s", path); return false; }
  std::fseek(f, 0, SEEK_END);
  const long sz = std::ftell(f);
  std::fseek(f, 0, SEEK_SET);
  if (sz <= 0) { std::fclose(f); std::snprintf(out->error, sizeof out->error, "bos dosya"); return false; }
  uint8_t *buf = arena.alloc_array<uint8_t>((uint32_t)sz);
  if (!buf) { std::fclose(f); std::snprintf(out->error, sizeof out->error, "arena dolu"); return false; }
  const size_t got = std::fread(buf, 1, (size_t)sz, f);
  std::fclose(f);
  return ktx2_parse(arena, buf, got, out);
}

bool ktx2_write(const char *path, VkFormat fmt, uint32_t w, uint32_t h, uint32_t levels, const uint8_t *const *data,
                const uint32_t *sizes, uint32_t block_w, uint32_t block_h, bool normal_xy) {
  if (levels == 0 || levels > Ktx2Image::kMaxLevels) return false;
  // DFD (temel blok, 1 ornek; 44 bayt) — okuyucumuz kullanmaz, konteyner gecerli olsun.
  uint8_t dfd[48] = {0};
  const uint32_t dfd_total = 4 + 24 + 16;
  wr32(dfd, dfd_total);
  wr32(dfd + 4, 0);                        // vendorId 0 (Khronos) | descriptorType 0
  wr32(dfd + 8, (2u) | (40u << 16));       // versionNumber 2 | descriptorBlockSize 40
  const bool astc = block_w > 1 || block_h > 1;
  dfd[12] = astc ? 162 : 1;                // colorModel: ASTC=162, RGBSDA=1
  dfd[13] = 1;                             // colorPrimaries BT709
  dfd[14] = (fmt == VK_FORMAT_ASTC_4x4_SRGB_BLOCK || fmt == VK_FORMAT_ASTC_6x6_SRGB_BLOCK || fmt == VK_FORMAT_ASTC_8x8_SRGB_BLOCK ||
             fmt == VK_FORMAT_ASTC_5x5_SRGB_BLOCK || fmt == VK_FORMAT_R8G8B8A8_SRGB) ? 2 : 1; // transfer: sRGB=2, linear=1
  dfd[15] = 1;                             // flags: alpha straight
  dfd[16] = (uint8_t)(block_w - 1); dfd[17] = (uint8_t)(block_h - 1); dfd[18] = 0; dfd[19] = 0;
  dfd[20] = astc ? 16 : 4;                 // bytesPlane0
  // ornek: bitOffset 0, bitLength (128 ya da 32) - 1, channelType RGBA (15) | qualifiers 0
  wr32(dfd + 28, (0u) | (((astc ? 128u : 32u) - 1u) << 16));
  wr32(dfd + 32, (15u << 24));
  wr32(dfd + 36, 0); wr32(dfd + 40, 0xFFFFFFFFu);
  // KVD: iki kanalli normal haritasi isareti. KTX2 duzeni — her giris
  // [uint32 uzunluk][anahtar\0deger\0], 4 bayt hizali dolgu.
  static const char kNormalKey[] = "TULPAR_normalXY";
  uint8_t kvd[32] = {0};
  uint32_t kvd_total = 0;
  if (normal_xy) {
    const uint32_t klen = (uint32_t)sizeof kNormalKey + 2; // "TULPAR_normalXY\0" + "1\0"
    wr32(kvd, klen);
    std::memcpy(kvd + 4, kNormalKey, sizeof kNormalKey);
    kvd[4 + sizeof kNormalKey] = '1';
    kvd[5 + sizeof kNormalKey] = 0;
    kvd_total = (4u + klen + 3u) & ~3u;
  }
  const uint64_t header_index = 80 + (uint64_t)levels * 24;
  const uint64_t dfd_off = header_index;
  const uint64_t kvd_off = dfd_off + dfd_total;
  uint64_t data_off = kvd_off + kvd_total;
  data_off = (data_off + 15) & ~15ull;
  uint8_t hdr[80 + 16 * 24] = {0};
  std::memcpy(hdr, kKtx2Id, 12);
  wr32(hdr + 12, (uint32_t)fmt);
  wr32(hdr + 16, 1);            // typeSize
  wr32(hdr + 20, w); wr32(hdr + 24, h); wr32(hdr + 28, 0); // depth 0 (2B)
  wr32(hdr + 32, 0);            // layerCount 0 (dizi degil)
  wr32(hdr + 36, 1);            // faceCount
  wr32(hdr + 40, levels);
  wr32(hdr + 44, 0);            // supercompression yok
  wr32(hdr + 48, (uint32_t)dfd_off); wr32(hdr + 52, dfd_total);
  wr32(hdr + 56, kvd_total ? (uint32_t)kvd_off : 0u); wr32(hdr + 60, kvd_total); // kvd
  wr64(hdr + 64, 0); wr64(hdr + 72, 0); // sgd
  // KTX2: seviyeler dosyada EN KUCUKTEN buyuge sirali (son mip once).
  uint64_t offs[Ktx2Image::kMaxLevels];
  uint64_t cur = data_off;
  for (int i = (int)levels - 1; i >= 0; i--) { cur = (cur + 15) & ~15ull; offs[i] = cur; cur += sizes[i]; }
  for (uint32_t i = 0; i < levels; i++) {
    uint8_t *e = hdr + 80 + i * 24;
    wr64(e, offs[i]); wr64(e + 8, sizes[i]); wr64(e + 16, sizes[i]);
  }
  FILE *f = std::fopen(path, "wb");
  if (!f) return false;
  bool ok = std::fwrite(hdr, 1, (size_t)header_index, f) == (size_t)header_index;
  ok = ok && std::fwrite(dfd, 1, dfd_total, f) == dfd_total;
  if (ok && kvd_total) ok = std::fwrite(kvd, 1, kvd_total, f) == kvd_total;
  uint64_t pos = kvd_off + kvd_total;
  static const uint8_t zero[16] = {0};
  for (int i = (int)levels - 1; i >= 0 && ok; i--) {
    while (pos < offs[i]) { std::fputc(0, f); pos++; }
    ok = std::fwrite(data[i], 1, sizes[i], f) == sizes[i];
    pos += sizes[i];
  }
  (void)zero;
  std::fclose(f);
  return ok;
}

bool astc_decode_rgba(Arena &arena, const uint8_t *blocks, uint32_t size, uint32_t w, uint32_t h, uint32_t block_w, uint32_t block_h,
                      bool srgb, uint8_t **out_rgba, AstcMap map) {
  astcenc_config cfg;
  if (astcenc_config_init(srgb ? ASTCENC_PRF_LDR_SRGB : ASTCENC_PRF_LDR, block_w, block_h, 1, ASTCENC_PRE_MEDIUM,
                          ASTCENC_FLG_DECOMPRESS_ONLY, &cfg) != ASTCENC_SUCCESS) return false;
  astcenc_context *ctx = nullptr;
  if (astcenc_context_alloc(&cfg, 1, &ctx, nullptr) != ASTCENC_SUCCESS) return false;
  uint8_t *rgba = arena.alloc_array<uint8_t>(w * h * 4);
  if (!rgba) { astcenc_context_free(ctx); return false; }
  void *slices[1] = {rgba};
  astcenc_image img{w, h, 1, ASTCENC_TYPE_U8, slices};
  // Normal haritasi rrrg olarak SAKLANIR (X parlaklikta, Y alfada): cozerken
  // X'i R'den, Y'yi A'dan al ve Z'yi yeniden kur (ASTCENC_SWZ_Z).
  const astcenc_swizzle sw = map == AstcMap::Normal
                                 ? astcenc_swizzle{ASTCENC_SWZ_R, ASTCENC_SWZ_A, ASTCENC_SWZ_Z, ASTCENC_SWZ_1}
                                 : astcenc_swizzle{ASTCENC_SWZ_R, ASTCENC_SWZ_G, ASTCENC_SWZ_B, ASTCENC_SWZ_A};
  const astcenc_error e = astcenc_decompress_image(ctx, blocks, size, &img, &sw, 0);
  astcenc_context_free(ctx);
  if (e != ASTCENC_SUCCESS) return false;
  *out_rgba = rgba;
  return true;
}

bool astc_encode_rgba(Arena &arena, const uint8_t *rgba, uint32_t w, uint32_t h, uint32_t block_w, uint32_t block_h, bool srgb,
                      float quality, uint8_t **out_blocks, uint32_t *out_size, AstcMap map) {
  astcenc_config cfg;
  // Normal haritasi asla sRGB profiliyle sikistirilmaz (veri, renk degil);
  // cagiran yanlislikla srgb=true verse bile burada duselir.
  const astcenc_profile prof = (srgb && map == AstcMap::Color) ? ASTCENC_PRF_LDR_SRGB : ASTCENC_PRF_LDR;
  const unsigned flags = map == AstcMap::Normal ? ASTCENC_FLG_MAP_NORMAL : 0u;
  if (astcenc_config_init(prof, block_w, block_h, 1, quality, flags, &cfg) != ASTCENC_SUCCESS) return false;
  astcenc_context *ctx = nullptr;
  if (astcenc_context_alloc(&cfg, 1, &ctx, nullptr) != ASTCENC_SUCCESS) return false;
  const uint32_t bx = (w + block_w - 1) / block_w, by = (h + block_h - 1) / block_h;
  const uint32_t size = bx * by * 16;
  uint8_t *blocks = arena.alloc_array<uint8_t>(size);
  if (!blocks) { astcenc_context_free(ctx); return false; }
  void *slices[1] = {const_cast<uint8_t *>(rgba)};
  astcenc_image img{w, h, 1, ASTCENC_TYPE_U8, slices};
  // MAP_NORMAL iki kanalli calisir ve girdi duzenini rrrg ister (astcenc.h):
  // X uc renk kanalinda, Y alfada. Cozme swizzle'i bunun tersidir.
  const astcenc_swizzle sw = map == AstcMap::Normal
                                 ? astcenc_swizzle{ASTCENC_SWZ_R, ASTCENC_SWZ_R, ASTCENC_SWZ_R, ASTCENC_SWZ_G}
                                 : astcenc_swizzle{ASTCENC_SWZ_R, ASTCENC_SWZ_G, ASTCENC_SWZ_B, ASTCENC_SWZ_A};
  const astcenc_error e = astcenc_compress_image(ctx, &img, &sw, blocks, size, 0);
  astcenc_context_free(ctx);
  if (e != ASTCENC_SUCCESS) return false;
  *out_blocks = blocks;
  *out_size = size;
  return true;
}

renderer::TextureHandle ktx2_upload(renderer::Renderer &r, const rhi::DeviceCaps &caps, Arena &arena, const Ktx2Image &img,
                                    Ktx2UploadInfo *info) {
  Ktx2UploadInfo tmp;
  if (!info) info = &tmp;
  info->levels = img.levels;
  info->normal_xy = img.normal_xy;
  if (!img.astc || caps.texture_compression_astc_ldr) {
    info->hardware = true;
    return r.create_texture_levels(img.vk_format, img.width, img.height, img.levels, img.level_data, img.level_size);
  }
  // CPU cozumu: her seviye RGBA8 (ayni mip zinciri; blit yok).
  info->hardware = false;
  const uint8_t *data[Ktx2Image::kMaxLevels];
  uint32_t sizes[Ktx2Image::kMaxLevels];
  uint32_t w = img.width, h = img.height;
  for (uint32_t i = 0; i < img.levels; i++) {
    uint8_t *rgba = nullptr;
    // AstcMap::Data (duz swizzle) BILEREK: donanim yolu bloklari oldugu gibi
    // ornekler ve iki kanalli normal haritasinda (x,x,x,y) verir. CPU yedegi
    // normali "kursaydi" (x,y,z) verirdi ve IKI YOL FARKLI GORUNURDU — sessiz
    // ayrisma. Ayni duzen, iki yol.
    if (!astc_decode_rgba(arena, img.level_data[i], img.level_size[i], w, h, img.block_w, img.block_h, img.srgb, &rgba,
                          AstcMap::Data))
      return renderer::TextureHandle{};
    data[i] = rgba; sizes[i] = w * h * 4;
    w = w > 1 ? w / 2 : 1; h = h > 1 ? h / 2 : 1;
  }
  return r.create_texture_levels(img.srgb ? VK_FORMAT_R8G8B8A8_SRGB : VK_FORMAT_R8G8B8A8_UNORM, img.width, img.height, img.levels, data, sizes);
}

double rgba_psnr(const uint8_t *a, const uint8_t *b, uint32_t w, uint32_t h) {
  double se = 0;
  for (uint32_t i = 0; i < w * h; i++)
    for (int c = 0; c < 3; c++) { const double d = (double)a[i * 4 + c] - (double)b[i * 4 + c]; se += d * d; }
  const double mse = se / (double)(w * h * 3);
  if (mse <= 0) return 999.0;
  return 10.0 * std::log10(255.0 * 255.0 / mse);
}

} // namespace tulpar::engine::content
