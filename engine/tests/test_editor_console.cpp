// editor_console kapilari — Konsol paneli: halka tamponu, siniflandirma,
// stdout/stderr yakalama ve panel cizimi.
//
// Her kapinin OLUMLU ve OLUMSUZ kontrolu var:
//  1) Halka tasmasi: 600 farkli satir 512'lik halkaya -> en eskiler DUSER ve
//     SAYILIR; kontrol: 100 satirda dusen 0 (yani sayac gercekten tasmayi
//     olcuyor, her yazmada artmiyor).
//  2) Duzey siniflandirmasi: "VUID-..." -> Hata; kontrol: duz bir bilgi satiri
//     Hata OLMAZ. "Validation Warning: [ VUID- ]" -> Uyari (sira kapisi).
//  3) Ardisik tekrar toplanir (xN); kontrol: araya baska satir girerse toplanmaz.
//  4) Yakalama ACIK: printf/fprintf satiri halkaya duser, duzeyi/etiketi dogru.
//     KONTROL: yakalama KAPALI iken ayni satir halkaya DUSMEZ (terminale gider).
//  5) Yarim satir '\n' gelene kadar BEKLER; kontrol: '\n' gelince tam satir.
//  6) Uzun satir DUSMEZ, gorunur "…" ile biter.
//  7) Suzgec + arama listeyi daraltir (olculen satir sayisi); kontrol: bos
//     suzgec hepsini gosterir.
//  8) Panel gercek yazi tipi + tema ile cizilir (PPM -> PNG, BAKILIR).
#include <cstdio>
#include <cstring>

#include <unistd.h>

#include "app/editor_console.hpp"
#include "app/editor_ui.hpp"
#include "tests/editor_probe.hpp"
#include "tests/test.hpp"

#include <imgui.h>

using namespace tulpar::engine;
using namespace tulpar::engine::test;

namespace {
constexpr const char *kOut = "/tmp/claude-1000/-mnt-veri-yazilim-Tulpar/1bc55e54-3de0-46ed-9830-7196bb6ac65e/scratchpad/agent-c2";
void out_path(char *buf, size_t n, const char *name) { std::snprintf(buf, n, "%s/%s", kOut, name); }

// Halkadaki SON kaydin metni (yoksa "").
const char *last_msg() {
  const uint32_t n = app::console_size();
  const app::ConsoleEntry *e = n ? app::console_at(n - 1) : nullptr;
  return e ? e->msg : "";
}
} // namespace

// --- 1) Halka tasmasi: en eski duser, DUSEN SAYILIR -------------------------
ENGINE_TEST(console_ring_overflow_drops_oldest_and_counts) {
  app::console_clear();
  const uint32_t drop0 = app::console_dropped();
  // KONTROL: kapasitenin altinda hicbir sey dusmemeli.
  for (uint32_t i = 0; i < 100; i++) app::console_log(app::ConsoleLevel::Bilgi, "test", "satir %u", i);
  const uint32_t drop_small = app::console_dropped() - drop0;
  const uint32_t size_small = app::console_size();
  // Simdi kapasiteyi 88 satir asalim.
  app::console_clear();
  const uint32_t drop1 = app::console_dropped();
  const uint32_t n = app::kConsoleCapacity + 88;
  for (uint32_t i = 0; i < n; i++) app::console_log(app::ConsoleLevel::Bilgi, "test", "tasma %u", i);
  const uint32_t dropped = app::console_dropped() - drop1;
  const app::ConsoleEntry *oldest = app::console_at(0);
  const app::ConsoleEntry *newest = app::console_at(app::console_size() - 1);
  char want_old[64], want_new[64];
  std::snprintf(want_old, sizeof want_old, "tasma %u", 88u);
  std::snprintf(want_new, sizeof want_new, "tasma %u", n - 1);
  std::printf("    [bilgi] kapasite %u: %u satir yazildi, halkada %u, dusen %u (kontrol: 100 satirda dusen %u, halkada %u)\n",
              app::kConsoleCapacity, n, app::console_size(), dropped, drop_small, size_small);
  std::printf("    [bilgi] en eski \"%s\" (beklenen \"%s\"), en yeni \"%s\"\n", oldest ? oldest->msg : "-", want_old, newest ? newest->msg : "-");
  CHECK(drop_small == 0 && size_small == 100); // KONTROL
  CHECK(dropped == 88);
  CHECK(app::console_size() == app::kConsoleCapacity);
  CHECK(oldest && std::strcmp(oldest->msg, want_old) == 0);
  CHECK(newest && std::strcmp(newest->msg, want_new) == 0);
  // Duzey sayaclari halkayla tutarli kalmali (dusen kayit sayactan da duser).
  const app::ConsoleCounts c = app::console_counts();
  CHECK(c.total() == app::console_size());
  app::console_clear();
}

// --- 2) Duzey ve etiket siniflandirmasi -------------------------------------
ENGINE_TEST(console_classify_level_and_tag) {
  struct Case {
    const char *line;
    bool stderr_;
    app::ConsoleLevel level;
    const char *tag;
  };
  const Case cases[] = {
      {"Validation Error: [ VUID-vkCmdDrawIndexed-subpass-02685 ] Object 0: ...", true, app::ConsoleLevel::Hata, "vulkan"},
      {"VUID-vkCmdDraw-None-08600(ERROR / SPEC)", true, app::ConsoleLevel::Hata, "vulkan"},
      {"Validation Warning: [ VUID-VkSamplerCreateInfo-maxLod ] Arm ...", true, app::ConsoleLevel::Uyari, "vulkan"},
      {"UNASSIGNED-BestPractices-vkCreateDevice-specialuse", true, app::ConsoleLevel::Uyari, "vulkan"},
      {"[engine_editor] sahne yuklendi: 12 varlik", false, app::ConsoleLevel::Bilgi, "editor"},
      {"[engine] GPU: llvmpipe (Vulkan 1.3)", false, app::ConsoleLevel::Bilgi, "motor"},
      {"sahne editor.sahne: 14. satir bilinmeyen anahtar", true, app::ConsoleLevel::Uyari, "sahne"},
      {"[engine_editor] HATA: golge atlasi yaratilamadi", false, app::ConsoleLevel::Hata, "editor"},
      {"[engine_editor] UYARI: bloom kapali", false, app::ConsoleLevel::Uyari, "editor"},
  };
  uint32_t ok_level = 0, ok_tag = 0;
  for (const Case &c : cases) {
    const app::ConsoleLevel l = app::console_classify_level(c.line, c.stderr_);
    const char *t = app::console_classify_tag(c.line);
    if (l == c.level) ok_level++;
    else std::printf("    [bilgi] duzey uyusmadi: \"%.48s\" -> %d (beklenen %d)\n", c.line, (int)l, (int)c.level);
    if (std::strcmp(t, c.tag) == 0) ok_tag++;
    else std::printf("    [bilgi] etiket uyusmadi: \"%.48s\" -> %s (beklenen %s)\n", c.line, t, c.tag);
  }
  const uint32_t n = (uint32_t)(sizeof cases / sizeof cases[0]);
  std::printf("    [bilgi] siniflandirma: duzey %u/%u, etiket %u/%u\n", ok_level, n, ok_tag, n);
  CHECK(ok_level == n);
  CHECK(ok_tag == n);
  // KONTROL: siradan bir satir Hata DEGILDIR (yoksa "VUID -> Hata" olcumu bos).
  CHECK(app::console_classify_level("[engine_editor] 60 kare cizildi", false) == app::ConsoleLevel::Bilgi);
  CHECK(app::console_classify_level("kaynak yuklendi: checker_cube.gltf", false) != app::ConsoleLevel::Hata);
  // KONTROL: "Validation Warning" satirinda VUID gecmesine RAGMEN Uyari kalir.
  CHECK(app::console_classify_level("Validation Warning: [ VUID-x ]", true) == app::ConsoleLevel::Uyari);
}

// --- 3) Ardisik tekrar toplanir --------------------------------------------
ENGINE_TEST(console_collapses_consecutive_duplicates) {
  app::console_clear();
  for (uint32_t i = 0; i < 5; i++) app::console_log(app::ConsoleLevel::Uyari, "motor", "ayni satir");
  const uint32_t n_collapsed = app::console_size();
  const app::ConsoleEntry *e = app::console_at(0);
  const uint32_t rep = e ? e->repeat : 0;
  // KONTROL: araya baska bir satir girerse toplanmaz (A B A B -> 4 kayit).
  app::console_clear();
  for (uint32_t i = 0; i < 2; i++) {
    app::console_log(app::ConsoleLevel::Bilgi, "motor", "A");
    app::console_log(app::ConsoleLevel::Bilgi, "motor", "B");
  }
  const uint32_t n_alt = app::console_size();
  std::printf("    [bilgi] tekrar toplama: 5 ayni satir -> %u kayit (xN = %u); KONTROL A/B/A/B -> %u kayit\n", n_collapsed, rep, n_alt);
  CHECK(n_collapsed == 1 && rep == 5);
  CHECK(n_alt == 4);
  app::console_clear();
}

// --- 4/5/6) stdout/stderr yakalama ------------------------------------------
ENGINE_TEST(console_capture_reads_stdout_and_stderr) {
  // KONTROL ONCE: yakalama KAPALIYKEN yazilan satir halkaya DUSMEZ.
  app::console_clear();
  std::printf("[engine] yakalama kapali iken yazildi\n");
  std::fflush(stdout);
  const uint32_t off_size = app::console_size();

  app::ConsoleCapture cap;
  cap.echo = false; // kapida terminali kirletme (yakalanan satir yutulur)
  if (!app::console_capture_begin(&cap)) {
    std::printf("    [bilgi] yakalama acilamadi: %s\n", cap.err_msg);
    skip("POSIX boru/dup2 yok");
    return;
  }
  std::printf("[engine] yakalanan bilgi satiri\n");
  std::fprintf(stderr, "Validation Error: [ VUID-vkCmdDraw-None-08600 ] test\n");
  std::fprintf(stderr, "yarim satir gelecek");           // '\n' YOK: beklemeli
  app::console_capture_drain();
  const uint32_t after_two = app::console_size();
  const bool partial_held = std::strcmp(last_msg(), "yarim satir gelecek") != 0;
  std::fprintf(stderr, " ve tamamlandi\n");              // simdi tamamlanir
  app::console_capture_drain();
  const uint32_t after_three = app::console_size();
  const char *third = last_msg();
  // Uzun satir: DUSMEZ, gorunur "…" ile biter.
  static char longline[900];
  for (size_t i = 0; i < sizeof longline - 2; i++) longline[i] = (char)('a' + (i % 26));
  longline[sizeof longline - 2] = '\n';
  longline[sizeof longline - 1] = 0;
  std::fputs(longline, stdout);
  app::console_capture_drain();
  const app::ConsoleEntry *le = app::console_size() ? app::console_at(app::console_size() - 1) : nullptr;
  const bool ell = le && le->truncated && std::strlen(le->msg) < app::kConsoleMsgLen &&
                   std::strcmp(le->msg + std::strlen(le->msg) - 3, "\xE2\x80\xA6") == 0;
  const uint32_t lines = cap.lines, bytes = cap.bytes, longs = cap.long_lines;
  app::console_capture_end(&cap);

  const app::ConsoleEntry *e0 = app::console_at(0);
  const app::ConsoleEntry *e1 = app::console_at(1);
  std::printf("    [bilgi] yakalama: %u satir, %u bayt, %u uzun satir; KONTROL kapaliyken halka %u kayit\n", lines, bytes, longs, off_size);
  std::printf("    [bilgi] 1. kayit [%s] duzey %d \"%.48s\" | 2. kayit [%s] duzey %d \"%.48s\"\n", e0 ? e0->tag : "-", e0 ? (int)e0->level : -1,
              e0 ? e0->msg : "-", e1 ? e1->tag : "-", e1 ? (int)e1->level : -1, e1 ? e1->msg : "-");
  std::printf("    [bilgi] yarim satir beklendi mi: %s; tamamlanmis satir: \"%.48s\"; uzun satir kirpildi mi: %s (%zu bayt)\n",
              partial_held ? "evet" : "HAYIR", third, ell ? "evet" : "HAYIR", le ? std::strlen(le->msg) : 0u);
  CHECK(off_size == 0); // KONTROL: yakalama kapaliyken halka bos kaldi
  CHECK(e0 && e0->level == app::ConsoleLevel::Bilgi && std::strcmp(e0->tag, "motor") == 0);
  CHECK(e0 && std::strstr(e0->msg, "yakalanan bilgi satiri") != nullptr);
  CHECK(e1 && e1->level == app::ConsoleLevel::Hata && std::strcmp(e1->tag, "vulkan") == 0);
  CHECK(after_two == 2 && partial_held);           // yarim satir BEKLEDI
  CHECK(after_three == 3);                         // '\n' gelince tek satir oldu
  CHECK(std::strcmp(third, "yarim satir gelecek ve tamamlandi") == 0);
  CHECK(ell && longs == 1);                        // uzun satir dusmedi, "…" ile bitti
  CHECK(lines == 4 && bytes > 900);
  app::console_clear();
}

// --- 7) Suzgec + arama listeyi daraltir -------------------------------------
ENGINE_TEST(console_filter_and_search_narrow_list) {
  app::console_clear();
  for (uint32_t i = 0; i < 6; i++) app::console_log(app::ConsoleLevel::Bilgi, "motor", "bilgi %u", i);
  for (uint32_t i = 0; i < 3; i++) app::console_log(app::ConsoleLevel::Uyari, "sahne", "uyari %u", i);
  for (uint32_t i = 0; i < 2; i++) app::console_log(app::ConsoleLevel::Hata, "vulkan", "VUID hatasi %u", i);
  static uint32_t idx[app::kConsoleCapacity];
  app::ConsoleView v; // KONTROL: bos suzgec + bos arama -> HEPSI
  const uint32_t all = app::console_filtered(v, idx, app::kConsoleCapacity);
  v.show[(uint32_t)app::ConsoleLevel::Bilgi] = false;
  const uint32_t no_info = app::console_filtered(v, idx, app::kConsoleCapacity);
  v.show[(uint32_t)app::ConsoleLevel::Bilgi] = true;
  std::snprintf(v.search, sizeof v.search, "vuid"); // harf duyarsiz
  const uint32_t by_text = app::console_filtered(v, idx, app::kConsoleCapacity);
  std::snprintf(v.search, sizeof v.search, "sahne"); // etikette arar
  const uint32_t by_tag = app::console_filtered(v, idx, app::kConsoleCapacity);
  std::snprintf(v.search, sizeof v.search, "boyle_bir_sey_yok");
  const uint32_t none = app::console_filtered(v, idx, app::kConsoleCapacity);
  const app::ConsoleCounts c = app::console_counts();
  std::printf("    [bilgi] suzgec: hepsi %u (bilgi %u uyari %u hata %u), bilgisiz %u, \"vuid\" %u, etiket \"sahne\" %u, eslesmeyen %u\n", all,
              c.bilgi(), c.uyari(), c.hata(), no_info, by_text, by_tag, none);
  CHECK(all == 11); // KONTROL
  CHECK(no_info == 5);
  CHECK(by_text == 2);
  CHECK(by_tag == 3);
  CHECK(none == 0);
  app::console_clear();
}

// --- 8) Panel cizimi (GORSEL) -----------------------------------------------
namespace {
struct PanelCtx {
  app::ConsoleView view;
  bool select_first_error = false;
};
void draw_console_probe(void *ctx, uint32_t frame) {
  auto *c = static_cast<PanelCtx *>(ctx);
  ImGuiIO &io = ImGui::GetIO();
  ImGui::SetNextWindowPos(ImVec2(0, 0));
  ImGui::SetNextWindowSize(io.DisplaySize);
  ImGui::Begin("Konsol", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse);
  app::console_panel(c->view);
  ImGui::End();
  (void)frame;
}
void seed_console() {
  app::console_clear();
  app::console_set_frame(12);
  app::console_log(app::ConsoleLevel::Bilgi, "motor", "GPU: NVIDIA GeForce RTX (Vulkan 1.4)");
  app::console_log(app::ConsoleLevel::Bilgi, "editor", "sahne y\xC3\xBCklendi: editor.sahne (14 varl\xC4\xB1k, 3 kaynak)");
  app::console_set_frame(13);
  app::console_log(app::ConsoleLevel::Uyari, "sahne", "kaynak y\xC3\xBCklenemedi: yok_boyle_bir_model.gltf");
  app::console_set_frame(14);
  for (uint32_t i = 0; i < 37; i++) // ardisik tekrar: tek satir + xN rozeti
    app::console_log(app::ConsoleLevel::Uyari, "vulkan", "UNASSIGNED-BestPractices-vkBindMemory-small-dedicated-allocation");
  app::console_set_frame(15);
  app::console_log(app::ConsoleLevel::Hata, "vulkan",
                   "Validation Error: [ VUID-vkCmdDrawIndexed-subpass-02685 ] Object 0: handle = 0x5f3c20, type = "
                   "VK_OBJECT_TYPE_COMMAND_BUFFER; | MessageID = 0x1a3d2f57 | vkCmdDrawIndexed(): the current subpass index");
  app::console_log(app::ConsoleLevel::Bilgi, "motor", "derlendi: editor.sahneb (4212 bayt, 9 cizim)");
  app::console_set_frame(16);
  app::console_log(app::ConsoleLevel::Hata, "sahne", "KAYDEDILEMEDI: dosya a\xC3\xA7\xC4\xB1lamad\xC4\xB1 (izin yok)");
}
} // namespace

ENGINE_TEST(console_panel_draws_mixed_levels) {
  seed_console();
  PanelCtx c;
  // Ayrinti bolmesi de goruntuye girsin: ilk HATA kaydini sec (secim halka
  // kaysa da sabit kalan seq ile tutulur, indeksle degil).
  for (uint32_t i = 0; i < app::console_size(); i++) {
    const app::ConsoleEntry *e = app::console_at(i);
    if (e && e->level == app::ConsoleLevel::Hata) { c.view.selected = (int32_t)e->seq; c.view.expanded = true; break; }
  }
  EditorProbe p;
  p.width = 760;
  p.height = 420;
  p.frames = 3;
  char path[512];
  out_path(path, sizeof path, "konsol_paneli.ppm");
  p.out_ppm = path;
  p.draw = draw_console_probe;
  p.ctx = &c;
  const ProbeStatus st = editor_probe_render(p);
  if (st == ProbeStatus::NoVulkan) { skip("Vulkan yok"); app::console_clear(); return; }
  if (st != ProbeStatus::Ok) std::printf("    [bilgi] sonda: %s\n", p.err);
  CHECK(st == ProbeStatus::Ok);
  if (st != ProbeStatus::Ok) { app::console_clear(); return; }
  std::printf("    [bilgi] panel: %u satir suzgecten gecti, clipper %u satir cizdi, %u vertex, halkada %u kayit\n", c.view.shown, c.view.clipped,
              p.vertices, app::console_size());
  CHECK(c.view.shown == app::console_size());
  CHECK(c.view.clipped > 0);
  CHECK(p.vertices > 500); // bos panel degil

  // KONTROL: yalniz Hata acik birakilinca cizilen satir sayisi DUSER ve
  // vertex sayisi da duser — suzgec gercekten listeyi daraltiyor.
  PanelCtx c2;
  c2.view.show[(uint32_t)app::ConsoleLevel::Bilgi] = false;
  c2.view.show[(uint32_t)app::ConsoleLevel::Uyari] = false;
  EditorProbe p2 = EditorProbe{};
  p2.width = 760;
  p2.height = 420;
  p2.frames = 3;
  char path2[512];
  out_path(path2, sizeof path2, "konsol_paneli_suzgecli.ppm");
  p2.out_ppm = path2;
  p2.draw = draw_console_probe;
  p2.ctx = &c2;
  const ProbeStatus st2 = editor_probe_render(p2);
  if (st2 == ProbeStatus::Ok) {
    std::printf("    [bilgi] KONTROL (yalniz Hata): %u satir, %u vertex (hepsi: %u satir, %u vertex)\n", c2.view.shown, p2.vertices, c.view.shown,
                p.vertices);
    CHECK(c2.view.shown == 2 && c2.view.shown < c.view.shown);
    CHECK(p2.vertices < p.vertices);
  }
  app::console_clear();
}
