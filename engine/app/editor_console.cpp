#include "app/editor_console.hpp"

#include <cstdio>
#include <cstring>

#include <fcntl.h>
#include <unistd.h>

#include <imgui.h>

#include "app/editor_ui.hpp" // Tone, editor_tone, editor_ellipsize (TEK palet)

namespace tulpar::engine::app {

namespace {

// --- Halka tamponu (statik; ayirma yok) -------------------------------------
struct Ring {
  ConsoleEntry e[kConsoleCapacity];
  uint32_t head = 0;  // EN ESKI kaydin indeksi
  uint32_t count = 0;
  uint32_t dropped = 0; // tasmada dusen en eski kayit
  uint32_t total = 0;   // acilistan beri acilan kayit (toplanan tekrarlar haric)
  uint32_t seq = 1;     // monoton kimlik (halka kaysa da secim sabit kalir)
  uint32_t frame = 0;
  ConsoleCounts counts;
};
Ring g_ring;
ConsoleCapture *g_cap = nullptr;

inline float ImTrunc(float v) { return (float)(int)v; }
ImU32 tone_u32(Tone t, float alpha = 1.0f) {
  float c[4];
  editor_tone(t, c);
  return ImGui::ColorConvertFloat4ToU32(ImVec4(c[0], c[1], c[2], c[3] * alpha));
}

char lower_ascii(char c) { return (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c; }
// ASCII buyuk/kucuk harf duyarsiz alt dizi. Turkce harfler UTF-8'de iki bayt
// oldugu icin katlanmaz — arama kutusu icin yeterli, siniflandirici zaten
// ASCII isaretlere bakiyor.
bool contains_ci(const char *hay, const char *needle) {
  if (!needle || !*needle) return true;
  if (!hay) return false;
  for (const char *h = hay; *h; h++) {
    const char *a = h;
    const char *b = needle;
    while (*a && *b && lower_ascii(*a) == lower_ascii(*b)) { a++; b++; }
    if (!*b) return true;
  }
  return false;
}

// Metni dst'ye kopyalar; sigmazsa UTF-8 SINIRINDA keser ve "…" ekler.
// force: sigsa bile kesik isaretle (yakalamada satir zaten boru tarafinda
// asilmisti, kuyrugu hic gelmedi). Donus: kesildi mi.
bool set_msg(char *dst, const char *src, bool force) {
  const uint32_t len = src ? (uint32_t)std::strlen(src) : 0u;
  if (len + 1 <= kConsoleMsgLen && !force) {
    if (len) std::memcpy(dst, src, len);
    dst[len] = 0;
    return false;
  }
  static const char kEll[] = "\xE2\x80\xA6"; // …
  uint32_t keep = len + 1 <= kConsoleMsgLen ? len : kConsoleMsgLen - 1;
  if (keep >= 3) keep -= 3; else keep = 0;
  while (keep > 0 && ((unsigned char)src[keep] & 0xC0) == 0x80) keep--; // UTF-8 sinirina geri cekil
  if (keep) std::memcpy(dst, src, keep);
  std::memcpy(dst + keep, kEll, 3);
  dst[keep + 3] = 0;
  return true;
}

void push(ConsoleLevel level, const char *tag, const char *text, bool force_trunc) {
  if ((uint32_t)level >= kConsoleLevelCount) level = ConsoleLevel::Bilgi;
  if (!text) text = "";
  if (!tag || !*tag) tag = kConsoleTagEngine;
  Ring &r = g_ring;
  // ARDISIK AYNI SATIR: yeni kayit acma, sayaci artir (Unity "Collapse").
  // Her karede tekrarlayan bir uyari yoksa halkayi tek karede suplurur ve
  // ondan onceki her sey kaybolurdu.
  if (r.count) {
    ConsoleEntry &last = r.e[(r.head + r.count - 1) % kConsoleCapacity];
    char probe[kConsoleMsgLen];
    set_msg(probe, text, force_trunc);
    if (last.level == level && std::strncmp(last.tag, tag, kConsoleTagLen - 1) == 0 && std::strcmp(last.msg, probe) == 0) {
      if (last.repeat < 0xFFFFFFFFu) last.repeat++;
      last.last_frame = r.frame;
      return;
    }
  }
  if (r.count == kConsoleCapacity) { // tasma: EN ESKI duser, SAYILIR
    const ConsoleEntry &old = r.e[r.head];
    if ((uint32_t)old.level < kConsoleLevelCount) r.counts.n[(uint32_t)old.level]--;
    r.head = (r.head + 1) % kConsoleCapacity;
    r.count--;
    r.dropped++;
  }
  ConsoleEntry &e = r.e[(r.head + r.count) % kConsoleCapacity];
  e = ConsoleEntry{};
  e.level = level;
  e.frame = e.last_frame = r.frame;
  e.repeat = 1;
  e.seq = r.seq++;
  std::snprintf(e.tag, sizeof e.tag, "%s", tag);
  e.truncated = set_msg(e.msg, text, force_trunc);
  r.count++;
  r.counts.n[(uint32_t)level]++;
  r.total++;
}

// --- Yakalama: tek akis ------------------------------------------------------
void stream_emit(ConsoleCaptureStream &s, ConsoleCapture &c) {
  s.partial[s.partial_len < kConsoleMsgLen ? s.partial_len : kConsoleMsgLen - 1] = 0;
  if (s.partial_len == 0 && !s.overflowing) { // bos satir: gurultu, kayit acma
    s.overflowing = false;
    return;
  }
  push(console_classify_level(s.partial, s.is_err), console_classify_tag(s.partial), s.partial, s.overflowing);
  c.lines++;
  if (s.overflowing) c.long_lines++;
  s.partial_len = 0;
  s.overflowing = false;
}

bool stream_begin(ConsoleCaptureStream &s, int fd, bool is_err, char *err, uint32_t err_cap) {
  int p[2] = {-1, -1};
  if (::pipe(p) != 0) {
    std::snprintf(err, err_cap, "boru acilamadi (fd %d)", fd);
    return false;
  }
  // OKUMA ucu bloklamaz: kare icinde read() cagirip veri yoksa beklemek
  // editoru dondururdu. YAZMA ucu BLOKLAR (varsayilan) — bilincli: glibc
  // EAGAIN alirsa FILE akisinin hata bayragini kaldirir ve sonraki printf'ler
  // SESSIZCE duser. Sozlesme bunun yerine "her kare drain" olsun: iki drain
  // arasinda boru tamponundan (64 KB) fazla yazan bir program beklerdi.
  const int fl = ::fcntl(p[0], F_GETFL, 0);
  ::fcntl(p[0], F_SETFL, (fl < 0 ? 0 : fl) | O_NONBLOCK);
  s.saved_fd = ::dup(fd);
  if (s.saved_fd < 0) {
    std::snprintf(err, err_cap, "fd %d kopyalanamadi", fd);
    ::close(p[0]);
    ::close(p[1]);
    return false;
  }
  if (::dup2(p[1], fd) < 0) {
    std::snprintf(err, err_cap, "fd %d yonlendirilemedi", fd);
    ::close(s.saved_fd);
    s.saved_fd = -1;
    ::close(p[0]);
    ::close(p[1]);
    return false;
  }
  ::close(p[1]);
  s.read_fd = p[0];
  s.target_fd = fd;
  s.is_err = is_err;
  s.partial_len = 0;
  s.overflowing = false;
  return true;
}

void stream_drain(ConsoleCaptureStream &s, ConsoleCapture &c) {
  if (s.read_fd < 0) return;
  char buf[4096];
  for (;;) {
    const ssize_t n = ::read(s.read_fd, buf, sizeof buf);
    if (n <= 0) break; // 0 = yazan yok, <0 = EAGAIN (bloklamayan bos boru)
    c.bytes += (uint32_t)n;
    // Terminal SUSMASIN: yakalanan baytlar ozgun fd'ye de gider. Yoksa
    // editoru terminalden calistiran gelistirici ciktinin tamamini kaybederdi.
    if (c.echo && s.saved_fd >= 0) {
      const ssize_t w = ::write(s.saved_fd, buf, (size_t)n);
      (void)w;
    }
    for (ssize_t i = 0; i < n; i++) {
      const char ch = buf[i];
      if (ch == '\n') { stream_emit(s, c); continue; }
      if (ch == '\r') continue; // CRLF: satir sonu bir kere sayilir
      if (s.overflowing) continue; // satir tampona sigmadi: '\n' gelene kadar at
      if (s.partial_len + 1 >= kConsoleMsgLen) { s.overflowing = true; continue; }
      s.partial[s.partial_len++] = ch;
    }
    if ((size_t)n < sizeof buf) break; // tampon dolmadi: boru bosaldi
  }
}

void stream_end(ConsoleCaptureStream &s, ConsoleCapture &c) {
  if (s.read_fd >= 0) {
    stream_drain(s, c);
    if (s.partial_len || s.overflowing) stream_emit(s, c); // '\n' ile bitmeyen son satir kaybolmasin
    ::close(s.read_fd);
    s.read_fd = -1;
  }
  if (s.saved_fd >= 0) {
    ::dup2(s.saved_fd, s.target_fd);
    ::close(s.saved_fd);
    s.saved_fd = -1;
  }
}

// Duzey simgesi ve tonu (● bilgi / ▲ uyari / ✗ hata) — font denetlendi.
const char *level_icon(ConsoleLevel l) {
  switch (l) {
    case ConsoleLevel::Uyari: return "\xE2\x96\xB2"; // ▲
    case ConsoleLevel::Hata: return "\xE2\x9C\x97";  // ✗
    default: return "\xE2\x97\x8F";                  // ●
  }
}
Tone level_tone(ConsoleLevel l) {
  switch (l) {
    case ConsoleLevel::Uyari: return Tone::Warn;
    case ConsoleLevel::Hata: return Tone::Err;
    default: return Tone::TextDim;
  }
}
const char *level_name(ConsoleLevel l) {
  switch (l) {
    case ConsoleLevel::Uyari: return "Uyar\xC4\xB1"; // Uyarı
    case ConsoleLevel::Hata: return "Hata";
    default: return "Bilgi";
  }
}

} // namespace

// --- Yazma ------------------------------------------------------------------
void console_set_frame(uint32_t frame) { g_ring.frame = frame; }
uint32_t console_frame() { return g_ring.frame; }

void console_logv(ConsoleLevel level, const char *tag, const char *fmt, va_list ap) {
  char buf[kConsoleMsgLen * 2]; // bicimlendirme tamponu: kesme set_msg'de, GORUNUR
  if (!fmt) fmt = "";
  std::vsnprintf(buf, sizeof buf, fmt, ap);
  push(level, tag, buf, false);
}
void console_log(ConsoleLevel level, const char *tag, const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  console_logv(level, tag, fmt, ap);
  va_end(ap);
}
void console_log_raw(ConsoleLevel level, const char *tag, const char *text) { push(level, tag, text, false); }

void console_clear() {
  g_ring.head = g_ring.count = 0;
  g_ring.counts = ConsoleCounts{};
  // dropped/total/seq SIFIRLANMAZ: "temizledim" bilgisi gecmisi silmez, sayac
  // kullanicinin ne kadar seyi kacirdigini soylemeye devam eder.
}

// --- Okuma ------------------------------------------------------------------
uint32_t console_size() { return g_ring.count; }
uint32_t console_dropped() { return g_ring.dropped; }
uint32_t console_total() { return g_ring.total; }
ConsoleCounts console_counts() { return g_ring.counts; }
const ConsoleEntry *console_at(uint32_t i) {
  if (i >= g_ring.count) return nullptr;
  return &g_ring.e[(g_ring.head + i) % kConsoleCapacity];
}
const ConsoleEntry *console_by_seq(uint32_t seq) {
  for (uint32_t i = 0; i < g_ring.count; i++) {
    const ConsoleEntry *e = console_at(i);
    if (e && e->seq == seq) return e;
  }
  return nullptr;
}

bool console_row_visible(const ConsoleEntry &e, const ConsoleView &v) {
  const uint32_t l = (uint32_t)e.level;
  if (l < kConsoleLevelCount && !v.show[l]) return false;
  if (v.search[0] == 0) return true;
  return contains_ci(e.msg, v.search) || contains_ci(e.tag, v.search);
}
uint32_t console_filtered(const ConsoleView &v, uint32_t *out, uint32_t cap) {
  uint32_t n = 0;
  for (uint32_t i = 0; i < g_ring.count && n < cap; i++) {
    const ConsoleEntry *e = console_at(i);
    if (e && console_row_visible(*e, v)) out[n++] = i;
  }
  return n;
}

// --- Siniflandirma ----------------------------------------------------------
ConsoleLevel console_classify_level(const char *line, bool from_stderr) {
  if (!line) return from_stderr ? ConsoleLevel::Uyari : ConsoleLevel::Bilgi;
  // SIRA ONEMLI. "Validation Warning: [ VUID-... ]" satirinda once VUID'e
  // bakilsaydi bir UYARI hata olarak gosterilirdi.
  // Turkce isaretler BUYUK HARF eslesir (motorun kurali: "HATA", "UYARI",
  // "KAYDEDILEMEDI"); kucuk harfli "hata ayiklama" gibi duz cumleler bu yuzden
  // yanlislikla kirmiziya boyanmaz. Ingilizce isaretler harf duyarsizdir
  // (kutuphaneler "Error:", "ERROR", "error" hepsini yazar).
  if (std::strstr(line, "HATA") || contains_ci(line, "error")) return ConsoleLevel::Hata;
  if (std::strstr(line, "UYARI") || contains_ci(line, "warning")) return ConsoleLevel::Uyari;
  if (std::strstr(line, "VUID")) return ConsoleLevel::Hata;
  // Isaretsiz stderr satiri: motorun olumcul yollari ("instance: %s", "arena")
  // anahtar kelime YAZMADAN stderr'e yazar. Bilgi demek onu gozden kacirmak
  // olurdu; Hata demek de asiri iddia — Uyari durust orta yol.
  return from_stderr ? ConsoleLevel::Uyari : ConsoleLevel::Bilgi;
}

const char *console_classify_tag(const char *line) {
  if (!line) return kConsoleTagEngine;
  while (*line == ' ' || *line == '\t') line++;
  if (std::strncmp(line, "[engine_editor]", 15) == 0 || std::strncmp(line, "[editor]", 8) == 0) return kConsoleTagEditor;
  if (std::strncmp(line, "[engine]", 8) == 0) return kConsoleTagEngine;
  if (std::strstr(line, "VUID") || std::strncmp(line, "Validation", 10) == 0 || std::strstr(line, "UNASSIGNED-") ||
      std::strstr(line, "Validation Performance Warning"))
    return kConsoleTagVulkan;
  if (std::strncmp(line, "sahne ", 6) == 0 || std::strncmp(line, "[sahne]", 7) == 0) return kConsoleTagScene;
  return kConsoleTagEngine;
}

// --- Yakalama ---------------------------------------------------------------
bool console_capture_begin(ConsoleCapture *c) {
  if (!c) return false;
  if (g_cap) { std::snprintf(c->err_msg, sizeof c->err_msg, "yakalama zaten acik"); return false; }
  c->err_msg[0] = 0;
  c->lines = c->bytes = c->long_lines = 0;
  // Bekleyen tamponlar OZGUN fd'ye gitsin: yonlendirmeden sonra bosaltilirsa
  // yakalama ONCESI yazilmis satirlar konsola dusup sirayi bozardi.
  std::fflush(stdout);
  std::fflush(stderr);
  if (!stream_begin(c->out, 1, false, c->err_msg, sizeof c->err_msg)) return false;
  if (!stream_begin(c->err, 2, true, c->err_msg, sizeof c->err_msg)) {
    stream_end(c->out, *c); // kismi kurulum geri alinir: ya hepsi ya hicbiri
    return false;
  }
  // Bir BORUYA yazan libc TAM TAMPONLAMAYA gecer (terminale satir tamponu).
  // Tamponsuz yapilmazsa cikti kilobaytlar birikene kadar konsola HIC gelmez.
  std::setvbuf(stdout, nullptr, _IONBF, 0);
  std::setvbuf(stderr, nullptr, _IONBF, 0);
  c->active = true;
  g_cap = c;
  return true;
}

void console_capture_drain() {
  ConsoleCapture *c = g_cap;
  if (!c || !c->active) return;
  stream_drain(c->out, *c);
  stream_drain(c->err, *c);
}

void console_capture_end(ConsoleCapture *c) {
  if (!c || !c->active) return;
  std::fflush(stdout); // fd hala boruya bakiyor: bekleyenler once yakalansin
  std::fflush(stderr);
  stream_end(c->out, *c);
  stream_end(c->err, *c);
  // Tampon kipi terminal varsayilanina (satir tamponu) doner. Ozgun kip
  // sorulamaz; satir tamponu terminalde dogru, boruda da zararsizdir.
  std::setvbuf(stdout, nullptr, _IOLBF, 0);
  c->active = false;
  if (g_cap == c) g_cap = nullptr;
}

const ConsoleCapture *console_capture_active() { return g_cap; }

// --- Panel ------------------------------------------------------------------
void console_panel(ConsoleView &v) {
  v.shown = 0;
  v.clipped = 0;
  v.scrolled = false;
  if (!ImGui::GetCurrentContext()) return;
  const ImGuiStyle &st = ImGui::GetStyle();
  const float fs = ImGui::GetFontSize();
  const float frame_h = ImGui::GetFrameHeight();
  const ConsoleCounts cc = console_counts();
  const ImU32 dim = tone_u32(Tone::TextDim), text = tone_u32(Tone::Text);

  // --- Arac cubugu: Temizle | ↓otomatik | ● N ▲ N ✗ N | ara ------------------
  {
    const float avail_w = ImGui::GetContentRegionAvail().x;
    if (ImGui::Button("Temizle")) console_clear();
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Listeyi temizle (d\xC3\xBC\xC5\x9F""en sat\xC4\xB1r say\xC4\xB1s\xC4\xB1 KALIR)");
    ImGui::SameLine(0.0f, st.ItemSpacing.x);
    // Otomatik kaydirma: etkinse vurgulu (arac cubuklarindaki ac-kapa kurali).
    ImGui::PushStyleColor(ImGuiCol_Button, v.autoscroll ? tone_u32(Tone::Accent, 0.35f) : tone_u32(Tone::Bg3));
    ImGui::PushStyleColor(ImGuiCol_Text, v.autoscroll ? tone_u32(Tone::AccentHi) : text);
    if (ImGui::Button("\xE2\x86\x93##otokay", ImVec2(frame_h, frame_h))) v.autoscroll = !v.autoscroll; // ↓
    ImGui::PopStyleColor(2);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Otomatik kayd\xC4\xB1r: yeni sat\xC4\xB1r geldik\xC3\xA7""e alta kay");
    // Uc duzey dugmesi: simge + sayi, kendi tonunda; kapali olan soluk.
    for (uint32_t l = 0; l < kConsoleLevelCount; l++) {
      ImGui::SameLine(0.0f, l == 0 ? st.ItemSpacing.x : st.ItemInnerSpacing.x);
      const ConsoleLevel lv = (ConsoleLevel)l;
      const bool on = v.show[l];
      char lbl[64];
      std::snprintf(lbl, sizeof lbl, "%s %u##duzey%u", level_icon(lv), cc.n[l], l);
      ImGui::PushStyleColor(ImGuiCol_Button, on ? tone_u32(Tone::Bg3) : tone_u32(Tone::Bg1));
      ImGui::PushStyleColor(ImGuiCol_Text, tone_u32(level_tone(lv), on ? 1.0f : 0.4f));
      if (ImGui::Button(lbl)) v.show[l] = !on;
      ImGui::PopStyleColor(2);
      if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s: %u kay\xC4\xB1t \xC2\xB7 tiklayarak gizle/g\xC3\xB6ster", level_name(lv), cc.n[l]);
    }
    // Dusen kayit rozeti: SESSIZ KESME YOK — halka tasmissa gorunur.
    const uint32_t drop = console_dropped();
    const float search_w = ImTrunc(fs * 9.0f);
    if (drop) {
      ImGui::SameLine(0.0f, st.ItemSpacing.x);
      ImGui::AlignTextToFramePadding();
      ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(tone_u32(Tone::Warn)), "\xE2\x9A\xA0 %u", drop); // ⚠
      if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Bu oturumda halka %u kez doldu ve o kadar ESKI sat\xC4\xB1r d\xC3\xBC\xC5\x9Ft\xC3\xBC (kapasite %u).\n"
                          "\x22Temizle\x22 bu sayac\xC4\xB1 SIFIRLAMAZ: neyi ka\xC3\xA7\xC4\xB1rd\xC4\xB1\xC4\x9F\xC4\xB1n bilgisi silinmez.",
                          drop, kConsoleCapacity);
    }
    const float x = avail_w - search_w + st.ItemSpacing.x;
    if (x > ImGui::GetCursorPosX()) ImGui::SameLine(x);
    else ImGui::SameLine(0.0f, st.ItemSpacing.x);
    ImGui::SetNextItemWidth(search_w);
    ImGui::InputTextWithHint("##konsolara", "ara\xE2\x80\xA6", v.search, sizeof v.search);
  }

  // --- Liste ------------------------------------------------------------------
  static uint32_t idx[kConsoleCapacity];
  const uint32_t n = console_filtered(v, idx, kConsoleCapacity);
  v.shown = n;
  const ConsoleEntry *sel = v.selected >= 0 ? console_by_seq((uint32_t)v.selected) : nullptr;
  if (v.selected >= 0 && !sel) v.selected = -1; // secili satir halkadan dustu
  const float detail_h = (sel && v.expanded) ? (frame_h + fs * 4.0f + st.ItemSpacing.y * 3.0f) : 0.0f;
  float body_h = ImGui::GetContentRegionAvail().y - detail_h;
  if (body_h < frame_h * 2.0f) body_h = frame_h * 2.0f;

  ImGui::PushStyleColor(ImGuiCol_ChildBg, tone_u32(Tone::Bg0, 0.4f));
  if (ImGui::BeginChild("##konsol_liste", ImVec2(0, body_h), ImGuiChildFlags_Borders, ImGuiWindowFlags_HorizontalScrollbar)) {
    ImDrawList *dl = ImGui::GetWindowDrawList();
    if (n == 0) {
      const char *msg = console_size() == 0 ? "Konsol bo\xC5\x9F" : "S\xC3\xBCzge\xC3\xA7le e\xC5\x9Fle\xC5\x9F""en sat\xC4\xB1r yok";
      ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(dim), "  %s", msg);
    } else {
      // Sabit sutunlar: kare numarasi ve etiket AYNI x'te baslar — degisken
      // genislikli yazi tipiyle bile satirlar hizali okunur (monospace yok).
      const float frame_col = ImGui::CalcTextSize("000000").x;
      const float tag_col = ImGui::CalcTextSize("[vulkan]").x;
      const float row_h = fs + st.FramePadding.y * 2.0f;
      ImGuiListClipper clip;
      clip.Begin((int)n, row_h);
      while (clip.Step()) {
        for (int k = clip.DisplayStart; k < clip.DisplayEnd; k++) {
          const ConsoleEntry *e = console_at(idx[k]);
          if (!e) continue;
          v.clipped++;
          ImGui::PushID((int)e->seq);
          const ImVec2 p = ImGui::GetCursorScreenPos();
          const float w = ImGui::GetContentRegionAvail().x;
          const bool is_sel = v.selected == (int32_t)e->seq;
          if (ImGui::Selectable("##satir", is_sel, ImGuiSelectableFlags_AllowOverlap, ImVec2(w, row_h))) {
            if (is_sel) v.expanded = !v.expanded;
            else { v.selected = (int32_t)e->seq; v.expanded = true; }
          }
          if ((k & 1) && !is_sel) dl->AddRectFilled(p, ImVec2(p.x + w, p.y + row_h), tone_u32(Tone::White, 0.02f));
          float x = p.x + st.FramePadding.x;
          const float ty = p.y + st.FramePadding.y;
          dl->AddText(ImVec2(x, ty), tone_u32(level_tone(e->level)), level_icon(e->level));
          x += fs + st.ItemInnerSpacing.x;
          char nb[24];
          std::snprintf(nb, sizeof nb, "%u", e->frame);
          const float nw = ImGui::CalcTextSize(nb).x;
          dl->AddText(ImVec2(x + frame_col - nw, ty), dim, nb); // saga dayali kare numarasi
          x += frame_col + st.ItemInnerSpacing.x;
          char tb[kConsoleTagLen + 4];
          std::snprintf(tb, sizeof tb, "[%s]", e->tag);
          dl->AddText(ImVec2(x, ty), dim, tb);
          x += tag_col + st.ItemInnerSpacing.x;
          // Tekrar rozeti sagda; mesaj ona kadar kirpilir (sessiz kesme yok).
          float right = p.x + w - st.FramePadding.x;
          char rb[24] = {0};
          if (e->repeat > 1) {
            std::snprintf(rb, sizeof rb, "x%u", e->repeat);
            const float rw = ImGui::CalcTextSize(rb).x + 8.0f;
            dl->AddRectFilled(ImVec2(right - rw, ty - 1.0f), ImVec2(right, ty + fs + 1.0f), tone_u32(Tone::Bg4), st.FrameRounding);
            dl->AddText(ImVec2(right - rw + 4.0f, ty), tone_u32(Tone::Text), rb);
            right -= rw + st.ItemInnerSpacing.x;
          }
          char mb[kConsoleMsgLen + 8];
          editor_ellipsize(e->msg, right - x, mb, sizeof mb);
          dl->AddText(ImVec2(x, ty), e->level == ConsoleLevel::Bilgi ? text : tone_u32(level_tone(e->level)), mb);
          if (ImGui::IsItemHovered() && std::strcmp(mb, e->msg) != 0) ImGui::SetTooltip("%s", e->msg);
          ImGui::PopID();
        }
      }
      clip.End();
      // ImGui gunluk penceresi kalibi: kullanici yukari kaydirdiysa yerinde
      // birakilir, dipteyse dipte tutulur.
      if (v.autoscroll && ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 1.0f) {
        ImGui::SetScrollHereY(1.0f);
        v.scrolled = true;
      }
    }
  }
  ImGui::EndChild();
  ImGui::PopStyleColor();

  // --- Ayrinti bolmesi (secili satirin TAM metni + kopyala) --------------------
  if (sel && v.expanded) {
    ImGui::PushStyleColor(ImGuiCol_ChildBg, tone_u32(Tone::Bg2, 0.8f));
    if (ImGui::BeginChild("##konsol_ayrinti", ImVec2(0, detail_h), ImGuiChildFlags_Borders)) {
      ImGui::AlignTextToFramePadding();
      ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(tone_u32(level_tone(sel->level))), "%s %s", level_icon(sel->level), level_name(sel->level));
      ImGui::SameLine();
      if (sel->repeat > 1)
        ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(dim), "\xC2\xB7 [%s] \xC2\xB7 kare %u\xE2\x80\x93%u \xC2\xB7 x%u", sel->tag, sel->frame,
                           sel->last_frame, sel->repeat);
      else ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(dim), "\xC2\xB7 [%s] \xC2\xB7 kare %u", sel->tag, sel->frame);
      ImGui::SameLine(ImGui::GetContentRegionAvail().x - ImGui::CalcTextSize("Kopyala").x - frame_h - st.ItemSpacing.x * 2.0f);
      if (ImGui::Button("Kopyala")) ImGui::SetClipboardText(sel->msg);
      ImGui::SameLine();
      if (ImGui::Button("\xC3\x97##ayrinti_kapat", ImVec2(frame_h, frame_h))) v.expanded = false; // ×
      ImGui::PushTextWrapPos(0.0f);
      ImGui::TextUnformatted(sel->msg);
      if (sel->truncated)
        ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(tone_u32(Tone::Warn)), "(sat\xC4\xB1r %u bayta k\xC4\xB1rp\xC4\xB1ld\xC4\xB1)",
                           kConsoleMsgLen - 1);
      ImGui::PopTextWrapPos();
    }
    ImGui::EndChild();
    ImGui::PopStyleColor();
  }
}

} // namespace tulpar::engine::app
