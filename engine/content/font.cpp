#include "content/font.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <stb_truetype.h> // govde: content/vendored_impl.c

namespace tulpar::engine::content {

namespace {
unsigned char *read_all(const char *path, size_t *n) {
  FILE *f = std::fopen(path, "rb");
  if (!f) return nullptr;
  std::fseek(f, 0, SEEK_END);
  long len = std::ftell(f);
  std::fseek(f, 0, SEEK_SET);
  if (len <= 0) { std::fclose(f); return nullptr; }
  unsigned char *buf = static_cast<unsigned char *>(std::malloc((size_t)len));
  if (!buf) { std::fclose(f); return nullptr; }
  size_t got = std::fread(buf, 1, (size_t)len, f);
  std::fclose(f);
  if (got != (size_t)len) { std::free(buf); return nullptr; }
  *n = got;
  return buf;
}
} // namespace

uint32_t Font::decode_utf8(const char **pp) {
  const unsigned char *p = reinterpret_cast<const unsigned char *>(*pp);
  uint32_t c = p[0];
  int n = 0;
  if (c < 0x80) n = 0;
  else if ((c & 0xE0) == 0xC0) { c &= 0x1F; n = 1; }
  else if ((c & 0xF0) == 0xE0) { c &= 0x0F; n = 2; }
  else if ((c & 0xF8) == 0xF0) { c &= 0x07; n = 3; }
  else { *pp += 1; return 0xFFFD; }
  int i = 1;
  for (; i <= n; i++) {
    if ((p[i] & 0xC0) != 0x80) { *pp += i; return 0xFFFD; }
    c = (c << 6) | (p[i] & 0x3F);
  }
  *pp += i;
  return c;
}

bool Font::load(Arena &arena, renderer::Renderer &r, const char *ttf_path, float pixel_height, uint32_t atlas_size) {
  size_t n = 0;
  unsigned char *ttf = read_all(ttf_path, &n);
  if (!ttf) return false;
  uint32_t total = 0;
  for (uint32_t i = 0; i < kRanges; i++) { range_offset_[i] = total; total += range_count_[i]; }
  glyphs_ = arena.alloc_array_zeroed<Glyph>(total);
  stbtt_packedchar *pc = static_cast<stbtt_packedchar *>(std::malloc(sizeof(stbtt_packedchar) * total));
  unsigned char *bitmap = static_cast<unsigned char *>(std::calloc((size_t)atlas_size * atlas_size, 1));
  if (!glyphs_ || !pc || !bitmap) { std::free(ttf); std::free(pc); std::free(bitmap); return false; }
  // Atlasa sigmazsa (28 px x 2 oversample x 213 glif 512'ye sigmadi — telefonda
  // "font yok" olarak goruldu) kenari ikiye katlayip yeniden dene, 2048'e kadar.
  bool ok = false;
  for (;;) {
    std::memset(bitmap, 0, (size_t)atlas_size * atlas_size);
    stbtt_pack_context spc;
    if (stbtt_PackBegin(&spc, bitmap, (int)atlas_size, (int)atlas_size, 0, 2, nullptr) != 0) {
      stbtt_PackSetOversampling(&spc, 2, 2);
      stbtt_pack_range ranges[kRanges];
      for (uint32_t i = 0; i < kRanges; i++) {
        ranges[i] = stbtt_pack_range{};
        ranges[i].font_size = pixel_height;
        ranges[i].first_unicode_codepoint_in_range = (int)range_first_[i];
        ranges[i].num_chars = (int)range_count_[i];
        ranges[i].chardata_for_range = pc + range_offset_[i];
      }
      ok = stbtt_PackFontRanges(&spc, ttf, 0, ranges, (int)kRanges) != 0;
      stbtt_PackEnd(&spc);
    }
    if (ok || atlas_size >= 2048) break;
    atlas_size *= 2;
    std::free(bitmap);
    bitmap = static_cast<unsigned char *>(std::calloc((size_t)atlas_size * atlas_size, 1));
    if (!bitmap) break;
  }
  if (!bitmap) { std::free(ttf); std::free(pc); return false; }
  if (ok) {
    stbtt_fontinfo info;
    if (stbtt_InitFont(&info, ttf, 0)) {
      int asc, desc, gap;
      stbtt_GetFontVMetrics(&info, &asc, &desc, &gap);
      float sc = stbtt_ScaleForPixelHeight(&info, pixel_height);
      ascent_ = asc * sc;
      line_ = (asc - desc + gap) * sc;
    } else { ascent_ = pixel_height * 0.8f; line_ = pixel_height * 1.2f; }
    px_ = pixel_height;
    atlas_size_ = atlas_size;
    const float inv = 1.0f / (float)atlas_size;
    for (uint32_t i = 0; i < total; i++) {
      const stbtt_packedchar &c = pc[i];
      Glyph &g = glyphs_[i];
      g.u0 = c.x0 * inv; g.v0 = c.y0 * inv; g.u1 = c.x1 * inv; g.v1 = c.y1 * inv;
      g.xoff = c.xoff; g.yoff = c.yoff; g.w = (float)(c.x1 - c.x0) / 2.0f; g.h = (float)(c.y1 - c.y0) / 2.0f; // 2x oversample
      g.advance = c.xadvance;
    }
    // RGBA: beyaz + alfa; (0,0)..(1,1) beyaz opak (ui_rect icin) — padding bolgesi, glif yok.
    unsigned char *rgba = static_cast<unsigned char *>(std::malloc((size_t)atlas_size * atlas_size * 4));
    if (rgba) {
      for (size_t i = 0; i < (size_t)atlas_size * atlas_size; i++) {
        rgba[i * 4 + 0] = rgba[i * 4 + 1] = rgba[i * 4 + 2] = 255;
        rgba[i * 4 + 3] = bitmap[i];
      }
      for (uint32_t y = 0; y < 2; y++) for (uint32_t x = 0; x < 2; x++) rgba[(y * atlas_size + x) * 4 + 3] = 255;
      renderer::TextureHandle t = r.create_texture(rgba, atlas_size, atlas_size, false, /*srgb=*/false); // kaplama: veri, renk degil
      if (t.valid()) atlas_ = r.create_material(t, {1, 1, 1});
      std::free(rgba);
    }
    ok = atlas_.valid();
  }
  std::free(bitmap);
  std::free(pc);
  std::free(ttf);
  return ok;
}

const Glyph *Font::glyph(uint32_t cp) const {
  for (uint32_t i = 0; i < kRanges; i++)
    if (cp >= range_first_[i] && cp < range_first_[i] + range_count_[i]) return &glyphs_[range_offset_[i] + (cp - range_first_[i])];
  return nullptr;
}

float Font::text_width(const char *s, float scale) const {
  float w = 0;
  const char *p = s;
  while (*p) {
    uint32_t cp = decode_utf8(&p);
    const Glyph *g = glyph(cp);
    if (!g) g = glyph('?');
    if (g) w += g->advance * scale;
  }
  return w;
}

void Font::draw(renderer::Renderer &r, float x, float y, const char *s, uint32_t rgba, float scale) const {
  if (!atlas_.valid()) return;
  r.ui_set_atlas(atlas_);
  float pen = x;
  const float base = y + ascent_ * scale;
  const char *p = s;
  while (*p) {
    uint32_t cp = decode_utf8(&p);
    if (cp == '\n') { pen = x; y += line_ * scale; continue; }
    const Glyph *g = glyph(cp);
    if (!g) g = glyph('?');
    if (!g) continue;
    // stb: xoff/yoff oversample'a gore piksel; 2x oversample ile w/h yarim
    r.ui_quad(pen + g->xoff * scale, base + g->yoff * scale, g->w * scale, g->h * scale, g->u0, g->v0, g->u1, g->v1, rgba);
    pen += g->advance * scale;
  }
}

} // namespace tulpar::engine::content
