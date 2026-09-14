// L6 CONTENT — TTF'ten glif atlasi (stb_truetype, yukleme aninda) ve UTF-8
// metin cizimi (renderer UI dortgenleri). Kapsam: ASCII + Latin-1 + Turkce
// harfler (ğ ı ş Ğ İ Ş). Atlas RGBA8 (beyaz + alfa); (0,0) texeli beyaz opak
// (ui_rect bunu kullanir).
#pragma once
#include <cstdint>

#include "core/memory/arena.hpp"
#include "renderer/renderer.hpp"

namespace tulpar::engine::content {

struct Glyph {
  float u0 = 0, v0 = 0, u1 = 0, v1 = 0; // atlas uv
  float xoff = 0, yoff = 0, w = 0, h = 0; // piksel, sol ust orijine gore
  float advance = 0;
};

class Font {
public:
  // pixel_height: glif yuksekligi (piksel); atlas_size: kare atlas kenari.
  bool load(Arena &arena, renderer::Renderer &r, const char *ttf_path, float pixel_height, uint32_t atlas_size = 512);
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
  static constexpr uint32_t kRanges = 4;
  Glyph *glyphs_ = nullptr; // her aralik icin ardisik
  uint32_t range_first_[kRanges] = {32, 0xA0, 0x11E, 0x15E};
  uint32_t range_count_[kRanges] = {95, 96, 20, 2};
  uint32_t range_offset_[kRanges] = {};
  renderer::MaterialHandle atlas_{};
  float px_ = 0, line_ = 0, ascent_ = 0;
  uint32_t atlas_size_ = 0;
};

} // namespace tulpar::engine::content
