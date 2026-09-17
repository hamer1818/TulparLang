// L6 CONTENT — TTF'ten glif atlasi (stb_truetype, yukleme aninda) ve UTF-8
// metin cizimi (renderer UI dortgenleri). Kapsam: ASCII + Latin-1 + Turkce
// harfler (ğ ı ş Ğ İ Ş). Atlas RGBA8 (beyaz + alfa); (0,0) texeli beyaz opak
// (ui_rect bunu kullanir).
#pragma once
#include <cstdint>

#include "core/memory/arena.hpp"
#include "renderer/renderer.hpp"

namespace tulpar::engine::content {

// Kapsanan kod noktalari (ASCII + Latin-1 + Turkce). Bitmap ve SDF atlaslari
// AYNI listeyi kullanir: iki yol karsilastirilabilir kalsin.
constexpr uint32_t kFontRanges = 4;
constexpr uint32_t kFontRangeFirst[kFontRanges] = {32, 0xA0, 0x11E, 0x15E};
constexpr uint32_t kFontRangeCount[kFontRanges] = {95, 96, 20, 2};
constexpr uint32_t kFontGlyphTotal = 95 + 96 + 20 + 2;

struct Glyph {
  float u0 = 0, v0 = 0, u1 = 0, v1 = 0; // atlas uv
  float xoff = 0, yoff = 0, w = 0, h = 0; // piksel, sol ust orijine gore
  float advance = 0;
};

// --- SDF (imzali uzaklik alani) atlasi ---------------------------------------
// Bitmap atlasin kapsama (coverage) degeri yerine KONTURA OLAN UZAKLIK durur:
// buyutuldugunde kenar bulanik ikili karisim degil, yeniden kurulabilen keskin
// bir egri olur. Shader tarafi BASKA bir katmanin isi (renderer): burada yalniz
// atlas uretilir ve shader'in ihtiyac duydugu iki sayi (on_edge, pixel_dist_scale)
// disari verilir — alpha = clamp(0.5 + (d - on_edge) * 255 / (pixel_dist_scale * fwidth_px)).
struct SdfAtlas {
  uint8_t *pixels = nullptr; // size*size, TEK kanal (Arena'da); on_edge = kontur
  uint32_t size = 0, padding = 0, glyph_count = 0;
  uint8_t on_edge = 128;        // konturun 8-bit degeri
  float pixel_dist_scale = 0;   // 1 piksellik uzaklik = kac 8-bit adim
  Glyph *glyphs = nullptr;      // kFontRange* sirasina gore ardisik
  float px = 0, line = 0, ascent = 0;
  uint32_t range_offset[kFontRanges] = {};
  const Glyph *glyph(uint32_t cp) const {
    if (!glyphs) return nullptr;
    for (uint32_t i = 0; i < kFontRanges; i++)
      if (cp >= kFontRangeFirst[i] && cp < kFontRangeFirst[i] + kFontRangeCount[i]) return &glyphs[range_offset[i] + (cp - kFontRangeFirst[i])];
    return nullptr;
  }
};
// TTF -> SDF atlas (CPU). Raf (shelf) paketleyici; sigmazsa atlas kenari
// ikiye katlanir (2048'e kadar). padding: glif cevresindeki uzaklik bandi
// (piksel) — buyutme oraninin yarisindan buyuk olmali, yoksa alan kirpilir.
// false: dosya okunamadi, arena dolu ya da glifler sigmadi.
bool font_build_sdf_atlas(Arena &arena, const char *ttf_path, float pixel_height, uint32_t atlas_size, uint32_t padding, SdfAtlas *out);

// SDF KALITE OLCUMU (CPU; GPU gerekmez). Ayni glifi ayni hedef cozunurlukte
// iki yolla yeniden kurar ve yuksek cozunurluklu GERCEK rasterle karsilastirir:
//   1. bitmap atlas: kapsama degeri bilineer buyutulur (bugunku yol),
//   2. SDF atlas: uzaklik bilineer ornekilir, kenar turev genisligiyle cozulur.
// edge_px = gecis bandindaki (0.25 < a < 0.75) piksel sayisi / gercek siluetin
// cevre uzunlugu — yani "kenar kac hedef piksele yayildi". Kucuk = keskin.
struct SdfQuality {
  float sdf_edge_px = 0, bitmap_edge_px = 0;
  float sdf_error = 0, bitmap_error = 0; // gercege gore yanlis siniflandirilan piksel orani
  uint32_t width = 0, height = 0, perimeter = 0;
  float magnify = 0, pixel_height = 0;
};
bool font_measure_sdf_quality(const char *ttf_path, uint32_t codepoint, float pixel_height, float magnify, uint32_t padding,
                              SdfQuality *out);

class Font {
public:
  // pixel_height: glif yuksekligi (piksel); atlas_size: kare atlas kenari.
  bool load(Arena &arena, renderer::Renderer &r, const char *ttf_path, float pixel_height, uint32_t atlas_size = 512);
  // SDF atlasi yukler (dortgen geometrisi ayni; DOKU ARTIK KAPSAMA DEGIL).
  // Cizim icin SDF'i cozen bir shader gerekir — sdf() true iken bitmap
  // shader'i kullanmak metni sismis/bulanik gosterir.
  bool load_sdf(Arena &arena, renderer::Renderer &r, const char *ttf_path, float pixel_height, uint32_t atlas_size = 1024,
                uint32_t padding = 8);
  bool sdf() const { return sdf_; }
  uint8_t sdf_on_edge() const { return sdf_on_edge_; }
  float sdf_pixel_dist_scale() const { return sdf_pixel_dist_scale_; }
  bool loaded() const { return atlas_.valid(); }
  renderer::MaterialHandle atlas() const { return atlas_; }
  float height() const { return px_; }
  float line_height() const { return line_; }
  float ascent() const { return ascent_; }
  const Glyph *glyph(uint32_t codepoint) const;
  float text_width(const char *utf8, float scale = 1.0f) const;
  // (x, y) metnin SOL UST kosesi. Renderer UI kuyruguna dortgen ekler.
  void draw(renderer::Renderer &r, float x, float y, const char *utf8, uint32_t rgba, float scale = 1.0f) const;
  static uint32_t decode_utf8(const char **p); // bir kod noktasi okur, *p ilerler

private:
  static constexpr uint32_t kRanges = kFontRanges;
  Glyph *glyphs_ = nullptr; // her aralik icin ardisik
  uint32_t range_first_[kRanges] = {kFontRangeFirst[0], kFontRangeFirst[1], kFontRangeFirst[2], kFontRangeFirst[3]};
  uint32_t range_count_[kRanges] = {kFontRangeCount[0], kFontRangeCount[1], kFontRangeCount[2], kFontRangeCount[3]};
  uint32_t range_offset_[kRanges] = {};
  renderer::MaterialHandle atlas_{};
  float px_ = 0, line_ = 0, ascent_ = 0;
  uint32_t atlas_size_ = 0;
  bool sdf_ = false;
  uint8_t sdf_on_edge_ = 128;
  float sdf_pixel_dist_scale_ = 0;
};

} // namespace tulpar::engine::content
