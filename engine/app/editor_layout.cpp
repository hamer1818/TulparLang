#include "app/editor_layout.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <imgui_internal.h> // DockBuilder*, ImGuiDockNode, ImGuiTabBar — ImGui'nin ic basligi

namespace tulpar::engine::app {

namespace {

const char *const k_panels[kLayoutPanelCount] = {kPanelSahne, kPanelGorunum, kPanelOzellikler, kPanelKaynaklar, kPanelDunya, kPanelKonsol};

// Modul durumu: host'un dockspace kimligi, son hata, son kayitta atlanan
// pencere sayisi. Ucu de kare icinde DEGISMEZ (yalniz duzen islemlerinde).
ImGuiID g_root = 0;
char g_err[192] = {0};
uint32_t g_skipped = 0;

uint32_t bits_of(float f) { uint32_t u; std::memcpy(&u, &f, 4); return u; }
bool feq(float a, float b) { return bits_of(a) == bits_of(b); } // BIT-tam (NaN dahil)

void clear_err() { g_err[0] = 0; }
// Hata YOLLARI SESSIZ DEGIL: sebep hem err'e (verildiyse) hem modul durumuna
// yazilir; satir numarali bicim .sahne okuyucusuyla ayni ("satir 7: ...").
bool fail(LayoutError *err, uint32_t line, const char *what) {
  if (line) std::snprintf(g_err, sizeof g_err, "satir %u: %s", line, what);
  else std::snprintf(g_err, sizeof g_err, "%s", what);
  if (err) {
    err->line = line;
    std::snprintf(err->msg, sizeof err->msg, "%s", g_err);
  }
  return false;
}

// --- yazici: bayt sayar, kapasite asilsa da uzunlugu dogru dondurur ----------
// (content/scene.cpp'deki Out'un ikizi; oradaki anonim isim alaninda oldugu
// icin paylasilamiyor, DAVRANIS birebir ayni tutuluyor.)
struct Out {
  char *buf;
  size_t cap, len = 0;
  void put(const char *s, size_t n) {
    if (buf && len < cap) {
      size_t room = cap - len, k = n < room ? n : room;
      std::memcpy(buf + len, s, k);
    }
    len += n;
  }
  void puts(const char *s) { put(s, std::strlen(s)); }
  void ch(char c) { put(&c, 1); }
  // En kisa, bit-tam geri okunan ondalik: %.6g'den %.9g'ye ilk tutan. strtof/
  // snprintf dogru yuvarlar (glibc, musl, bionic, Apple) -> deterministik.
  void num(float v) {
    char tmp[40];
    for (int p = 6; p <= 9; p++) {
      std::snprintf(tmp, sizeof tmp, "%.*g", p, (double)v);
      if (feq(std::strtof(tmp, nullptr), v)) break;
    }
    puts(tmp);
  }
  void i32(int32_t v) { char t[16]; std::snprintf(t, sizeof t, "%d", v); puts(t); }
  void u32(uint32_t v) { char t[16]; std::snprintf(t, sizeof t, "%u", v); puts(t); }
  void hex32(uint32_t v) { char t[16]; std::snprintf(t, sizeof t, "0x%08X", v); puts(t); }
  void str(const char *s) { ch('"'); puts(s); ch('"'); }
  void finish() {
    if (buf && cap) buf[len < cap ? len : cap - 1] = 0;
  }
};

// --- ayristirici (bicim .sahne ile ayni disiplinde) --------------------------
struct Tok {
  const char *s;
  size_t n;
  bool quoted;
};
constexpr size_t kMaxTok = 8;

// Satiri bosluklardan boler; "..." tek jeton. Donus: jeton sayisi;
// *bad = kapanmamis tirnak / fazla jeton.
size_t split_line(const char *line, size_t len, Tok *toks, bool *bad) {
  size_t n = 0, i = 0;
  *bad = false;
  while (i < len) {
    while (i < len && (line[i] == ' ' || line[i] == '\t' || line[i] == '\r')) i++;
    if (i >= len) break;
    if (line[i] == '#') break; // yorum
    if (n == kMaxTok) { *bad = true; return n; }
    if (line[i] == '"') {
      size_t j = i + 1;
      while (j < len && line[j] != '"') j++;
      if (j >= len) { *bad = true; return n; }
      toks[n++] = Tok{line + i + 1, j - i - 1, true};
      i = j + 1;
    } else {
      size_t j = i;
      while (j < len && line[j] != ' ' && line[j] != '\t' && line[j] != '\r') j++;
      toks[n++] = Tok{line + i, j - i, false};
      i = j;
    }
  }
  return n;
}
bool tok_is(const Tok &t, const char *kw) { return !t.quoted && std::strlen(kw) == t.n && std::memcmp(t.s, kw, t.n) == 0; }

struct Parser {
  LayoutError *err;
  uint32_t line = 0;
  bool fail_at(const char *what) { return fail(err, line, what); }
  bool num(const Tok &t, float *out) {
    char tmp[64];
    if (t.quoted || t.n == 0 || t.n >= sizeof tmp) return fail_at("sayi bekleniyor");
    std::memcpy(tmp, t.s, t.n);
    tmp[t.n] = 0;
    char *end = nullptr;
    const float v = std::strtof(tmp, &end);
    if (end != tmp + t.n || !std::isfinite(v)) return fail_at("gecersiz sayi");
    *out = v;
    return true;
  }
  bool i32(const Tok &t, int32_t *out) {
    char tmp[32];
    if (t.quoted || t.n == 0 || t.n >= sizeof tmp) return fail_at("tamsayi bekleniyor");
    std::memcpy(tmp, t.s, t.n);
    tmp[t.n] = 0;
    char *end = nullptr;
    const long v = std::strtol(tmp, &end, 10);
    if (end != tmp + t.n || v < -1 || v > 0x7FFFFFFFl) return fail_at("gecersiz tamsayi");
    *out = (int32_t)v;
    return true;
  }
  bool u32(const Tok &t, uint32_t *out, int base = 10) {
    char tmp[32];
    if (t.quoted || t.n == 0 || t.n >= sizeof tmp) return fail_at("tamsayi bekleniyor");
    std::memcpy(tmp, t.s, t.n);
    tmp[t.n] = 0;
    char *end = nullptr;
    const unsigned long v = std::strtoul(tmp, &end, base);
    if (end != tmp + t.n || tmp[0] == '-' || v > 0xFFFFFFFFul) return fail_at("gecersiz tamsayi");
    *out = (uint32_t)v;
    return true;
  }
  bool str(const Tok &t, char *out, size_t cap) {
    if (!t.quoted) return fail_at("tirnakli metin bekleniyor");
    if (t.n >= cap) return fail_at("ad cok uzun");
    std::memcpy(out, t.s, t.n);
    out[t.n] = 0;
    return true;
  }
};

// --- yasayan ImGui agaci ----------------------------------------------------
// Bir pencerenin dugum icindeki SEKME sirasi; sekme cubugu yoksa -1.
int32_t tab_order_of(const ImGuiDockNode *n, const ImGuiWindow *w) {
  if (!n->TabBar) return -1;
  for (int i = 0; i < n->TabBar->Tabs.Size; i++)
    if (n->TabBar->Tabs[i].Window == w) return i;
  return -1;
}

// On-sirali (preorder) gezinti — dosyadaki dugum sirasinin TANIMI budur.
// Ozyineleme derinligi agac derinligi kadar (<= kLayoutMaxNodes).
bool collect_nodes(const ImGuiDockNode *n, const ImGuiDockNode **arr, uint32_t *count) {
  if (*count >= kLayoutMaxNodes) return false;
  arr[(*count)++] = n;
  if (n->ChildNodes[0] && n->ChildNodes[1]) {
    if (!collect_nodes(n->ChildNodes[0], arr, count)) return false;
    if (!collect_nodes(n->ChildNodes[1], arr, count)) return false;
  }
  return true;
}

bool capture_rec(const ImGuiDockNode *n, int32_t parent, LayoutFile *out, LayoutError *err) {
  if (out->node_count >= kLayoutMaxNodes) {
    char b[80];
    std::snprintf(b, sizeof b, "dugum sayisi tavani asildi (%u)", kLayoutMaxNodes);
    return fail(err, 0, b);
  }
  const uint32_t idx = out->node_count++;
  LayoutNode &ln = out->nodes[idx];
  ln.parent = parent;
  // SizeRef, DockNodeTreeUpdatePosSize'in tek girdisi. Hic yazilmamissa (0)
  // gecerli Size'a duselim; o da 0 ise 1 — 0 bir dugum olcusu OLAMAZ
  // (DockBuilderSetNodeSize bunu dogrular) ve kirpilan deger dosyada GORUNUR.
  const float sw = n->SizeRef.x > 0.0f ? n->SizeRef.x : n->Size.x;
  const float sh = n->SizeRef.y > 0.0f ? n->SizeRef.y : n->Size.y;
  ln.w = sw > 0.0f ? sw : 1.0f;
  ln.h = sh > 0.0f ? sh : 1.0f;
  if (n->ChildNodes[0] && n->ChildNodes[1]) {
    ln.axis = (n->SplitAxis == ImGuiAxis_Y) ? 1u : 0u;
    const int32_t c0 = (int32_t)out->node_count;
    if (!capture_rec(n->ChildNodes[0], (int32_t)idx, out, err)) return false;
    const int32_t c1 = (int32_t)out->node_count;
    if (!capture_rec(n->ChildNodes[1], (int32_t)idx, out, err)) return false;
    out->nodes[idx].child[0] = c0;
    out->nodes[idx].child[1] = c1;
    return true;
  }
  ln.axis = 2;
  // Yaprak: panelleri topla, (sekme sirasi, ad) ikilisine gore sirala ve
  // KANONIK 0..n-1 sirasiyla yaz. Ham DockOrder yazilmaz; esit/-1 degerler
  // dosyayi belirlenimsiz yapardi.
  char names[kLayoutMaxWindows][kLayoutNameLen];
  int32_t ord[kLayoutMaxWindows];
  uint32_t cnt = 0;
  for (int i = 0; i < n->Windows.Size; i++) {
    const ImGuiWindow *w = n->Windows[i];
    if (!w || !w->Name) continue;
    // "Görünüm###Gorunum" gibi bir etikette kimlik ### sonrasidir (ImGui
    // kurali); dosyaya KIMLIK yazilir, etiket degil (bkz. kPanel*Label).
    const char *id_name = w->Name;
    if (const char *h = std::strstr(w->Name, "###")) id_name = h + 3;
    if (!layout_is_panel(id_name)) { g_skipped++; continue; }
    if (cnt >= kLayoutMaxWindows) {
      char b[80];
      std::snprintf(b, sizeof b, "pencere sayisi tavani asildi (%u)", kLayoutMaxWindows);
      return fail(err, 0, b);
    }
    std::snprintf(names[cnt], kLayoutNameLen, "%s", id_name);
    const int32_t o = tab_order_of(n, w);
    ord[cnt] = (o < 0) ? 0x7FFFFFFF : o; // sekme cubugu yok -> ada gore sirala
    cnt++;
  }
  if (cnt == 0) {
    char b[120];
    std::snprintf(b, sizeof b, "yaprak dugum %u penceresiz — bos dugum ImGui'de gorunmez olur ve duzen cokerdi", idx);
    return fail(err, 0, b);
  }
  // Ekleme siralamasi (n <= 24): (sira, ad) ikilisi TOPLAM bir sira verir.
  for (uint32_t i = 1; i < cnt; i++) {
    char nm[kLayoutNameLen];
    std::memcpy(nm, names[i], kLayoutNameLen);
    const int32_t o = ord[i];
    uint32_t j = i;
    while (j > 0 && (ord[j - 1] > o || (ord[j - 1] == o && std::strcmp(names[j - 1], nm) > 0))) {
      std::memcpy(names[j], names[j - 1], kLayoutNameLen);
      ord[j] = ord[j - 1];
      j--;
    }
    std::memcpy(names[j], nm, kLayoutNameLen);
    ord[j] = o;
  }
  for (uint32_t i = 0; i < cnt; i++) {
    if (out->window_count >= kLayoutMaxWindows) {
      char b[80];
      std::snprintf(b, sizeof b, "pencere sayisi tavani asildi (%u)", kLayoutMaxWindows);
      return fail(err, 0, b);
    }
    LayoutWindow &lw = out->windows[out->window_count++];
    // SESSIZ KIRPMA YASAK: ad 32 bayta sigmazsa dosyaya KISALMIS bir ad yazilir
    // ve o ad panel tablosunda bulunamayacagi icin dosya bir daha YUKLENEMEZ —
    // uzerine, hata kaydederken degil YUKLERKEN cikar. Burada adiyla duruyoruz.
    // (Bugunku bes panel adi kisa; bu koruma sozlesmeyi ayakta tutuyor.)
    {
      const size_t nlen = std::strlen(names[i]);
      if (nlen >= sizeof lw.name) {
        char b[160];
        std::snprintf(b, sizeof b, "pencere adi %u bayt, tavan %u (kirpilsaydi dosya yuklenemezdi)",
                      (unsigned)nlen, (unsigned)sizeof lw.name - 1u);
        return fail(err, 0, b);
      }
      std::memcpy(lw.name, names[i], nlen + 1);
    }
    lw.node = idx;
    lw.order = i;
  }
  return true;
}

// Kare/baglam on kosulu — DockBuilder DockSpace'i cagirir, o da GECERLI
// PENCEREYI okur: kare disinda cagrilirsa null isaretci uzerinden coker.
bool imgui_ready(LayoutError *err) {
  ImGuiContext *ctx = ImGui::GetCurrentContext();
  if (!ctx) return fail(err, 0, "ImGui baglami yok");
  if (!ctx->WithinFrameScope) return fail(err, 0, "ImGui karesi disinda cagrildi (NewFrame ile Render arasi olmali)");
  if (!(ctx->IO.ConfigFlags & ImGuiConfigFlags_DockingEnable)) return fail(err, 0, "kenetleme kapali (ImGuiConfigFlags_DockingEnable ayarlanmamis)");
  return true;
}

float trunc_min1(float v) {
  const float t = (float)(int)v; // IM_TRUNC ile ayni (pozitif degerler)
  return t < 1.0f ? 1.0f : t;
}

} // namespace

const char *layout_panel_name(uint32_t i) { return i < kLayoutPanelCount ? k_panels[i] : nullptr; }
bool layout_is_panel(const char *name) {
  if (!name) return false;
  for (uint32_t i = 0; i < kLayoutPanelCount; i++)
    if (std::strcmp(name, k_panels[i]) == 0) return true;
  return false;
}

void layout_set_dockspace_id(ImGuiID id) { g_root = id; }
ImGuiID layout_dockspace_id() { return g_root; }
const char *layout_last_error() { return g_err; }
uint32_t layout_skipped() { return g_skipped; }

bool layout_rect_equal(const LayoutRect &a, const LayoutRect &b) {
  return std::memcmp(a.name, b.name, kLayoutNameLen) == 0 && feq(a.x, b.x) && feq(a.y, b.y) && feq(a.w, b.w) && feq(a.h, b.h) &&
         a.node == b.node && a.flags == b.flags;
}

bool layout_file_equal(const LayoutFile &a, const LayoutFile &b) {
  if (a.version != b.version || a.root_id != b.root_id || !feq(a.root_w, b.root_w) || !feq(a.root_h, b.root_h)) return false;
  if (a.node_count != b.node_count || a.window_count != b.window_count) return false;
  for (uint32_t i = 0; i < a.node_count; i++) {
    const LayoutNode &x = a.nodes[i], &y = b.nodes[i];
    if (x.parent != y.parent || x.child[0] != y.child[0] || x.child[1] != y.child[1] || x.axis != y.axis) return false;
    if (!feq(x.w, y.w) || !feq(x.h, y.h)) return false;
  }
  for (uint32_t i = 0; i < a.window_count; i++) {
    const LayoutWindow &x = a.windows[i], &y = b.windows[i];
    if (std::memcmp(x.name, y.name, kLayoutNameLen) != 0 || x.node != y.node || x.order != y.order) return false;
  }
  return true;
}

// --- varsayilan yerlesim ----------------------------------------------------
// On-sirali dugumler:
//   0 kok      (Y: ust / alt)
//   1  ust     (X: sol / kalan)
//   2   sol    -> Sahne
//   3   kalan  (X: orta / sag)
//   4    orta  -> Gorunum
//   5    sag   -> Ozellikler
//   6  alt     -> Kaynaklar + Dunya (sekmeli)
// Olculer trunc'lanir: dosya tam sayilarla okunur ve ImGui zaten ImTrunc'luyor.
bool layout_default(ImGuiID dockspace_id, float w, float h, LayoutFile *out, LayoutError *err) {
  clear_err();
  if (!out) return fail(err, 0, "cikti yok");
  if (!std::isfinite(w) || !std::isfinite(h) || w < 64.0f || h < 64.0f)
    return fail(err, 0, "dockspace olcusu gecersiz (en az 64x64 olmali)");
  *out = LayoutFile{};
  out->version = kLayoutVersion;
  out->root_id = dockspace_id;
  out->root_w = trunc_min1(w);
  out->root_h = trunc_min1(h);
  const float W = out->root_w, H = out->root_h;
  const float bot = trunc_min1(H * kLayoutDefaultBottom);
  const float top = trunc_min1(H - bot);
  const float left = trunc_min1(W * kLayoutDefaultLeft);
  const float right = trunc_min1(W * kLayoutDefaultRight);
  const float rest = trunc_min1(W - left);
  const float mid = trunc_min1(W - left - right);
  out->node_count = 7;
  LayoutNode *n = out->nodes;
  n[0] = LayoutNode{-1, {1, 6}, 1, W, H};
  n[1] = LayoutNode{0, {2, 3}, 0, W, top};
  n[2] = LayoutNode{1, {-1, -1}, 2, left, top};
  n[3] = LayoutNode{1, {4, 5}, 0, rest, top};
  n[4] = LayoutNode{3, {-1, -1}, 2, mid, top};
  n[5] = LayoutNode{3, {-1, -1}, 2, right, top};
  n[6] = LayoutNode{0, {-1, -1}, 2, W, bot};
  struct Slot { const char *name; uint32_t node, order; };
  // Alt sektor sekmeli: Kaynaklar / Dunya / Konsol (Unity'nin Project+Console
  // sekmeleri gibi) — konsol ayri bir yer kaplamasin, gerektiginde one gelsin.
  const Slot slots[] = {{kPanelSahne, 2, 0},      {kPanelGorunum, 4, 0}, {kPanelOzellikler, 5, 0},
                        {kPanelKaynaklar, 6, 0}, {kPanelDunya, 6, 1},   {kPanelKonsol, 6, 2}};
  out->window_count = (uint32_t)(sizeof slots / sizeof slots[0]);
  for (uint32_t i = 0; i < out->window_count; i++) {
    // Kaynak BIZIM sabit tablomuz (k_panels), kirpma olamaz; yine de sinirli kopya.
    std::memcpy(out->windows[i].name, slots[i].name, std::strlen(slots[i].name) + 1);
    out->windows[i].node = slots[i].node;
    out->windows[i].order = slots[i].order;
  }
  return true;
}

// --- metin bicimi -----------------------------------------------------------
size_t layout_write(const LayoutFile &f, char *buf, size_t cap) {
  Out o{buf, cap};
  o.puts("duzen "); o.u32(f.version); o.ch('\n');
  o.puts("kok "); o.hex32(f.root_id); o.ch(' '); o.num(f.root_w); o.ch(' '); o.num(f.root_h); o.ch('\n');
  for (uint32_t i = 0; i < f.node_count; i++) {
    const LayoutNode &n = f.nodes[i];
    o.puts("dugum "); o.u32(i); o.ch(' '); o.i32(n.parent); o.ch(' ');
    o.ch(n.axis == 0 ? 'x' : n.axis == 1 ? 'y' : '-');
    o.ch(' '); o.num(n.w); o.ch(' '); o.num(n.h); o.ch('\n');
  }
  for (uint32_t i = 0; i < f.window_count; i++) {
    const LayoutWindow &w = f.windows[i];
    o.puts("pencere "); o.str(w.name); o.ch(' '); o.u32(w.node); o.ch(' '); o.u32(w.order); o.ch('\n');
  }
  o.finish();
  return o.len;
}

bool layout_parse(const char *text, size_t len, LayoutFile *out, LayoutError *err) {
  clear_err();
  if (!out) return fail(err, 0, "cikti yok");
  if (!text) return fail(err, 0, "metin yok");
  *out = LayoutFile{};
  Parser p{err};
  bool have_version = false, have_root = false;
  uint32_t child_used[kLayoutMaxNodes] = {0}; // her dugume kac cocuk bagli
  uint32_t node_line[kLayoutMaxNodes] = {0};
  size_t i = 0;
  while (i <= len) {
    size_t j = i;
    while (j < len && text[j] != '\n') j++;
    p.line++;
    Tok t[kMaxTok];
    bool bad = false;
    const size_t n = split_line(text + i, j - i, t, &bad);
    i = j + 1;
    if (bad) return p.fail_at("bozuk satir (kapanmamis tirnak ya da fazla jeton)");
    if (n == 0) { if (j >= len) break; else continue; }
    if (!have_version) {
      if (!tok_is(t[0], "duzen")) return p.fail_at("dosya 'duzen <surum>' ile baslamali");
      if (n != 2) return p.fail_at("duzen <surum>");
      uint32_t v = 0;
      if (!p.u32(t[1], &v)) return false;
      if (v != kLayoutVersion) {
        char b[80];
        std::snprintf(b, sizeof b, "surum %u desteklenmiyor (beklenen %u)", v, kLayoutVersion);
        return p.fail_at(b);
      }
      out->version = v;
      have_version = true;
    } else if (tok_is(t[0], "kok")) {
      if (have_root) return p.fail_at("'kok' iki kez");
      if (n != 4) return p.fail_at("kok <kimlik> <genislik> <yukseklik>");
      if (!p.u32(t[1], &out->root_id, 16)) return false;
      if (!p.num(t[2], &out->root_w) || !p.num(t[3], &out->root_h)) return false;
      if (out->root_w <= 0.0f || out->root_h <= 0.0f) return p.fail_at("kok olcusu pozitif olmali");
      have_root = true;
    } else if (tok_is(t[0], "dugum")) {
      if (!have_root) return p.fail_at("'dugum' 'kok' satirindan once");
      if (n != 6) return p.fail_at("dugum <indeks> <ebeveyn> <x|y|-> <genislik> <yukseklik>");
      if (out->node_count >= kLayoutMaxNodes) {
        char b[80];
        std::snprintf(b, sizeof b, "dugum sayisi tavani asildi (%u)", kLayoutMaxNodes);
        return p.fail_at(b);
      }
      uint32_t idx = 0;
      if (!p.u32(t[1], &idx)) return false;
      if (idx != out->node_count) return p.fail_at("dugum indeksleri 0'dan baslayip birer artmali (on-sirali)");
      int32_t parent = 0;
      if (!p.i32(t[2], &parent)) return false;
      LayoutNode &nd = out->nodes[idx];
      nd = LayoutNode{};
      nd.parent = parent;
      if (tok_is(t[3], "x")) nd.axis = 0;
      else if (tok_is(t[3], "y")) nd.axis = 1;
      else if (tok_is(t[3], "-")) nd.axis = 2;
      else return p.fail_at("bolunme 'x', 'y' ya da '-' olmali");
      if (!p.num(t[4], &nd.w) || !p.num(t[5], &nd.h)) return false;
      if (nd.w <= 0.0f || nd.h <= 0.0f) return p.fail_at("dugum olcusu pozitif olmali");
      node_line[idx] = p.line;
      if (idx == 0) {
        if (parent != -1) return p.fail_at("kok dugumun ebeveyni -1 olmali");
      } else {
        if (parent < 0 || (uint32_t)parent >= idx) return p.fail_at("ebeveyn dugum KENDINDEN ONCE tanimlanmis olmali (on-sirali)");
        LayoutNode &pn = out->nodes[parent];
        if (pn.axis == 2) return p.fail_at("ebeveyn yaprak olarak isaretli ('-'), cocugu olamaz");
        if (child_used[parent] >= 2) return p.fail_at("bir dugumun en fazla 2 cocugu olabilir");
        pn.child[child_used[parent]++] = (int32_t)idx;
      }
      out->node_count++;
    } else if (tok_is(t[0], "pencere")) {
      if (out->node_count == 0) return p.fail_at("'pencere' dugumlerden once");
      if (n != 4) return p.fail_at("pencere \"<ad>\" <dugum> <sira>");
      if (out->window_count >= kLayoutMaxWindows) {
        char b[80];
        std::snprintf(b, sizeof b, "pencere sayisi tavani asildi (%u)", kLayoutMaxWindows);
        return p.fail_at(b);
      }
      LayoutWindow &lw = out->windows[out->window_count];
      if (!p.str(t[1], lw.name, kLayoutNameLen)) return false;
      if (!layout_is_panel(lw.name)) {
        char b[120];
        std::snprintf(b, sizeof b, "bilinmeyen pencere \"%s\" (editorun panel tablosunda yok)", lw.name);
        return p.fail_at(b);
      }
      for (uint32_t k = 0; k < out->window_count; k++)
        if (std::strcmp(out->windows[k].name, lw.name) == 0) {
          char b[120];
          std::snprintf(b, sizeof b, "pencere \"%s\" iki kez", lw.name);
          return p.fail_at(b);
        }
      if (!p.u32(t[2], &lw.node) || !p.u32(t[3], &lw.order)) return false;
      if (lw.node >= out->node_count) return p.fail_at("pencere olmayan bir dugume baglanmis");
      if (out->nodes[lw.node].axis != 2) return p.fail_at("pencere ancak YAPRAK dugume baglanabilir");
      out->window_count++;
    } else return p.fail_at("bilinmeyen anahtar");
    if (j >= len) break;
  }
  if (!have_version) return fail(err, 0, "bos dosya: 'duzen' satiri yok");
  if (!have_root) return fail(err, 0, "'kok' satiri yok");
  if (out->node_count == 0) return fail(err, 0, "dugum yok");
  // Yapisal butunluk: bolme dugumunun TAM 2 cocugu olmali, her yaprakta en az
  // bir pencere. Ikisi de sessizce cokerdi (ImGui gorunmez dugumu tam ekran
  // yapar), bu yuzden satir numarasiyla reddediliyor.
  for (uint32_t k = 0; k < out->node_count; k++) {
    const LayoutNode &nd = out->nodes[k];
    if (nd.axis != 2 && child_used[k] != 2) {
      p.line = node_line[k];
      return p.fail_at("bolme dugumunun tam 2 cocugu olmali");
    }
    if (nd.axis == 2) {
      bool any = false;
      for (uint32_t m = 0; m < out->window_count && !any; m++) any = out->windows[m].node == k;
      if (!any) {
        p.line = node_line[k];
        return p.fail_at("yaprak dugum penceresiz");
      }
    }
  }
  return true;
}

// --- yasayan agaci oku ------------------------------------------------------
bool layout_capture(ImGuiID dockspace_id, LayoutFile *out, LayoutError *err) {
  clear_err();
  g_skipped = 0;
  if (!out) return fail(err, 0, "cikti yok");
  if (!ImGui::GetCurrentContext()) return fail(err, 0, "ImGui baglami yok");
  if (!dockspace_id) return fail(err, 0, "dockspace kimligi bilinmiyor (once layout_apply_default ya da layout_set_dockspace_id)");
  const ImGuiDockNode *root = ImGui::DockBuilderGetNode(dockspace_id);
  if (!root) {
    char b[96];
    std::snprintf(b, sizeof b, "dockspace dugumu yok (0x%08X)", dockspace_id);
    return fail(err, 0, b);
  }
  *out = LayoutFile{};
  out->version = kLayoutVersion;
  out->root_id = dockspace_id;
  out->root_w = root->Size.x > 0.0f ? root->Size.x : 1.0f;
  out->root_h = root->Size.y > 0.0f ? root->Size.y : 1.0f;
  return capture_rec(root, -1, out, err);
}

// --- agaci kur --------------------------------------------------------------
bool layout_apply(const LayoutFile &f, LayoutError *err) {
  clear_err();
  if (!imgui_ready(err)) return false;
  if (f.version != kLayoutVersion) return fail(err, 0, "duzen surumu desteklenmiyor");
  if (f.node_count == 0) return fail(err, 0, "duzende dugum yok");
  if (!(f.root_w > 0.0f) || !(f.root_h > 0.0f)) return fail(err, 0, "kok olcusu pozitif olmali");
  const ImGuiID root_id = g_root ? g_root : f.root_id;
  if (!root_id) return fail(err, 0, "dockspace kimligi bilinmiyor (layout_set_dockspace_id cagrilmali)");

  ImGui::DockBuilderRemoveNode(root_id); // eski agac + kenetli pencereler
  ImGui::DockBuilderAddNode(root_id, ImGuiDockNodeFlags_DockSpace);
  ImGuiDockNode *root = ImGui::DockBuilderGetNode(root_id);
  if (!root) return fail(err, 0, "dockspace dugumu yaratilamadi");
  // MERKEZ DUGUM YOK. DockSpace() kok dugumu CentralNode bayragiyla yaratir; o
  // bayrak bolmelerde bir cocuga miras kalir ve DockNodeTreeUpdatePosSize'i
  // "merkez artani alir" dalina sokar. Bayragi dusurunce TEK dal calisir
  // (oranla dagitim), yani her panelin dikdortgeni yalniz SizeRef'lerin ve kok
  // dikdortgeninin fonksiyonu olur — kaydet/yukle icin aradigimiz sey tam bu.
  // Yan fayda: pencere buyuyunce butun paneller oranli buyur, biri hepsini
  // yutmaz.
  root->SetLocalFlags(ImGuiDockNodeFlags_DockSpace);
  ImGui::DockBuilderSetNodeSize(root_id, ImVec2(f.root_w, f.root_h));

  ImGuiID ids[kLayoutMaxNodes] = {0};
  ids[0] = root_id;
  for (uint32_t i = 0; i < f.node_count; i++) {
    const LayoutNode &n = f.nodes[i];
    if (n.axis == 2) continue;
    if (!ids[i]) return fail(err, 0, "bolme dugumu ebeveyninden once geldi (dosya on-sirali degil)");
    const int32_t c0 = n.child[0], c1 = n.child[1];
    if (c0 < 0 || c1 < 0 || (uint32_t)c0 >= f.node_count || (uint32_t)c1 >= f.node_count) return fail(err, 0, "bolme dugumunun cocuklari eksik");
    const bool vertical = n.axis == 1;
    const float a = vertical ? f.nodes[c0].h : f.nodes[c0].w;
    const float b = vertical ? f.nodes[c1].h : f.nodes[c1].w;
    float ratio = (a + b) > 0.0f ? a / (a + b) : 0.5f;
    if (ratio < 0.05f) ratio = 0.05f;
    if (ratio > 0.95f) ratio = 0.95f;
    // Sol/Ust yonu: donen ilk kimlik ChildNodes[0], yani dosyadaki child[0].
    // (Oran burada yalnizca ARA durumu makul tutar; kesin olculer asagida
    // SizeRef olarak yeniden yaziliyor.)
    ImGuiID a_id = 0, b_id = 0;
    ImGui::DockBuilderSplitNode(ids[i], vertical ? ImGuiDir_Up : ImGuiDir_Left, ratio, &a_id, &b_id);
    if (!a_id || !b_id) return fail(err, 0, "dugum bolunemedi");
    ids[c0] = a_id;
    ids[c1] = b_id;
  }
  // Kesin olculer: Pos/Size her karede SizeRef'ten turetildigi icin BIT-TAM
  // ayni dikdortgenler ancak boyle geri gelir.
  for (uint32_t i = 1; i < f.node_count; i++) {
    if (!ids[i]) return fail(err, 0, "dugum agaca baglanamadi (dosya tutarsiz)");
    const float w = f.nodes[i].w > 0.0f ? f.nodes[i].w : 1.0f;
    const float h = f.nodes[i].h > 0.0f ? f.nodes[i].h : 1.0f;
    ImGui::DockBuilderSetNodeSize(ids[i], ImVec2(w, h));
  }
  for (uint32_t i = 0; i < f.window_count; i++) {
    const LayoutWindow &lw = f.windows[i];
    if (lw.node >= f.node_count || !ids[lw.node]) return fail(err, 0, "pencere olmayan bir dugume baglanmis");
    ImGui::DockBuilderDockWindow(lw.name, ids[lw.node]);
    // DockBuilderDockWindow DockOrder'i -1'e cekiyor ("goreli sirayi
    // korumuyoruz"), sekme cubugu da ayni karede beliren sekmeleri DockOrder'a
    // gore siraliyor: sirayi SONRA yaziyoruz, yoksa sekmeler alfabetik dizilir.
    const ImGuiID wid = ImHashStr(lw.name);
    if (ImGuiWindow *w = ImGui::FindWindowByID(wid)) w->DockOrder = (short)lw.order;
    else if (ImGuiWindowSettings *s = ImGui::FindWindowSettingsByID(wid)) s->DockOrder = (short)lw.order;
  }
  ImGui::DockBuilderFinish(root_id);
  g_root = root_id;
  return true;
}

bool layout_apply_default(ImGuiID dockspace_id, float w, float h) {
  LayoutFile f;
  LayoutError e{};
  if (!dockspace_id) return fail(nullptr, 0, "dockspace kimligi 0");
  if (!layout_default(dockspace_id, w, h, &f, &e)) return false;
  g_root = dockspace_id;
  return layout_apply(f, &e);
}

// --- dosya ------------------------------------------------------------------
bool layout_save(const char *path, LayoutError *err) {
  clear_err();
  if (!path || !*path) return fail(err, 0, "dosya yolu yok");
  LayoutFile f;
  if (!layout_capture(g_root, &f, err)) return false;
  char buf[kLayoutMaxBytes];
  const size_t need = layout_write(f, buf, sizeof buf);
  if (need + 1 > sizeof buf) {
    char b[96];
    std::snprintf(b, sizeof b, "duzen metni tampona sigmadi (%zu > %u bayt)", need, kLayoutMaxBytes - 1);
    return fail(err, 0, b);
  }
  FILE *fp = std::fopen(path, "wb");
  if (!fp) {
    char b[160];
    std::snprintf(b, sizeof b, "dosya yazilamadi: %s", path);
    return fail(err, 0, b);
  }
  const bool ok = std::fwrite(buf, 1, need, fp) == need;
  std::fclose(fp);
  if (!ok) {
    char b[160];
    std::snprintf(b, sizeof b, "yazma eksik: %s", path);
    return fail(err, 0, b);
  }
  return true;
}

bool layout_load(const char *path, LayoutError *err) {
  clear_err();
  if (!path || !*path) return fail(err, 0, "dosya yolu yok");
  FILE *fp = std::fopen(path, "rb");
  if (!fp) {
    char b[160];
    std::snprintf(b, sizeof b, "dosya acilamadi: %s", path);
    return fail(err, 0, b);
  }
  char buf[kLayoutMaxBytes];
  const size_t got = std::fread(buf, 1, sizeof buf - 1, fp);
  const bool more = std::fgetc(fp) != EOF; // tavani ASAN dosya sessizce kirpilmasin
  std::fclose(fp);
  if (more) {
    char b[96];
    std::snprintf(b, sizeof b, "dosya cok buyuk (tavan %u bayt)", kLayoutMaxBytes - 1);
    return fail(err, 0, b);
  }
  buf[got] = 0;
  LayoutFile f;
  if (!layout_parse(buf, got, &f, err)) return false;
  return layout_apply(f, err);
}

// --- olcum ------------------------------------------------------------------
uint32_t layout_snapshot(LayoutRect *out, uint32_t max) {
  clear_err();
  if (!out || max == 0) return 0;
  if (!ImGui::GetCurrentContext()) {
    fail(nullptr, 0, "ImGui baglami yok");
    return 0;
  }
  // Dugumleri ON-SIRALI diz: raporlanan indeks, ImGui'nin her yeniden kurusta
  // DEGISEN dugum kimligi degil, agactaki YER'dir — iki anlik goruntu ancak
  // boyle karsilastirilabilir.
  const ImGuiDockNode *nodes[kLayoutMaxNodes];
  uint32_t node_count = 0;
  if (g_root)
    if (const ImGuiDockNode *root = ImGui::DockBuilderGetNode(g_root))
      if (!collect_nodes(root, nodes, &node_count)) node_count = 0; // tavan asildi: indeks verme
  uint32_t n = 0;
  for (uint32_t i = 0; i < kLayoutPanelCount && n < max; i++) {
    ImGuiWindow *w = ImGui::FindWindowByName(k_panels[i]);
    if (!w) continue; // pencere henuz yaratilmadi (hic Begin edilmedi)
    LayoutRect r{};
    std::memcpy(r.name, k_panels[i], std::strlen(k_panels[i]) + 1);
    const ImGuiDockNode *dn = w->DockNode;
    if (dn) {
      r.x = dn->Pos.x; r.y = dn->Pos.y; r.w = dn->Size.x; r.h = dn->Size.y;
      r.flags |= kLayoutRectDocked;
      r.node = 0xFFFFFFFFu;
      for (uint32_t k = 0; k < node_count; k++)
        if (nodes[k] == dn) { r.node = k; break; }
    } else {
      r.x = w->Pos.x; r.y = w->Pos.y; r.w = w->Size.x; r.h = w->Size.y;
      r.node = 0xFFFFFFFFu;
    }
    out[n++] = r;
  }
  return n;
}

} // namespace tulpar::engine::app
