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
#include "app/editor_viewport.hpp"
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

  // Bilesenlerin payi: 12 + 12 kenar + 2 parcali ok. light_glyph/camera_frustum
  // (yeni gizmo turleri) burada izole edilmek icin HEPSINDE kapatiliyor --
  // onlarin kendi sayimi ayri testte (editor_camera_and_light_glyph_gizmos...).
  app::GizmoOptions only_light, only_shadow, only_sun, all_off;
  only_light.shadow_volume = only_light.sun_dir = only_light.light_glyph = only_light.camera_frustum = false;
  only_shadow.light_radius = only_shadow.sun_dir = only_shadow.light_glyph = only_shadow.camera_frustum = false;
  only_sun.light_radius = only_sun.shadow_volume = only_sun.light_glyph = only_sun.camera_frustum = false;
  all_off.light_radius = all_off.shadow_volume = all_off.sun_dir = all_off.light_glyph = all_off.camera_frustum = false;
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
  CHECK(ret[2] == 29);                    // 12 isik-yaricap + 3 isik-isaret + 12 golge + 2 ok (kamera yok)
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

// Kamera/Yonlu-isik gizmolari: eskiden HER ikisi de ayirt edilemeyen tek bir
// sari kup cizerdi (editor_app.cpp:2340 fallback) -- bu kapi sekillerin
// GERCEKTEN farkli olduğunu (cizim sayisi + piksel) pozitif kontrolle olcer.
ENGINE_TEST(editor_camera_and_directional_light_gizmos_draw_with_control) {
  if (!rhi::vk_api_load(g_api)) { skip("Vulkan loader yok"); return; }
  static SystemArena sys;
  if (sys.capacity() == 0 && !sys.reserve(64u << 20, "editor_gizmo_cam_test")) { CHECK(false); return; }
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
  ren.set_camera(Mat4::look_at({0, 3, 12}, {0, 0, 0}, {0, 1, 0}), Mat4::perspective(1.0f, 1.0f, 0.1f, 60.0f));
  ren.set_render_size(W, H);
  ren.set_light(normalize(Vec3{0.4f, 1.0f, 0.2f}), {0.2f, 0.2f, 0.25f}, 0.9f);

  content::SceneDesc d{};
  content::SceneEntity ce{};
  std::snprintf(ce.name, sizeof ce.name, "kam");
  ce.components = content::kSceneCamera;
  ce.pos = {-2.0f, 1.0f, 0.0f};
  ce.cam_fov = 60.0f; ce.cam_near = 0.1f; ce.cam_far = 200.0f;
  CHECK(d.insert_entity(0, ce));
  content::SceneEntity de{};
  std::snprintf(de.name, sizeof de.name, "gunes");
  de.components = content::kSceneLight;
  de.light_type = content::SceneLightType::Directional;
  de.light_color = {1.0f, 0.9f, 0.7f};
  de.pos = {2.0f, 1.0f, 0.0f};
  CHECK(d.insert_entity(1, de));

  app::GizmoOptions off_opt, cam_only, light_only;
  off_opt.light_radius = off_opt.light_glyph = off_opt.shadow_volume = off_opt.sun_dir = off_opt.camera_frustum = false;
  cam_only = off_opt; cam_only.camera_frustum = true;
  light_only = off_opt; light_only.light_glyph = true;

  ren.begin_frame(0);
  const uint32_t n_off = app::editor_draw_gizmos(ren, cube, d, nullptr, 0, off_opt);
  const uint32_t n_cam = app::editor_draw_gizmos(ren, cube, d, nullptr, 0, cam_only);
  // Yonlu isik yildiz DEGIL, gunes-oku cizer (arrow() = 2 cizim; nokta olsaydi
  // light_glyph() = 3 olurdu) -- tur dallanmasinin fiilen calistigini olcer.
  const uint32_t n_dir_light = app::editor_draw_gizmos(ren, cube, d, nullptr, 0, light_only);
  CHECK(n_off == 0);
  CHECK(n_cam == 24);       // camera_frustum: 4+4+4 kenar + wire_box govdesi (12)
  CHECK(n_dir_light == 2);  // arrow: govde + uc

  const app::GizmoOptions *plan[2] = {&off_opt, &cam_only};
  static uint8_t px[2][W * H * 4];
  for (int pass = 0; pass < 2; pass++) {
    ren.begin_frame(0);
    ren.draw(cube, Mat4::scale({0.4f, 0.4f, 0.4f}), {0.5f, 0.5f, 0.5f}); // sabit gonderme
    app::editor_draw_gizmos(ren, cube, d, nullptr, 0, *plan[pass]);
    if (!rhi::offscreen_render_custom(off, oc, rec_only_scene, &ren, &ores, rec_only_shadow)) { CHECK(false); break; }
    std::memcpy(px[pass], ores.pixels, sizeof px[pass]);
  }
  const uint32_t diff = pixel_diff(px[0], px[1], W * H);
  std::printf("    [bilgi] kamera gizmosu: cizim %u, piksel farki %u\n", n_cam, diff);
  if (test::gpu_is_virtual(dev.caps().device_name)) skip("sanal GPU: piksel olcumu gercek cihazda");
  else CHECK(diff > 100); // frustum ekranda gercekten gorunuyor
  ren.shutdown();
  rhi::offscreen_destroy(off);
  dev.shutdown();
}

// =============================================================================
// Viewport koordinat cevirisi (app::viewport_map_mouse) — SAF ve CIHAZSIZ:
// asagidaki kapilar Vulkan ISTEMEZ, hicbir pencere ACMAZ, hicbir sey ayirmaz.
//
// Neden ayri bir kapi ailesi: 3B artik tam ekran degil, bir ImGui PANELININ
// icerigi (E1.2). Fare olayi EKRAN uzayinda gelir, secim isini DOKU uzayinda
// atilir. Aradaki cevirinin iki sessiz hata bicimi var ve ikisi de "calisiyor
// gibi" gorunur:
//   a) Panel disi tiklamanin 0 donmesi. 0 GECERLI bir pikseldir (sol-ust kose),
//      yani `valid`i denetlemeyi unutan cagiran her bos tiklamada sol-ust
//      koseden isin atar ve bu "bazen yanlis nesne seciliyor" diye gorunur.
//      Sozlesme -1 der; kapi 3 (disarisi) bunu ACIKCA olcer: yalniz valid
//      degil, dort alanin da -1 oldugu ve HICBIRININ 0 olmadigi.
//   b) Doku olcusu panel olcusunden farkliyken oranin panel uzayinda kalmasi:
//      MERKEZ yanlis olceklemede bile dogru cikar, yalniz kenarlar kayar. Bu
//      yuzden olcek kapisi merkezi degil ceyrek/uc-ceyrek noktalarini olcer.
// Her iddianin KONTROLU var: "gecerli" demek icin gecersiz olan da olculur.
// =============================================================================
namespace {
float vp_abs(float a) { return a < 0.0f ? -a : a; }
bool vp_near(float a, float b, float eps) { return vp_abs(a - b) <= eps; }
// Sozlesmenin "gecersiz" bicimi TAM OLARAK budur: valid=false VE dort alan -1.
// Yalniz valid'e bakmak yetmez — kapi 3'un tum meselesi -1 ile 0 farkidir.
bool vp_sentinel(const app::ViewportPick &p) {
  return !p.valid && p.u == -1.0f && p.v == -1.0f && p.x == -1.0f && p.y == -1.0f;
}
bool vp_in_unit(const app::ViewportPick &p) { return p.u >= 0.0f && p.u < 1.0f && p.v >= 0.0f && p.v < 1.0f; }
void vp_print(const char *label, const app::ViewportPick &p) {
  std::printf("    [bilgi] viewport %-26s gecerli=%d u=%+.6f v=%+.6f x=%+.4f y=%+.4f\n", label, (int)p.valid, (double)p.u, (double)p.v, (double)p.x,
              (double)p.y);
}
} // namespace

// Kapi 1+2: panelin tam ortasi ve sol-ust kosesi.
// KONTROL: sol-ust kosenin BIR piksel disi. tl'nin sifirlari "varsayilan sifir"
// degil "koken pikseli" olmali; disarisi -1 vermeli — ikisi ayni sayi olsaydi
// bu kapinin tamami bos olurdu.
ENGINE_TEST(editor_viewport_maps_center_and_topleft) {
  const app::ViewportRect panel{100.0f, 50.0f, 800.0f, 600.0f};
  const uint32_t W = 800, H = 600;
  const app::ViewportPick mid = app::viewport_map_mouse(panel, panel.x + 400.0f, panel.y + 300.0f, W, H);
  const app::ViewportPick tl = app::viewport_map_mouse(panel, panel.x, panel.y, W, H);
  const app::ViewportPick off = app::viewport_map_mouse(panel, panel.x - 1.0f, panel.y - 1.0f, W, H);
  vp_print("merkez", mid);
  vp_print("sol-ust (ICERIDE)", tl);
  vp_print("KONTROL bir piksel disi", off);
  CHECK(mid.valid);
  CHECK(vp_near(mid.u, 0.5f, 1e-6f) && vp_near(mid.v, 0.5f, 1e-6f));
  CHECK(vp_near(mid.x, 400.0f, 1e-3f) && vp_near(mid.y, 300.0f, 1e-3f)); // W/2, H/2
  CHECK(tl.valid);
  CHECK(tl.u == 0.0f && tl.v == 0.0f); // tam sifir: koken
  CHECK(tl.x == 0.0f && tl.y == 0.0f);
  CHECK(vp_sentinel(off));                  // KONTROL: disarisi 0 DEGIL, -1
  CHECK(tl.x != off.x && tl.y != off.y);    // koken ile "gecersiz" ayirt edilebilir
  CHECK(vp_in_unit(tl) && !vp_in_unit(off));

  // Panelin EKRANDAKI yeri sonuca girmemeli (u,v panele gore bagil).
  const app::ViewportRect moved{-37.5f, 912.0f, panel.w, panel.h};
  const app::ViewportPick mid2 = app::viewport_map_mouse(moved, moved.x + 400.0f, moved.y + 300.0f, W, H);
  CHECK(mid2.valid && mid2.u == mid.u && mid2.v == mid.v && mid2.x == mid.x && mid2.y == mid.y);
  // KONTROL: ayni MUTLAK fare konumu, tasinmis panelde artik disarida — yani
  // yukaridaki esitlik "fonksiyon paneli hic okumuyor" demek degil.
  const app::ViewportPick stale = app::viewport_map_mouse(moved, panel.x + 400.0f, panel.y + 300.0f, W, H);
  CHECK(vp_sentinel(stale));
  std::printf("    [bilgi] viewport merkez beklenen u/v 0.500000/0.500000 x/y %u/%u; panel tasindi -> ayni (%.4f/%.4f), eski mutlak konum disarida=%d\n",
              W / 2, H / 2, (double)mid2.x, (double)mid2.y, (int)!stale.valid);
}

// Kapi 3 (kenar kurali): yari acik aralik [x, x+w) x [y, y+h) — sol/ust kenar
// ICERIDE, sag/alt kenar DISARIDA (bitisik panellerle cakisma olmasin).
// KONTROL: ayni kenardan bir piksel iceri gecerli — "dis" hukmu kenara ozgu.
ENGINE_TEST(editor_viewport_edges_are_half_open) {
  const app::ViewportRect panel{0.0f, 0.0f, 800.0f, 600.0f};
  const uint32_t W = 800, H = 600;
  const app::ViewportPick r_edge = app::viewport_map_mouse(panel, panel.x + panel.w, panel.y + 10.0f, W, H);
  const app::ViewportPick b_edge = app::viewport_map_mouse(panel, panel.x + 10.0f, panel.y + panel.h, W, H);
  const app::ViewportPick br_edge = app::viewport_map_mouse(panel, panel.x + panel.w, panel.y + panel.h, W, H);
  const app::ViewportPick r_in = app::viewport_map_mouse(panel, panel.x + panel.w - 1.0f, panel.y + 10.0f, W, H);
  const app::ViewportPick b_in = app::viewport_map_mouse(panel, panel.x + 10.0f, panel.y + panel.h - 1.0f, W, H);
  const app::ViewportPick l_edge = app::viewport_map_mouse(panel, panel.x, panel.y + 10.0f, W, H);
  const app::ViewportPick t_edge = app::viewport_map_mouse(panel, panel.x + 10.0f, panel.y, W, H);
  vp_print("sag kenar (DISARIDA)", r_edge);
  vp_print("alt kenar (DISARIDA)", b_edge);
  vp_print("KONTROL sag-1px (ICERIDE)", r_in);
  vp_print("KONTROL alt-1px (ICERIDE)", b_in);
  CHECK(vp_sentinel(r_edge));
  CHECK(vp_sentinel(b_edge));
  CHECK(vp_sentinel(br_edge));
  CHECK(r_in.valid && vp_near(r_in.x, 799.0f, 1e-3f)); // KONTROL: bir piksel icerisi gecerli
  CHECK(b_in.valid && vp_near(b_in.y, 599.0f, 1e-3f));
  CHECK(l_edge.valid && l_edge.u == 0.0f);  // sol kenar: araligin KAPALI ucu
  CHECK(t_edge.valid && t_edge.v == 0.0f);  // ust kenar: ayni
  std::printf("    [bilgi] viewport kenar: sag %.1f -> gecerli=%d (sag-1px %.1f -> gecerli=%d, x=%.3f), alt %.1f -> gecerli=%d (alt-1px gecerli=%d, "
              "y=%.3f), sol/ust kenar gecerli=%d/%d\n",
              (double)(panel.x + panel.w), (int)r_edge.valid, (double)(panel.x + panel.w - 1.0f), (int)r_in.valid, (double)r_in.x,
              (double)(panel.y + panel.h), (int)b_edge.valid, (int)b_in.valid, (double)b_in.y, (int)l_edge.valid, (int)t_edge.valid);
}

// Kapi 4 (panel disi, dort yon): valid=false VE dort alan -1 — 0 DEGIL.
// Ayrim onemli: 0 gecerli bir piksel, `valid`i denetlemeyi unutan cagiran
// sessizce sol-ust koseden isin atardi. Kapi hem sentinel bicimini hem de
// "hicbir alan 0 degil" ve "[0,1)^2 icine dusmuyor" sonucunu olcer.
// KONTROL: ayni dort yonun bir piksel ICERISI gecerli ve birim karenin icinde.
ENGINE_TEST(editor_viewport_outside_returns_minus_one_not_zero) {
  const app::ViewportRect panel{100.0f, 50.0f, 800.0f, 600.0f};
  const uint32_t W = 640, H = 480; // doku panelden FARKLI: -1 olcekten gelmesin
  const float cx = panel.x + panel.w * 0.5f, cy = panel.y + panel.h * 0.5f;
  const char *yon[4] = {"disarida sol", "disarida ust", "disarida sag", "disarida alt"};
  const float ox[4] = {panel.x - 0.5f, cx, panel.x + panel.w + 0.5f, cx};
  const float oy[4] = {cy, panel.y - 0.5f, cy, panel.y + panel.h + 0.5f};
  const float ix[4] = {panel.x + 0.5f, cx, panel.x + panel.w - 0.5f, cx};
  const float iy[4] = {cy, panel.y + 0.5f, cy, panel.y + panel.h - 0.5f};
  uint32_t sentinels = 0, any_zero = 0, in_unit = 0;
  for (int i = 0; i < 4; i++) {
    const app::ViewportPick p = app::viewport_map_mouse(panel, ox[i], oy[i], W, H);
    vp_print(yon[i], p);
    CHECK(!p.valid);
    if (vp_sentinel(p)) sentinels++;
    if (p.u == 0.0f || p.v == 0.0f || p.x == 0.0f || p.y == 0.0f) any_zero++;
    if (vp_in_unit(p)) in_unit++;
  }
  CHECK(sentinels == 4); // dordu de -1
  CHECK(any_zero == 0);  // hicbir alan 0 degil (0 olsaydi sessiz sol-ust isini)
  CHECK(in_unit == 0);   // -1 ile atilan isin goruntu hacminin disinda kalir
  uint32_t inside_ok = 0;
  for (int i = 0; i < 4; i++) {
    const app::ViewportPick p = app::viewport_map_mouse(panel, ix[i], iy[i], W, H);
    if (p.valid && vp_in_unit(p) && p.x >= 0.0f && p.x < (float)W && p.y >= 0.0f && p.y < (float)H) inside_ok++;
  }
  CHECK(inside_ok == 4); // KONTROL: 1 piksel icerisi dort yonde de gecerli
  // Gercek hayattaki en sik "disarisi": ImGui fare yokken io.MousePos'u
  // (-FLT_MAX, -FLT_MAX) yapar. Bu da panel disi sayilmali, yoksa fare
  // yokken her kare sol-ust koseden isin atilir.
  const app::ViewportPick nomouse = app::viewport_map_mouse(panel, -3.4028235e38f, -3.4028235e38f, W, H);
  CHECK(vp_sentinel(nomouse));
  std::printf("    [bilgi] viewport disi: %u/4 sentinel (-1), 0 alan sayisi %u, birim kare icinde %u; KONTROL 1px icerisi gecerli %u/4; ImGui fare-yok "
              "(-FLT_MAX) sentinel=%d\n",
              sentinels, any_zero, in_unit, inside_ok, (int)vp_sentinel(nomouse));
}

// Kapi 5 (dejenere dikdortgen / dejenere doku): w=0, h=0, negatif olcu, hic
// kurulmamis panel ve 0 olculu doku -> gecersiz, cokme yok.
// KONTROL: 1x1 panel + 1x1 doku CALISIR — yani yukaridaki -1'ler "kucukluk"ten
// degil DEJENERELIKTEN geliyor.
ENGINE_TEST(editor_viewport_degenerate_rect_is_invalid) {
  const uint32_t W = 800, H = 600;
  const app::ViewportRect zero_w{10.0f, 10.0f, 0.0f, 600.0f};
  const app::ViewportRect zero_h{10.0f, 10.0f, 800.0f, 0.0f};
  const app::ViewportRect negative{10.0f, 10.0f, -800.0f, -600.0f};
  const app::ViewportRect unset{}; // ImGui'nin ilk karesi: icerik alani 0x0
  const app::ViewportRect tiny{10.0f, 10.0f, 1.0f, 1.0f};
  const app::ViewportPick d[7] = {
      app::viewport_map_mouse(zero_w, 10.0f, 10.0f, W, H),   app::viewport_map_mouse(zero_h, 10.0f, 10.0f, W, H),
      app::viewport_map_mouse(negative, 10.0f, 10.0f, W, H), app::viewport_map_mouse(unset, 0.0f, 0.0f, W, H),
      app::viewport_map_mouse(tiny, 10.0f, 10.0f, 0, H),     app::viewport_map_mouse(tiny, 10.0f, 10.0f, W, 0),
      app::viewport_map_mouse(tiny, 10.0f, 10.0f, 0, 0),
  };
  uint32_t bad = 0;
  for (int i = 0; i < 7; i++)
    if (vp_sentinel(d[i])) bad++;
  CHECK(bad == 7);
  const app::ViewportPick ok = app::viewport_map_mouse(tiny, 10.0f, 10.0f, 1, 1);
  const app::ViewportPick half = app::viewport_map_mouse(tiny, 10.5f, 10.5f, 1, 1);
  vp_print("KONTROL 1x1 panel/doku", ok);
  CHECK(ok.valid && ok.u == 0.0f && ok.v == 0.0f && ok.x == 0.0f && ok.y == 0.0f); // KONTROL: dejenere degil, calisiyor
  CHECK(half.valid && half.x >= 0.0f && half.x < 1.0f && half.y >= 0.0f && half.y < 1.0f);
  std::printf("    [bilgi] viewport dejenere: 7/7 gecersiz (w=0, h=0, negatif, kurulmamis, doku 0xH, doku Wx0, doku 0x0) -> %u; KONTROL 1x1 gecerli=%d\n",
              bad, (int)ok.valid);
}

// Kapi 6 (doku olcusu panelden FARKLI): panel 800x600, doku 1280x720 (1.6x/1.2x,
// izotropik DEGIL). Merkez olculmez — merkez yanlis olceklemede bile dogru
// cikar; ceyrek/uc-ceyrek noktalari olculur, beklenen piksel ELDE hesaplanmis
// sabittir (fonksiyonun formulu tekrar edilmez).
// KONTROL: panel uzayinda kalan bir gerceklestirme 200/450 verirdi — kapi bu
// ikisini acikca ayirir.
ENGINE_TEST(editor_viewport_scales_to_texture_size) {
  const app::ViewportRect panel{30.0f, 17.0f, 800.0f, 600.0f};
  const uint32_t TW = 1280, TH = 720;
  const app::ViewportPick q = app::viewport_map_mouse(panel, panel.x + 200.0f, panel.y + 450.0f, TW, TH);
  vp_print("doku 1280x720 (0.25,0.75)", q);
  CHECK(q.valid);
  CHECK(vp_near(q.u, 0.25f, 1e-6f) && vp_near(q.v, 0.75f, 1e-6f)); // u,v doku olcusunden BAGIMSIZ
  CHECK(vp_near(q.x, 320.0f, 1e-3f));                              // 0.25 * 1280
  CHECK(vp_near(q.y, 540.0f, 1e-3f));                              // 0.75 * 720
  CHECK(!vp_near(q.x, 200.0f, 1.0f) && !vp_near(q.y, 450.0f, 1.0f)); // KONTROL: panel uzayinda kalmis DEGIL
  CHECK(vp_near(q.x / 200.0f, 1.6f, 1e-4f));                       // eksen basina olcek
  CHECK(vp_near(q.y / 450.0f, 1.2f, 1e-4f));                       // tek olcekle carpan gerceklestirme bunu tutturamaz

  // Ayni fare, KUCUK doku (200x150): u,v birebir ayni, piksel kuculur.
  const app::ViewportPick s = app::viewport_map_mouse(panel, panel.x + 200.0f, panel.y + 450.0f, 200, 150);
  CHECK(s.valid && s.u == q.u && s.v == q.v);
  CHECK(vp_near(s.x, 50.0f, 1e-3f) && vp_near(s.y, 112.5f, 1e-3f));
  CHECK(s.x != q.x && s.y != q.y); // KONTROL: doku olcusu sonuca GERCEKTEN giriyor

  // Elde hesaplanmis oran tablosu (panel ofseti, beklenen doku pikseli).
  const float dx[4] = {0.0f, 100.0f, 400.0f, 700.0f};
  const float dy[4] = {0.0f, 525.0f, 300.0f, 150.0f};
  const float ex[4] = {0.0f, 160.0f, 640.0f, 1120.0f}; // dx/800 * 1280
  const float ey[4] = {0.0f, 630.0f, 360.0f, 180.0f};  // dy/600 * 720
  uint32_t hit = 0;
  for (int i = 0; i < 4; i++) {
    const app::ViewportPick p = app::viewport_map_mouse(panel, panel.x + dx[i], panel.y + dy[i], TW, TH);
    if (p.valid && vp_near(p.x, ex[i], 1e-3f) && vp_near(p.y, ey[i], 1e-3f)) hit++;
    else vp_print("TABLO SAPMASI", p);
  }
  CHECK(hit == 4);
  std::printf("    [bilgi] viewport olcek: panel 800x600 -> doku 1280x720; (0.25,0.75) x=%.3f (beklenen 320), y=%.3f (beklenen 540), panel uzayi olsa "
              "200/450 olurdu; 200x150 dokuda ayni fare x=%.3f y=%.3f; tablo %u/4\n",
              (double)q.x, (double)q.y, (double)s.x, (double)s.y, hit);
}

// Kapi 7 (tasma yok): panelin sag/alt sinirina EN YAKIN iceri nokta bile
// x < doku_w ve y < doku_h vermeli — son piksel tasmamali (u < 1 iken kayan
// nokta yuvarlamasi u*w == w yapabilir; hedef disi yazma/okuma bundan dogar).
// KONTROL: bir sonraki float DISARIDA (yani gercekten sinirdayiz) ve tarama
// dongusu gercekten kostu (ornek sayisi olculur, bos tarama yesil olmasin).
ENGINE_TEST(editor_viewport_inside_pixel_never_overflows_texture) {
  const app::ViewportRect panel{12.5f, 7.25f, 800.0f, 600.0f};
  const uint32_t tex[5][2] = {{1, 1}, {17, 9}, {800, 600}, {1280, 720}, {4096, 4096}};
  const float last_x = std::nextafterf(panel.x + panel.w, panel.x); // sinirdan onceki son float
  const float last_y = std::nextafterf(panel.y + panel.h, panel.y);
  for (int t = 0; t < 5; t++) {
    const uint32_t TW = tex[t][0], TH = tex[t][1];
    const app::ViewportPick last = app::viewport_map_mouse(panel, last_x, last_y, TW, TH);
    const app::ViewportPick past = app::viewport_map_mouse(panel, panel.x + panel.w, last_y, TW, TH);
    CHECK(last.valid);
    CHECK(last.x >= 0.0f && last.x < (float)TW);
    CHECK(last.y >= 0.0f && last.y < (float)TH);
    CHECK(last.u < 1.0f && last.v < 1.0f);
    CHECK(vp_sentinel(past)); // KONTROL: bir sonraki float disarida

    uint32_t samples = 0, invalid = 0, out_of_range = 0, non_monotonic = 0;
    float prev_x = -1.0f, max_x = 0.0f, max_y = 0.0f;
    for (float d = 0.0f; d < panel.w; d += 0.5f) { // 1600 ornek, kose-kose capraz
      const app::ViewportPick p = app::viewport_map_mouse(panel, panel.x + d, panel.y + d * 0.75f, TW, TH);
      samples++;
      if (!p.valid) invalid++;
      if (!(p.x >= 0.0f && p.x < (float)TW) || !(p.y >= 0.0f && p.y < (float)TH)) out_of_range++;
      if (p.x < prev_x) non_monotonic++;
      prev_x = p.x;
      if (p.x > max_x) max_x = p.x;
      if (p.y > max_y) max_y = p.y;
    }
    CHECK(samples == 1600); // KONTROL: dongu gercekten kostu
    CHECK(invalid == 0);
    CHECK(out_of_range == 0);
    CHECK(non_monotonic == 0);
    // Basamak sayisi onemli: %.5f ile 1x1 dokudaki 0.99999994 "1.00000" diye
    // gorunur ve kapi tasiyor SANILIR. Pay (doku_olcusu - x) ayrica basilir.
    std::printf("    [bilgi] viewport tasma: doku %ux%u, sinira en yakin iceri nokta x=%.8f y=%.8f (pay %.8f / %.8f, sinir %u / %u), tarama %u ornek, "
                "tasan %u, gecersiz %u, geri giden %u, en buyuk x=%.6f y=%.6f\n",
                TW, TH, (double)last.x, (double)last.y, (double)((float)TW - last.x), (double)((float)TH - last.y), TW, TH, samples, out_of_range,
                invalid, non_monotonic, (double)max_x, (double)max_y);
  }
}

// Kurulmamis viewport (init() cagrilmadi): uye map_mouse SOZLESMENIN gecersiz
// bicimini dondurmeli — panel gecerli olsa bile. Cihaz gerektirmez.
// KONTROL: ayni girdi saf fonksiyona verilince GECERLI cikiyor, yani
// gecersizlik girdiden degil KURULMAMIS hedeften geliyor.
ENGINE_TEST(editor_viewport_uninitialized_maps_to_sentinel) {
  app::EditorViewport vp;
  const app::ViewportRect panel{0.0f, 0.0f, 800.0f, 600.0f};
  const app::ViewportPick p = vp.map_mouse(panel, 400.0f, 300.0f);
  const app::ViewportPick pure = app::viewport_map_mouse(panel, 400.0f, 300.0f, 800, 600);
  vp_print("kurulmamis uye map_mouse", p);
  vp_print("KONTROL saf fonksiyon", pure);
  CHECK(!vp.ok());
  CHECK(vp.width() == 0 && vp.height() == 0);
  CHECK(vp.recreate_count() == 0 && vp.resize_requests() == 0);
  CHECK(vp.aspect() == 1.0f); // h_ == 0: bolme yok
  CHECK(vp_sentinel(p));
  CHECK(pure.valid && vp_near(pure.u, 0.5f, 1e-6f) && vp_near(pure.x, 400.0f, 1e-3f)); // KONTROL
  std::printf("    [bilgi] viewport kurulmamis: ok=%d olcu %ux%u, map_mouse sentinel=%d; KONTROL ayni girdi saf fonksiyonda gecerli=%d (x=%.3f)\n",
              (int)vp.ok(), vp.width(), vp.height(), (int)vp_sentinel(p), (int)pure.valid, (double)pure.x);
}

// NaN fare konumu GECERSIZ olmali. Bu kapi bir sondayla bulundu: olumsuz
// yazilmis sinir kontrolu (`dx < 0 || dx >= w`) NaN icin HER IKI kosulda da
// false donuyor, yani NaN "panel icinde" sayilip NaN isin atiliyordu. Bugun
// ImGui bu yola NaN vermiyor (fare yokken -FLT_MAX veriyor), yani erisilebilir
// bir hata degildi — ama sessiz ve teshisi zor bir sinif oldugu icin kapisi var.
ENGINE_TEST(editor_viewport_nan_mouse_is_invalid) {
  const app::ViewportRect panel{0, 0, 800, 600};
  const float nan = std::nanf("");
  const app::ViewportPick n1 = app::viewport_map_mouse(panel, nan, 300.0f, 800, 600);
  const app::ViewportPick n2 = app::viewport_map_mouse(panel, 400.0f, nan, 800, 600);
  const app::ViewportPick n3 = app::viewport_map_mouse(panel, nan, nan, 800, 600);
  std::printf("    [bilgi] NaN fare: x-nan gecerli=%d, y-nan gecerli=%d, ikisi-nan gecerli=%d (hepsi 0 olmali)\n",
              (int)n1.valid, (int)n2.valid, (int)n3.valid);
  CHECK(!n1.valid && !n2.valid && !n3.valid);
  CHECK(n1.u == -1.0f && n2.u == -1.0f && n3.u == -1.0f);
  // KONTROL: ayni panelde NORMAL nokta hala gecerli — reddin sebebi NaN,
  // "her seyi reddeden" bir koruma degil.
  const app::ViewportPick ok = app::viewport_map_mouse(panel, 400.0f, 300.0f, 800, 600);
  std::printf("    [bilgi] KONTROL normal nokta: gecerli=%d u=%.5f x=%.1f\n", (int)ok.valid, (double)ok.u, (double)ok.x);
  CHECK(ok.valid && ok.x == 400.0f && ok.y == 300.0f);
  // +inf de gecersiz kalmali (olumlu kontrol onu da eliyor).
  const float inf = 1.0f / 0.0f;
  CHECK(!app::viewport_map_mouse(panel, inf, 300.0f, 800, 600).valid);
}
