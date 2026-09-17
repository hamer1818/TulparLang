#include "app/editor_files.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

#include <imgui.h>

#include "app/editor_ui.hpp" // Tone, editor_tone, editor_ellipsize (TEK palet)

namespace tulpar::engine::app {

namespace {

inline float ImTrunc(float v) { return (float)(int)v; }
ImU32 tone_u32(Tone t, float alpha = 1.0f) {
  float c[4];
  editor_tone(t, c);
  return ImGui::ColorConvertFloat4ToU32(ImVec4(c[0], c[1], c[2], c[3] * alpha));
}

char lower_ascii(char c) { return (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c; }
bool ends_with_ci(const char *s, const char *suffix) {
  if (!s || !suffix || !*suffix) return false;
  const size_t ls = std::strlen(s), lx = std::strlen(suffix);
  if (lx > ls) return false;
  const char *t = s + (ls - lx);
  for (size_t i = 0; i < lx; i++)
    if (lower_ascii(t[i]) != lower_ascii(suffix[i])) return false;
  return true;
}

// --- Son dosyalar: modul durumu (editor_layout gibi TEK kopya) --------------
struct RecentState {
  char paths[kRecentMax][kFilePathLen];
  bool exists[kRecentMax];
  uint32_t count = 0;
  char err[192] = {0};
};
RecentState g_recent;
constexpr const char *kRecentHeader = "tulpar_son_dosyalar 1";

void recent_fail(const char *fmt, const char *a) { std::snprintf(g_recent.err, sizeof g_recent.err, fmt, a); }
// Listenin SONUNA ekler (dosya en yeniden eskiye yazilir; ayristirirken sira
// korunmali). recent_push one ekler — ikisi ayni sey DEGIL.
void recent_append(const char *path) {
  if (!path || !*path) return;
  if (g_recent.count >= kRecentMax) return;
  if (std::strlen(path) + 1 > kFilePathLen) return;
  for (uint32_t i = 0; i < g_recent.count; i++)
    if (std::strcmp(g_recent.paths[i], path) == 0) return; // dosyada kopya varsa tekillestir
  std::snprintf(g_recent.paths[g_recent.count], kFilePathLen, "%s", path);
  g_recent.exists[g_recent.count] = false;
  g_recent.count++;
}

} // namespace

// --- Saf yol yardimcilari ---------------------------------------------------
bool file_path_join(const char *dir, const char *name, char *out, uint32_t cap) {
  if (!out || cap == 0) return false;
  out[0] = 0;
  if (!name || !*name) return false;
  if (!dir || !*dir) { // dizinsiz: adin kendisi
    if (std::strlen(name) + 1 > cap) return false;
    std::snprintf(out, cap, "%s", name);
    return true;
  }
  size_t dl = std::strlen(dir);
  while (dl > 1 && dir[dl - 1] == '/') dl--; // sondaki '/' tekrar etmesin
  const bool root = (dl == 1 && dir[0] == '/');
  const size_t need = (root ? 1 : dl + 1) + std::strlen(name) + 1;
  if (need > cap) return false;
  if (root) std::snprintf(out, cap, "/%s", name);
  else {
    std::memcpy(out, dir, dl);
    out[dl] = '/';
    std::snprintf(out + dl + 1, cap - dl - 1, "%s", name);
  }
  return true;
}

bool file_path_parent(const char *dir, char *out, uint32_t cap) {
  if (!dir || !*dir || !out || cap == 0) return false;
  out[0] = 0;
  size_t n = std::strlen(dir);
  while (n > 1 && dir[n - 1] == '/') n--;
  if (n == 1 && dir[0] == '/') return false; // kok: daha yukarisi yok
  size_t slash = (size_t)-1;
  for (size_t i = 0; i < n; i++)
    if (dir[i] == '/') slash = i;
  if (slash == (size_t)-1) { // goreli tek bilesen ("a") -> "."
    if (n == 1 && dir[0] == '.') return false;
    if (cap < 2) return false;
    out[0] = '.';
    out[1] = 0;
    return true;
  }
  const size_t len = slash == 0 ? 1 : slash; // "/a" -> "/"
  if (len + 1 > cap) return false;
  std::memcpy(out, dir, len);
  out[len] = 0;
  return true;
}

const char *file_path_base(const char *path) {
  if (!path) return "";
  const char *b = path;
  for (const char *p = path; *p; p++)
    if (*p == '/') b = p + 1;
  return b;
}

bool file_exists(const char *path) {
  if (!path || !*path) return false;
  struct stat s;
  return ::stat(path, &s) == 0;
}
bool file_is_dir(const char *path) {
  if (!path || !*path) return false;
  struct stat s;
  return ::stat(path, &s) == 0 && S_ISDIR(s.st_mode);
}

bool scene_path_with_extension(const char *in, const char *ext, char *out, uint32_t cap) {
  if (!out || cap == 0) return false;
  out[0] = 0;
  if (!in || !*in) return false;
  const size_t li = std::strlen(in);
  if (in[li - 1] == '/') return false; // dizin yolu: dosya adi yok
  if (!ext || !*ext) {                 // uzantisiz istendi: oldugu gibi
    if (li + 1 > cap) return false;
    std::memcpy(out, in, li + 1);
    return true;
  }
  const bool has = ends_with_ci(in, ext);
  const size_t need = li + (has ? 0 : std::strlen(ext)) + 1;
  if (need > cap) return false;
  if (has) std::memcpy(out, in, li + 1);
  else std::snprintf(out, cap, "%s%s", in, ext);
  return true;
}

// --- Dizin listesi ----------------------------------------------------------
FileListResult file_list_dir(const char *dir, const char *ext, FileEntry *out, uint32_t cap) {
  FileListResult r{};
  if (!dir || !*dir || !out || cap == 0) {
    std::snprintf(r.err, sizeof r.err, "dizin yolu yok");
    return r;
  }
  DIR *dp = ::opendir(dir);
  if (!dp) {
    std::snprintf(r.err, sizeof r.err, "Dizin a\xC3\xA7\xC4\xB1lamad\xC4\xB1: %s", dir); // Dizin açılamadı
    return r;
  }
  r.ok = true;
  char full[kFilePathLen];
  for (struct dirent *de = ::readdir(dp); de; de = ::readdir(dp)) {
    const char *nm = de->d_name;
    if (nm[0] == '.') continue; // gizli girdiler + "." + ".."
    if (std::strlen(nm) + 1 > kFileNameLen || !file_path_join(dir, nm, full, sizeof full)) {
      r.truncated++; // ad/yol tavani asildi: SESSIZ atlama yok, sayilir
      continue;
    }
    // d_type yerine stat: bazi dosya sistemleri DT_UNKNOWN doner ve dizinler
    // dosya gibi listelenirdi (girilemezlerdi).
    const bool isdir = file_is_dir(full);
    if (!isdir && ext && *ext && !ends_with_ci(nm, ext)) continue;
    if (r.count >= cap) {
      r.truncated++;
      continue;
    }
    FileEntry f;
    std::snprintf(f.name, sizeof f.name, "%s", nm);
    f.dir = isdir;
    // Once DIZINLER, sonra dosyalar; her iki bolum kendi icinde ada gore
    // sirali (readdir sirasi dosya sistemine baglidir — belirlenimli olmali).
    if (isdir) {
      for (uint32_t k = r.count; k > r.dirs; k--) out[k] = out[k - 1]; // dosyalari bir saga kaydir
      uint32_t k = r.dirs;
      while (k > 0 && std::strcmp(out[k - 1].name, f.name) > 0) { out[k] = out[k - 1]; k--; }
      out[k] = f;
      r.dirs++;
    } else {
      uint32_t k = r.count;
      while (k > r.dirs && std::strcmp(out[k - 1].name, f.name) > 0) { out[k] = out[k - 1]; k--; }
      out[k] = f;
    }
    r.count++;
  }
  ::closedir(dp);
  return r;
}

// --- Son dosyalar -----------------------------------------------------------
void recent_clear() {
  g_recent.count = 0;
  g_recent.err[0] = 0;
}
uint32_t recent_count() { return g_recent.count; }
bool recent_exists(uint32_t i) { return i < g_recent.count && g_recent.exists[i]; }
const char *recent_last_error() { return g_recent.err; }

void recent_refresh() {
  for (uint32_t i = 0; i < g_recent.count; i++) g_recent.exists[i] = file_exists(g_recent.paths[i]);
}

void recent_push(const char *scene_path) {
  if (!scene_path || !*scene_path) return;
  if (std::strlen(scene_path) + 1 > kFilePathLen) {
    recent_fail("son dosya yolu \xC3\xA7ok uzun: %.100s", scene_path); // yol çok uzun
    return;
  }
  uint32_t found = kRecentMax;
  for (uint32_t i = 0; i < g_recent.count; i++)
    if (std::strcmp(g_recent.paths[i], scene_path) == 0) { found = i; break; }
  if (found < kRecentMax) { // zaten var: ONE TASI (kopya olmaz)
    for (uint32_t i = found; i > 0; i--) {
      std::memcpy(g_recent.paths[i], g_recent.paths[i - 1], kFilePathLen);
      g_recent.exists[i] = g_recent.exists[i - 1];
    }
  } else {
    const uint32_t n = g_recent.count < kRecentMax ? g_recent.count + 1 : kRecentMax;
    for (uint32_t i = n - 1; i > 0; i--) {
      std::memcpy(g_recent.paths[i], g_recent.paths[i - 1], kFilePathLen);
      g_recent.exists[i] = g_recent.exists[i - 1];
    }
    g_recent.count = n;
  }
  std::snprintf(g_recent.paths[0], kFilePathLen, "%s", scene_path);
  g_recent.exists[0] = file_exists(scene_path);
}

uint32_t recent_list(const char **out, uint32_t cap) {
  if (!out) return 0;
  const uint32_t n = g_recent.count < cap ? g_recent.count : cap;
  for (uint32_t i = 0; i < n; i++) out[i] = g_recent.paths[i];
  return n;
}

size_t recent_write(char *buf, size_t cap) {
  // Belirlenimli: baslik + her yol bir satir, EN YENI ONCE. Ayni liste ->
  // ayni bayt (kapi bunu olcer; kontrol: farkli sira -> farkli bayt).
  size_t need = 0;
  auto put = [&](const char *s) {
    const size_t l = std::strlen(s);
    if (buf && need < cap) std::snprintf(buf + need, cap - need, "%s", s);
    need += l;
  };
  put(kRecentHeader);
  put("\n");
  for (uint32_t i = 0; i < g_recent.count; i++) {
    put("yol ");
    put(g_recent.paths[i]);
    put("\n");
  }
  if (buf && cap) buf[need < cap ? need : cap - 1] = 0;
  return need;
}

bool recent_parse(const char *text, size_t len) {
  recent_clear();
  if (!text || len == 0) {
    recent_fail("son dosyalar metni bo\xC5\x9F%s", "");
    return false;
  }
  size_t i = 0;
  uint32_t line_no = 0;
  bool header = false;
  char line[kFilePathLen + 16];
  while (i < len) {
    size_t j = i;
    while (j < len && text[j] != '\n') j++;
    const size_t l = j - i;
    line_no++;
    if (l + 1 <= sizeof line) {
      std::memcpy(line, text + i, l);
      line[l] = 0;
    } else {
      line[0] = 0; // satir tavani asti: asagida hata
    }
    i = j < len ? j + 1 : j;
    if (line[0] == 0 && l != 0) {
      std::snprintf(g_recent.err, sizeof g_recent.err, "%u. sat\xC4\xB1r \xC3\xA7ok uzun", line_no);
      recent_clear();
      return false;
    }
    if (line[0] == 0) continue; // bos satir
    if (!header) {
      if (std::strcmp(line, kRecentHeader) != 0) {
        std::snprintf(g_recent.err, sizeof g_recent.err, "%u. sat\xC4\xB1r: ba\xC5\x9Fl\xC4\xB1k bekleniyordu", line_no);
        recent_clear();
        return false;
      }
      header = true;
      continue;
    }
    // Bilinmeyen anahtar SESSIZCE ATLANIR (ileri uyumluluk): yeni bir alan
    // ekleyen surum eski editorde listeyi bosaltmasin.
    if (std::strncmp(line, "yol ", 4) != 0) continue;
    recent_append(line + 4);
  }
  if (!header) {
    recent_fail("ba\xC5\x9Fl\xC4\xB1k yok%s", "");
    recent_clear();
    return false;
  }
  recent_refresh();
  return true;
}

bool recent_load(const char *path) {
  recent_clear();
  if (!path || !*path) {
    recent_fail("son dosyalar yolu yok%s", "");
    return false;
  }
  FILE *f = std::fopen(path, "rb");
  if (!f) {
    recent_fail("son dosyalar okunamad\xC4\xB1: %.120s", path); // ilk calistirma: hata DEGIL, bos liste
    return false;
  }
  static char buf[kRecentMax * (kFilePathLen + 8) + 64];
  const size_t n = std::fread(buf, 1, sizeof buf - 1, f);
  std::fclose(f);
  buf[n] = 0;
  return recent_parse(buf, n);
}

bool recent_save(const char *path) {
  if (!path || !*path) {
    recent_fail("son dosyalar yolu yok%s", "");
    return false;
  }
  static char buf[kRecentMax * (kFilePathLen + 8) + 64];
  const size_t need = recent_write(buf, sizeof buf);
  if (need + 1 > sizeof buf) {
    recent_fail("son dosyalar metni tampona s\xC4\xB1\xC4\x9Fmad\xC4\xB1%s", "");
    return false;
  }
  FILE *f = std::fopen(path, "wb");
  if (!f) {
    recent_fail("son dosyalar yaz\xC4\xB1lamad\xC4\xB1: %.120s", path);
    return false;
  }
  const bool ok = std::fwrite(buf, 1, need, f) == need;
  std::fclose(f);
  if (!ok) recent_fail("son dosyalar yazmas\xC4\xB1 eksik: %.120s", path);
  return ok;
}

// --- Dosya diyalogu ---------------------------------------------------------
namespace {
// Goreli yolu MUTLAK yapar (realpath YOK: onun tamponu PATH_MAX ister ve bizim
// tavanimiz 1024 — tasma riski; ustelik null tamponlu bicimi malloc eder).
// ".." coz(un)mez: diyalog yukari cikarken file_path_parent kullanir, yani
// yola hicbir zaman ".." eklenmez.
bool absolutize(const char *in, char *out, uint32_t cap) {
  if (!out || cap == 0) return false;
  out[0] = 0;
  if (in && in[0] == '/') {
    if (std::strlen(in) + 1 > cap) return false;
    std::snprintf(out, cap, "%s", in);
    return true;
  }
  char cwd[kFilePathLen];
  if (!::getcwd(cwd, sizeof cwd)) return false;
  if (!in || !*in || std::strcmp(in, ".") == 0) {
    if (std::strlen(cwd) + 1 > cap) return false;
    std::snprintf(out, cap, "%s", cwd);
    return true;
  }
  return file_path_join(cwd, in, out, cap);
}
// Diyalogu baska bir dizine tasir (kirinti tiklamasi, ".." ya da cift tik).
void dialog_goto(FileDialog &d, const char *dir) {
  if (!dir || !*dir) return;
  if (std::strlen(dir) + 1 > kFilePathLen) {
    std::snprintf(d.err, sizeof d.err, "Yol \xC3\xA7ok uzun (%u bayt tavan\xC4\xB1)", kFilePathLen - 1);
    return;
  }
  std::snprintf(d.dir, sizeof d.dir, "%s", dir);
  file_dialog_refresh(d);
}
} // namespace

void file_dialog_refresh(FileDialog &d) {
  d.list = file_list_dir(d.dir, d.ext[0] ? d.ext : nullptr, d.items, kFileListMax);
  d.sel = -1;
  std::snprintf(d.err, sizeof d.err, "%s", d.list.ok ? "" : d.list.err);
}

bool file_dialog_target_path(const FileDialog &d, char *out, uint32_t cap) {
  if (!out || cap == 0) return false;
  out[0] = 0;
  if (d.mode == FileDialogMode::Kaydet) {
    char withext[kFileNameLen + 16];
    if (!scene_path_with_extension(d.name, d.ext, withext, sizeof withext)) return false;
    return file_path_join(d.dir, withext, out, cap);
  }
  if (d.sel < 0 || d.sel >= (int32_t)d.list.count || d.items[d.sel].dir) return false;
  return file_path_join(d.dir, d.items[d.sel].name, out, cap);
}

bool file_dialog_would_overwrite(const FileDialog &d) {
  if (d.mode != FileDialogMode::Kaydet) return false;
  char target[kFilePathLen];
  return file_dialog_target_path(d, target, sizeof target) && file_exists(target);
}

bool file_dialog_open(FileDialog &d, FileDialogMode mode, const char *start_dir, const char *ext, const char *title) {
  d.mode = mode;
  std::snprintf(d.title, sizeof d.title, "%s",
                (title && *title) ? title
                                  : (mode == FileDialogMode::Ac ? "Sahne a\xC3\xA7" : "Farkl\xC4\xB1 kaydet")); // Sahne aç / Farklı kaydet
  std::snprintf(d.ext, sizeof d.ext, "%s", ext ? ext : "");
  d.name[0] = d.path[0] = d.err[0] = 0;
  d.sel = -1;
  d.ask_overwrite = false;
  d.focus_name = mode == FileDialogMode::Kaydet;
  d.shown = 0;
  // start_dir bir DOSYA yolu da olabilir (editorun acik sahnesi): dizini ondan,
  // Kaydet kipinde ad kutusunun baslangicini da ondan al.
  char start[kFilePathLen];
  std::snprintf(start, sizeof start, "%s", (start_dir && *start_dir) ? start_dir : ".");
  if (!file_is_dir(start)) {
    const char *base = file_path_base(start);
    if (*base && mode == FileDialogMode::Kaydet) std::snprintf(d.name, sizeof d.name, "%.*s", (int)kFileNameLen - 1, base);
    char par[kFilePathLen];
    if (file_path_parent(start, par, sizeof par)) std::snprintf(start, sizeof start, "%s", par);
    else std::snprintf(start, sizeof start, ".");
  }
  char abs[kFilePathLen];
  if (absolutize(start, abs, sizeof abs)) std::snprintf(d.dir, sizeof d.dir, "%s", abs);
  else std::snprintf(d.dir, sizeof d.dir, "%s", start);
  file_dialog_refresh(d);
  d.open = true;
  d.need_open = true;
  return d.list.ok;
}

FileDialogAction file_dialog_draw(FileDialog &d) {
  d.shown = 0;
  if (!ImGui::GetCurrentContext() || !d.open) return FileDialogAction::None;
  const ImGuiStyle &st = ImGui::GetStyle();
  const float fs = ImGui::GetFontSize();
  const float frame_h = ImGui::GetFrameHeight();
  const ImU32 dim = tone_u32(Tone::TextDim), text = tone_u32(Tone::Text), accent = tone_u32(Tone::Accent);
  // Kimlik "###" SONRASIDIR: baslik metni degisse de (Ac/Kaydet) ayni pencere.
  char label[96];
  std::snprintf(label, sizeof label, "%s###dosya_diyalogu", d.title[0] ? d.title : "Dosya");
  if (d.need_open) {
    ImGui::OpenPopup(label);
    d.need_open = false;
  }
  FileDialogAction act = FileDialogAction::None;
  ImGui::SetNextWindowSize(ImVec2(ImTrunc(fs * 34.0f), ImTrunc(fs * 24.0f)), ImGuiCond_Appearing);
  if (!ImGui::BeginPopupModal(label, nullptr, ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoCollapse)) {
    // Esc (ya da baska bir kapatma) ile kapandi: VAZGECILDI say.
    d.open = false;
    return FileDialogAction::Cancelled;
  }

  // --- Kirinti (breadcrumb): tiklanabilir yol bilesenleri --------------------
  {
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(2.0f, st.ItemSpacing.y));
    ImGui::PushID("kirinti");
    if (ImGui::SmallButton("/")) dialog_goto(d, "/");
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("K\xC3\xB6k dizin"); // Kök dizin
    const size_t len = std::strlen(d.dir);
    size_t seg_start = 1;
    uint32_t seg = 0;
    char piece[kFileNameLen];
    for (size_t i = 1; i <= len && seg < kFileCrumbMax; i++) {
      if (i != len && d.dir[i] != '/') continue;
      const size_t l = i - seg_start;
      if (l == 0) { seg_start = i + 1; continue; }
      if (l + 1 > sizeof piece) break; // asiri uzun bilesen: kirintiyi orada kes
      std::memcpy(piece, d.dir + seg_start, l);
      piece[l] = 0;
      ImGui::PushID((int)seg);
      // Satira sigmiyorsa alt satira gec (uzun yol kirintisi tasmasin).
      const float w = ImGui::CalcTextSize(piece).x + st.FramePadding.x * 2.0f + fs * 0.6f;
      if (ImGui::GetContentRegionAvail().x > w) ImGui::SameLine(0.0f, 2.0f);
      ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(dim), "\xE2\x80\xBA"); // ›
      ImGui::SameLine(0.0f, 2.0f);
      if (ImGui::SmallButton(piece)) {
        char up[kFilePathLen];
        const size_t cut = i;
        if (cut + 1 <= sizeof up) {
          std::memcpy(up, d.dir, cut);
          up[cut] = 0;
          dialog_goto(d, up);
        }
      }
      ImGui::PopID();
      seg++;
      seg_start = i + 1;
    }
    ImGui::PopID();
    ImGui::PopStyleVar();
  }
  // Gorunur hata satiri: dizin acilamadiysa liste SESSIZ BOS kalmaz.
  if (d.err[0]) {
    char eb[224];
    editor_ellipsize(d.err, ImGui::GetContentRegionAvail().x, eb, sizeof eb);
    ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(tone_u32(Tone::Err)), "\xE2\x9A\xA0 %s", eb); // ⚠
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", d.err);
  }

  // --- Govde: dizinler, sonra dosyalar --------------------------------------
  float footer = frame_h + st.ItemSpacing.y * 2.0f;
  if (d.mode == FileDialogMode::Kaydet) footer += frame_h + st.ItemSpacing.y;
  if (d.ask_overwrite) footer += frame_h + st.ItemSpacing.y;
  float body_h = ImGui::GetContentRegionAvail().y - footer;
  if (body_h < frame_h * 3.0f) body_h = frame_h * 3.0f;
  char chosen[kFilePathLen] = {0}; // bu karede kabul edilen yol
  bool accept_now = false;
  bool overwrite_ok = false; // kullanici "Uzerine yaz" dedi: bir daha SORMA
  ImGui::PushStyleColor(ImGuiCol_ChildBg, tone_u32(Tone::Bg0, 0.4f));
  if (ImGui::BeginChild("##dosya_liste", ImVec2(0, body_h), ImGuiChildFlags_Borders)) {
    ImDrawList *dl = ImGui::GetWindowDrawList();
    const float row_h = fs + st.FramePadding.y * 2.0f;
    char par[kFilePathLen];
    if (file_path_parent(d.dir, par, sizeof par)) {
      const ImVec2 p = ImGui::GetCursorScreenPos();
      if (ImGui::Selectable("##ustdizin", false, ImGuiSelectableFlags_AllowDoubleClick, ImVec2(0, row_h))) dialog_goto(d, par);
      dl->AddText(ImVec2(p.x + st.FramePadding.x, p.y + st.FramePadding.y), accent, "\xE2\x86\x91"); // ↑
      dl->AddText(ImVec2(p.x + st.FramePadding.x + fs + st.ItemInnerSpacing.x, p.y + st.FramePadding.y), dim,
                  ".. (\xC3\x9Cst dizin)"); // .. (Üst dizin)
    }
    for (uint32_t i = 0; i < d.list.count; i++) {
      const FileEntry &f = d.items[i];
      ImGui::PushID((int)i);
      const ImVec2 p = ImGui::GetCursorScreenPos();
      const float w = ImGui::GetContentRegionAvail().x;
      const bool is_sel = d.sel == (int32_t)i;
      if (ImGui::Selectable("##girdi", is_sel, ImGuiSelectableFlags_AllowDoubleClick | ImGuiSelectableFlags_AllowOverlap,
                            ImVec2(w, row_h))) {
        d.sel = (int32_t)i;
        if (!f.dir && d.mode == FileDialogMode::Kaydet) {
          std::snprintf(d.name, sizeof d.name, "%s", f.name);
          d.ask_overwrite = false;
        }
        if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
          if (f.dir) {
            char sub[kFilePathLen];
            if (file_path_join(d.dir, f.name, sub, sizeof sub)) dialog_goto(d, sub);
            else std::snprintf(d.err, sizeof d.err, "Yol \xC3\xA7ok uzun: %.100s", f.name);
          } else if (file_path_join(d.dir, f.name, chosen, sizeof chosen)) {
            accept_now = true;
          }
        }
      }
      float x = p.x + st.FramePadding.x;
      const float ty = p.y + st.FramePadding.y;
      dl->AddText(ImVec2(x, ty), f.dir ? accent : dim, f.dir ? "\xE2\x96\xB8" : "\xE2\x97\x86"); // ▸ / ◆
      x += fs + st.ItemInnerSpacing.x;
      char nb[kFileNameLen + 8];
      editor_ellipsize(f.name, p.x + w - st.FramePadding.x - x, nb, sizeof nb);
      dl->AddText(ImVec2(x, ty), f.dir ? text : text, nb);
      if (ImGui::IsItemHovered() && std::strcmp(nb, f.name) != 0) ImGui::SetTooltip("%s", f.name);
      ImGui::PopID();
      d.shown++;
    }
    if (d.list.ok && d.list.count == 0) {
      char msg[160];
      if (d.ext[0]) std::snprintf(msg, sizeof msg, "  Bu dizinde %s dosyas\xC4\xB1 yok", d.ext);
      else std::snprintf(msg, sizeof msg, "  Dizin bo\xC5\x9F");
      ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(dim), "%s", msg);
    }
    if (d.list.truncated)
      ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(tone_u32(Tone::Warn)), "  \xE2\x9A\xA0 %u girdi listeye s\xC4\xB1\xC4\x9Fmad\xC4\xB1",
                         d.list.truncated); // ⚠ N girdi listeye sığmadı
  }
  ImGui::EndChild();
  ImGui::PopStyleColor();

  // --- Ad kutusu (yalniz Kaydet) --------------------------------------------
  if (d.mode == FileDialogMode::Kaydet) {
    if (d.focus_name) {
      ImGui::SetKeyboardFocusHere();
      d.focus_name = false;
    }
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - ImGui::CalcTextSize(d.ext).x - st.ItemSpacing.x * 2.0f);
    if (ImGui::InputTextWithHint("##dosyaadi", "dosya ad\xC4\xB1", d.name, sizeof d.name, ImGuiInputTextFlags_EnterReturnsTrue)) {
      if (file_dialog_target_path(d, chosen, sizeof chosen)) accept_now = true;
      else std::snprintf(d.err, sizeof d.err, "Ge\xC3\xA7""ersiz dosya ad\xC4\xB1"); // Geçersiz dosya adı
    }
    if (ImGui::IsItemEdited()) d.ask_overwrite = false; // ad degisti: onay yeniden sorulur
    ImGui::SameLine(0.0f, st.ItemSpacing.x);
    ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(dim), "%s", d.ext);
  }
  // --- Uzerine yazma onayi ---------------------------------------------------
  // IC ICE KIPLI PENCERE YOK: ImGui'nin kipli yigini sira duyarlidir ve takilan
  // bir ic pencere editoru kilitler. Onay, kullanicinin zaten baktigi yerde —
  // ad kutusunun hemen altinda — bir seritte sorulur.
  if (d.ask_overwrite) {
    char warn[kFileNameLen + 64];
    std::snprintf(warn, sizeof warn, "\xE2\x9A\xA0 %.*s zaten var.", (int)kFileNameLen - 1, file_path_base(d.path)); // ⚠ ... zaten var.
    ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(tone_u32(Tone::Warn)), "%s", warn);
    ImGui::SameLine(0.0f, st.ItemSpacing.x);
    if (ImGui::SmallButton("\xC3\x9Czerine yaz")) { // Üzerine yaz
      std::snprintf(chosen, sizeof chosen, "%s", d.path);
      accept_now = true;
      overwrite_ok = true; // ask_overwrite'i burada kapatmak sonsuz dongu olurdu
    }
    ImGui::SameLine(0.0f, st.ItemSpacing.x);
    if (ImGui::SmallButton("Vazge\xC3\xA7##uzerine")) d.ask_overwrite = false;
  }

  // --- Dugmeler --------------------------------------------------------------
  {
    const char *ok_label = d.mode == FileDialogMode::Ac ? "A\xC3\xA7" : "Kaydet"; // Aç / Kaydet
    const float bw = ImTrunc(fs * 5.0f);
    const float total = bw * 2.0f + st.ItemSpacing.x;
    const float x = ImGui::GetContentRegionAvail().x - total;
    if (x > 0) ImGui::SetCursorPosX(ImGui::GetCursorPosX() + x);
    const bool can_ok = d.mode == FileDialogMode::Kaydet ? d.name[0] != 0
                                                         : (d.sel >= 0 && d.sel < (int32_t)d.list.count && !d.items[d.sel].dir);
    ImGui::BeginDisabled(!can_ok);
    ImGui::PushStyleColor(ImGuiCol_Button, tone_u32(Tone::Accent, 0.55f)); // birincil eylem vurgulu
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, tone_u32(Tone::Accent, 0.8f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, tone_u32(Tone::AccentHi, 0.9f));
    const bool ok_clicked = ImGui::Button(ok_label, ImVec2(bw, 0));
    ImGui::PopStyleColor(3);
    if (ok_clicked) {
      if (d.mode == FileDialogMode::Ac) {
        if (!file_path_join(d.dir, d.items[d.sel].name, chosen, sizeof chosen)) std::snprintf(d.err, sizeof d.err, "Yol \xC3\xA7ok uzun");
        else accept_now = true;
      } else if (file_dialog_target_path(d, chosen, sizeof chosen)) {
        accept_now = true;
      } else {
        std::snprintf(d.err, sizeof d.err, "Ge\xC3\xA7""ersiz dosya ad\xC4\xB1");
      }
    }
    ImGui::EndDisabled();
    ImGui::SameLine(0.0f, st.ItemSpacing.x);
    if (ImGui::Button("Vazge\xC3\xA7", ImVec2(bw, 0))) { // Vazgeç
      ImGui::CloseCurrentPopup();
      d.open = false;
      act = FileDialogAction::Cancelled;
    }
  }

  if (accept_now && chosen[0]) {
    // Kaydet: dosya varsa ONCE onay. Onay serit halinde sorulur, kabul edilince
    // ayni yol ikinci kez buraya gelir (ask_overwrite false).
    if (d.mode == FileDialogMode::Kaydet && !overwrite_ok && file_dialog_would_overwrite(d)) {
      std::snprintf(d.path, sizeof d.path, "%s", chosen);
      d.ask_overwrite = true;
    } else {
      std::snprintf(d.path, sizeof d.path, "%s", chosen);
      d.ask_overwrite = false;
      ImGui::CloseCurrentPopup();
      d.open = false;
      act = FileDialogAction::Accepted;
    }
  }
  ImGui::EndPopup();
  return act;
}

// --- Onay kutusu ------------------------------------------------------------
ConfirmResult confirm_modal(ConfirmState &s, const char *title, const char *message, const char *ok, const char *cancel, const char *third) {
  if (!ImGui::GetCurrentContext() || !s.open) return ConfirmResult::None;
  char label[128];
  std::snprintf(label, sizeof label, "%s###onay_kutusu", (title && *title) ? title : "Onay");
  if (!s.need_open) {
    ImGui::OpenPopup(label);
    s.need_open = true; // "OpenPopup cagrildi": bir daha cagrilmasin
  }
  ConfirmResult r = ConfirmResult::None;
  const float fs = ImGui::GetFontSize();
  ImGui::SetNextWindowSize(ImVec2(ImTrunc(fs * 24.0f), 0), ImGuiCond_Appearing);
  if (!ImGui::BeginPopupModal(label, nullptr, ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_AlwaysAutoResize)) {
    s.open = false; // Esc: vazgecildi
    s.need_open = false;
    return ConfirmResult::Cancel;
  }
  ImGui::PushTextWrapPos(0.0f);
  ImGui::TextUnformatted(message ? message : "");
  ImGui::PopTextWrapPos();
  ImGui::Spacing();
  auto close = [&](ConfirmResult res) {
    r = res;
    s.open = false;
    s.need_open = false;
    ImGui::CloseCurrentPopup();
  };
  ImGui::PushStyleColor(ImGuiCol_Button, tone_u32(Tone::Accent, 0.55f)); // birincil eylem vurgulu
  ImGui::PushStyleColor(ImGuiCol_ButtonHovered, tone_u32(Tone::Accent, 0.8f));
  ImGui::PushStyleColor(ImGuiCol_ButtonActive, tone_u32(Tone::AccentHi, 0.9f));
  const bool ok_clicked = ImGui::Button(ok && *ok ? ok : "Tamam");
  ImGui::PopStyleColor(3);
  if (ok_clicked) close(ConfirmResult::Ok);
  if (third && *third) {
    ImGui::SameLine();
    if (ImGui::Button(third)) close(ConfirmResult::Third);
  }
  ImGui::SameLine();
  if (ImGui::Button(cancel && *cancel ? cancel : "Vazge\xC3\xA7")) close(ConfirmResult::Cancel);
  if (r == ConfirmResult::None && (ImGui::IsKeyPressed(ImGuiKey_Enter) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter))) close(ConfirmResult::Ok);
  ImGui::EndPopup();
  return r;
}

} // namespace tulpar::engine::app
