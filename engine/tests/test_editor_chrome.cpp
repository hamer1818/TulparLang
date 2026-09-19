// editor_chrome kapilari: menu cubugu, arac cubugu, durum cubugu ve dockspace
// calisma alani sozlesmesi.
//
// Her kapi GERCEK yazi tipi + temayla offscreen cizer (EditorProbe) ve pikseli
// TAHMIN ETTIGI yerden degil, cubugun KAYDETTIGI dikdortgenden okur
// (chrome_probe_*). Menu ogeleri ve arac dugmeleri sentetik fare olaylariyla
// (io.AddMousePosEvent / AddMouseButtonEvent, bir kare gecikmeli) GERCEKTEN
// tiklanir; sayan geri cagrilar tablonun hangi komutu calistirdigini soyler.
// Her kapinin pozitif VE negatif kontrolu var; Vulkan yoksa gorunur ATLANDI.
//
// SONDA BUTCESI: editor_probe.cpp'nin statik arenasi (64 MB) sondalar arasinda
// GERI SARILMIYOR; her sonda ~2 x W x H x 4 bayt (okuma tamponu + hedef) yer.
// 2026-09-17'de 14. sonda (1024x600 agirlikli) arenayi tasirdi ve kosum SIGABRT
// ile bitti. Bu yuzden piksel karsilastirmalari TEK bir A/B fikstürüne (durmus /
// oynatiliyor) bagli; tiklamalar tek sondada sirayla. Toplam 8 sonda.
//
// Sonda ciktilari (PPM): $TULPAR_PROBE_DIR ya da gecici dizin;
// engine/tools/ppm2png.py ile PNG'ye cevirip BAK.
#include "tests/test.hpp"
#include "tests/editor_probe.hpp"
#include "app/editor_chrome.hpp"
#include "app/editor_commands.hpp"
#include "app/editor_ui.hpp"
#include <imgui.h>
#include <imgui_internal.h> // DockBuilder (vitrin sondasinda bolunmus dok)
#include <cmath>
#include <cstdio>
#include <cstring>

using namespace tulpar::engine;
using namespace tulpar::engine::test;
using app::ChromeRect;
using app::CommandCategory;
using app::CommandId;

namespace {
// Kategorideki komut sayisi TABLODAN gelir, elle yazilmaz: tabloya komut
// eklenince kapi kendiliginden dogru sayiyi bekler (2026-09-17'de kes/kopyala/
// yapistir eklenince elle yazilmis "4" duserdi — kapi tabloyla ayrisamasin).
uint32_t category_item_count(app::CommandCategory k) {
  const app::CommandDesc *out[app::kCommandCount];
  return app::commands_in_category(app::command_defaults(), app::kCommandCount, k, out, app::kCommandCount);
}

// --- Sonda ciktisi -----------------------------------------------------------
void probe_path(char *buf, size_t n, const char *name) {
  const char *d = std::getenv("TULPAR_PROBE_DIR");
  std::snprintf(buf, n, "%s/%s.ppm", (d && *d) ? d : tmp_dir(), name);
}

// --- Mevcut duman kapisi (korunuyor) ---------------------------------------
void draw_smoke(void *, uint32_t) {
  ImGui::SetNextWindowPos(ImVec2(24, 24));
  ImGui::SetNextWindowSize(ImVec2(320, 160));
  ImGui::Begin("Sonda");
  ImGui::Text("Türkçe: Görünüm Özellikler Işık ğüşiöç");
  ImGui::Text("Simge: ▶ ■ ● ◆ ⚙ ☀ ✓ ✗ ← → ↻");
  char buf[64];
  app::editor_ellipsize("/mnt/veri/yazilim/Tulpar/engine/tests/assets/editor.sahne", 200.0f, buf, sizeof buf);
  ImGui::TextUnformatted(buf);
  float c[4];
  app::editor_tone(app::Tone::AxisX, c);
  ImGui::TextColored(ImVec4(c[0], c[1], c[2], c[3]), "X");
  ImGui::SameLine();
  app::editor_tone(app::Tone::AxisY, c);
  ImGui::TextColored(ImVec4(c[0], c[1], c[2], c[3]), "Y");
  ImGui::SameLine();
  app::editor_tone(app::Tone::AxisZ, c);
  ImGui::TextColored(ImVec4(c[0], c[1], c[2], c[3]), "Z");
  ImGui::End();
}

// --- Sayan komut tablosu -----------------------------------------------------
struct Cnt {
  uint32_t hits = 0;
  bool enabled = true;
  bool checked = false;
};
void cb_hit(void *c) { static_cast<Cnt *>(c)->hits++; }
bool cb_enabled(const void *c) { return static_cast<const Cnt *>(c)->enabled; }
bool cb_checked(const void *c) { return static_cast<const Cnt *>(c)->checked; }

// Arac cubugu tiklama adimi: 3 kare (konum, bas, birak). enable: adim basinda
// etkinlestirilecek komut (pasif -> etkin gecisi ayni sondada olculsun).
struct ClickStep {
  CommandId tool = CommandId::None;
  ChromeRect rect = ChromeRect::Count;
  CommandId enable = CommandId::None;
};

struct Ctx {
  app::CommandTable t;
  Cnt cnt[app::kCommandCount];
  app::ChromeState s;
  app::ChromeOutput out;
  // Sonda ayarlari
  bool with_dock = true;                  // dockspace + iki dok'lu pencere
  bool only_menu = false;                 // negatif kontrol: yalniz menu cubugu
  int open_category = -1;                 // sentetik fare ile acilacak menu
  CommandId click_item = CommandId::None; // acik menude tiklanacak oge
  ClickStep steps[6];
  uint32_t n_steps = 0;
  // Olcumler
  float work_pos_y[2] = {0, 0}, work_size_y[2] = {0, 0}; // [0] kare 0, [1] son kare
  float view_win[4] = {0, 0, 0, 0};                      // dok'lu "Görünüm" penceresi
  uint32_t snap_toggled_frames = 0;
  float menu_h = 0, tool_h = 0, status_h = 0;
  app::ChromeStats stats;
};

Cnt &cnt(Ctx &c, CommandId id) { return c.cnt[(uint32_t)id - 1]; }

void bind_all(Ctx &c) {
  for (uint32_t i = 0; i < app::kCommandCount; i++) c.t.bind((CommandId)(i + 1), cb_hit, &c.cnt[i], cb_enabled, cb_checked);
}

// Vitrin durumu: gercek editorde gorulecek turden degerler.
void fill_state(Ctx &c) {
  c.s = app::ChromeState{};
  c.s.playing = false;
  c.s.dirty = true;
  c.s.scene_path = "/mnt/veri/yazilim/Tulpar/engine/tests/assets/editor.sahne";
  c.s.primary_name = "kure_1";
  c.s.selection_count = 3;
  c.s.undo_count = 3;
  c.s.redo_count = 0;
  c.s.frame = 11;
  c.s.tick = 12;
  c.s.frame_ms = 16.5f;
  c.s.entity_count = 64;
  c.s.gizmo_op = 0;
  c.s.snap = true;
  c.s.snap_value = 0.5f;
  c.s.gizmos_visible = true;
  c.s.status = "yüklendi: /mnt/veri/yazilim/Tulpar/engine/tests/assets/editor.sahne";
  c.s.cam_eye[0] = 15.1f;
  c.s.cam_eye[1] = 12.3f;
  c.s.cam_eye[2] = 14.9f;
  cnt(c, CommandId::ViewGizmos).checked = true;
}

void mouse_to(const float r[4]) { ImGui::GetIO().AddMousePosEvent((r[0] + r[2]) * 0.5f, (r[1] + r[3]) * 0.5f); }
void mouse_button(bool down) { ImGui::GetIO().AddMouseButtonEvent(0, down); }

// Sonda cizimi: cerceve + dockspace + iki dok'lu pencere + sentetik fare takvimi.
// Fare olaylari kare N'de kuyruga girer, N+1'de islenir; menu BASINCA acilir,
// menu ogesi ve dugme BIRAKINCA calisir (imgui_widgets.cpp BeginMenuEx /
// MenuItemEx / ButtonBehavior).
void draw_chrome(void *vctx, uint32_t f) {
  Ctx &c = *static_cast<Ctx *>(vctx);
  ImGuiViewport *vp = ImGui::GetMainViewport();
  for (int i = 0; i < 3; i++) cnt(c, (CommandId)((uint32_t)CommandId::GizmoTranslate + i)).checked = c.s.gizmo_op == i;
  cnt(c, CommandId::PlayToggle).checked = c.s.playing;

  app::chrome_menu_bar(c.t, c.s);
  if (!c.only_menu) {
    app::chrome_toolbar(c.t, c.s, &c.out);
    app::chrome_status_bar(c.s);
    if (c.out.snap_toggled) { c.snap_toggled_frames++; c.s.snap = !c.s.snap; }
  }
  c.menu_h = app::chrome_menu_height();
  c.tool_h = app::chrome_toolbar_height();
  c.status_h = app::chrome_status_height();
  // Calisma alani BU karede: ImGui daraltmayi bir kare gecikmeli uygular, kare 0
  // ile son kare ayri kaydedilir ki gecikme de olculsun.
  const int slot = f == 0 ? 0 : 1;
  c.work_pos_y[slot] = vp->WorkPos.y;
  c.work_size_y[slot] = vp->WorkSize.y;

  if (c.with_dock) {
    const ImGuiID dock = ImGui::DockSpaceOverViewport(ImGui::GetID("SondaDock"), vp, 0);
    if (f == 0) { // varsayilan bolunme: sol %22 sahne listesi, sag gorunum (editor_layout ile ayni desen)
      ImGui::DockBuilderRemoveNode(dock);
      ImGui::DockBuilderAddNode(dock, ImGuiDockNodeFlags_DockSpace);
      ImGui::DockBuilderSetNodeSize(dock, vp->WorkSize);
      ImGuiID left = 0, right = 0;
      ImGui::DockBuilderSplitNode(dock, ImGuiDir_Left, 0.22f, &left, &right);
      ImGui::DockBuilderDockWindow("Sahne", left);
      ImGui::DockBuilderDockWindow("Görünüm", right);
      ImGui::DockBuilderFinish(dock);
    }
    if (ImGui::Begin("Sahne")) {
      static const char *kNames[] = {"zemin", "kure_1", "kure_2", "kutu_1", "isik_gunes", "isik_nokta", "kamera"};
      for (int i = 0; i < 7; i++) ImGui::Selectable(kNames[i], i == 1);
    }
    ImGui::End();
    if (ImGui::Begin("Görünüm")) {
      const ImVec2 p = ImGui::GetWindowPos(), s = ImGui::GetWindowSize();
      c.view_win[0] = p.x; c.view_win[1] = p.y; c.view_win[2] = p.x + s.x; c.view_win[3] = p.y + s.y;
      float dim[4];
      app::editor_tone(app::Tone::TextDim, dim);
      ImGui::TextColored(ImVec4(dim[0], dim[1], dim[2], dim[3]), "3B görünüm (sonda: sahne dokusu yok)");
    }
    ImGui::End();
  }
  c.stats = app::chrome_stats();

  // Sentetik fare takvimi (dikdortgenler BU karede kaydedildi, o yuzden cerceveden sonra).
  float r[4];
  if (c.open_category >= 0) {
    if (f == 1 && app::chrome_probe_menu_header((CommandCategory)c.open_category, r)) mouse_to(r);
    if (f == 2) mouse_button(true);
    if (f == 3) mouse_button(false);
    if (c.click_item != CommandId::None) {
      if (f == 4 && app::chrome_probe_menu_item(c.click_item, r)) mouse_to(r);
      if (f == 5) mouse_button(true);
      if (f == 6) mouse_button(false);
    }
  }
  for (uint32_t k = 0; k < c.n_steps; k++) {
    const uint32_t base = 1 + 3 * k;
    const ClickStep &st = c.steps[k];
    if (f == base) {
      if (st.enable != CommandId::None) cnt(c, st.enable).enabled = true;
      const bool ok = st.tool != CommandId::None ? app::chrome_probe_tool(st.tool, r) : app::chrome_probe_rect(st.rect, r);
      if (ok) mouse_to(r);
    }
    if (f == base + 1) mouse_button(true);
    if (f == base + 2) mouse_button(false);
  }
}

// Sonda pikselleri statik bir tampona bakar ve BIR SONRAKI sondada ezilir;
// karsilastirma icin kopya alinir (ayirma yok: iki statik tampon).
constexpr uint32_t kMaxPix = 1920u * 1080u * 4u;
uint8_t g_pix_a[kMaxPix], g_pix_b[kMaxPix];

ProbeStatus run_probe(Ctx &c, uint32_t w, uint32_t h, uint32_t frames, const char *name, uint8_t *copy, EditorProbe *out = nullptr) {
  EditorProbe p;
  p.width = w;
  p.height = h;
  p.frames = frames;
  p.draw = draw_chrome;
  p.ctx = &c;
  char path[512];
  if (name) { probe_path(path, sizeof path, name); p.out_ppm = path; }
  const ProbeStatus st = editor_probe_render(p);
  if (st == ProbeStatus::Ok && copy) std::memcpy(copy, p.pixels, (size_t)w * h * 4);
  if (st == ProbeStatus::Fail) std::printf("    [bilgi] sonda %s: %s\n", name ? name : "?", p.err);
  if (out) *out = p;
  return st;
}

// Dikdortgen icinde farkli piksel sayisi (RGB).
uint32_t rect_diff(const uint8_t *a, const uint8_t *b, uint32_t W, uint32_t H, const float r[4]) {
  uint32_t d = 0;
  const int x0 = (int)r[0], y0 = (int)r[1], x1 = (int)r[2], y1 = (int)r[3];
  for (int y = y0 < 0 ? 0 : y0; y < y1 && y < (int)H; y++)
    for (int x = x0 < 0 ? 0 : x0; x < x1 && x < (int)W; x++) {
      const size_t i = ((size_t)y * W + (size_t)x) * 4;
      if (a[i] != b[i] || a[i + 1] != b[i + 1] || a[i + 2] != b[i + 2]) d++;
    }
  return d;
}

// Palet tonu -> hedefte okunacak sRGB8. editor_tone tema uygulandiktan sonra
// DOGRUSAL doner (srgb_target=true); sRGB hedef onu geri kodlar, yani piksel
// paletin ham degerine yakin cikar.
void tone_srgb8(app::Tone t, uint8_t out[3]) {
  float c[4];
  app::editor_tone(t, c);
  for (int i = 0; i < 3; i++) {
    const float lin = c[i];
    const float s = lin <= 0.0031308f ? lin * 12.92f : 1.055f * std::pow(lin, 1.0f / 2.4f) - 0.055f;
    out[i] = (uint8_t)(s * 255.0f + 0.5f);
  }
}
int max_ch_diff(const uint8_t *px, const uint8_t tone[3]) {
  int m = 0;
  for (int i = 0; i < 3; i++) {
    const int d = (int)px[i] - (int)tone[i];
    if (d > m) m = d;
    if (-d > m) m = -d;
  }
  return m;
}
const uint8_t *pix_at(const uint8_t *buf, uint32_t W, float x, float y) { return buf + ((size_t)(uint32_t)y * W + (uint32_t)x) * 4; }
// Dugmenin sol ic kenari, dikey orta: kenarlik (1px) ve metin disinda kalan dolgu.
const uint8_t *fill_px(const uint8_t *buf, uint32_t W, const float r[4]) { return pix_at(buf, W, r[0] + 4.0f, (r[1] + r[3]) * 0.5f); }

// Her komut modelde TAM BIR KEZ mi? Donus: ihlal sayisi (0 = temiz).
uint32_t model_violations(const app::CommandDesc *descs, uint32_t n) {
  CommandId model[app::kCommandCount * 4];
  const uint32_t m = app::chrome_menu_model(descs, n, model, app::kCommandCount * 4);
  uint32_t seen[app::kCommandCount] = {};
  uint32_t bad = 0;
  for (uint32_t i = 0; i < m && i < app::kCommandCount * 4; i++) {
    if (model[i] == CommandId::None) continue;
    const uint32_t k = (uint32_t)model[i] - 1;
    if (k >= app::kCommandCount) { bad++; continue; }
    seen[k]++;
  }
  for (uint32_t k = 0; k < app::kCommandCount; k++)
    if (seen[k] != 1) bad++;
  return bad;
}

// --- A/B fiksturu ------------------------------------------------------------
// A: durmus, Tasi, kirli, yakala acik, gizmolar acik, hepsi etkin, yuklendi mesaji.
// B: oynatiliyor, Dondur, temiz, yakala acik, gizmolar KAPALI, Kaydet PASIF, kisa mesaj.
// Yerlesim ikisinde AYNI (sabit genislikler), dikdortgenler A'dan alinir ve B
// icin de gecerlidir. Ilk cagiran ucreti oder; sonrakiler onbellekten okur.
struct Fixture {
  bool ran = false;
  ProbeStatus st = ProbeStatus::Fail;
  uint32_t W = 1280, H = 720;
  uint32_t vertices_a = 0;
  float transport[4], seg[3][4], dot[4], name[4], save[4], gizvis[4], snap[4], msg_a[4], segs_a[4], msg_b[4], segs_b[4], bar[4];
  float work_pos0 = 0, work_size0 = 0, work_pos = 0, work_size = 0, view_win[4], menu_h = 0, tool_h = 0, status_h = 0;
  app::ChromeStats stats_a;
};
Fixture g_fx;
Ctx g_ctx; // buyuk (CommandTable + 64 sayac); yiginda degil

const Fixture &fixture() {
  if (g_fx.ran) return g_fx;
  g_fx.ran = true;
  g_ctx = Ctx{};
  bind_all(g_ctx);
  fill_state(g_ctx);
  EditorProbe p;
  g_fx.st = run_probe(g_ctx, g_fx.W, g_fx.H, 3, "chrome_stopped", g_pix_a, &p);
  if (g_fx.st != ProbeStatus::Ok) return g_fx;
  g_fx.vertices_a = p.vertices;
  g_fx.stats_a = g_ctx.stats;
  g_fx.work_pos0 = g_ctx.work_pos_y[0]; g_fx.work_size0 = g_ctx.work_size_y[0];
  g_fx.work_pos = g_ctx.work_pos_y[1]; g_fx.work_size = g_ctx.work_size_y[1];
  for (int i = 0; i < 4; i++) g_fx.view_win[i] = g_ctx.view_win[i];
  g_fx.menu_h = g_ctx.menu_h; g_fx.tool_h = g_ctx.tool_h; g_fx.status_h = g_ctx.status_h;
  bool ok = app::chrome_probe_rect(ChromeRect::Transport, g_fx.transport);
  ok = app::chrome_probe_tool(CommandId::GizmoTranslate, g_fx.seg[0]) && ok;
  ok = app::chrome_probe_tool(CommandId::GizmoRotate, g_fx.seg[1]) && ok;
  ok = app::chrome_probe_tool(CommandId::GizmoScale, g_fx.seg[2]) && ok;
  ok = app::chrome_probe_rect(ChromeRect::DirtyDot, g_fx.dot) && ok;
  ok = app::chrome_probe_rect(ChromeRect::SceneName, g_fx.name) && ok;
  ok = app::chrome_probe_tool(CommandId::FileSave, g_fx.save) && ok;
  ok = app::chrome_probe_rect(ChromeRect::GizmoVisible, g_fx.gizvis) && ok;
  ok = app::chrome_probe_rect(ChromeRect::SnapToggle, g_fx.snap) && ok;
  ok = app::chrome_probe_rect(ChromeRect::StatusMessage, g_fx.msg_a) && ok;
  ok = app::chrome_probe_rect(ChromeRect::StatusSegments, g_fx.segs_a) && ok;
  ok = app::chrome_probe_rect(ChromeRect::StatusBar, g_fx.bar) && ok;
  if (!ok) { std::printf("    [bilgi] fikstur A: bir sonda dikdortgeni kaydedilmedi\n"); g_fx.st = ProbeStatus::Fail; return g_fx; }

  g_ctx = Ctx{};
  bind_all(g_ctx);
  fill_state(g_ctx);
  g_ctx.s.playing = true;
  g_ctx.s.gizmo_op = 1;
  g_ctx.s.dirty = false;
  g_ctx.s.status = "oynatılıyor";
  cnt(g_ctx, CommandId::ViewGizmos).checked = false;
  cnt(g_ctx, CommandId::FileSave).enabled = false;
  g_fx.st = run_probe(g_ctx, g_fx.W, g_fx.H, 3, "chrome_playing", g_pix_b, &p);
  if (g_fx.st != ProbeStatus::Ok) return g_fx;
  ok = app::chrome_probe_rect(ChromeRect::StatusMessage, g_fx.msg_b);
  ok = app::chrome_probe_rect(ChromeRect::StatusSegments, g_fx.segs_b) && ok;
  float tr_b[4];
  ok = app::chrome_probe_rect(ChromeRect::Transport, tr_b) && ok;
  if (ok)
    for (int i = 0; i < 4; i++) ok = ok && std::fabs(tr_b[i] - g_fx.transport[i]) < 0.5f; // yerlesim A == B
  if (!ok) { std::printf("    [bilgi] fikstur B: dikdortgen eksik ya da yerlesim A'dan farkli\n"); g_fx.st = ProbeStatus::Fail; }
  return g_fx;
}

} // namespace

ENGINE_TEST(editor_probe_renders_font_and_glyphs) {
  EditorProbe p;
  p.width = 400; p.height = 220;
  char path[512];
  probe_path(path, sizeof path, "probe_smoke");
  p.out_ppm = path;
  p.draw = draw_smoke;
  const ProbeStatus st = editor_probe_render(p);
  if (st == ProbeStatus::NoVulkan) { skip("Vulkan yok"); return; }
  if (st != ProbeStatus::Ok) std::printf("    [bilgi] sonda: %s\n", p.err);
  CHECK(st == ProbeStatus::Ok);
  std::printf("    [bilgi] sonda: %u vertex\n", p.vertices);
  CHECK(p.vertices > 0);
}

// Vitrin 1280x720: durmus (A) / oynatiliyor (B) / "Duzen" menusu acik. GORMEK
// icin var; sayimlari da dogrular: 6 menu, 13 oge, 7 arac dugmesi, acik menude 4 oge.
ENGINE_TEST(editor_chrome_showcase_renders_menu_toolbar_status_dock) {
  const Fixture &fx = fixture();
  if (fx.st == ProbeStatus::NoVulkan) { skip("Vulkan yok"); return; }
  CHECK(fx.st == ProbeStatus::Ok);
  if (fx.st != ProbeStatus::Ok) return;
  std::printf("    [bilgi] vitrin A: %u vertex, menu %u, oge %u (cizilen %u), arac dugmesi %u; yukseklik menu %.0f arac %.0f durum %.0f\n", fx.vertices_a,
              fx.stats_a.menus_submitted, fx.stats_a.items_enumerated, fx.stats_a.items_submitted, fx.stats_a.tools_submitted, (double)fx.menu_h,
              (double)fx.tool_h, (double)fx.status_h);
  CHECK(fx.vertices_a > 0);
  CHECK(fx.stats_a.menus_submitted == app::kCommandCategoryCount);
  CHECK(fx.stats_a.items_enumerated == app::kCommandCount);
  CHECK(fx.stats_a.items_submitted == 0); // hicbir menu acik degil
  CHECK(fx.stats_a.tools_submitted == 7); // oynat + 3 gizmo + gizmolar + kaydet + derle
  CHECK(fx.menu_h > 0 && fx.tool_h > fx.menu_h && fx.status_h > 0);

  static Ctx c;
  c = Ctx{};
  bind_all(c);
  fill_state(c);
  c.open_category = (int)CommandCategory::Edit;
  cnt(c, CommandId::EditDelete).enabled = false; // pasif oge menude soluk gorunsun
  const ProbeStatus st = run_probe(c, 1280, 720, 5, "chrome_menu_open", nullptr);
  CHECK(st == ProbeStatus::Ok);
  const uint32_t want_edit = category_item_count(CommandCategory::Edit);
  std::printf("    [bilgi] vitrin menu acik: cizilen oge %u (Duzen: tablodan %u bekleniyor), menu %u\n", c.stats.items_submitted, want_edit,
              c.stats.menus_submitted);
  CHECK(c.stats.items_submitted == want_edit);
}

// Menu modeli: her komut tam bir kez, kategori sirasinda, ayiraclar uclarda degil.
// Pozitif kontrol: kopya id'li bir tablo AYNI denetimden gecemez. Sonda gerekmez.
ENGINE_TEST(editor_chrome_menu_lists_every_command_once) {
  const app::CommandDesc *d = app::command_defaults();
  CommandId model[app::kCommandCount * 2];
  const uint32_t m = app::chrome_menu_model(d, app::kCommandCount, model, app::kCommandCount * 2);
  uint32_t cmds = 0, seps = 0, order_bad = 0, sep_bad = 0;
  uint32_t last_cat = 0;
  for (uint32_t i = 0; i < m; i++) {
    if (model[i] == CommandId::None) {
      seps++;
      if (i == 0 || i + 1 == m || model[i + 1] == CommandId::None) sep_bad++;
      continue;
    }
    cmds++;
    const uint32_t cat = (uint32_t)app::command_default(model[i]).category;
    if (cat < last_cat) order_bad++;
    last_cat = cat;
  }
  std::printf("    [bilgi] menu modeli: %u girdi = %u komut + %u ayirac; sira ihlali %u, ayirac ihlali %u; ihlal %u\n", m, cmds, seps, order_bad,
              sep_bad, model_violations(d, app::kCommandCount));
  CHECK(cmds == app::kCommandCount);
  CHECK(seps >= 1);
  CHECK(order_bad == 0 && sep_bad == 0);
  CHECK(model_violations(d, app::kCommandCount) == 0);
  // Pozitif kontrol: Kaydet iki kez, Derle hic -> 2 ihlal (biri 2 kez, biri 0 kez).
  app::CommandDesc dup[app::kCommandCount];
  for (uint32_t i = 0; i < app::kCommandCount; i++) dup[i] = d[i];
  dup[1] = d[0];
  const uint32_t v = model_violations(dup, app::kCommandCount);
  std::printf("    [bilgi] menu modeli KONTROL (kopya id): ihlal %u (0 olsaydi denetim kor)\n", v);
  CHECK(v == 2);
}

// Uretilen MenuItem gercekten t.invoke'a gidiyor mu: Duzen menusu sentetik fareyle
// acilir, "Geri al" tiklanir -> yalniz EditUndo bir kez. Negatif: pasif oge tiklanir, 0.
ENGINE_TEST(editor_chrome_menu_item_click_invokes_command) {
  static Ctx c;
  c = Ctx{};
  bind_all(c);
  fill_state(c);
  c.open_category = (int)CommandCategory::Edit;
  c.click_item = CommandId::EditUndo;
  ProbeStatus st = run_probe(c, 800, 450, 8, "chrome_menu_click", nullptr);
  if (st == ProbeStatus::NoVulkan) { skip("Vulkan yok"); return; }
  CHECK(st == ProbeStatus::Ok);
  uint32_t others = 0;
  for (uint32_t i = 0; i < app::kCommandCount; i++)
    if ((CommandId)(i + 1) != CommandId::EditUndo) others += c.cnt[i].hits;
  std::printf("    [bilgi] menu tiklama: Geri al %u kez, diger komutlar toplam %u (menu acilinca cizilen oge %u)\n", cnt(c, CommandId::EditUndo).hits,
              others, c.stats.items_submitted);
  CHECK(cnt(c, CommandId::EditUndo).hits == 1);
  CHECK(others == 0);

  c = Ctx{};
  bind_all(c);
  fill_state(c);
  c.open_category = (int)CommandCategory::Edit;
  c.click_item = CommandId::EditUndo;
  cnt(c, CommandId::EditUndo).enabled = false;
  st = run_probe(c, 800, 450, 8, nullptr, nullptr);
  CHECK(st == ProbeStatus::Ok);
  std::printf("    [bilgi] menu tiklama KONTROL (pasif): Geri al %u kez (0 olmali), cizilen oge %u\n", cnt(c, CommandId::EditUndo).hits,
              c.stats.items_submitted);
  CHECK(cnt(c, CommandId::EditUndo).hits == 0);
  CHECK(c.stats.items_submitted == category_item_count(CommandCategory::Edit)); // menu yine acildi, oge yine cizildi — sadece calismadi
}

// Oynat/Durdur ve bolumlu gizmo kontrolu: durum -> piksel. A durmus+Tasi, B oynatiliyor+Dondur.
// Dolgu: oynarken ~Accent, dururken ~Bg3; etkin kip ~Accent, digerleri ~Bg3.
// Kontrol: Yakala dugmesi iki durumda da ayni (0 fark).
ENGINE_TEST(editor_chrome_transport_and_gizmo_segments_reflect_state) {
  const Fixture &fx = fixture();
  if (fx.st == ProbeStatus::NoVulkan) { skip("Vulkan yok"); return; }
  CHECK(fx.st == ProbeStatus::Ok);
  if (fx.st != ProbeStatus::Ok) return;
  // Etkin kip artik doygun Accent DEGIL, sakin Select zemini (bkz.
  // docs/engine/EDITOR-TASARIM.md §6 ve editor_chrome.cpp Look::Fill).
  // Testin ANLAMI degismedi: etkin dugme, kendisine ayrilmis tonda olmali
  // ve pasif dugmelerden AYIRT EDILEBILIR kalmali.
  uint8_t acc[3], bg3[3];
  tone_srgb8(app::Tone::Select, acc);
  tone_srgb8(app::Tone::Bg3, bg3);
  const int d_stop = max_ch_diff(fill_px(g_pix_a, fx.W, fx.transport), bg3);
  const int d_play = max_ch_diff(fill_px(g_pix_b, fx.W, fx.transport), acc);
  const uint32_t diff_tr = rect_diff(g_pix_a, g_pix_b, fx.W, fx.H, fx.transport);
  const uint32_t diff_snap = rect_diff(g_pix_a, g_pix_b, fx.W, fx.H, fx.snap);
  std::printf("    [bilgi] oynat: dugme %.0fx%.0f, dururken piksel-Bg3 farki %d, oynarken piksel-Select farki %d; dugmede %u piksel degisti, Yakala'da %u\n",
              (double)(fx.transport[2] - fx.transport[0]), (double)(fx.transport[3] - fx.transport[1]), d_stop, d_play, diff_tr, diff_snap);
  CHECK(d_stop <= 8);
  CHECK(d_play <= 8);
  CHECK(diff_tr > 100);
  CHECK(diff_snap == 0);
  // Bolumlu kontrol: A'da Tasi, B'de Dondur etkin.
  int da[3], db[3];
  for (int i = 0; i < 3; i++) {
    da[i] = max_ch_diff(fill_px(g_pix_a, fx.W, fx.seg[i]), i == 0 ? acc : bg3);
    db[i] = max_ch_diff(fill_px(g_pix_b, fx.W, fx.seg[i]), i == 1 ? acc : bg3);
  }
  const float w0 = fx.seg[0][2] - fx.seg[0][0], w1 = fx.seg[1][2] - fx.seg[1][0], w2 = fx.seg[2][2] - fx.seg[2][0];
  std::printf("    [bilgi] gizmo kipi: A(Tasi) fark %d/%d/%d, B(Dondur) fark %d/%d/%d (etkin~Select, digerleri~Bg3); bolum genislikleri %.0f/%.0f/%.0f, "
              "bitisik\n",
              da[0], da[1], da[2], db[0], db[1], db[2], (double)w0, (double)w1, (double)w2);
  for (int i = 0; i < 3; i++) { CHECK(da[i] <= 8); CHECK(db[i] <= 8); }
  CHECK(std::fabs(w0 - w1) < 0.5f && std::fabs(w1 - w2) < 0.5f);
  CHECK(std::fabs(fx.seg[0][2] - fx.seg[1][0]) < 0.5f && std::fabs(fx.seg[1][2] - fx.seg[2][0]) < 0.5f);
}

// Kirli noktasi: A kirli -> nokta hucresinde Warn tonu; B temiz -> hucre bos.
// Kontrol: Yakala dugmesi ayni. Ad: taban ad (tam yol degil), sag kenarin icinde.
ENGINE_TEST(editor_chrome_dirty_dot_marks_unsaved_scene) {
  const Fixture &fx = fixture();
  if (fx.st == ProbeStatus::NoVulkan) { skip("Vulkan yok"); return; }
  CHECK(fx.st == ProbeStatus::Ok);
  if (fx.st != ProbeStatus::Ok) return;
  uint8_t warn[3];
  tone_srgb8(app::Tone::Warn, warn);
  const int d_warn = max_ch_diff(pix_at(g_pix_a, fx.W, (fx.dot[0] + fx.dot[2]) * 0.5f, (fx.dot[1] + fx.dot[3]) * 0.5f), warn);
  const uint32_t diff_dot = rect_diff(g_pix_a, g_pix_b, fx.W, fx.H, fx.dot);
  const uint32_t diff_snap = rect_diff(g_pix_a, g_pix_b, fx.W, fx.H, fx.snap);
  const float name_w = fx.name[2] - fx.name[0];
  std::printf("    [bilgi] kirli nokta: hucre %.0fx%.0f, merkez-Warn farki %d; kirli/temiz farkli piksel noktada %u, Yakala'da %u; ad %.0f px genis, sag kenar "
              "%.0f/%u\n",
              (double)(fx.dot[2] - fx.dot[0]), (double)(fx.dot[3] - fx.dot[1]), d_warn, diff_dot, diff_snap, (double)name_w, (double)fx.name[2], fx.W);
  CHECK(d_warn <= 12);
  CHECK(diff_dot > 10);
  CHECK(diff_snap == 0);
  CHECK(name_w > 0 && name_w < 200.0f); // "editor.sahne" ~90px; tam yol 450+ olurdu
  CHECK(fx.name[2] <= (float)fx.W);
}

// Etkinlik ve isaret TABLODAN gelir: B'de Kaydet pasif -> soluk (piksel farki), B'de
// ViewGizmos checked=false -> Gizmolar anahtari kapali cizilir (piksel farki).
// Kontrol: Yakala (tablo disi, iki durumda da acik) 0 fark.
ENGINE_TEST(editor_chrome_enabled_and_checked_come_from_table) {
  const Fixture &fx = fixture();
  if (fx.st == ProbeStatus::NoVulkan) { skip("Vulkan yok"); return; }
  CHECK(fx.st == ProbeStatus::Ok);
  if (fx.st != ProbeStatus::Ok) return;
  const uint32_t diff_save = rect_diff(g_pix_a, g_pix_b, fx.W, fx.H, fx.save);
  const uint32_t diff_giz = rect_diff(g_pix_a, g_pix_b, fx.W, fx.H, fx.gizvis);
  const uint32_t diff_snap = rect_diff(g_pix_a, g_pix_b, fx.W, fx.H, fx.snap);
  const uint32_t area_save = (uint32_t)((fx.save[2] - fx.save[0]) * (fx.save[3] - fx.save[1]));
  std::printf("    [bilgi] tablo -> cizim: Kaydet etkin/pasif farkli piksel %u / %u, Gizmolar isaretli/isaretsiz %u, Yakala (kontrol) %u\n", diff_save,
              area_save, diff_giz, diff_snap);
  CHECK(diff_save > 50);
  CHECK(diff_giz > 20);
  CHECK(diff_snap == 0);
}

// Arac cubugu tiklamalari tek sondada, sirayla: Oynat -> Olcekle -> Kaydet (PASIF,
// calismamali) -> Kaydet (etkinlestirilip, calismali) -> Yakala (tabloda yok:
// ChromeOutput.snap_toggled, uygulama s.snap'i cevirir, adim kaydiraci kaybolur).
ENGINE_TEST(editor_chrome_toolbar_clicks_route_to_table_and_snap_output) {
  static Ctx c;
  c = Ctx{};
  bind_all(c);
  fill_state(c);
  cnt(c, CommandId::FileSave).enabled = false;
  c.steps[0].tool = CommandId::PlayToggle;
  c.steps[1].tool = CommandId::GizmoScale;
  c.steps[2].tool = CommandId::FileSave;                                        // pasif
  c.steps[3].tool = CommandId::FileSave; c.steps[3].enable = CommandId::FileSave; // etkin
  c.steps[4].rect = ChromeRect::SnapToggle;
  c.n_steps = 5;
  const ProbeStatus st = run_probe(c, 800, 450, 1 + 3 * 5 + 2, nullptr, nullptr);
  if (st == ProbeStatus::NoVulkan) { skip("Vulkan yok"); return; }
  CHECK(st == ProbeStatus::Ok);
  uint32_t others = 0;
  for (uint32_t i = 0; i < app::kCommandCount; i++) {
    const CommandId id = (CommandId)(i + 1);
    if (id != CommandId::PlayToggle && id != CommandId::GizmoScale && id != CommandId::FileSave) others += c.cnt[i].hits;
  }
  float v[4];
  const bool value_after = app::chrome_probe_rect(ChromeRect::SnapValue, v);
  std::printf("    [bilgi] arac tiklama: Oynat %u, Olcekle %u, Kaydet %u (pasif 1 + etkin 1 tiklama), diger %u; yakala isaret %u kare, snap=%d, kaydirac "
              "cizildi=%d\n",
              cnt(c, CommandId::PlayToggle).hits, cnt(c, CommandId::GizmoScale).hits, cnt(c, CommandId::FileSave).hits, others, c.snap_toggled_frames,
              (int)c.s.snap, (int)value_after);
  CHECK(cnt(c, CommandId::PlayToggle).hits == 1);
  CHECK(cnt(c, CommandId::GizmoScale).hits == 1);
  CHECK(cnt(c, CommandId::FileSave).hits == 1); // pasifken 0, etkinken 1
  CHECK(others == 0);
  CHECK(c.snap_toggled_frames == 1);
  CHECK(!c.s.snap);
  CHECK(!value_after);
  // Kontrol: fiksturde (tiklamasiz) yakala isareti yok, kaydirac cizili, deger kopyalanmis.
  const Fixture &fx = fixture();
  CHECK(fx.st == ProbeStatus::Ok);
  std::printf("    [bilgi] arac tiklama KONTROL (fikstur B, tiklamasiz): yakala isaret %u, snap=%d, out.snap_value %.2f\n", g_ctx.snap_toggled_frames,
              (int)g_ctx.s.snap, (double)g_ctx.out.snap_value);
  CHECK(g_ctx.snap_toggled_frames == 0);
  CHECK(g_ctx.s.snap);
  CHECK(std::fabs(g_ctx.out.snap_value - 0.5f) < 1e-6f);
}

// Calisma alani sozlesmesi: uc cubuk viewport'un WorkRect'ini daraltir, dockspace
// tam arada oturur (ikinci kareden itibaren; kare 0 gecikmeli). Kontrol: yalniz
// menu cubugu -> alan sadece menu kadar daralir.
ENGINE_TEST(editor_chrome_bars_shrink_work_area_for_dockspace) {
  const Fixture &fx = fixture();
  if (fx.st == ProbeStatus::NoVulkan) { skip("Vulkan yok"); return; }
  CHECK(fx.st == ProbeStatus::Ok);
  if (fx.st != ProbeStatus::Ok) return;
  const float top = fx.menu_h + fx.tool_h;
  const float expect_size = (float)fx.H - top - fx.status_h;
  std::printf("    [bilgi] calisma alani: kare0 y=%.0f h=%.0f (gecikme), son kare y=%.0f h=%.0f; beklenen y=%.0f h=%.0f; Gorunum penceresi y %.0f..%.0f\n",
              (double)fx.work_pos0, (double)fx.work_size0, (double)fx.work_pos, (double)fx.work_size, (double)top, (double)expect_size,
              (double)fx.view_win[1], (double)fx.view_win[3]);
  CHECK(std::fabs(fx.work_pos - top) < 1.0f);
  CHECK(std::fabs(fx.work_size - expect_size) < 1.0f);
  CHECK(fx.work_pos0 == 0.0f); // ImGui bir kare gecikmeli: belgelenmis, olculmus
  // Dok'lanmis pencere cubuklarin arasinda (ustune/altina kacmiyor) ve alani dolduruyor.
  CHECK(fx.view_win[1] >= top - 0.5f);
  CHECK(fx.view_win[3] <= (float)fx.H - fx.status_h + 0.5f);
  CHECK(fx.view_win[3] - fx.view_win[1] > expect_size * 0.5f);

  static Ctx c;
  c = Ctx{};
  bind_all(c);
  fill_state(c);
  c.only_menu = true;
  const uint32_t W = 640, H = 360;
  const ProbeStatus st = run_probe(c, W, H, 3, nullptr, nullptr);
  CHECK(st == ProbeStatus::Ok);
  std::printf("    [bilgi] calisma alani KONTROL (yalniz menu, %ux%u): y=%.0f h=%.0f (menu %.0f)\n", W, H, (double)c.work_pos_y[1],
              (double)c.work_size_y[1], (double)c.menu_h);
  CHECK(std::fabs(c.work_pos_y[1] - c.menu_h) < 1.0f);
  CHECK(std::fabs(c.work_size_y[1] - ((float)H - c.menu_h)) < 1.0f);
}

// Durum cubugu: mesaj sag olcumlerin uzerine BINMEZ, dar pencerede mesaj "…" ile
// kirpilir ve olcumler sagdan dusurulur; tek satir, sarmaz.
ENGINE_TEST(editor_chrome_status_bar_never_overlaps_or_wraps) {
  const Fixture &fx = fixture();
  if (fx.st == ProbeStatus::NoVulkan) { skip("Vulkan yok"); return; }
  CHECK(fx.st == ProbeStatus::Ok);
  if (fx.st != ProbeStatus::Ok) return;
  static Ctx c;
  c = Ctx{};
  bind_all(c);
  fill_state(c); // ayni uzun "yuklendi: <tam yol>" mesaji, 560 px'te sigmaz
  const ProbeStatus st = run_probe(c, 560, 400, 2, "chrome_status_narrow", nullptr);
  CHECK(st == ProbeStatus::Ok);
  float msg_n[4], segs_n[4], bar_n[4];
  CHECK(app::chrome_probe_rect(ChromeRect::StatusMessage, msg_n));
  CHECK(app::chrome_probe_rect(ChromeRect::StatusSegments, segs_n));
  CHECK(app::chrome_probe_rect(ChromeRect::StatusBar, bar_n));
  const float w_a = fx.msg_a[2] - fx.msg_a[0], w_b = fx.msg_b[2] - fx.msg_b[0], w_n = msg_n[2] - msg_n[0];
  const float sg_a = fx.segs_a[2] - fx.segs_a[0], sg_b = fx.segs_b[2] - fx.segs_b[0], sg_n = segs_n[2] - segs_n[0];
  const float line_h = fx.msg_a[3] - fx.msg_a[1];
  std::printf("    [bilgi] durum cubugu: %.0f px yuksek, satir %.0f px; mesaj A %.0f px (olcumler %.0f'da), B kisa %.0f px, dar 560 px'te %.0f px (kirpildi, "
              "olcumler %.0f'da); olcum blogu A %.0f, B %.0f, dar %.0f px (sagdan dusuruldu)\n",
              (double)(fx.bar[3] - fx.bar[1]), (double)line_h, (double)w_a, (double)fx.segs_a[0], (double)w_b, (double)w_n, (double)segs_n[0], (double)sg_a,
              (double)sg_b, (double)sg_n);
  CHECK(w_a > w_b * 3.0f);              // uzun mesaj gercekten uzun cizildi (A)
  CHECK(fx.msg_a[2] <= fx.segs_a[0]);   // ama olcumlerin uzerine binmedi
  CHECK(fx.msg_b[2] <= fx.segs_b[0]);
  CHECK(std::fabs(sg_a - sg_b) < 0.5f); // olcumler mesajdan bagimsiz
  CHECK(w_n < w_a);                     // dar pencerede kirpildi
  CHECK(msg_n[2] <= segs_n[0]);         // ve yine binmedi
  CHECK(sg_n < sg_a);                   // olcumler sagdan dusuruldu
  CHECK(fx.bar[3] - fx.bar[1] < 2.0f * line_h);  // tek satir: yukseklik iki satira yetmez
  CHECK(bar_n[3] - bar_n[1] < 2.0f * line_h);
}
