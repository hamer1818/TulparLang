#include "renderer/debug_draw.hpp"

#include <cmath>

#include "renderer/renderer.hpp"

// --- Kutuphane YAPILANDIRMASI (include'dan ONCE) -----------------------
// glampert/debug-draw bu makrolari `#ifndef` ile korur; asagidakiler
// kutuphanenin KENDI dosyasina DOKUNMADAN davranisini Tulpar kurallarina
// uydurur.

// Mobil butce: kutuphanenin varsayilani 32768 cizgi. Bizim backend cizgi
// basina BIR cizim cagrisi yaptigi icin (bkz. debug_draw.hpp'deki odun
// notu) bu kadar yuksek bir tavan kare suresini yer. Dusuk tutuluyor.
#define DEBUG_DRAW_MAX_LINES 4096
#define DEBUG_DRAW_MAX_POINTS 2048
#define DEBUG_DRAW_MAX_STRINGS 64

namespace tulpar::engine::renderer {
namespace {
uint32_t g_overflow_count = 0;
} // namespace
} // namespace tulpar::engine::renderer

// Tampon dolunca SESSIZCE kirpma YOK -- motorun genel ilkesi (bkz.
// RendererStats::dropped, ui_stats().dropped). Kutuphane zaten bir uyari
// kancasi sunuyor; sayaca bagliyoruz.
#define DEBUG_DRAW_OVERFLOWED(message) (void)(message), ++::tulpar::engine::renderer::g_overflow_count

#define DEBUG_DRAW_IMPLEMENTATION
// ACI PARANTEZ SART: `"debug_draw.hpp"` once BU dosyanin dizinine bakar ve
// motorun KENDI `renderer/debug_draw.hpp`'sini bulur (ayni ad). O baslik en
// ustte zaten include edilmis oldugu icin koruma devreye girer, dosya BOS
// gelir ve `dd::` hic tanimlanmaz. Aci parantez cagiran dizini atlayip
// third_party/debug_draw'a gider. (Derleyicisiz fark edilemezdi.)
#include <debug_draw.hpp>

namespace tulpar::engine::renderer {

namespace {

// Bir cizgiyi INCE BIR KUTU olarak ciz: birim kupu (yari-boyut 0.5, yani
// olcek == tam boyut) A-B ekseni boyunca uzat ve dondur.
// Donme: kupun yerel +Z'sini cizgi yonune goturen eksen-aci.
Mat4 line_transform(Vec3 a, Vec3 b, float thickness) {
  const Vec3 d = b - a;
  const float len = length(d);
  if (len < 1e-6f) return Mat4::translate(a) * Mat4::scale({thickness, thickness, thickness});

  const Vec3 dir = d * (1.0f / len);
  const Vec3 z{0.0f, 0.0f, 1.0f};
  const Vec3 axis = cross(z, dir);
  const float s = length(axis);
  const float c = dot(z, dir);

  Mat4 rot = Mat4::identity();
  if (s > 1e-6f) {
    rot = Mat4::rotate(axis * (1.0f / s), std::atan2(s, c));
  } else if (c < 0.0f) {
    // dir TAM OLARAK -Z: eksen tanimsiz, herhangi bir dik eksen etrafinda 180 derece.
    rot = Mat4::rotate(Vec3{1.0f, 0.0f, 0.0f}, kPi);
  }
  return Mat4::translate((a + b) * 0.5f) * rot * Mat4::scale({thickness, thickness, len});
}

// dd::RenderInterface'in Tulpar uyarlamasi. Kutuphane cizim KOMUTLARINI ve
// tum sekil matematigini yapar; burasi yalniz sonucu ekrana dokuyor.
class TulparDebugRenderer final : public dd::RenderInterface {
 public:
  Renderer *r = nullptr;       // yalniz flush() sirasinda gecerli
  MeshHandle cube{};
  float thickness = 0.03f;
  uint32_t line_budget = 2048;
  DebugDrawStats stats{};

  ~TulparDebugRenderer() override = default;

  void drawLineList(const dd::DrawVertex *lines, int count, bool depthEnabled) override {
    (void)depthEnabled; // derinlik testi acik/kapali ayrimi YOK: tek boru hatti var
    if (!r || !lines || count <= 0) return;
    // Kosuler CIFTLER halinde gelir (baslangic, bitis).
    for (int i = 0; i + 1 < count; i += 2) {
      if (stats.lines_drawn >= line_budget) {
        stats.dropped++;
        continue; // sayilarak dusurulur, sessizce degil
      }
      const Vec3 a{lines[i].line.x, lines[i].line.y, lines[i].line.z};
      const Vec3 b{lines[i + 1].line.x, lines[i + 1].line.y, lines[i + 1].line.z};
      const Vec3 color{lines[i].line.r, lines[i].line.g, lines[i].line.b};
      r->draw(cube, line_transform(a, b, thickness), color);
      stats.lines_drawn++;
    }
  }

  void drawPointList(const dd::DrawVertex *points, int count, bool depthEnabled) override {
    (void)depthEnabled;
    if (!r || !points || count <= 0) return;
    for (int i = 0; i < count; i++) {
      if (stats.points_drawn >= line_budget) {
        stats.dropped++;
        continue;
      }
      const Vec3 p{points[i].point.x, points[i].point.y, points[i].point.z};
      const Vec3 color{points[i].point.r, points[i].point.g, points[i].point.b};
      // dd'nin `size` alani PIKSEL cinsindendir; burada kutu cizdigimiz icin
      // dunya olcegine cevirmek kameraya bagli olurdu -- bilincli olarak
      // sabit bir dunya boyutu kullaniyoruz (kalinligin 2 kati).
      const float sz = thickness * 2.0f;
      r->draw(cube, Mat4::translate(p) * Mat4::scale({sz, sz, sz}), color);
      stats.points_drawn++;
    }
  }

  // --- Metin: BILINCLI olarak baglanmadi --------------------------------
  // Kutuphanenin metin yolu bir glif dokusu ister; motorun kendi metin
  // yolu (content/font.hpp + Renderer::ui_*) ZATEN var ve 2B arayuz
  // uzayinda calisiyor. Ikisini birlestirmek ayri bir istir; o zamana
  // kadar dd::screenText/projectedText cagrilari SESSIZCE yok sayilmaz --
  // doku olusturulamadigi icin kutuphane metni zaten atlar.
  dd::GlyphTextureHandle createGlyphTexture(int width, int height, const void *pixels) override {
    (void)width, (void)height, (void)pixels;
    return nullptr;
  }
  void destroyGlyphTexture(dd::GlyphTextureHandle t) override { (void)t; }
  void drawGlyphList(const dd::DrawVertex *glyphs, int count, dd::GlyphTextureHandle t) override {
    (void)glyphs, (void)count, (void)t;
  }
};

TulparDebugRenderer g_backend;
bool g_ready = false;

} // namespace

bool debug_draw_init(MeshHandle cube, uint32_t max_lines_per_frame) {
  if (g_ready || !cube.valid() || max_lines_per_frame == 0) return false;
  g_backend.cube = cube;
  g_backend.line_budget = max_lines_per_frame;
  g_backend.stats = DebugDrawStats{};
  g_overflow_count = 0;
  if (!dd::initialize(&g_backend)) return false;
  g_ready = true;
  return true;
}

void debug_draw_shutdown() {
  if (!g_ready) return;
  dd::shutdown();
  g_ready = false;
}

bool debug_draw_ready() { return g_ready; }

void debug_draw_flush(Renderer &r, float line_thickness) {
  if (!g_ready) return;
  g_backend.r = &r;
  g_backend.thickness = line_thickness;
  g_backend.stats.lines_drawn = 0;
  g_backend.stats.points_drawn = 0;
  g_backend.stats.dropped = 0;

  // currTimeMillis: kutuphanenin "N ms boyunca kalsin" ozelligi icin. 0
  // gecmek TUM birikmis komutlari bu karede cizip temizler -- bizim
  // kullanimimiz kare-basina oldugu icin dogru davranis budur.
  dd::flush(0);

  g_backend.stats.dropped += g_overflow_count; // kutuphanenin kendi tasmasi
  g_overflow_count = 0;
  g_backend.r = nullptr;
}

DebugDrawStats debug_draw_stats() { return g_backend.stats; }

} // namespace tulpar::engine::renderer
