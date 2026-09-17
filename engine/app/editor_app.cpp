#include "app/editor_app.hpp"

#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <imgui.h>
#include <ImGuizmo.h>

#include "app/demo_scene.hpp"
#include "app/editor_ui.hpp"
#include "content/gltf.hpp"
#include "content/scene.hpp"
#include "content/scene_blob.hpp"
#include "core/jobs/job_system.hpp"
#include "core/memory/arena.hpp"
#include "core/profiler/profiler.hpp"
#include "platform/time.hpp"
#include "rhi/device.hpp"
#include "app/editor_layout.hpp"
#include "app/editor_viewport.hpp"
#include "rhi/offscreen.hpp"
#include "rhi/swapchain.hpp"
#include "sim/schedule.hpp"

namespace tulpar::engine::app {

namespace {
constexpr float kPi = 3.14159265358979f;
using content::SceneDesc;
using content::SceneEntity;

struct Cam {
  float yaw = 0.7f, pitch = 0.45f, radius = 26.0f;
  Vec3 target{0, 1.0f, -3.0f};
  Vec3 eye() const {
    return {target.x + std::cos(pitch) * std::sin(yaw) * radius, target.y + std::sin(pitch) * radius,
            target.z + std::cos(pitch) * std::cos(yaw) * radius};
  }
  Mat4 view() const { return Mat4::look_at(eye(), target, {0, 1, 0}); }
};
struct RecordCtx {
  renderer::Renderer *r;
  EditorUi *ui;
  EditorViewport *vp;
  rhi::Device *dev;
};
// ANA GECIS ARTIK YALNIZ ImGui ICERIYOR. 3B sahne kendi VIEWPORT gecisine,
// yani ImGui'nin doku olarak ornekleyebilecegi offscreen hedefe ciziliyor.
// Onceden ucu de (3B + HUD + ImGui) ayni renk subpass'indeydi: sahne tam ekran,
// ImGui ustunde yuzuyordu — "debug kaplamali oyun" modeli. Sahne bir panele o
// yuzden konamiyordu.
void record_cb(VkCommandBuffer cb, void *user) {
  auto *c = static_cast<RecordCtx *>(user);
  // SUBPASS'I BIZ ILERLETIYORUZ. Ana gecis (swapchain ve offscreen, ikisi de)
  // IKI subpass tanimliyor: derinlik on-gecisi + renk. Eskiden ikinciye
  // `Renderer::record` geciyordu (renderer.cpp:2407) ve ImGui'nin boru hatti da
  // subpass 1 icin kurulmustu. Renderer artik VIEWPORT gecisine cizdigi icin
  // ilerleten kimse kalmadi: ImGui subpass 0'da cizmeye calisip
  // VUID-vkCmdDrawIndexed-subpass-02685 veriyordu ve KARE TAMAMEN SIYAH
  // cikiyordu (olculdu: 9910 benzersiz renk -> 1). Ustelik Vulkan iki subpass'li
  // bir gecisi subpass 0'da BITIRMEYE de izin vermez, yani ilerletmek sart.
  c->dev->api().vkCmdNextSubpass(cb, VK_SUBPASS_CONTENTS_INLINE);
  c->ui->record(cb);
}
// Ana gecis BASLAMADAN once kosan kayit: kendi gecisi olan her sey burada.
// SIRA ONEMLI — golge once, sonra viewport; ikisi de ayri gecis.
void before_cb(VkCommandBuffer cb, void *user) {
  auto *c = static_cast<RecordCtx *>(user);
  c->r->record_shadow(cb);
  if (c->vp->begin_pass(cb)) {
    c->r->record(cb);
    c->r->ui_record(cb);
    c->vp->end_pass(cb);
  }
}

void entity_from_matrix(SceneEntity &e, const Mat4 &mat) {
  ImGuizmo::DecomposeMatrixToComponents(&mat.m[0][0], &e.pos.x, &e.rot_deg.x, &e.scale.x);
}

// Editor durumu: veri modeli (gercek) + turetilmis sim/gpu kaynaklari.
struct EditorState {
  SceneDesc scene;
  content::SceneHistory hist;
  char scene_path[1024];
  char scene_dir[1024];
  content::Model models[content::kSceneMaxAssets];
  content::UploadedModel ups[content::kSceneMaxAssets];
  bool have[content::kSceneMaxAssets];
  content::PoseScratch pose_scratch;
  sim::BodyId bodies[content::kSceneMaxEntities];
  bool bodies_live = false;
  Selection sel;   // coklu secim: items[0] = ana secili (gizmo ona bagli)
  OpGroups groups; // bir kullanici eylemi = gunlukte N islem (grup tasima/silme)
  bool playing = false, dirty = false;
  float play_time = 0;
  // Surukleme / metin duzenleme: aktiflesince kopya, birakinca tek islem.
  SceneEntity edit_before;
  bool edit_active = false, gizmo_was_using = false;
  // Grup suruklemesi: surukleme basindaki kopyalar; bitince selection_commit ile
  // gunluge TEK grup (her karede degil).
  int32_t drag_items[Selection::kMax] = {};
  SceneEntity drag_before[Selection::kMax], drag_after[Selection::kMax];
  uint32_t drag_count = 0;
  content::SceneWorld world_before; // Dunya paneli surukleme/metin: tek islem
  bool world_edit_active = false;
  bool prev_lmb = false;
  GizmoOptions gizmos;      // isik yaricapi / golge hacmi / gunes yonu
  uint32_t gizmo_draws = 0; // son karede gizmolarin yaptigi cizim sayisi
  AssetFile browse[64];     // kaynak tarayici (sahne dosyasinin dizini)
  uint32_t browse_count = 0;
  char status[160];
};

// Varlik silindikten / geri alindiktan sonra secimi gecerli tut.
void clamp_selection(EditorState &st) {
  for (uint32_t k = st.sel.count; k > 0; k--)
    if (st.sel.items[k - 1] >= (int32_t)st.scene.entity_count) st.sel.erase(st.sel.items[k - 1]);
}

// Fare pikselinden dunya isini (kamera tabanindan; matris tersi gerekmez).
void camera_ray(const Cam &cam, float fovy, float aspect, float mx, float my, float fw, float fh, Vec3 *origin, Vec3 *dir) {
  const Vec3 eye = cam.eye();
  const Vec3 f = normalize(cam.target - eye);
  const Vec3 r = normalize(cross(f, Vec3{0, 1, 0}));
  const Vec3 u = cross(r, f);
  const float th = std::tan(fovy * 0.5f);
  const float nx = (2.0f * mx / fw - 1.0f) * th * aspect;
  const float ny = (1.0f - 2.0f * my / fh) * th;
  *origin = eye;
  *dir = normalize(f + r * nx + u * ny);
}
// Tum varliklarin dunya AABB'si (secim icin) — model sinirlari yuklu modelden.
uint32_t entity_world_bounds(const EditorState &st, const sim::Physics &phys, content::SceneBounds *out) {
  for (uint32_t i = 0; i < st.scene.entity_count; i++) {
    const SceneEntity &e = st.scene.entities[i];
    const content::SceneBounds *mb = nullptr;
    content::SceneBounds mbs;
    if ((e.components & content::kSceneModel) && e.asset >= 0 && e.asset < (int32_t)st.scene.asset_count && st.have[e.asset]) {
      mbs = {st.models[e.asset].bounds_min, st.models[e.asset].bounds_max};
      mb = &mbs;
    }
    const bool simulated = st.playing && st.bodies_live && st.bodies[i].valid() && e.dynamic;
    const Mat4 m = simulated ? content::scene_body_matrix(e, phys, st.bodies[i]) : content::scene_entity_matrix(e);
    out[i] = content::scene_world_bounds(content::scene_entity_local_bounds(e, mb), m);
  }
  return st.scene.entity_count;
}

void set_status(EditorState &st, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
void set_status(EditorState &st, const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  std::vsnprintf(st.status, sizeof st.status, fmt, ap);
  va_end(ap);
}

void bodies_spawn(EditorState &st, sim::Physics &ph) {
  if (st.bodies_live) return;
  content::scene_spawn_bodies(st.scene, ph, st.bodies);
  st.bodies_live = true;
}
void bodies_remove(EditorState &st, sim::Physics &ph) {
  if (!st.bodies_live) return;
  content::scene_remove_bodies(ph, st.bodies, st.scene.entity_count);
  st.bodies_live = false;
}
// Varlik listesini degistiren islemler (ekle/sil/geri al/yinele) oynarken
// govde indekslerini kaydirir: once govdeler cikar, sonra yeniden girer.
template <class F> void with_bodies(EditorState &st, sim::Physics &ph, F &&f) {
  const bool live = st.bodies_live;
  if (live) bodies_remove(st, ph);
  f();
  if (live) bodies_spawn(st, ph);
}

// Ozellik paneli: surukleme/metin girisi bir islem olarak gunluge girer.
void track_edit(EditorState &st, SceneEntity &e, int index) {
  if (ImGui::IsItemActivated() && !st.edit_active) { st.edit_before = e; st.edit_active = true; }
  if (ImGui::IsItemDeactivated() && st.edit_active) {
    st.edit_active = false;
    if (ImGui::IsItemDeactivatedAfterEdit()) {
      const SceneEntity after = e;
      e = st.edit_before;
      if (st.hist.set_entity(st.scene, (uint32_t)index, after)) { st.groups.push(1); st.dirty = true; }
    }
  }
}
// Dunya paneli (gunes/ortam/golge): ayni kural, SceneOp::World olarak gunluge.
void track_world_edit(EditorState &st) {
  if (ImGui::IsItemActivated() && !st.world_edit_active) { st.world_before = st.scene.world(); st.world_edit_active = true; }
  if (ImGui::IsItemDeactivated() && st.world_edit_active) {
    st.world_edit_active = false;
    if (ImGui::IsItemDeactivatedAfterEdit()) {
      const content::SceneWorld after = st.scene.world();
      st.scene.set_world(st.world_before);
      if (st.hist.set_world(st.scene, after)) { st.groups.push(1); st.dirty = true; }
    }
  }
}
// Ayrik widget (onay kutusu, secim): kopya uzerinde degisiklik, hemen islem.
bool commit(EditorState &st, int index, const SceneEntity &after) {
  if (!st.hist.set_entity(st.scene, (uint32_t)index, after)) return false;
  st.groups.push(1);
  st.dirty = true;
  return true;
}
} // namespace

int editor_run(const EditorOptions &opts, const EditorHost *host) {
  const bool headless = host == nullptr || opts.headless_frames > 0;
  static SystemArena sys;
  if (!sys.reserve(512u << 20, "editor")) { std::fprintf(stderr, "arena\n"); return 1; }
  FrameArena frame;
  sys.carve(frame, 8u << 20, "frame");
  JobSystem jobs;
  if (!jobs.init(sys, JobSystemConfig{})) { std::fprintf(stderr, "job sistemi\n"); return 1; }
  Profiler prof;
  ProfilerConfig pc;
  pc.frame_capacity = 600;
  pc.zone_capacity = 32768;
  prof.init(sys, pc);

  rhi::VkApi api;
  if (!rhi::vk_api_load(api)) { std::fprintf(stderr, "Vulkan loader yok\n"); return 1; }
  rhi::DeviceConfig dc;
  dc.validation = opts.validation;
  dc.best_practices = opts.validation;
  if (!headless) {
    uint32_t n = 0;
    dc.instance_extensions = host->instance_extensions(host->user, &n);
    dc.instance_extension_count = n;
  }
  rhi::Device dev;
  if (!dev.init_instance(api, dc)) { std::fprintf(stderr, "instance: %s\n", dev.last_error()); return 1; }
  VkSurfaceKHR surface = VK_NULL_HANDLE;
  if (!headless && !host->create_surface(host->user, api, dev.instance(), &surface)) { std::fprintf(stderr, "yuzey\n"); return 1; }
  if (!dev.init_device(surface)) { std::fprintf(stderr, "cihaz: %s\n", dev.last_error()); return 1; }
  std::printf("[engine_editor] GPU: %s (Vulkan %u.%u)\n", dev.caps().device_name, VK_API_VERSION_MAJOR(dev.caps().api_version),
              VK_API_VERSION_MINOR(dev.caps().api_version));

  uint32_t width = opts.width, height = opts.height;
  rhi::Swapchain swap;
  rhi::OffscreenTarget *off = nullptr;
  rhi::OffscreenConfig oc;
  rhi::OffscreenResult ores;
  VkRenderPass rp = VK_NULL_HANDLE;
  uint32_t image_count = 2;
  if (headless) {
    oc.width = width; oc.height = height; oc.srgb = true;
    off = rhi::offscreen_create(dev, sys, oc, &ores);
    if (!off) { std::fprintf(stderr, "offscreen: %s\n", ores.error); return 1; }
    rp = rhi::offscreen_render_pass(off);
  } else {
    uint32_t fw = 0, fh = 0;
    host->poll(host->user, &fw, &fh);
    if (!swap.init(dev, sys, surface, fw ? fw : width, fh ? fh : height)) { std::fprintf(stderr, "swapchain\n"); return 1; }
    rp = swap.render_pass();
    image_count = swap.image_count();
    width = swap.extent().width; height = swap.extent().height;
  }
  // --- VIEWPORT ONCE KURULUR, RENDERER ONUN GECISINE GORE ---------------------
  // Vulkan'da gecis UYUMLULUGU subpass BAGIMLILIKLARINI da kapsar, yalniz
  // formatlari degil. Renderer'i swapchain gecisine gore kurup viewport
  // gecisine kaydetmek dogrulama katmanini kiriyor (olculdu):
  //   srcStageMask incompatible: FRAGMENT_SHADER_BIT != COLOR_ATTACHMENT_OUTPUT_BIT
  // Bu yuzden sira: vp.init -> ren.init(vp.render_pass()) -> ui.init(rp).
  // vp.render_pass() yeniden boyutlanmada YENIDEN YARATILMAZ, yani renderer'in
  // boru hatlari panel olcusu degisince gecerli kalir.
  static EditorViewport vp;
  EditorViewportConfig vc;
  if (!vp.init(dev, vc, width, height)) { std::fprintf(stderr, "viewport: %s\n", vp.last_error()); return 1; }

  renderer::Renderer ren;
  renderer::RendererConfig rc;
  rc.srgb_target = true; // hedef ARTIK viewport ve o *_SRGB (bkz. EditorViewportConfig)
  if (!ren.init(dev, sys, vp.render_pass(), rc)) { std::fprintf(stderr, "renderer\n"); return 1; }
  ren.set_render_size(vp.width(), vp.height());

  // --- Sahne dosyasi (veri modeli) ---
  static EditorState st;
  st.hist.init(sys, 256);
  const char *adir = std::getenv("TULPAR_ENGINE_ASSETS");
  if (opts.scene_path) std::snprintf(st.scene_path, sizeof st.scene_path, "%s", opts.scene_path);
  else if (adir && *adir) std::snprintf(st.scene_path, sizeof st.scene_path, "%s/editor.sahne", adir);
  else std::snprintf(st.scene_path, sizeof st.scene_path, "%s/tests/assets/editor.sahne", ENGINE_SOURCE_DIR);
  content::scene_dir_of(st.scene_path, st.scene_dir, sizeof st.scene_dir);
  {
    content::SceneError err{};
    if (!content::scene_load(sys, st.scene_path, &st.scene, &err)) { std::fprintf(stderr, "sahne %s: %s\n", st.scene_path, err.msg); return 1; }
  }
  char path[1024];
  auto asset = [&](const char *name) {
    std::snprintf(path, sizeof path, "%s/%s", st.scene_dir, name);
    return path;
  };
  // Kaynak yukleme (glTF -> GPU); tarayicidan eklenen kaynak da buradan gecer.
  auto load_asset = [&](int32_t i) {
    if (i < 0 || i >= (int32_t)st.scene.asset_count) return false;
    st.have[i] = content::gltf_load(sys, asset(st.scene.assets[i]), &st.models[i]) && content::upload_model(ren, sys, st.models[i], &st.ups[i]);
    if (!st.have[i]) std::printf("[engine_editor] kaynak yuklenemedi: %s\n", st.scene.assets[i]);
    return st.have[i];
  };
  for (uint32_t i = 0; i < st.scene.asset_count; i++) load_asset((int32_t)i);
  st.browse_count = editor_scan_assets(st.scene_dir, st.scene, st.browse, 64);
  std::printf("[engine_editor] sahne %s: %u varlik, %u kaynak\n", st.scene_path, st.scene.entity_count, st.scene.asset_count);

  // Arka plan: demo sahnesi (ajanlar, Jolt kutulari) — govdeler ayni fizik dunyasina.
  DemoScene::DrawSet ds;
  renderer::Vertex v[24];
  uint32_t idx[36];
  uint32_t n = renderer::Renderer::cube(v, idx);
  ds.cube = ren.create_mesh(v, 24, idx, n);
  n = renderer::Renderer::plane(v, idx, 8.0f);
  ds.plane = ren.create_mesh(v, 4, idx, n);
  {
    static uint8_t px[64 * 64 * 4];
    for (uint32_t i = 0; i < 64 * 64; i++) { uint8_t c = ((i % 64) / 8 + (i / 64) / 8) % 2 ? 200 : 90; px[i * 4] = px[i * 4 + 1] = px[i * 4 + 2] = c; px[i * 4 + 3] = 255; }
    ds.ground = ren.create_material(ren.create_texture(px, 64, 64, true), {1, 1, 1});
  }
  ds.box_mesh = ds.cube;
  ds.box_mat = ren.default_material();
  {
    for (uint32_t i = 0; i < st.scene.asset_count; i++)
      if (st.have[i] && std::strstr(st.scene.assets[i], "checker_cube") && st.ups[i].mesh_count) { ds.box_mesh = st.ups[i].meshes[0]; ds.box_mat = st.ups[i].materials[0]; }
  }
  DemoScene scene;
  if (!scene.init(sys, &jobs)) { std::fprintf(stderr, "sahne\n"); return 1; }
  sim::Physics &phys = scene.physics();

  EditorUi ui;
  {
    char fpath[1024];
    std::snprintf(fpath, sizeof fpath, "%s/assets/fonts/DejaVuSans.ttf", ENGINE_SOURCE_DIR);
    if (!ui.init(dev, rp, 1, image_count, fpath, 17.0f)) { std::fprintf(stderr, "editor ui: %s\n", ui.last_error()); return 1; }
  }

  Cam cam;
  cam.target = st.scene.cam_target; cam.yaw = st.scene.cam_yaw; cam.pitch = st.scene.cam_pitch; cam.radius = st.scene.cam_radius;
  if (headless) st.sel.set_single(0);
  st.playing = headless;
  if (st.playing) bodies_spawn(st, phys);
  if (headless && st.scene.entity_count) {
    // Betikli durum: bir islem + geri al (gunluk kapali dongude calisiyor mu).
    SceneEntity e = st.scene.entities[0];
    e.pos.x += 1.0f;
    if (st.hist.set_entity(st.scene, 0, e)) st.groups.push(1);
    st.groups.undo_size(); // grup muhasebesi gunlukle hizali kalsin
    st.hist.undo(st.scene);
  }
  set_status(st, "yuklendi: %s", st.scene_path);
  int gizmo_op = 0; // 0 tasi, 1 dondur, 2 olcekle
  sim::FixedStep fs;
  uint64_t last_ns = platform::now_ns();
  uint32_t frame_i = 0, tick_i = 0;
  double prev_mx = 0, prev_my = 0;
  bool prev_rmb = false;
  RecordCtx rctx{&ren, &ui, &vp, &dev};
  bool running = true;
  auto set_playing = [&](bool p) {
    if (p == st.playing) return;
    st.playing = p;
    if (p) { st.play_time = 0; bodies_spawn(st, phys); }
    else bodies_remove(st, phys); // durdur: veri modeli (yazar donusumu) gecerli
  };
  // Geri al / yinele GRUP isler: bir kullanici eylemi gunlukte N islem olabilir
  // (grup tasima = N set_entity, grup silme = N remove_entity). Sinirlar OpGroups'ta;
  // gunluk once biterse dongu orada durur (grup muhasebesi gunlugun onune gecemez).
  auto do_undo = [&]() {
    if (st.hist.undo_count() == 0) return;
    const uint32_t k = st.groups.undo_size();
    uint32_t done = 0;
    with_bodies(st, phys, [&] {
      for (uint32_t i = 0; i < k && st.hist.undo(st.scene); i++) done++;
    });
    if (done) { st.dirty = true; clamp_selection(st); set_status(st, "geri alindi (%u islem, %u kaldi)", done, st.hist.undo_count()); }
  };
  auto do_redo = [&]() {
    if (st.hist.redo_count() == 0) return;
    const uint32_t k = st.groups.redo_size();
    uint32_t done = 0;
    with_bodies(st, phys, [&] {
      for (uint32_t i = 0; i < k && st.hist.redo(st.scene); i++) done++;
    });
    if (done) { st.dirty = true; clamp_selection(st); set_status(st, "yinelendi (%u islem, %u kaldi)", done, st.hist.redo_count()); }
  };
  auto do_save = [&]() {
    st.scene.cam_target = cam.target; st.scene.cam_yaw = cam.yaw; st.scene.cam_pitch = cam.pitch; st.scene.cam_radius = cam.radius;
    content::SceneError err{};
    if (content::scene_save(frame, st.scene, st.scene_path, &err)) { st.dirty = false; set_status(st, "kaydedildi: %s", st.scene_path); }
    else set_status(st, "KAYDEDILEMEDI: %s", err.msg);
  };
  // Derle: veri modeli -> runtime blob (.sahneb, sahne dosyasinin yanina; PLAN §6).
  // Kaydet gibi kamerayi da yazar; dosyayi degil bellekteki sahneyi derler.
  auto do_compile = [&]() {
    st.scene.cam_target = cam.target; st.scene.cam_yaw = cam.yaw; st.scene.cam_pitch = cam.pitch; st.scene.cam_radius = cam.radius;
    char out[1024];
    if (!content::scene_blob_path_for(st.scene_path, out, sizeof out)) { set_status(st, "DERLENEMEDI: yol cok uzun"); return; }
    content::SceneError err{};
    if (!content::scene_blob_save(frame, st.scene, out, &err)) { set_status(st, "DERLENEMEDI: %s", err.msg); return; }
    content::SceneBlobView v;
    if (!content::scene_blob_load(frame, out, &v, &err)) { set_status(st, "DERLENDI ama acilamadi: %s", err.msg); return; }
    set_status(st, "derlendi: %s (%u bayt, %u cizim, %u isik, %u govde, ozet %08x)", out, v.h->total_size, v.h->draw_count, v.h->light_count,
               v.h->body_count, (unsigned)v.h->hash_lo);
  };
  auto do_add = [&]() {
    SceneEntity e{};
    const int32_t prim = st.sel.primary();
    if (prim >= 0) { e = st.scene.entities[prim]; std::snprintf(e.name, sizeof e.name, "%.24s_kopya", st.scene.entities[prim].name); e.pos.x += 1.5f; }
    else {
      std::snprintf(e.name, sizeof e.name, "nesne_%u", st.scene.entity_count + 1);
      e.pos = cam.target;
      if (st.scene.asset_count) { e.components = content::kSceneModel; e.asset = 0; }
    }
    with_bodies(st, phys, [&] {
      if (st.hist.add_entity(st.scene, e)) {
        st.groups.push(1);
        st.sel.set_single((int32_t)st.scene.entity_count - 1);
        st.dirty = true;
        set_status(st, "eklendi: %s", e.name);
      } else set_status(st, "eklenemedi (kapasite %u)", content::kSceneMaxEntities);
    });
  };
  // Grup silme: secilenlerin tamami, buyukten kucuge (indeksler kaymasin); gunluge
  // N islem ama TEK eylem (geri al hepsini birden getirir).
  auto do_remove = [&]() {
    if (st.sel.count == 0) return;
    int32_t idx[Selection::kMax];
    const uint32_t n = st.sel.sorted_desc(idx);
    uint32_t ops = 0;
    with_bodies(st, phys, [&] { ops = selection_remove(st.scene, st.hist, idx, n); });
    if (ops) {
      st.groups.push(ops);
      st.dirty = true;
      set_status(st, "silindi (%u varlik)", ops);
    }
    st.sel.clear();
  };
  // Kaynak tarayicidan ekleme: kaynak sahneye (varsa mevcut indeks) + o kaynakla
  // yeni varlik; kaynak henuz yuklenmemisse burada yuklenir.
  auto do_add_asset = [&](const char *file) {
    int32_t a = -1;
    uint32_t ops = 0;
    with_bodies(st, phys, [&] { ops = editor_add_asset_entity(st.scene, st.hist, file, cam.target, &a); });
    if (!ops) { set_status(st, "kaynak eklenemedi: %s", file); return; }
    st.groups.push(ops);
    st.dirty = true;
    if (a >= 0 && !st.have[a]) load_asset(a);
    st.sel.set_single((int32_t)st.scene.entity_count - 1);
    st.browse_count = editor_scan_assets(st.scene_dir, st.scene, st.browse, 64);
    set_status(st, "kaynak eklendi: %s (%s)", file, (a >= 0 && st.have[a]) ? "yuklendi" : "YUKLENEMEDI");
  };
  while (running) {
    ENGINE_ZONE("frame");
    uint64_t now = platform::now_ns();
    float dt = (float)((now - last_ns) / 1e9);
    last_ns = now;
    if (dt > 0.25f) dt = 0.25f;
    if (headless) dt = 1.0f / 60.0f;
    prof.begin_frame();
    frame.begin_frame();
    const platform::InputState *in = nullptr;
    uint32_t fw = width, fh = height;
    if (!headless) {
      if (!host->poll(host->user, &fw, &fh)) running = false;
      in = host->input ? host->input(host->user) : nullptr;
      if (fw == 0 || fh == 0) continue; // kucultulmus
    }
    // Kamera: sag fare surukle = yorunge, tekerlek = yakinlik (ImGui uzerinde degilken).
    if (in && !ui.wants_mouse()) {
      const bool rmb = in->mouse_down[1];
      if (rmb && prev_rmb) {
        cam.yaw -= (float)(in->mouse_x - prev_mx) * 0.005f;
        cam.pitch += (float)(in->mouse_y - prev_my) * 0.005f;
        if (cam.pitch < 0.05f) cam.pitch = 0.05f;
        if (cam.pitch > 1.5f) cam.pitch = 1.5f;
      }
      prev_rmb = rmb;
    }
    if (in) { prev_mx = in->mouse_x; prev_my = in->mouse_y; }
    static double prev_scroll = 0;
    if (in && !ui.wants_mouse()) {
      const double ds_ = in->scroll_y - prev_scroll;
      if (ds_ != 0) { cam.radius *= (float)std::pow(0.9, ds_); if (cam.radius < 3) cam.radius = 3; if (cam.radius > 80) cam.radius = 80; }
    }
    if (in) prev_scroll = in->scroll_y;
    if (in && !ui.wants_keyboard()) {
      if (in->key_down[GLFW_KEY_T]) gizmo_op = 0;
      if (in->key_down[GLFW_KEY_R]) gizmo_op = 1;
      if (in->key_down[GLFW_KEY_S] && !in->key_down[GLFW_KEY_LEFT_CONTROL] && !in->key_down[GLFW_KEY_RIGHT_CONTROL]) gizmo_op = 2;
    }

    // Sim: yalniz oynatilirken (sabit adim).
    if (st.playing) {
      ENGINE_ZONE("sim");
      uint32_t ticks = fs.advance(dt);
      for (uint32_t t = 0; t < ticks; t++) { scene.tick(fs.step_s, tick_i++); st.play_time += fs.step_s; }
    }

    // En-boy orani artik PENCERENIN degil, sahnenin icinde yasadigi PANELIN
    // orani: 3B viewport dokusuna ciziliyor ve o dokunun olcusu panelden geliyor.
    // Pencere oranini kullanmak sahneyi panelde gerilmis gosterirdi.
    const float aspect = vp.aspect();
    Mat4 proj = Mat4::perspective(kPi / 3.5f, aspect, 0.1f, 200.0f);
    Mat4 view = cam.view();
    ren.set_camera(view, proj);
    ren.clear_point_lights();

    // --- ImGui paneller ---
    ui.begin_frame(in, (float)fw, (float)fh, dt);
    // Kisayollar (metin girisi aktifken ImGui'nin kendi geri al'i calisir).
    if (!ImGui::GetIO().WantTextInput) {
      if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_S)) do_save();
      if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_Z)) do_undo();
      if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_Y) || ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_Z)) do_redo();
      if (ImGui::IsKeyChordPressed(ImGuiKey_Delete)) do_remove();
      if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_B)) do_compile();
      if (ImGui::IsKeyChordPressed(ImGuiKey_G)) { // gizmolarin tamami ac/kapa
        const bool on = !(st.gizmos.light_radius || st.gizmos.shadow_volume || st.gizmos.sun_dir);
        st.gizmos.light_radius = st.gizmos.shadow_volume = st.gizmos.sun_dir = on;
      }
      if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_A)) { // tumunu sec
        st.sel.clear();
        for (uint32_t i = st.scene.entity_count; i > 0; i--) st.sel.toggle((int32_t)(i - 1));
      }
      if (ImGui::IsKeyChordPressed(ImGuiKey_Escape)) st.sel.clear();
    }
    if (ImGui::BeginMainMenuBar()) {
      if (ImGui::Button(st.playing ? "Durdur" : "Oynat")) set_playing(!st.playing);
      if (ImGui::Button("Kaydet")) do_save();
      if (ImGui::Button("Geri al")) do_undo();
      if (ImGui::Button("Yinele")) do_redo();
      if (ImGui::Button("Derle")) do_compile();
      char selbuf[72];
      const int32_t prim_i = st.sel.primary();
      if (prim_i >= 0 && st.sel.count > 1) std::snprintf(selbuf, sizeof selbuf, "%s +%u", st.scene.entities[prim_i].name, st.sel.count - 1);
      else std::snprintf(selbuf, sizeof selbuf, "%s", prim_i >= 0 ? st.scene.entities[prim_i].name : "-");
      ImGui::Text("| %s%s | gunluk %u/%u | kare %u | tick %u | gizmo %s (T/R/S) | %s", st.dirty ? "*" : "", selbuf, st.hist.undo_count(),
                  st.hist.redo_count(), frame_i, tick_i, gizmo_op == 0 ? "tasi" : gizmo_op == 1 ? "dondur" : "olcekle", st.status);
      ImGui::EndMainMenuBar();
    }
    // --- DOCKSPACE -----------------------------------------------------------
    // Paneller artik ekranda yuzen sabit pencereler degil; kullanici surukleyip
    // yeniden duzenleyebiliyor (Unity/Godot/Blender'in dordunde de boyle).
    // Varsayilan yerlesim ILK KAREDE programatik kuruluyor: ImGui'nin kendi
    // imgui.ini'si kapali (io.IniFilename = nullptr), yani duzen diskten
    // gelmiyor — kurulmazsa her acilista paneller serbest gelirdi.
    const ImGuiID dock_id = ImGui::DockSpaceOverViewport(ImGui::GetID("TulparDock"), ImGui::GetMainViewport(), 0);
    if (frame_i == 0) {
      layout_set_dockspace_id(dock_id);
      if (!layout_apply_default(dock_id, (float)fw, (float)fh))
        std::fprintf(stderr, "[editor] varsayilan duzen: %s\n", layout_last_error());
    }

    // --- GORUNUM: 3B sahnenin YASADIGI panel --------------------------------
    // Sahne viewport dokusuna ciziliyor ve burada gosteriliyor. Panel olcusu
    // degisince hedef yeniden yaratiliyor (yalniz GERCEKTEN degistiyse) ve
    // renderer'in cizim olcusu ona baglaniyor — en-boy orani artik pencerenin
    // degil PANELIN orani.
    ViewportRect view_rect{};
    bool view_hovered = false;
    if (ImGui::Begin(kPanelGorunum)) {
      const ImVec2 avail = ImGui::GetContentRegionAvail();
      const ImVec2 origin = ImGui::GetCursorScreenPos();
      if (avail.x >= 1.0f && avail.y >= 1.0f) {
        vp.resize((uint32_t)avail.x, (uint32_t)avail.y);
        ren.set_render_size(vp.width(), vp.height());
        if (vp.texture_id()) {
          ImGui::Image((ImTextureID)vp.texture_id(), ImVec2((float)vp.width(), (float)vp.height()));
          view_hovered = ImGui::IsItemHovered();
        } else {
          ImGui::TextUnformatted(vp.last_error()); // sessiz siyah panel YOK
        }
        view_rect = ViewportRect{origin.x, origin.y, (float)vp.width(), (float)vp.height()};
      }
    }
    ImGui::End();

    if (ImGui::Begin("Sahne")) {
      if (ImGui::Button("Ekle")) do_add();
      ImGui::SameLine();
      if (ImGui::Button("Sil")) do_remove();
      ImGui::Separator();
      for (uint32_t i = 0; i < st.scene.entity_count; i++) {
        ImGui::PushID((int)i);
        // Ctrl+tik: secime ekle/cikar (3B tiklamayla ayni kural).
        if (ImGui::Selectable(st.scene.entities[i].name, st.sel.contains((int32_t)i))) {
          if (ImGui::GetIO().KeyCtrl) st.sel.toggle((int32_t)i);
          else st.sel.set_single((int32_t)i);
        }
        ImGui::PopID();
      }
      ImGui::Separator();
      ImGui::Text("secili %u (Ctrl+tik ekler, Ctrl+A tumu, Esc temizler)", st.sel.count);
      ImGui::Text("sim: %u entity", scene.entities());
      ImGui::Text("kamera %.1f/%.1f/%.1f", cam.eye().x, cam.eye().y, cam.eye().z);
    }
    ImGui::End();
    if (ImGui::Begin("Ozellikler")) {
      const int si = (int)st.sel.primary();
      if (si >= 0 && si < (int)st.scene.entity_count) {
        SceneEntity &e = st.scene.entities[si];
        if (st.sel.count > 1) ImGui::Text("grup: %u secili — alanlar ana secilide, gizmo grubu tasir", st.sel.count);
        ImGui::InputText("ad", e.name, sizeof e.name); track_edit(st, e, si);
        ImGui::DragFloat3("konum", &e.pos.x, 0.05f); track_edit(st, e, si);
        ImGui::DragFloat3("donus", &e.rot_deg.x, 0.5f); track_edit(st, e, si);
        ImGui::DragFloat3("olcek", &e.scale.x, 0.02f, 0.05f, 20.0f); track_edit(st, e, si);
        ImGui::Separator();
        SceneEntity after = e;
        bool has = (e.components & content::kSceneModel) != 0;
        if (ImGui::Checkbox("model", &has)) { after.components ^= content::kSceneModel; if (has && after.asset < 0) after.asset = 0; commit(st, si, after); }
        if (e.components & content::kSceneModel) {
          int a = e.asset;
          if (ImGui::SliderInt("kaynak", &a, 0, (int)st.scene.asset_count - 1, st.scene.asset_count && a >= 0 ? st.scene.assets[a] : "-")) { after.asset = a; commit(st, si, after); }
          ImGui::ColorEdit3("renk", &e.tint.x, ImGuiColorEditFlags_NoInputs); track_edit(st, e, si);
        }
        after = e;
        has = (e.components & content::kSceneAnim) != 0;
        if (ImGui::Checkbox("animasyon", &has)) { after.components ^= content::kSceneAnim; commit(st, si, after); }
        if (e.components & content::kSceneAnim) {
          ImGui::DragFloat("faz", &e.phase, 0.01f, 0.0f, 10.0f); track_edit(st, e, si);
          ImGui::DragFloat("hiz", &e.speed, 0.01f, 0.0f, 10.0f); track_edit(st, e, si);
        }
        after = e;
        has = (e.components & content::kSceneLight) != 0;
        if (ImGui::Checkbox("isik", &has)) { after.components ^= content::kSceneLight; commit(st, si, after); }
        if (e.components & content::kSceneLight) {
          ImGui::ColorEdit3("isik rengi", &e.light_color.x, ImGuiColorEditFlags_NoInputs); track_edit(st, e, si);
          ImGui::DragFloat("siddet", &e.light_intensity, 0.05f, 0.0f, 100.0f); track_edit(st, e, si);
          ImGui::DragFloat("yaricap", &e.light_radius, 0.05f, 0.1f, 100.0f); track_edit(st, e, si);
        }
        after = e;
        has = (e.components & content::kSceneBody) != 0;
        if (ImGui::Checkbox("govde", &has)) { after.components ^= content::kSceneBody; commit(st, si, after); }
        if (e.components & content::kSceneBody) {
          int shape = (int)e.shape;
          if (ImGui::Combo("sekil", &shape, "kutu\0kure\0")) { after.shape = (content::SceneShape)shape; commit(st, si, after); }
          if (e.shape == content::SceneShape::Box) { ImGui::DragFloat3("yarim kenar", &e.half.x, 0.02f, 0.01f, 50.0f); track_edit(st, e, si); }
          else { ImGui::DragFloat("yaricap ", &e.radius, 0.02f, 0.01f, 50.0f); track_edit(st, e, si); }
          bool dyn = e.dynamic;
          if (ImGui::Checkbox("dinamik", &dyn)) { after.dynamic = dyn; commit(st, si, after); }
        }
      } else ImGui::TextUnformatted("Sahne listesinden sec");
    }
    ImGui::End();
    // Dunya paneli: gunes/ortam/golge (gunluge SceneOp::World), kamera (canli; sahneye yazmak ayri islem).
    if (ImGui::Begin("Dunya")) {
      ImGui::TextUnformatted("Isik");
      ImGui::DragFloat3("gunes yonu", &st.scene.sun_dir.x, 0.01f, -1.0f, 1.0f); track_world_edit(st);
      ImGui::DragFloat("gunes", &st.scene.sun_diffuse, 0.01f, 0.0f, 5.0f); track_world_edit(st);
      ImGui::ColorEdit3("ortam", &st.scene.ambient.x, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_Float); track_world_edit(st);
      ImGui::Separator();
      ImGui::TextUnformatted("Golge hacmi");
      ImGui::DragFloat3("merkez", &st.scene.shadow_center.x, 0.1f); track_world_edit(st);
      ImGui::DragFloat("yaricap", &st.scene.shadow_radius, 0.1f, 1.0f, 500.0f); track_world_edit(st);
      ImGui::DragFloat("derinlik", &st.scene.shadow_depth, 0.5f, 1.0f, 2000.0f); track_world_edit(st);
      ImGui::Separator();
      ImGui::TextUnformatted("Gizmolar (G)");
      ImGui::Checkbox("isik yaricapi", &st.gizmos.light_radius);
      ImGui::Checkbox("golge hacmi", &st.gizmos.shadow_volume);
      ImGui::Checkbox("gunes yonu", &st.gizmos.sun_dir);
      ImGui::Text("gizmo cizimi: %u", st.gizmo_draws);
      ImGui::Separator();
      ImGui::TextUnformatted("Kamera (canli)");
      ImGui::DragFloat3("hedef", &cam.target.x, 0.05f);
      ImGui::DragFloat("yaw", &cam.yaw, 0.01f);
      ImGui::DragFloat("pitch", &cam.pitch, 0.01f, 0.05f, 1.5f);
      ImGui::DragFloat("uzaklik", &cam.radius, 0.1f, 3.0f, 80.0f);
      if (ImGui::Button("sahneye yaz")) {
        content::SceneWorld w = st.scene.world();
        w.cam_target = cam.target; w.cam_yaw = cam.yaw; w.cam_pitch = cam.pitch; w.cam_radius = cam.radius;
        if (st.hist.set_world(st.scene, w)) { st.dirty = true; set_status(st, "kamera sahneye yazildi"); }
      }
      ImGui::SameLine();
      if (ImGui::Button("sahnedekine git")) { cam.target = st.scene.cam_target; cam.yaw = st.scene.cam_yaw; cam.pitch = st.scene.cam_pitch; cam.radius = st.scene.cam_radius; }
    }
    ImGui::End();
    // Kaynak tarayici: sahne dosyasinin dizinindeki glTF'ler + sahnenin kaynak
    // tablosu (yuklendi/yuklenemedi). Ekleme dongu icinde yapilmaz (liste
    // yeniden taranir): secilen dosya adi kopyalanip donguden sonra islenir.
    if (ImGui::Begin("Kaynaklar")) {
      char add_file[content::kScenePathLen] = {0};
      ImGui::Text("dizin: %s", st.scene_dir);
      if (ImGui::Button("Yenile")) st.browse_count = editor_scan_assets(st.scene_dir, st.scene, st.browse, 64);
      ImGui::Separator();
      ImGui::Text("Dizindeki glTF (%u)", st.browse_count);
      for (uint32_t i = 0; i < st.browse_count; i++) {
        ImGui::PushID(0x4000 + (int)i);
        if (ImGui::SmallButton("Ekle")) std::snprintf(add_file, sizeof add_file, "%s", st.browse[i].name);
        ImGui::SameLine();
        ImGui::Text("%s%s", st.browse[i].name, st.browse[i].in_scene ? " (sahnede)" : "");
        ImGui::PopID();
      }
      if (!st.browse_count) ImGui::TextUnformatted("  (.gltf/.glb yok)");
      ImGui::Separator();
      ImGui::Text("Sahnedeki kaynaklar (%u)", st.scene.asset_count);
      for (uint32_t i = 0; i < st.scene.asset_count; i++)
        ImGui::Text("  %u %s — %s", i, st.scene.assets[i], st.have[i] ? "yuklendi" : "YUKLENEMEDI");
      if (add_file[0]) do_add_asset(add_file);
    }
    ImGui::End();
    // Gizmo: ImGuizmo GL gelenegi (NDC y yukari) bekler; Vulkan projeksiyonun y'si tersken duzeltilir.
    // Surukleme tek islem: IsUsing baslarken kopya, bitince gunluge.
    const int32_t gz = st.sel.primary();
    if (gz >= 0 && gz < (int32_t)st.scene.entity_count) {
      SceneEntity &e = st.scene.entities[gz];
      Mat4 proj_gl = proj;
      proj_gl.m[1][1] = -proj_gl.m[1][1];
      ImGuizmo::SetOrthographic(false);
      ImGuizmo::SetRect(0, 0, (float)fw, (float)fh);
      Mat4 mtx = content::scene_entity_matrix(e);
      const ImGuizmo::OPERATION op = gizmo_op == 0 ? ImGuizmo::TRANSLATE : gizmo_op == 1 ? ImGuizmo::ROTATE : ImGuizmo::SCALE;
      const bool changed = ImGuizmo::Manipulate(&view.m[0][0], &proj_gl.m[0][0], op, ImGuizmo::WORLD, &mtx.m[0][0]);
      const bool using_now = ImGuizmo::IsUsing();
      if (using_now && !st.gizmo_was_using) { // surukleme basi: grubun tamaminin kopyasi
        st.edit_before = e;
        st.drag_count = 0;
        for (uint32_t k = 0; k < st.sel.count; k++) { // bayat indeks varsa disarida kalir
          const int32_t i = st.sel.items[k];
          if (i < 0 || i >= (int32_t)st.scene.entity_count) continue;
          st.drag_items[st.drag_count] = i;
          st.drag_before[st.drag_count] = st.scene.entities[i];
          st.drag_count++;
        }
      }
      if (changed) {
        entity_from_matrix(e, mtx);
        // Grup: ana secilinin KONUM deltasi digerlerine (dondur/olcek ana varlikta kalir).
        const Vec3 delta = e.pos - st.edit_before.pos;
        for (uint32_t k = 0; k < st.drag_count; k++) {
          const int32_t i = st.drag_items[k];
          if (i == gz || i < 0 || i >= (int32_t)st.scene.entity_count) continue;
          st.scene.entities[i].pos = st.drag_before[k].pos + delta;
        }
      }
      if (!using_now && st.gizmo_was_using) { // surukleme sonu: gunluge TEK grup
        for (uint32_t k = 0; k < st.drag_count; k++) st.drag_after[k] = st.scene.entities[st.drag_items[k]];
        const uint32_t ops = selection_commit(st.scene, st.hist, st.drag_items, st.drag_count, st.drag_before, st.drag_after);
        if (ops) {
          st.groups.push(ops);
          st.dirty = true;
          if (ops > 1) set_status(st, "tasindi (%u varlik)", ops);
        }
        st.drag_count = 0;
      }
      st.gizmo_was_using = using_now;
    } else st.gizmo_was_using = false;
    // Tiklamayla secim: sol tus basildi (gecis), ImGui/gizmo uzerinde degil.
    {
      const bool lmb = in && in->mouse_down[0];
      const bool pressed = lmb && !st.prev_lmb;
      st.prev_lmb = lmb;
      // Fare konumu GORUNUM PANELININ dikdortgenine cevrilir. Panel disindaki
      // tik bir viewport tiklamasi DEGILDIR ve hicbir sey secmez — `map_mouse`
      // orada valid=false donuyor (ve -1 sentinel veriyor, 0 degil: 0 gecerli
      // bir piksel olurdu ve sessizce kosede bir isin atardik).
      const ViewportPick pick = in ? vp.map_mouse(view_rect, (float)in->mouse_x, (float)in->mouse_y) : ViewportPick{};
      if (pressed && pick.valid && view_hovered && !ImGuizmo::IsUsing() && !ImGuizmo::IsOver()) {
        static content::SceneBounds wb[content::kSceneMaxEntities];
        const uint32_t nb = entity_world_bounds(st, phys, wb);
        Vec3 o, d;
        camera_ray(cam, kPi / 3.5f, aspect, pick.x, pick.y, (float)vp.width(), (float)vp.height(), &o, &d);
        float t = 0;
        const int32_t hit = content::scene_pick(wb, nb, o, d, &t);
        const bool ctrl = ImGui::GetIO().KeyCtrl;
        if (hit >= 0) {
          if (ctrl) st.sel.toggle(hit);
          else st.sel.set_single(hit);
          set_status(st, "secildi: %s (%.1f m, %u secili)", st.scene.entities[hit].name, t, st.sel.count);
        } else if (!ctrl) {
          st.sel.clear();
          set_status(st, "secim yok");
        }
      }
    }
    if (headless && frame_i == 0 && st.scene.entity_count) {
      // Betikli secim kapisi: ilk varligin merkezi ekrana izdusurulur, o pikselden
      // atilan isin ayni varligi secmeli (kamera isini + sinirlar + secim uctan uca).
      static content::SceneBounds wb[content::kSceneMaxEntities];
      const uint32_t nb = entity_world_bounds(st, phys, wb);
      const Vec3 p0 = st.scene.entities[0].pos;
      const Vec4 clip = proj * (view * Vec4{p0.x, p0.y, p0.z, 1.0f});
      const float px = (clip.x / clip.w * 0.5f + 0.5f) * (float)fw, py = (clip.y / clip.w * 0.5f + 0.5f) * (float)fh; // Vulkan: NDC y asagi
      Vec3 o, d;
      camera_ray(cam, kPi / 3.5f, aspect, px, py, (float)fw, (float)fh, &o, &d);
      float t = 0;
      const int32_t hit = content::scene_pick(wb, nb, o, d, &t);
      const bool ok = hit == 0;
      std::printf("[engine_editor] secim kapisi: %s piksel (%.0f, %.0f) -> %s (t=%.2f) %s\n", st.scene.entities[0].name, px, py,
                  hit >= 0 ? st.scene.entities[hit].name : "-", hit >= 0 ? t : 0.0f, ok ? "OK" : "HATA");
      if (!ok) return 1;
      // Derleme kapisi: veri modeli -> blob -> ac; sayilar veri modeliyle tutarli olmali.
      const size_t need = content::scene_blob_compile(st.scene, nullptr, 0);
      void *blob = frame.alloc(need, content::kSceneBlobAlign);
      content::SceneBlobView bv;
      content::SceneError berr{};
      const bool bok = blob && content::scene_blob_compile(st.scene, blob, need) == need && content::scene_blob_open(blob, need, &bv, &berr) &&
                       bv.h->entity_count == st.scene.entity_count && bv.h->asset_count == st.scene.asset_count;
      std::printf("[engine_editor] derleme kapisi: %zu bayt, %u varlik, %u cizim, %u isik, %u govde, ozet %016llx %s%s\n", need,
                  bok ? bv.h->entity_count : 0u, bok ? bv.h->draw_count : 0u, bok ? bv.h->light_count : 0u, bok ? bv.h->body_count : 0u,
                  bok ? (unsigned long long)bv.hash() : 0ull, bok ? "OK" : "HATA ", bok ? "" : berr.msg);
      if (!bok) return 1;
      // Coklu secim kapisi: iki varlik secili, grup tasima gunluge TEK eylem
      // (2 islem); metin degismeli, grup geri al ikisini BIRDEN geri almali.
      // Karsilastirma bayt bayt (scene_write) — konum kontrolu tek basina yeterli degil.
      static char txt_a[65536], txt_b[65536], txt_c[65536];
      content::scene_write(st.scene, txt_a, sizeof txt_a);
      st.sel.set_single(0);
      st.sel.toggle(1);
      const Vec3 gd{1.5f, 0.25f, -0.75f};
      const Vec3 p0b = st.scene.entities[0].pos, p1b = st.scene.entities[1].pos;
      const uint32_t gops = selection_translate(st.scene, st.hist, st.sel.items, st.sel.count, gd);
      if (gops) st.groups.push(gops);
      const bool moved = st.scene.entities[0].pos == p0b + gd && st.scene.entities[1].pos == p1b + gd;
      content::scene_write(st.scene, txt_b, sizeof txt_b);
      const bool txt_changed = std::strcmp(txt_a, txt_b) != 0;
      do_undo(); // tek geri al = tum grup
      content::scene_write(st.scene, txt_c, sizeof txt_c);
      const bool back = std::strcmp(txt_a, txt_c) == 0;
      const bool gok = gops == 2 && moved && txt_changed && back && st.hist.undo_count() == 0;
      std::printf("[engine_editor] coklu secim kapisi: %u secili, grup tasima %u islem, ikisi de delta kadar %s, metin degisti %s, grup geri al -> baslangic baytlari %s %s\n",
                  2u, gops, moved ? "evet" : "HAYIR", txt_changed ? "evet" : "HAYIR", back ? "evet" : "HAYIR", gok ? "OK" : "HATA");
      if (!gok) return 1;
      // Kaynak tarayici kapisi: dizindeki glTF'ler bulunmali, secilen kaynakla
      // eklenen varlik kSceneModel almali; geri al sahneyi bayt bayt geri getirmeli.
      st.browse_count = editor_scan_assets(st.scene_dir, st.scene, st.browse, 64);
      if (st.browse_count == 0) {
        std::printf("[engine_editor] kaynak tarayici kapisi: ATLANDI (dizinde .gltf/.glb yok: %s)\n", st.scene_dir);
      } else {
        const uint32_t n_before = st.scene.entity_count;
        int32_t a = -1;
        const uint32_t aops = editor_add_asset_entity(st.scene, st.hist, st.browse[0].name, Vec3{0, 0, 0}, &a);
        if (aops) st.groups.push(aops);
        const SceneEntity &ne = st.scene.entities[st.scene.entity_count ? st.scene.entity_count - 1 : 0];
        const bool added = aops == 1 && st.scene.entity_count == n_before + 1 && (ne.components & content::kSceneModel) != 0 && ne.asset == a && a >= 0;
        std::printf("[engine_editor] kaynak tarayici kapisi: %u dosya (ilk \"%s\"), varlik \"%s\" kaynak %d, model bileseni %s", st.browse_count,
                    st.browse[0].name, added ? ne.name : "-", a, added ? "var" : "YOK");
        do_undo();
        content::scene_write(st.scene, txt_c, sizeof txt_c);
        const bool aok = added && std::strcmp(txt_a, txt_c) == 0 && st.scene.entity_count == n_before;
        std::printf(", geri al -> baslangic baytlari %s %s\n", std::strcmp(txt_a, txt_c) == 0 ? "evet" : "HAYIR", aok ? "OK" : "HATA");
        if (!aok) return 1;
      }
      st.sel.set_single(0);
      st.dirty = false; // kapilar sahneyi geri aldi: dosya kirlenmedi
      set_status(st, "yuklendi: %s", st.scene_path);
    }
    ui.end_frame();

    // --- 3B cizim: veri modelinden (dunya isigi/golgesi de her kare modelden: panel canli) ---
    ren.set_light(normalize(st.scene.sun_dir), st.scene.ambient, st.scene.sun_diffuse);
    ren.set_shadow_volume(st.scene.shadow_center, st.scene.shadow_radius, st.scene.shadow_depth);
    ren.set_shadow_focus(cam.target); // yakin kademeler kameranin baktigi yerde
    ren.begin_frame(headless ? 0 : frame_i);
    scene.draw(ren, ds);
    for (uint32_t i = 0; i < st.scene.entity_count; i++) {
      const SceneEntity &e = st.scene.entities[i];
      const bool simulated = st.playing && st.bodies_live && st.bodies[i].valid() && e.dynamic;
      const Mat4 m = simulated ? content::scene_body_matrix(e, phys, st.bodies[i]) : content::scene_entity_matrix(e);
      const bool sel = st.sel.contains((int32_t)i);
      const Vec3 tint = sel ? Vec3{1.0f, 0.9f, 0.4f} : e.tint;
      bool drew = false;
      if ((e.components & content::kSceneModel) && e.asset >= 0 && e.asset < (int32_t)st.scene.asset_count && st.have[e.asset]) {
        const content::Model &mdl = st.models[e.asset];
        const content::UploadedModel &up = st.ups[e.asset];
        content::ModelLod lod;
        lod.camera_pos = cam.eye();
        lod.distance1 = 24.0f; lod.distance2 = 34.0f;
        if ((e.components & content::kSceneAnim) && e.clip < mdl.clip_count) {
          const float dur = mdl.clips[e.clip].duration;
          float t = std::fmod((st.playing ? st.play_time * e.speed : 0.0f) + e.phase, 2.0f * dur);
          if (t > dur) t = 2.0f * dur - t;
          content::ModelPose pose;
          if (content::model_pose_evaluate(mdl, e.clip, t, st.pose_scratch, &pose)) {
            content::draw_model(ren, mdl, up, m, tint, nullptr, nullptr, &pose);
            drew = true;
          }
        }
        if (!drew) { content::draw_model(ren, mdl, up, m, tint, &lod); drew = true; }
      }
      if (!drew && (e.components & content::kSceneBody)) {
        // Modelsiz govde: carpisan hacmi kutu olarak goster (kure de kutu, yaricap kadar).
        const Vec3 s = e.shape == content::SceneShape::Box ? e.half * 2.0f : Vec3{e.radius * 2, e.radius * 2, e.radius * 2};
        ren.draw(ds.cube, m * Mat4::scale(s), sel ? tint : Vec3{0.55f, 0.6f, 0.7f});
        drew = true;
      }
      if (!drew) ren.draw(ds.cube, m * Mat4::scale({0.3f, 0.3f, 0.3f}), sel ? tint : Vec3{0.9f, 0.9f, 0.3f}); // bos/isik isareti
      if (e.components & content::kSceneLight) {
        renderer::PointLight pl;
        pl.pos = {m.m[3][0], m.m[3][1], m.m[3][2]};
        pl.radius = e.light_radius;
        pl.color = e.light_color;
        pl.intensity = e.light_intensity;
        ren.add_point_light(pl);
      }
    }
    // Isik yaricapi / golge hacmi / gunes yonu: motorun kendi draw'u ile ince kutular.
    st.gizmo_draws = editor_draw_gizmos(ren, ds.cube, st.scene, st.sel.items, st.sel.count, st.gizmos);
    if (headless) {
      if (!rhi::offscreen_render_custom(off, oc, record_cb, &rctx, &ores, before_cb)) { std::fprintf(stderr, "kare: %s\n", ores.error); return 1; }
    } else {
      rhi::FrameContext fc;
      if (swap.acquire(&fc)) {
        // IKI YOL DA AYNI IKI FONKSIYONU CAGIRIR — ayrisamasinlar diye.
        // Burada bir kez `ui.record()` DOGRUDAN cagrildi ve pencereli editor
        // SIMSIYAH acildi: ana gecis (swapchain de offscreen de) IKI subpass
        // tanimliyor, ImGui'nin boru hatti subpass 1 icin kurulu ve ilerletmeyi
        // `record_cb` yapiyor. Headless yol record_cb'den gectigi icin calisti,
        // pencereli yol gecmedigi icin hicbir sey cizmedi. Ayni hata, tek yolda
        // duzeltilmis hali. Artik tek kaynak var.
        before_cb(fc.cmd, &rctx);   // golge + viewport (ikisi de KENDI gecisi)
        swap.begin_render_pass(fc);
        record_cb(fc.cmd, &rctx);   // subpass ilerlet + ImGui
        swap.end_frame(fc);
      }
      // Swapchain yeniden yaratimi artik renderer'in cizim olcusunu DEGISTIRMEZ:
      // renderer viewport'a ciziyor, onun olcusu panelden geliyor.
      if (swap.needs_recreate() && fw && fh) swap.recreate(fw, fh);
    }
    prof.end_frame();
    frame_i++;
    if (headless && frame_i >= opts.headless_frames) running = false;
  }
  static uint64_t scratch[1200]; // 2x kare kapasitesi (profiler sozlesmesi); 600 iken 300+ karede assert
  FrameStats stt = prof.frame_stats(Span<uint64_t>(scratch, 1200), 0);
  const EditorUiStats us = ui.stats();
  const int32_t prim_end = st.sel.primary();
  std::printf("[engine_editor] %u kare | p50 %.2f ms p99 %.2f ms | ui %u vertex %u indeks %u liste | cizim %u (gizmo %u) | nesne %u | kaynak %u/%u dosya | gunluk %u/%u%s | secili %u (%s) | tick %u\n",
              frame_i, stt.p50_ns / 1e6, stt.p99_ns / 1e6, us.vertices, us.indices, us.draw_lists, ren.stats().draws, st.gizmo_draws,
              st.scene.entity_count, st.scene.asset_count, st.browse_count, st.hist.undo_count(), st.hist.redo_count(),
              st.dirty ? " (kaydedilmedi)" : "", st.sel.count, prim_end >= 0 ? st.scene.entities[prim_end].name : "-", tick_i);
  if (headless && opts.out_path) {
    if (rhi::write_ppm(opts.out_path, ores.pixels, oc.width, oc.height)) std::printf("[engine_editor] goruntu: %s\n", opts.out_path);
  }
  dev.api().vkDeviceWaitIdle(dev.handle());
  // IS SISTEMI ONCE SUSTURULUR — alt sistemlerden ONCE.
  //
  // Eskiden en SONDA kapaniyordu: fizik, renderer ve cihaz yikilirken worker
  // thread'leri HALA CALISIYORDU. Jolt'un is uyarlayicisi (FiberJoltJobs)
  // bizim kuyruga CIPLAK Job* itiyor; `delete impl_->jobs` o havuzu yok
  // ediyor. Bir worker o sirada elinde eski bir girdi tutuyorsa cop bir
  // isaretciyi cagiriyor.
  //
  // Olculdu (CI macOS/arm64, 2026-09-16): `thread: tulpar-job`, SIGSEGV,
  // fault_addr 0x8bc94512aa864210 (null degil — COP). Yigin izi iki cerceve,
  // cunku fiber yigini cozucuyu kesiyor. Dort kosumun ikisinde dustu: yaris.
  //
  // `jobs.shutdown()` worker'lari JOIN eder ve hicbir fiber'in park halinde
  // kalmadigini ENGINE_ASSERT ile dogrular. Ondan sonrasi tek thread'lidir,
  // yani bu sinif tamamen kapanir. Kapanis yolunda is URETEN kimse yok
  // (yikim yalniz Vulkan/arena nesnesi serbest birakiyor).
  jobs.shutdown();
  bodies_remove(st, phys);
  // VIEWPORT ImGui'DEN ONCE KAPANIR: doku descriptor'i ImGui'nin havuzundan
  // geliyor, once ImGui kapanirsa o set'i iade edecek yer kalmaz. Eklenmedigi
  // ilk halde dogrulama katmani kapanista VUID-vkDestroyDevice-device-05137
  // veriyordu (cihaz yok edilirken cocuk nesneler duruyor).
  vp.shutdown();
  ui.shutdown();
  scene.shutdown();
  ren.shutdown();
  if (off) rhi::offscreen_destroy(off);
  if (!headless) swap.shutdown();
  dev.shutdown();
  return 0;
}

} // namespace tulpar::engine::app
