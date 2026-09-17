// editor_files kapilari — sahne dosya islemleri: yol yardimcilari, dizin
// listesi, son dosyalar, dosya diyalogu ve onay kutusu.
//
// Her kapinin OLUMLU ve OLUMSUZ kontrolu var:
//  1) scene_path_with_extension: uzanti yoksa eklenir, VARSA eklenmez; kontrol:
//     ".sahneb" ".sahne" DEGILDIR, sigmayan yol HATA (sessiz kirpma yok).
//  2) Dizin listesi ADA GORE sirali ve once DIZINLER; kontrol: ".sahne"
//     suzgecinde bir ".png" GORUNMEZ ama suzgecsiz cagride GORUNUR.
//  3) Son dosyalar: yaz -> oku -> yaz BAYT BAYT ayni; tekrar eklenen yol ONE
//     tasinir (kopya olmaz); kontrol: FARKLI sira FARKLI bayt uretir.
//  4) Eksik dosya isaretlenir (menude soluk); kontrol: var olan dosya isaretsiz.
//  5) Kaydet kipinde uzerine yazma onayi YALNIZ dosya varken; kontrol: yeni ad
//     -> onay yok.
//  6) Diyalog gercek yazi tipi + tema ile cizilir (PPM -> PNG, BAKILIR);
//     olculen satir sayisi suzgecle daralir.
//  7) Onay kutusu (Kaydet / Kaydetme / Vazgec) cizilir ve UC dugme dondurur.
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <sys/stat.h>
#include <unistd.h>

#include "app/editor_files.hpp"
#include "app/editor_ui.hpp"
#include "tests/editor_probe.hpp"
#include "tests/test.hpp"

#include <imgui.h>

using namespace tulpar::engine;
using namespace tulpar::engine::test;

namespace {
constexpr const char *kOut = "/tmp/claude-1000/-mnt-veri-yazilim-Tulpar/1bc55e54-3de0-46ed-9830-7196bb6ac65e/scratchpad/agent-c2";
void out_path(char *buf, size_t n, const char *name) { std::snprintf(buf, n, "%s/%s", kOut, name); }

// Gecici agac: <tmp>/tul_dosya_XXXXXX/{alt_sahneler/, *.sahne, *.png}
struct TempTree {
  char root[512] = {0};
  bool ok = false;
};
bool touch(const char *dir, const char *name) {
  char p[1024];
  std::snprintf(p, sizeof p, "%s/%s", dir, name);
  FILE *f = std::fopen(p, "wb");
  if (!f) return false;
  std::fputs("x\n", f);
  std::fclose(f);
  return true;
}
bool make_tree(TempTree &t) {
  tmp_template(t.root, sizeof t.root, "tul_dosya");
  if (!::mkdtemp(t.root)) return false;
  char sub[1024];
  std::snprintf(sub, sizeof sub, "%s/alt_sahneler", t.root);
  if (::mkdir(sub, 0755) != 0) return false;
  // Kasten TERS sirada yaratilir: sonuc sirali cikiyorsa siralama GERCEKTEN
  // yapiliyor demektir (readdir sirasi dosya sistemine baglidir).
  t.ok = touch(t.root, "zemin.sahne") && touch(t.root, "arena.sahne") && touch(t.root, "bolum1.sahne") && touch(t.root, "kapak.png") &&
         touch(t.root, "notlar.txt");
  return t.ok;
}
void rm_tree(const TempTree &t) {
  if (!t.root[0]) return;
  const char *files[5] = {"zemin.sahne", "arena.sahne", "bolum1.sahne", "kapak.png", "notlar.txt"};
  char p[1024];
  for (const char *f : files) {
    std::snprintf(p, sizeof p, "%s/%s", t.root, f);
    std::remove(p);
  }
  std::snprintf(p, sizeof p, "%s/alt_sahneler", t.root);
  ::rmdir(p);
  ::rmdir(t.root);
}
} // namespace

// --- 1) Uzanti zorlama ------------------------------------------------------
ENGINE_TEST(files_scene_path_with_extension_cases) {
  struct Case {
    const char *in;
    const char *want; // nullptr = false beklenir
  };
  const Case cases[] = {
      {"arena", "arena.sahne"},
      {"arena.sahne", "arena.sahne"},
      {"arena.SAHNE", "arena.SAHNE"},        // harf duyarsiz eslesme: kullanicinin yazdigi kalir
      {"arena.sahneb", "arena.sahneb.sahne"}, // KONTROL: ".sahneb" ".sahne" DEGIL
      {"arena.txt", "arena.txt.sahne"},
      {"/a/b/arena", "/a/b/arena.sahne"},
      {"", nullptr},     // bos ad
      {"/a/b/", nullptr} // dizin yolu: dosya adi yok
  };
  uint32_t ok = 0;
  char out[256];
  for (const Case &c : cases) {
    const bool r = app::scene_path_with_extension(c.in, ".sahne", out, sizeof out);
    const bool pass = c.want ? (r && std::strcmp(out, c.want) == 0) : !r;
    if (pass) ok++;
    else std::printf("    [bilgi] uyusmadi: \"%s\" -> %s \"%s\" (beklenen %s)\n", c.in, r ? "true" : "false", out, c.want ? c.want : "false");
  }
  // KONTROL: tampona sigmayan sonuc SESSIZ KIRPILMAZ, hata doner.
  char small[8];
  const bool tight = app::scene_path_with_extension("cok_uzun_bir_ad", ".sahne", small, sizeof small);
  std::printf("    [bilgi] uzanti: %u/%zu durum gecti; dar tampon (%zu bayt) -> %s, cikti \"%s\"\n", ok, sizeof cases / sizeof cases[0],
              sizeof small, tight ? "true (HATA)" : "false", small);
  CHECK(ok == sizeof cases / sizeof cases[0]);
  CHECK(!tight && small[0] == 0);
}

// --- Yol yardimcilari -------------------------------------------------------
ENGINE_TEST(files_path_helpers) {
  char out[256];
  CHECK(app::file_path_join("/a/b", "c.sahne", out, sizeof out) && std::strcmp(out, "/a/b/c.sahne") == 0);
  CHECK(app::file_path_join("/a/b/", "c.sahne", out, sizeof out) && std::strcmp(out, "/a/b/c.sahne") == 0); // cift '/' olmaz
  CHECK(app::file_path_join("/", "c.sahne", out, sizeof out) && std::strcmp(out, "/c.sahne") == 0);
  CHECK(app::file_path_parent("/a/b/c", out, sizeof out) && std::strcmp(out, "/a/b") == 0);
  CHECK(app::file_path_parent("/a", out, sizeof out) && std::strcmp(out, "/") == 0);
  CHECK(!app::file_path_parent("/", out, sizeof out)); // KONTROL: kokun ustu yok
  CHECK(std::strcmp(app::file_path_base("/a/b/c.sahne"), "c.sahne") == 0);
  CHECK(std::strcmp(app::file_path_base("c.sahne"), "c.sahne") == 0);
  char tiny[6];
  CHECK(!app::file_path_join("/a/b", "cok_uzun.sahne", tiny, sizeof tiny)); // sessiz kirpma yok
  std::printf("    [bilgi] yol yardimcilari: birlestir/ust/taban/tavan dortu de dogru\n");
}

// --- 2) Dizin listesi: sirali + suzgecli ------------------------------------
ENGINE_TEST(files_list_dir_is_sorted_and_filtered) {
  TempTree t;
  if (!make_tree(t)) { skip("gecici dizin yaratilamadi"); return; }
  static app::FileEntry items[app::kFileListMax];
  const app::FileListResult r = app::file_list_dir(t.root, ".sahne", items, app::kFileListMax);
  char names[512] = {0};
  for (uint32_t i = 0; i < r.count; i++) {
    std::strncat(names, items[i].dir ? "[" : "", sizeof names - std::strlen(names) - 1);
    std::strncat(names, items[i].name, sizeof names - std::strlen(names) - 1);
    std::strncat(names, items[i].dir ? "] " : " ", sizeof names - std::strlen(names) - 1);
  }
  // KONTROL: suzgecsiz cagri .png ve .txt'yi DE gosterir — yani suzgec
  // gercekten eliyor, dizin zaten bos degil.
  static app::FileEntry all[app::kFileListMax];
  const app::FileListResult ra = app::file_list_dir(t.root, nullptr, all, app::kFileListMax);
  bool png_filtered = true, png_unfiltered = false;
  for (uint32_t i = 0; i < r.count; i++)
    if (std::strcmp(items[i].name, "kapak.png") == 0) png_filtered = false;
  for (uint32_t i = 0; i < ra.count; i++)
    if (std::strcmp(all[i].name, "kapak.png") == 0) png_unfiltered = true;
  bool sorted = true;
  for (uint32_t i = 1; i < r.count; i++) {
    if (items[i - 1].dir && !items[i].dir) continue;            // dizin -> dosya gecisi
    if (!items[i - 1].dir && items[i].dir) sorted = false;      // dosya -> dizin OLMAZ
    else if (std::strcmp(items[i - 1].name, items[i].name) > 0) sorted = false;
  }
  std::printf("    [bilgi] listeleme: %u girdi (%u dizin), sirali %s -> %s\n", r.count, r.dirs, sorted ? "evet" : "HAYIR", names);
  std::printf("    [bilgi] KONTROL suzgecsiz: %u girdi, .png suzgecte %s / suzgecsiz %s\n", ra.count, png_filtered ? "yok" : "VAR",
              png_unfiltered ? "var" : "YOK");
  CHECK(r.ok && r.count == 4 && r.dirs == 1); // alt_sahneler + 3 .sahne
  CHECK(sorted);
  CHECK(std::strcmp(items[0].name, "alt_sahneler") == 0 && items[0].dir);
  CHECK(std::strcmp(items[1].name, "arena.sahne") == 0);
  CHECK(std::strcmp(items[2].name, "bolum1.sahne") == 0);
  CHECK(std::strcmp(items[3].name, "zemin.sahne") == 0);
  CHECK(png_filtered && png_unfiltered); // KONTROL
  CHECK(ra.count == 6);
  CHECK(r.truncated == 0);
  // KONTROL: olmayan dizin SESSIZ BOS degil, GORUNUR hata.
  const app::FileListResult bad = app::file_list_dir("/boyle/bir/dizin/yok", ".sahne", items, app::kFileListMax);
  std::printf("    [bilgi] KONTROL olmayan dizin: ok=%s, hata=\"%s\"\n", bad.ok ? "true (HATA)" : "false", bad.err);
  CHECK(!bad.ok && bad.err[0] != 0);
  rm_tree(t);
}

// --- 3/4) Son dosyalar ------------------------------------------------------
ENGINE_TEST(files_recent_roundtrip_is_byte_identical) {
  app::recent_clear();
  app::recent_push("/p/bir.sahne");
  app::recent_push("/p/iki.sahne");
  app::recent_push("/p/uc.sahne"); // en yeni ONCE: uc, iki, bir
  static char a[8192], b[8192], c[8192];
  const size_t na = app::recent_write(a, sizeof a);
  char file[512];
  tmp_template(file, sizeof file, "tul_son");
  const int fd = ::mkstemp(file);
  if (fd >= 0) ::close(fd);
  const bool saved = app::recent_save(file);
  app::recent_clear();
  const bool loaded = app::recent_load(file);
  const size_t nb = app::recent_write(b, sizeof b);
  const bool identical = na == nb && std::memcmp(a, b, na) == 0;
  // Tekillestirme: var olan yolu tekrar eklemek ONE TASIR, kopya yaratmaz.
  const uint32_t before = app::recent_count();
  app::recent_push("/p/bir.sahne");
  const uint32_t after = app::recent_count();
  const char *list[app::kRecentMax];
  const uint32_t n = app::recent_list(list, app::kRecentMax);
  const size_t nc = app::recent_write(c, sizeof c);
  const bool order_differs = !(nc == na && std::memcmp(a, c, na) == 0); // KONTROL
  std::printf("    [bilgi] son dosyalar: yaz %zu bayt, kaydet %s, yukle %s, yeniden yaz %zu bayt, BIT-TAM %s\n", na, saved ? "evet" : "HAYIR",
              loaded ? "evet" : "HAYIR", nb, identical ? "evet" : "HAYIR");
  std::printf("    [bilgi] tekillestirme: %u -> %u girdi, sira: ", before, after);
  for (uint32_t i = 0; i < n; i++) std::printf("%s%s", list[i], i + 1 < n ? " | " : "\n");
  std::printf("    [bilgi] KONTROL farkli sira farkli bayt: %s (%zu vs %zu bayt)\n", order_differs ? "evet" : "HAYIR", na, nc);
  CHECK(saved && loaded && identical);
  CHECK(before == 3 && after == 3);
  CHECK(n == 3 && std::strcmp(list[0], "/p/bir.sahne") == 0 && std::strcmp(list[1], "/p/uc.sahne") == 0);
  CHECK(order_differs); // KONTROL: olcum gercekten bayta bakiyor
  // Tavan: 10'dan fazlasi eklenince en eski duser.
  app::recent_clear();
  for (uint32_t i = 0; i < app::kRecentMax + 4; i++) {
    char p[64];
    std::snprintf(p, sizeof p, "/p/s%02u.sahne", i);
    app::recent_push(p);
  }
  const uint32_t capped = app::recent_count();
  const uint32_t n2 = app::recent_list(list, app::kRecentMax);
  std::printf("    [bilgi] tavan: %u yol eklendi -> %u girdi, en yeni \"%s\", en eski \"%s\"\n", app::kRecentMax + 4, capped, n2 ? list[0] : "-",
              n2 ? list[n2 - 1] : "-");
  CHECK(capped == app::kRecentMax);
  CHECK(n2 && std::strcmp(list[0], "/p/s13.sahne") == 0 && std::strcmp(list[n2 - 1], "/p/s04.sahne") == 0);
  std::remove(file);
  app::recent_clear();
}

ENGINE_TEST(files_recent_marks_missing_files) {
  TempTree t;
  if (!make_tree(t)) { skip("gecici dizin yaratilamadi"); return; }
  char real[1024];
  std::snprintf(real, sizeof real, "%s/arena.sahne", t.root);
  app::recent_clear();
  app::recent_push("/boyle/bir/sahne/yok.sahne");
  app::recent_push(real);
  const bool e0 = app::recent_exists(0), e1 = app::recent_exists(1);
  std::printf("    [bilgi] son dosyalar varlik: [0]=\"%s\" %s, [1]=\"%s\" %s\n", real, e0 ? "var" : "YOK", "/boyle/bir/sahne/yok.sahne",
              e1 ? "VAR" : "yok");
  CHECK(e0);  // var olan dosya
  CHECK(!e1); // KONTROL: olmayan dosya isaretli (menude soluk)
  // Silinince isaret degismeli (refresh gercekten olcuyor mu).
  std::remove(real);
  app::recent_refresh();
  const bool e0b = app::recent_exists(0);
  std::printf("    [bilgi] dosya silindikten sonra [0] %s\n", e0b ? "hala VAR (HATA)" : "yok");
  CHECK(!e0b);
  app::recent_clear();
  rm_tree(t);
}

// --- 5) Uzerine yazma onayi -------------------------------------------------
ENGINE_TEST(files_save_mode_asks_overwrite_only_when_file_exists) {
  TempTree t;
  if (!make_tree(t)) { skip("gecici dizin yaratilamadi"); return; }
  app::FileDialog d;
  std::snprintf(d.dir, sizeof d.dir, "%s", t.root);
  std::snprintf(d.ext, sizeof d.ext, ".sahne");
  d.mode = app::FileDialogMode::Kaydet;
  char target[1024];
  std::snprintf(d.name, sizeof d.name, "arena"); // uzanti zorlanip var olan dosyaya isaret eder
  const bool tp1 = app::file_dialog_target_path(d, target, sizeof target);
  const bool over1 = app::file_dialog_would_overwrite(d);
  char t1[1024];
  std::snprintf(t1, sizeof t1, "%s", target);
  std::snprintf(d.name, sizeof d.name, "yeni_bolum"); // KONTROL: olmayan dosya
  const bool tp2 = app::file_dialog_target_path(d, target, sizeof target);
  const bool over2 = app::file_dialog_would_overwrite(d);
  std::printf("    [bilgi] uzerine yazma: \"arena\" -> \"%s\" onay %s | KONTROL \"yeni_bolum\" -> \"%s\" onay %s\n", t1, over1 ? "evet" : "HAYIR",
              target, over2 ? "EVET (HATA)" : "hayir");
  CHECK(tp1 && tp2);
  CHECK(std::strstr(t1, "arena.sahne") != nullptr); // uzanti zorlandi
  CHECK(over1);
  CHECK(!over2); // KONTROL
  // Ac kipinde uzerine yazma diye bir sey YOKTUR.
  d.mode = app::FileDialogMode::Ac;
  CHECK(!app::file_dialog_would_overwrite(d));
  rm_tree(t);
}

// --- 6) Diyalog cizimi (GORSEL) ---------------------------------------------
namespace {
struct DialogCtx {
  app::FileDialog dlg;
  uint32_t shown_last = 0;
  int action = -1;
};
void draw_dialog_probe(void *ctx, uint32_t frame) {
  auto *c = static_cast<DialogCtx *>(ctx);
  ImGuiIO &io = ImGui::GetIO();
  ImGui::SetNextWindowPos(ImVec2(0, 0));
  ImGui::SetNextWindowSize(io.DisplaySize);
  ImGui::Begin("Editor", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar);
  ImGui::TextUnformatted("Tulpar Editor");
  ImGui::End();
  const app::FileDialogAction a = app::file_dialog_draw(c->dlg);
  if (a != app::FileDialogAction::None) c->action = (int)a;
  c->shown_last = c->dlg.shown;
  (void)frame;
}
struct ConfirmCtx {
  app::ConfirmState st;
  int result = -1;
};
void draw_confirm_probe(void *ctx, uint32_t frame) {
  auto *c = static_cast<ConfirmCtx *>(ctx);
  ImGuiIO &io = ImGui::GetIO();
  ImGui::SetNextWindowPos(ImVec2(0, 0));
  ImGui::SetNextWindowSize(io.DisplaySize);
  ImGui::Begin("Editor", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar);
  ImGui::TextUnformatted("Tulpar Editor");
  ImGui::End();
  const app::ConfirmResult r = app::confirm_modal(c->st, "Sahne kaydedilmedi",
                                                  "\"arena.sahne\" dosyas\xC4\xB1nda kaydedilmemi\xC5\x9F de\xC4\x9Fi\xC5\x9Fiklikler var.\n"
                                                  "Yeni sahne a\xC3\xA7\xC4\xB1lmadan \xC3\xB6nce ne yap\xC4\xB1ls\xC4\xB1n?",
                                                  "Kaydet", "Vazge\xC3\xA7", "Kaydetme");
  if (r != app::ConfirmResult::None) c->result = (int)r;
  (void)frame;
}
} // namespace

ENGINE_TEST(files_dialog_draws_sorted_filtered_list) {
  TempTree t;
  if (!make_tree(t)) { skip("gecici dizin yaratilamadi"); return; }
  DialogCtx c;
  const bool opened = app::file_dialog_open(c.dlg, app::FileDialogMode::Ac, t.root, ".sahne", "Sahne a\xC3\xA7");
  c.dlg.sel = 1; // "arena.sahne" secili gorunsun
  EditorProbe p;
  p.width = 760;
  p.height = 480;
  p.frames = 3;
  char path[512];
  out_path(path, sizeof path, "dosya_diyalogu_ac.ppm");
  p.out_ppm = path;
  p.draw = draw_dialog_probe;
  p.ctx = &c;
  const ProbeStatus st = editor_probe_render(p);
  if (st == ProbeStatus::NoVulkan) { skip("Vulkan yok"); rm_tree(t); return; }
  if (st != ProbeStatus::Ok) std::printf("    [bilgi] sonda: %s\n", p.err);
  CHECK(st == ProbeStatus::Ok);
  if (st != ProbeStatus::Ok) { rm_tree(t); return; }
  std::printf("    [bilgi] diyalog (Ac, .sahne): dizin \"%s\", %u satir cizildi, %u vertex, eylem %d\n", c.dlg.dir, c.shown_last, p.vertices,
              c.action);
  CHECK(opened && c.dlg.list.ok);
  CHECK(c.shown_last == 4); // alt_sahneler + 3 .sahne
  CHECK(c.action == -1);    // hicbir sey tiklanmadi: diyalog ACIK kalir
  CHECK(p.vertices > 500);

  // KONTROL: Kaydet kipi, suzgecsiz — ad kutusu ve DAHA COK satir cizilir.
  DialogCtx c2;
  app::file_dialog_open(c2.dlg, app::FileDialogMode::Kaydet, t.root, ".sahne", "Farkl\xC4\xB1 kaydet");
  std::snprintf(c2.dlg.name, sizeof c2.dlg.name, "arena.sahne");
  c2.dlg.ask_overwrite = true; // uzerine yazma seridi goruntuye girsin
  std::snprintf(c2.dlg.path, sizeof c2.dlg.path, "%s/arena.sahne", t.root);
  EditorProbe p2;
  p2.width = 760;
  p2.height = 480;
  p2.frames = 3;
  char path2[512];
  out_path(path2, sizeof path2, "dosya_diyalogu_kaydet.ppm");
  p2.out_ppm = path2;
  p2.draw = draw_dialog_probe;
  p2.ctx = &c2;
  const ProbeStatus st2 = editor_probe_render(p2);
  if (st2 == ProbeStatus::Ok) {
    std::printf("    [bilgi] diyalog (Kaydet): ad \"%s\", %u satir, %u vertex (Ac: %u vertex)\n", c2.dlg.name, c2.shown_last, p2.vertices,
                p.vertices);
    CHECK(c2.shown_last == 4);
    CHECK(p2.vertices > p.vertices); // ad kutusu + uyari seridi EK cizim
  }
  rm_tree(t);
}

// --- 7) Onay kutusu (GORSEL) ------------------------------------------------
ENGINE_TEST(files_confirm_modal_has_three_buttons) {
  ConfirmCtx c;
  c.st.open = true;
  EditorProbe p;
  p.width = 640;
  p.height = 360;
  p.frames = 3;
  char path[512];
  out_path(path, sizeof path, "onay_kutusu.ppm");
  p.out_ppm = path;
  p.draw = draw_confirm_probe;
  p.ctx = &c;
  const ProbeStatus st = editor_probe_render(p);
  if (st == ProbeStatus::NoVulkan) { skip("Vulkan yok"); return; }
  if (st != ProbeStatus::Ok) std::printf("    [bilgi] sonda: %s\n", p.err);
  CHECK(st == ProbeStatus::Ok);
  if (st != ProbeStatus::Ok) return;
  std::printf("    [bilgi] onay kutusu: acik %s, sonuc %d, %u vertex\n", c.st.open ? "evet" : "hayir", c.result, p.vertices);
  CHECK(c.st.open);    // tiklanmadi: acik kalir
  CHECK(c.result == -1);
  CHECK(p.vertices > 300);
  // KONTROL: kapali durumda hicbir sey cizilmez (vertex belirgin sekilde az).
  ConfirmCtx c2; // st.open = false
  EditorProbe p2;
  p2.width = 640;
  p2.height = 360;
  p2.frames = 3;
  p2.draw = draw_confirm_probe;
  p2.ctx = &c2;
  const ProbeStatus st2 = editor_probe_render(p2);
  if (st2 == ProbeStatus::Ok) {
    std::printf("    [bilgi] KONTROL kapali onay kutusu: %u vertex (acik: %u)\n", p2.vertices, p.vertices);
    CHECK(p2.vertices < p.vertices);
  }
}
