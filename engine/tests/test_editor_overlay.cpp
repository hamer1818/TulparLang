// editor_overlay kapilari: Gorunum kaplamasi, Kaynaklar paneli, tema (dock
// sekmeleri). Hepsi editor_probe ile motorun offscreen gecisine GERCEK yazi
// tipi + temayla cizer; PPM'ler scratchpad/agent-c altina yazilir (ppm2png ile
// BAKILIR — piksel kapisi goruntu dogrulamanin kendisi degil, olcusudur).
//
// Her kapinin OLUMLU ve OLUMSUZ kontrolu var:
//  1) Eksen izdusumu (saf): birim gorunumde +X sagda, +Y yukarida; Y etrafinda
//     180 derece donunce +X SOLA gecer (kontrol) — yoksa "sagda" iddiasi bos.
//  2) +X ucunun pikseli palette AxisX'e esit; gostergeden uzak bir piksel DEGIL.
//  3) Odak cercevesi: yalniz IC kenar seridi degisir; icerideki pikseller ayni.
//  4) Kucuk dikdortgen (40x20): disina TEK piksel tasmaz; buyuk dikdortgende
//     ayni cagri haplari cizer (kontrol: kucuklukten dolayi atlandigi belli).
//  5) Kaynaklar: sentetik cift tik add_index verir; yalniz ustunde durma VERMEZ.
//  6) Bos durum metni cizilir; liste kipi de cizer.
//  7) Tema: secili dock sekmesinin pikseli == WindowBg; pasif sekmeninki !=.
//  8) Soldan kirpma kuyrugu '/' sinirinda korur.
#include <cmath>
#include <cstdio>
#include <cstring>

#include "app/editor_overlay.hpp"
#include "app/editor_ui.hpp"
#include "core/math/vec.hpp"
#include "tests/editor_probe.hpp"
#include "tests/test.hpp"

#include <imgui.h>
#include <imgui_internal.h> // dock sekmesi dikdortgeni (ImGuiTabBar) — tests/ her seyi gorur

using namespace tulpar::engine;
using namespace tulpar::engine::test;

namespace {
constexpr const char *kOut = "/tmp/claude-1000/-mnt-veri-yazilim-Tulpar/1bc55e54-3de0-46ed-9830-7196bb6ac65e/scratchpad/agent-c";
void out_path(char *buf, size_t n, const char *name) { std::snprintf(buf, n, "%s/%s", kOut, name); }

// Dogrusal ton -> sRGB 8 bit. Offscreen hedef _SRGB: donanim yazarken kodlar,
// okunan bayt paletteki altigen degerin ta kendisidir (+-1 yuvarlama).
uint8_t enc8(float c) {
  c = c <= 0.0031308f ? 12.92f * c : 1.055f * std::pow(c, 1.0f / 2.4f) - 0.055f;
  const float v = c * 255.0f + 0.5f;
  return (uint8_t)(v < 0 ? 0 : v > 255 ? 255 : v);
}
void tone8(app::Tone t, uint8_t out[3]) {
  float c[4];
  app::editor_tone(t, c);
  out[0] = enc8(c[0]); out[1] = enc8(c[1]); out[2] = enc8(c[2]);
}
bool near8(const uint8_t *p, const uint8_t *e, int tol) {
  for (int i = 0; i < 3; i++) {
    const int d = (int)p[i] - (int)e[i];
    if (d > tol || d < -tol) return false;
  }
  return true;
}
// 3B goruntunun YERINE gecen zemin: gok->yer gecisi + ufuk + perspektif izgara.
// Kaplamanin "goruntu ustunde okunur mu" sorusu duz griyle olculemezdi.
void stand_in(ImDrawList *dl, ImVec2 a, ImVec2 b) {
  const ImU32 sky0 = IM_COL32(74, 84, 98, 255), sky1 = IM_COL32(40, 46, 56, 255);
  const ImU32 gnd0 = IM_COL32(52, 54, 58, 255), gnd1 = IM_COL32(30, 32, 36, 255);
  const float hy = a.y + (b.y - a.y) * 0.48f;
  dl->AddRectFilledMultiColor(a, ImVec2(b.x, hy), sky0, sky0, sky1, sky1);
  dl->AddRectFilledMultiColor(ImVec2(a.x, hy), b, gnd0, gnd0, gnd1, gnd1);
  const ImU32 grid = IM_COL32(120, 128, 140, 70);
  const float cx = (a.x + b.x) * 0.5f;
  for (int i = -8; i <= 8; i++) dl->AddLine(ImVec2(cx + (float)i * 18.0f, hy), ImVec2(cx + (float)i * 140.0f, b.y), grid, 1.0f);
  for (int k = 1; k < 9; k++) {
    const float y = hy + (b.y - hy) * (float)(k * k) / 81.0f;
    dl->AddLine(ImVec2(a.x, y), ImVec2(b.x, y), grid, 1.0f);
  }
  dl->AddRectFilled(ImVec2(cx - 60, hy - 30), ImVec2(cx + 10, hy + 40), IM_COL32(150, 90, 70, 255));
  dl->AddRectFilled(ImVec2(cx + 60, hy - 12), ImVec2(cx + 120, hy + 30), IM_COL32(90, 130, 100, 255));
}

// --- Kaplama sondasi ----------------------------------------------------------
struct OverlayCtx {
  app::OverlayInfo info;
  app::OverlayLayout lay;
  bool draw = true;          // false: yalniz zemin (fark olcumu icin)
  bool sub_rect = false;     // true: kaplama icerigin icinde kucuk bir dikdortgene
  app::ViewportRect sub{0, 0, 40, 20};
  app::ViewportRect rect{};  // kullanilan dikdortgen (cikti)
  uint8_t axis_x[3], bg1[3];
};
void draw_overlay_probe(void *ctx, uint32_t) {
  auto *c = static_cast<OverlayCtx *>(ctx);
  const ImGuiIO &io = ImGui::GetIO();
  ImGui::SetNextWindowPos(ImVec2(0, 0));
  ImGui::SetNextWindowSize(io.DisplaySize);
  ImGui::Begin("G\xC3\xB6r\xC3\xBCn\xC3\xBCm", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse);
  const ImVec2 origin = ImGui::GetCursorScreenPos();
  const ImVec2 avail = ImGui::GetContentRegionAvail();
  stand_in(ImGui::GetWindowDrawList(), origin, ImVec2(origin.x + avail.x, origin.y + avail.y));
  ImGui::Dummy(avail); // ImGui::Image'in yerine: etkilesimsiz oge, ayni olcu
  c->rect = c->sub_rect ? app::ViewportRect{origin.x + c->sub.x, origin.y + c->sub.y, c->sub.w, c->sub.h}
                        : app::ViewportRect{origin.x, origin.y, avail.x, avail.y};
  if (c->draw) app::viewport_overlay(c->rect, c->info, &c->lay);
  tone8(app::Tone::AxisX, c->axis_x);
  tone8(app::Tone::Bg1, c->bg1);
  ImGui::End();
}
void fill_info(app::OverlayInfo &i, Vec3 eye, Vec3 target) {
  const Mat4 v = Mat4::look_at(eye, target, {0, 1, 0});
  std::memcpy(i.view, &v.m[0][0], sizeof i.view);
  i.cam_eye[0] = eye.x; i.cam_eye[1] = eye.y; i.cam_eye[2] = eye.z;
  i.cam_target[0] = target.x; i.cam_target[1] = target.y; i.cam_target[2] = target.z;
  i.frame_ms = 16.5f;
  i.draw_calls = 12;
  i.entity_count = 3;
  i.hint = "Sol t\xC4\xB1k se\xC3\xA7 \xC2\xB7 Ctrl+t\xC4\xB1k ekle \xC2\xB7 T/R/S gizmo";
}
} // namespace

ENGINE_TEST(editor_overlay_axes_project_identity_right_up_and_flip) {
  // Birim donus: goz +z'de, hedefe bakar -> gorunum donusu birim matris.
  Mat4 id = Mat4::look_at({0, 0, 5}, {0, 0, 0}, {0, 1, 0});
  app::AxisProjection p;
  app::overlay_project_axes(&id.m[0][0], 34.0f, &p);
  std::printf("    [bilgi] birim gorunum: +X (%.1f, %.1f, d %.2f) +Y (%.1f, %.1f, d %.2f) +Z (%.1f, %.1f, d %.2f)\n", (double)p.x[0], (double)p.y[0],
              (double)p.depth[0], (double)p.x[1], (double)p.y[1], (double)p.depth[1], (double)p.x[2], (double)p.y[2], (double)p.depth[2]);
  CHECK(p.x[0] > 30.0f && std::fabs(p.y[0]) < 1e-3f);  // +X sagda
  CHECK(p.y[1] < -30.0f && std::fabs(p.x[1]) < 1e-3f); // +Y yukarida (ekran y asagi)
  CHECK(std::fabs(p.x[2]) < 1e-3f && std::fabs(p.y[2]) < 1e-3f && p.depth[2] > 0.99f); // +Z kameraya dogru: merkezde, onde
  // KONTROL: Y etrafinda 180 derece (goz -z'de) -> +X SOLDA, +Z arkada.
  Mat4 flip = Mat4::look_at({0, 0, -5}, {0, 0, 0}, {0, 1, 0});
  app::AxisProjection q;
  app::overlay_project_axes(&flip.m[0][0], 34.0f, &q);
  std::printf("    [bilgi] 180 derece: +X (%.1f, %.1f, d %.2f) +Z d %.2f\n", (double)q.x[0], (double)q.y[0], (double)q.depth[0], (double)q.depth[2]);
  CHECK(q.x[0] < -30.0f);
  CHECK(q.depth[2] < -0.99f);
  CHECK(q.y[1] < -30.0f); // +Y hala yukarida (yalniz yaw dondu)
  // Egik bakis (editorun varsayilan kamerasina benzer): derinlikler 0 degil,
  // izdusum uzunluklari yaricapi asmaz.
  Mat4 obl = Mat4::look_at({15.1f, 12.3f, 14.9f}, {0, 1, -3}, {0, 1, 0});
  app::AxisProjection o;
  app::overlay_project_axes(&obl.m[0][0], 34.0f, &o);
  for (int a = 0; a < 3; a++) CHECK(std::sqrt(o.x[a] * o.x[a] + o.y[a] * o.y[a]) <= 34.0f + 1e-3f);
  CHECK(std::fabs(o.depth[0]) > 0.1f && std::fabs(o.depth[2]) > 0.1f);
  // Gecersiz girdi: cokmez, sifir.
  app::overlay_project_axes(nullptr, 34.0f, &o);
  CHECK(o.x[0] == 0.0f && o.depth[2] == 0.0f);
  app::overlay_project_axes(&id.m[0][0], 34.0f, nullptr);
}

ENGINE_TEST(editor_overlay_axis_x_endpoint_pixel_matches_palette) {
  OverlayCtx c;
  fill_info(c.info, {15.1f, 12.3f, 14.9f}, {0, 0, 0});
  c.info.focused = true;
  c.info.hovered = true;
  c.info.gizmo_op = 0;
  EditorProbe p;
  p.width = 960; p.height = 600;
  char path[512];
  out_path(path, sizeof path, "overlay_focused.ppm");
  p.out_ppm = path;
  p.draw = draw_overlay_probe;
  p.ctx = &c;
  ProbeStatus st = editor_probe_render(p);
  if (st == ProbeStatus::NoVulkan) { skip("Vulkan yok"); return; }
  if (st != ProbeStatus::Ok) std::printf("    [bilgi] sonda: %s\n", p.err);
  CHECK(st == ProbeStatus::Ok);
  if (st != ProbeStatus::Ok) return;
  CHECK(c.lay.gizmo && c.lay.top_row && c.lay.camera && c.lay.hint && c.lay.border);
  CHECK(c.lay.pills == 3); // Perspektif + kip + istatistik (oynatma yok, gizmolar acik)
  app::AxisProjection pr;
  app::overlay_project_axes(c.info.view, c.lay.gizmo_r, &pr);
  const float ex = c.lay.gizmo_cx + pr.x[0], ey = c.lay.gizmo_cy + pr.y[0];
  // Ucun diskinde, harfin DISINDA dort ornek (eksen hizali; "X" harfi kosegen).
  const float o = c.lay.gizmo_end_r * 0.7f;
  const float sx[4] = {ex + o, ex - o, ex, ex}, sy[4] = {ey, ey, ey + o, ey - o};
  uint32_t hit = 0;
  for (int i = 0; i < 4; i++) {
    uint8_t px[4];
    probe_pixel(p, (uint32_t)(sx[i] + 0.5f), (uint32_t)(sy[i] + 0.5f), px);
    const bool ok = near8(px, c.axis_x, 10);
    if (ok) hit++;
    std::printf("    [bilgi] +X ucu ornek %d (%.0f,%.0f): %02X%02X%02X beklenen %02X%02X%02X %s\n", i, (double)sx[i], (double)sy[i], px[0], px[1], px[2],
                c.axis_x[0], c.axis_x[1], c.axis_x[2], ok ? "esit" : "FARKLI");
  }
  CHECK(pr.depth[0] > -0.02f); // bu kamerada +X ondedir (tam renk beklenir)
  CHECK(hit >= 3);
  // KONTROL: gostergeden uzak, zeminin ortasi AxisX DEGIL.
  uint8_t mid[4];
  probe_pixel(p, 480, 300, mid);
  CHECK(!near8(mid, c.axis_x, 10));
  std::printf("    [bilgi] kaplama: gosterge merkez (%.0f,%.0f) r %.0f; %u vertex; PPM %s\n", (double)c.lay.gizmo_cx, (double)c.lay.gizmo_cy,
              (double)c.lay.gizmo_r, p.vertices, path);
  // Ikinci goruntu: yalniz ustunde (odak yok), oynatiliyor, dondur kipi, gizmolar kapali.
  c.info.focused = false;
  c.info.playing = true;
  c.info.gizmo_op = 1;
  c.info.gizmos_visible = false;
  out_path(path, sizeof path, "overlay_hovered.ppm");
  st = editor_probe_render(p);
  CHECK(st == ProbeStatus::Ok);
  CHECK(c.lay.pills == 5);
}

ENGINE_TEST(editor_overlay_focus_border_changes_inner_edge_only) {
  OverlayCtx c;
  fill_info(c.info, {15.1f, 12.3f, 14.9f}, {0, 0, 0});
  EditorProbe p;
  p.width = 640; p.height = 400;
  p.draw = draw_overlay_probe;
  p.ctx = &c;
  static uint8_t px[3][640 * 400 * 4];
  const bool foc[3] = {false, true, false}, hov[3] = {false, false, true};
  for (int k = 0; k < 3; k++) {
    c.info.focused = foc[k];
    c.info.hovered = hov[k];
    const ProbeStatus st = editor_probe_render(p);
    if (st == ProbeStatus::NoVulkan) { skip("Vulkan yok"); return; }
    CHECK(st == ProbeStatus::Ok);
    if (st != ProbeStatus::Ok) return;
    std::memcpy(px[k], p.pixels, sizeof px[k]);
  }
  // Ic kenar seridi (1 px) ile icerisi ayri sayilir.
  const int x0 = (int)c.rect.x, y0 = (int)c.rect.y, x1 = (int)(c.rect.x + c.rect.w) - 1, y1 = (int)(c.rect.y + c.rect.h) - 1;
  uint32_t ring_f = 0, ring_h = 0, inner_f = 0, ring_n = 0, ring_fh = 0;
  for (int y = y0; y <= y1; y++)
    for (int x = x0; x <= x1; x++) {
      const bool ring = x == x0 || x == x1 || y == y0 || y == y1;
      const size_t i = ((size_t)y * 640 + (size_t)x) * 4;
      const bool d01 = std::memcmp(px[0] + i, px[1] + i, 3) != 0;
      const bool d02 = std::memcmp(px[0] + i, px[2] + i, 3) != 0;
      const bool d12 = std::memcmp(px[1] + i, px[2] + i, 3) != 0;
      if (ring) { ring_n++; if (d01) ring_f++; if (d02) ring_h++; if (d12) ring_fh++; }
      else if (d01 || d02) inner_f++;
    }
  std::printf("    [bilgi] odak cercevesi: serit %u piksel; odakli fark %u, ustunde fark %u, odakli-ustunde fark %u; ic fark %u (0 olmali)\n", ring_n, ring_f,
              ring_h, ring_fh, inner_f);
  CHECK(ring_f > ring_n / 2);   // odak: seridin cogu degisti
  CHECK(ring_h > ring_n / 2);   // ustunde: de degisti (daha soluk)
  CHECK(ring_fh > ring_n / 2);  // ikisi AYNI DEGIL (ustunde daha soluk)
  CHECK(inner_f == 0);          // KONTROL: cerceve disinda hicbir piksel degismedi
}

ENGINE_TEST(editor_overlay_tiny_rect_never_overflows) {
  OverlayCtx c;
  fill_info(c.info, {15.1f, 12.3f, 14.9f}, {0, 0, 0});
  c.info.focused = true;
  c.sub_rect = true;
  c.sub = app::ViewportRect{120, 80, 40, 20};
  EditorProbe p;
  p.width = 480; p.height = 320;
  p.draw = draw_overlay_probe;
  p.ctx = &c;
  static uint8_t px[2][480 * 320 * 4];
  for (int k = 0; k < 2; k++) {
    c.draw = k == 1;
    const ProbeStatus st = editor_probe_render(p);
    if (st == ProbeStatus::NoVulkan) { skip("Vulkan yok"); return; }
    CHECK(st == ProbeStatus::Ok);
    if (st != ProbeStatus::Ok) return;
    std::memcpy(px[k], p.pixels, sizeof px[k]);
  }
  uint32_t out_diff = 0, in_diff = 0;
  for (int y = 0; y < 320; y++)
    for (int x = 0; x < 480; x++) {
      const bool inside = (float)x >= c.rect.x && (float)x < c.rect.x + c.rect.w && (float)y >= c.rect.y && (float)y < c.rect.y + c.rect.h;
      const size_t i = ((size_t)y * 480 + (size_t)x) * 4;
      if (std::memcmp(px[0] + i, px[1] + i, 3) != 0) { if (inside) in_diff++; else out_diff++; }
    }
  std::printf("    [bilgi] 40x20: haplar %d gosterge %d kamera %d ipucu %d cerceve %d; ic fark %u, DIS fark %u (0 olmali)\n", (int)c.lay.top_row,
              (int)c.lay.gizmo, (int)c.lay.camera, (int)c.lay.hint, (int)c.lay.border, in_diff, out_diff);
  CHECK(!c.lay.top_row && !c.lay.gizmo && !c.lay.camera && !c.lay.hint); // sigmayan atlandi
  CHECK(c.lay.border && in_diff > 0);                                     // cerceve yine de icerde
  CHECK(out_diff == 0);                                                    // disariya tek piksel yok
  // KONTROL: ayni cagri 360x240'ta haplari ve gostergeyi cizer — "hicbir sey
  // cizmiyor" degil, "sigmayani atliyor".
  c.sub = app::ViewportRect{40, 30, 360, 240};
  c.draw = true;
  const ProbeStatus st = editor_probe_render(p);
  CHECK(st == ProbeStatus::Ok);
  std::printf("    [bilgi] 360x240 kontrol: haplar %u gosterge %d kamera %d ipucu %d\n", c.lay.pills, (int)c.lay.gizmo, (int)c.lay.camera, (int)c.lay.hint);
  CHECK(c.lay.top_row && c.lay.gizmo && c.lay.camera);
}

// --- Kaynaklar sondasi ---------------------------------------------------------
namespace {
struct AssetsCtx {
  app::AssetsView view;
  app::AssetFile files[5];
  uint32_t n = 5;
  char scene[3][content::kScenePathLen] = {};
  bool loaded[3] = {true, false, true};
  uint32_t ns = 3;
  const char *dir = "/mnt/veri/yazilim/Tulpar/engine/tests/assets";
  app::AssetsLayout lay;
  int add_seen = -1, add_frame = -1;
  bool refresh_seen = false;
  bool synth_click = false; // frame 2..5: cift tik
  bool synth_hover = false; // frame 0: fare karo 1'in ortasina
  int tile = 1;
};
void make_files(AssetsCtx &c) {
  const char *names[5] = {"checker_cube.gltf", "lod_sphere.gltf", "skinned_arm_with_a_very_long_file_name_that_will_not_fit.gltf", "arena.glb",
                          "zemin.gltf"};
  for (uint32_t i = 0; i < 5; i++) {
    std::snprintf(c.files[i].name, sizeof c.files[i].name, "%s", names[i]);
    c.files[i].in_scene = (i == 0 || i == 3);
    c.files[i].index = i == 0 ? 0 : i == 3 ? 1 : -1;
  }
  std::snprintf(c.scene[0], sizeof c.scene[0], "checker_cube.gltf");
  std::snprintf(c.scene[1], sizeof c.scene[1], "yok_boyle_bir_model.gltf");
  std::snprintf(c.scene[2], sizeof c.scene[2], "arena.glb");
}
void draw_assets_probe(void *ctx, uint32_t frame) {
  auto *c = static_cast<AssetsCtx *>(ctx);
  ImGuiIO &io = ImGui::GetIO();
  ImGui::SetNextWindowPos(ImVec2(0, 0));
  ImGui::SetNextWindowSize(io.DisplaySize);
  ImGui::Begin("Kaynaklar", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse);
  app::AssetsAction act;
  app::assets_panel(c->view, c->dir, c->files, c->n, c->scene, c->loaded, c->ns, &act, &c->lay);
  if (act.add_index >= 0) { c->add_seen = act.add_index; c->add_frame = (int)frame; }
  if (act.refresh) c->refresh_seen = true;
  ImGui::End();
  // Sentetik girdi: olaylar BIR SONRAKI karede islenir (NewFrame kuyrugu).
  if ((c->synth_hover || c->synth_click) && frame == 0 && c->lay.cols > 0) {
    const int col = c->tile % c->lay.cols, row = c->tile / c->lay.cols;
    const float mx = c->lay.origin_x + (float)col * (c->lay.tile_w + c->lay.gap) + c->lay.tile_w * 0.5f;
    const float my = c->lay.origin_y + (float)row * (c->lay.tile_h + c->lay.gap) + c->lay.tile_h * 0.5f;
    io.AddMousePosEvent(mx, my);
  }
  if (c->synth_click) {
    if (frame == 2 || frame == 4) io.AddMouseButtonEvent(0, true);
    if (frame == 3 || frame == 5) io.AddMouseButtonEvent(0, false);
  }
}
} // namespace

ENGINE_TEST(editor_assets_panel_double_click_adds_hover_does_not) {
  AssetsCtx c;
  make_files(c);
  c.synth_click = true;
  EditorProbe p;
  p.width = 640; p.height = 420;
  p.frames = 8;
  char path[512];
  out_path(path, sizeof path, "assets_grid_dblclick.ppm");
  p.out_ppm = path;
  p.draw = draw_assets_probe;
  p.ctx = &c;
  ProbeStatus st = editor_probe_render(p);
  if (st == ProbeStatus::NoVulkan) { skip("Vulkan yok"); return; }
  if (st != ProbeStatus::Ok) std::printf("    [bilgi] sonda: %s\n", p.err);
  CHECK(st == ProbeStatus::Ok);
  if (st != ProbeStatus::Ok) return;
  std::printf("    [bilgi] cift tik: add_index %d (kare %d), izgara %d sutun karo %.0fx%.0f, gosterilen %u, %u vertex\n", c.add_seen, c.add_frame,
              c.lay.cols, (double)c.lay.tile_w, (double)c.lay.tile_h, c.lay.shown, p.vertices);
  CHECK(c.lay.cols >= 2 && c.lay.shown == 5);
  CHECK(c.add_seen == c.tile);
  CHECK(!c.refresh_seen);
  // KONTROL: yalniz ustunde durma (tik yok) -> add_index -1 kalir; ama karo
  // "ustunde" haline gecer (piksel farki), yani fare gercekten oradaydi.
  AssetsCtx h;
  make_files(h);
  h.synth_hover = true;
  static uint8_t px_hover[640 * 420 * 4], px_plain[640 * 420 * 4];
  out_path(path, sizeof path, "assets_grid_hover.ppm");
  p.ctx = &h;
  st = editor_probe_render(p);
  CHECK(st == ProbeStatus::Ok);
  if (st != ProbeStatus::Ok) return;
  std::memcpy(px_hover, p.pixels, sizeof px_hover);
  CHECK(h.add_seen == -1);
  AssetsCtx n;
  make_files(n);
  out_path(path, sizeof path, "assets_grid.ppm");
  p.ctx = &n;
  st = editor_probe_render(p);
  CHECK(st == ProbeStatus::Ok);
  if (st != ProbeStatus::Ok) return;
  std::memcpy(px_plain, p.pixels, sizeof px_plain);
  const uint32_t d = probe_diff(px_hover, px_plain, 640 * 420);
  std::printf("    [bilgi] ustunde kontrolu: add_index %d, ustunde-duz piksel farki %u (>0: fare karodaydi)\n", h.add_seen, d);
  CHECK(d > 50);
  // Suzgec: eslesen yok -> bos durum, gosterilen 0.
  AssetsCtx f;
  make_files(f);
  std::snprintf(f.view.filter, sizeof f.view.filter, "yok_boyle");
  p.ctx = &f;
  p.out_ppm = nullptr;
  st = editor_probe_render(p);
  CHECK(st == ProbeStatus::Ok && f.lay.shown == 0 && f.lay.empty);
  std::snprintf(f.view.filter, sizeof f.view.filter, "GLB"); // buyuk/kucuk duyarsiz
  st = editor_probe_render(p);
  CHECK(st == ProbeStatus::Ok && f.lay.shown == 1 && !f.lay.empty);
}

ENGINE_TEST(editor_assets_panel_list_mode_and_empty_state_render) {
  AssetsCtx c;
  make_files(c);
  c.view.grid = false;
  EditorProbe p;
  p.width = 480; p.height = 420; // dar: dikey yigin duzeni (genis paneller yan yana)
  char path[512];
  out_path(path, sizeof path, "assets_list.ppm");
  p.out_ppm = path;
  p.draw = draw_assets_probe;
  p.ctx = &c;
  ProbeStatus st = editor_probe_render(p);
  if (st == ProbeStatus::NoVulkan) { skip("Vulkan yok"); return; }
  CHECK(st == ProbeStatus::Ok);
  if (st != ProbeStatus::Ok) return;
  CHECK(c.lay.cols == 1 && c.lay.shown == 5 && !c.lay.empty);
  const uint32_t list_verts = p.vertices;
  // Bos dizin: bos durum metni govdenin ORTASINDA — o bant zemin renginden
  // farkli piksel icermeli; KONTROL: bantin disinda kalan govde koseleri temiz.
  AssetsCtx e;
  make_files(e);
  e.n = 0;
  out_path(path, sizeof path, "assets_empty.ppm");
  p.ctx = &e;
  st = editor_probe_render(p);
  CHECK(st == ProbeStatus::Ok);
  if (st != ProbeStatus::Ok) return;
  CHECK(e.lay.empty && e.lay.shown == 0);
  const int cx = (int)(e.lay.body_x + e.lay.body_w * 0.5f), cy = (int)(e.lay.body_y + e.lay.body_h * 0.5f);
  uint8_t base[4];
  probe_pixel(p, (uint32_t)(e.lay.body_x + 6), (uint32_t)(e.lay.body_y + e.lay.body_h - 6), base); // govde kosesi: duz zemin
  uint32_t text_px = 0, corner_px = 0;
  for (int y = cy - 12; y <= cy + 12; y++)
    for (int x = cx - 120; x <= cx + 120; x++) {
      uint8_t q[4];
      probe_pixel(p, (uint32_t)x, (uint32_t)y, q);
      if (!near8(q, base, 6)) text_px++;
    }
  for (int y = 0; y < 10; y++)
    for (int x = 0; x < 40; x++) {
      uint8_t q[4];
      probe_pixel(p, (uint32_t)(e.lay.body_x + 4 + x), (uint32_t)(e.lay.body_y + e.lay.body_h - 14 + y), q);
      if (!near8(q, base, 6)) corner_px++;
    }
  std::printf("    [bilgi] liste %u vertex; bos durum: orta bantta zeminden farkli %u piksel (>0), alt-sol kosede %u (0); govde (%.0f,%.0f %.0fx%.0f)\n",
              list_verts, text_px, corner_px, (double)e.lay.body_x, (double)e.lay.body_y, (double)e.lay.body_w, (double)e.lay.body_h);
  CHECK(text_px > 40);
  CHECK(corner_px == 0);
  CHECK(list_verts > 0);
}

// --- Tema: dock sekmeleri --------------------------------------------------------
namespace {
struct DockCtx {
  OverlayCtx ov;
  AssetsCtx as;
  float active_x = -1, active_y = -1, inactive_x = -1, inactive_y = -1;
  float bar_bottom = -1; // sekme cubugunun alt kenari: hemen alti pencere dolgusu (WindowBg)
  bool found = false;
  uint8_t win_bg[3], bg0[3];
};
void draw_dock_probe(void *ctx, uint32_t frame) {
  auto *c = static_cast<DockCtx *>(ctx);
  const ImGuiViewport *vp = ImGui::GetMainViewport();
  const ImGuiID dock = ImGui::DockSpaceOverViewport(ImGui::GetID("SondaDock"), vp, 0);
  if (frame == 0) {
    ImGui::DockBuilderRemoveNode(dock);
    ImGui::DockBuilderAddNode(dock, ImGuiDockNodeFlags_DockSpace);
    ImGui::DockBuilderSetNodeSize(dock, vp->Size);
    ImGuiID left = 0, right = 0, right_top = 0, right_bottom = 0;
    ImGui::DockBuilderSplitNode(dock, ImGuiDir_Left, 0.26f, &left, &right);
    ImGui::DockBuilderSplitNode(right, ImGuiDir_Down, 0.40f, &right_bottom, &right_top);
    ImGui::DockBuilderDockWindow("Sahne", left);
    ImGui::DockBuilderDockWindow("G\xC3\xB6r\xC3\xBCn\xC3\xBCm", right_top);
    ImGui::DockBuilderDockWindow("D\xC3\xBCnya", right_bottom);
    ImGui::DockBuilderDockWindow("Kaynaklar", right_bottom);
    ImGui::DockBuilderFinish(dock);
  }
  if (ImGui::Begin("Sahne")) {
    if (ImGui::Button("Ekle")) {}
    ImGui::SameLine();
    if (ImGui::Button("Sil")) {}
    ImGui::SeparatorText("Hiyerar\xC5\x9Fi");
    const char *rows[4] = {"zemin", "kutu_1", "lamba", "kamera_yolu"};
    for (int i = 0; i < 4; i++) ImGui::Selectable(rows[i], i == 1);
    ImGui::SeparatorText("Tablo");
    if (ImGui::BeginTable("t", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders)) {
      ImGui::TableSetupColumn("ad");
      ImGui::TableSetupColumn("deger");
      ImGui::TableHeadersRow();
      for (int i = 0; i < 3; i++) {
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::Text("satir %d", i);
        ImGui::TableSetColumnIndex(1);
        ImGui::Text("%.2f", 1.5 * i);
      }
      ImGui::EndTable();
    }
    static bool chk = true;
    ImGui::Checkbox("g\xC3\xB6lge", &chk);
    static float sl = 0.4f;
    ImGui::SliderFloat("yo\xC4\x9Funluk", &sl, 0, 1);
  }
  ImGui::End();
  if (ImGui::Begin("G\xC3\xB6r\xC3\xBCn\xC3\xBCm")) {
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    if (avail.x > 2 && avail.y > 2) {
      stand_in(ImGui::GetWindowDrawList(), origin, ImVec2(origin.x + avail.x, origin.y + avail.y));
      ImGui::Dummy(avail);
      c->ov.info.focused = false;
      app::viewport_overlay(app::ViewportRect{origin.x, origin.y, avail.x, avail.y}, c->ov.info, &c->ov.lay);
    }
  }
  ImGui::End();
  if (ImGui::Begin("D\xC3\xBCnya")) ImGui::TextUnformatted("g\xC3\xBCne\xC5\x9F / g\xC3\xB6lge");
  ImGui::End();
  if (ImGui::Begin("Kaynaklar")) {
    app::AssetsAction act;
    app::assets_panel(c->as.view, c->as.dir, c->as.files, c->as.n, c->as.scene, c->as.loaded, c->as.ns, &act, &c->as.lay);
  }
  ImGui::End();
  if (frame == 1) ImGui::SetWindowFocus("Kaynaklar");
  // Son karede: Kaynaklar'in dugumundeki sekme cubugu -> secili / pasif sekme
  // orneklem noktalari (sekmenin sol kenarindan 4 px iceri, dikeyde orta).
  if (ImGuiWindow *w = ImGui::FindWindowByName("Kaynaklar"))
    if (w->DockNode && w->DockNode->TabBar) {
      ImGuiTabBar *tb = w->DockNode->TabBar;
      for (int i = 0; i < tb->Tabs.Size; i++) {
        const ImGuiTabItem &t = tb->Tabs[i];
        const float x = tb->BarRect.Min.x + t.Offset - tb->ScrollingAnim + 4.0f;
        const float y = (tb->BarRect.Min.y + tb->BarRect.Max.y) * 0.5f;
        if (t.ID == tb->SelectedTabId) { c->active_x = x; c->active_y = y; }
        else { c->inactive_x = x; c->inactive_y = y; }
      }
      c->bar_bottom = tb->BarRect.Max.y;
      c->found = c->active_x >= 0 && c->inactive_x >= 0;
    }
  tone8(app::Tone::Bg1, c->win_bg);
  tone8(app::Tone::Bg0, c->bg0);
}
} // namespace

ENGINE_TEST(editor_theme_dock_tab_active_blends_into_window) {
  DockCtx c;
  fill_info(c.ov.info, {15.1f, 12.3f, 14.9f}, {0, 1, -3});
  c.ov.info.hovered = true;
  make_files(c.as);
  EditorProbe p;
  p.width = 1100; p.height = 680;
  p.frames = 4;
  char path[512];
  out_path(path, sizeof path, "dock.ppm");
  p.out_ppm = path;
  p.draw = draw_dock_probe;
  p.ctx = &c;
  const ProbeStatus st = editor_probe_render(p);
  if (st == ProbeStatus::NoVulkan) { skip("Vulkan yok"); return; }
  if (st != ProbeStatus::Ok) std::printf("    [bilgi] sonda: %s\n", p.err);
  CHECK(st == ProbeStatus::Ok);
  if (st != ProbeStatus::Ok) return;
  CHECK(c.found);
  if (!c.found) return;
  uint8_t a[4], b[4];
  probe_pixel(p, (uint32_t)c.active_x, (uint32_t)c.active_y, a);
  probe_pixel(p, (uint32_t)c.inactive_x, (uint32_t)c.inactive_y, b);
  std::printf("    [bilgi] dock sekmesi: secili (%.0f,%.0f) %02X%02X%02X, pasif (%.0f,%.0f) %02X%02X%02X, WindowBg %02X%02X%02X, Bg0 %02X%02X%02X; PPM %s\n",
              (double)c.active_x, (double)c.active_y, a[0], a[1], a[2], (double)c.inactive_x, (double)c.inactive_y, b[0], b[1], b[2], c.win_bg[0],
              c.win_bg[1], c.win_bg[2], c.bg0[0], c.bg0[1], c.bg0[2], path);
  CHECK(near8(a, c.win_bg, 2));  // secili sekme pencere zeminiyle BIRLESIR
  CHECK(!near8(b, c.win_bg, 2)); // KONTROL: pasif sekme ayni renk DEGIL
  CHECK(!near8(a, b, 2));
  // Sekme cubugunun hemen alti (pencere dolgusu) da WindowBg: sekme gercekten
  // "iceriye akar", arada baska renkte bir serit yok.
  uint8_t in[4];
  probe_pixel(p, (uint32_t)(c.active_x + 20.0f), (uint32_t)(c.bar_bottom + 3.0f), in);
  std::printf("    [bilgi] sekme cubugunun 3 px alti (dolgu): %02X%02X%02X\n", in[0], in[1], in[2]);
  CHECK(near8(in, c.win_bg, 2));
}

ENGINE_TEST(editor_overlay_ellipsize_left_keeps_tail_on_slash) {
  struct Ctx {
    char a[64], b[64], c[64], d[64];
    float w_full = 0, w_a = 0;
  } cx{};
  EditorProbe p;
  p.width = 64; p.height = 32;
  p.ctx = &cx;
  p.draw = [](void *v, uint32_t) {
    auto *c = static_cast<Ctx *>(v);
    const char *path = "/mnt/veri/yazilim/Tulpar/engine/tests/assets";
    c->w_full = ImGui::CalcTextSize(path).x;
    app::overlay_ellipsize_left(path, c->w_full + 1.0f, c->a, sizeof c->a); // sigiyor: oldugu gibi
    app::overlay_ellipsize_left(path, ImGui::CalcTextSize("\xE2\x80\xA6/tests/assets").x + 2.0f, c->b, sizeof c->b);
    app::overlay_ellipsize_left(path, ImGui::CalcTextSize("\xE2\x80\xA6ssets").x + 1.0f, c->c, sizeof c->c); // '/' siniri sigmaz: karakter
    app::overlay_ellipsize_left(path, 4.0f, c->d, sizeof c->d); // hicbir sey sigmaz: en az "…" + 1 karakter
    c->w_a = ImGui::CalcTextSize(c->b).x;
  };
  const ProbeStatus st = editor_probe_render(p);
  if (st == ProbeStatus::NoVulkan) { skip("Vulkan yok"); return; }
  CHECK(st == ProbeStatus::Ok);
  std::printf("    [bilgi] soldan kirpma: tam='%s' | '/tests/assets' genisligi='%s' | dar='%s' | 4px='%s'\n", cx.a, cx.b, cx.c, cx.d);
  CHECK(!std::strcmp(cx.a, "/mnt/veri/yazilim/Tulpar/engine/tests/assets"));
  CHECK(!std::strcmp(cx.b, "\xE2\x80\xA6/tests/assets"));
  CHECK(!std::strncmp(cx.c, "\xE2\x80\xA6", 3) && std::strstr(cx.c, "ssets") != nullptr && std::strlen(cx.c) < 3 + 8);
  CHECK(!std::strncmp(cx.d, "\xE2\x80\xA6", 3) && std::strlen(cx.d) >= 4);
  // KONTROL: sagdan kirpan editor_ellipsize ayni genislikte KUYRUGU kaybeder.
  CHECK(cx.w_a > 0);
}

// --- 3B gizmo cilasi: golge hacmi artik "alarm" degil -----------------------------
// Ayni yaricap ve merkezdeki iki tel kutu: golge hacmi (0.35x kalinlik, solgun)
// ile isik yaricapi (tam kalinlik). Golge kutusunun degistirdigi piksel sayisi
// isik kutusununkinden AZ olmali (ince cizgi); ikisi de 0'dan buyuk (gercekten
// cizildiler). PPM: isik ayri konumda, hepsi acik — bakilsin.
#include "core/memory/arena.hpp"
#include "renderer/renderer.hpp"
#include "rhi/device.hpp"
#include "rhi/offscreen.hpp"
#include "rhi/vk_api.hpp"
namespace {
rhi::VkApi g_gz_api;
void gz_rec_scene(VkCommandBuffer cb, void *u) { static_cast<renderer::Renderer *>(u)->record(cb); }
void gz_rec_shadow(VkCommandBuffer cb, void *u) { static_cast<renderer::Renderer *>(u)->record_shadow(cb); }
} // namespace

ENGINE_TEST(editor_gizmo_shadow_volume_is_subtler_than_light_box) {
  if (!rhi::vk_api_load(g_gz_api)) { skip("Vulkan loader yok"); return; }
  static SystemArena sys;
  if (sys.capacity() == 0 && !sys.reserve(64u << 20, "editor_gizmo_probe")) { CHECK(false); return; }
  rhi::Device dev;
  rhi::DeviceConfig dc;
  if (!dev.init(sys, g_gz_api, dc)) { skip("Vulkan cihazi yok"); return; }
  const uint32_t W = 512, H = 384;
  rhi::OffscreenConfig oc;
  oc.srgb = true;
  oc.width = W; oc.height = H;
  oc.clear[0] = 0x12; oc.clear[1] = 0x15; oc.clear[2] = 0x1A; oc.clear[3] = 255;
  rhi::OffscreenResult ores;
  rhi::OffscreenTarget *off = rhi::offscreen_create(dev, sys, oc, &ores);
  if (!off) { CHECK(false); dev.shutdown(); return; }
  renderer::Renderer ren;
  renderer::RendererConfig rc;
  rc.frames_in_flight = 1;
  rc.shadow_size = 0;
  rc.srgb_target = true;
  if (!ren.init(dev, sys, rhi::offscreen_render_pass(off), rc)) { CHECK(false); rhi::offscreen_destroy(off); dev.shutdown(); return; }
  renderer::Vertex cv[24];
  uint32_t ci[36];
  const uint32_t cn = renderer::Renderer::cube(cv, ci);
  const renderer::MeshHandle cube = ren.create_mesh(cv, 24, ci, cn);
  ren.set_camera(Mat4::look_at({9, 6, 12}, {0, 0.5f, 0}, {0, 1, 0}), Mat4::perspective(0.9f, (float)W / (float)H, 0.1f, 80.0f));
  ren.set_render_size(W, H);
  ren.set_light(normalize(Vec3{0.4f, 1.0f, 0.2f}), {0.2f, 0.2f, 0.25f}, 0.9f);

  content::SceneDesc d{};
  d.shadow_center = {0, 0, 0};
  d.shadow_radius = 3.0f;
  d.sun_dir = {0.4f, 1.0f, 0.2f};
  content::SceneEntity le{};
  std::snprintf(le.name, sizeof le.name, "lamba");
  le.components = content::kSceneLight;
  le.light_color = {1.0f, 0.3f, 0.2f};
  le.light_radius = 3.0f; // OLCUM: golge hacmiyle AYNI yaricap ve merkez
  CHECK(d.insert_entity(0, le));

  app::GizmoOptions all_off, only_shadow, only_light, all_on;
  all_off.light_radius = all_off.shadow_volume = all_off.sun_dir = false;
  only_shadow.light_radius = only_shadow.sun_dir = false;
  only_light.shadow_volume = only_light.sun_dir = false;
  static uint8_t px[3][W * H * 4];
  const app::GizmoOptions *plan[3] = {&all_off, &only_shadow, &only_light};
  for (int pass = 0; pass < 3; pass++) {
    ren.begin_frame(0);
    ren.draw(cube, Mat4::translate({0, 0.5f, 0}), {0.6f, 0.6f, 0.6f}); // sahne nesnesi
    ren.draw(cube, Mat4::translate({0, -0.55f, 0}) * Mat4::scale({12, 0.1f, 12}), {0.35f, 0.36f, 0.38f}); // zemin
    app::editor_draw_gizmos(ren, cube, d, nullptr, 0, *plan[pass]);
    if (!rhi::offscreen_render_custom(off, oc, gz_rec_scene, &ren, &ores, gz_rec_shadow)) { CHECK(false); break; }
    std::memcpy(px[pass], ores.pixels, sizeof px[pass]);
  }
  const uint32_t a = probe_diff(px[0], px[1], W * H), b = probe_diff(px[0], px[2], W * H);
  // Ortalama parlaklik farki da olculur: golge kutusu daha solgun.
  auto mean_delta = [&](const uint8_t *q) {
    double sum = 0; uint32_t n = 0;
    for (uint32_t i = 0; i < W * H; i++)
      if (std::memcmp(px[0] + i * 4, q + i * 4, 3) != 0) { sum += (q[i * 4] + q[i * 4 + 1] + q[i * 4 + 2]) / 3.0; n++; }
    return n ? sum / n : 0.0;
  };
  const double ms = mean_delta(px[1]), ml = mean_delta(px[2]);
  std::printf("    [bilgi] gizmo cilasi: golge hacmi %u piksel (ort. parlaklik %.0f), isik kutusu (ayni yaricap) %u piksel (ort. %.0f)\n", a, ms, b, ml);
  const bool virt = test::gpu_is_virtual(dev.caps().device_name);
  if (virt) skip("sanal GPU (Apple Paravirtual): piksel olcumu gercek cihazda");
  else {
    CHECK(a > 100 && b > 100); // ikisi de gercekten cizildi
    CHECK(a < b);              // golge kutusu ince: daha az piksel
  }
  // Bakilacak goruntu: isik baska yerde (kutular ust uste binmesin), hepsi acik.
  d.entities[0].light_radius = 1.5f;
  d.entities[0].pos = {4.5f, 1.0f, -1.0f};
  ren.begin_frame(0);
  ren.draw(cube, Mat4::translate({0, 0.5f, 0}), {0.6f, 0.6f, 0.6f});
  ren.draw(cube, Mat4::translate({0, -0.55f, 0}) * Mat4::scale({12, 0.1f, 12}), {0.35f, 0.36f, 0.38f});
  const uint32_t n_on = app::editor_draw_gizmos(ren, cube, d, nullptr, 0, all_on);
  CHECK(n_on == 26);
  if (rhi::offscreen_render_custom(off, oc, gz_rec_scene, &ren, &ores, gz_rec_shadow)) {
    char path[512];
    out_path(path, sizeof path, "gizmo_3d.ppm");
    rhi::write_ppm(path, ores.pixels, W, H);
    std::printf("    [bilgi] gizmo PPM: %s\n", path);
  }
  ren.shutdown();
  rhi::offscreen_destroy(off);
  dev.shutdown();
}
