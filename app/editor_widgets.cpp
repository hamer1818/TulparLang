// L6 APP — editor_widgets: bkz. editor_widgets.hpp.
//
// Tasarim ilkeleri (Unity Inspector / Godot Inspector / Unreal Details):
//  * Etiket solda, soluk; deger sagda, tam genislik. Hizalama tablo ile.
//  * Bilesik widget'lar (vec3, renk secici) PropItem'i alt ogelerinden
//    TOPLAR — geri alma "son oge"ye degil, bu toplama bakar.
//  * Renk yalniz editor_tone / ImGuiCol_*; olculer temanin FramePadding /
//    ItemInnerSpacing'inden turetilir ki UI olcegi (DPI) ile birlikte buyusun.
//  * Sinirli genislige giren her yazi editor_ellipsize'dan gecer; kirpilan
//    yazinin tamami ipucunda gorunur (sessiz kesme yok).
#include "app/editor_widgets.hpp"

#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstring>

#include <imgui.h>
#include <IconsMaterialDesign.h> // ikon makrolari (IconFontCppHeaders, Zlib)
#include <imgui_internal.h> // ClearActiveID (arama kutusu temizlenince metin durumu da sifirlansin)

namespace tulpar::engine::app {
namespace {

ImVec4 tone(Tone t, float alpha_mul = 1.0f) {
  float c[4];
  editor_tone(t, c);
  return ImVec4(c[0], c[1], c[2], c[3] * alpha_mul);
}
ImU32 tone_u32(Tone t, float alpha_mul = 1.0f) { return ImGui::GetColorU32(tone(t, alpha_mul)); }

WidgetRect item_rect() {
  const ImVec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
  return WidgetRect{a.x, a.y, b.x, b.y};
}

const char *g_help = nullptr; // prop_help: bir sonraki satir tuketir
PropVec3Layout g_vec3_layout;
ComponentHeaderLayout g_hdr_layout;
HierarchyRowLayout g_row_layout;
WidgetRect g_last_prop;

// Son ImGui ogesinin etkinlik durumunu PropItem'e ekler (bilesik widget'lar
// her alt ogeden sonra cagirir) ve dikdortgenini kapilar icin birakir.
void accumulate(PropItem &it, bool changed) {
  it.changed |= changed;
  it.activated |= ImGui::IsItemActivated();
  it.deactivated |= ImGui::IsItemDeactivated();
  it.deactivated_after_edit |= ImGui::IsItemDeactivatedAfterEdit();
  g_last_prop = item_rect();
}

// Bir ozellik satiri: etiket hucresi (soluk, dikey ortali, kirpilmis) ve
// deger hucresine gecis (tam genislik). Kirpildiysa ya da yardim varsa ipucu.
void prop_label(const char *label) {
  ImGui::TableNextRow();
  ImGui::TableSetColumnIndex(0);
  ImGui::AlignTextToFramePadding();
  char buf[96];
  editor_ellipsize(label, ImGui::GetContentRegionAvail().x, buf, sizeof buf);
  ImGui::PushStyleColor(ImGuiCol_Text, tone(Tone::TextDim));
  ImGui::TextUnformatted(buf);
  ImGui::PopStyleColor();
  const bool cut = std::strcmp(buf, label) != 0;
  if ((cut || g_help) && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) {
    if (g_help) ImGui::SetTooltip("%s\n%s", label, g_help);
    else ImGui::SetTooltip("%s", label);
  }
  g_help = nullptr;
  ImGui::TableSetColumnIndex(1);
  ImGui::SetNextItemWidth(-FLT_MIN);
}

// Saydam zeminli, ustune gelince beliren kucuk simge dugmesi (baslik "✕",
// arama temizleme). Donus: basildi.
bool ghost_button(const char *label, float size) {
  const ImGuiStyle &s = ImGui::GetStyle();
  ImGui::PushStyleColor(ImGuiCol_Button, tone(Tone::Bg0, 0.0f));
  ImGui::PushStyleColor(ImGuiCol_ButtonHovered, tone(Tone::Bg4));
  ImGui::PushStyleColor(ImGuiCol_ButtonActive, tone(Tone::AccentLo));
  ImGui::PushStyleColor(ImGuiCol_Text, tone(Tone::TextDim));
  ImGui::PushStyleColor(ImGuiCol_Border, tone(Tone::Bg0, 0.0f));
  ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, 0));
  ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, s.FrameRounding * 0.75f);
  const bool pressed = ImGui::Button(label, ImVec2(size, size));
  ImGui::PopStyleVar(2);
  ImGui::PopStyleColor(5);
  return pressed;
}

// Ortalanmis soluk ipucu: buyuk daire simgesi + sarilmis metin, alanin
// ust ucte birinde (Unity'nin "Nothing selected"i gibi).
void centered_hint(const char *hint) {
  const ImGuiStyle &s = ImGui::GetStyle();
  const ImVec2 avail = ImGui::GetContentRegionAvail();
  const ImVec2 p = ImGui::GetCursorScreenPos();
  ImDrawList *dl = ImGui::GetWindowDrawList();
  const float big = s.FontSizeBase * 2.2f;
  ImGui::PushFont(nullptr, big);
  const ImVec2 isz = ImGui::CalcTextSize("\xE2\x97\x8B"); // ○
  const float icon_h = ImGui::GetFontSize();
  ImGui::PopFont();
  const float wrap_w = avail.x > 40.0f ? avail.x - s.WindowPadding.x * 2.0f : avail.x;
  ImGui::PushStyleColor(ImGuiCol_Text, tone(Tone::TextDim));
  const ImVec2 tsz = ImGui::CalcTextSize(hint, nullptr, false, wrap_w);
  const float block_h = icon_h + s.ItemSpacing.y * 2.0f + tsz.y;
  float y = p.y + (avail.y - block_h) * 0.38f;
  if (y < p.y) y = p.y;
  ImGui::PushFont(nullptr, big);
  dl->AddText(ImGui::GetFont(), ImGui::GetFontSize(), ImVec2(p.x + (avail.x - isz.x) * 0.5f, y), tone_u32(Tone::Line), "\xE2\x97\x8B");
  ImGui::PopFont();
  y += icon_h + s.ItemSpacing.y * 2.0f;
  ImGui::SetCursorScreenPos(ImVec2(p.x + (avail.x - tsz.x) * 0.5f, y));
  ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + wrap_w);
  ImGui::TextUnformatted(hint);
  ImGui::PopTextWrapPos();
  ImGui::PopStyleColor();
}

// Turkce-duyarsiz katlama: ASCII kucuk harf; İ/ı -> i, Ğ/ğ -> g, Ş/ş -> s,
// Ö/ö -> o, Ü/ü -> u, Ç/ç -> c. Diger UTF-8 baytlari oldugu gibi gecer.
// Amac "Isik" yazana "isik"i de, "ISIK"i da bulmak — dilbilgisel dogruluk degil.
uint32_t fold(const char *s, char *out, uint32_t cap) {
  uint32_t n = 0;
  if (!s) { out[0] = 0; return 0; }
  const unsigned char *u = (const unsigned char *)s;
  while (*u && n + 1 < cap) {
    unsigned char c = *u;
    if (c < 0x80) {
      out[n++] = (c >= 'A' && c <= 'Z') ? (char)(c + 32) : (char)c;
      u++;
      continue;
    }
    char rep = 0;
    if (c == 0xC4 && u[1]) {
      if (u[1] == 0xB0 || u[1] == 0xB1) rep = 'i';      // İ ı
      else if (u[1] == 0x9E || u[1] == 0x9F) rep = 'g'; // Ğ ğ
    } else if (c == 0xC5 && u[1]) {
      if (u[1] == 0x9E || u[1] == 0x9F) rep = 's';      // Ş ş
    } else if (c == 0xC3 && u[1]) {
      if (u[1] == 0x96 || u[1] == 0xB6) rep = 'o';      // Ö ö
      else if (u[1] == 0x9C || u[1] == 0xBC) rep = 'u'; // Ü ü
      else if (u[1] == 0x87 || u[1] == 0xA7) rep = 'c'; // Ç ç
    }
    if (rep) { out[n++] = rep; u += 2; continue; }
    out[n++] = (char)c;
    u++;
  }
  out[n] = 0;
  return n;
}

// Cizimle buyutec. Metin fontunda (DejaVuSans) U+2315 yok; ikon fontu
// birlestikten sonra ICON_MD_SEARCH da kullanilabilir ama bu cizim
// olceklenirken daha keskin duruyor ve ikon fontu yuklenemezse de calisir.
void draw_magnifier(ImDrawList *dl, ImVec2 c, float size, ImU32 col) {
  const float r = size * 0.30f;
  const float t = size > 14.0f ? 1.6f : 1.2f;
  dl->AddCircle(ImVec2(c.x - r * 0.35f, c.y - r * 0.35f), r, col, 0, t);
  const float d = r * 0.707f;
  dl->AddLine(ImVec2(c.x - r * 0.35f + d, c.y - r * 0.35f + d), ImVec2(c.x + r * 1.15f, c.y + r * 1.15f), col, t + 0.6f);
}

} // namespace

// =============================================================================
// Ozellik tablosu
// =============================================================================

bool prop_begin(const char *id, float label_fraction) {
  if (!(label_fraction > 0.1f)) label_fraction = 0.30f; // NaN de buraya duser
  if (label_fraction > 0.8f) label_fraction = 0.8f;
  const ImGuiStyle &s = ImGui::GetStyle();
  const float avail = ImGui::GetContentRegionAvail().x;
  // Satir araligi: temanin CellPadding.y'si (3) + cerceve yuksekligi zaten
  // hava birakiyor; yatayda etiket ile deger arasina dar bir bosluk yeter.
  ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(s.CellPadding.x * 0.5f, s.CellPadding.y * 0.8f));
  const ImGuiTableFlags flags = ImGuiTableFlags_NoSavedSettings | ImGuiTableFlags_NoPadOuterX | ImGuiTableFlags_SizingFixedFit;
  if (!ImGui::BeginTable(id, 2, flags, ImVec2(-FLT_MIN, 0))) {
    ImGui::PopStyleVar();
    return false;
  }
  // Etiket sutunu UYARLANABILIR. Sabit oran (eski: 0.38) dar panelde deger
  // sutununu ACLIKTAN OLDURUYORDU: 250 px'lik bir panelde etiket 95 px
  // aliyor, kalan 155 px uc alana bolununce metne 26 px kaliyor ve "6.00"
  // KIRPILIYORDU (ekran goruntusunde "6.0(" olarak gorunen hata).
  // Alt sinir: en kisa etiket okunur kalsin. Ust sinir: genis panelde
  // etiket sutunu gereksiz yere buyuyup degerleri sага itmesin.
  // Etiket zaten sigmazsa "..." ile kirpiliyor ve ustune gelince tam adi
  // ipucunda gosteriliyor (bkz. prop_label), yani daraltmak bilgi kaybetmez.
  const float lo = std::floor(56.0f * (s.FramePadding.x / 6.0f)); // olcekle buyur
  const float hi = std::floor(140.0f * (s.FramePadding.x / 6.0f));
  float label_w = std::floor(avail * label_fraction);
  if (label_w < lo) label_w = lo;
  if (label_w > hi) label_w = hi;
  if (label_w > avail * 0.6f) label_w = std::floor(avail * 0.6f); // cok dar panel
  ImGui::TableSetupColumn("etiket", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoResize, label_w);
  ImGui::TableSetupColumn("deger", ImGuiTableColumnFlags_WidthStretch | ImGuiTableColumnFlags_NoResize);
  return true;
}

void prop_end() {
  ImGui::EndTable();
  ImGui::PopStyleVar();
}

void prop_help(const char *text) { g_help = text; }

PropItem prop_vec3(const char *label, float v[3], float speed, float min, float max, const char *fmt) {
  PropItem it;
  ImGui::PushID(label);
  prop_label(label);
  const ImGuiStyle &s = ImGui::GetStyle();
  const float w = ImGui::GetContentRegionAvail().x;
  const float h = ImGui::GetFrameHeight();
  static const char *const kAxis[3] = {"X", "Y", "Z"};
  static const Tone kTone[3] = {Tone::AxisX, Tone::AxisY, Tone::AxisZ};
  // Rozet: harf + iki yanda yarim FramePadding. Alanin sol kenarina BINER
  // (yuvarlak koseyi orter) — tek parca [X|0.000] kapsulu, arada bosluk yok.
  //
  // Rozet zemini eksenin renginde DEGIL, alanin kendi (cokuk) zemininde;
  // eksen rengi yalniz SOL KENARDAKI ince cubukta ve harfte gorunur. Dolu
  // renkli blok, satirda uc koyu doygun dikdortgen olusturup ozellik
  // panelini "oyuncak" gosteriyordu; sektor editorlerinde (UE5) eksen rengi
  // ince bir kenar isaretidir, yuzey degil.
  float badge_w = std::floor(ImGui::CalcTextSize("X").x + s.FramePadding.x);
  const float overlap = s.FrameRounding;
  const float gap = s.ItemInnerSpacing.x;
  float field_w = std::floor((w - 3.0f * (badge_w - overlap) - 2.0f * gap) / 3.0f);
  // Alan, iki yandaki dolgudan SONRA en az "-000.00" kadar metin tasimali.
  // Tasiyamiyorsa rozetten HARFI dusurup yalniz eksen cubugunu birakiyoruz:
  // renk zaten hangi eksen oldugunu soyluyor, sayinin kirpilmasi ise bilgi
  // KAYBIDIR. Once metne yer acilir, en son caresi kirpmadir.
  const float need = ImGui::CalcTextSize("-000.00").x + s.FramePadding.x * 2.0f;
  bool letter = true;
  if (field_w < need) {
    const float bar_only = std::floor(s.FramePadding.x * 0.5f) + 2.0f;
    const float regained = (badge_w - bar_only) * 3.0f;
    if (std::floor((w - 3.0f * (bar_only - overlap) - 2.0f * gap) / 3.0f) >= need) {
      letter = false;
      badge_w = bar_only;
      field_w = std::floor((w - 3.0f * (badge_w - overlap) - 2.0f * gap) / 3.0f);
    }
    (void)regained;
  }
  if (field_w < badge_w) field_w = badge_w;
  ImDrawList *dl = ImGui::GetWindowDrawList();
  ImVec2 p = ImGui::GetCursorScreenPos();
  const ImGuiSliderFlags sf = (min < max) ? ImGuiSliderFlags_AlwaysClamp : ImGuiSliderFlags_None;
  for (int a = 0; a < 3; a++) {
    const float fx = p.x + badge_w - overlap;
    ImGui::SetCursorScreenPos(ImVec2(fx, p.y));
    ImGui::SetNextItemWidth(field_w);
    ImGui::PushID(a);
    const bool ch = ImGui::DragFloat("##v", &v[a], speed, min, max, fmt, sf);
    ImGui::PopID();
    accumulate(it, ch);
    g_vec3_layout.field[a] = item_rect();
    // Rozet alandan SONRA cizilir ki alanin sol kosesini ortsun.
    const ImVec2 b0(p.x, p.y), b1(p.x + badge_w, p.y + h);
    dl->AddRectFilled(b0, b1, tone_u32(Tone::Input), s.FrameRounding, ImDrawFlags_RoundCornersLeft);
    // Eksen cubugu: sol kenarda, cerceve yuksekliginin tamami boyunca.
    // Genislik FramePadding'e oranli (olcekle buyur), en az 3 piksel:
    // tests/test_editor_widgets.cpp rozeti `x0 + 2` pikselinden ornekliyor,
    // daha ince bir cubuk o ornegi kenar yumusatmasinin icine dusururdu.
    float bar_w = std::floor(s.FramePadding.x * 0.5f);
    if (bar_w < 3.0f) bar_w = 3.0f;
    dl->AddRectFilled(b0, ImVec2(b0.x + bar_w, b1.y), tone_u32(kTone[a]), s.FrameRounding,
                      ImDrawFlags_RoundCornersLeft);
    if (letter) {
      const ImVec2 ts = ImGui::CalcTextSize(kAxis[a]);
      dl->AddText(ImVec2(std::floor(b0.x + bar_w + (badge_w - bar_w - ts.x) * 0.5f),
                         std::floor(b0.y + (h - ts.y) * 0.5f)),
                  tone_u32(kTone[a]), kAxis[a]);
    }
    g_vec3_layout.badge[a] = WidgetRect{b0.x, b0.y, b1.x, b1.y};
    p.x = fx + field_w + gap;
  }
  g_last_prop = WidgetRect{g_vec3_layout.badge[0].x0, g_vec3_layout.badge[0].y0, g_vec3_layout.field[2].x1, g_vec3_layout.field[2].y1};
  ImGui::PopID();
  return it;
}

PropItem prop_float(const char *label, float *v, float speed, float min, float max, const char *fmt) {
  PropItem it;
  ImGui::PushID(label);
  prop_label(label);
  const ImGuiSliderFlags sf = (min < max) ? ImGuiSliderFlags_AlwaysClamp : ImGuiSliderFlags_None;
  accumulate(it, ImGui::DragFloat("##v", v, speed, min, max, fmt, sf));
  ImGui::PopID();
  return it;
}

PropItem prop_int(const char *label, int *v, int min, int max) {
  PropItem it;
  ImGui::PushID(label);
  prop_label(label);
  // Surukleme (kaydirac degil): tamsayi alanlari (klip indeksi, sayac) icin
  // Unity'nin metin alani gibi okunur; 0.2 hiz = 5 piksel/birim.
  const ImGuiSliderFlags sf = (min < max) ? ImGuiSliderFlags_AlwaysClamp : ImGuiSliderFlags_None;
  accumulate(it, ImGui::DragInt("##v", v, 0.2f, min, max, "%d", sf));
  ImGui::PopID();
  return it;
}

PropItem prop_text(const char *label, char *buf, uint32_t cap) {
  PropItem it;
  ImGui::PushID(label);
  prop_label(label);
  accumulate(it, ImGui::InputText("##v", buf, cap));
  ImGui::PopID();
  return it;
}

PropItem prop_color(const char *label, float rgb[3]) {
  PropItem it;
  ImGui::PushID(label);
  prop_label(label);
  const ImGuiStyle &s = ImGui::GetStyle();
  const float w = ImGui::GetContentRegionAvail().x;
  const float h = ImGui::GetFrameHeight();
  ImGuiStorage *st = ImGui::GetStateStorage();
  const ImGuiID k_open = ImGui::GetID("acik"), k_seen = ImGui::GetID("kare");
  const ImGuiID k_c[3] = {ImGui::GetID("r0"), ImGui::GetID("g0"), ImGui::GetID("b0")};
  // Widget bir onceki kare cizilmediyse (varlik degisti) acik-durumu BAYATTIR:
  // sessizce sifirla; yoksa baska bir varligin rengine geri alma islemi yazilir.
  bool was_open = st->GetInt(k_open, 0) != 0;
  if (was_open && st->GetInt(k_seen, -2) != ImGui::GetFrameCount() - 1) { was_open = false; st->SetInt(k_open, 0); }

  const ImVec4 col(rgb[0], rgb[1], rgb[2], 1.0f);
  const bool clicked = ImGui::ColorButton("##sw", col, ImGuiColorEditFlags_NoAlpha | ImGuiColorEditFlags_NoTooltip, ImVec2(w, h));
  const WidgetRect r = item_rect();
  g_last_prop = r;
  // Altigen kod ornegin icinde, sagda; acik renkte koyu, koyu renkte acik yazi.
  char hex[16];
  auto to8 = [](float f) { return (int)(f <= 0 ? 0 : f >= 1 ? 255 : f * 255.0f + 0.5f); };
  std::snprintf(hex, sizeof hex, "#%02X%02X%02X", to8(rgb[0]), to8(rgb[1]), to8(rgb[2]));
  const float lum = 0.299f * rgb[0] + 0.587f * rgb[1] + 0.114f * rgb[2];
  ImGui::PushFont(nullptr, s.FontSizeBase * 0.88f);
  const ImVec2 hs = ImGui::CalcTextSize(hex);
  ImGui::GetWindowDrawList()->AddText(ImGui::GetFont(), ImGui::GetFontSize(), ImVec2(r.x1 - hs.x - s.FramePadding.x, r.y0 + (h - hs.y) * 0.5f),
                                      lum > 0.55f ? tone_u32(Tone::Bg0, 0.85f) : tone_u32(Tone::White, 0.85f), hex);
  ImGui::PopFont();
  if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) ImGui::SetTooltip("%s  (%.3f, %.3f, %.3f)", hex, rgb[0], rgb[1], rgb[2]);

  if (clicked) {
    ImGui::OpenPopup("secici");
    st->SetFloat(k_c[0], rgb[0]);
    st->SetFloat(k_c[1], rgb[1]);
    st->SetFloat(k_c[2], rgb[2]);
    st->SetInt(k_open, 1);
    it.activated = true;
  }
  bool open_now = false;
  ImGui::SetNextWindowPos(ImVec2(r.x0, r.y1 + s.ItemInnerSpacing.y * 0.5f));
  if (ImGui::BeginPopup("secici")) {
    open_now = true;
    ImGui::SetNextItemWidth(std::floor(s.FontSizeBase * 13.0f));
    it.changed |= ImGui::ColorPicker3("##p", rgb,
                                      ImGuiColorEditFlags_NoAlpha | ImGuiColorEditFlags_PickerHueBar | ImGuiColorEditFlags_NoSidePreview |
                                          ImGuiColorEditFlags_NoSmallPreview | ImGuiColorEditFlags_DisplayRGB | ImGuiColorEditFlags_DisplayHex);
    ImGui::EndPopup();
  }
  if (open_now) st->SetInt(k_seen, ImGui::GetFrameCount());
  if ((was_open || clicked) && !open_now) {
    st->SetInt(k_open, 0);
    it.deactivated = true;
    it.deactivated_after_edit = rgb[0] != st->GetFloat(k_c[0]) || rgb[1] != st->GetFloat(k_c[1]) || rgb[2] != st->GetFloat(k_c[2]);
  }
  ImGui::PopID();
  return it;
}

PropItem prop_check(const char *label, bool *v) {
  PropItem it;
  ImGui::PushID(label);
  prop_label(label);
  accumulate(it, ImGui::Checkbox("##v", v));
  ImGui::PopID();
  return it;
}

PropItem prop_combo(const char *label, int *v, const char *items_zero_separated) {
  PropItem it;
  ImGui::PushID(label);
  prop_label(label);
  accumulate(it, ImGui::Combo("##v", v, items_zero_separated));
  ImGui::PopID();
  return it;
}

PropItem prop_asset(const char *label, int *index, const char (*names)[128], uint32_t count) {
  PropItem it;
  ImGui::PushID(label);
  prop_label(label);
  const ImGuiStyle &s = ImGui::GetStyle();
  const bool has = (count > 0 && names != nullptr);
  const char *cur = (has && index && *index >= 0 && (uint32_t)*index < count) ? names[*index] : "-";
  char preview[160];
  // Onizleme: ok + iki padding disinda kalan genislige kirp.
  editor_ellipsize(cur, ImGui::GetContentRegionAvail().x - ImGui::GetFrameHeight() - s.FramePadding.x * 2.0f, preview, sizeof preview);
  if (!has) ImGui::BeginDisabled();
  const bool open = ImGui::BeginCombo("##v", preview);
  // Etkinlik bayraklari KUTUNUN kendisinden (liste acikken "son oge" bir
  // Selectable olurdu). Deger degisimi asagida listeden isaretlenir.
  accumulate(it, false);
  if (open) {
    const float lw = ImGui::GetContentRegionAvail().x;
    for (uint32_t i = 0; i < count; i++) {
      ImGui::PushID((int)i);
      char row[160];
      editor_ellipsize(names[i], lw, row, sizeof row);
      const bool sel = index && *index == (int)i;
      if (ImGui::Selectable(row, sel)) {
        if (index && *index != (int)i) { *index = (int)i; it.changed = true; }
      }
      if (std::strcmp(row, names[i]) != 0 && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", names[i]);
      if (sel) ImGui::SetItemDefaultFocus();
      ImGui::PopID();
    }
    ImGui::EndCombo();
  }
  if (!has) ImGui::EndDisabled();
  if (std::strcmp(preview, cur) != 0 && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) ImGui::SetTooltip("%s", cur);
  ImGui::PopID();
  return it;
}

void section_label(const char *text) {
  const ImGuiStyle &s = ImGui::GetStyle();
  ImGui::Dummy(ImVec2(0, s.ItemSpacing.y * 0.4f));
  // Boyut TEK KAYNAKTAN: docs/engine/EDITOR-TASARIM.md §4 tipografi olcegi.
  push_text_size(TextSize::Sm);
  ImGui::PushStyleColor(ImGuiCol_Text, tone(Tone::TextDim));
  ImGui::TextUnformatted(text);
  ImGui::PopStyleColor();
  pop_text_size();
  const ImVec2 t0 = ImGui::GetItemRectMin(), t1 = ImGui::GetItemRectMax();
  const float x_end = ImGui::GetCursorScreenPos().x + ImGui::GetContentRegionAvail().x;
  const float y = std::floor((t0.y + t1.y) * 0.5f) + 0.5f;
  const float x0 = t1.x + s.ItemInnerSpacing.x;
  if (x_end - x0 > 8.0f) ImGui::GetWindowDrawList()->AddLine(ImVec2(x0, y), ImVec2(x_end, y), tone_u32(Tone::Line), 1.0f);
}

// =============================================================================
// Bilesen basligi
// =============================================================================

bool component_header(const char *icon, const char *name, bool *enabled, bool *remove_clicked, bool default_open, Tone icon_tone) {
  if (remove_clicked) *remove_clicked = false;
  const ImGuiStyle &s = ImGui::GetStyle();
  const float h = ImGui::GetFrameHeight();
  const float indent = std::floor(s.IndentSpacing * 0.5f); // govde girintisi olculu (component_end ile AYNI)
  ImGui::PushStyleColor(ImGuiCol_Header, tone(Tone::Bg2));
  ImGui::PushStyleColor(ImGuiCol_HeaderHovered, tone(Tone::Bg3));
  ImGui::PushStyleColor(ImGuiCol_HeaderActive, tone(Tone::Bg4));
  ImGui::PushStyleVar(ImGuiStyleVar_IndentSpacing, indent);
  ImGuiTreeNodeFlags fl = ImGuiTreeNodeFlags_Framed | ImGuiTreeNodeFlags_SpanFullWidth | ImGuiTreeNodeFlags_AllowOverlap |
                          ImGuiTreeNodeFlags_NoTreePushOnOpen; // TreePush'i biz yapariz (ID sirasi: bkz. asagisi)
  if (default_open) fl |= ImGuiTreeNodeFlags_DefaultOpen;
  // Metin bos: ok disinda her seyi biz cizeriz (renkli simge, kirpilmis ad).
  const bool open = ImGui::TreeNodeEx(name, fl, "%s", );
  ImGui::PopStyleVar();
  ImGui::PopStyleColor(3);
  const WidgetRect hr = item_rect();
  g_hdr_layout.header = hr;
  const bool on = enabled ? *enabled : true;

  // Ust ogeler baslik ID'si altinda (iki bilesenin "##on"u carpismasin).
  ImGui::PushID(name);
  float x = hr.x0 + s.FramePadding.x + ImGui::GetFontSize() + s.ItemInnerSpacing.x; // okun sagi
  // Etkin onay kutusu: basligin icinde biraz kucuk (dikey FramePadding yarim).
  g_hdr_layout.check = WidgetRect{};
  if (enabled) {
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(s.FramePadding.x, std::floor(s.FramePadding.y * 0.5f)));
    const float ch = ImGui::GetFrameHeight();
    ImGui::SetCursorScreenPos(ImVec2(x, std::floor(hr.y0 + (h - ch) * 0.5f)));
    ImGui::Checkbox("##on", enabled);
    ImGui::PopStyleVar();
    g_hdr_layout.check = item_rect();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) ImGui::SetTooltip("%s", *enabled ? "Bileşen etkin" : "Bileşen devre dışı");
    x = g_hdr_layout.check.x1 + s.ItemInnerSpacing.x;
  }
  // Kaldir dugmesi: sagda, hayalet.
  const float bw = std::floor(ImGui::GetFontSize() + s.FramePadding.y);
  const float bx = hr.x1 - bw - std::floor(s.FramePadding.x * 0.5f);
  ImGui::SetCursorScreenPos(ImVec2(bx, std::floor(hr.y0 + (h - bw) * 0.5f)));
  if (ghost_button("\xE2\x9C\x95", bw) && remove_clicked) *remove_clicked = true; // ✕
  g_hdr_layout.remove = item_rect();
  if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) ImGui::SetTooltip("Bileşeni kaldır");
  ImGui::PopID();

  // Simge + ad (cizim listesi; oge degil, basligin tiklamasini bolmez).
  ImDrawList *dl = ImGui::GetWindowDrawList();
  const float ty = std::floor(hr.y0 + (h - ImGui::GetFontSize()) * 0.5f);
  if (icon && *icon) {
    dl->AddText(ImVec2(x, ty), on ? tone_u32(icon_tone) : tone_u32(icon_tone, 0.45f), icon);
    x += ImGui::CalcTextSize(icon).x + s.ItemInnerSpacing.x;
  }
  char nb[96];
  const float name_w = bx - s.ItemInnerSpacing.x - x;
  editor_ellipsize(name, name_w, nb, sizeof nb);
  dl->AddText(ImVec2(x, ty), on ? tone_u32(Tone::Text) : tone_u32(Tone::TextDim), nb);
  // Kirpildiysa tam ad basligin ipucunda (baslik dikdortgeni uzerinde).
  if (std::strcmp(nb, name) != 0 && ImGui::IsMouseHoveringRect(ImVec2(hr.x0, hr.y0), ImVec2(hr.x1, hr.y1))) ImGui::SetTooltip("%s", name);

  // Imleci basligin altina; x mevcut girinti baslangicinda kalir (ItemSize
  // her ogeden sonra satir basina doner).
  ImGui::SetCursorScreenPos(ImVec2(ImGui::GetCursorScreenPos().x, hr.y1 + s.ItemSpacing.y));
  if (open) {
    // TreeNodeEx icindeki TreePush'i kapattik: ust ogelerin PushID/PopID'si
    // agac ID'sinin ICINE degil DISINA dussun (yigin LIFO; yoksa PopID agacin
    // kimligini dusurur). Girinti burada, component_end'de Unindent+PopID.
    ImGui::Indent(indent);
    ImGui::PushID(name);
  }
  return open;
}

void component_end() {
  const float indent = std::floor(ImGui::GetStyle().IndentSpacing * 0.5f);
  ImGui::PopID();
  ImGui::Unindent(indent);
  ImGui::Dummy(ImVec2(0, ImGui::GetStyle().ItemSpacing.y * 0.6f));
}

// Bilesen ekleme agaci. `code` alanlari content::kSceneXxx BITLERIDIR ve
// bilerek isimli sabitlerden yaziliyor: ham "1u << 8" yazilirsa scene.hpp'de
// bir bit numarasi kaydiginda menu SESSIZCE yanlis bileseni ekler.
const CreateMenuItem kCompModel[] = {
    {"Model (glTF)", "\xE2\x97\x86", content::kSceneModel, nullptr, 0},
    {"Animasyon", "\xE2\x86\xBB", content::kSceneAnim, nullptr, 0}
};
const CreateMenuItem kCompLight[] = {
    {"I\xC5\x9F\xC4\xB1k", "\xE2\x98\x80", content::kSceneLight, nullptr, 0}
};
const CreateMenuItem kCompPhysics[] = {
    {"Fizik G\xC3\xB6vdesi", "\xE2\x97\xBC", content::kSceneBody, nullptr, 0},
    {"Karakter Kontrolc\xC3\xBC", "\xE2\x8A\x99", content::kSceneCharacter, nullptr, 0},
    {"Fizik Eklemi (Joint)", ICON_MD_LINK, content::kSceneJoint, nullptr, 0}
};
const CreateMenuItem kCompAudio2[] = {
    {"Ses Kayna\xC4\x9F\xC4\xB1",  ICON_MD_VOLUME_UP , content::kSceneAudio, nullptr, 0},
    {"Yank\xC4\xB1 Alan\xC4\xB1 (Reverb)", ICON_MD_WAVES, content::kSceneReverb, nullptr, 0}
};
const CreateMenuItem kCompCamera[] = {
    {"Kamera",  ICON_MD_VIDEOCAM , content::kSceneCamera, nullptr, 0}
};
const CreateMenuItem kCompScript[] = {
    {"Tulpar Betik",  ICON_MD_DESCRIPTION , content::kSceneScript, nullptr, 0}
};
const CreateMenuItem kCompVFX[] = {
    {"Partik\xC3\xBCl Emitter",  ICON_MD_AUTO_AWESOME , content::kSceneParticle, nullptr, 0},
    {"R\xC3\xBCzgar Alan\xC4\xB1", "\xE2\x86\xAF", content::kSceneWind, nullptr, 0}
};
const CreateMenuItem kCompEnvironment[] = {
    {"Arazi (Terrain)",  ICON_MD_TERRAIN , content::kSceneTerrain, nullptr, 0},
    {"Su (Gerstner)",  ICON_MD_WATER , content::kSceneWater, nullptr, 0},
    {"Voksel D\xC3\xBCnyas\xC4\xB1", "\xE2\x96\xA4", content::kSceneVoxel, nullptr, 0},
    {"G\xC3\xB6ky\xC3\xBCz\xC3\xBC (Skybox)", ICON_MD_CLOUD, content::kSceneSkybox, nullptr, 0},
    {"Yans\xC4\xB1ma Sondas\xC4\xB1 (Probe)", ICON_MD_LENS, content::kSceneRefProbe, nullptr, 0}
};
const CreateMenuItem kCompAI[] = {
    {"Yapay Zeka Ajan\xC4\xB1 (NavAgent)",  ICON_MD_DIRECTIONS_RUN , content::kSceneNavAgent, nullptr, 0}
};
const CreateMenuItem kComponentMenu[] = {
    {"Render", nullptr, 0, kCompModel, 2},
    {"VFX", nullptr, 0, kCompVFX, 2},
    {"\xC3\x87""evre (Environment)", nullptr, 0, kCompEnvironment, 5},
    {"I\xC5\x9F\xC4\xB1k", nullptr, 0, kCompLight, 1},
    {"Fizik", nullptr, 0, kCompPhysics, 3},
    {"Kamera", nullptr, 0, kCompCamera, 1},
    {"Ses", nullptr, 0, kCompAudio2, 2},
    {"Betik", nullptr, 0, kCompScript, 1},
    {"Yapay Zeka (AI)", nullptr, 0, kCompAI, 1}
};
const uint32_t kComponentMenuCount = 9;

static bool draw_component_tree(const CreateMenuItem *items, uint32_t count, uint32_t existing, const char *filter, uint32_t *chosen) {
  bool ret = false;
  for (uint32_t i = 0; i < count; i++) {
    const auto &it = items[i];
    if (it.children) {
      // Eger filtre varsa, sadece icinde eslesen olan kategorileri goster
      bool match = false;
      if (filter && filter[0]) {
        for (uint32_t j = 0; j < it.child_count; j++) {
           if (hierarchy_filter_match(it.children[j].label, filter)) { match = true; break; }
        }
        if (!match) continue; // kategori atla
      }
      char buf[128];
      if (it.icon) snprintf(buf, sizeof buf, "%s  %s", it.icon, it.label);
      else snprintf(buf, sizeof buf, "%s", it.label);
      
      bool open = true;
      if (!filter || !filter[0]) {
         open = ImGui::TreeNodeEx(buf, ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanFullWidth);
      } else {
         ImGui::TextUnformatted(buf); // filtre acikken duz baslik
      }
      if (open) {
        if (draw_component_tree(it.children, it.child_count, existing, filter, chosen)) ret = true;
        if (!filter || !filter[0]) ImGui::TreePop();
      }
    } else {
      if (existing & (uint32_t)it.code) continue;
      if (filter && filter[0] && !hierarchy_filter_match(it.label, filter)) continue;
      char buf[128];
      if (it.icon) snprintf(buf, sizeof buf, "%s  %s", it.icon, it.label);
      else snprintf(buf, sizeof buf, "%s", it.label);
      if (ImGui::Selectable(buf)) {
        *chosen = (uint32_t)it.code;
        ret = true;
      }
    }
  }
  return ret;
}

uint32_t component_add_button(const CreateMenuItem *items, uint32_t count, uint32_t existing_components) {
  uint32_t chosen = 0;
  const ImGuiStyle &s = ImGui::GetStyle();
  ImGui::PushID("bilesen_ekle");
  ImGui::Dummy(ImVec2(0, s.ItemSpacing.y * 0.6f));
  if (ImGui::Button("+  Bile\xC5\x9F" "en ekle", ImVec2(-FLT_MIN, 0))) ImGui::OpenPopup("liste");
  const WidgetRect b = item_rect();
  ImGui::SetNextWindowPos(ImVec2(b.x0, b.y1 + s.ItemInnerSpacing.y * 0.5f));
  ImGui::SetNextWindowSize(ImVec2(b.x1 - b.x0, 0));
  if (ImGui::BeginPopup("liste")) {
    static char filter[64] = {0};
    ImGui::SetNextItemWidth(-FLT_MIN);
    if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
    ImGui::InputTextWithHint("##arama",  ICON_MD_SEARCH " Arama...", filter, sizeof filter);
    ImGui::Separator();
    
    if (count == 0) ImGui::TextDisabled("Eklenecek bile\xC5\x9F" "en kalmad\xC4\xB1");
    if (draw_component_tree(items, count, existing_components, filter, &chosen)) {
       ImGui::CloseCurrentPopup();
       filter[0] = 0; // kapatinca sifirla
    }
    ImGui::EndPopup();
  }
  ImGui::PopID();
  return chosen;
}

const CreateMenuItem kCreate3D[] = {
    {"K\xC3\xBCp", "\xE2\x97\xBC", 10, nullptr, 0},
    {"K\xC3\xBCre", "\xE2\x97\x8F", 11, nullptr, 0},
    {"Kaps\xC3\xBCl", "\xE2\x97\xBC", 20, nullptr, 0},
    {"Silindir", "\xE2\x97\xBC", 21, nullptr, 0},
    {"Koni", "\xE2\x97\xBC", 22, nullptr, 0},
    {"D\xC3\xBCzlem", "\xE2\x96\xAC", 8, nullptr, 0},
    {"D\xC3\xB6rtgen", "\xE2\x96\xAC", 23, nullptr, 0},
    {"Simit", "\xE2\x97\xBC", 24, nullptr, 0}
};
const CreateMenuItem kCreateLight[] = {
    {"Y\xC3\xB6nl\xC3\xBC (G\xC3\xBCne\xC5\x9F)", "\xE2\x86\x97", 14, nullptr, 0},
    {"Nokta", "\xE2\x97\x8F", 3, nullptr, 0}
};
const CreateMenuItem kCreatePhysics[] = {
    {"Sabit Kutu G\xC3\xB6vde", "\xE2\x96\xA1", 4, nullptr, 0},
    {"Sabit K\xC3\xBCre G\xC3\xB6vde", "\xE2\x97\x8B", 5, nullptr, 0},
    {"Dinamik Kutu G\xC3\xB6vde", "\xE2\x96\xA7", 6, nullptr, 0},
    {"Dinamik K\xC3\xBCre G\xC3\xB6vde", "\xE2\x97\x8D", 7, nullptr, 0}
};
const CreateMenuItem kCreateAudio[] = {
    {"Ses Kayna\xC4\x9F\xC4\xB1",  ICON_MD_VOLUME_UP , 13, nullptr, 0}
};
const CreateMenuItem kCreateMenu[] = {
    {"Bo\xC5\x9F Varl\xC4\xB1k", "\xE2\x97\x8B", 1, nullptr, 0},
    {"Model (glTF)", "\xE2\x97\x86", 2, nullptr, 0},
    {"Animasyonlu Model", "\xE2\x86\xBB", 9, nullptr, 0},
    {"3B Nesne", nullptr, 0, kCreate3D, 8},
    {"I\xC5\x9F\xC4\xB1k", "\xE2\x98\x80", 0, kCreateLight, 2},
    {"Fizik", nullptr, 0, kCreatePhysics, 4},
    {"Ses", nullptr, 0, kCreateAudio, 1},
    {"Kamera Varl\xC4\xB1\xC4\x9F\xC4\xB1",  ICON_MD_VIDEOCAM , 12, nullptr, 0}
};
const uint32_t kCreateMenuCount = 8;

int create_menu_draw(const CreateMenuItem *items, uint32_t count) {
  int result = 0;
  for (uint32_t i = 0; i < count; i++) {
    const auto &it = items[i];
    if (it.children) {
      char buf[128];
      if (it.icon) snprintf(buf, sizeof buf, "%s  %s", it.icon, it.label);
      else snprintf(buf, sizeof buf, "%s", it.label);
      if (ImGui::BeginMenu(buf)) {
        int r = create_menu_draw(it.children, it.child_count);
        if (r) result = r;
        ImGui::EndMenu();
      }
    } else {
      char buf[128];
      if (it.icon) snprintf(buf, sizeof buf, "%s  %s", it.icon, it.label);
      else snprintf(buf, sizeof buf, "%s", it.label);
      if (ImGui::MenuItem(buf)) result = it.code;
    }
  }
  return result;
}

// =============================================================================
// Inspector baslik satiri
// =============================================================================

PropItem inspector_title(const char *icon, char *name, uint32_t cap, const char *subtitle, Tone icon_tone) {
  PropItem it;
  const ImGuiStyle &s = ImGui::GetStyle();
  ImGui::PushID("baslik");
  push_text_size(TextSize::Lg); // olcek jetonu (docs/engine/EDITOR-TASARIM.md §4)
  ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(s.FramePadding.x, std::floor(s.FramePadding.y * 1.5f)));
  const float h = ImGui::GetFrameHeight();
  const ImVec2 p = ImGui::GetCursorScreenPos();
  ImDrawList *dl = ImGui::GetWindowDrawList();
  float x = p.x;
  if (icon && *icon) {
    const ImVec2 isz = ImGui::CalcTextSize(icon);
    dl->AddText(ImGui::GetFont(), ImGui::GetFontSize(), ImVec2(p.x + s.FramePadding.x * 0.5f, std::floor(p.y + (h - isz.y) * 0.5f)), tone_u32(icon_tone), icon);
    x += isz.x + s.FramePadding.x * 0.5f + s.ItemInnerSpacing.x;
  }
  ImGui::SetCursorScreenPos(ImVec2(x, p.y));
  ImGui::SetNextItemWidth(-FLT_MIN);
  // Ad kutusu BASLIK gibi okunmali. Onceki hali (Bg3 %35) panelden ACIK bir
  // dikdortgen biraktigi icin "bos bir duzenleme alani" gibi duruyordu.
  // Modern desen: sakin haldeyken zemin YOK (duz baslik); girdi yuzu yalniz
  // ETKILESIMDE ortaya cikar -- uzerine gelince cokuk yuzey, yazarken daha
  // koyu. Hem baslik gibi okunur hem yeniden adlandirilabilir oldugu
  // kesfedilebilir kalir.
  ImGui::PushStyleColor(ImGuiCol_FrameBg, tone(Tone::Bg1, 0.0f));
  ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, tone(Tone::Input));
  ImGui::PushStyleColor(ImGuiCol_FrameBgActive, tone(Tone::Bg0));
  ImGui::PushStyleColor(ImGuiCol_Border, tone(Tone::Line, 0.0f));
  accumulate(it, ImGui::InputText("##ad", name, cap));
  ImGui::PopStyleColor(4);
  ImGui::PopStyleVar();
  pop_text_size();
  if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal) && !ImGui::IsItemActive()) ImGui::SetTooltip("Varlık adı (yeniden adlandırmak için tıkla)");
  if (subtitle && *subtitle) {
    push_text_size(TextSize::Sm);
    ImGui::PushStyleColor(ImGuiCol_Text, tone(Tone::TextDim));
    ImGui::SetCursorScreenPos(ImVec2(x + s.FramePadding.x, ImGui::GetCursorScreenPos().y - std::floor(s.ItemSpacing.y * 0.4f)));
    char sb[128];
    editor_ellipsize(subtitle, ImGui::GetContentRegionAvail().x - s.FramePadding.x, sb, sizeof sb);
    ImGui::TextUnformatted(sb);
    if (std::strcmp(sb, subtitle) != 0 && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", subtitle);
    ImGui::PopStyleColor();
    pop_text_size();
  }
  ImGui::Dummy(ImVec2(0, s.ItemSpacing.y * 0.2f));
  ImGui::Separator();
  ImGui::PopID();
  return it;
}

void inspector_empty(const char *hint) { centered_hint(hint); }

// =============================================================================
// Hiyerarsi
// =============================================================================

bool hierarchy_search(char *buf, uint32_t cap) {
  const ImGuiStyle &s = ImGui::GetStyle();
  ImGui::PushID("ara");
  const float icon_w = std::floor(ImGui::GetFontSize() * 0.95f);
  const float h = ImGui::GetFrameHeight();
  // Metin buyutecin sagindan baslasin: sol FramePadding buyutulur (ImGui
  // paddingi iki yana esit uygular; sagdaki fazlalik ✕'e yer olur).
  ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(s.FramePadding.x + icon_w + s.ItemInnerSpacing.x, s.FramePadding.y));
  ImGui::SetNextItemWidth(-FLT_MIN);
  ImGui::SetNextItemAllowOverlap();
  bool changed = ImGui::InputTextWithHint("##q", "Ara…", buf, cap);
  ImGui::PopStyleVar();
  const WidgetRect r = item_rect();
  draw_magnifier(ImGui::GetWindowDrawList(), ImVec2(r.x0 + s.FramePadding.x + icon_w * 0.5f, std::floor((r.y0 + r.y1) * 0.5f)), icon_w,
                 tone_u32(Tone::TextDim));
  if (buf[0]) {
    const float bw = std::floor(ImGui::GetFontSize() + s.FramePadding.y);
    ImGui::SetCursorScreenPos(ImVec2(r.x1 - bw - std::floor(s.FramePadding.x * 0.5f), std::floor(r.y0 + (h - bw) * 0.5f)));
    if (ghost_button("\xE2\x9C\x95", bw)) { // ✕
      buf[0] = 0;
      changed = true;
      // Metin kutusu etkinken tampon disaridan silinirse ImGui kendi kopyasini
      // geri yazar; etkin ogeyi birakinca kutuyu tampondan yeniden okur.
      ImGui::ClearActiveID();
    }
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) ImGui::SetTooltip("Süzgeci temizle");
  }
  ImGui::SetCursorScreenPos(ImVec2(ImGui::GetCursorScreenPos().x, r.y1 + s.ItemSpacing.y));
  ImGui::PopID();
  return changed;
}

bool hierarchy_filter_match(const char *name, const char *filter) {
  if (!filter || !filter[0]) return true;
  if (!name) return false;
  char fn[256], ff[256];
  fold(name, fn, sizeof fn);
  fold(filter, ff, sizeof ff);
  return std::strstr(fn, ff) != nullptr;
}

// --- agac durumu (cagiran sahibi) --------------------------------------------
void HierarchyCollapse::set(uint32_t i, bool v) {
  if (i >= kMax) return;
  if (v) bits[i >> 5] |= 1u << (i & 31);
  else bits[i >> 5] &= ~(1u << (i & 31));
}
void HierarchyCollapse::clear() {
  for (uint32_t k = 0; k < (kMax + 31) / 32; k++) bits[k] = 0;
}
void HierarchyCollapse::after_remove(uint32_t removed) {
  if (removed >= kMax) return;
  for (uint32_t i = removed; i + 1 < kMax; i++) set(i, collapsed(i + 1));
  set(kMax - 1, false);
}
void HierarchyCollapse::after_insert(uint32_t at) {
  if (at >= kMax) return;
  for (uint32_t i = kMax - 1; i > at; i--) set(i, collapsed(i - 1));
  set(at, false);
}
void HierarchyRename::begin(int32_t i, const char *name) {
  index = i;
  std::snprintf(buf, sizeof buf, "%s", name ? name : );
  focus = true;
}
void hierarchy_begin_rename(HierarchyState *st, int32_t index, const char *name) {
  if (st) st->rename.begin(index, name);
}

namespace {
// Satirin ortak govdesi. `st` null ise agac sureclerinin hicbiri acilmaz —
// eski duz hierarchy_row tam olarak bu yoldan geciyor (ikinci bir kopya yok).
HierarchyResult hierarchy_row_impl(int id, const HierarchyRow &r, HierarchyState *st) {
  HierarchyResult res;
  res.index = id;
  const ImGuiStyle &s = ImGui::GetStyle();
  ImGui::PushID(id);
  const float fs = ImGui::GetFontSize();
  const float h = std::floor(fs + s.FramePadding.y * 1.5f);
  const float step = std::floor(fs * 0.85f); // bir derinlik kademesi
  const float indent = step * (float)r.depth;
  const float arrow_col = std::floor(fs * 0.9f);

  // --- yerinde ad duzenleme: satirin YERINE metin kutusu -----------------------
  if (st && st->rename.index == id) {
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(s.ItemSpacing.x, 2.0f));
    if (indent + arrow_col > 0) { ImGui::Dummy(ImVec2(indent + arrow_col, h)); ImGui::SameLine(0, 0); }
    ImGui::SetNextItemWidth(-FLT_MIN);
    if (st->rename.focus) { ImGui::SetKeyboardFocusHere(); st->rename.focus = false; }
    const bool enter = ImGui::InputText("##ad", st->rename.buf, sizeof st->rename.buf,
                                        ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
    const bool left = ImGui::IsItemDeactivated();
    ImGui::PopStyleVar();
    g_row_layout.row = item_rect();
    std::snprintf(g_row_layout.text, sizeof g_row_layout.text, "%s", st->rename.buf);
    if (enter && st->rename.buf[0]) { // bos ad kabul edilmez (sessizce degil: kip acik kalir)
      res.action = HierarchyAction::Rename;
      std::snprintf(res.name, sizeof res.name, "%s", st->rename.buf);
      st->rename.cancel();
    } else if (enter || ImGui::IsKeyPressed(ImGuiKey_Escape) || (left && !enter)) {
      st->rename.cancel(); // Esc ya da odagi birakma: vazgec
    }
    ImGui::PopID();
    return res;
  }

  // Satirlar sik: dikey bosluk 2px; Selectable zemini boslugun yarisini kaplar.
  ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(s.ItemSpacing.x, 2.0f));
  ImGui::SetNextItemAllowOverlap(); // ok / gorunurluk / kilit satirin UZERINDE
  ImGui::SetNextItemSelectionUserData(id);
  const bool clicked = ImGui::Selectable("##satir", r.selected, ImGuiSelectableFlags_AllowDoubleClick, ImVec2(0, h));
  ImGui::PopStyleVar();
  
  // Varlık Bağlam Menüsü (Sağ Tık)
  if (ImGui::BeginPopupContextItem()) {
    // NOT: "Prefab Olarak Kaydet" buradan KALDIRILDI. Govdesi bostu -- tiklanan
    // ama hicbir sey yapmayan bir menu ogesi, eksik ozellikten DAHA kotudur:
    // kullaniciya var olmayan bir yetenek soyler. Prefab'in arkasi (alt agaci
    // dosyaya yazma + tarayicida listeleme + ornekleme) yazildiginda geri gelir.
    if (ImGui::MenuItem("Yeniden Adlandır")) {
      if (st) { st->rename.index = id; st->rename.focus = true; std::snprintf(st->rename.buf, sizeof st->rename.buf, "%s", r.name ? r.name : ); }
    }
    ImGui::EndPopup();
  }

  const WidgetRect rr = item_rect();
  g_row_layout.row = rr;
  // Uzerinde olma KOSULU satirin DIKDORTGENI, ogenin kendisi degil: ok/goz/kilit
  // dugmeleri satirin ustunde durur ve isaretci onlarin uzerindeyken Selectable'in
  // IsItemHovered'i FALSE doner. Oge durumuna baglansaydi, fare kilide yaklasinca
  // simgeler kaybolur, kaybolunca hover satira doner, simgeler geri gelir —
  // titreyen ve TIKLANAMAYAN bir dugme (olculdu: goz kapisinin goruntusunde
  // kilit hic cizilmemisti).
  const bool hovered = ImGui::IsItemHovered() ||
                       (ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows) &&
                        ImGui::IsMouseHoveringRect(ImVec2(rr.x0, rr.y0), ImVec2(rr.x1, rr.y1)));
  g_row_layout.arrow = g_row_layout.eye = g_row_layout.lock = WidgetRect{};
  const bool dbl = clicked && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);
  if (clicked && !dbl) {
    res.action = HierarchyAction::Select;
    res.ctrl = ImGui::GetIO().KeyCtrl;
  }
  if (st) {
    // Surukle-birak. Yuk = varlik indeksi. Satirin UZERINE birakmak = cocuk yap;
    // listenin altindaki bos alan (hierarchy_root_drop_zone) = koke tasi.
    // Satirlar ARASINA birakma YOK: varlik sirasi = dizi indeksi, araya birakmak
    // butun indeksleri kaydirirdi (gunluk/blob/secim hepsi etkilenir) — bilincli
    // olarak ertelendi, yerine baglam menusunde "Ebeveynden ayir" var.
    if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceNoHoldToOpenOthers)) {
      st->drag_source = id;
      ImGui::SetDragDropPayload("TULPAR_VARLIK", &id, sizeof id);
      ImGui::TextUnformatted(r.name ? r.name : );
      ImGui::EndDragDropSource();
    }
    if (ImGui::BeginDragDropTarget()) {
      if (const ImGuiPayload *pl = ImGui::AcceptDragDropPayload("TULPAR_VARLIK")) {
        int src = -1;
        if (pl->DataSize == (int)sizeof src) std::memcpy(&src, pl->Data, sizeof src);
        if (src >= 0 && src != id) {
          res.action = HierarchyAction::Reparent;
          res.index = src;
          res.target = id;
        }
        st->drag_source = -1;
      }
      ImGui::EndDragDropTarget();
    }
    // Baglam menusu (sag tik). Eylemi DONDURUR, sahneye dokunmaz.
    if (ImGui::BeginPopupContextItem("baglam")) {
      if (ImGui::MenuItem("Yeniden adland\xC4\xB1r", "F2")) {
        st->rename.begin(id, r.name);
        ImGui::CloseCurrentPopup();
      }
      if (ImGui::MenuItem("\xC3\x87o\xC4\x9F" "alt")) res.action = HierarchyAction::Duplicate;
      if (ImGui::MenuItem("Ebeveynden ay\xC4\xB1r")) res.action = HierarchyAction::Detach;
      ImGui::Separator();
      if (ImGui::MenuItem("Sil", "Del")) res.action = HierarchyAction::Delete;
      ImGui::Separator();
      if (ImGui::MenuItem("Kes", "Ctrl+X")) res.action = HierarchyAction::Cut;
      if (ImGui::MenuItem("Kopyala", "Ctrl+C")) res.action = HierarchyAction::Copy;
      if (ImGui::MenuItem("Yap\xC4\xB1\xC5\x9Ft\xC4\xB1r", "Ctrl+V")) res.action = HierarchyAction::Paste;
      ImGui::EndPopup();
    }
    if (dbl) st->rename.begin(id, r.name); // cift tik: yerinde ad
  }

  ImDrawList *dl = ImGui::GetWindowDrawList();
  const float ty = std::floor(rr.y0 + (rr.y1 - rr.y0 - fs) * 0.5f);
  float x = rr.x0 + s.FramePadding.x + indent;

  // --- acilir ok: yalniz cocugu olanda; sutun genisligi HER ZAMAN ayrilir ki
  //     kardeslerin adlari hizali kalsin.
  if (r.has_children) {
    const char *arrow = r.expanded ? "\xE2\x96\xBE" : "\xE2\x96\xB8"; // ▾ / ▸
    const ImVec2 asz = ImGui::CalcTextSize(arrow);
    dl->AddText(ImVec2(std::floor(x + (arrow_col - asz.x) * 0.5f), ty), tone_u32(Tone::TextDim), arrow);
    if (st) {
      ImGui::SetCursorScreenPos(ImVec2(x, rr.y0));
      if (ImGui::InvisibleButton("##ok", ImVec2(arrow_col, rr.y1 - rr.y0))) {
        res.action = HierarchyAction::Toggle;
        res.index = id;
      }
      g_row_layout.arrow = item_rect();
    }
  }
  x += arrow_col;

  // Tur simgesi: sabit genislikli sutun (adlar hizali kalsin).
  const char *icon;
  Tone it;
  if (r.has_light) { icon = "\xE2\x98\x80"; it = Tone::Warn; }       // ☀
  else if (r.has_model) { icon = "\xE2\x97\x86"; it = Tone::Text; }  // ◆
  else if (r.has_body) { icon = "\xE2\x97\xBC"; it = Tone::AxisZ; }  // ◼
  else { icon = "\xE2\x97\x8B"; it = Tone::TextDim; }                // ○
  const float icon_col = std::floor(fs * 1.05f);
  const ImVec2 isz = ImGui::CalcTextSize(icon);
  dl->AddText(ImVec2(std::floor(x + (icon_col - isz.x) * 0.5f), ty), tone_u32(it, r.hidden ? 0.45f : 1.0f), icon);
  x += icon_col + s.ItemInnerSpacing.x;

  float right = rr.x1 - s.FramePadding.x;
  // --- gorunurluk + kilit (en sagda). DejaVuSans'ta goz/asma kilit glifi YOK
  //     (olculdu: U+1F441 ve U+1F512 fontta degil), bu yuzden ⊙ gorunur /
  //     ◌ gizli, ⊘ kilitli kullaniliyor. Kilit yalniz KILITLIYKEN ya da fare
  //     satirdayken cizilir: sakin listede gurultu yapmasin.
  if (st) {
    const float bw = std::floor(fs * 1.1f);
    // Iki sutun HER satirda AYRILIR, cizim kosullu olsa da. Yerlesim de kosullu
    // olsaydi fare satira girdiginde sutunlar acilir, ad daralir ve imlecin
    // altindaki dugme KAYARDI (olculdu: goz kapisinin sentetik tiklamasi
    // kilidin uzerine dusuyordu). Yerlesim sabit, gorunurluk degisken.
    ImGui::SetCursorScreenPos(ImVec2(right - bw, rr.y0));
    if (ImGui::InvisibleButton("##kilit", ImVec2(bw, rr.y1 - rr.y0))) { res.action = HierarchyAction::Lock; res.index = id; }
    g_row_layout.lock = item_rect();
    if (r.locked || hovered) {
      const char *g = "\xE2\x8A\x98"; // ⊘
      const ImVec2 gs2 = ImGui::CalcTextSize(g);
      dl->AddText(ImVec2(std::floor(right - bw + (bw - gs2.x) * 0.5f), ty), tone_u32(r.locked ? Tone::Warn : Tone::TextDim, r.locked ? 1.0f : 0.5f), g);
    }
    right -= bw;
    ImGui::SetCursorScreenPos(ImVec2(right - bw, rr.y0));
    if (ImGui::InvisibleButton("##goz", ImVec2(bw, rr.y1 - rr.y0))) { res.action = HierarchyAction::Visibility; res.index = id; }
    g_row_layout.eye = item_rect();
    if (r.hidden || hovered) {
      const char *g = r.hidden ? "\xE2\x97\x8C" : "\xE2\x8A\x99"; // ◌ gizli / ⊙ gorunur
      const ImVec2 gs2 = ImGui::CalcTextSize(g);
      dl->AddText(ImVec2(std::floor(right - bw + (bw - gs2.x) * 0.5f), ty), tone_u32(Tone::TextDim, r.hidden ? 1.0f : 0.5f), g);
    }
    right -= bw + s.ItemInnerSpacing.x;
  }

  // Sag: bilesen glifleri, kucuk ve soluk (◆ model, ☀ isik, ◼ govde, ↻ animasyon).
  char glyphs[32] = {0};
  if (r.has_model) std::strcat(glyphs, "\xE2\x97\x86 ");
  if (r.has_light) std::strcat(glyphs, "\xE2\x98\x80 ");
  if (r.has_body) std::strcat(glyphs, "\xE2\x97\xBC ");
  if (r.has_anim) std::strcat(glyphs, "\xE2\x86\xBB ");
  if (glyphs[0]) {
    glyphs[std::strlen(glyphs) - 1] = 0; // son bosluk
    ImGui::PushFont(nullptr, s.FontSizeBase * 0.78f);
    const ImVec2 gs = ImGui::CalcTextSize(glyphs);
    const float gy = std::floor(rr.y0 + (rr.y1 - rr.y0 - gs.y) * 0.5f);
    dl->AddText(ImGui::GetFont(), ImGui::GetFontSize(), ImVec2(right - gs.x, gy), tone_u32(Tone::TextDim, 0.85f), glyphs);
    ImGui::PopFont();
    right -= gs.x + s.ItemInnerSpacing.x;
  }

  // Ad: kalan genislige kirp; kirpildiysa tam ad ipucunda. Gizli varlik soluk.
  g_row_layout.text_max_w = right - x;
  g_row_layout.text_x = x;
  editor_ellipsize(r.name ? r.name : , g_row_layout.text_max_w, g_row_layout.text, sizeof g_row_layout.text);
  g_row_layout.ellipsized = r.name && std::strcmp(g_row_layout.text, r.name) != 0;
  dl->AddText(ImVec2(x, ty), tone_u32(r.hidden ? Tone::TextDim : Tone::Text), g_row_layout.text);
  if (g_row_layout.ellipsized && hovered && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) ImGui::SetTooltip("%s", r.name);
  ImGui::PopID();
  return res;
}
} // namespace

bool hierarchy_row(int id, const HierarchyRow &r) {
  const HierarchyResult res = hierarchy_row_impl(id, r, nullptr);
  return res.action == HierarchyAction::Select;
}
HierarchyResult hierarchy_tree_row(int id, const HierarchyRow &r, HierarchyState *st) { return hierarchy_row_impl(id, r, st); }

HierarchyResult hierarchy_root_drop_zone(HierarchyState *st) {
  HierarchyResult res;
  if (!st) return res;
  const float avail = ImGui::GetContentRegionAvail().y;
  if (avail < 4.0f) return res; // yer yok: hicbir sey cizme (gorunmez oge de yok)
  ImGui::InvisibleButton("##kok_birakma", ImVec2(-FLT_MIN, avail));
  if (ImGui::BeginDragDropTarget()) {
    // Cerceve: birakilabilir alan GORUNSUN (sessiz hedef kullanilamaz).
    const WidgetRect z = item_rect();
    ImGui::GetWindowDrawList()->AddRect(ImVec2(z.x0, z.y0), ImVec2(z.x1, z.y1), tone_u32(Tone::Accent, 0.6f), ImGui::GetStyle().FrameRounding);
    if (const ImGuiPayload *pl = ImGui::AcceptDragDropPayload("TULPAR_VARLIK")) {
      int src = -1;
      if (pl->DataSize == (int)sizeof src) std::memcpy(&src, pl->Data, sizeof src);
      if (src >= 0) { res.action = HierarchyAction::Detach; res.index = src; }
      st->drag_source = -1;
    }
    ImGui::EndDragDropTarget();
  }
  return res;
}

int hierarchy_toolbar(uint32_t entity_count, bool has_selection) {
  int result = 0;
  const ImGuiStyle &s = ImGui::GetStyle();
  ImGui::PushID("arac");
  const float h = ImGui::GetFrameHeight();
  if (ImGui::Button("+", ImVec2(h, h))) ImGui::OpenPopup("ekle");
  if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) ImGui::SetTooltip("Varlık ekle");
  const WidgetRect b = item_rect();
  ImGui::SetNextWindowPos(ImVec2(b.x0, b.y1 + s.ItemInnerSpacing.y * 0.5f));
  if (ImGui::BeginPopup("ekle")) {
    result = create_menu_draw(kCreateMenu, kCreateMenuCount);
    ImGui::EndPopup();
  }
  ImGui::SameLine(0, std::floor(s.ItemInnerSpacing.x * 0.5f));
  ImGui::BeginDisabled(!has_selection);
  if (ImGui::Button("\xE2\x88\x92", ImVec2(h, h))) result = 100; // − sil
  ImGui::EndDisabled();
  if (has_selection && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) ImGui::SetTooltip("Seçili varlığı sil (Del)");
  // Sag: sayi, soluk.
  char cnt[40];
  std::snprintf(cnt, sizeof cnt, "%u varlık", entity_count);
  ImGui::SameLine();
  const float tw = ImGui::CalcTextSize(cnt).x;
  const float right = ImGui::GetCursorScreenPos().x + ImGui::GetContentRegionAvail().x;
  if (right - tw > ImGui::GetCursorScreenPos().x) {
    ImGui::SetCursorScreenPos(ImVec2(right - tw, ImGui::GetCursorScreenPos().y));
    ImGui::AlignTextToFramePadding();
    ImGui::PushStyleColor(ImGuiCol_Text, tone(Tone::TextDim));
    ImGui::TextUnformatted(cnt);
    ImGui::PopStyleColor();
  } else {
    ImGui::NewLine();
  }
  ImGui::PopID();
  return result;
}

void hierarchy_empty(const char *hint) { centered_hint(hint); }

// =============================================================================
// Son yerlesim (kapilar)
// =============================================================================

const PropVec3Layout &prop_vec3_last_layout() { return g_vec3_layout; }
const ComponentHeaderLayout &component_header_last_layout() { return g_hdr_layout; }
const HierarchyRowLayout &hierarchy_row_last_layout() { return g_row_layout; }
const WidgetRect &prop_last_rect() { return g_last_prop; }

} // namespace tulpar::engine::app
