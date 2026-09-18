#include "app/editor_overlay.hpp"

#include <cmath>
#include <cstdio>
#include <cstring>

#include <imgui.h>
#include <imgui_internal.h> // GetCurrentWindow: imlec/yerlesim durumunu TAM geri almak icin

namespace tulpar::engine::app {

namespace {
// Piksel hizali olcu (ImTrunc imgui_internal.h'de; bu TU yalniz acik basligi gorur).
inline float ImTrunc(float v) { return (float)(int)v; }
// Palet -> ImU32 (alfa carpaniyla). Tema srgb_target ile uygulandiysa ton zaten
// dogrusaldir; burada bir daha CEVRILMEZ (editor_tone sozlesmesi).
ImU32 tone_u32(Tone t, float alpha = 1.0f) {
  float c[4];
  editor_tone(t, c);
  return ImGui::ColorConvertFloat4ToU32(ImVec4(c[0], c[1], c[2], c[3] * alpha));
}
// Iki tonun karisimi (k: 0 = a, 1 = b) — arka eksenlerin "solgun" hali icin.
ImU32 tone_mix(Tone a, Tone b, float k, float alpha = 1.0f) {
  float ca[4], cb[4];
  editor_tone(a, ca);
  editor_tone(b, cb);
  return ImGui::ColorConvertFloat4ToU32(ImVec4(ca[0] + (cb[0] - ca[0]) * k, ca[1] + (cb[1] - ca[1]) * k, ca[2] + (cb[2] - ca[2]) * k, alpha));
}
ImVec2 text_size(const char *s, float font_px) { return ImGui::GetFont()->CalcTextSizeA(font_px, 3.4e38f, 0.0f, s); }

// --- Hap (pill): yuvarlak, yari saydam zemin + ince kenarlik + [simge] metin ---
struct PillStyle {
  float font_px, pad_x, pad_y, rounding, gap;
  float h() const { return font_px + 2.0f * pad_y; }
};
float pill_width(const PillStyle &ps, const char *icon, const char *text) {
  float w = 2.0f * ps.pad_x + text_size(text, ps.font_px).x;
  if (icon && *icon) w += text_size(icon, ps.font_px).x + ps.gap;
  return ImTrunc(w);
}
void pill_draw(ImDrawList *dl, const PillStyle &ps, ImVec2 p, float w, const char *icon, ImU32 icon_col, const char *text, ImU32 fg, ImU32 bg,
               ImU32 border) {
  const ImVec2 q(p.x + w, p.y + ps.h());
  dl->AddRectFilled(p, q, bg, ps.rounding);
  if ((border >> 24) != 0) dl->AddRect(p, q, border, ps.rounding, 1.0f);
  float x = p.x + ps.pad_x;
  if (icon && *icon) {
    dl->AddText(ImGui::GetFont(), ps.font_px, ImVec2(x, p.y + ps.pad_y), icon_col, icon);
    x += text_size(icon, ps.font_px).x + ps.gap;
  }
  dl->AddText(ImGui::GetFont(), ps.font_px, ImVec2(x, p.y + ps.pad_y), fg, text);
}

// Tek bir eksen ucu (izdusum + cizim sirasi icin derinlik).
struct AxisEnd {
  int axis;      // 0 X, 1 Y, 2 Z
  bool positive; // +uc harfli dolu disk, -uc ici bos halka
  float x, y, depth;
};
void sort_by_depth(AxisEnd *e, int n) { // arkadakiler ONCE cizilir (n = 6, yerlestirmeli)
  for (int i = 1; i < n; i++) {
    const AxisEnd v = e[i];
    int j = i;
    while (j > 0 && e[j - 1].depth > v.depth) { e[j] = e[j - 1]; j--; }
    e[j] = v;
  }
}
Tone axis_tone(int a) { return a == 0 ? Tone::AxisX : a == 1 ? Tone::AxisY : Tone::AxisZ; }

// hover_axis: vurgulanacak uc (a*2 + (negatif?1:0)), -1 = yok. Vurgu YALNIZ fare
// diskin ustundeyken gelir; kapilarin cogu faresiz kosar ve pikselleri degismez.
void draw_axis_gizmo(ImDrawList *dl, ImVec2 c, float R, float end_r, float font_px, const float view[16], int hover_axis) {
  AxisProjection pr;
  overlay_project_axes(view, R, &pr);
  // Arka disk: gostergeyi goruntuden ayirir ama onu ortmez (alfa dusuk).
  dl->AddCircleFilled(c, R + end_r + ImTrunc(font_px * 0.25f), tone_u32(Tone::Bg0, 0.35f), 40);
  AxisEnd ends[6];
  for (int a = 0; a < 3; a++) {
    ends[a * 2] = AxisEnd{a, true, pr.x[a], pr.y[a], pr.depth[a]};
    ends[a * 2 + 1] = AxisEnd{a, false, -pr.x[a], -pr.y[a], -pr.depth[a]};
  }
  sort_by_depth(ends, 6);
  static const char *const kLetter[3] = {"X", "Y", "Z"};
  const float letter_px = ImTrunc(font_px * 0.8f);
  const float line_th = font_px > 12.0f ? ImTrunc(font_px * 0.12f) : 1.0f;
  for (int i = 0; i < 6; i++) {
    const AxisEnd &e = ends[i];
    const bool back = e.depth < -0.02f; // kameradan uzaga bakan uc: soluk
    const ImVec2 p(c.x + e.x, c.y + e.y);
    const Tone t = axis_tone(e.axis);
    const int end_id = e.axis * 2 + (e.positive ? 0 : 1);
    if (end_id == hover_axis) // vurgu: ucun arkasinda genis, yumusak halka
      dl->AddCircleFilled(p, end_r * 1.45f, tone_u32(Tone::AccentHi, 0.55f), 24);
    if (e.positive) {
      const ImU32 fill = back ? tone_mix(t, Tone::Bg0, 0.55f) : tone_u32(t);
      dl->AddLine(c, p, back ? tone_mix(t, Tone::Bg0, 0.55f, 0.8f) : tone_u32(t, 0.95f), line_th);
      dl->AddCircleFilled(p, end_r, fill, 24);
      const ImVec2 ls = text_size(kLetter[e.axis], letter_px);
      dl->AddText(ImGui::GetFont(), letter_px, ImVec2(p.x - ls.x * 0.5f, p.y - ls.y * 0.5f), back ? tone_u32(Tone::TextDim) : tone_u32(Tone::Bg0),
                  kLetter[e.axis]);
    } else {
      // Negatif uc: halka (Blender gelenegi) — yon bilgisi kaybolmaz, harf yok.
      dl->AddCircleFilled(p, end_r * 0.72f, tone_u32(Tone::Bg0, back ? 0.25f : 0.45f), 20);
      dl->AddCircle(p, end_r * 0.72f, tone_u32(t, back ? 0.30f : 0.65f), 20, 1.5f);
    }
  }
}

// Fare hangi eksen UCUNUN uzerinde? Donus: a*2 + (negatif?1:0), yoksa -1.
// Ust uste binen uclerde ONDEKI kazanir (skora derinlik yanliligi eklenir) —
// yoksa arkadaki ucu tiklamak one gecmis gibi gorunur.
int axis_end_at(const float view[16], float R, float end_r, ImVec2 c, ImVec2 m) {
  AxisProjection pr;
  overlay_project_axes(view, R, &pr);
  const float reach = end_r * 1.35f;
  int best = -1;
  float best_score = 3.4e38f;
  for (int a = 0; a < 3; a++)
    for (int s = 0; s < 2; s++) {
      const float ex = s ? -pr.x[a] : pr.x[a], ey = s ? -pr.y[a] : pr.y[a], dep = s ? -pr.depth[a] : pr.depth[a];
      const float dx = m.x - (c.x + ex), dy = m.y - (c.y + ey);
      const float d = std::sqrt(dx * dx + dy * dy);
      if (d > reach) continue;
      const float score = d - dep * end_r * 0.6f;
      if (score < best_score) { best_score = score; best = a * 2 + s; }
    }
  return best;
}

// Kutu (marquee) suruklemesinin KARELERE YAYILAN durumu. Kaplama durumsuz bir
// fonksiyon oldugu icin burada duruyor; editorde ayni anda tek "Gorunum" paneli
// var, yani tek kayit yeter. Sol tus birakilinca her halukarda sifirlanir —
// baska bir sondaya/kareye bulasik surukleme kalmaz.
struct BoxDrag {
  bool active = false;
  float x0 = 0, y0 = 0, x1 = 0, y1 = 0;
};
BoxDrag g_box;
constexpr float kBoxMin = 4.0f; // bu esigin altinda surukleme TIKTIR, kutu degil

// ASCII buyuk/kucuk harf duyarsiz alt dizi (dosya adi suzgeci).
bool contains_ci(const char *hay, const char *needle) {
  if (!needle || !*needle) return true;
  const size_t nh = std::strlen(hay), nn = std::strlen(needle);
  if (nn > nh) return false;
  for (size_t i = 0; i + nn <= nh; i++) {
    size_t k = 0;
    for (; k < nn; k++) {
      char a = hay[i + k], b = needle[k];
      if (a >= 'A' && a <= 'Z') a = (char)(a - 'A' + 'a');
      if (b >= 'A' && b <= 'Z') b = (char)(b - 'A' + 'a');
      if (a != b) break;
    }
    if (k == nn) return true;
  }
  return false;
}
} // namespace

void overlay_project_axes(const float view[16], float radius, AxisProjection *out) {
  if (!out) return;
  *out = AxisProjection{};
  if (!view) return;
  // Sutun-major: dunya ekseni a'nin gorunum uzayi karsiligi = 3x3'un a. SUTUNU
  // (view[a*4 + 0..2]). Gorunum uzayinda +y yukari, ekranda asagi: y ters.
  // z: gorunum uzayinda kamera -z'ye bakar, +z kameraya DOGRU = "on".
  for (int a = 0; a < 3; a++) {
    const float vx = view[a * 4 + 0], vy = view[a * 4 + 1], vz = view[a * 4 + 2];
    out->x[a] = vx * radius;
    out->y[a] = -vy * radius;
    out->depth[a] = vz;
  }
}

void viewport_overlay(const ViewportRect &r, const OverlayInfo &info, OverlayLayout *out_layout, OverlayResult *out_res) {
  OverlayLayout lay;
  OverlayResult res;
  if (out_layout) *out_layout = lay;
  if (out_res) *out_res = res;
  if (!ImGui::GetCurrentContext() || !(r.w >= 2.0f) || !(r.h >= 2.0f)) return;
  ImDrawList *dl = ImGui::GetWindowDrawList();
  const ImGuiStyle &st = ImGui::GetStyle();
  const float fs = ImGui::GetFontSize();
  const ImVec2 rmin(r.x, r.y), rmax(r.x + r.w, r.y + r.h);
  const ImVec2 mouse = ImGui::GetIO().MousePos;
  // Her sey r'ye kirpilir: yuvarlatilmis kenar/ok ucu bile disari tasmaz.
  dl->PushClipRect(rmin, rmax, true);

  const PillStyle ps{fs, st.FramePadding.x, st.FramePadding.y, ImTrunc(fs * 0.35f), st.ItemInnerSpacing.x};
  const float pad = ImTrunc(fs * 0.5f);
  const float gap = st.ItemInnerSpacing.x;
  const ImU32 pill_bg = tone_u32(Tone::Bg0, 0.75f), pill_border = tone_u32(Tone::Line, 0.9f);
  const ImU32 text = tone_u32(Tone::Text), dim = tone_u32(Tone::TextDim), accent = tone_u32(Tone::Accent);
  // Tiklanabilir cip: kenarligi vurgu renginde (bakinca "buna tiklanir" belli).
  const ImU32 chip_border = tone_u32(Tone::Accent, 0.55f);
  const ImU32 chip_bg_hot = tone_u32(Tone::Bg3, 0.95f), chip_border_hot = tone_u32(Tone::AccentHi, 0.95f);

  // Etkilesimli ogelerin ORTAK SOZLESMESI (bkz. baslik): oge yalniz fare TAM
  // USTUNDEYKEN eklenir. Her karede eklenseydi goruntunun o parcasi tiklamayi
  // ve ustunde-durmayi kaybederdi; boyleyken diskin/cipin disinda kalan her
  // piksel altindaki ImGui::Image'e duser.
  // Yerlesim durumu TAM geri alinir (imlec + CursorMaxPos + IsSetPos): kaplama
  // pencerenin akisina hicbir iz birakmaz. SetCursorScreenPos ile geri almak
  // yetmez — ImGui End()'te "SetCursorPos ile sinir buyutuldu" hatasi verir
  // (son ISLEM imlec tasimasi olur, ardindan oge gelmez) ve CursorMaxPos
  // dugmenin dikdortgenine buyumus kalir, yani panel bosuna kaydirilabilir olur.
  auto hot_item = [&](const char *id, ImVec2 p, ImVec2 sz, bool *out_clicked) {
    ImGuiWindow *w = ImGui::GetCurrentWindow();
    const ImVec2 save_pos = w->DC.CursorPos, save_max = w->DC.CursorMaxPos;
    const bool save_setpos = w->DC.IsSetPos;
    ImGui::SetCursorScreenPos(p);
    ImGui::InvisibleButton(id, sz);
    const bool hov = ImGui::IsItemHovered();
    if (out_clicked) *out_clicked = ImGui::IsItemClicked(ImGuiMouseButton_Left);
    w->DC.CursorPos = save_pos;
    w->DC.CursorMaxPos = save_max;
    w->DC.IsSetPos = save_setpos;
    return hov;
  };

  // --- b) Eksen gostergesi (sag-ust): sigarsa. Ust satir ve alt satir buna gore
  // yer birakir; sigmazsa hicbir sey cizilmez (yarim gosterge yok).
  const float R = ImTrunc(fs * 2.0f), end_r = ImTrunc(fs * 0.55f);
  const float gizmo_box = 2.0f * (R + end_r + ImTrunc(fs * 0.25f));
  ImVec2 gizmo_c(0, 0);
  if (r.w >= gizmo_box + 2.0f * pad && r.h >= gizmo_box + 2.0f * pad) {
    lay.gizmo = true;
    gizmo_c = ImVec2(rmax.x - pad - gizmo_box * 0.5f, rmin.y + pad + gizmo_box * 0.5f);
    lay.gizmo_cx = gizmo_c.x;
    lay.gizmo_cy = gizmo_c.y;
    lay.gizmo_r = R;
    lay.gizmo_end_r = end_r;
  }

  // --- b2) Gostergenin ETKILESIMI. Oge, karesel bir kutu degil DISKIN KENDISI
  // kadar: once dairesel mesafe olculur, oge ancak fare diskin icindeyse eklenir.
  // Kosedeki (diskin disindaki, kutunun icindeki) tiklama boylece goruntuye duser.
  int axis_hover = -1;
  if (lay.gizmo) {
    const float disc_r = gizmo_box * 0.5f;
    const float ddx = mouse.x - gizmo_c.x, ddy = mouse.y - gizmo_c.y;
    if (ddx * ddx + ddy * ddy <= disc_r * disc_r) {
      bool clicked = false;
      if (hot_item("##eksen_gostergesi", ImVec2(gizmo_c.x - disc_r, gizmo_c.y - disc_r), ImVec2(disc_r * 2.0f, disc_r * 2.0f), &clicked)) {
        res.consumed_mouse = true; // diskin GOVDESI de fareyi yutar (bu bir arac, delik degil)
        axis_hover = axis_end_at(info.view, R, end_r, gizmo_c, mouse);
        res.axis_hovered = axis_hover;
        if (clicked && axis_hover >= 0) res.axis_clicked = axis_hover;
      }
    }
  }

  // --- a) Ust satir haplari: adaylar oncelik sirasiyla; sigmayanlar dusurulur
  // (once en az onemli), kalanlar gorsel sirayla cizilir. chip >= 0 olanlar
  // TIKLANABILIR (izdusum / kamera kipi / gizmo uzayi).
  //
  // DUSURME SIRASI (drop_rank, buyuk = once dusur): 0 oynatiliyor, 1 gizmo kipi,
  // 2 izdusum, 3 gizmo uzayi, 4 kamera kipi, 5 golgeleme, 6 "gizmolar kapali",
  // 7 istatistik. Istatistik EN ONCE duser cunku hem en genisi odur hem de ayni
  // kare suresi DURUM CUBUGUNDA zaten yaziyor; tiklanabilir cipler ise islev
  // kaybi demektir, en sona kalirlar.
  struct Cand {
    const char *icon, *text;
    ImU32 icon_col, fg, bg, border;
    int drop_rank; // buyuk = once dusur
    int chip;      // -1 pasif etiket; 0 izdusum, 1 kamera kipi, 2 gizmo uzayi
    float w, x;
    bool keep, hot;
  };
  char stats[96] = {0};
  if (info.frame_ms > 0.0f)
    std::snprintf(stats, sizeof stats, "%.1f ms \xC2\xB7 %.0f fps \xC2\xB7 %u \xC3\xA7izim \xC2\xB7 %u varl\xC4\xB1k", (double)info.frame_ms,
                  (double)(1000.0f / info.frame_ms), info.draw_calls, info.entity_count);
  static const char *const kOpIcon[3] = {"\xE2\x9C\xA5", "\xE2\x86\xBB", "\xE2\x87\xB2"}; // ✥ ↻ ⇲ (DejaVuSans'ta var)
  static const char *const kOpName[3] = {"Ta\xC5\x9F\xC4\xB1", "D\xC3\xB6nd\xC3\xBCr", "\xC3\x96l\xC3\xA7""ekle"};
  const int op = info.gizmo_op >= 0 && info.gizmo_op < 3 ? info.gizmo_op : 0;
  const char *proj_txt = info.proj == CameraProjection::Perspective ? "Perspektif" : "Ortografik";
  const char *mode_txt = info.cam_mode == CameraMode::Orbit ? "Y\xC3\xB6r\xC3\xBCnge" : "U\xC3\xA7u\xC5\x9F";
  const char *space_txt = info.gizmo_space == GizmoSpace::World ? "D\xC3\xBCnya" : "Yerel";
  Cand cand[8] = {
      {"\xE2\x96\xB6", "OYNATILIYOR", tone_u32(Tone::Bg0), tone_u32(Tone::Bg0), tone_u32(Tone::Accent, 0.92f), 0, 0, -1, 0, 0, false, false},
      {nullptr, proj_txt, 0, text, pill_bg, chip_border, 2, 0, 0, 0, false, false},
      {nullptr, mode_txt, 0, text, pill_bg, chip_border, 4, 1, 0, 0, false, false},
      {kOpIcon[op], kOpName[op], accent, text, pill_bg, pill_border, 1, -1, 0, 0, false, false},
      {nullptr, space_txt, 0, text, pill_bg, chip_border, 3, 2, 0, 0, false, false},
      {nullptr, info.shading, 0, dim, pill_bg, pill_border, 5, -1, 0, 0, false, false},
      {nullptr, stats, 0, dim, pill_bg, pill_border, 7, -1, 0, 0, false, false},
      {"\xE2\x97\x87", "gizmolar kapal\xC4\xB1", dim, dim, pill_bg, pill_border, 6, -1, 0, 0, false, false},
  };
  const int n_cand = 8;
  const float top_limit = lay.gizmo ? (gizmo_c.x - gizmo_box * 0.5f - gap) : (rmax.x - pad);
  float top_avail = top_limit - (rmin.x + pad);
  const float top_y = rmin.y + pad;
  if (r.h >= ps.h() + 2.0f * pad) {
    float total = 0;
    int n_keep = 0;
    for (int i = 0; i < n_cand; i++) {
      Cand &c = cand[i];
      const bool wanted = (i != 0 || info.playing) && (i != 5 || (info.shading && *info.shading)) && (i != 6 || stats[0]) &&
                          (i != 7 || !info.gizmos_visible);
      if (!wanted) continue;
      c.w = pill_width(ps, c.icon, c.text);
      c.keep = true;
      total += c.w + (n_keep ? gap : 0.0f);
      n_keep++;
    }
    while (n_keep > 0 && total > top_avail) { // en dusuk oncelikli hapi at
      int worst = -1;
      for (int i = 0; i < n_cand; i++)
        if (cand[i].keep && (worst < 0 || cand[i].drop_rank > cand[worst].drop_rank)) worst = i;
      cand[worst].keep = false;
      n_keep--;
      total -= cand[worst].w + (n_keep ? gap : 0.0f);
    }
    // Once KONUM (cizim degil): ciplerin dikdortgeni etkilesim icin gerekli ve
    // "ustunde" hali cizimi degistiriyor, yani hit-test cizimden ONCE olmali.
    float x = rmin.x + pad;
    for (int i = 0; i < n_cand; i++) {
      if (!cand[i].keep) continue;
      cand[i].x = x;
      x += cand[i].w + gap;
    }
    OverlayRect *const chip_out[3] = {&lay.chip_proj, &lay.chip_mode, &lay.chip_space};
    static const char *const kChipId[3] = {"##kaplama_izdusum", "##kaplama_kip", "##kaplama_uzay"};
    static const char *const kChipTip[3] = {"\xC4\xB0zd\xC3\xBC\xC5\x9F\xC3\xBCm: perspektif / ortografik", "Kamera: y\xC3\xB6r\xC3\xBCnge / u\xC3\xA7u\xC5\x9F",
                                            "Gizmo ekseni: d\xC3\xBCnya / yerel"};
    for (int i = 0; i < n_cand; i++) {
      Cand &c = cand[i];
      if (!c.keep || c.chip < 0) continue;
      const OverlayRect cr{c.x, top_y, c.w, ps.h()};
      *chip_out[c.chip] = cr;
      if (!cr.contains(mouse.x, mouse.y)) continue;
      bool clicked = false;
      if (!hot_item(kChipId[c.chip], ImVec2(cr.x, cr.y), ImVec2(cr.w, cr.h), &clicked)) continue;
      c.hot = true;
      res.consumed_mouse = true;
      ImGui::SetTooltip("%s", kChipTip[c.chip]);
      if (clicked) {
        if (c.chip == 0) res.ortho_toggled = true;
        else if (c.chip == 1) res.mode_toggled = true;
        else res.gizmo_space_toggled = true;
      }
    }
    for (int i = 0; i < n_cand; i++) {
      const Cand &c = cand[i];
      if (!c.keep) continue;
      pill_draw(dl, ps, ImVec2(c.x, top_y), c.w, c.icon, c.icon_col, c.text, c.fg, c.hot ? chip_bg_hot : c.bg, c.hot ? chip_border_hot : c.border);
      lay.pills++;
    }
    lay.top_row = lay.pills > 0;
  }

  if (lay.gizmo) draw_axis_gizmo(dl, gizmo_c, R, end_r, fs, info.view, axis_hover);

  // --- c) + e) Alt satir: kamera hapi solda, ipucu sagda. Ust satirin ve
  // gostergenin ALTINDA kalmali (ust uste binme yok); sigmayan dusurulur.
  {
    const float row_y = rmax.y - pad - ps.h();
    float top_used = rmin.y + pad;
    if (lay.top_row) top_used += ps.h();
    if (lay.gizmo) top_used = rmin.y + pad + gizmo_box;
    if (row_y >= top_used + gap) {
      char cam[128];
      std::snprintf(cam, sizeof cam, "kamera %.1f, %.1f, %.1f \xC2\xB7 hedef %.1f, %.1f, %.1f", (double)info.cam_eye[0], (double)info.cam_eye[1],
                    (double)info.cam_eye[2], (double)info.cam_target[0], (double)info.cam_target[1], (double)info.cam_target[2]);
      const float cam_w = pill_width(ps, nullptr, cam);
      // Ipucu da KENDI hapinin icinde: cıplak metin acik zeminde (dama tahtasi
      // zemin, gokyuzu) golgeye ragmen okunmuyordu — kamera hapiyla ayni zemin.
      const float hint_w = info.hint && *info.hint ? pill_width(ps, nullptr, info.hint) : 0.0f;
      const float avail = r.w - 2.0f * pad;
      const bool both = hint_w > 0.0f && cam_w + gap + hint_w <= avail;
      const bool cam_only = !both && cam_w <= avail;
      const bool hint_only = !both && !cam_only && hint_w > 0.0f && hint_w <= avail;
      if (both || cam_only) {
        pill_draw(dl, ps, ImVec2(rmin.x + pad, row_y), cam_w, nullptr, 0, cam, dim, pill_bg, pill_border);
        lay.camera = true;
      }
      if (both || hint_only) {
        pill_draw(dl, ps, ImVec2(rmax.x - pad - hint_w, row_y), hint_w, nullptr, 0, info.hint, dim, pill_bg, pill_border);
        lay.hint = true;
      }
    }
  }

  // --- f) Kutu (marquee) secim. KENDI OGESINI EKLEMEZ: altindaki ImGui::Image'in
  // tiklamasini kullanir (bir oge eklenseydi tek tikla secim olmezdi). Surukleme
  // durumu g_box'ta; sol tus birakilinca HER HALUKARDA sifirlanir, yani kareler
  // arasi bulasik surukleme kalmaz.
  {
    const bool inside = mouse.x >= rmin.x && mouse.x < rmax.x && mouse.y >= rmin.y && mouse.y < rmax.y;
    const bool down = ImGui::IsMouseDown(ImGuiMouseButton_Left);
    auto span = [](const BoxDrag &b, float *o) {
      o[0] = b.x0 < b.x1 ? b.x0 : b.x1;
      o[1] = b.y0 < b.y1 ? b.y0 : b.y1;
      o[2] = b.x0 < b.x1 ? b.x1 : b.x0;
      o[3] = b.y0 < b.y1 ? b.y1 : b.y0;
    };
    auto big_enough = [](const BoxDrag &b) {
      const float w = b.x1 - b.x0 >= 0 ? b.x1 - b.x0 : b.x0 - b.x1, h = b.y1 - b.y0 >= 0 ? b.y1 - b.y0 : b.y0 - b.y1;
      return w >= kBoxMin || h >= kBoxMin;
    };
    if (!down) {
      // IsMouseReleased sart: yalniz GERCEKTEN bu karede birakilan bir surukleme
      // secime doner. Yoksa panel bir kare cizilmeden kalip geri geldiginde
      // (ya da baska bir baglamda) bayat bir g_box "birakilmis" gibi sayilir.
      if (g_box.active && big_enough(g_box) && ImGui::IsMouseReleased(ImGuiMouseButton_Left)) { // birakildi: secim BU KARE uygulanir
        res.box_done = true;
        span(g_box, res.box);
      }
      g_box.active = false;
    } else if (!g_box.active) {
      // Baslangic: goruntunun ustunde, kaplamanin bir ogesi fareyi ALMAMISKEN.
      if (inside && info.hovered && !res.consumed_mouse && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        g_box.active = true;
        g_box.x0 = g_box.x1 = mouse.x;
        g_box.y0 = g_box.y1 = mouse.y;
      }
    } else {
      g_box.x1 = mouse.x; // kirpilmaz: goruntunun disina surukleyip birakmak gecerli
      g_box.y1 = mouse.y;
      if (big_enough(g_box)) {
        res.box_active = true;
        span(g_box, res.box);
        // Yari saydam dolgu + 1 px kenarlik; PushClipRect sayesinde r'nin disina
        // tasmaz (kapi bunu da olcer).
        dl->AddRectFilled(ImVec2(res.box[0], res.box[1]), ImVec2(res.box[2], res.box[3]), tone_u32(Tone::Accent, 0.16f));
        dl->AddRect(ImVec2(res.box[0], res.box[1]), ImVec2(res.box[2], res.box[3]), tone_u32(Tone::AccentHi, 0.95f), 0.0f, 1.0f);
        lay.box = true;
      }
    }
  }

  // --- d) Odak / ustunde cercevesi: 1 px IC kenarlik (Unreal odak cizgisi).
  if (info.focused || info.hovered) {
    // AddRect 0.5 px kaydirmayi KENDISI yapar (kenar orta cizgisi piksel
    // merkezine gelir); ikinci bir 0.5 eklenirse cizgi iki piksele yayilir ve
    // "yalniz 1 px serit degisir" kapisi duser (olculdu: ic fark 1946).
    dl->AddRect(rmin, rmax, tone_u32(Tone::Accent, info.focused ? 0.95f : 0.45f), 0.0f, 1.0f);
    lay.border = true;
  }
  dl->PopClipRect();
  if (out_layout) *out_layout = lay;
  if (out_res) *out_res = res;
}

uint32_t viewport_box_select(const Mat4 &view_proj, const content::SceneBounds *bounds, uint32_t n, const ViewportRect &view, float x0, float y0,
                             float x1, float y1, bool require_full_containment, int32_t *out, uint32_t cap) {
  if (!bounds || n == 0 || !(view.w > 0.0f) || !(view.h > 0.0f)) return 0;
  const float rx0 = x0 < x1 ? x0 : x1, rx1 = x0 < x1 ? x1 : x0;
  const float ry0 = y0 < y1 ? y0 : y1, ry1 = y0 < y1 ? y1 : y0;
  uint32_t written = 0;
  for (uint32_t i = 0; i < n; i++) {
    const content::SceneBounds &b = bounds[i];
    float bx0 = 3.4e38f, by0 = 3.4e38f, bx1 = -3.4e38f, by1 = -3.4e38f;
    uint32_t front = 0;
    for (int k = 0; k < 8; k++) {
      const Vec4 c = view_proj * Vec4{(k & 1) ? b.hi.x : b.lo.x, (k & 2) ? b.hi.y : b.lo.y, (k & 4) ? b.hi.z : b.lo.z, 1.0f};
      // ⚠ w <= 0: kose kameranin ARKASINDA (ya da tam duzleminde). Bolmek
      // izdusumu kokten aynalar ve arkadaki nesne dikdortgenin icine dusmus
      // gibi gorunur — klasik "kutu secince sahnenin yarisi secildi" hatasi.
      if (!(c.w > 1e-6f)) continue;
      front++;
      const float sx = view.x + (c.x / c.w * 0.5f + 0.5f) * view.w;
      const float sy = view.y + (c.y / c.w * 0.5f + 0.5f) * view.h; // Vulkan: NDC y zaten ASAGI
      if (sx < bx0) bx0 = sx;
      if (sx > bx1) bx1 = sx;
      if (sy < by0) by0 = sy;
      if (sy > by1) by1 = sy;
    }
    if (front == 0) continue; // tamami arkada: ASLA secilmez
    // Yakin duzlemi kesen kutu (0 < front < 8) TAM ICERME'yi saglayamaz:
    // gorunmeyen parcasinin dikdortgenin icinde oldugu iddia edilemez.
    const bool hit = require_full_containment ? (front == 8 && bx0 >= rx0 && bx1 <= rx1 && by0 >= ry0 && by1 <= ry1)
                                              : (bx0 <= rx1 && bx1 >= rx0 && by0 <= ry1 && by1 >= ry0);
    if (!hit) continue;
    if (out) {
      if (written >= cap) break; // tampon doldu: kalanlar ATLANIR (sozlesme)
      out[written] = (int32_t)i;
    }
    written++;
  }
  return written;
}

uint32_t overlay_ellipsize_left(const char *s, float max_w, char *out, uint32_t cap) {
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
  // Once '/' sinirlari: sigan EN UZUN bilesen kuyrugu ("…/tests/assets").
  uint32_t k = n;
  for (uint32_t i = 0; i < n; i++)
    if (s[i] == '/' && dots_w + ImGui::CalcTextSize(s + i).x <= max_w) { k = i; break; }
  if (k == n) { // hicbir bilesen sinirinda sigmiyor: karakter karakter (UTF-8 sinirinda)
    uint32_t last = n; // son karakterin baslangici: en az o kalir ("…s"), yalniz "…" degil
    while (last > 0 && ((unsigned char)s[--last] & 0xC0) == 0x80) {}
    k = 0;
    while (k < last) {
      k++;
      while (k < last && ((unsigned char)s[k] & 0xC0) == 0x80) k++;
      if (dots_w + ImGui::CalcTextSize(s + k).x <= max_w) break;
    }
  }
  uint32_t m = n - k;
  if (m + 3 >= cap) { // tampona sigmayan kuyruk: bastan kirp (UTF-8 sinirinda)
    m = cap > 4 ? cap - 4 : 0;
    k = n - m;
    while (k < n && ((unsigned char)s[k] & 0xC0) == 0x80) { k++; m--; }
  }
  std::memcpy(out, kDots, 3);
  std::memcpy(out + 3, s + k, m);
  out[3 + m] = 0;
  return 3 + m;
}

// ===========================================================================
// Kaynaklar paneli
// ===========================================================================
namespace {
constexpr float kTileMin = 64.0f, kTileMax = 192.0f;

// Karo adi: EN FAZLA iki satir, kucuk yazi (px). Ilk satir sigan en uzun
// on-ek; mumkunse bir ayiracta ('_' '-') kirilir -- NOKTA'da degil, uzanti
// adindan kopmasin. Ikinci satir sagdan "…"
// ile kirpik. Donus: ad kirpildi mi (cagiran ipucunda tamamini gosterir).
bool tile_name(ImDrawList *dl, float px, const char *name, ImVec2 pos, float w, ImU32 col) {
  ImFont *font = ImGui::GetFont();
  const uint32_t n = (uint32_t)std::strlen(name);
  const float full = font->CalcTextSizeA(px, 3.4e38f, 0.0f, name).x;
  if (full <= w) { // tek satir, ortali
    dl->AddText(font, px, ImVec2(pos.x + (w - full) * 0.5f, pos.y), col, name);
    return false;
  }
  // Ilk satira sigan en uzun on-ek (UTF-8 sinirinda), sonra ayirac tercihi.
  uint32_t fit = 0;
  for (uint32_t k = 1; k <= n; k++) {
    if (k < n && ((unsigned char)name[k] & 0xC0) == 0x80) continue;
    if (font->CalcTextSizeA(px, 3.4e38f, 0.0f, name, name + k).x > w) break;
    fit = k;
  }
  // Kirilma NOKTA'da YAPILMAZ: nokta uzanti ayiracidir ve orada bolmek
  // "checker_cube." / "gltf" gibi adi uzantisindan KOPARIR -- ekran
  // goruntusunde gorulen hata buydu. Dosya tarayicilari uzantiyi hicbir
  // zaman ayirmaz. Yalniz '_' ve '-' tercih edilir; ikisi de yoksa `fit`
  // noktasindan sert kirilir (uzanti ikinci satirda adin devamiyla kalir).
  uint32_t brk = fit;
  for (uint32_t k = fit; k > fit / 2; k--)
    if (name[k - 1] == '_' || name[k - 1] == '-') { brk = k; break; }
  if (brk == 0) brk = fit > 0 ? fit : 1;
  const float w1 = font->CalcTextSizeA(px, 3.4e38f, 0.0f, name, name + brk).x;
  dl->AddText(font, px, ImVec2(pos.x + (w - w1) * 0.5f, pos.y), col, name, name + brk);
  // Ikinci satir: kalan, sagdan kirpik (editor_ellipsize gecerli yazi boyutunu
  // kullanir; PushFont ile bu boyuta gecilir).
  // PushFont'un boyutu OLCEKSIZ tabandir (GetFontSize = taban * FontScaleMain *
  // FontScaleDpi); px zaten olcekli, geri bolunmezse DPI'da iki kez buyur ve
  // olcum cizimden buyuk cikar (fazla kirpma).
  const ImGuiStyle &sty = ImGui::GetStyle();
  const float scale = sty.FontScaleMain * sty.FontScaleDpi;
  ImGui::PushFont(nullptr, scale > 0.0f ? px / scale : px);
  char rest[content::kScenePathLen + 4];
  editor_ellipsize(name + brk, w, rest, sizeof rest);
  const float w2 = ImGui::CalcTextSize(rest).x;
  ImGui::PopFont();
  dl->AddText(font, px, ImVec2(pos.x + (w - w2) * 0.5f, pos.y + px + 2.0f), col, rest);
  return std::strcmp(rest, name + brk) != 0;
}

// Karo/satir uzerindeki "sahneye ekle" dugmesi. Karo InvisibleButton'u
// SetNextItemAllowOverlap ile cizildigi icin bu dugme onun ustunde tiklanabilir.
bool add_button(ImVec2 pos, float size) {
  ImGui::SetCursorScreenPos(pos);
  ImGui::PushStyleColor(ImGuiCol_Button, tone_u32(Tone::Accent, 0.85f));
  ImGui::PushStyleColor(ImGuiCol_ButtonHovered, tone_u32(Tone::AccentHi));
  ImGui::PushStyleColor(ImGuiCol_ButtonActive, tone_u32(Tone::AccentLo));
  ImGui::PushStyleColor(ImGuiCol_Text, tone_u32(Tone::Bg0));
  ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, 0));
  const bool pressed = ImGui::Button("\xE2\x9C\x9A##ekle", ImVec2(size, size)); // ✚
  ImGui::PopStyleVar();
  ImGui::PopStyleColor(4);
  if (ImGui::IsItemHovered()) ImGui::SetTooltip("Sahneye ekle");
  return pressed;
}

void badge(ImDrawList *dl, ImVec2 p, const char *txt, float font_px, float pad_x, float rounding, ImU32 fg, ImU32 bg, float *out_w) {
  const ImVec2 ts = ImGui::GetFont()->CalcTextSizeA(font_px, 3.4e38f, 0.0f, txt);
  const float w = ts.x + 2.0f * pad_x, h = ts.y + 2.0f;
  dl->AddRectFilled(p, ImVec2(p.x + w, p.y + h), bg, rounding);
  dl->AddText(ImGui::GetFont(), font_px, ImVec2(p.x + pad_x, p.y + 1.0f), fg, txt);
  if (out_w) *out_w = w;
}

void empty_state(const char *msg) {
  const ImVec2 avail = ImGui::GetContentRegionAvail();
  const ImVec2 ts = ImGui::CalcTextSize(msg);
  const ImVec2 p = ImGui::GetCursorScreenPos();
  const float x = p.x + (avail.x - ts.x) * 0.5f, y = p.y + (avail.y - ts.y) * 0.5f;
  ImGui::GetWindowDrawList()->AddText(ImVec2(x > p.x ? x : p.x, y > p.y ? y : p.y), tone_u32(Tone::TextDim), msg);
  ImGui::Dummy(ImVec2(avail.x, avail.y > ts.y ? avail.y : ts.y));
}
} // namespace

void assets_panel(AssetsView &v, const char *dir, const AssetFile *files, uint32_t file_count, const char (*scene_assets)[content::kScenePathLen],
                  const bool *loaded, uint32_t scene_asset_count, AssetsAction *out, AssetsLayout *out_layout) {
  if (out) *out = AssetsAction{};
  AssetsLayout lay;
  if (out_layout) *out_layout = lay;
  if (!ImGui::GetCurrentContext()) return;
  if (!(v.tile >= kTileMin)) v.tile = kTileMin;
  if (v.tile > kTileMax) v.tile = kTileMax;
  const ImGuiStyle &st = ImGui::GetStyle();
  const float fs = ImGui::GetFontSize();
  const float frame_h = ImGui::GetFrameHeight();
  const ImU32 text = tone_u32(Tone::Text), dim = tone_u32(Tone::TextDim), accent = tone_u32(Tone::Accent);

  // --- Baslik satiri: yol (soldan kirpik) | ↻ | ▦ ☰ | karo | ara ---------------
  {
    const float avail_w = ImGui::GetContentRegionAvail().x;
    const float btn_w = frame_h; // kare simge dugmeleri
    const float slider_w = ImTrunc(fs * 5.0f), search_w = ImTrunc(fs * 8.5f);
    const float cluster_w = btn_w * 3.0f + slider_w + search_w + st.ItemSpacing.x * 4.0f + st.ItemInnerSpacing.x;
    const bool two_rows = avail_w < cluster_w + fs * 6.0f; // dar panel: yol kendi satirinda
    const float path_w = two_rows ? avail_w - btn_w - st.ItemSpacing.x : avail_w - cluster_w - st.ItemSpacing.x;
    char pbuf[content::kScenePathLen + 8];
    overlay_ellipsize_left(dir ? dir : "", path_w > fs ? path_w : fs, pbuf, sizeof pbuf);
    ImGui::AlignTextToFramePadding();
    ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(dim), "%s", pbuf);
    if (ImGui::IsItemHovered() && dir && *dir) ImGui::SetTooltip("%s", dir);
    if (!two_rows) ImGui::SameLine(avail_w - cluster_w + st.ItemSpacing.x);
    else ImGui::SameLine(avail_w - btn_w + st.ItemSpacing.x);
    if (ImGui::Button("\xE2\x86\xBB##yenile", ImVec2(btn_w, btn_w)) && out) out->refresh = true; // ↻
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Dizini yeniden tara");
    if (!two_rows) ImGui::SameLine(0.0f, st.ItemSpacing.x);
    // Izgara / liste: etkin olan vurgulu.
    for (int mode = 0; mode < 2; mode++) {
      const bool on = (mode == 0) == v.grid;
      ImGui::PushStyleColor(ImGuiCol_Button, on ? tone_u32(Tone::Accent, 0.35f) : tone_u32(Tone::Bg3));
      ImGui::PushStyleColor(ImGuiCol_Text, on ? tone_u32(Tone::AccentHi) : text);
      if (ImGui::Button(mode == 0 ? "\xE2\x96\xA6##izgara" : "\xE2\x98\xB0##liste", ImVec2(btn_w, btn_w))) v.grid = (mode == 0); // ▦ ☰
      ImGui::PopStyleColor(2);
      if (ImGui::IsItemHovered()) ImGui::SetTooltip(mode == 0 ? "Karo izgarasi" : "Liste");
      ImGui::SameLine(0.0f, mode == 0 ? st.ItemInnerSpacing.x : st.ItemSpacing.x);
    }
    ImGui::SetNextItemWidth(slider_w);
    ImGui::BeginDisabled(!v.grid);
    ImGui::SliderFloat("##karo", &v.tile, kTileMin, kTileMax, "%.0f px", ImGuiSliderFlags_AlwaysClamp);
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Karo boyutu");
    ImGui::SameLine(0.0f, st.ItemSpacing.x);
    ImGui::SetNextItemWidth(search_w);
    ImGui::InputTextWithHint("##ara", "ara\xE2\x80\xA6", v.filter, sizeof v.filter);
  }

  // --- Govde ------------------------------------------------------------------
  // GENIS panel (alt dok, kisa ve genis): Unity Project penceresi gibi IKI BOLME
  // yan yana — solda "Sahnedeki kaynaklar", sagda karo izgarasi; boylece kisa
  // panelde izgaraya bir karo satirindan az yer kalmaz (olculdu: 250 px'lik
  // panelde dikey yigin izgaraya 100 px birakiyordu). DAR panel: dikey yigin,
  // altta katlanabilir bolum (v.scene_open), ikisi de kendi cocugunda kaydirir.
  const float row_h = frame_h;
  const bool side_by_side = ImGui::GetContentRegionAvail().x >= fs * 34.0f;
  const ImU32 pane_bg = tone_u32(Tone::Bg0, 0.35f);

  auto draw_scene_rows = [&](void) {
    ImDrawList *rdl = ImGui::GetWindowDrawList();
    if (scene_asset_count == 0) ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(dim), "  sahnede kaynak yok");
    for (uint32_t i = 0; i < scene_asset_count; i++) {
      const bool ok = loaded && loaded[i];
      const ImVec2 p = ImGui::GetCursorScreenPos();
      const float w = ImGui::GetContentRegionAvail().x;
      const float cy = p.y + row_h * 0.5f;
      rdl->AddCircleFilled(ImVec2(p.x + st.FramePadding.x + fs * 0.25f, cy), fs * 0.22f, tone_u32(ok ? Tone::Ok : Tone::Err), 16);
      char idx[16];
      std::snprintf(idx, sizeof idx, "%u", i);
      float x = p.x + st.FramePadding.x + fs * 0.8f;
      rdl->AddText(ImVec2(x, p.y + st.FramePadding.y), dim, idx);
      x += ImGui::CalcTextSize("00").x + st.ItemInnerSpacing.x;
      static const char kFail[] = "Y\xC3\x9CKLENEMED\xC4\xB0"; // YÜKLENEMEDİ
      // Dar satirda (yan yana duzenin sol bolmesi) etiket ada yer birakmiyordu
      // ("yok_…"): 16 em'den darsa etiket dusur — kirmizi nokta + kirmizi ad +
      // ipucu durumu zaten soyler; ad okunur kalir.
      const bool label = !ok && w >= fs * 16.0f;
      const float fail_w = label ? ImGui::CalcTextSize(kFail).x + st.ItemInnerSpacing.x : 0.0f;
      char nb[content::kScenePathLen + 4];
      editor_ellipsize(scene_assets ? scene_assets[i] : "", (p.x + w - st.FramePadding.x - fail_w) - x, nb, sizeof nb);
      rdl->AddText(ImVec2(x, p.y + st.FramePadding.y), ok ? text : tone_u32(Tone::Err, 0.85f), nb);
      if (label) rdl->AddText(ImVec2(p.x + w - st.FramePadding.x - ImGui::CalcTextSize(kFail).x, p.y + st.FramePadding.y), tone_u32(Tone::Err), kFail);
      ImGui::Dummy(ImVec2(w, row_h));
      if (ImGui::IsItemHovered() && scene_assets)
        ImGui::SetTooltip("%s\n%s", scene_assets[i], ok ? "y\xC3\xBCklendi" : "Y\xC3\x9CKLENEMED\xC4\xB0: dosya okunamad\xC4\xB1 / bulunamad\xC4\xB1");
    }
  };

  // Dosya karolari / satirlari: cagiran BeginChild icindedir.
  auto draw_files = [&](void) {
    // COCUGUN cizim listesi: kirpma ve kaydirma ona ait (dis pencerenin listesine
    // cizilseydi karolar cocugun disina tasar, kaydirma kirpmazdi).
    ImDrawList *cdl = ImGui::GetWindowDrawList();
    const float pad = st.WindowPadding.x;
    ImGui::SetCursorPos(ImVec2(ImGui::GetCursorPosX() + pad * 0.5f, ImGui::GetCursorPosY() + pad * 0.5f));
    lay.body_x = ImGui::GetWindowPos().x;
    lay.body_y = ImGui::GetWindowPos().y;
    lay.body_w = ImGui::GetWindowSize().x;
    lay.body_h = ImGui::GetWindowSize().y;
    uint32_t shown = 0;
    if (file_count == 0) {
      empty_state("Bu dizinde .gltf/.glb yok");
      lay.empty = true;
    } else if (v.grid) {
      const float name_px = ImTrunc(fs * 0.85f); // karo adi: kucuk, iki satir
      const float tw = ImTrunc(v.tile), th = ImTrunc(tw + name_px * 2.0f + fs * 0.6f), sp = st.ItemSpacing.x;
      const float avail_w = ImGui::GetContentRegionAvail().x - pad * 0.5f;
      int cols = (int)((avail_w + sp) / (tw + sp));
      if (cols < 1) cols = 1;
      const ImVec2 origin = ImGui::GetCursorScreenPos();
      lay.origin_x = origin.x; lay.origin_y = origin.y; lay.tile_w = tw; lay.tile_h = th; lay.gap = sp; lay.cols = cols;
      const float glyph_px = ImTrunc(tw * 0.42f);
      const float badge_px = ImTrunc(fs * 0.78f);
      const float btn = ImTrunc(fs * 1.25f);
      for (uint32_t i = 0; i < file_count; i++) {
        const AssetFile &f = files[i];
        if (!contains_ci(f.name, v.filter)) continue;
        const int col = (int)(shown % (uint32_t)cols), row = (int)(shown / (uint32_t)cols);
        const ImVec2 p(origin.x + (float)col * (tw + sp), origin.y + (float)row * (th + sp));
        ImGui::SetCursorScreenPos(p);
        ImGui::PushID((int)i);
        ImGui::SetNextItemAllowOverlap();
        ImGui::InvisibleButton("##karo", ImVec2(tw, th));
        const bool hov = ImGui::IsItemHovered();
        const bool dbl = hov && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);
        // Kart: zemin (ustundeyken bir ton acik), sahnedeyse vurgu kenarligi.
        const ImVec2 q(p.x + tw, p.y + th);
        cdl->AddRectFilled(p, q, hov ? tone_u32(Tone::Bg3) : tone_u32(Tone::Bg2), st.FrameRounding);
        cdl->AddRect(p, q, f.in_scene ? tone_u32(Tone::Accent, 0.55f) : tone_u32(Tone::Line, 0.6f), st.FrameRounding, 1.0f);
        // Kucuk resim yerine simge (◆): glTF'nin on izlemesi yok — durust bos.
        static const char kDiamond[] = "\xE2\x97\x86";
        const ImVec2 gs = ImGui::GetFont()->CalcTextSizeA(glyph_px, 3.4e38f, 0.0f, kDiamond);
        const float thumb_h = tw; // ust kare
        cdl->AddText(ImGui::GetFont(), glyph_px, ImVec2(p.x + (tw - gs.x) * 0.5f, p.y + (thumb_h - gs.y) * 0.5f),
                     f.in_scene ? tone_u32(Tone::Accent, 0.9f) : tone_u32(Tone::Bg4), kDiamond);
        // Ad: kucuk yazi, EN FAZLA IKI SATIR — ilk satir mumkunse '_' '-' '.'
        // sinirinda kirilir, ikinci satir sagdan kirpik; tam ad ipucunda.
        const bool cut = tile_name(cdl, name_px, f.name, ImVec2(p.x + st.FramePadding.x * 0.5f, p.y + thumb_h + fs * 0.2f), tw - st.FramePadding.x, text);
        if (hov && cut) ImGui::SetTooltip("%s", f.name);
        
        // Sürükle-Bırak (Drag & Drop) Kaynağı
        if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {
          ImGui::SetDragDropPayload("ASSET_FILE", f.name, std::strlen(f.name) + 1);
          ImGui::Text("Sahneye Ekle:\n%s", f.name);
          ImGui::EndDragDropSource();
        }
        
        if (f.in_scene) badge(cdl, ImVec2(p.x + 4.0f, p.y + 4.0f), "sahnede", badge_px, 5.0f, st.FrameRounding, tone_u32(Tone::AccentHi), tone_u32(Tone::Accent, 0.25f), nullptr);
        bool add = dbl;
        if (hov || ImGui::IsItemActive()) add |= add_button(ImVec2(q.x - btn - 4.0f, p.y + 4.0f), btn);
        if (add && out) out->add_index = (int)i;
        ImGui::PopID();
        shown++;
      }
      const uint32_t rows = shown ? (shown + (uint32_t)cols - 1) / (uint32_t)cols : 0;
      ImGui::SetCursorScreenPos(ImVec2(origin.x, origin.y + (float)rows * (th + sp)));
      ImGui::Dummy(ImVec2(1.0f, pad * 0.5f)); // icerik sinirini kapat (kaydirma dogru olcsun)
    } else {
      const float btn = ImTrunc(fs * 1.1f);
      const float badge_px = ImTrunc(fs * 0.78f);
      const ImVec2 origin = ImGui::GetCursorScreenPos();
      lay.origin_x = origin.x; lay.origin_y = origin.y; lay.tile_w = ImGui::GetContentRegionAvail().x - pad * 0.5f; lay.tile_h = row_h;
      lay.gap = st.ItemSpacing.y * 0.5f; lay.cols = 1;
      for (uint32_t i = 0; i < file_count; i++) {
        const AssetFile &f = files[i];
        if (!contains_ci(f.name, v.filter)) continue;
        ImGui::PushID((int)i);
        const ImVec2 p = ImGui::GetCursorScreenPos();
        const float w = ImGui::GetContentRegionAvail().x - pad * 0.5f;
        ImGui::SetNextItemAllowOverlap();
        ImGui::InvisibleButton("##satir", ImVec2(w, row_h));
        const bool hov = ImGui::IsItemHovered();
        const bool dbl = hov && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);
        const ImVec2 q(p.x + w, p.y + row_h);
        if (hov) cdl->AddRectFilled(p, q, tone_u32(Tone::Bg3), st.FrameRounding);
        else if (shown & 1) cdl->AddRectFilled(p, q, tone_u32(Tone::White, 0.025f), st.FrameRounding);
        float x = p.x + st.FramePadding.x;
        cdl->AddText(ImVec2(x, p.y + st.FramePadding.y), f.in_scene ? accent : tone_u32(Tone::Bg4), "\xE2\x97\x86"); // ◆
        x += fs + st.ItemInnerSpacing.x;
        float right = q.x - st.FramePadding.x - btn - st.ItemInnerSpacing.x;
        if (f.in_scene) {
          const ImVec2 bs = ImGui::GetFont()->CalcTextSizeA(badge_px, 3.4e38f, 0.0f, "sahnede");
          const float badge_w = bs.x + 10.0f;
          badge(cdl, ImVec2(right - badge_w, p.y + (row_h - bs.y - 2.0f) * 0.5f), "sahnede", badge_px, 5.0f, st.FrameRounding, tone_u32(Tone::AccentHi),
                tone_u32(Tone::Accent, 0.25f), nullptr);
          right -= badge_w + st.ItemInnerSpacing.x;
        }
        char nb[content::kScenePathLen + 4];
        editor_ellipsize(f.name, right - x, nb, sizeof nb);
        cdl->AddText(ImVec2(x, p.y + st.FramePadding.y), text, nb);
        if (hov && std::strcmp(nb, f.name) != 0) ImGui::SetTooltip("%s", f.name);
        
        // Sürükle-Bırak (Drag & Drop) Kaynağı (Liste Görünümü İçin)
        if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {
          ImGui::SetDragDropPayload("ASSET_FILE", f.name, std::strlen(f.name) + 1);
          ImGui::Text("Sahneye Ekle:\n%s", f.name);
          ImGui::EndDragDropSource();
        }
        
        bool add = dbl;
        if (hov || ImGui::IsItemActive()) add |= add_button(ImVec2(q.x - st.FramePadding.x - btn, p.y + (row_h - btn) * 0.5f), btn);
        if (add && out) out->add_index = (int)i;
        ImGui::SetCursorScreenPos(ImVec2(p.x, q.y + st.ItemSpacing.y * 0.5f));
        ImGui::PopID();
        shown++;
      }
      ImGui::Dummy(ImVec2(1.0f, pad * 0.5f));
    }
    if (file_count && !shown) {
      char msg[128];
      std::snprintf(msg, sizeof msg, "E\xC5\x9Fle\xC5\x9F""en kaynak yok: %s", v.filter);
      empty_state(msg);
      lay.empty = true;
    }
    lay.shown = shown;
  };

  char hdr[64];
  std::snprintf(hdr, sizeof hdr, "Sahnedeki kaynaklar (%u)", scene_asset_count);
  ImGui::PushStyleColor(ImGuiCol_ChildBg, pane_bg);
  if (side_by_side) {
    // Sol bolme: sabit genislik (panelin ~%30'u, 14..20 em), tam yukseklik.
    float left_w = ImTrunc(ImGui::GetContentRegionAvail().x * 0.30f);
    if (left_w < fs * 14.0f) left_w = ImTrunc(fs * 14.0f);
    if (left_w > fs * 20.0f) left_w = ImTrunc(fs * 20.0f);
    if (ImGui::BeginChild("##sahne_bolme", ImVec2(left_w, 0), ImGuiChildFlags_Borders, ImGuiWindowFlags_None)) {
      ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(dim), "%s", hdr);
      ImGui::Separator();
      draw_scene_rows();
    }
    ImGui::EndChild();
    ImGui::SameLine();
    if (ImGui::BeginChild("##kaynak_govde", ImVec2(0, 0), ImGuiChildFlags_Borders, ImGuiWindowFlags_None)) draw_files();
    ImGui::EndChild();
  } else {
    const uint32_t scene_rows_shown = scene_asset_count < 4 ? scene_asset_count : 4;
    const float scene_h = frame_h + st.ItemSpacing.y + (v.scene_open ? (float)scene_rows_shown * (row_h + st.ItemSpacing.y) + st.ItemSpacing.y : 0.0f);
    float body_h = ImGui::GetContentRegionAvail().y - scene_h - st.ItemSpacing.y;
    if (body_h < row_h * 2.0f) body_h = row_h * 2.0f; // dar pencere: dis pencere kaydirir, govde silinmez
    if (ImGui::BeginChild("##kaynak_govde", ImVec2(0, body_h), ImGuiChildFlags_Borders, ImGuiWindowFlags_None)) draw_files();
    ImGui::EndChild();
    // Altta katlanabilir bolum; acikken en fazla 4 satir gosterir, gerisi kaydirir.
    ImGui::PushStyleColor(ImGuiCol_Header, tone_u32(Tone::Bg2));
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, tone_u32(Tone::Bg3));
    ImGui::PushStyleColor(ImGuiCol_HeaderActive, tone_u32(Tone::Bg4));
    ImGui::SetNextItemOpen(v.scene_open);
    char hid[96];
    std::snprintf(hid, sizeof hid, "%s##sahne_kaynak", hdr);
    v.scene_open = ImGui::CollapsingHeader(hid);
    ImGui::PopStyleColor(3);
    if (v.scene_open) {
      const float list_h = (float)scene_rows_shown * (row_h + st.ItemSpacing.y) + st.ItemSpacing.y;
      if (ImGui::BeginChild("##sahne_kaynak_liste", ImVec2(0, list_h > row_h ? list_h : row_h), ImGuiChildFlags_None, ImGuiWindowFlags_None))
        draw_scene_rows();
      ImGui::EndChild();
    }
  }
  ImGui::PopStyleColor();
  if (out_layout) *out_layout = lay;
}

} // namespace tulpar::engine::app
