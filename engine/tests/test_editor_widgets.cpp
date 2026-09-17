// editor_widgets kapilari: Ozellikler / Sahne panel widget'lari.
//
// Her kapi editorun GERCEK yazi tipi + temasiyla offscreen cizer (editor_probe)
// ve PIKSEL ya da PropItem olcer; goruntunun kendisi
// ($TMPDIR/editor_widgets_*.ppm -> engine/tools/ppm2png.py) yazan kisinin
// bakmasi icindir. Sentetik fare: cizim geri cagrisi icinde io.AddMouse*Event
// kuyruga girer, bir SONRAKI karede islenir; bu yuzden takvim kare kare.
#include "tests/test.hpp"
#include "tests/editor_probe.hpp"
#include "app/editor_widgets.hpp"
#include "app/editor_ui.hpp"
#include <imgui.h>
#include <cmath>
#include <cstdio>
#include <cstring>

using namespace tulpar::engine;
using namespace tulpar::engine::test;

namespace {

void ppm_path(char *out, size_t n, const char *stem) { std::snprintf(out, n, "%s/editor_widgets_%s.ppm", tmp_dir(), stem); }

// Dogrusal (tema sRGB hedefte) -> 8-bit sRGB: sondanin okudugu tampon *_SRGB
// ekte kodlanmis oldugu icin beklenen piksel ton'un sRGB haline esittir.
int lin_to_srgb8(float c) {
  const float s = c <= 0.0031308f ? c * 12.92f : 1.055f * std::pow(c, 1.0f / 2.4f) - 0.055f;
  return (int)(s * 255.0f + 0.5f);
}
int tone_dist(const uint8_t px[4], app::Tone t) {
  float c[4];
  app::editor_tone(t, c);
  return std::abs((int)px[0] - lin_to_srgb8(c[0])) + std::abs((int)px[1] - lin_to_srgb8(c[1])) + std::abs((int)px[2] - lin_to_srgb8(c[2]));
}

// --- Ozellikler paneli maketi -------------------------------------------------
struct InspectorMock {
  char name[32] = "Kahraman";
  float pos[3] = {1.25f, 0.5f, -3.0f}, rot[3] = {0, 90, 0}, scale[3] = {1, 1, 1};
  bool model_on = true, light_on = true, body_on = true;
  int asset = 0;
  float tint[3] = {0.90f, 0.62f, 0.25f};
  float light_color[3] = {1.0f, 0.93f, 0.80f};
  float intensity = 1.5f, radius = 6.0f;
  int shape = 0;
  float half[3] = {0.5f, 0.9f, 0.5f};
  bool dynamic = true;
  char assets[3][128] = {"assets/kahraman.gltf", "assets/checker_cube.gltf", "assets/cok/uzun/bir/yol/adi/zemin_karo_buyuk.glb"};
  app::PropItem frame_pos[16] = {};
  bool raw_activated[16] = {};
  bool remove_hits[16] = {};
  // Sentetik fare takvimi (bkz. kapilar); -1 = yok.
  int drag_axis = -1;
  int click_remove = 0; // 1: kaldir dugmesine tikla
};

void draw_inspector(void *ctx, uint32_t frame) {
  auto *m = static_cast<InspectorMock *>(ctx);
  ImGui::SetNextWindowPos(ImVec2(0, 0));
  ImGui::SetNextWindowSize(ImVec2(380, 760));
  ImGui::Begin("Özellikler", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse);
  app::inspector_title("\xE2\x97\x86", m->name, sizeof m->name, "3 bileşen · kutu gövde · dinamik", app::Tone::Accent);
  app::section_label("DÖNÜŞÜM");
  if (app::prop_begin("donusum")) {
    app::PropItem p = app::prop_vec3("Konum", m->pos, 0.05f);
    if (frame < 16) { m->frame_pos[frame] = p; m->raw_activated[frame] = ImGui::IsItemActivated(); }
    app::prop_help("Euler derece, Z·Y·X sirasi");
    app::prop_vec3("Dönüş", m->rot, 0.5f, 0, 0, "%.1f°");
    app::prop_vec3("Ölçek", m->scale, 0.02f, 0.05f, 20.0f);
    app::prop_end();
  }
  app::section_label("BİLEŞENLER");
  bool rem = false;
  if (app::component_header("\xE2\x97\x86", "Model", &m->model_on, &rem, true, app::Tone::Text)) {
    if (app::prop_begin("model")) {
      app::prop_asset("Kaynak", &m->asset, m->assets, 3);
      app::prop_color("Renk", m->tint);
      app::prop_end();
    }
    app::component_end();
  }
  if (frame < 16) m->remove_hits[frame] = rem;
  // Onay kutusuz cesit (enabled = null): SceneEntity'de bilesen "devre disi"
  // bayragi yok, gercek panel bu bicimi kullanir — gorunumu burada da olsun.
  if (app::component_header("\xE2\x98\x80", "Işık", nullptr, &rem, true, app::Tone::Warn)) {
    if (app::prop_begin("isik")) {
      app::prop_color("Renk", m->light_color);
      app::prop_float("Şiddet", &m->intensity, 0.05f, 0.0f, 100.0f, "%.2f");
      app::prop_float("Yarıçap", &m->radius, 0.05f, 0.1f, 100.0f, "%.2f m");
      app::prop_end();
    }
    app::component_end();
  }
  if (app::component_header("\xE2\x97\xBC", "Gövde", &m->body_on, &rem, true, app::Tone::AxisZ)) {
    if (app::prop_begin("govde")) {
      app::prop_combo("Şekil", &m->shape, "Kutu\0Küre\0");
      app::prop_vec3("Yarım kenar", m->half, 0.02f, 0.01f, 50.0f, "%.2f");
      app::prop_check("Dinamik", &m->dynamic);
      app::prop_end();
    }
    app::component_end();
  }
  static const char *const kAdd[] = {"\xE2\x86\xBB  Animasyon"};
  app::component_add_button(kAdd, 1);
  ImGui::End();
}

// Sentetik girdi kapilari icin kucuk maket: Konum vec3'unun yerlesimi cizildigi
// anda kopyalanir (son yerlesim sonraki vec3 ile ezilir). Takvim: kare 1 fare
// konumu -> 2 bas -> 3 tasi -> 4 birak; her olay bir SONRAKI karede islenir.
struct DragMock : InspectorMock {
  app::PropVec3Layout konum{};
};
void draw_inspector_drag(void *ctx, uint32_t frame) {
  auto *m = static_cast<DragMock *>(ctx);
  ImGui::SetNextWindowPos(ImVec2(0, 0));
  ImGui::SetNextWindowSize(ImVec2(380, 300));
  ImGui::Begin("Özellikler", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse);
  app::section_label("DÖNÜŞÜM");
  if (app::prop_begin("donusum")) {
    app::PropItem p = app::prop_vec3("Konum", m->pos, 0.05f);
    m->konum = app::prop_vec3_last_layout();
    // KONTROL: ImGui'nin "son ogesi" Z alanidir; Y suruklenirken bu false kalir.
    const bool raw = ImGui::IsItemActivated();
    if (frame < 16) { m->frame_pos[frame] = p; m->raw_activated[frame] = raw; }
    app::prop_vec3("Ölçek", m->scale, 0.02f, 0.05f, 20.0f);
    app::prop_end();
  }
  bool rem = false;
  if (app::component_header("\xE2\x97\x86", "Model", &m->model_on, &rem, true, app::Tone::Text)) {
    if (app::prop_begin("model")) {
      app::prop_asset("Kaynak", &m->asset, m->assets, 3);
      app::prop_end();
    }
    app::component_end();
  }
  if (frame < 16) m->remove_hits[frame] = rem;
  const app::ComponentHeaderLayout hdr = app::component_header_last_layout();
  ImGui::End();

  ImGuiIO &io = ImGui::GetIO();
  if (m->drag_axis >= 0) {
    const app::WidgetRect f = m->konum.field[m->drag_axis];
    if (frame == 1) io.AddMousePosEvent(f.cx(), f.cy());
    if (frame == 2) io.AddMouseButtonEvent(0, true);
    if (frame == 3) io.AddMousePosEvent(f.cx() + 40.0f, f.cy());
    if (frame == 4) io.AddMouseButtonEvent(0, false);
  }
  if (m->click_remove) {
    if (frame == 1) io.AddMousePosEvent(hdr.remove.cx(), hdr.remove.cy());
    if (frame == 2) io.AddMouseButtonEvent(0, true);
    if (frame == 3) io.AddMouseButtonEvent(0, false);
  }
}

// --- Sahne paneli maketi ------------------------------------------------------
struct HierarchyMock {
  char filter[64] = {0};
  int toolbar_result = 0;
  int clicked_row = -1;
  bool only_two = false;       // yalniz iki satir (secili/secili degil piksel kapisi)
  bool second_selected = true; // only_two: ikinci satir secili mi
  app::HierarchyRowLayout long_row{};
  float long_row_text_w = 0;
  app::HierarchyRowLayout short_row{};
};
void draw_hierarchy(void *ctx, uint32_t) {
  auto *m = static_cast<HierarchyMock *>(ctx);
  ImGui::SetNextWindowPos(ImVec2(0, 0));
  ImGui::SetNextWindowSize(ImVec2(300, 500));
  ImGui::Begin("Sahne", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse);
  if (m->only_two) {
    app::HierarchyRow a{"Zemin", false, true, false, true, false};
    app::HierarchyRow b{"Kahraman", m->second_selected, true, false, true, true};
    app::hierarchy_row(0, a);
    app::hierarchy_row(1, b);
    ImGui::End();
    return;
  }
  const int tb = app::hierarchy_toolbar(8, true);
  if (tb) m->toolbar_result = tb;
  app::hierarchy_search(m->filter, sizeof m->filter);
  static const app::HierarchyRow kRows[8] = {
      {"Zemin", false, true, false, true, false},
      {"Kahraman", true, true, false, true, true},
      {"Güneş ışığı", false, false, true, false, false},
      {"Kule_sol", false, true, false, false, false},
      {"Kule_sağ", true, true, false, false, false},
      {"Görünmez duvar (batı kenarı, uzun ad)", false, false, false, true, false},
      {"Fener", false, true, true, false, false},
      {"Boş grup", false, false, false, false, false},
  };
  uint32_t shown = 0;
  for (int i = 0; i < 8; i++) {
    if (!app::hierarchy_filter_match(kRows[i].name, m->filter)) continue;
    shown++;
    if (app::hierarchy_row(i, kRows[i])) m->clicked_row = i;
    if (i == 5) { m->long_row = app::hierarchy_row_last_layout(); m->long_row_text_w = ImGui::CalcTextSize(m->long_row.text).x; }
    if (i == 0) m->short_row = app::hierarchy_row_last_layout();
  }
  if (shown == 0) app::hierarchy_empty("Süzgeçle eşleşen varlık yok");
  ImGui::End();
}

// Renk ornegi: bir secici oturumu = bir duzenleme. Takvim (her olay bir sonraki
// karede islenir; ColorButton BIRAKMADA tiklanir):
//   1 fare ornege, 2 bas, 3 birak -> 4: secici acilir, activated
//   edit ise: 4 fare secicinin SV karesine, 5 bas (renk degisir), 6 birak
//   7 fare disariya, 8 bas -> 9'da islenir ama ImGui disari tiklamayla acilir
//   pencereyi karenin SONUNDA kapatir: BeginPopup ilk kez 10'da false doner,
//   deactivated(_after_edit) 10'da (olculdu; 9 degil)
struct ColorMock {
  float rgb[3] = {0.90f, 0.62f, 0.25f};
  bool edit = false;
  app::PropItem items[12] = {};
  app::WidgetRect swatch{};
};
void draw_color_probe(void *ctx, uint32_t frame) {
  auto *m = static_cast<ColorMock *>(ctx);
  ImGui::SetNextWindowPos(ImVec2(0, 0));
  ImGui::SetNextWindowSize(ImVec2(380, 640));
  ImGui::Begin("Renk", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse);
  if (app::prop_begin("renk")) {
    const app::PropItem it = app::prop_color("Renk", m->rgb);
    m->swatch = app::prop_last_rect();
    if (frame < 12) m->items[frame] = it;
    app::prop_end();
  }
  const ImGuiStyle &s = ImGui::GetStyle();
  ImGui::End();
  ImGuiIO &io = ImGui::GetIO();
  const ImVec2 popup(m->swatch.x0, m->swatch.y1 + s.ItemInnerSpacing.y * 0.5f); // prop_color'un SetNextWindowPos'u
  if (frame == 1) io.AddMousePosEvent(m->swatch.cx(), m->swatch.cy());
  if (frame == 2) io.AddMouseButtonEvent(0, true);
  if (frame == 3) io.AddMouseButtonEvent(0, false);
  if (m->edit) {
    if (frame == 4) io.AddMousePosEvent(popup.x + s.WindowPadding.x + 60.0f, popup.y + s.WindowPadding.y + 60.0f); // SV karesi ici
    if (frame == 5) io.AddMouseButtonEvent(0, true);
    if (frame == 6) io.AddMouseButtonEvent(0, false);
  }
  if (frame == 7) io.AddMousePosEvent(200.0f, 610.0f); // pencere ici, secici disi
  if (frame == 8) io.AddMouseButtonEvent(0, true);
  if (frame == 9) io.AddMouseButtonEvent(0, false);
}

void draw_empty_states(void *, uint32_t) {
  ImGui::SetNextWindowPos(ImVec2(0, 0));
  ImGui::SetNextWindowSize(ImVec2(300, 260));
  ImGui::Begin("Sahne", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse);
  app::hierarchy_toolbar(0, false);
  static char f[8] = {0};
  app::hierarchy_search(f, sizeof f);
  app::hierarchy_empty("Sahne boş — “+” ile varlık ekle");
  ImGui::End();
  ImGui::SetNextWindowPos(ImVec2(310, 0));
  ImGui::SetNextWindowSize(ImVec2(300, 260));
  ImGui::Begin("Özellikler", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse);
  app::inspector_empty("Sahne listesinden bir varlık seç");
  ImGui::End();
}

constexpr uint32_t kBufPx = 400 * 800;
uint8_t g_buf_a[kBufPx * 4], g_buf_b[kBufPx * 4], g_buf_c[kBufPx * 4];
void keep(uint8_t *dst, const EditorProbe &p) { std::memcpy(dst, p.pixels, (size_t)p.width * p.height * 4); }

} // namespace

// Maketler: Ozellikler + Sahne + bos durumlar; PPM yazilir, vertex > 0.
ENGINE_TEST(editor_widgets_mock_panels_render) {
  static InspectorMock im;
  EditorProbe p;
  p.width = 380; p.height = 760;
  char path[512];
  ppm_path(path, sizeof path, "ozellikler");
  p.out_ppm = path;
  p.draw = draw_inspector;
  p.ctx = &im;
  p.frames = 3;
  ProbeStatus st = editor_probe_render(p);
  if (st == ProbeStatus::NoVulkan) { skip("Vulkan yok"); return; }
  if (st != ProbeStatus::Ok) std::printf("    [bilgi] sonda: %s\n", p.err);
  CHECK(st == ProbeStatus::Ok);
  std::printf("    [bilgi] Özellikler: %u vertex -> %s\n", p.vertices, path);
  CHECK(p.vertices > 400);

  static HierarchyMock hm;
  EditorProbe q;
  q.width = 300; q.height = 500;
  char path2[512];
  ppm_path(path2, sizeof path2, "sahne");
  q.out_ppm = path2;
  q.draw = draw_hierarchy;
  q.ctx = &hm;
  q.frames = 3;
  st = editor_probe_render(q);
  CHECK(st == ProbeStatus::Ok);
  std::printf("    [bilgi] Sahne: %u vertex -> %s\n", q.vertices, path2);
  CHECK(q.vertices > 200);

  EditorProbe e;
  e.width = 610; e.height = 260;
  char path3[512];
  ppm_path(path3, sizeof path3, "bos");
  e.out_ppm = path3;
  e.draw = draw_empty_states;
  e.frames = 2;
  st = editor_probe_render(e);
  CHECK(st == ProbeStatus::Ok);
  std::printf("    [bilgi] bos durumlar: %u vertex -> %s\n", e.vertices, path3);
  CHECK(e.vertices > 50);
}

// PropItem toplama: Y alt-alani suruklenir; activated basma karesinde,
// deactivated_after_edit birakma karesinde, deger degismis. KONTROL: ham
// ImGui::IsItemActivated (son oge = Z) ayni karede false — toplama olmadan
// track_edit bu duzenlemeyi hic gormezdi. Negatif: fare yokken hicbir bayrak.
ENGINE_TEST(editor_widgets_vec3_propitem_aggregates_subfields) {
  static DragMock m;
  m = DragMock{};
  m.drag_axis = 1;
  EditorProbe p;
  p.width = 380; p.height = 300;
  p.draw = draw_inspector_drag;
  p.ctx = &m;
  p.frames = 7;
  const ProbeStatus st = editor_probe_render(p);
  if (st == ProbeStatus::NoVulkan) { skip("Vulkan yok"); return; }
  if (st != ProbeStatus::Ok) std::printf("    [bilgi] sonda: %s\n", p.err);
  CHECK(st == ProbeStatus::Ok);
  for (int f = 0; f < 7; f++)
    std::printf("    [bilgi] kare %d: changed=%d act=%d deact=%d deact_edit=%d ham_act=%d\n", f, m.frame_pos[f].changed, m.frame_pos[f].activated,
                m.frame_pos[f].deactivated, m.frame_pos[f].deactivated_after_edit, m.raw_activated[f]);
  std::printf("    [bilgi] konum.y: 0.500 -> %.3f\n", m.pos[1]);
  CHECK(m.frame_pos[3].activated);          // basma karesi
  CHECK(!m.raw_activated[3]);               // kontrol: son oge (Z) etkinlesmedi
  CHECK(m.frame_pos[4].changed);            // tasima karesi
  CHECK(m.frame_pos[5].deactivated);        // birakma karesi
  CHECK(m.frame_pos[5].deactivated_after_edit);
  CHECK(m.pos[1] != 0.5f);
  CHECK(m.pos[0] == 1.25f && m.pos[2] == -3.0f); // yalniz Y
  // Negatif: fare olmayan karelerde bayrak yok.
  CHECK(!m.frame_pos[1].activated && !m.frame_pos[1].deactivated && !m.frame_pos[1].changed);

  static DragMock n;
  n = DragMock{};
  n.drag_axis = -1;
  EditorProbe q;
  q.width = 380; q.height = 300;
  q.draw = draw_inspector_drag;
  q.ctx = &n;
  q.frames = 7;
  CHECK(editor_probe_render(q) == ProbeStatus::Ok);
  bool any = false;
  for (int f = 0; f < 7; f++) any |= n.frame_pos[f].activated || n.frame_pos[f].deactivated || n.frame_pos[f].changed || n.frame_pos[f].deactivated_after_edit;
  CHECK(!any);
  CHECK(n.pos[1] == 0.5f);
}

// X/Y/Z rozetleri: her rozetin dolgusundaki piksel kendi eksen tonuna yakin,
// digerlerine uzak. Kontrol: alanin icindeki piksel (Bg3) hicbirine yakin degil.
ENGINE_TEST(editor_widgets_vec3_badges_carry_axis_tones) {
  static DragMock m;
  m = DragMock{};
  EditorProbe p;
  p.width = 380; p.height = 300;
  p.draw = draw_inspector_drag;
  p.ctx = &m;
  p.frames = 3;
  const ProbeStatus st = editor_probe_render(p);
  if (st == ProbeStatus::NoVulkan) { skip("Vulkan yok"); return; }
  CHECK(st == ProbeStatus::Ok);
  static const app::Tone kT[3] = {app::Tone::AxisX, app::Tone::AxisY, app::Tone::AxisZ};
  for (int a = 0; a < 3; a++) {
    const app::WidgetRect b = m.konum.badge[a];
    uint8_t px[4];
    probe_pixel(p, (uint32_t)(b.x0 + 2.0f), (uint32_t)b.cy(), px);
    const int d_own = tone_dist(px, kT[a]);
    int d_other = 1 << 20;
    for (int o = 0; o < 3; o++)
      if (o != a) { const int d = tone_dist(px, kT[o]); if (d < d_other) d_other = d; }
    std::printf("    [bilgi] rozet %c: piksel (%u,%u,%u) kendi tonuna %d, en yakin digerine %d\n", "XYZ"[a], px[0], px[1], px[2], d_own, d_other);
    CHECK(d_own <= 24);
    CHECK(d_other > 60);
  }
  // Kontrol: alanin ortasi (sayi metni disi, ust kenar yakini) bir eksen tonu degil.
  const app::WidgetRect f = m.konum.field[0];
  uint8_t px[4];
  probe_pixel(p, (uint32_t)(f.x1 - 3.0f), (uint32_t)(f.y0 + 3.0f), px);
  int dmin = 1 << 20;
  for (int a = 0; a < 3; a++) { const int d = tone_dist(px, kT[a]); if (d < dmin) dmin = d; }
  std::printf("    [bilgi] alan kontrol: piksel (%u,%u,%u), en yakin eksen tonu %d\n", px[0], px[1], px[2], dmin);
  CHECK(dmin > 60);
}

// Bilesen basligi: etkin -> devre disi pikselleri degistirir (ad soluklasir,
// onay kutusu bosalir); kaldir dugmesine sentetik tik remove_clicked'i
// birakma karesinde bir kez atesler. Kontrol: ayni durum iki kez = 0 fark.
ENGINE_TEST(editor_widgets_component_header_toggle_and_remove) {
  static DragMock m;
  m = DragMock{};
  EditorProbe p;
  p.width = 380; p.height = 300;
  p.draw = draw_inspector_drag;
  p.ctx = &m;
  p.frames = 3;
  ProbeStatus st = editor_probe_render(p);
  if (st == ProbeStatus::NoVulkan) { skip("Vulkan yok"); return; }
  CHECK(st == ProbeStatus::Ok);
  keep(g_buf_a, p);
  const uint32_t n = p.width * p.height;
  st = editor_probe_render(p); // ayni durum
  CHECK(st == ProbeStatus::Ok);
  keep(g_buf_b, p);
  const uint32_t same = probe_diff(g_buf_a, g_buf_b, n);
  m.model_on = false;
  st = editor_probe_render(p);
  CHECK(st == ProbeStatus::Ok);
  keep(g_buf_c, p);
  const uint32_t diff = probe_diff(g_buf_a, g_buf_c, n);
  std::printf("    [bilgi] baslik etkin/etkin fark %u piksel (kontrol), etkin/devre disi fark %u piksel\n", same, diff);
  CHECK(same == 0);
  CHECK(diff > 30);

  static DragMock r;
  r = DragMock{};
  r.click_remove = 1;
  EditorProbe q;
  q.width = 380; q.height = 300;
  q.draw = draw_inspector_drag;
  q.ctx = &r;
  q.frames = 6;
  CHECK(editor_probe_render(q) == ProbeStatus::Ok);
  int hits = 0, hit_frame = -1;
  for (int f = 0; f < 6; f++) if (r.remove_hits[f]) { hits++; hit_frame = f; }
  std::printf("    [bilgi] kaldir: %d tetik, kare %d (birakma karesi 4 beklenir)\n", hits, hit_frame);
  CHECK(hits == 1);
  CHECK(hit_frame == 4);
  // Negatif: tik yokken hic tetik yok.
  bool any = false;
  for (int f = 0; f < 16; f++) any |= m.remove_hits[f];
  CHECK(!any);
}

// Renk ornegi: secici oturumu tek duzenleme. Degistirmeden kapat -> activated
// 1 kez, deactivated 1 kez, after_edit HIC (kontrol); SV karesine tiklayip
// kapat -> changed gorulur, kapanista after_edit, renk farkli.
ENGINE_TEST(editor_widgets_color_swatch_session_is_one_edit) {
  static ColorMock a;
  a = ColorMock{};
  EditorProbe p;
  p.width = 380; p.height = 640;
  p.draw = draw_color_probe;
  p.ctx = &a;
  p.frames = 11;
  const ProbeStatus st = editor_probe_render(p);
  if (st == ProbeStatus::NoVulkan) { skip("Vulkan yok"); return; }
  if (st != ProbeStatus::Ok) std::printf("    [bilgi] sonda: %s\n", p.err);
  CHECK(st == ProbeStatus::Ok);
  int act = 0, deact = 0, after = 0, changed = 0, act_f = -1, deact_f = -1;
  for (int f = 0; f < 11; f++) {
    act += a.items[f].activated; deact += a.items[f].deactivated; after += a.items[f].deactivated_after_edit; changed += a.items[f].changed;
    if (a.items[f].activated) act_f = f;
    if (a.items[f].deactivated) deact_f = f;
  }
  std::printf("    [bilgi] duzenlemesiz: activated %d (kare %d), deactivated %d (kare %d), after_edit %d, changed %d\n", act, act_f, deact, deact_f, after, changed);
  CHECK(act == 1 && act_f == 4);
  CHECK(deact == 1 && deact_f == 10);
  CHECK(after == 0);
  CHECK(changed == 0);
  CHECK(a.rgb[0] == 0.90f && a.rgb[1] == 0.62f && a.rgb[2] == 0.25f);

  static ColorMock b;
  b = ColorMock{};
  b.edit = true;
  EditorProbe q;
  q.width = 380; q.height = 640;
  q.draw = draw_color_probe;
  q.ctx = &b;
  q.frames = 11;
  CHECK(editor_probe_render(q) == ProbeStatus::Ok);
  act = deact = after = changed = 0; act_f = deact_f = -1;
  for (int f = 0; f < 11; f++) {
    act += b.items[f].activated; deact += b.items[f].deactivated; after += b.items[f].deactivated_after_edit; changed += b.items[f].changed;
    if (b.items[f].activated) act_f = f;
    if (b.items[f].deactivated) deact_f = f;
  }
  std::printf("    [bilgi] duzenlemeli: activated %d (kare %d), deactivated %d (kare %d), after_edit %d, changed %d; renk (%.3f, %.3f, %.3f)\n", act, act_f,
              deact, deact_f, after, changed, b.rgb[0], b.rgb[1], b.rgb[2]);
  CHECK(act == 1 && act_f == 4);
  CHECK(deact == 1 && deact_f == 10);
  CHECK(after == 1);
  CHECK(changed >= 1);
  CHECK(b.rgb[0] != 0.90f || b.rgb[1] != 0.62f || b.rgb[2] != 0.25f);
}

// Suzgec eslesmesi: bos, buyuk/kucuk harf, Turkce katlama, eslesmeyen.
ENGINE_TEST(editor_widgets_hierarchy_filter_match_cases) {
  CHECK(app::hierarchy_filter_match("Kahraman", ""));
  CHECK(app::hierarchy_filter_match("Kahraman", nullptr));
  CHECK(app::hierarchy_filter_match("Kahraman", "kah"));
  CHECK(app::hierarchy_filter_match("Kahraman", "KAHRA"));
  CHECK(app::hierarchy_filter_match("Kahraman", "aman"));
  CHECK(app::hierarchy_filter_match("Güneş ışığı", "isigi")); // i/s/g katlanir: "isigi" -> "isigi"
  CHECK(app::hierarchy_filter_match("Güneş ışığı", "gunes"));
  CHECK(app::hierarchy_filter_match("Güneş ışığı", "GÜNEŞ"));
  CHECK(app::hierarchy_filter_match("IŞIK", "ışık"));
  CHECK(app::hierarchy_filter_match("İstanbul", "ist"));
  CHECK(!app::hierarchy_filter_match("Kahraman", "zemin"));
  CHECK(!app::hierarchy_filter_match("Kahraman", "kahramanlar")); // suzgec addan uzun
  CHECK(!app::hierarchy_filter_match(nullptr, "a"));
  CHECK(!app::hierarchy_filter_match("", "a"));
}

// Hiyerarsi satiri: secili ve secili degil piksel farki > 0 (kontrol: ayni
// durum = 0); uzun ad kirpilir ve cizilen metin ayrilan genisligi asmaz,
// kisa ad kirpilmaz.
ENGINE_TEST(editor_widgets_hierarchy_row_selection_and_ellipsis) {
  static HierarchyMock m;
  m = HierarchyMock{};
  m.only_two = true;
  m.second_selected = false;
  EditorProbe p;
  p.width = 300; p.height = 120;
  p.draw = draw_hierarchy;
  p.ctx = &m;
  p.frames = 2;
  ProbeStatus st = editor_probe_render(p);
  if (st == ProbeStatus::NoVulkan) { skip("Vulkan yok"); return; }
  CHECK(st == ProbeStatus::Ok);
  keep(g_buf_a, p);
  const uint32_t n = p.width * p.height;
  CHECK(editor_probe_render(p) == ProbeStatus::Ok);
  keep(g_buf_b, p);
  const uint32_t same = probe_diff(g_buf_a, g_buf_b, n);
  m.second_selected = true;
  CHECK(editor_probe_render(p) == ProbeStatus::Ok);
  keep(g_buf_c, p);
  const uint32_t diff = probe_diff(g_buf_a, g_buf_c, n);
  std::printf("    [bilgi] satir secimsiz/secimsiz fark %u (kontrol), secimsiz/secili fark %u piksel\n", same, diff);
  CHECK(same == 0);
  CHECK(diff > 200);

  static HierarchyMock e;
  e = HierarchyMock{};
  EditorProbe q;
  q.width = 300; q.height = 500;
  q.draw = draw_hierarchy;
  q.ctx = &e;
  q.frames = 2;
  CHECK(editor_probe_render(q) == ProbeStatus::Ok);
  std::printf("    [bilgi] uzun ad -> \"%s\" (%.1f px, sinir %.1f px), kisa ad kirpildi=%d\n", e.long_row.text, e.long_row_text_w, e.long_row.text_max_w,
              e.short_row.ellipsized);
  CHECK(e.long_row.ellipsized);
  CHECK(e.long_row_text_w <= e.long_row.text_max_w);
  CHECK(e.long_row_text_w > 0.0f);
  const size_t L = std::strlen(e.long_row.text);
  CHECK(L >= 3 && std::memcmp(e.long_row.text + L - 3, "\xE2\x80\xA6", 3) == 0); // "…" ile biter
  CHECK(!e.short_row.ellipsized);
  CHECK(std::strcmp(e.short_row.text, "Zemin") == 0);
}
