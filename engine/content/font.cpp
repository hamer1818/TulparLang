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

// --- SDF atlasi --------------------------------------------------------------
// stb_truetype'in kendi SDF uretecini kullaniriz (stbtt_GetCodepointSDF): glif
// basina, konturun ISARETLI uzakligini 8 bite olceklenmis bir bitmap verir
// (on_edge = kontur, buyuk = ic). Paketleme RAF (shelf) duzeni: glifler
// yuksekliklerine gore siraya dizilmez, geldikleri sirada satirlara konur —
// deterministik ve yeterli (atlas doluluk orani kritik degil, atlas bir kez
// kurulur). Sigmazsa kenar ikiye katlanir, 2048'e kadar.
bool font_build_sdf_atlas(Arena &arena, const char *ttf_path, float pixel_height, uint32_t atlas_size, uint32_t padding,
                          SdfAtlas *out) {
  if (!out) return false;
  *out = SdfAtlas{};
  size_t n = 0;
  unsigned char *ttf = read_all(ttf_path, &n);
  if (!ttf) return false;
  stbtt_fontinfo info;
  if (!stbtt_InitFont(&info, ttf, 0)) { std::free(ttf); return false; }

  uint32_t total = 0;
  for (uint32_t i = 0; i < kFontRanges; i++) { out->range_offset[i] = total; total += kFontRangeCount[i]; }
  Glyph *glyphs = arena.alloc_array_zeroed<Glyph>(total);
  if (!glyphs) { std::free(ttf); return false; }

  const float scale = stbtt_ScaleForPixelHeight(&info, pixel_height);
  const uint8_t on_edge = 128;
  // Uzaklik olcegi: 1 piksel kac 8-bit adim. on_edge'in altinda/ustunde esit
  // bant kalsin diye 128/padding (padding piksel = tam doyum).
  const float pixel_dist_scale = padding > 0 ? (float)on_edge / (float)padding : 32.0f;

  for (;;) {
    uint8_t *pix = arena.alloc_array_zeroed<uint8_t>((size_t)atlas_size * atlas_size);
    if (!pix) { std::free(ttf); return false; }
    uint32_t pen_x = 2, pen_y = 2, row_h = 0, packed = 0; // (0,0)..(1,1) beyaz texel icin bosluk
    bool fits = true;
    for (uint32_t r = 0; r < kFontRanges && fits; r++) {
      for (uint32_t c = 0; c < kFontRangeCount[r] && fits; c++) {
        const uint32_t cp = kFontRangeFirst[r] + c;
        Glyph &g = glyphs[out->range_offset[r] + c];
        int adv = 0, lsb = 0;
        stbtt_GetCodepointHMetrics(&info, (int)cp, &adv, &lsb);
        g.advance = adv * scale;
        int w = 0, h = 0, xoff = 0, yoff = 0;
        unsigned char *sdf = stbtt_GetCodepointSDF(&info, scale, (int)cp, (int)padding, on_edge, pixel_dist_scale, &w, &h, &xoff, &yoff);
        if (!sdf || w <= 0 || h <= 0) { if (sdf) stbtt_FreeSDF(sdf, nullptr); continue; } // bosluk gibi cizimsiz glif
        if (pen_x + (uint32_t)w + 1 >= atlas_size) { pen_x = 2; pen_y += row_h + 1; row_h = 0; } // yeni raf
        if (pen_y + (uint32_t)h + 1 >= atlas_size) { stbtt_FreeSDF(sdf, nullptr); fits = false; break; }
        for (int y = 0; y < h; y++)
          std::memcpy(pix + (size_t)(pen_y + y) * atlas_size + pen_x, sdf + (size_t)y * w, (size_t)w);
        stbtt_FreeSDF(sdf, nullptr);
        const float inv = 1.0f / (float)atlas_size;
        g.u0 = pen_x * inv; g.v0 = pen_y * inv;
        g.u1 = (pen_x + w) * inv; g.v1 = (pen_y + h) * inv;
        g.xoff = (float)xoff; g.yoff = (float)yoff;
        g.w = (float)w; g.h = (float)h;
        pen_x += (uint32_t)w + 1;
        if ((uint32_t)h > row_h) row_h = (uint32_t)h;
        packed++;
      }
    }
    if (fits) {
      int asc = 0, desc = 0, gap = 0;
      stbtt_GetFontVMetrics(&info, &asc, &desc, &gap);
      out->pixels = pix;
      out->size = atlas_size;
      out->padding = padding;
      out->glyph_count = packed;
      out->on_edge = on_edge;
      out->pixel_dist_scale = pixel_dist_scale;
      out->glyphs = glyphs;
      out->px = pixel_height;
      out->ascent = asc * scale;
      out->line = (asc - desc + gap) * scale;
      std::free(ttf);
      return true;
    }
    if (atlas_size >= 2048) { std::free(ttf); return false; } // arena'daki deneme serbest birakilmaz (yukleme ani)
    atlas_size *= 2;
  }
}

bool Font::load_sdf(Arena &arena, renderer::Renderer &r, const char *ttf_path, float pixel_height, uint32_t atlas_size,
                    uint32_t padding) {
  SdfAtlas a;
  if (!font_build_sdf_atlas(arena, ttf_path, pixel_height, atlas_size, padding, &a)) return false;
  glyphs_ = a.glyphs;
  for (uint32_t i = 0; i < kRanges; i++) range_offset_[i] = a.range_offset[i];
  px_ = a.px;
  line_ = a.line;
  ascent_ = a.ascent;
  atlas_size_ = a.size;
  sdf_ = true;
  sdf_on_edge_ = a.on_edge;
  sdf_pixel_dist_scale_ = a.pixel_dist_scale;
  // ALFA kanali uzaklik (ui_sdf.frag sozlesmesi); renk beyaz. Doku VERI, renk
  // degil: srgb=false, mip yok (SDF'in mip'i kenari kaydirir).
  unsigned char *rgba = static_cast<unsigned char *>(std::malloc((size_t)a.size * a.size * 4));
  if (!rgba) return false;
  for (size_t i = 0; i < (size_t)a.size * a.size; i++) {
    rgba[i * 4 + 0] = rgba[i * 4 + 1] = rgba[i * 4 + 2] = 255;
    rgba[i * 4 + 3] = a.pixels[i];
  }
  // (0,0)..(1,1): ui_rect'in duz kutusu buradan ornekler — tam ic (doyum).
  for (uint32_t y = 0; y < 2; y++)
    for (uint32_t x = 0; x < 2; x++) rgba[(y * a.size + x) * 4 + 3] = 255;
  renderer::TextureHandle t = r.create_texture(rgba, a.size, a.size, false, /*srgb=*/false);
  std::free(rgba);
  if (!t.valid()) return false;
  atlas_ = r.create_material(t, {1, 1, 1});
  return atlas_.valid();
}

// SDF KALITE OLCUMU — bkz. font.hpp'deki sozlesme. GPU gerekmez: her iki yol
// da CPU'da yeniden kurulur ve YUKSEK COZUNURLUKLU gercek rasterle kiyaslanir.
bool font_measure_sdf_quality(const char *ttf_path, uint32_t codepoint, float pixel_height, float magnify, uint32_t padding,
                              SdfQuality *out) {
  if (!out || magnify < 1.0f) return false;
  *out = SdfQuality{};
  size_t n = 0;
  unsigned char *ttf = read_all(ttf_path, &n);
  if (!ttf) return false;
  stbtt_fontinfo info;
  if (!stbtt_InitFont(&info, ttf, 0)) { std::free(ttf); return false; }
  const float small_scale = stbtt_ScaleForPixelHeight(&info, pixel_height);
  const float big_scale = stbtt_ScaleForPixelHeight(&info, pixel_height * magnify);

  // 1) GERCEK: hedef cozunurlukte dogrudan rasterle (referans).
  int bw = 0, bh = 0, bxo = 0, byo = 0;
  unsigned char *truth = stbtt_GetCodepointBitmap(&info, 0, big_scale, (int)codepoint, &bw, &bh, &bxo, &byo);
  // 2) BITMAP YOLU: kucuk kapsama bitmap'i, bilineer buyutulmus.
  int sw = 0, sh = 0, sxo = 0, syo = 0;
  unsigned char *small = stbtt_GetCodepointBitmap(&info, 0, small_scale, (int)codepoint, &sw, &sh, &sxo, &syo);
  // 3) SDF YOLU: kucuk SDF, bilineer ornekilip kenar turevle cozulur.
  const uint8_t on_edge = 128;
  const float pds = padding > 0 ? (float)on_edge / (float)padding : 32.0f;
  int dw = 0, dh = 0, dxo = 0, dyo = 0;
  unsigned char *sdf = stbtt_GetCodepointSDF(&info, small_scale, (int)codepoint, (int)padding, on_edge, pds, &dw, &dh, &dxo, &dyo);
  if (!truth || !small || !sdf || bw <= 0 || bh <= 0) {
    if (truth) stbtt_FreeBitmap(truth, nullptr);
    if (small) stbtt_FreeBitmap(small, nullptr);
    if (sdf) stbtt_FreeSDF(sdf, nullptr);
    std::free(ttf);
    return false;
  }
  auto bilinear = [](const unsigned char *src, int w, int h, float x, float y) {
    if (w <= 0 || h <= 0) return 0.0f;
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (x > (float)(w - 1)) x = (float)(w - 1);
    if (y > (float)(h - 1)) y = (float)(h - 1);
    const int x0 = (int)x, y0 = (int)y;
    const int x1 = x0 + 1 < w ? x0 + 1 : x0, y1 = y0 + 1 < h ? y0 + 1 : y0;
    const float fx = x - (float)x0, fy = y - (float)y0;
    const float a = (float)src[(size_t)y0 * w + x0], b = (float)src[(size_t)y0 * w + x1];
    const float c = (float)src[(size_t)y1 * w + x0], d = (float)src[(size_t)y1 * w + x1];
    return (a * (1 - fx) + b * fx) * (1 - fy) + (c * (1 - fx) + d * fx) * fy;
  };
  uint32_t perim = 0, sdf_edge = 0, bmp_edge = 0, sdf_bad = 0, bmp_bad = 0;
  for (int y = 0; y < bh; y++) {
    for (int x = 0; x < bw; x++) {
      const float t = (float)truth[(size_t)y * bw + x] / 255.0f;
      // Gercek siluetin cevresi: komsusundan farkli ikili sinif (kenar pikseli).
      const bool inside = t >= 0.5f;
      bool boundary = false;
      for (int dy = -1; dy <= 1 && !boundary; dy++)
        for (int dx = -1; dx <= 1 && !boundary; dx++) {
          const int nx = x + dx, ny = y + dy;
          const float tn = (nx < 0 || ny < 0 || nx >= bw || ny >= bh) ? 0.0f : (float)truth[(size_t)ny * bw + nx] / 255.0f;
          if ((tn >= 0.5f) != inside) boundary = true;
        }
      if (boundary) perim++;
      // Hedef piksel (x,y) -> kucuk kaynagin koordinati. Iki kaynak farkli
      // ofsetlerle (xoff/yoff) uretildigi icin GERCEK glif kutusuna gore eslenir.
      const float gx = ((float)x + 0.5f) / magnify, gy = ((float)y + 0.5f) / magnify;
      const float sx = gx + ((float)bxo / magnify - (float)sxo);
      const float sy = gy + ((float)byo / magnify - (float)syo);
      const float dx_ = gx + ((float)bxo / magnify - (float)dxo);
      const float dy_ = gy + ((float)byo / magnify - (float)dyo);
      const float bmp = bilinear(small, sw, sh, sx, sy) / 255.0f; // dogrudan kapsama
      // SDF: mesafeyi coz. Genislik = 1 hedef piksel (turev), yani buyutmeye
      // bagimsiz keskinlik — shader'daki fwidth'in CPU karsiligi.
      const float d = bilinear(sdf, dw, dh, dx_, dy_) / 255.0f;
      const float w_px = (float)on_edge / 255.0f / (pds * magnify); // 1 hedef pikselin uzaklik uzayindaki karsiligi
      const float lo = 0.5f - w_px, hi = 0.5f + w_px;
      float a = (d - lo) / (hi - lo > 0 ? hi - lo : 1e-5f);
      a = a < 0 ? 0 : (a > 1 ? 1 : a);
      a = a * a * (3.0f - 2.0f * a); // smoothstep
      if (bmp > 0.25f && bmp < 0.75f) bmp_edge++;
      if (a > 0.25f && a < 0.75f) sdf_edge++;
      if ((bmp >= 0.5f) != inside) bmp_bad++;
      if ((a >= 0.5f) != inside) sdf_bad++;
    }
  }
  const float px_total = (float)(bw * bh);
  out->width = (uint32_t)bw;
  out->height = (uint32_t)bh;
  out->perimeter = perim;
  out->magnify = magnify;
  out->pixel_height = pixel_height;
  const float p = perim > 0 ? (float)perim : 1.0f;
  out->sdf_edge_px = (float)sdf_edge / p;
  out->bitmap_edge_px = (float)bmp_edge / p;
  out->sdf_error = (float)sdf_bad / (px_total > 0 ? px_total : 1.0f);
  out->bitmap_error = (float)bmp_bad / (px_total > 0 ? px_total : 1.0f);
  stbtt_FreeBitmap(truth, nullptr);
  stbtt_FreeBitmap(small, nullptr);
  stbtt_FreeSDF(sdf, nullptr);
  std::free(ttf);
  return true;
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
