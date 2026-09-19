#include "app/editor_ui.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <dirent.h>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <IconsMaterialDesign.h>
#include <imgui.h>
#include <imnodes.h>
#include <imgui_impl_vulkan.h>
#include <ImGuizmo.h>

namespace tulpar::engine::app {

namespace {
struct LoaderCtx {
  rhi::VkApi *api;
  VkInstance instance;
};
// ImGui'nin tablosu WSI fonksiyonlarini da ister (vkCreateSwapchainKHR...); yuzey
// uzantisi olmayan instance'ta (headless, testler) bunlar null gelir ve yukleme
// basarisiz olur. Yalniz ImGui_ImplVulkanH_* yardimcilari kullanir; biz kullanmiyoruz.
// Null yerine SESLI cokme stub'u verilir: yanlislikla cagrilirsa adiyla durur.
void missing_vk_function() { std::fprintf(stderr, "[editor-ui] bu instance'ta olmayan Vulkan fonksiyonu cagrildi (WSI?)\n"); std::abort(); }
PFN_vkVoidFunction imgui_loader(const char *name, void *user) {
  LoaderCtx *c = static_cast<LoaderCtx *>(user);
  PFN_vkVoidFunction f = c->api->vkGetInstanceProcAddr(c->instance, name);
  if (!f) f = (PFN_vkVoidFunction)missing_vk_function;
  return f;
}
void check_vk(VkResult r) {
  if (r != VK_SUCCESS) std::fprintf(stderr, "[editor-ui] vulkan hatasi %d\n", (int)r);
}
// GLFW tus kodu -> ImGuiKey. Metin duzenlemenin (Insert/PageUp/keypad) ve
// kisayol yakalamanin (F1..F12, noktalama) ihtiyaci olan HER tus burada olmali:
// eslenmeyen tus ImGui icin HIC BASILMAMIS gibidir, tus kutusunda sessizce
// calismaz. Not: kod noktasi metni AddInputCharacter'dan ayri gelir.
ImGuiKey map_key(int k) {
  if (k >= GLFW_KEY_A && k <= GLFW_KEY_Z) return (ImGuiKey)(ImGuiKey_A + (k - GLFW_KEY_A));
  if (k >= GLFW_KEY_0 && k <= GLFW_KEY_9) return (ImGuiKey)(ImGuiKey_0 + (k - GLFW_KEY_0));
  if (k >= GLFW_KEY_F1 && k <= GLFW_KEY_F12) return (ImGuiKey)(ImGuiKey_F1 + (k - GLFW_KEY_F1));
  if (k >= GLFW_KEY_KP_0 && k <= GLFW_KEY_KP_9) return (ImGuiKey)(ImGuiKey_Keypad0 + (k - GLFW_KEY_KP_0));
  switch (k) {
  case GLFW_KEY_TAB: return ImGuiKey_Tab;
  case GLFW_KEY_LEFT: return ImGuiKey_LeftArrow;
  case GLFW_KEY_RIGHT: return ImGuiKey_RightArrow;
  case GLFW_KEY_UP: return ImGuiKey_UpArrow;
  case GLFW_KEY_DOWN: return ImGuiKey_DownArrow;
  case GLFW_KEY_PAGE_UP: return ImGuiKey_PageUp;
  case GLFW_KEY_PAGE_DOWN: return ImGuiKey_PageDown;
  case GLFW_KEY_HOME: return ImGuiKey_Home;
  case GLFW_KEY_END: return ImGuiKey_End;
  case GLFW_KEY_INSERT: return ImGuiKey_Insert;
  case GLFW_KEY_DELETE: return ImGuiKey_Delete;
  case GLFW_KEY_BACKSPACE: return ImGuiKey_Backspace;
  case GLFW_KEY_SPACE: return ImGuiKey_Space;
  case GLFW_KEY_ENTER: return ImGuiKey_Enter;
  case GLFW_KEY_ESCAPE: return ImGuiKey_Escape;
  case GLFW_KEY_APOSTROPHE: return ImGuiKey_Apostrophe;
  case GLFW_KEY_COMMA: return ImGuiKey_Comma;
  case GLFW_KEY_MINUS: return ImGuiKey_Minus;
  case GLFW_KEY_PERIOD: return ImGuiKey_Period;
  case GLFW_KEY_SLASH: return ImGuiKey_Slash;
  case GLFW_KEY_SEMICOLON: return ImGuiKey_Semicolon;
  case GLFW_KEY_EQUAL: return ImGuiKey_Equal;
  case GLFW_KEY_LEFT_BRACKET: return ImGuiKey_LeftBracket;
  case GLFW_KEY_BACKSLASH: return ImGuiKey_Backslash;
  case GLFW_KEY_RIGHT_BRACKET: return ImGuiKey_RightBracket;
  case GLFW_KEY_GRAVE_ACCENT: return ImGuiKey_GraveAccent;
  case GLFW_KEY_CAPS_LOCK: return ImGuiKey_CapsLock;
  case GLFW_KEY_SCROLL_LOCK: return ImGuiKey_ScrollLock;
  case GLFW_KEY_NUM_LOCK: return ImGuiKey_NumLock;
  case GLFW_KEY_PRINT_SCREEN: return ImGuiKey_PrintScreen;
  case GLFW_KEY_PAUSE: return ImGuiKey_Pause;
  case GLFW_KEY_KP_DECIMAL: return ImGuiKey_KeypadDecimal;
  case GLFW_KEY_KP_DIVIDE: return ImGuiKey_KeypadDivide;
  case GLFW_KEY_KP_MULTIPLY: return ImGuiKey_KeypadMultiply;
  case GLFW_KEY_KP_SUBTRACT: return ImGuiKey_KeypadSubtract;
  case GLFW_KEY_KP_ADD: return ImGuiKey_KeypadAdd;
  case GLFW_KEY_KP_ENTER: return ImGuiKey_KeypadEnter;
  case GLFW_KEY_KP_EQUAL: return ImGuiKey_KeypadEqual;
  case GLFW_KEY_LEFT_CONTROL: return ImGuiKey_LeftCtrl;
  case GLFW_KEY_RIGHT_CONTROL: return ImGuiKey_RightCtrl;
  case GLFW_KEY_LEFT_SHIFT: return ImGuiKey_LeftShift;
  case GLFW_KEY_RIGHT_SHIFT: return ImGuiKey_RightShift;
  case GLFW_KEY_LEFT_ALT: return ImGuiKey_LeftAlt;
  case GLFW_KEY_RIGHT_ALT: return ImGuiKey_RightAlt;
  case GLFW_KEY_LEFT_SUPER: return ImGuiKey_LeftSuper;
  case GLFW_KEY_RIGHT_SUPER: return ImGuiKey_RightSuper;
  case GLFW_KEY_MENU: return ImGuiKey_Menu;
  default: return ImGuiKey_None;
  }
}

// ===========================================================================
// TULPAR KOYU — editor paleti
// ===========================================================================
// Isimli sabitler; ImGuiCol_* girdilerinin TAMAMI bunlardan turer, tabloda
// baska ham renk yok. Tam sozlesme: docs/engine/EDITOR-TASARIM.md
//
// KIMLIK: notrler SICAK komur (R > G > B, bir-iki kademe). UE5, Unity ve
// Godot'un UCU DE soguk mavi-gri + mavi vurguda duruyor; oraya benzemek hem
// kopya riskine yaklasmak hem kendi kimligini silmek olurdu. Sicak notr +
// turkuaz vurgu bu ucuyle karismaz ve 3B goruntudeki soguk gokyuzu / sicak
// gunes tonlariyla da carpismaz.
//
// YUZEY ALTI KADEME, her biri oncekinden ~%35 acik:
//   void < sunken(girdi) < panel < raised(baslik) < control(dugme) < control_hi
// Kural tek cumle: GIRDI COKER, DUGME KABARIR, panel arada durur.
constexpr ImVec4 rgb(int r, int g, int b, float a = 1.0f) {
  return ImVec4((float)r / 255.0f, (float)g / 255.0f, (float)b / 255.0f, a);
}
constexpr ImVec4 fade(ImVec4 c, float a) { return ImVec4(c.x, c.y, c.z, a); }

constexpr ImVec4 kBg0 = rgb(0x0A, 0x09, 0x08);      // void: dok boslugu, panel arasi, guclu ayirac
constexpr ImVec4 kInput = rgb(0x10, 0x0F, 0x0E);    // sunken: girdi kuyusu, liste zemini, arama
constexpr ImVec4 kBg1 = rgb(0x1A, 0x19, 0x18);      // panel: ana zemin
constexpr ImVec4 kBg2 = rgb(0x23, 0x22, 0x20);      // raised: baslik bandi, sekme seridi, tablo basligi
constexpr ImVec4 kBg3 = rgb(0x30, 0x2E, 0x2B);      // control: dugme, birlesik denetim
constexpr ImVec4 kBg4 = rgb(0x3C, 0x39, 0x36);      // control_hi: uzerine gelince
constexpr ImVec4 kLine = rgb(0x2A, 0x28, 0x26);     // ince ayirac (tablo izgarasi, pencere kenari)
constexpr ImVec4 kText = rgb(0xD8, 0xD5, 0xD0);     // ana metin (hafif SICAK beyaz)
constexpr ImVec4 kTextDim = rgb(0x8C, 0x88, 0x82);  // etiket, ikincil bilgi
constexpr ImVec4 kTextMute = rgb(0x5E, 0x5B, 0x57); // pasif, ipucu, yer tutucu
// Vurgu: Tulpar turkuazi, DOYGUNLUGU DUSURULMUS. Bir karede ekranin
// %5'inden azini kaplamali (bkz. docs/engine/EDITOR-TASARIM.md §3).
constexpr ImVec4 kAccent = rgb(0x2F, 0x9B, 0x8F);
constexpr ImVec4 kAccentHi = rgb(0x4F, 0xC4, 0xB6); // imlec, tik, baglanti
constexpr ImVec4 kAccentLo = rgb(0x1E, 0x64, 0x5C); // basili tutamak
constexpr ImVec4 kSelect = rgb(0x24, 0x40, 0x3D);   // liste/agac secim seridi -- DOYGUN DEGIL
constexpr ImVec4 kWarn = rgb(0xD9, 0xA4, 0x41);     // kaydedilmemis, surukle hedefi
constexpr ImVec4 kWhite = rgb(0xFF, 0xFF, 0xFF);    // yalniz saydam katmanlar icin
// Eksen: doygunluk BILEREK dusuk -- eksen rengi bir KENAR ISARETI, yuzey degil.
constexpr ImVec4 kAxisX = rgb(0xC0, 0x53, 0x4C);
constexpr ImVec4 kAxisY = rgb(0x6B, 0x9E, 0x45);
constexpr ImVec4 kAxisZ = rgb(0x4E, 0x79, 0xBE);
constexpr ImVec4 kOk = rgb(0x5F, 0xA8, 0x5E);
constexpr ImVec4 kErr = rgb(0xD2, 0x54, 0x4B);
bool g_theme_srgb = false; // son editor_apply_theme'in srgb_target'i (editor_tone bunu izler)

// sRGB (8-bit yazarken kastettigimiz deger) -> dogrusal. Alfa CEVRILMEZ.
float srgb_to_linear_ch(float c) {
  return c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f);
}

// Olculer: OLCEKSIZ taban. ScaleAllSizes bunlari scale ile carpar.
void theme_sizes(ImGuiStyle &s) {
  // 4 piksellik izgara. Onceki degerler (8x4 / 8x5) profesyonel araclara gore
  // GEVSEKTI; sikilastirma ekrana ~%15 daha fazla bilgi sigdirir.
  s.WindowPadding = ImVec2(8, 6);
  s.FramePadding = ImVec2(6, 3);
  s.ItemSpacing = ImVec2(6, 3);
  s.ItemInnerSpacing = ImVec2(4, 2);
  s.CellPadding = ImVec2(6, 3); // FramePadding ile ayni: tablo hucresi ve kutu ayni ritimde
  s.TouchExtraPadding = ImVec2(0, 0);
  s.IndentSpacing = 14.0f;
  s.ScrollbarSize = 12.0f;
  s.GrabMinSize = 10.0f;
  s.WindowMinSize = ImVec2(180, 72);
  s.WindowBorderSize = 1.0f;
  s.ChildBorderSize = 1.0f;
  s.PopupBorderSize = 1.0f;
  // Cerceve cizgisi YOK: ayrimi artik YUZEY TONU yapiyor (cokuk girdi /
  // kabarik dugme). Ikisi birden olunca her kutunun etrafinda ikinci bir
  // kontur olusuyor ve arayuz "cizgili" gorunuyordu.
  s.FrameBorderSize = 0.0f;
  s.TabBorderSize = 0.0f;
  s.TabBarBorderSize = 1.0f;   // cubuk alt cizgisi: rengi TabSelected (= pencere zemini), yani sekme icerige AKAR
  s.TabBarOverlineSize = 2.0f; // secili sekmenin uzerinde vurgu cizgisi
  s.DockingSeparatorSize = 2.0f;
  s.SeparatorTextBorderSize = 1.0f; // kalin cizgi baslik degil bolme gibi okunuyordu
  s.SeparatorTextPadding = ImVec2(16, 6);
  // TEK yuvarlaklik (4): pencere, cocuk, acilir, kutu, tutamak, sekme — farkli
  // yaricaplar yan yana gelince cerceve "duz/tutarsiz" okunuyordu.
  // Sektor editorleri neredeyse KARE koseler kullanir (UE5 ~2px). Genis
  // yaricap arayuzu "uygulama" degil "widget seti" gosterir.
  s.WindowRounding = 0.0f; // doklanmis panel: kare
  s.ChildRounding = 2.0f;
  s.PopupRounding = 3.0f;
  s.FrameRounding = 2.0f;
  s.GrabRounding = 2.0f;
  s.TabRounding = 2.0f;
  s.ScrollbarRounding = 2.0f;
  s.WindowTitleAlign = ImVec2(0.0f, 0.5f);          // sola dayali baslik (Unity/Godot)
  s.WindowMenuButtonPosition = ImGuiDir_None;       // daraltma oku yok: baslik temiz
  s.ColorButtonPosition = ImGuiDir_Right;
  s.ButtonTextAlign = ImVec2(0.5f, 0.5f);
  s.SelectableTextAlign = ImVec2(0.0f, 0.5f);
  s.SeparatorTextAlign = ImVec2(0.0f, 0.5f);
  s.AntiAliasedLines = true;
  s.AntiAliasedLinesUseTex = true;
  s.AntiAliasedFill = true;
  s.CircleTessellationMaxError = 0.20f;
}

// Renkler: palet -> ImGuiCol_*. Alfa'li girdilerde ZEMIN degil KATMAN kullanilir
// (fade), boylece pencere uzerinde de dok uzerinde de ayni okunur.
void theme_colors(ImGuiStyle &s) {
  ImVec4 *c = s.Colors;
  c[ImGuiCol_Text] = kText;
  c[ImGuiCol_TextDisabled] = kTextMute;
  c[ImGuiCol_WindowBg] = kBg1;
  c[ImGuiCol_ChildBg] = fade(kBg0, 0.0f); // saydam: cocuk alan pencereyi bozmasin
  c[ImGuiCol_PopupBg] = fade(kBg2, 0.98f);
  c[ImGuiCol_Border] = kLine;
  c[ImGuiCol_BorderShadow] = fade(kBg0, 0.0f);
  // Girdi alani COKUK (panelden koyu), dugme KABARIK (panelden acik).
  c[ImGuiCol_FrameBg] = kInput;
  c[ImGuiCol_FrameBgHovered] = rgb(0x17, 0x16, 0x15);
  c[ImGuiCol_FrameBgActive] = kBg0; // yazarken daha da coker
  c[ImGuiCol_InputTextCursor] = kAccentHi;
  // Baslik / dock sekme SERIDI: odakli ya da degil, hep en dip ton. Odak
  // sekmenin ustundeki cizgiyle (overline) gosterilir; serit rengi degisince
  // (eski: aktif Bg2) odakli panelin sekme cubugu icerikten daha acik kaliyor ve
  // pasif sekme seridin icinde kayboluyordu (Unity/Unreal: serit hep koyu).
  c[ImGuiCol_TitleBg] = kBg0;
  c[ImGuiCol_TitleBgActive] = kBg0;
  c[ImGuiCol_TitleBgCollapsed] = fade(kBg0, 0.75f);
  c[ImGuiCol_MenuBarBg] = kBg0;
  c[ImGuiCol_ScrollbarBg] = fade(kBg0, 0.55f);
  c[ImGuiCol_ScrollbarGrab] = kBg3;
  c[ImGuiCol_ScrollbarGrabHovered] = kBg4;
  c[ImGuiCol_ScrollbarGrabActive] = rgb(0x4A, 0x47, 0x43); // vurgu DEGIL: notr kalsin
  c[ImGuiCol_CheckMark] = kAccentHi;
  c[ImGuiCol_CheckboxSelectedBg] = fade(kAccent, 0.30f);
  c[ImGuiCol_SliderGrab] = rgb(0x56, 0x52, 0x4E);
  c[ImGuiCol_SliderGrabActive] = kAccent;
  c[ImGuiCol_Button] = kBg3;
  c[ImGuiCol_ButtonHovered] = kBg4;
  c[ImGuiCol_ButtonActive] = rgb(0x28, 0x28, 0x28); // basilinca ICERI goker
  // Liste/agac secimi: UE5'te secim DOYGUN DEGIL -- sakin bir mavi-gri seritle
  // gosterilir, yazi okunur kalir. Doygun dolgu satiri "secili" degil
  // "uyarilmis" gosterir ve uzun listede goz yorar.
  c[ImGuiCol_Header] = kSelect;
  c[ImGuiCol_HeaderHovered] = kBg2; // uzerine gelme: yuzey BIR kademe yukari
  c[ImGuiCol_HeaderActive] = kAccentLo;
  // Panel ayiraci UE5'te ACIK degil KOYU bir cizgidir: paneller birbirinden
  // "isikla" degil "bosluk"la ayrilir.
  c[ImGuiCol_Separator] = kBg0;
  c[ImGuiCol_SeparatorHovered] = kAccentLo;
  c[ImGuiCol_SeparatorActive] = kAccent;
  c[ImGuiCol_ResizeGrip] = fade(kLine, 0.55f);
  c[ImGuiCol_ResizeGripHovered] = kAccentLo;
  c[ImGuiCol_ResizeGripActive] = kAccent;
  // Sekmeler: pasif sekme seritle AYNI (yalniz yazi), secili sekme pencere
  // zeminiyle BIRLESIR (odakli/odaksiz fark etmez — Unity'de de panel odagini
  // sekmenin rengi degil ustundeki cizgi soyler), ustunde bir ton acik.
  c[ImGuiCol_Tab] = kBg0;
  c[ImGuiCol_TabHovered] = kBg2;
  c[ImGuiCol_TabSelected] = kBg1;
  c[ImGuiCol_TabSelectedOverline] = kAccent;
  c[ImGuiCol_TabDimmed] = kBg0;
  c[ImGuiCol_TabDimmedSelected] = kBg1;
  c[ImGuiCol_TabDimmedSelectedOverline] = fade(kTextDim, 0.45f);
  c[ImGuiCol_DockingPreview] = fade(kAccent, 0.35f);
  c[ImGuiCol_DockingEmptyBg] = kBg0;
  c[ImGuiCol_PlotLines] = kTextDim;
  c[ImGuiCol_PlotLinesHovered] = kAccentHi;
  c[ImGuiCol_PlotHistogram] = kAccent;
  c[ImGuiCol_PlotHistogramHovered] = kAccentHi;
  c[ImGuiCol_TableHeaderBg] = kBg2;
  c[ImGuiCol_TableBorderStrong] = kLine;
  c[ImGuiCol_TableBorderLight] = fade(kLine, 0.5f);
  c[ImGuiCol_TableRowBg] = fade(kBg0, 0.0f);
  c[ImGuiCol_TableRowBgAlt] = fade(kWhite, 0.025f);
  c[ImGuiCol_TextLink] = kAccentHi;
  c[ImGuiCol_TextSelectedBg] = fade(kAccent, 0.35f);
  c[ImGuiCol_TreeLines] = fade(kLine, 0.7f);
  c[ImGuiCol_DragDropTarget] = kWarn;
  c[ImGuiCol_DragDropTargetBg] = fade(kWarn, 0.15f);
  c[ImGuiCol_UnsavedMarker] = kWarn;
  c[ImGuiCol_NavCursor] = kAccentHi;
  c[ImGuiCol_NavWindowingHighlight] = fade(kWhite, 0.70f);
  c[ImGuiCol_NavWindowingDimBg] = fade(kBg0, 0.55f);
  c[ImGuiCol_ModalWindowDimBg] = fade(kBg0, 0.70f);
}
} // namespace

void editor_apply_theme(float scale, bool srgb_target) {
  if (!ImGui::GetCurrentContext()) return;
  if (!(scale > 0.0f)) scale = 1.0f; // NaN de buraya duser
  if (scale < 0.5f) scale = 0.5f;
  if (scale > 4.0f) scale = 4.0f;
  // SIFIRDAN kur: ImGuiStyle() olculeri tabana dondurur (_MainScale dahil), yoksa
  // ust uste ScaleAllSizes cagrilari olcegi BIRIKTIRIR (olculer trunc'la buyur).
  ImGuiStyle &s = ImGui::GetStyle();
  s = ImGuiStyle();
  theme_sizes(s);
  theme_colors(s);
  s.ScaleAllSizes(scale); // yazi DISINDAKI her sey (bosluk, yuvarlaklik, kalinlik)
  g_theme_srgb = srgb_target;
  if (srgb_target)
    for (int i = 0; i < ImGuiCol_COUNT; i++) {
      s.Colors[i].x = srgb_to_linear_ch(s.Colors[i].x);
      s.Colors[i].y = srgb_to_linear_ch(s.Colors[i].y);
      s.Colors[i].z = srgb_to_linear_ch(s.Colors[i].z);
    }
}


// --- Tipografi olcegi --------------------------------------------------------
// ImGui 1.92: PushFont(NULL, px) yalniz BOYUTU degistirir, yuzu korur.
// Carpanlar tabandan (FontSizeBase) turer ki DPI olcegi kendiliginden gecsin.
void push_text_size(TextSize t) {
  if (!ImGui::GetCurrentContext()) return;
  const float base = ImGui::GetStyle().FontSizeBase;
  if (!(base > 0.0f)) { ImGui::PushFont(nullptr, 0.0f); return; } // 0 = degisme
  float f = 1.0f;
  if (t == TextSize::Sm) f = 11.0f / 13.0f;
  else if (t == TextSize::Lg) f = 15.0f / 13.0f;
  ImGui::PushFont(nullptr, base * f);
}
void pop_text_size() {
  if (!ImGui::GetCurrentContext()) return;
  ImGui::PopFont();
}

void editor_tone(Tone t, float out[4]) {
  // SIRA, Tone enum'uyla BIREBIR aynidir; biri degisirse digeri de degismeli.
  static constexpr ImVec4 kTable[(int)Tone::Count] = {
      kBg0,   kBg1,      kBg2,      kBg3,  kBg4,   kLine,  kText,  kTextDim, kAccent,
      kAccentHi, kAccentLo, kWarn,  kWhite, kAxisX, kAxisY, kAxisZ, kOk,      kErr, kInput,
      kTextMute, kSelect};
  static_assert((int)Tone::Count == 21, "Tone enum ile kTable AYRISTI");
  const ImVec4 c = (int)t < (int)Tone::Count ? kTable[(int)t] : kText; // enum tabani uint8_t: negatif olamaz
  out[0] = g_theme_srgb ? srgb_to_linear_ch(c.x) : c.x;
  out[1] = g_theme_srgb ? srgb_to_linear_ch(c.y) : c.y;
  out[2] = g_theme_srgb ? srgb_to_linear_ch(c.z) : c.z;
  out[3] = c.w;
}

uint32_t editor_ellipsize(const char *s, float max_w, char *out, uint32_t cap) {
  if (!out || cap == 0) return 0;
  out[0] = 0;
  if (!s) return 0;
  const uint32_t n = (uint32_t)std::strlen(s);
  if (!ImGui::GetCurrentContext() || ImGui::CalcTextSize(s).x <= max_w) {
    const uint32_t m = n < cap - 1 ? n : cap - 1;
    std::memcpy(out, s, m);
    out[m] = 0;
    return m;
  }
  static const char kDots[] = "\xE2\x80\xA6"; // U+2026
  const float dots_w = ImGui::CalcTextSize(kDots).x;
  // Sondan geri: UTF-8 devam baytlarini (10xxxxxx) atlayarak on-ek kisalt.
  uint32_t m = n;
  while (m > 0) {
    m--;
    while (m > 0 && ((unsigned char)s[m] & 0xC0) == 0x80) m--;
    if (ImGui::CalcTextSize(s, s + m).x + dots_w <= max_w) break;
  }
  if (m + 3 >= cap) m = cap > 4 ? cap - 4 : 0;
  std::memcpy(out, s, m);
  std::memcpy(out + m, kDots, 3);
  out[m + 3] = 0;
  return m + 3;
}

bool EditorUi::init(rhi::Device &dev, VkRenderPass rp, uint32_t subpass, uint32_t image_count, const char *font_ttf, float font_px,
                    float ui_scale, bool srgb_target, const char *icon_ttf) {
  dev_ = &dev;
  static LoaderCtx lc;
  lc.api = &dev.api();
  lc.instance = dev.instance();
  if (!ImGui_ImplVulkan_LoadFunctions(dev.caps().api_version, imgui_loader, &lc)) {
    std::snprintf(err_, sizeof err_, "ImGui Vulkan fonksiyonlari yuklenemedi");
    return false;
  }
  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImNodes::CreateContext();
  ImGuiIO &io = ImGui::GetIO();
  // Kendi imgui.ini'sini YAZMAZ: duzen kaliciligi editorun kendi belirlenimli
  // dosyasinin isi (ImGui'nin ini'si karenin yan etkisi olarak yazilir, headless
  // kosumda dosya birakir ve iki kosumu ayni yapmaz).
  io.IniFilename = "tulpar_layout.ini";
  io.LogFilename = nullptr;
  io.BackendPlatformName = "tulpar-window";
  // Klavye gezinmesi acik; AMA WantCaptureKeyboard'u ele GECIRMEDEN.
  // ConfigNavCaptureKeyboard varsayilani true'dur ve io.NavActive (= gezinme
  // acik + odakli bir ImGui penceresi var) oldugu anda WantCaptureKeyboard'u
  // true yapar. Editorde her zaman odakli bir pencere vardir, yani bayrak
  // SURESIZ true kalir ve ham tus kisayollari (T/R/S gizmo kipi) bir daha
  // hic calismazdi. False yapinca bayrak eski anlamina doner: "ImGui gercekten
  // bir ogeyi tutuyor" (metin kutusu / surukleme / kipli pencere).
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
  // DOCKING: panellerin kullanici tarafindan surukleyip yeniden duzenlenebilmesi
  // (Unity/Godot/Blender'in dordunde de var). Vendored ImGui docking dalinda.
  // ⚠ ViewportsEnable ACILMIYOR: platform backend'imiz yok, bayrak set edilirse
  // ImGui_ImplVulkan WSI cagrisina gidip abort-stub'i tetikler.
  io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
  io.ConfigNavCaptureKeyboard = false;
  // Pencere yalniz BASLIGINDAN surukleniyor: bos zemine tiklamak pencereyi
  // kaydirmaz. 3B'ye gecen kazara surukleme sinifini bastan siler.
  io.ConfigWindowsMoveFromTitleBarOnly = true;
  io.ConfigDragClickToInputText = true; // kaydiraca tiklayip deger yazma
  font_px_ = font_px > 8.0f ? font_px : 0.0f; // 0 = ImGui varsayilan boyutu
  if (font_ttf && *font_ttf) {
    FILE *f = std::fopen(font_ttf, "rb");
    if (f) {
      std::fclose(f);
      const float px = font_px > 8.0f ? font_px : 18.0f;
      io.Fonts->AddFontFromFileTTF(font_ttf, px);
      font_px_ = px;
      // --- Ikon fontu AYNI atlasa birlestirilir ----------------------------
      // MergeMode: ikinci font ayri bir ImFont DEGIL, oncekinin devamidir --
      // yani "ICON_MD_SAVE Kaydet" tek bir Text() cagrisinda yazilabilir,
      // PushFont/PopFont gerekmez. Her ImGui tabanli editorun yaptigi sey.
      //
      // ImGui 1.92'de GlyphRanges *LEGACY* (imgui.h:3759): glifler talep
      // uzerine yuklenir, bu yuzden klasik "aralik tablosu" tarifine gerek
      // yok. GlyphMinAdvanceX ikonlari tek genislige oturtur (etiketler
      // hizali kalsin); GlyphOffset.y ikonu metin taban cizgisine indirir --
      // ikon fontlarinin kutusu metin fontundan farkli oturur.
      if (icon_ttf && *icon_ttf) {
        FILE *g = std::fopen(icon_ttf, "rb");
        if (g) {
          std::fclose(g);
          ImFontConfig cfg;
          cfg.MergeMode = true;
          cfg.PixelSnapH = true;
          cfg.GlyphMinAdvanceX = px;          // tek genislik: ikonlar sutun gibi hizalanir
          cfg.GlyphOffset = ImVec2(0.0f, 3.0f); // taban cizgisine indir (17 px metinde olculdu)
          icons_ok_ = io.Fonts->AddFontFromFileTTF(icon_ttf, px, &cfg) != nullptr;
        }
      }
    }
  }
  srgb_target_ = srgb_target;
  ui_scale_ = 1.0f;
  set_ui_scale(ui_scale); // temayi da uygular
  ImGui_ImplVulkan_InitInfo ii{};
  ii.ApiVersion = dev.caps().api_version;
  ii.Instance = dev.instance();
  ii.PhysicalDevice = dev.physical();
  ii.Device = dev.handle();
  ii.QueueFamily = dev.queue_family();
  ii.Queue = dev.queue();
  ii.DescriptorPoolSize = 32; // dahili havuz (font atlasi + kullanici dokulari)
  ii.MinImageCount = image_count < 2 ? 2 : image_count;
  ii.ImageCount = ii.MinImageCount;
  ii.PipelineInfoMain.RenderPass = rp;
  ii.PipelineInfoMain.Subpass = subpass;
  ii.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
  ii.CheckVkResultFn = check_vk;
  ii.MinAllocationSize = 1024 * 1024; // BestPractices: kucuk ayirma uyarisi yok
  if (!ImGui_ImplVulkan_Init(&ii)) {
    std::snprintf(err_, sizeof err_, "ImGui_ImplVulkan_Init basarisiz");
    ImNodes::DestroyContext();
    ImGui::DestroyContext();
    return false;
  }
  ok_ = true;
  return true;
}

void EditorUi::shutdown() {
  if (!ok_) return;
  dev_->api().vkDeviceWaitIdle(dev_->handle());
  ImGui_ImplVulkan_Shutdown();
  ImNodes::DestroyContext();
  ImGui::DestroyContext();
  ok_ = false;
}

void EditorUi::set_ui_scale(float s) {
  if (!(s > 0.0f)) s = 1.0f;
  if (s < 0.5f) s = 0.5f;
  if (s > 4.0f) s = 4.0f;
  ui_scale_ = s;
  if (!ImGui::GetCurrentContext()) return;
  editor_apply_theme(s, srgb_target_);
  // Yazi: 1.92'de yazi tipi boyutu DINAMIK (istenen boyutta yeniden pisirilir).
  // FontSizeBase taban (olceksiz) boyut, FontScaleDpi ekran olcegi;
  // GetFontSize() == FontSizeBase * FontScaleMain * FontScaleDpi.
  ImGuiStyle &st = ImGui::GetStyle();
  st.FontSizeBase = font_px_; // 0 = yuklu yazi tipinin kendi boyutu
  st.FontScaleDpi = s;
}

void EditorUi::set_pointer_scale(float s) { pointer_scale_ = s > 0.0f ? s : 1.0f; }

// Girdi -> ImGui olay kuyrugu. Kenar tespiti burada; ImGui'nin kendi durumu
// SADECE olaylarla ilerler, her kare tam durum GONDERILMEZ.
void EditorUi::push_input(const platform::InputState *in) {
  ImGuiIO &io = ImGui::GetIO();
  if (!in) return;
  io.AddMousePosEvent((float)(in->mouse_x * pointer_scale_), (float)(in->mouse_y * pointer_scale_));
  for (int b = 0; b < 3; b++)
    if (in->mouse_down[b] != prev_mouse_[b]) { io.AddMouseButtonEvent(b, in->mouse_down[b]); prev_mouse_[b] = in->mouse_down[b]; }
  const double ds = in->scroll_y - prev_scroll_;
  if (ds != 0) { io.AddMouseWheelEvent(0.0f, (float)ds); prev_scroll_ = in->scroll_y; }
  for (int k = 0; k < 512; k++)
    if (in->key_down[k] != prev_keys_[k]) {
      prev_keys_[k] = in->key_down[k];
      const ImGuiKey ik = map_key(k);
      if (ik != ImGuiKey_None) io.AddKeyEvent(ik, in->key_down[k]);
    }
  // Degistiriciler SOL/SAG'IN BIRLESIMIDIR — tek tusun kenarindan degil.
  // Eskiden her tus kendi kenarinda ImGuiMod_* gonderiyordu: Sol Ctrl BASILIYKEN
  // Sag Ctrl'a basip birakmak "Ctrl kalkti" olayi uretiyor ve o andan sonra
  // Ctrl+S / Ctrl+Z / Ctrl+A calismiyordu (Sol Ctrl hala basili olmasina ragmen),
  // ta ki Sol Ctrl birakilip yeniden basilana kadar. AddKeyEvent ayni degeri
  // ikinci kez kuyruga KOYMAZ, bu yuzden her kare kosulsuz gondermek bedavadir.
  io.AddKeyEvent(ImGuiMod_Ctrl, in->key_down[GLFW_KEY_LEFT_CONTROL] || in->key_down[GLFW_KEY_RIGHT_CONTROL]);
  io.AddKeyEvent(ImGuiMod_Shift, in->key_down[GLFW_KEY_LEFT_SHIFT] || in->key_down[GLFW_KEY_RIGHT_SHIFT]);
  io.AddKeyEvent(ImGuiMod_Alt, in->key_down[GLFW_KEY_LEFT_ALT] || in->key_down[GLFW_KEY_RIGHT_ALT]);
  io.AddKeyEvent(ImGuiMod_Super, in->key_down[GLFW_KEY_LEFT_SUPER] || in->key_down[GLFW_KEY_RIGHT_SUPER]);
  for (uint32_t i = 0; i < in->char_count; i++) io.AddInputCharacter(in->chars[i]);
}

void EditorUi::begin_frame(const platform::InputState *in, float width, float height, float dt) {
  ImGuiIO &io = ImGui::GetIO();
  io.DisplaySize = ImVec2(width > 1 ? width : 1, height > 1 ? height : 1);
  io.DeltaTime = dt > 0 ? dt : 1.0f / 60.0f;
  push_input(in);
  ImGui_ImplVulkan_NewFrame();
  ImGui::NewFrame();
  ImGuizmo::BeginFrame();
}

void EditorUi::end_frame() {
  ImGui::Render();
  ImDrawData *dd = ImGui::GetDrawData();
  stats_ = EditorUiStats{};
  if (dd) { stats_.vertices = (uint32_t)dd->TotalVtxCount; stats_.indices = (uint32_t)dd->TotalIdxCount; stats_.draw_lists = (uint32_t)dd->CmdLists.Size; }
}

void EditorUi::record(VkCommandBuffer cb) {
  ImDrawData *dd = ImGui::GetDrawData();
  if (dd) ImGui_ImplVulkan_RenderDrawData(dd, cb);
}

bool EditorUi::wants_mouse() const { return ok_ && ImGui::GetIO().WantCaptureMouse; }
bool EditorUi::wants_keyboard() const { return ok_ && ImGui::GetIO().WantCaptureKeyboard; }
bool EditorUi::wants_text_input() const { return ok_ && ImGui::GetIO().WantTextInput; }


// ===========================================================================
// Editor mantigi (ImGui'siz): coklu secim, islem gruplari, kaynak tarayici,
// tel gizmolari. Panel kodu (editor_app.cpp) yalniz bunlari cagirir; kapilar
// (tests/test_editor.cpp) ayni fonksiyonlari olcer.
// ===========================================================================

bool Selection::contains(int32_t i) const {
  for (uint32_t k = 0; k < count; k++)
    if (items[k] == i) return true;
  return false;
}
void Selection::set_single(int32_t i) {
  count = 0;
  if (i >= 0) { items[0] = i; count = 1; }
}
void Selection::erase(int32_t i) {
  for (uint32_t k = 0; k < count; k++)
    if (items[k] == i) {
      for (uint32_t j = k; j + 1 < count; j++) items[j] = items[j + 1];
      count--;
      return;
    }
}
bool Selection::toggle(int32_t i) {
  if (i < 0) return false;
  if (contains(i)) { erase(i); return false; }
  if (count >= kMax) return false;
  for (uint32_t k = count; k > 0; k--) items[k] = items[k - 1]; // en son tiklanan ana secili olur
  items[0] = i;
  count++;
  return true;
}
void Selection::after_remove(int32_t removed) {
  erase(removed);
  for (uint32_t k = 0; k < count; k++)
    if (items[k] > removed) items[k]--;
}
uint32_t Selection::sorted_desc(int32_t *out) const {
  for (uint32_t k = 0; k < count; k++) out[k] = items[k];
  for (uint32_t a = 1; a < count; a++) { // yerleştirmeli siralama (n <= 256, ayirma yok)
    const int32_t v = out[a];
    uint32_t b = a;
    while (b > 0 && out[b - 1] < v) { out[b] = out[b - 1]; b--; }
    out[b] = v;
  }
  return count;
}

void OpGroups::push(uint32_t n) {
  if (n == 0) return;
  count_ = cursor_; // yeni eylem yinele kuyrugunu siler (gunluk de siliyor)
  if (count_ >= kCap) {
    for (uint32_t i = 1; i < kCap; i++) sizes_[i - 1] = sizes_[i];
    count_ = kCap - 1;
  }
  sizes_[count_++] = n;
  cursor_ = count_;
}
uint32_t OpGroups::undo_size() { return cursor_ ? sizes_[--cursor_] : 1u; }
uint32_t OpGroups::redo_size() { return cursor_ < count_ ? sizes_[cursor_++] : 1u; }

namespace {
bool ends_with_ci(const char *s, const char *suf) {
  const size_t ls = std::strlen(s), lf = std::strlen(suf);
  if (lf > ls) return false;
  for (size_t i = 0; i < lf; i++) {
    char a = s[ls - lf + i], b = suf[i];
    if (a >= 'A' && a <= 'Z') a = (char)(a - 'A' + 'a');
    if (a != b) return false;
  }
  return true;
}
} // namespace

uint32_t editor_scan_assets(const char *dir, const content::SceneDesc &d, AssetFile *out, uint32_t cap) {
  if (!dir || !*dir || !out || cap == 0) return 0;
  DIR *dp = ::opendir(dir);
  if (!dp) return 0;
  uint32_t n = 0;
  for (struct dirent *de = ::readdir(dp); de; de = ::readdir(dp)) {
    const char *nm = de->d_name;
    if (nm[0] == '.') continue; // gizli + . / ..
    if (!ends_with_ci(nm, ".gltf") && !ends_with_ci(nm, ".glb")) continue;
    if (std::strlen(nm) >= content::kScenePathLen) continue;
    if (n >= cap) break;
    AssetFile f;
    std::snprintf(f.name, sizeof f.name, "%s", nm);
    for (uint32_t i = 0; i < d.asset_count; i++)
      if (!std::strcmp(d.assets[i], nm)) { f.in_scene = true; f.index = (int32_t)i; break; }
    uint32_t k = n; // ada gore sirali ekle: readdir sirasi dosya sistemine bagli
    while (k > 0 && std::strcmp(out[k - 1].name, f.name) > 0) { out[k] = out[k - 1]; k--; }
    out[k] = f;
    n++;
  }
  ::closedir(dp);
  return n;
}

uint32_t editor_add_asset_entity(content::SceneDesc &d, content::SceneHistory &h, const char *file, Vec3 pos, int32_t *out_asset) {
  if (out_asset) *out_asset = -1;
  if (!file || !*file) return 0;
  const int32_t a = d.add_asset(file);
  if (a < 0) return 0; // kaynak tablosu dolu
  if (out_asset) *out_asset = a;
  char stem[content::kSceneNameLen] = {0};
  std::snprintf(stem, sizeof stem, "%s", file);
  for (char *q = stem; *q; q++)
    if (*q == '.') { *q = 0; break; }
  content::SceneEntity e{};
  std::snprintf(e.name, sizeof e.name, "%.20s_%u", stem, d.entity_count + 1);
  e.pos = pos;
  e.components = content::kSceneModel;
  e.asset = a;
  return h.add_entity(d, e) ? 1u : 0u;
}

uint32_t selection_commit(content::SceneDesc &d, content::SceneHistory &h, const int32_t *sel, uint32_t n,
                          const content::SceneEntity *before, const content::SceneEntity *after) {
  uint32_t ops = 0;
  for (uint32_t k = 0; k < n; k++) {
    const int32_t i = sel[k];
    if (i < 0 || i >= (int32_t)d.entity_count) continue;
    d.entities[i] = before[k]; // gunluk once/sonra'yi kendisi kaydeder
    if (h.set_entity(d, (uint32_t)i, after[k])) ops++;
  }
  return ops;
}

uint32_t selection_translate(content::SceneDesc &d, content::SceneHistory &h, const int32_t *sel, uint32_t n, Vec3 delta) {
  uint32_t ops = 0;
  for (uint32_t k = 0; k < n; k++) {
    const int32_t i = sel[k];
    if (i < 0 || i >= (int32_t)d.entity_count) continue;
    content::SceneEntity after = d.entities[i];
    after.pos += delta;
    if (h.set_entity(d, (uint32_t)i, after)) ops++;
  }
  return ops;
}

uint32_t selection_remove(content::SceneDesc &d, content::SceneHistory &h, const int32_t *sel, uint32_t n) {
  int32_t idx[Selection::kMax];
  const uint32_t m = n < Selection::kMax ? n : Selection::kMax;
  for (uint32_t k = 0; k < m; k++) idx[k] = sel[k];
  for (uint32_t a = 1; a < m; a++) { // buyukten kucuge: silinen indeks sonrakileri kaydirir
    const int32_t v = idx[a];
    uint32_t b = a;
    while (b > 0 && idx[b - 1] < v) { idx[b] = idx[b - 1]; b--; }
    idx[b] = v;
  }
  uint32_t ops = 0;
  int32_t prev = -1;
  for (uint32_t k = 0; k < m; k++) {
    if (idx[k] < 0 || idx[k] == prev || idx[k] >= (int32_t)d.entity_count) continue;
    prev = idx[k];
    if (h.remove_entity(d, (uint32_t)idx[k])) ops++;
  }
  return ops;
}

namespace {
// Tel kutu: 12 kenar, her biri ince eksen hizali kutu (birim kup +-0.5).
void wire_box(renderer::Renderer &ren, renderer::MeshHandle cube, Vec3 c, Vec3 half, Vec3 color, float th, uint32_t *n) {
  const Vec3 s = half * 2.0f + Vec3{th, th, th};
  for (int a = 0; a < 4; a++) {
    const float y = (a & 1) ? half.y : -half.y, z = (a & 2) ? half.z : -half.z;
    ren.draw(cube, Mat4::translate({c.x, c.y + y, c.z + z}) * Mat4::scale({s.x, th, th}), color);
    (*n)++;
  }
  for (int a = 0; a < 4; a++) {
    const float x = (a & 1) ? half.x : -half.x, z = (a & 2) ? half.z : -half.z;
    ren.draw(cube, Mat4::translate({c.x + x, c.y, c.z + z}) * Mat4::scale({th, s.y, th}), color);
    (*n)++;
  }
  for (int a = 0; a < 4; a++) {
    const float x = (a & 1) ? half.x : -half.x, y = (a & 2) ? half.y : -half.y;
    ren.draw(cube, Mat4::translate({c.x + x, c.y + y, c.z}) * Mat4::scale({th, th, s.z}), color);
    (*n)++;
  }
}
// a -> b arasi ince kutu: sutunlar (sag, yukari, yon) * kalinlik/uzunluk.
Mat4 segment_matrix(Vec3 a, Vec3 b, float th) {
  Vec3 dv = b - a;
  float len = length(dv);
  if (len < 1e-6f) { dv = {0, 0, 1e-6f}; len = 1e-6f; }
  const Vec3 f = dv * (1.0f / len);
  const Vec3 up = std::fabs(f.y) > 0.99f ? Vec3{1, 0, 0} : Vec3{0, 1, 0};
  const Vec3 r = normalize(cross(up, f));
  const Vec3 u = cross(f, r);
  Mat4 m;
  m.m[0][0] = r.x * th; m.m[0][1] = r.y * th; m.m[0][2] = r.z * th; m.m[0][3] = 0;
  m.m[1][0] = u.x * th; m.m[1][1] = u.y * th; m.m[1][2] = u.z * th; m.m[1][3] = 0;
  m.m[2][0] = f.x * len; m.m[2][1] = f.y * len; m.m[2][2] = f.z * len; m.m[2][3] = 0;
  m.m[3][0] = (a.x + b.x) * 0.5f; m.m[3][1] = (a.y + b.y) * 0.5f; m.m[3][2] = (a.z + b.z) * 0.5f; m.m[3][3] = 1;
  return m;
}
// Ok: govde + uc (kalin kisa kutu). Donus: cizim sayisi.
uint32_t arrow(renderer::Renderer &ren, renderer::MeshHandle cube, Vec3 a, Vec3 b, Vec3 color, float th) {
  const Vec3 dv = b - a;
  const float len = length(dv);
  if (len < 1e-4f) return 0;
  const Vec3 f = dv * (1.0f / len);
  const Vec3 neck = a + f * (len * 0.82f);
  ren.draw(cube, segment_matrix(a, neck, th), color);
  ren.draw(cube, segment_matrix(neck, b, th * 3.0f), color);
  return 2;
}
// Isik varligini isaretleyen isaret: merkezden gecen 3 eksenli cubuk (yildiz,
// 3 cizim -- her eksen TEK kutu, -len..+len).
// Yaricap tel-kutusundan (wire_box) AYRI amac: o hacmi gosterir, bu "burada bir
// isik var" der -- ikisi ayni sekilde cizilince kullanicinin gordugu "duz kup"
// sorunuydu. Kup mesh KULLANMAZ demiyoruz (motor cizgiyi de kup'tan orer,
// editor_ui.hpp'nin "ayri boru hatti yok" kurali), yalniz kup TEK BASINA
// (dolgun, buyuk) yerine ince cubuklardan yildiz cizilir.
uint32_t light_glyph(renderer::Renderer &ren, renderer::MeshHandle cube, Vec3 c, Vec3 color, float th) {
  static const Vec3 kAxes[3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
  const float len = 0.35f;
  uint32_t n = 0;
  for (int a = 0; a < 3; a++) {
    ren.draw(cube, segment_matrix(c - kAxes[a] * len, c + kAxes[a] * len, th), color);
    n++;
  }
  return n;
}
// Kamera gizmosu: govde (kucuk tel kutu, "bu bos kutu degil kamera" isareti)
// + FOV/near/far'dan hesaplanan tel kafes gorus alani (yakin+uzak dortgen ve
// baglayan 4 kenar). Uzak duzlem GORSEL olarak kirpilir (golge hacmindeki
// "kilavuz, alarm degil" ilkesiyle ayni: cam_far varsayilani 200 birim,
// kirpilmezse frustum sahneye hakim olur ve hata gibi okunur).
uint32_t camera_frustum(renderer::Renderer &ren, renderer::MeshHandle cube, Vec3 eye, Vec3 fwd, Vec3 up, float fov_deg, float znear,
                         float zfar, Vec3 color, float th) {
  const float aspect = 16.0f / 9.0f;
  const float fov = (fov_deg > 1.0f ? fov_deg : 60.0f) * 3.14159265f / 180.0f;
  const float near_d = znear > 0.01f ? znear : 0.1f;
  float far_d = zfar > near_d ? zfar : 8.0f;
  if (far_d > 6.0f) far_d = 6.0f;
  const Vec3 f = length_sq(fwd) > 1e-8f ? normalize(fwd) : Vec3{0, 0, -1};
  const Vec3 u0 = length_sq(up) > 1e-8f ? normalize(up) : Vec3{0, 1, 0};
  const Vec3 r = normalize(cross(f, u0));
  const Vec3 u = cross(r, f);
  const float dists[2] = {near_d, far_d};
  Vec3 corners[2][4];
  for (int p = 0; p < 2; p++) {
    const float hh = std::tan(fov * 0.5f) * dists[p];
    const float hw = hh * aspect;
    const Vec3 center = eye + f * dists[p];
    corners[p][0] = center + r * hw + u * hh;
    corners[p][1] = center - r * hw + u * hh;
    corners[p][2] = center - r * hw - u * hh;
    corners[p][3] = center + r * hw - u * hh;
  }
  uint32_t n = 0;
  for (int p = 0; p < 2; p++)
    for (int k = 0; k < 4; k++) { ren.draw(cube, segment_matrix(corners[p][k], corners[p][(k + 1) & 3], th), color); n++; }
  for (int k = 0; k < 4; k++) { ren.draw(cube, segment_matrix(corners[0][k], corners[1][k], th), color); n++; }
  wire_box(ren, cube, eye, {0.12f, 0.12f, 0.12f}, color, th, &n);
  return n;
}
} // namespace

uint32_t editor_draw_gizmos(renderer::Renderer &ren, renderer::MeshHandle cube, const content::SceneDesc &d, const int32_t *sel,
                            uint32_t n, const GizmoOptions &o) {
  if (!cube.valid()) return 0;
  const float th = o.thickness > 0.005f ? o.thickness : 0.005f;
  uint32_t draws = 0;
  if (o.light_radius || o.light_glyph) {
    for (uint32_t i = 0; i < d.entity_count; i++) {
      const content::SceneEntity &e = d.entities[i];
      if (!(e.components & content::kSceneLight)) continue;
      // DUNYA matrisi: ebeveyni olan bir isik yerelde konumlanirsa yanlis
      // yerde cizilirdi (bu, kullanicinin gordugu yer degistirmeli "kare"
      // sorununun bir parcasiydi). scene_entity_matrix SADECE kok icin dogru.
      const Mat4 m = content::scene_entity_world_matrix(d, i);
      const Vec3 p{m.m[3][0], m.m[3][1], m.m[3][2]};
      bool is_sel = false;
      for (uint32_t k = 0; k < n && !is_sel; k++) is_sel = sel && sel[k] == (int32_t)i;
      const Vec3 tint = e.light_color * (is_sel ? 1.0f : 0.35f);
      const bool directional = e.light_type == content::SceneLightType::Directional;
      // Yaricap tel-kutusu yalniz Nokta icin anlamli (Yonlu'de "yaricap" alani
      // panelde de gizli -- ikisi tutarli olmali).
      if (o.light_radius && !directional) {
        // editor.sahne'deki kirmizi isik 8 birim yaricapli; tam kalinlikta 16
        // birimlik kirmizi kutu sahneye hakim oluyor ve hata gibi okunuyordu
        // (kullanicinin gordugu "buyuk kalin kirmizi kutu" bu) -- secili
        // degilse yarim kalinlik.
        const float r = e.light_radius;
        wire_box(ren, cube, p, {r, r, r}, tint, is_sel ? th : th * 0.5f, &draws);
      }
      if (o.light_glyph) {
        if (directional) {
          // Nokta'nin yildizindan AYRI, taniniir bir "gunes oku": varligin
          // DONUSUNDEN (-Z sutunu, kamera gizmosuyla ayni sozlesme) hesaplanan
          // yon boyunca isaga dogru bir ok.
          const Vec3 fwd{-m.m[2][0], -m.m[2][1], -m.m[2][2]};
          const Vec3 dir = length_sq(fwd) > 1e-8f ? normalize(fwd) : Vec3{0, -1, 0};
          draws += arrow(ren, cube, p, p + dir * 1.2f, tint, is_sel ? th * 1.5f : th);
        } else {
          draws += light_glyph(ren, cube, p, tint, is_sel ? th * 1.5f : th);
        }
      }
    }
  }
  if (o.camera_frustum) {
    for (uint32_t i = 0; i < d.entity_count; i++) {
      const content::SceneEntity &e = d.entities[i];
      if (!(e.components & content::kSceneCamera)) continue;
      const Mat4 m = content::scene_entity_world_matrix(d, i);
      const Vec3 eye{m.m[3][0], m.m[3][1], m.m[3][2]};
      const Vec3 fwd{-m.m[2][0], -m.m[2][1], -m.m[2][2]};
      const Vec3 up{m.m[1][0], m.m[1][1], m.m[1][2]};
      bool is_sel = false;
      for (uint32_t k = 0; k < n && !is_sel; k++) is_sel = sel && sel[k] == (int32_t)i;
      const Vec3 color = is_sel ? Vec3{1.0f, 0.9f, 0.4f} : Vec3{0.55f, 0.75f, 0.95f};
      draws += camera_frustum(ren, cube, eye, fwd, up, e.cam_fov, e.cam_near, e.cam_far, color, is_sel ? th : th * 0.6f);
    }
  }
  if (o.shadow_volume) {
    // Golge hacmi bir KILAVUZ, alarm degil: sahnenin en buyuk kutusu oldugu
    // icin tam kalinlik + doygun mavi (eski) goruntuye hakim oluyor ve hata
    // gibi okunuyordu. Kalinlik 0.35x (0.06 -> ~0.02 dunya birimi), renk
    // solgun kursuni-mavi. Isik yaricapi ve gunes oku tam kalinlikta kalir
    // (onlar kucuk ve secilebilir; cizim sayisi degismez: 12 kenar).
    const float r = d.shadow_radius;
    wire_box(ren, cube, d.shadow_center, {r, r, r}, {0.30f, 0.38f, 0.50f}, th * 0.35f, &draws);
  }
  if (o.sun_dir) {
    const Vec3 dir = normalize(d.sun_dir); // isiga dogru (shader: dot(n, light_dir))
    const float len = d.shadow_radius > 1.0f ? d.shadow_radius * 0.8f : 4.0f;
    draws += arrow(ren, cube, d.shadow_center, d.shadow_center + dir * len, {1.0f, 0.85f, 0.25f}, th * 1.5f);
  }
  return draws;
}

// Donusumlu tel kutu: 12 kenar YEREL uzayda kurulur, sonra m ile dunyaya
// tasinir. wire_box eksen hizali (AABB) cizer; carpisma hacmi ise govdeyle
// birlikte DONER -- dondurulmus bir kutu govdesini AABB ile gostermek
// carpismanin gercekte nerede oldugu hakkinda yalan soylerdi.
uint32_t editor_wire_box_m(renderer::Renderer &ren, renderer::MeshHandle cube, const Mat4 &m, Vec3 half, Vec3 color,
                           float th) {
  uint32_t n = 0;
  const Vec3 h = half;
  for (int a = 0; a < 4; a++) {
    const float s0 = (a & 1) ? 1.0f : -1.0f, s1 = (a & 2) ? 1.0f : -1.0f;
    ren.draw(cube, m * Mat4::translate({0, s0 * h.y, s1 * h.z}) * Mat4::scale({h.x * 2 + th, th, th}), color); n++;
    ren.draw(cube, m * Mat4::translate({s0 * h.x, 0, s1 * h.z}) * Mat4::scale({th, h.y * 2 + th, th}), color); n++;
    ren.draw(cube, m * Mat4::translate({s0 * h.x, s1 * h.y, 0}) * Mat4::scale({th, th, h.z * 2 + th}), color); n++;
  }
  return n;
}

uint32_t editor_wire_aabb(renderer::Renderer &ren, renderer::MeshHandle cube, Vec3 lo, Vec3 hi, Vec3 color, float th) {
  uint32_t n = 0;
  wire_box(ren, cube, (lo + hi) * 0.5f, (hi - lo) * 0.5f, color, th, &n);
  return n;
}

} // namespace tulpar::engine::app
