// Editor kapilari:
//  1) Arayuz (Dear ImGui) motorun offscreen gecisine ciziyor mu? Pencere yok:
//     pencere + metin + dugme karesi, bos ImGui karesinden farkli pikseller
//     vermeli (>500); bos kare (kontrol) 0 fark ve 0 vertex.
//  2) Donusum sirasi (T*Rz*Ry*Rx*S) ImGuizmo gelenegiyle ayni mi.
//  3) Coklu secim: secim kumesi, grup tasima/silme gunluge TEK eylem (N islem),
//     grup geri al baslangic BAYTLARINI getirir (kontrol: tek geri al yetmez).
//  4) Kaynak tarayici: dizindeki glTF'ler (kontrol: olmayan dizin 0, png sayilmaz),
//     secilen kaynakla eklenen varlik kSceneModel alir.
//  5) Isik/golge gizmolari: acikken cizim sayisi ve piksel artar
//     (kontrol: kapaliyken fark 0).
#include <cmath>
#include <cstdio>
#include <cstring>

#include "app/editor_ui.hpp"
#include "core/memory/arena.hpp"
#include "renderer/renderer.hpp"
#include "rhi/device.hpp"
#include "rhi/offscreen.hpp"
#include "rhi/vk_api.hpp"
#include "tests/test.hpp"

#include <imgui.h>

using namespace tulpar::engine;
using namespace tulpar::engine::test;

namespace {
rhi::VkApi g_api;
struct Rec { renderer::Renderer *r; app::EditorUi *ui; };
void rec_main(VkCommandBuffer cb, void *u) { auto *c = static_cast<Rec *>(u); c->r->record(cb); c->r->ui_record(cb); c->ui->record(cb); }
void rec_shadow(VkCommandBuffer cb, void *u) { static_cast<Rec *>(u)->r->record_shadow(cb); }
// Arayuzsuz kayit (gizmo kapisi): yalniz 3B.
void rec_only_scene(VkCommandBuffer cb, void *u) { static_cast<renderer::Renderer *>(u)->record(cb); }
void rec_only_shadow(VkCommandBuffer cb, void *u) { static_cast<renderer::Renderer *>(u)->record_shadow(cb); }
uint32_t pixel_diff(const uint8_t *a, const uint8_t *b, uint32_t n) {
  uint32_t d = 0;
  for (uint32_t i = 0; i < n; i++)
    if (a[i * 4] != b[i * 4] || a[i * 4 + 1] != b[i * 4 + 1] || a[i * 4 + 2] != b[i * 4 + 2]) d++;
  return d;
}
} // namespace

ENGINE_TEST(editor_imgui_draws_into_offscreen_pass) {
  if (!rhi::vk_api_load(g_api)) { skip("Vulkan loader yok"); return; }
  static SystemArena sys;
  if (!sys.reserve(64u << 20, "editor_test")) { CHECK(false); return; }
  rhi::Device dev;
  rhi::DeviceConfig dc;
  if (!dev.init(sys, g_api, dc)) { skip("Vulkan cihazi yok"); return; }
  const uint32_t W = 256, H = 256;
  rhi::OffscreenConfig oc;
  oc.srgb = true;
  oc.width = W; oc.height = H;
  rhi::OffscreenResult ores;
  rhi::OffscreenTarget *off = rhi::offscreen_create(dev, sys, oc, &ores);
  if (!off) { CHECK(false); dev.shutdown(); return; }
  renderer::Renderer ren;
  renderer::RendererConfig rc;
  rc.frames_in_flight = 1;
  rc.shadow_size = 0;
  if (!ren.init(dev, sys, rhi::offscreen_render_pass(off), rc)) { CHECK(false); rhi::offscreen_destroy(off); dev.shutdown(); return; }
  app::EditorUi ui;
  bool ok = ui.init(dev, rhi::offscreen_render_pass(off), 1, 2, nullptr, 0);
  if (!ok) std::printf("    [bilgi] editor ui: %s\n", ui.last_error());
  CHECK(ok);
  if (!ok) { ren.shutdown(); rhi::offscreen_destroy(off); dev.shutdown(); return; }
  ren.set_camera(Mat4::look_at({0, 3, 6}, {0, 0, 0}, {0, 1, 0}), Mat4::perspective(1.0f, 1.0f, 0.1f, 50.0f));
  ren.set_render_size(W, H);
  Rec rr{&ren, &ui};
  static uint8_t px[2][W * H * 4];
  uint32_t verts[2] = {0, 0};
  for (int pass = 0; pass < 2; pass++) {
    ui.begin_frame(nullptr, (float)W, (float)H, 1.0f / 60.0f);
    if (pass == 0) {
      ImGui::SetNextWindowPos(ImVec2(20, 20));
      ImGui::SetNextWindowSize(ImVec2(200, 120));
      ImGui::Begin("Tulpar");
      ImGui::Text("Editor testi");
      ImGui::Button("Dugme");
      ImGui::End();
    }
    ui.end_frame();
    verts[pass] = ui.stats().vertices;
    ren.begin_frame(0);
    if (!rhi::offscreen_render_custom(off, oc, rec_main, &rr, &ores, rec_shadow)) { CHECK(false); break; }
    std::memcpy(px[pass], ores.pixels, sizeof px[pass]);
  }
  uint32_t diff = 0;
  for (uint32_t i = 0; i < W * H; i++)
    if (px[0][i * 4] != px[1][i * 4] || px[0][i * 4 + 1] != px[1][i * 4 + 1] || px[0][i * 4 + 2] != px[1][i * 4 + 2]) diff++;
  std::printf("    [bilgi] ImGui: pencereli kare %u vertex, bos kare %u vertex; farkli piksel %u / %u\n", verts[0], verts[1], diff, W * H);
  CHECK(verts[0] > 0);
  CHECK(verts[1] == 0);   // kontrol: bos kare hicbir sey cizmez
  CHECK(diff > 500);      // pencere gercekten piksel yazdi
  ui.shutdown();
  ren.shutdown();
  rhi::offscreen_destroy(off);
  dev.shutdown();
}

// Sahne veri modelinin donusum sirasi (T*Rz*Ry*Rx*S) ImGuizmo'nun
// Recompose/Decompose gelenegiyle ayni olmali; yoksa gizmo her karede
// Euler'i "duzeltir" ve varlik titrer. Kontrol: ters sira farkli matris verir.
#include "content/scene.hpp"
#include <ImGuizmo.h>
ENGINE_TEST(editor_scene_matrix_matches_gizmo_convention) {
  content::SceneEntity e{};
  e.pos = {1, 2, 3}; e.rot_deg = {30, 45, 60}; e.scale = {1, 2, 0.5f};
  const Mat4 ours = content::scene_entity_matrix(e);
  float g[16];
  ImGuizmo::RecomposeMatrixFromComponents(&e.pos.x, &e.rot_deg.x, &e.scale.x, g);
  float max_d = 0;
  for (int c = 0; c < 4; c++)
    for (int r = 0; r < 4; r++) {
      const float d = std::fabs(ours.m[c][r] - g[c * 4 + r]);
      if (d > max_d) max_d = d;
    }
  CHECK(max_d < 1e-5f);
  // Ayristir -> yeniden kur: ayni matris (Euler farkli olabilir, matris ayni).
  float p[3], r[3], s[3], g2[16];
  ImGuizmo::DecomposeMatrixToComponents(&ours.m[0][0], p, r, s);
  ImGuizmo::RecomposeMatrixFromComponents(p, r, s, g2);
  float max_d2 = 0;
  for (int i = 0; i < 16; i++) { const float d = std::fabs(g2[i] - (&ours.m[0][0])[i]); if (d > max_d2) max_d2 = d; }
  CHECK(max_d2 < 1e-4f);
  // Kontrol: sira ters (Rx*Ry*Rz) olsaydi fark buyuk olurdu.
  const float k = 3.14159265f / 180.0f;
  const Mat4 wrong = Mat4::translate(e.pos) * Mat4::rotate({1, 0, 0}, 30 * k) * Mat4::rotate({0, 1, 0}, 45 * k) *
                     Mat4::rotate({0, 0, 1}, 60 * k) * Mat4::scale(e.scale);
  float max_w = 0;
  for (int c = 0; c < 3; c++)
    for (int rr = 0; rr < 3; rr++) { const float d = std::fabs(wrong.m[c][rr] - g[c * 4 + rr]); if (d > max_w) max_w = d; }
  CHECK(max_w > 1e-2f);
  std::printf("    [bilgi] gizmo gelenegi: en buyuk fark %.2e (ayristir/kur %.2e), ters sira %.3f\n", max_d, max_d2, max_w);
}

namespace {
// Kapilar icin kucuk sahne: uc adli varlik, bileseni yok.
void make_scene(content::SceneDesc &d) {
  d = content::SceneDesc{};
  for (int i = 0; i < 3; i++) {
    content::SceneEntity e{};
    std::snprintf(e.name, sizeof e.name, "nesne_%d", i);
    e.pos = {(float)i * 2.0f, 1.0f, -3.0f};
    d.insert_entity(d.entity_count, e);
  }
}
SystemArena &gate_arena() {
  static SystemArena a;
  if (a.capacity() == 0) a.reserve(8u << 20, "editor_gate");
  return a;
}
bool ends_with(const char *s, const char *suf) {
  const size_t ls = std::strlen(s), lf = std::strlen(suf);
  return ls >= lf && !std::strcmp(s + ls - lf, suf);
}
} // namespace

// Coklu secim: Ctrl+tik kumesi, grup tasima ve grup silme gunlukte N ardisik
// islem ama TEK kullanici eylemi. Olcum bayt bayt (scene_write): grup geri
// alindiktan sonra metin baslangic metniyle ayni olmali.
// KONTROL: tek geri al yetmemeli (yoksa "grup" tek islemdir, kapi bos olur).
ENGINE_TEST(editor_multi_select_group_move_and_delete_undo_as_one) {
  content::SceneDesc d;
  make_scene(d);
  content::SceneHistory h;
  CHECK(h.init(gate_arena(), 64));

  app::Selection sel;
  sel.set_single(2);
  CHECK(sel.count == 1 && sel.primary() == 2);
  CHECK(sel.toggle(0));                        // Ctrl+tik: ekler
  CHECK(sel.count == 2 && sel.primary() == 0); // en son tiklanan ana secili
  CHECK(sel.contains(2) && !sel.contains(1));
  CHECK(!sel.toggle(0));                       // ayni ogeye Ctrl+tik: cikarir
  CHECK(sel.count == 1 && sel.primary() == 2);
  sel.set_single(0);
  sel.toggle(1);

  static char a[8192], b[8192], c[8192];
  content::scene_write(d, a, sizeof a);
  const Vec3 delta{1.5f, 0.25f, -0.75f};
  const Vec3 p0 = d.entities[0].pos, p1 = d.entities[1].pos, p2 = d.entities[2].pos;
  app::OpGroups g;
  const uint32_t ops = app::selection_translate(d, h, sel.items, sel.count, delta);
  g.push(ops);
  CHECK(ops == 2);
  CHECK(d.entities[0].pos == p0 + delta && d.entities[1].pos == p1 + delta);
  CHECK(d.entities[2].pos == p2); // secili olmayan kimildamadi
  content::scene_write(d, b, sizeof b);
  CHECK(std::strcmp(a, b) != 0);
  CHECK(h.undo_count() == 2);
  CHECK(h.undo(d));
  content::scene_write(d, c, sizeof c);
  CHECK(std::strcmp(a, c) != 0); // KONTROL: tek geri al grubu geri almaz
  CHECK(h.undo(d));
  content::scene_write(d, c, sizeof c);
  CHECK(std::strcmp(a, c) == 0); // grup geri alindi: baslangic baytlari
  // Grup sinirlari (editorun geri al'i bu sayiyi kullanir).
  CHECK(g.undo_size() == 2);
  CHECK(g.redo_size() == 2);
  CHECK(g.undo_size() == 2);
  app::OpGroups empty;
  CHECK(empty.undo_size() == 1); // KONTROL: sinir bilinmiyorsa tek islem
  CHECK(h.redo(d) && h.redo(d));
  content::scene_write(d, c, sizeof c);
  CHECK(std::strcmp(b, c) == 0); // grup yinelendi

  // Grup silme: buyukten kucuge (indeksler kaymasin), geri al hepsini getirir.
  sel.set_single(0);
  sel.toggle(2);
  int32_t desc[app::Selection::kMax];
  const uint32_t nd = sel.sorted_desc(desc);
  CHECK(nd == 2 && desc[0] == 2 && desc[1] == 0);
  const uint32_t rops = app::selection_remove(d, h, desc, nd);
  CHECK(rops == 2 && d.entity_count == 1);
  CHECK(!std::strcmp(d.entities[0].name, "nesne_1")); // ortadaki kaldi
  for (uint32_t i = 0; i < rops; i++) CHECK(h.undo(d));
  content::scene_write(d, c, sizeof c);
  CHECK(d.entity_count == 3 && !std::strcmp(b, c));

  // Silme sonrasi secim indeksleri kaymali (geri al/sil sonrasi gecerli kalsin).
  app::Selection s2;
  s2.set_single(1);
  s2.toggle(3);
  s2.after_remove(2);
  CHECK(s2.count == 2 && s2.contains(1) && s2.contains(2) && !s2.contains(3));
  s2.after_remove(1);
  CHECK(s2.count == 1 && s2.contains(1));
  std::printf("    [bilgi] coklu secim: grup tasima %u islem, grup silme %u islem; tek geri al yetmiyor (kontrol), iki geri al baslangic baytlari\n", ops,
              rops);
}

// Kaynak tarayici: sahne dizinindeki glTF'ler listelenir (ada gore sirali),
// secilen kaynakla eklenen varlik kSceneModel alir.
// KONTROLLER: olmayan dizin 0 dosya; dizindeki .png listeye GIRMEZ (dosyanin
// var oldugu ayrica dogrulanir, yoksa filtre kapisi bos olurdu); bos ad 0 islem.
ENGINE_TEST(editor_asset_browser_lists_gltf_and_adds_entity) {
  content::SceneDesc d{};
  CHECK(d.add_asset("lod_sphere.gltf") == 0);
  char dir[512], png[640];
  std::snprintf(dir, sizeof dir, "%s/tests/assets", ENGINE_SOURCE_DIR);
  std::snprintf(png, sizeof png, "%s/checker_64.png", dir);
  FILE *pf = std::fopen(png, "rb");
  CHECK(pf != nullptr); // kontrolun bos olmadiginin kaniti: dizinde glTF olmayan dosya var
  if (pf) std::fclose(pf);

  app::AssetFile files[16];
  const uint32_t n = app::editor_scan_assets(dir, d, files, 16);
  CHECK(n >= 3);
  bool sorted = true, only_gltf = true;
  uint32_t in_scene = 0;
  for (uint32_t i = 0; i < n; i++) {
    if (i && std::strcmp(files[i - 1].name, files[i].name) > 0) sorted = false;
    if (!ends_with(files[i].name, ".gltf") && !ends_with(files[i].name, ".glb")) only_gltf = false;
    if (files[i].in_scene) {
      in_scene++;
      CHECK(files[i].index == 0);
      CHECK(!std::strcmp(files[i].name, "lod_sphere.gltf"));
    }
  }
  CHECK(sorted);
  CHECK(only_gltf); // KONTROL: .png / .ktx2 / .sahne listeye girmedi
  CHECK(in_scene == 1);
  CHECK(app::editor_scan_assets("/yok/boyle/bir/dizin", d, files, 16) == 0); // KONTROL

  content::SceneHistory h;
  CHECK(h.init(gate_arena(), 32));
  const uint32_t n_ent = d.entity_count, n_asset = d.asset_count;
  int32_t a = -1;
  CHECK(app::editor_add_asset_entity(d, h, "checker_cube.gltf", Vec3{1, 2, 3}, &a) == 1);
  CHECK(a == (int32_t)n_asset && d.asset_count == n_asset + 1);
  CHECK(d.entity_count == n_ent + 1);
  const content::SceneEntity &e = d.entities[d.entity_count - 1];
  CHECK((e.components & content::kSceneModel) != 0); // kaynakla kurulan varlik model bileseni alir
  CHECK(e.asset == a);
  const Vec3 want_pos{1, 2, 3}; // CHECK makrosu susli parantez icindeki virgulu ayirir
  CHECK(e.pos == want_pos);
  int32_t a2 = -1;
  CHECK(app::editor_add_asset_entity(d, h, "checker_cube.gltf", Vec3{0, 0, 0}, &a2) == 1);
  CHECK(a2 == a && d.asset_count == n_asset + 1); // ayni kaynak tabloya ikinci kez girmez
  CHECK(app::editor_add_asset_entity(d, h, "", Vec3{}, nullptr) == 0); // KONTROL
  const uint32_t n2 = app::editor_scan_assets(dir, d, files, 16);
  uint32_t marked = 0;
  for (uint32_t i = 0; i < n2; i++)
    if (files[i].in_scene) marked++;
  CHECK(marked == 2); // yeni kaynak listede "sahnede" gorunur
  CHECK(h.undo(d) && h.undo(d));
  CHECK(d.entity_count == n_ent);
  std::printf("    [bilgi] kaynak tarayici: %s -> %u glTF (sirali %s, yalniz glTF %s), eklenen varlik kaynak %d model bileseniyle\n", dir, n,
              sorted ? "evet" : "HAYIR", only_gltf ? "evet" : "HAYIR", a);
}

// Isik yaricapi / golge hacmi / gunes yonu gizmolari gercekten ciziliyor mu?
// Olcum: ayni sahne iki kez kapali (KONTROL: cizim farki 0, piksel farki 0),
// sonra acik (cizim sayisi ve piksel artar). Cizim sayisi KAYITTA sayilir
// (Tuzaklar 8aa): stats() her zaman offscreen_render_custom'dan SONRA okunur.
ENGINE_TEST(editor_light_and_shadow_gizmos_draw_with_control) {
  if (!rhi::vk_api_load(g_api)) { skip("Vulkan loader yok"); return; }
  static SystemArena sys;
  if (sys.capacity() == 0 && !sys.reserve(64u << 20, "editor_gizmo_test")) { CHECK(false); return; }
  rhi::Device dev;
  rhi::DeviceConfig dc;
  if (!dev.init(sys, g_api, dc)) { skip("Vulkan cihazi yok"); return; }
  const uint32_t W = 256, H = 256;
  rhi::OffscreenConfig oc;
  oc.srgb = true;
  oc.width = W; oc.height = H;
  rhi::OffscreenResult ores;
  rhi::OffscreenTarget *off = rhi::offscreen_create(dev, sys, oc, &ores);
  if (!off) { CHECK(false); dev.shutdown(); return; }
  renderer::Renderer ren;
  renderer::RendererConfig rc;
  rc.frames_in_flight = 1;
  rc.shadow_size = 0;
  if (!ren.init(dev, sys, rhi::offscreen_render_pass(off), rc)) { CHECK(false); rhi::offscreen_destroy(off); dev.shutdown(); return; }
  renderer::Vertex cv[24];
  uint32_t ci[36];
  const uint32_t cn = renderer::Renderer::cube(cv, ci);
  const renderer::MeshHandle cube = ren.create_mesh(cv, 24, ci, cn);
  ren.set_camera(Mat4::look_at({0, 2.5f, 11}, {0, 0, 0}, {0, 1, 0}), Mat4::perspective(1.0f, 1.0f, 0.1f, 60.0f));
  ren.set_render_size(W, H);
  ren.set_light(normalize(Vec3{0.4f, 1.0f, 0.2f}), {0.2f, 0.2f, 0.25f}, 0.9f);

  content::SceneDesc d{};
  d.shadow_center = {0, 0, 0};
  d.shadow_radius = 3.0f;
  d.sun_dir = {0.4f, 1.0f, 0.2f};
  content::SceneEntity le{};
  std::snprintf(le.name, sizeof le.name, "lamba");
  le.components = content::kSceneLight;
  le.light_color = {1.0f, 0.3f, 0.2f};
  le.light_radius = 2.0f;
  CHECK(d.insert_entity(0, le));

  // Bilesenlerin payi: 12 + 12 kenar + 2 parcali ok.
  app::GizmoOptions only_light, only_shadow, only_sun, all_off;
  only_light.shadow_volume = only_light.sun_dir = false;
  only_shadow.light_radius = only_shadow.sun_dir = false;
  only_sun.light_radius = only_sun.shadow_volume = false;
  all_off.light_radius = all_off.shadow_volume = all_off.sun_dir = false;
  ren.begin_frame(0);
  CHECK(app::editor_draw_gizmos(ren, cube, d, nullptr, 0, only_light) == 12);
  CHECK(app::editor_draw_gizmos(ren, cube, d, nullptr, 0, only_shadow) == 12);
  CHECK(app::editor_draw_gizmos(ren, cube, d, nullptr, 0, only_sun) == 2);
  CHECK(app::editor_draw_gizmos(ren, cube, d, nullptr, 0, all_off) == 0);

  const app::GizmoOptions all_on;
  const app::GizmoOptions *plan[3] = {&all_off, &all_off, &all_on};
  uint32_t drew[3] = {}, ret[3] = {};
  static uint8_t px[3][W * H * 4];
  for (int pass = 0; pass < 3; pass++) {
    ren.begin_frame(0);
    ren.draw(cube, Mat4::scale({0.5f, 0.5f, 0.5f}), {0.6f, 0.6f, 0.6f}); // sabit gonderme (her karede ayni)
    ret[pass] = app::editor_draw_gizmos(ren, cube, d, nullptr, 0, *plan[pass]);
    if (!rhi::offscreen_render_custom(off, oc, rec_only_scene, &ren, &ores, rec_only_shadow)) { CHECK(false); break; }
    drew[pass] = ren.stats().draws; // kayittan SONRA (Tuzaklar 8aa)
    std::memcpy(px[pass], ores.pixels, sizeof px[pass]);
  }
  const uint32_t diff_ctrl = pixel_diff(px[0], px[1], W * H);
  const uint32_t diff_on = pixel_diff(px[1], px[2], W * H);
  std::printf("    [bilgi] gizmo: cizim %u / %u / %u (donen %u), piksel farki kontrol %u, acik %u\n", drew[0], drew[1], drew[2], ret[2], diff_ctrl,
              diff_on);
  CHECK(ret[0] == 0 && ret[1] == 0);
  CHECK(drew[1] == drew[0]);              // KONTROL: kapaliyken cizim farki 0
  CHECK(diff_ctrl == 0);                  // KONTROL: kapaliyken piksel farki 0
  CHECK(ret[2] == 26);                    // 12 isik + 12 golge + 2 ok
  CHECK(drew[2] - drew[1] == ret[2]);     // artan cizimler gizmolarinki
  // Cizim SAYILARI (ret[2] == 26, drew farki) her yerde olculuyor; yalniz
  // "ekranda gercekten gorundu" iddiasi sanal GPU'da (CI macOS) sonuc vermiyor.
  if (test::gpu_is_virtual(dev.caps().device_name))
    skip("sanal GPU (Apple Paravirtual, CI macOS): gizmo PIKSEL farki gercek cihazda olculur");
  else
    CHECK(diff_on > 300);               // ekranda gercekten gorunuyor
  ren.shutdown();
  rhi::offscreen_destroy(off);
  dev.shutdown();
}
