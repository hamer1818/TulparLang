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
  // Normal haritasi IKI KANALLI saklanmis (astcenc MAP_NORMAL, girdi rrrg):
  // texel RGB'de X'in KOPYASI, alfada Y vardir; Z yeniden kurulur. G/B
  // kanallari COPTUR (kodlayici agirliklarini sifirlar — astcenc_entry.cpp).
  // Bunu bilmeden ornekleyen shader duz bir gri harita gorur; bu yuzden
  // konteynere YAZILIR (KTX2 key/value: "TULPAR_normalXY") ve okunur.
  bool normal_xy = false;
  const uint8_t *level_data[kMaxLevels] = {};
  uint32_t level_size[kMaxLevels] = {};
  char error[128] = {0};
};

// Dosyadan (Arena'ya kopyalanir). false: out->error.
bool ktx2_load(Arena &arena, const char *path, Ktx2Image *out);
bool ktx2_parse(Arena &arena, const uint8_t *bytes, size_t size, Ktx2Image *out);
// KOPYASIZ ayristirma (Faz 6 "ASTC dogrudan"): seviye isaretcileri `bytes`in
// icine bakar — arena kullanilmaz, AYIRMA YOK. Kaynak bellek (mmap'li pack
// blogu) goruntu yasadigi surece yasamali ve salt okunur olabilir: ASTC
// bloklari buradan dogrudan GPU'ya kopyalanir (ktx2_upload).
bool ktx2_parse_inplace(const uint8_t *bytes, size_t size, Ktx2Image *out);
// Yazici (arac): seviye verileri verilen bicimde.
bool ktx2_write(const char *path, VkFormat fmt, uint32_t w, uint32_t h, uint32_t levels, const uint8_t *const *data,
                const uint32_t *sizes, uint32_t block_w, uint32_t block_h, bool normal_xy = false);

// Dokunun NE OLDUGU: sikistiricinin hata olcutunu ve kanal duzenini belirler.
//   Color  : renk (albedo/isima) — sRGB profili, algisal agirliklar.
//   Data   : dogrusal veri (ORM: occlusion/roughness/metallic) — sRGB YOK,
//            algisal agirlik YOK; kanallar bagimsiz sayilardir, birlikte bir
//            renk olusturmazlar.
//   Normal : teget uzayi normali — ASTCENC_FLG_MAP_NORMAL, yani hata olcutu
//            DOGRUSAL PSNR degil ACISAL. Kodlayicinin istedigi kanal duzeni
//            rrrg (X parlaklikta, Y alfada); Z cozerken yeniden kurulur.
//            Ayni duzeni cozerken de kullaniyoruz, yani .ktx2'nin ICINDE
//            saklanan sey X ve Y'dir — kapi content_astc_normal_map_encoding
//            bunu acisal hatayla olcuyor.
enum class AstcMap : uint8_t { Color = 0, Data = 1, Normal = 2 };

// URUN KARARI — OLCUMLE VERILDI, ezberle degil. engine_texpack --tur normal
// hangi kipi kullanir? Sayilar ve gerekce: content_astc_normal_map_encoding
// kapisi (ayni kaynak, 4x4 / 6x6 / 8x8, acisal hata).
constexpr bool kAstcNormalUsesMapMode = true;

// ASTC blok verisini RGBA8'e cozer (CPU; belirlenimli, her platformda ayni).
// `map == Normal` YALNIZ OLCUM icindir: X'i R'den, Y'yi A'dan alip Z'yi kurar,
// yani GERCEK normali dondurur. YUKLEME yolunda kullanilmaz — donanim ASTC
// bloklari oldugu gibi ornekler ve (x,x,x,y) verir; CPU yedegi de ayni seyi
// vermeli, yoksa iki yol farkli goruntu uretir (ktx2_upload bu yuzden Data
// swizzle'i ile cozer).
bool astc_decode_rgba(Arena &arena, const uint8_t *blocks, uint32_t size, uint32_t w, uint32_t h, uint32_t block_w, uint32_t block_h,
                      bool srgb, uint8_t **out_rgba, AstcMap map = AstcMap::Color);
// ASTC sikistirma (arac; quality: ASTCENC_PRE_* 0..100).
bool astc_encode_rgba(Arena &arena, const uint8_t *rgba, uint32_t w, uint32_t h, uint32_t block_w, uint32_t block_h, bool srgb,
                      float quality, uint8_t **out_blocks, uint32_t *out_size, AstcMap map = AstcMap::Color);

struct Ktx2UploadInfo {
  bool hardware = false; // true: bloklar GPU'ya oldugu gibi; false: CPU'da cozuldu (RGBA8)
  uint32_t levels = 0;
  // Yuklenen doku iki kanalli normal haritasi: ornekleyen shader X'i .r'den,
  // Y'yi .a'dan almali ve Z'yi kurmali. Bugun motorun malzeme yolu (glTF ->
  // PNG -> RGB normal) bu duzeni KULLANMAZ; bayrak, sessizce yanlis ornekleyen
  // bir tuketici cikmasin diye tasiniyor.
  bool normal_xy = false;
};
// GPU'ya yukler: ASTC LDR destegi varsa dogrudan, yoksa CPU cozumu.
renderer::TextureHandle ktx2_upload(renderer::Renderer &r, const rhi::DeviceCaps &caps, Arena &arena, const Ktx2Image &img,
                                    Ktx2UploadInfo *info);

// Iki RGBA8 goruntu arasi PSNR (dB; RGB, alfa haric). Ayni = sonsuz (999).
double rgba_psnr(const uint8_t *a, const uint8_t *b, uint32_t w, uint32_t h);

} // namespace tulpar::engine::content
