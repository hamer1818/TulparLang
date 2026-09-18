// L6 APP — Editor cercevesi: menu / arac / durum cubugu (neden: editor_chrome.hpp).
//
// Uc cubuk da ImGui::BeginViewportSideBar (imgui_internal.h) ile kurulur: bu,
// ana viewport'un calisma alanini daraltan TEK yoldur; sonra gelen
// DockSpaceOverViewport kalan alani doldurur. BeginMainMenuBar da ayni
// fonksiyonun ustundeki ince bir sargidir — yani menu cubugu, arac cubugu ve
// durum cubugu ayni mekanizmayi paylasir, ozel bir yerlesim hesabi yoktur.
//
// Arac cubugu dugmeleri ImGui::Button DEGIL: InvisibleButton + kendi cizimimiz.
// Sebep bolumlu (segmented) gizmo kontrolu — ImGui::Button her kosesini ayni
// yuvarlar, uclu grubun ic koseleri keskin, dis koseleri yuvarlak olmali
// (ImDrawFlags_RoundCornersLeft/Right). Tek yardimci hepsini cizer ki oynat,
// yakala, kaydet dugmeleri de birbirinin aynisi gorunsun.
#include "app/editor_chrome.hpp"

#include <cstdio>

#include "app/editor_ui.hpp"

#include <imgui.h>
#include <imgui_internal.h> // BeginViewportSideBar, IM_TRUNC

namespace tulpar::engine::app {
namespace {

// --- Sonda kaydi ------------------------------------------------------------
// Duz sayilar: baglam kapandiktan sonra da okunur (kapi sonda bittikten sonra
// bakar). Her chrome_* cagrisi YALNIZ kendi girdilerini sifirlar.
struct RectRec {
  float r[4] = {0, 0, 0, 0};
  bool valid = false;
};
RectRec g_rects[(int)ChromeRect::Count];
RectRec g_menu_hdr[kCommandCategoryCount];
RectRec g_menu_item[kCommandCount];
RectRec g_tool[kCommandCount];
ChromeStats g_stats;

void rec(RectRec &o, ImVec2 a, ImVec2 b) {
  o.r[0] = a.x; o.r[1] = a.y; o.r[2] = b.x; o.r[3] = b.y;
  o.valid = true;
}
void rec_item(RectRec &o) { rec(o, ImGui::GetItemRectMin(), ImGui::GetItemRectMax()); }
void rec_clear(RectRec *a, uint32_t n) {
  for (uint32_t i = 0; i < n; i++) a[i] = RectRec{};
}
bool rec_get(const RectRec &o, float out[4]) {
  if (out) { out[0] = o.r[0]; out[1] = o.r[1]; out[2] = o.r[2]; out[3] = o.r[3]; }
  return o.valid;
}

// id -> dizi indeksi; gecersiz id kCommandCount ("yok").
uint32_t idx(CommandId id) {
  const uint32_t v = (uint32_t)id;
  return (v == 0 || v > kCommandCount) ? kCommandCount : v - 1;
}
RectRec &tool_slot(CommandId id) {
  static RectRec sink; // gecersiz id: kaydi yutan yer (dizi disina yazma yok)
  const uint32_t i = idx(id);
  return i < kCommandCount ? g_tool[i] : sink;
}
RectRec &menu_item_slot(CommandId id) {
  static RectRec sink;
  const uint32_t i = idx(id);
  return i < kCommandCount ? g_menu_item[i] : sink;
}

// --- Renk: yalniz palet ------------------------------------------------------
ImVec4 tone(Tone t, float alpha_mul = 1.0f) {
  float c[4];
  editor_tone(t, c);
  return ImVec4(c[0], c[1], c[2], c[3] * alpha_mul);
}
ImU32 tone_u32(Tone t, float alpha_mul = 1.0f) { return ImGui::GetColorU32(tone(t, alpha_mul)); }

// --- Olculer (stil olcegine bagli; ham piksel sabiti yok) -------------------
ImVec2 toolbar_pad() {
  const ImGuiStyle &st = ImGui::GetStyle();
  return ImVec2(st.WindowPadding.x, IM_TRUNC(st.FramePadding.y * 1.5f));
}
float status_pad_y() { return IM_TRUNC(ImGui::GetStyle().FramePadding.y * 0.75f); }
float gap_group() { return IM_TRUNC(ImGui::GetStyle().ItemSpacing.x * 1.5f); } // gruplar arasi (ayirac cevresi)
float gap_item() { return ImGui::GetStyle().ItemInnerSpacing.x; }               // grup ici
constexpr float kStatusFontScale = 0.85f; // durum cubugu: kucuk yazi
// Durum cubugunun kucuk yazi tipi: OLCEKSIZ taban boyut (PushFont bunu ister;
// sonuc = taban * FontScaleMain * FontScaleDpi).
float status_font_base() {
  const ImGuiStyle &st = ImGui::GetStyle();
  const float scale = st.FontScaleMain * st.FontScaleDpi;
  return (scale > 0.0f ? ImGui::GetFontSize() / scale : ImGui::GetFontSize()) * kStatusFontScale;
}

// --- Menu ayiraclari ---------------------------------------------------------
// Bu komutlardan ONCE ayirac (kategoride ilk degilse): Kaydet | Derle,
// Geri al / Yinele | Ekle / Sil. Menu modeli ile cizim ayni listeyi kullanir.
constexpr CommandId k_separator_before[] = {CommandId::FileCompile, CommandId::EditDuplicate};
bool separator_before(CommandId id) {
  for (const CommandId s : k_separator_before)
    if (s == id) return true;
  return false;
}

const char *basename_of(const char *p) {
  if (!p) return "";
  const char *b = p;
  for (const char *q = p; *q; q++)
    if (*q == '/' || *q == '\\') b = q + 1;
  return b;
}

// Ipucu: "Ad  (Kisayol)" + ikinci satirda aciklama. Ikincil kisayol da yazilir
// (menu yalniz birincili gosterebilir; ipucu ikisini de soylesin).
void tooltip_text(const CommandDesc &d, const char *label, char *buf, uint32_t cap) {
  char sc[32], sc2[32];
  command_shortcut_text(d.shortcut, sc, sizeof sc);
  command_shortcut_text(d.shortcut_alt, sc2, sizeof sc2);
  const char *name = (label && *label) ? label : d.name;
  const char *nl = (d.help && d.help[0]) ? "\n" : "";
  const char *help = (d.help && d.help[0]) ? d.help : "";
  if (sc[0] && sc2[0]) std::snprintf(buf, cap, "%s  (%s ya da %s)%s%s", name, sc, sc2, nl, help);
  else if (sc[0]) std::snprintf(buf, cap, "%s  (%s)%s%s", name, sc, nl, help);
  else std::snprintf(buf, cap, "%s%s%s", name, nl, help);
}

// Son ogenin ipucu — pasif dugmede de (kullanici "neden basilmiyor"u okusun).
void tip_last_item(const char *text) {
  if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip | ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("%s", text);
}

// --- Arac cubugu dugmesi ------------------------------------------------------
// Gorunum: iki "acik" hali var ve bilerek FARKLI — hepsi vurgu dolgulu olunca
// "hangi arac secili" ile "hangi anahtarlar acik" ayni sesle konusur (ilk sonda
// oyle ciktiydi: Tasi + Yakala + Gizmolar uc dolu turkuaz). Unity/Blender
// gelenegi: SECILI ARAC ve OYNATILIYOR dolgulu (Fill), ac-kapa anahtarlari ise
// sessiz — vurgu kenarlik + vurgu metin (Outline).
enum class Look : uint8_t { Plain, Fill, Outline };

struct ToolBtn {
  const char *id = "";
  const char *label = "";
  float width = 0.0f;   // 0 = metinden (+ FramePadding)
  Look look = Look::Plain;
  bool enabled = true;
  ImDrawFlags corners = ImDrawFlags_RoundCornersAll;
  bool border = true;   // bolumlu kontrolde grup kenarligi disaridan cizilir (false)
};

// Donus: tiklandi. Pasifken tiklanmaz, IsItemHovered ipucu icin yine calisir
// (AllowWhenDisabled). Plain: Bg3 -> Bg4 (uzerinde) -> AccentLo (basili), metin
// Text. Fill: Accent -> AccentHi (uzerinde) -> AccentLo (basili), metin koyu (Bg0).
// Outline: Plain dolgu + Accent kenarlik + AccentHi metin.
bool tool_button(const ToolBtn &b, float height) {
  const ImGuiStyle &st = ImGui::GetStyle();
  const ImVec2 ts = ImGui::CalcTextSize(b.label);
  const float w = b.width > 0.0f ? b.width : ts.x + st.FramePadding.x * 2.0f;
  const ImVec2 p = ImGui::GetCursorScreenPos();
  const ImVec2 size(w, height);
  ImGui::BeginDisabled(!b.enabled);
  const bool clicked = ImGui::InvisibleButton(b.id, size);
  const bool hovered = ImGui::IsItemHovered();
  const bool held = ImGui::IsItemActive();
  ImGui::EndDisabled();
  const float a = b.enabled ? 1.0f : st.DisabledAlpha;
  Tone bg = held ? Tone::AccentLo : hovered ? Tone::Bg4 : Tone::Bg3;
  Tone line = Tone::Line, text = Tone::Text;
  if (b.look == Look::Fill) {
    // ETKIN kip: DOYGUN DOLGU DEGIL. Onceki hali (tam Accent zemin + koyu
    // metin) arac cubugunda iki buyuk turkuaz slab birakiyordu; etkin dugme
    // "secili" degil "baska bir widget" gibi okunuyor ve vurgunun seyrek
    // kullanim kuralini tek basina ciyordu (EDITOR-TASARIM.md §3).
    // Sektor editorlerinde etkin gecis sakin bir zemin + PARLAK metindir.
    bg = held ? Tone::AccentLo : hovered ? Tone::Bg3 : Tone::Select;
    text = Tone::AccentHi;
  } else if (b.look == Look::Outline) {
    line = Tone::Accent;
    text = Tone::AccentHi;
  }
  ImDrawList *dl = ImGui::GetWindowDrawList();
  const ImVec2 p1(p.x + size.x, p.y + size.y);
  dl->AddRectFilled(p, p1, tone_u32(bg, a), st.FrameRounding, b.corners);
  // 1.92.8+: AddRect(p0, p1, col, rounding, THICKNESS, flags) — eski sira derlenmez.
  if (b.border) dl->AddRect(p, p1, tone_u32(line, a), st.FrameRounding, 1.0f, b.corners);
  const ImVec2 tp(IM_TRUNC(p.x + (w - ts.x) * 0.5f), IM_TRUNC(p.y + (height - ts.y) * 0.5f));
  dl->AddText(tp, tone_u32(text, a), b.label);
  return clicked;
}

// Ince dikey ayirac: gruplar arasinda, dugme yuksekliginden biraz kisa.
void vsep(float height) {
  ImGui::SameLine(0.0f, gap_group());
  const ImVec2 p = ImGui::GetCursorScreenPos();
  const float inset = IM_TRUNC(height * 0.2f);
  ImGui::GetWindowDrawList()->AddLine(ImVec2(p.x + 0.5f, p.y + inset), ImVec2(p.x + 0.5f, p.y + height - inset), tone_u32(Tone::Line));
  ImGui::Dummy(ImVec2(1.0f, height));
  ImGui::SameLine(0.0f, gap_group());
}

// Tablodaki bir komutun arac cubugu dugmesi: ciz, tiklandiysa calistir, kaydet.
void command_button(CommandTable &t, CommandId id, ToolBtn b, float height, RectRec *also) {
  const CommandDesc &d = t.desc(id);
  b.enabled = t.enabled(id);
  if (tool_button(b, height)) t.invoke(id);
  rec_item(tool_slot(id));
  if (also) rec_item(*also);
  g_stats.tools_submitted++;
  char tip[256];
  tooltip_text(d, b.label, tip, sizeof tip);
  tip_last_item(tip);
}

} // namespace

// ============================================================================
// Menu modeli
// ============================================================================

uint32_t chrome_menu_model(const CommandDesc *descs, uint32_t n, CommandId *out, uint32_t cap) {
  uint32_t k = 0;
  auto put = [&](CommandId id) {
    if (out != nullptr && k < cap) out[k] = id;
    k++;
  };
  if (descs == nullptr) return 0;
  for (uint32_t c = 0; c < kCommandCategoryCount; c++) {
    bool first = true;
    for (uint32_t i = 0; i < n; i++) {
      if ((uint32_t)descs[i].category != c) continue;
      if (!first && separator_before(descs[i].id)) put(CommandId::None);
      put(descs[i].id);
      first = false;
    }
  }
  return k;
}

// ============================================================================
// Menu cubugu
// ============================================================================

void chrome_menu_bar(CommandTable &t, const ChromeState &s, ChromeMenuExtra extra) {
  (void)s; // sag ucta urun adi; sahne adi + kirli noktasi arac cubugunda (tek yerde)
  rec_clear(g_menu_hdr, kCommandCategoryCount);
  rec_clear(g_menu_item, kCommandCount);
  g_rects[(int)ChromeRect::MenuBar] = RectRec{};
  g_stats = ChromeStats{};
  if (!ImGui::GetCurrentContext()) return;

  // Model: cizim TAM bu listeyi tuketir; kapi ayni listeyi sayar.
  CommandId model[kCommandCount * 2];
  const uint32_t n = chrome_menu_model(t.all(), CommandTable::count(), model, kCommandCount * 2);
  const uint32_t n_use = n < kCommandCount * 2 ? n : kCommandCount * 2;

  // BeginMainMenuBar false donerse End()'i KENDI cagirmistir (imgui_widgets.cpp).
  if (!ImGui::BeginMainMenuBar()) return;
  rec(g_rects[(int)ChromeRect::MenuBar], ImGui::GetWindowPos(),
      ImVec2(ImGui::GetWindowPos().x + ImGui::GetWindowSize().x, ImGui::GetWindowPos().y + ImGui::GetWindowSize().y));
  const ImGuiStyle &st = ImGui::GetStyle();

  uint32_t cur = kCommandCategoryCount; // acik kategori (yok)
  bool open = false;
  for (uint32_t i = 0; i < n_use; i++) {
    const CommandId id = model[i];
    if (id == CommandId::None) {
      if (open) ImGui::Separator();
      continue;
    }
    const CommandDesc &d = t.desc(id);
    if ((uint32_t)d.category != cur) {
      if (cur != kCommandCategoryCount && open) {
        if (extra.fn) extra.fn(extra.ctx, (CommandCategory)cur);
        ImGui::EndMenu();
      }
      cur = (uint32_t)d.category;
      const char *title = command_category_name(d.category);
      // Baslik dikdortgeni: BeginMenuEx menubar'da Selectable'i ItemSpacing*2 ile
      // cizer; sondanin tiklayacagi merkez bunun icinde kalir.
      const ImVec2 hp = ImGui::GetCursorScreenPos();
      const float hw = ImGui::CalcTextSize(title).x + st.ItemSpacing.x * 2.0f;
      if (cur < kCommandCategoryCount) rec(g_menu_hdr[cur], hp, ImVec2(hp.x + hw, hp.y + ImGui::GetFrameHeight()));
      open = ImGui::BeginMenu(title);
      g_stats.menus_submitted++;
    }
    g_stats.items_enumerated++;
    if (!open) continue;
    char sc[32];
    command_shortcut_text(d.shortcut, sc, sizeof sc);
    if (ImGui::MenuItem(d.name, sc[0] ? sc : nullptr, t.checked(id), t.enabled(id))) t.invoke(id);
    rec_item(menu_item_slot(id));
    g_stats.items_submitted++;
    if (d.help && d.help[0]) {
      char tip[256];
      char sc2[32];
      command_shortcut_text(d.shortcut_alt, sc2, sizeof sc2);
      if (sc2[0]) std::snprintf(tip, sizeof tip, "%s\n(ikincil kısayol: %s)", d.help, sc2);
      else std::snprintf(tip, sizeof tip, "%s", d.help);
      ImGui::SetItemTooltip("%s", tip);
    }
  }
  if (open) {
    if (extra.fn && cur < kCommandCategoryCount) extra.fn(extra.ctx, (CommandCategory)cur);
    ImGui::EndMenu();
  }

  // Sag uc: urun adi, soluk. Sahne adi arac cubugunda (kirli noktasiyla birlikte).
  static const char kBrand[] = "Tulpar Editör";
  const float bw = ImGui::CalcTextSize(kBrand).x;
  ImGui::SameLine(ImGui::GetWindowWidth() - bw - st.WindowPadding.x);
  ImGui::PushStyleColor(ImGuiCol_Text, tone(Tone::TextDim));
  ImGui::TextUnformatted(kBrand);
  ImGui::PopStyleColor();
  ImGui::EndMainMenuBar();
}

// ============================================================================
// Arac cubugu
// ============================================================================

void chrome_toolbar(CommandTable &t, const ChromeState &s, ChromeOutput *out) {
  ChromeOutput local;
  if (out == nullptr) out = &local;
  *out = ChromeOutput{};
  out->snap_value = s.snap_value;
  rec_clear(g_tool, kCommandCount);
  for (int r = (int)ChromeRect::Toolbar; r <= (int)ChromeRect::DirtyDot; r++) g_rects[r] = RectRec{};
  g_stats.tools_submitted = 0;
  if (!ImGui::GetCurrentContext()) return;

  const ImGuiStyle &st = ImGui::GetStyle();
  const float h = chrome_toolbar_height();
  const float bh = ImGui::GetFrameHeight();
  ImGui::PushStyleColor(ImGuiCol_WindowBg, tone(Tone::Bg1));
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, toolbar_pad());
  ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
  const ImGuiWindowFlags flags = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoSavedSettings |
                                 ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus;
  if (ImGui::BeginViewportSideBar("##tulpar_toolbar", ImGui::GetMainViewport(), ImGuiDir_Up, h, flags)) {
    const ImVec2 wp = ImGui::GetWindowPos(), ws = ImGui::GetWindowSize();
    rec(g_rects[(int)ChromeRect::Toolbar], wp, ImVec2(wp.x + ws.x, wp.y + ws.y));
    ImDrawList *dl = ImGui::GetWindowDrawList();
    // Alt kenar cizgisi: serit, altindaki dok alanindan ayrilsin.
    dl->AddLine(ImVec2(wp.x, wp.y + ws.y - 0.5f), ImVec2(wp.x + ws.x, wp.y + ws.y - 0.5f), tone_u32(Tone::Line));
    const float row_y = ImGui::GetCursorScreenPos().y;

    // 1) Oynat / Durdur — sabit genislik: iki metin arasinda dugme zipla(ma)sin.
    {
      static const char kPlay[] = "▶ Oynat", kStop[] = "■ Durdur";
      const float w = ImMax(ImGui::CalcTextSize(kPlay).x, ImGui::CalcTextSize(kStop).x) + st.FramePadding.x * 2.0f;
      ToolBtn b;
      b.id = "##oynat";
      b.label = s.playing ? kStop : kPlay;
      b.width = w;
      b.look = s.playing ? Look::Fill : Look::Plain; // oynatiliyor: dolu vurgu (yuksek sesle)
      command_button(t, CommandId::PlayToggle, b, bh, &g_rects[(int)ChromeRect::Transport]);
    }
    vsep(bh);

    // 2) Gizmo kipi: uclu bolumlu kontrol (esit genislik, dis koseler yuvarlak).
    {
      struct Seg { CommandId id; const char *label; const char *iid; };
      static const Seg segs[3] = {{CommandId::GizmoTranslate, "✥ Taşı", "##gz_tasi"},
                                  {CommandId::GizmoRotate, "↻ Döndür", "##gz_dondur"},
                                  {CommandId::GizmoScale, "▣ Ölçekle", "##gz_olcekle"}};
      float w = 0.0f;
      for (const Seg &sg : segs) w = ImMax(w, ImGui::CalcTextSize(sg.label).x);
      w = IM_TRUNC(w + st.FramePadding.x * 2.0f);
      const ImVec2 g0 = ImGui::GetCursorScreenPos();
      for (int i = 0; i < 3; i++) {
        ToolBtn b;
        b.id = segs[i].iid;
        b.label = segs[i].label;
        b.width = w;
        b.look = s.gizmo_op == i ? Look::Fill : Look::Plain; // secili arac: dolu vurgu
        b.corners = i == 0 ? ImDrawFlags_RoundCornersLeft : i == 2 ? ImDrawFlags_RoundCornersRight : ImDrawFlags_RoundCornersNone;
        b.border = false;
        command_button(t, segs[i].id, b, bh, nullptr);
        if (i < 2) ImGui::SameLine(0.0f, 0.0f);
      }
      const ImVec2 g1(g0.x + w * 3.0f, g0.y + bh);
      dl->AddRect(g0, g1, tone_u32(Tone::Line), st.FrameRounding, 1.0f, ImDrawFlags_RoundCornersAll);
      for (int k = 1; k < 3; k++)
        dl->AddLine(ImVec2(g0.x + w * (float)k + 0.5f, g0.y + 1.0f), ImVec2(g0.x + w * (float)k + 0.5f, g1.y - 1.0f), tone_u32(Tone::Line));
      rec(g_rects[(int)ChromeRect::GizmoSegments], g0, g1);
    }
    vsep(bh);

    // 3) Yakalama (tabloda yok -> ChromeOutput) + adim kaydiraci (acikken).
    {
      ToolBtn b;
      b.id = "##yakala";
      b.label = "⊞ Yakala";
      b.look = s.snap ? Look::Outline : Look::Plain; // anahtar: sessiz (kenarlik + metin)
      if (tool_button(b, bh)) out->snap_toggled = true;
      rec_item(g_rects[(int)ChromeRect::SnapToggle]);
      tip_last_item(s.snap ? "Yakala: açık\nGizmo hareketini adıma yuvarlar" : "Yakala: kapalı\nGizmo hareketini adıma yuvarlar");
      if (s.snap) {
        ImGui::SameLine(0.0f, gap_item());
        ImGui::SetNextItemWidth(IM_TRUNC(ImGui::CalcTextSize("00.00").x + st.FramePadding.x * 2.0f + 8.0f));
        float v = s.snap_value;
        if (ImGui::DragFloat("##yakala_adim", &v, 0.05f, 0.01f, 100.0f, "%.2f")) out->snap_value_changed = true;
        out->snap_value = v;
        rec_item(g_rects[(int)ChromeRect::SnapValue]);
        tip_last_item("Yakalama adımı (dünya birimi)");
      }
    }
    ImGui::SameLine(0.0f, gap_item());

    // 4) Gizmo gorunurlugu — isaret durumu TABLODAN (menudeki tikle ayni kaynak).
    {
      ToolBtn b;
      b.id = "##gizmolar";
      b.label = "☀ Gizmolar";
      b.look = t.checked(CommandId::ViewGizmos) ? Look::Outline : Look::Plain;
      command_button(t, CommandId::ViewGizmos, b, bh, &g_rects[(int)ChromeRect::GizmoVisible]);
    }
    vsep(bh);

    // 5) Kaydet / Derle
    {
      ToolBtn b;
      b.id = "##kaydet";
      b.label = "⬇ Kaydet";
      command_button(t, CommandId::FileSave, b, bh, nullptr);
      ImGui::SameLine(0.0f, gap_item());
      ToolBtn c;
      c.id = "##derle";
      c.label = "⚒ Derle";
      command_button(t, CommandId::FileCompile, c, bh, nullptr);
    }

    // 6) Sag blok: [nokta hucresi][sahne adi]. Nokta yalniz kirliyken cizilir ama
    // hucresi hep ayrilir — ad, kaydedince saga ziplamasin.
    {
      const float left_min = ImGui::GetItemRectMax().x + gap_group();
      const float right = wp.x + ws.x - toolbar_pad().x;
      const char *name = basename_of(s.scene_path);
      if (!*name) name = "(sahne yok)";
      static const char kDot[] = "●";
      const float dot_w = ImGui::CalcTextSize(kDot).x;
      const float avail = right - left_min - dot_w - gap_item();
      char nb[256];
      editor_ellipsize(name, avail > 0.0f ? avail : 0.0f, nb, sizeof nb);
      const float nw = ImGui::CalcTextSize(nb).x;
      float x0 = right - nw - gap_item() - dot_w;
      if (x0 < left_min) x0 = left_min;
      const float ty = IM_TRUNC(row_y + (bh - ImGui::GetTextLineHeight()) * 0.5f);
      if (s.dirty) dl->AddText(ImVec2(IM_TRUNC(x0), ty), tone_u32(Tone::Warn), kDot);
      dl->AddText(ImVec2(IM_TRUNC(x0 + dot_w + gap_item()), ty), tone_u32(s.dirty ? Tone::Text : Tone::TextDim), nb);
      rec(g_rects[(int)ChromeRect::DirtyDot], ImVec2(x0, row_y), ImVec2(x0 + dot_w, row_y + bh));
      rec(g_rects[(int)ChromeRect::SceneName], ImVec2(x0 + dot_w + gap_item(), row_y), ImVec2(x0 + dot_w + gap_item() + nw, row_y + bh));
      // Ipucu: tam yol (ad kirpilmis olabilir) + kirli durumu.
      ImGui::SetCursorScreenPos(ImVec2(x0, row_y));
      ImGui::InvisibleButton("##sahne_adi", ImVec2(right - x0 > 1.0f ? right - x0 : 1.0f, bh));
      char tip[1100];
      std::snprintf(tip, sizeof tip, "%s%s", s.scene_path ? s.scene_path : "(sahne yok)",
                    s.dirty ? "\nKaydedilmemiş değişiklik var (Ctrl+S)" : "\nKaydedildi");
      tip_last_item(tip);
    }
  }
  ImGui::End();
  ImGui::PopStyleVar(2);
  ImGui::PopStyleColor();
}

// ============================================================================
// Durum cubugu
// ============================================================================

void chrome_status_bar(const ChromeState &s) {
  g_rects[(int)ChromeRect::StatusBar] = RectRec{};
  g_rects[(int)ChromeRect::StatusMessage] = RectRec{};
  g_rects[(int)ChromeRect::StatusSegments] = RectRec{};
  if (!ImGui::GetCurrentContext()) return;

  const ImGuiStyle &st = ImGui::GetStyle();
  ImGui::PushFont(nullptr, status_font_base());
  const float line_h = ImGui::GetTextLineHeight();
  const ImVec2 pad(st.WindowPadding.x, status_pad_y());
  const float h = line_h + pad.y * 2.0f;
  ImGui::PushStyleColor(ImGuiCol_WindowBg, tone(Tone::Bg0));
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, pad);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
  const ImGuiWindowFlags flags = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoSavedSettings |
                                 ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus | ImGuiWindowFlags_NoNav;
  if (ImGui::BeginViewportSideBar("##tulpar_status", ImGui::GetMainViewport(), ImGuiDir_Down, h, flags)) {
    const ImVec2 wp = ImGui::GetWindowPos(), ws = ImGui::GetWindowSize();
    rec(g_rects[(int)ChromeRect::StatusBar], wp, ImVec2(wp.x + ws.x, wp.y + ws.y));
    ImDrawList *dl = ImGui::GetWindowDrawList();
    dl->AddLine(ImVec2(wp.x, wp.y + 0.5f), ImVec2(wp.x + ws.x, wp.y + 0.5f), tone_u32(Tone::Line));
    const float y = IM_TRUNC(wp.y + pad.y);

    // Sag olcumler: soldan saga onem sirasi. Sigmazsa SAGDAN dusurulur (kamera
    // ilk gider, secim en son) — hicbir sey sarmaz, ust uste binmez.
    enum { kSegs = 6 };
    char seg[kSegs][96];
    if (s.primary_name && s.primary_name[0]) {
      if (s.selection_count > 1) std::snprintf(seg[0], sizeof seg[0], "%s +%u", s.primary_name, s.selection_count - 1);
      else std::snprintf(seg[0], sizeof seg[0], "%s", s.primary_name);
    } else std::snprintf(seg[0], sizeof seg[0], "seçim yok");
    std::snprintf(seg[1], sizeof seg[1], "geri al %u · yinele %u", s.undo_count, s.redo_count);
    std::snprintf(seg[2], sizeof seg[2], "kare %u · tick %u", s.frame, s.tick);
    std::snprintf(seg[3], sizeof seg[3], "%.1f ms", (double)s.frame_ms);
    std::snprintf(seg[4], sizeof seg[4], "%u varlık", s.entity_count);
    std::snprintf(seg[5], sizeof seg[5], "kamera %.1f, %.1f, %.1f", (double)s.cam_eye[0], (double)s.cam_eye[1], (double)s.cam_eye[2]);
    float w[kSegs];
    float total = 0.0f;
    for (int i = 0; i < kSegs; i++) { w[i] = ImGui::CalcTextSize(seg[i]).x; total += w[i]; }
    const float sep_gap = st.ItemSpacing.x;
    const float min_msg = ImGui::CalcTextSize("mesaj…").x * 3.0f; // mesaja birakilan asgari yer
    const float room = ws.x - pad.x * 2.0f - min_msg;
    int n = kSegs;
    while (n > 1 && total + sep_gap * 2.0f * (float)(n - 1) > room) { n--; total -= w[n]; }

    const float right = wp.x + ws.x - pad.x;
    float x = right;
    for (int i = n - 1; i >= 0; i--) {
      x -= w[i];
      dl->AddText(ImVec2(IM_TRUNC(x), y), tone_u32(i == 0 ? Tone::Text : Tone::TextDim), seg[i]);
      if (i > 0) {
        x -= sep_gap;
        dl->AddLine(ImVec2(IM_TRUNC(x) + 0.5f, y + 2.0f), ImVec2(IM_TRUNC(x) + 0.5f, y + line_h - 2.0f), tone_u32(Tone::Line));
        x -= sep_gap;
      }
    }
    rec(g_rects[(int)ChromeRect::StatusSegments], ImVec2(x, y), ImVec2(right, y + line_h));

    // Sol mesaj: kalan genislige kirpilir ("…"), hicbir zaman sarmaz.
    const float mx = wp.x + pad.x;
    const float max_w = x - sep_gap * 2.0f - mx;
    char mb[256];
    editor_ellipsize(s.status ? s.status : "", max_w > 0.0f ? max_w : 0.0f, mb, sizeof mb);
    dl->AddText(ImVec2(IM_TRUNC(mx), y), tone_u32(Tone::TextDim), mb);
    rec(g_rects[(int)ChromeRect::StatusMessage], ImVec2(mx, y), ImVec2(mx + ImGui::CalcTextSize(mb).x, y + line_h));
  }
  ImGui::End();
  ImGui::PopStyleVar(2);
  ImGui::PopStyleColor();
  ImGui::PopFont();
}

// ============================================================================
// Olculer + sonda
// ============================================================================

float chrome_menu_height() { return ImGui::GetCurrentContext() ? ImGui::GetFrameHeight() : 0.0f; }

float chrome_toolbar_height() {
  if (!ImGui::GetCurrentContext()) return 0.0f;
  return ImGui::GetFrameHeight() + toolbar_pad().y * 2.0f;
}

float chrome_status_height() {
  if (!ImGui::GetCurrentContext()) return 0.0f;
  ImGui::PushFont(nullptr, status_font_base());
  const float h = ImGui::GetTextLineHeight() + status_pad_y() * 2.0f;
  ImGui::PopFont();
  return h;
}

bool chrome_probe_rect(ChromeRect r, float out[4]) {
  const int i = (int)r;
  if (i < 0 || i >= (int)ChromeRect::Count) return false;
  return rec_get(g_rects[i], out);
}
bool chrome_probe_menu_header(CommandCategory c, float out[4]) {
  const uint32_t i = (uint32_t)c;
  if (i >= kCommandCategoryCount) return false;
  return rec_get(g_menu_hdr[i], out);
}
bool chrome_probe_menu_item(CommandId id, float out[4]) {
  const uint32_t i = idx(id);
  if (i >= kCommandCount) return false;
  return rec_get(g_menu_item[i], out);
}
bool chrome_probe_tool(CommandId id, float out[4]) {
  const uint32_t i = idx(id);
  if (i >= kCommandCount) return false;
  return rec_get(g_tool[i], out);
}
ChromeStats chrome_stats() { return g_stats; }

} // namespace tulpar::engine::app
