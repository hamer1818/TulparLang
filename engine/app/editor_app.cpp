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
#include "app/editor_camera.hpp"
#include "app/editor_chrome.hpp"
#include "app/editor_console.hpp"
#include "app/editor_commands.hpp"
#include "app/editor_files.hpp"
#include "app/editor_layout.hpp"
#include "app/editor_overlay.hpp"
#include "app/editor_viewport.hpp"
#include "app/editor_widgets.hpp"
#include "rhi/offscreen.hpp"
#include "rhi/swapchain.hpp"
#include "sim/schedule.hpp"

namespace tulpar::engine::app {

namespace {
constexpr float kPi = 3.14159265358979f;
using content::SceneDesc;
using content::SceneEntity;

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
// Renderer'in CALISMA ZAMANINDA degistirilebilen ayarlari. Sahne dosyasina
// YAZILMAZ: SceneWorld'e eklemek surum artirimi + scene_blob degisikligi
// ister; bu tur oturum ayari olarak duruyor ve her karede uygulaniyor.
//
// Varsayilanlar RendererConfig'ten TUREIR -- ayni sayi iki yerde tutulmaz,
// motorun varsayilani degisirse editor kendiliginden uyar.
const renderer::RendererConfig kRenderDefaults{};

struct RenderSettings {
  bool shadows = true;
  float shadow_bias = kRenderDefaults.shadow_bias;
  float shadow_normal_offset = kRenderDefaults.shadow_normal_offset;
  float exposure = kRenderDefaults.exposure;
  float bloom_threshold = kRenderDefaults.bloom_threshold;
  float bloom_intensity = kRenderDefaults.bloom_intensity;
  float bloom_knee = kRenderDefaults.bloom_soft_knee;
  float bloom_radius = kRenderDefaults.bloom_radius;
  float render_scale = kRenderDefaults.temporal.render_scale;
  int upscaler = (int)kRenderDefaults.temporal.upscaler;
  float sharpness = kRenderDefaults.temporal.sharpness;
  bool jitter = kRenderDefaults.temporal.jitter;
};

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
  RenderSettings render;    // golge/pozlama/bloom/olceklendirme (oturum ayari)
  GizmoOptions gizmos;      // isik yaricapi / golge hacmi / gunes yonu
  uint32_t gizmo_draws = 0; // son karede gizmolarin yaptigi cizim sayisi
  AssetFile browse[64];     // kaynak tarayici (sahne dosyasinin dizini)
  uint32_t browse_count = 0;
  char status[160];
  char filter[64] = {0}; // Sahne paneli suzgeci
  HierarchyState tree;   // Sahne agaci: katlama bitleri + yerinde ad + surukleme
  AssetsView assets_view; // Kaynaklar paneli gorunumu (izgara/liste, karo, suzgec)
  // Pano: editorun KENDI tamponu (isletim sistemi panosu degil — metin degil
  // yapi tasiyoruz). Kes/kopyala secimi buraya yazar, yapistir buradan ekler.
  SceneEntity clip[Selection::kMax];
  uint32_t clip_count = 0;
  // Duraklatma DURDURMAK DEGILDIR: playing true kalir, govdeler yerinde durur,
  // yalniz zaman akmaz. step_request duraklatilmisken tek adim ilerletir.
  bool paused = false;
  uint32_t step_request = 0;
};

// Varlik silindikten / geri alindiktan sonra secimi gecerli tut.
void clamp_selection(EditorState &st) {
  for (uint32_t k = st.sel.count; k > 0; k--)
    if (st.sel.items[k - 1] >= (int32_t)st.scene.entity_count) st.sel.erase(st.sel.items[k - 1]);
}

// Fare pikselinden dunya isini (kamera tabanindan; matris tersi gerekmez).
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
    const Mat4 m = simulated ? content::scene_body_matrix(e, phys, st.bodies[i]) : content::scene_entity_world_matrix(st.scene, i);
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
  // Durum cubugu yalniz SON iletiyi tutar: art arda iki hatada ilki okunmadan
  // siliniyordu. Ayni satir konsola da dusuyor (gecmis orada kaliyor); duzey
  // metinden turetiliyor ("KAYDEDILEMEDI" -> Hata), siniflandirici konsolunkiyle
  // AYNI — iki yerde iki kural olmasin.
  console_log_raw(console_classify_level(st.status, false), kConsoleTagEditor, st.status);
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

// Ozellik paneli: PropItem uzerinden — ImGui "son oge"sine BAKMAZ. Bilesik bir
// widget'ta (vec3 = uc surukleme) son oge yalniz Z alanidir; Y suruklenirken
// IsItemActivated() false kalir ve gunluge islem dusmezdi (editor_widgets kapisi).
void track_edit(EditorState &st, SceneEntity &e, int index, const PropItem &it) {
  if (it.activated && !st.edit_active) { st.edit_before = e; st.edit_active = true; }
  if (it.deactivated && st.edit_active) {
    st.edit_active = false;
    if (it.deactivated_after_edit) {
      const SceneEntity after = e;
      e = st.edit_before;
      if (st.hist.set_entity(st.scene, (uint32_t)index, after)) { st.groups.push(1); st.dirty = true; }
    }
  }
}
// Dunya paneli (gunes/ortam/golge): ayni kural, SceneOp::World olarak gunluge.
void track_world_edit(EditorState &st, const PropItem &it) {
  if (it.activated && !st.world_edit_active) { st.world_before = st.scene.world(); st.world_edit_active = true; }
  if (it.deactivated && st.world_edit_active) {
    st.world_edit_active = false;
    if (it.deactivated_after_edit) {
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
    if (!content::scene_load(sys, st.scene_path, &st.scene, &err)) {
      console_log(ConsoleLevel::Hata, kConsoleTagScene, "sahne %s: %s", st.scene_path, err.msg);
      std::fprintf(stderr, "sahne %s: %s\n", st.scene_path, err.msg);
      return 1;
    }
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
    if (!st.have[i]) {
      console_log(ConsoleLevel::Uyari, kConsoleTagScene, "kaynak yuklenemedi: %s", st.scene.assets[i]);
      std::printf("[engine_editor] kaynak yuklenemedi: %s\n", st.scene.assets[i]);
    }
    return st.have[i];
  };
  for (uint32_t i = 0; i < st.scene.asset_count; i++) load_asset((int32_t)i);
  st.browse_count = editor_scan_assets(st.scene_dir, st.scene, st.browse, 64);
  std::printf("[engine_editor] sahne %s: %u varlik, %u kaynak\n", st.scene_path, st.scene.entity_count, st.scene.asset_count);
  console_log(ConsoleLevel::Bilgi, kConsoleTagEditor, "sahne %s: %u varlik, %u kaynak", st.scene_path, st.scene.entity_count,
              st.scene.asset_count);

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

  EditorCamera cam;
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
  bool snap_on = false;   // arac cubugundaki "Yakala": ImGuizmo adim kilidi
  float snap_step = 0.5f; // dunya birimi / derece / olcek adimi
  sim::FixedStep fs;
  uint64_t last_ns = platform::now_ns();
  uint32_t frame_i = 0, tick_i = 0;
  double prev_mx = 0, prev_my = 0, prev_scroll = 0;
  GizmoSpace gizmo_space = GizmoSpace::World; // ImGuizmo: dunya / yerel eksen
  // Kamera girdisi ONCEKI karenin panel durumunu okur (panel kare icinde daha
  // sonra ciziliyor): bir kare gecikme gorunmez, yanlis kosul gorunur olurdu.
  ViewportRect view_rect{};
  bool view_hovered = false;
  OverlayResult ovres;       // kaplamanin son karede urettigi etkilesim
  bool cam_dragging = false; // kamera tusu basili: panelden cikmak donusu kesmesin
  bool prev_f = false;
  RecordCtx rctx{&ren, &ui, &vp, &dev};
  bool running = true;
  // Konsol + dosya islemleri durumu. static: ic tamponlari buyuk (halka ~150 KB,
  // diyalogun dizin listesi 512 girdi) ve surec basina TEK editor kosuyor.
  static ConsoleView console_view;
  static ConsoleCapture console_cap;
  static FileDialog dlg;
  static ConfirmState confirm;
  bool show_console = true;
  enum PendingAction { PendingNone = 0, PendingNew = 1, PendingOpen = 2, PendingOpenPath = 3 };
  int pending = PendingNone;
  char pending_path[1024] = {0}; // "Son dosyalar"dan secilen yol
  char recent_file[1024];
  {
    const char *home = std::getenv("HOME");
    std::snprintf(recent_file, sizeof recent_file, "%s/.tulpar_son_sahneler", (home && *home) ? home : ENGINE_SOURCE_DIR);
    recent_load(recent_file); // dosya yoksa false doner, liste bos kalir — HATA DEGIL
    recent_push(st.scene_path);
  }
  auto set_playing = [&](bool p) {
    if (p == st.playing) return;
    st.playing = p;
    st.paused = false; // durdur/baslat duraklatmayi da sifirlar
    st.step_request = 0;
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
  auto do_save_as = [&]() {
    file_dialog_open(dlg, FileDialogMode::Kaydet, st.scene_path[0] ? st.scene_path : st.scene_dir, ".sahne", "Farkl\xC4\xB1 kaydet");
  };
  // Sahneyi verilen yola yazar ve editorun ACIK DOSYASINI oraya tasir (kaynak
  // tarayicisi da yeni dizini gosterir — sahne dosyasi dizinini takip eder).
  auto save_scene_to = [&](const char *path) {
    st.scene.cam_target = cam.target; st.scene.cam_yaw = cam.yaw; st.scene.cam_pitch = cam.pitch; st.scene.cam_radius = cam.radius;
    content::SceneError err{};
    if (!content::scene_save(frame, st.scene, path, &err)) { set_status(st, "KAYDEDILEMEDI: %s", err.msg); return false; }
    std::snprintf(st.scene_path, sizeof st.scene_path, "%s", path);
    content::scene_dir_of(st.scene_path, st.scene_dir, sizeof st.scene_dir);
    st.browse_count = editor_scan_assets(st.scene_dir, st.scene, st.browse, 64);
    st.dirty = false;
    recent_push(st.scene_path);
    recent_save(recent_file);
    set_status(st, "kaydedildi: %s", st.scene_path);
    return true;
  };
  auto do_save = [&]() {
    if (st.scene_path[0] == 0) { do_save_as(); return; } // adsiz sahne: once yer sor
    save_scene_to(st.scene_path);
  };
  // Dosyadan yukleme: veri modeli + turetilmis her sey (kaynaklar, tarayici,
  // kamera) yenilenir ve GUNLUK SIFIRLANIR — eski sahnenin geri al kayitlari
  // yeni sahneye uygulanamaz (indeksler baska bir sahneye ait).
  auto load_scene_from = [&](const char *path) {
    content::SceneDesc nd;
    content::SceneError err{};
    if (!content::scene_load(sys, path, &nd, &err)) { set_status(st, "ACILAMADI: %s (%s)", path, err.msg); return false; }
    with_bodies(st, phys, [&] {
      st.scene = nd;
      st.hist.clear();
      st.groups.clear();
      st.sel.clear();
      st.dirty = false;
    });
    std::snprintf(st.scene_path, sizeof st.scene_path, "%s", path);
    content::scene_dir_of(st.scene_path, st.scene_dir, sizeof st.scene_dir);
    for (uint32_t i = 0; i < content::kSceneMaxAssets; i++) st.have[i] = false;
    for (uint32_t i = 0; i < st.scene.asset_count; i++) load_asset((int32_t)i);
    st.browse_count = editor_scan_assets(st.scene_dir, st.scene, st.browse, 64);
    cam.target = st.scene.cam_target; cam.yaw = st.scene.cam_yaw; cam.pitch = st.scene.cam_pitch; cam.radius = st.scene.cam_radius;
    st.clip_count = 0; // pano baska bir sahnenin varliklarini tasiyordu
    recent_push(st.scene_path);
    recent_save(recent_file);
    set_status(st, "acildi: %s (%u varlik)", st.scene_path, st.scene.entity_count);
    return true;
  };
  auto do_new = [&]() {
    content::SceneDesc fresh; // varsayilan dunya + 0 varlik
    with_bodies(st, phys, [&] {
      st.scene = fresh;
      st.hist.clear();
      st.groups.clear();
      st.sel.clear();
      st.dirty = false;
    });
    st.scene_path[0] = 0; // ADSIZ: ilk Kaydet "Farkli kaydet"e duser
    for (uint32_t i = 0; i < content::kSceneMaxAssets; i++) st.have[i] = false;
    st.clip_count = 0;
    st.browse_count = editor_scan_assets(st.scene_dir, st.scene, st.browse, 64);
    set_status(st, "yeni sahne (henuz kaydedilmedi)");
  };
  auto run_pending = [&](int a) {
    if (a == PendingNew) do_new();
    else if (a == PendingOpen) file_dialog_open(dlg, FileDialogMode::Ac, st.scene_dir, ".sahne", "Sahne a\xC3\xA7");
    else if (a == PendingOpenPath && pending_path[0]) load_scene_from(pending_path);
  };
  // KIRLI SAHNE KORUMASI: kaydedilmemis is varken Yeni/Ac ONCE sorar. Onay kipli
  // oldugu icin eylem ERTELENIR (pending) ve cevap gelince calisir.
  auto guard_then = [&](int action, const char *path = nullptr) {
    std::snprintf(pending_path, sizeof pending_path, "%s", path ? path : "");
    if (!st.dirty) { run_pending(action); return; }
    pending = action;
    confirm.open = true;
  };
  auto do_new_guarded = [&]() { guard_then(PendingNew); };
  auto do_open_guarded = [&]() { guard_then(PendingOpen); };
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
  // kind: 0 = kopya/varsayilan (secili varsa kopyasi, yoksa bos ya da model),
  // 1 bos, 2 model, 3 isik, 4 govde (Sahne panelinin "+" menusu).
  auto do_add = [&](int kind = 0) {
    SceneEntity e{};
    const int32_t prim = st.sel.primary();
    if (kind == 0 && prim >= 0) { e = st.scene.entities[prim]; std::snprintf(e.name, sizeof e.name, "%.24s_kopya", st.scene.entities[prim].name); e.pos.x += 1.5f; }
    else {
      static const char *const kStem[5] = {"nesne", "nesne", "model", "isik", "govde"};
      std::snprintf(e.name, sizeof e.name, "%s_%u", kStem[kind < 0 || kind > 4 ? 0 : kind], st.scene.entity_count + 1);
      e.pos = cam.target;
      if (kind == 2 || (kind == 0 && st.scene.asset_count)) { e.components = content::kSceneModel; e.asset = st.scene.asset_count ? 0 : -1; }
      if (kind == 3) e.components = content::kSceneLight;
      if (kind == 4) e.components = content::kSceneBody;
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
    for (uint32_t k = 0; k < n; k++) st.tree.collapse.after_remove((uint32_t)idx[k]); // buyukten kucuge silindi
    if (ops) {
      st.groups.push(ops);
      st.dirty = true;
      set_status(st, "silindi (%u varlik)", ops);
    }
    st.sel.clear();
  };
  // Pano: kopyalama secimi ARTAN indeks sirasinda alir (belirlenimli — secim
  // kumesinin kendi sirasi tiklama sirasidir, yapistirma sirasi ona bagli olmasin).
  auto do_copy = [&]() {
    if (st.sel.count == 0) return;
    int32_t idx[Selection::kMax];
    const uint32_t n = st.sel.sorted_desc(idx); // buyukten kucuge; tersten okuyacagiz
    st.clip_count = 0;
    for (uint32_t i = n; i > 0; i--) st.clip[st.clip_count++] = st.scene.entities[idx[i - 1]];
    set_status(st, "panoya alindi (%u varlik)", st.clip_count);
  };
  auto do_cut = [&]() {
    if (st.sel.count == 0) return;
    do_copy();
    do_remove();
    set_status(st, "kesildi (%u varlik)", st.clip_count);
  };
  // Yapistir: her varlik yeni bir varliktir (kopya adi + kucuk otelemeyle
  // ustuste binmesin). Gunluge N islem ama TEK eylem: geri al hepsini alir.
  auto do_paste = [&]() {
    if (st.clip_count == 0) return;
    uint32_t ops = 0;
    const uint32_t first = st.scene.entity_count;
    with_bodies(st, phys, [&] {
      for (uint32_t i = 0; i < st.clip_count; i++) {
        SceneEntity e = st.clip[i];
        e.pos.x += 1.0f;
        if (st.hist.add_entity(st.scene, e)) ops++;
        else break; // kapasite doldu: sessizce kirpma yok, asagida bildirilir
      }
    });
    if (!ops) { set_status(st, "yapistirilamadi (kapasite %u)", content::kSceneMaxEntities); return; }
    st.groups.push(ops);
    st.dirty = true;
    st.sel.clear();
    for (uint32_t i = 0; i < ops; i++) st.sel.toggle((int32_t)(first + i));
    if (ops < st.clip_count) set_status(st, "yapistirildi (%u/%u — kapasite %u doldu)", ops, st.clip_count, content::kSceneMaxEntities);
    else set_status(st, "yapistirildi (%u varlik)", ops);
  };
  // Sahne panelinin dondurdugu NIYETI uygular. Panel sahneyi DEGISTIRMEZ; gunluk,
  // secim ve govde yeniden kurulumu tek yerde — burada.
  auto apply_hierarchy = [&](const HierarchyResult &r) {
    const int32_t i = r.index;
    const bool valid = i >= 0 && i < (int32_t)st.scene.entity_count;
    if (!valid && r.action != HierarchyAction::None) return;
    switch (r.action) {
    case HierarchyAction::None: break;
    case HierarchyAction::Select:
      if (r.ctrl) st.sel.toggle(i);
      else st.sel.set_single(i);
      break;
    case HierarchyAction::Toggle: st.tree.collapse.toggle((uint32_t)i); break;
    case HierarchyAction::Rename: {
      SceneEntity after = st.scene.entities[i];
      std::snprintf(after.name, sizeof after.name, "%s", r.name);
      if (st.hist.set_entity(st.scene, (uint32_t)i, after)) {
        st.groups.push(1);
        st.dirty = true;
        set_status(st, "adlandirildi: %s", after.name);
      }
      break;
    }
    case HierarchyAction::Delete:
      st.sel.set_single(i);
      do_remove();
      st.tree.collapse.after_remove((uint32_t)i);
      break;
    case HierarchyAction::Duplicate:
      st.sel.set_single(i);
      do_add(0);
      break;
    case HierarchyAction::Detach:
    case HierarchyAction::Reparent: {
      const int32_t par = (r.action == HierarchyAction::Detach) ? -1 : r.target;
      bool ok = false;
      with_bodies(st, phys, [&] { ok = st.hist.reparent(st.scene, (uint32_t)i, par); });
      if (ok) {
        st.groups.push(1);
        st.dirty = true;
        if (par < 0) set_status(st, "ebeveynden ayrildi: %s", st.scene.entities[i].name);
        else set_status(st, "ebeveyn: %s", st.scene.entities[par].name);
      } else if (par >= 0) set_status(st, "ebeveynlenemedi (dongu ya da derinlik tavani %u)", content::kSceneMaxDepth);
      break;
    }
    case HierarchyAction::Visibility:
    case HierarchyAction::Lock: {
      SceneEntity after = st.scene.entities[i];
      after.flags ^= (r.action == HierarchyAction::Visibility) ? content::kSceneHidden : content::kSceneLocked;
      if (st.hist.set_entity(st.scene, (uint32_t)i, after)) {
        st.groups.push(1);
        st.dirty = true;
      }
      break;
    }
    }
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
  // --- Komut tablosu: menu, arac cubugu ve kisayollar TEK kaynaktan ----------
  // (editor_commands.hpp'nin varlik sebebi: ayni komut iki yerde yazilmasin,
  // cakisma sessiz kalmasin, koruma kurali tek olsun). Geri cagrilar yukaridaki
  // yakalayan lambda'lar; CommandFn duz isaretci oldugu icin arada bir baglam
  // yapisi var — lambda'lar adresle tutulur, kopyalanmaz.
  struct CmdCtx {
    EditorState *st;
    int *gizmo_op;
    decltype(&do_save) save;
    decltype(&do_compile) compile;
    decltype(&do_undo) undo;
    decltype(&do_redo) redo;
    decltype(&do_add) add;
    decltype(&do_remove) remove;
    decltype(&set_playing) play;
    decltype(&do_cut) cut;
    decltype(&do_copy) copy;
    decltype(&do_paste) paste;
    decltype(&do_new_guarded) newscene;
    decltype(&do_open_guarded) open;
    decltype(&do_save_as) saveas;
    bool *show_console;
  } cc{&st,      &gizmo_op, &do_save, &do_compile,      &do_undo,         &do_redo,     &do_add,     &do_remove,
       &set_playing, &do_cut,   &do_copy, &do_paste,    &do_new_guarded,  &do_open_guarded, &do_save_as, &show_console};
  CommandTable cmds;
  cmds.bind(CommandId::FileNew, [](void *c) { (*static_cast<CmdCtx *>(c)->newscene)(); }, &cc);
  cmds.bind(CommandId::FileOpen, [](void *c) { (*static_cast<CmdCtx *>(c)->open)(); }, &cc);
  cmds.bind(CommandId::FileSave, [](void *c) { (*static_cast<CmdCtx *>(c)->save)(); }, &cc);
  cmds.bind(CommandId::FileSaveAs, [](void *c) { (*static_cast<CmdCtx *>(c)->saveas)(); }, &cc);
  cmds.bind(CommandId::FileCompile, [](void *c) { (*static_cast<CmdCtx *>(c)->compile)(); }, &cc);
  cmds.bind(CommandId::EditUndo, [](void *c) { (*static_cast<CmdCtx *>(c)->undo)(); }, &cc,
            [](const void *c) { return static_cast<const CmdCtx *>(c)->st->hist.undo_count() > 0; });
  cmds.bind(CommandId::EditRedo, [](void *c) { (*static_cast<CmdCtx *>(c)->redo)(); }, &cc,
            [](const void *c) { return static_cast<const CmdCtx *>(c)->st->hist.redo_count() > 0; });
  cmds.bind(CommandId::EditDuplicate, [](void *c) { (*static_cast<CmdCtx *>(c)->add)(); }, &cc);
  cmds.bind(CommandId::EditDelete, [](void *c) { (*static_cast<CmdCtx *>(c)->remove)(); }, &cc,
            [](const void *c) { return static_cast<const CmdCtx *>(c)->st->sel.count > 0; });
  cmds.bind(CommandId::EditCut, [](void *c) { (*static_cast<CmdCtx *>(c)->cut)(); }, &cc,
            [](const void *c) { return static_cast<const CmdCtx *>(c)->st->sel.count > 0; });
  cmds.bind(CommandId::EditCopy, [](void *c) { (*static_cast<CmdCtx *>(c)->copy)(); }, &cc,
            [](const void *c) { return static_cast<const CmdCtx *>(c)->st->sel.count > 0; });
  cmds.bind(CommandId::EditPaste, [](void *c) { (*static_cast<CmdCtx *>(c)->paste)(); }, &cc,
            [](const void *c) { return static_cast<const CmdCtx *>(c)->st->clip_count > 0; });
  cmds.bind(CommandId::SelectAll,
            [](void *c) {
              EditorState *s = static_cast<CmdCtx *>(c)->st;
              s->sel.clear();
              for (uint32_t i = s->scene.entity_count; i > 0; i--) s->sel.toggle((int32_t)(i - 1));
            },
            &cc, [](const void *c) { return static_cast<const CmdCtx *>(c)->st->scene.entity_count > 0; });
  cmds.bind(CommandId::SelectClear, [](void *c) { static_cast<CmdCtx *>(c)->st->sel.clear(); }, &cc,
            [](const void *c) { return static_cast<const CmdCtx *>(c)->st->sel.count > 0; });
  cmds.bind(CommandId::ViewGizmos,
            [](void *c) { // gizmolarin tamami ac/kapa
              GizmoOptions &g = static_cast<CmdCtx *>(c)->st->gizmos;
              const bool on = !(g.light_radius || g.shadow_volume || g.sun_dir);
              g.light_radius = g.shadow_volume = g.sun_dir = on;
            },
            &cc, nullptr, [](const void *c) {
              const GizmoOptions &g = static_cast<const CmdCtx *>(c)->st->gizmos;
              return g.light_radius || g.shadow_volume || g.sun_dir;
            });
  cmds.bind(CommandId::ViewConsole, [](void *c) { bool *b = static_cast<CmdCtx *>(c)->show_console; *b = !*b; }, &cc, nullptr,
            [](const void *c) { return *static_cast<const CmdCtx *>(c)->show_console; });
  cmds.bind(CommandId::GizmoTranslate, [](void *c) { *static_cast<CmdCtx *>(c)->gizmo_op = 0; }, &cc, nullptr,
            [](const void *c) { return *static_cast<const CmdCtx *>(c)->gizmo_op == 0; });
  cmds.bind(CommandId::GizmoRotate, [](void *c) { *static_cast<CmdCtx *>(c)->gizmo_op = 1; }, &cc, nullptr,
            [](const void *c) { return *static_cast<const CmdCtx *>(c)->gizmo_op == 1; });
  cmds.bind(CommandId::GizmoScale, [](void *c) { *static_cast<CmdCtx *>(c)->gizmo_op = 2; }, &cc, nullptr,
            [](const void *c) { return *static_cast<const CmdCtx *>(c)->gizmo_op == 2; });
  cmds.bind(CommandId::PlayToggle,
            [](void *c) {
              CmdCtx *x = static_cast<CmdCtx *>(c);
              (*x->play)(!x->st->playing);
            },
            &cc, nullptr, [](const void *c) { return static_cast<const CmdCtx *>(c)->st->playing; });
  cmds.bind(CommandId::PlayPause, [](void *c) { EditorState *s = static_cast<CmdCtx *>(c)->st; s->paused = !s->paused; }, &cc,
            [](const void *c) { return static_cast<const CmdCtx *>(c)->st->playing; },
            [](const void *c) { return static_cast<const CmdCtx *>(c)->st->paused; });
  cmds.bind(CommandId::PlayStep, [](void *c) { static_cast<CmdCtx *>(c)->st->step_request++; }, &cc,
            [](const void *c) {
              const EditorState *s = static_cast<const CmdCtx *>(c)->st;
              return s->playing && s->paused;
            });
  // Dosya menusune "Son dosyalar" alt menusu: komut tablosu KOMUT tasir, bu ise
  // bir veri listesi — chrome'un ek oge kancasindan geliyor.
  struct MenuExtraCtx {
    decltype(&guard_then) guard;
    int open_action;
  } mx{&guard_then, PendingOpenPath};
  ChromeMenuExtra menu_extra;
  menu_extra.ctx = &mx;
  menu_extra.fn = [](void *ctx, CommandCategory cat) {
    if (cat != CommandCategory::File) return;
    MenuExtraCtx *m = static_cast<MenuExtraCtx *>(ctx);
    const char *rec[kRecentMax];
    const uint32_t nrec = recent_list(rec, kRecentMax);
    if (!ImGui::BeginMenu("Son dosyalar", nrec > 0)) return;
    for (uint32_t i = 0; i < nrec; i++) {
      const bool var = recent_exists(i);
      char lbl[kFilePathLen + 16];
      std::snprintf(lbl, sizeof lbl, "%u  %s", i + 1, rec[i]);
      ImGui::BeginDisabled(!var); // eksik dosya SOLUK, gizli degil
      if (ImGui::MenuItem(lbl)) (*m->guard)(m->open_action, rec[i]);
      ImGui::EndDisabled();
      if (!var && ImGui::IsItemHovered()) ImGui::SetTooltip("Dosya bulunamadi: %s", rec[i]);
    }
    ImGui::EndMenu();
  };
  {
    // Kurulum denetimi: bagli kalmayan komut = menude tiklanmayan satir. Sessiz
    // gecmez; headless kosuda da gorunur.
    CommandId ub[kCommandCount];
    const uint32_t n = cmds.unbound(ub, kCommandCount);
    if (n) {
      console_log(ConsoleLevel::Uyari, kConsoleTagEditor, "%u komut BAGLANMADI (menude olu satir), ilki: %s", n, cmds.desc(ub[0]).name);
      std::fprintf(stderr, "[editor] %u komut BAGLANMADI (menude olu satir), ilki: %s\n", n, cmds.desc(ub[0]).name);
    }
  }
  // stdout/stderr yakalama: motorun printf'i ve dogrulama katmani konsola aksin.
  // DONGUDEN HEMEN ONCE aciliyor — yukaridaki kurulum yollari `return 1` ile
  // cikabiliyor ve borudaki bayt drain edilmeden kaybolurdu. HEADLESS'ta
  // ACILMAZ: kapi satirlari ([engine_editor] ... OK) dogrudan akmali.
  if (!headless && !console_capture_begin(&console_cap))
    console_log(ConsoleLevel::Uyari, kConsoleTagEditor, "cikti yakalanamadi: %s", console_cap.err_msg);

  while (running) {
    ENGINE_ZONE("frame");
    console_set_frame(frame_i);
    console_capture_drain(); // kare basina BIR kez; yakalama kapaliysa no-op
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
      // HiDPI: imlec mantiksal pikselde, arayuz framebuffer pikselinde. Oran
      // her karede okunur (pencere baska ekrana tasininca degisir).
      if (host->window_size) {
        uint32_t ww = 0, wh = 0;
        host->window_size(host->user, &ww, &wh);
        if (ww > 0) ui.set_pointer_scale((float)fw / (float)ww);
      }
    }
    // --- KAMERA: karar editor_camera.hpp'de (saf gecis fonksiyonu; kapilar orayi
    // olcer, burasi yalniz girdiyi toplar).
    if (in) {
      CameraInput ci;
      ci.dx = (float)(in->mouse_x - prev_mx);
      ci.dy = (float)(in->mouse_y - prev_my);
      ci.scroll = (float)(in->scroll_y - prev_scroll);
      ci.lmb = in->mouse_down[0];
      ci.rmb = in->mouse_down[1];
      ci.mmb = in->mouse_down[2];
      ci.shift = in->key_down[GLFW_KEY_LEFT_SHIFT] || in->key_down[GLFW_KEY_RIGHT_SHIFT];
      ci.ctrl = in->key_down[GLFW_KEY_LEFT_CONTROL] || in->key_down[GLFW_KEY_RIGHT_CONTROL];
      ci.alt = in->key_down[GLFW_KEY_LEFT_ALT] || in->key_down[GLFW_KEY_RIGHT_ALT];
      ci.key_w = in->key_down[GLFW_KEY_W];
      ci.key_a = in->key_down[GLFW_KEY_A];
      ci.key_s = in->key_down[GLFW_KEY_S];
      ci.key_d = in->key_down[GLFW_KEY_D];
      ci.key_q = in->key_down[GLFW_KEY_Q];
      ci.key_e = in->key_down[GLFW_KEY_E];
      // DIKKAT: kosul !ui.wants_mouse() DEGIL. 3B artik bir ImGui panelinin
      // icinde yasiyor, yani fare goruntunun uzerindeyken WantCaptureMouse
      // ZATEN true olur ve kamera goruntude HIC donmezdi. Dogru kosul: fare
      // goruntunun ustunde ve kaplama/gizmo onu almamis — ya da surukleme
      // zaten basladi (panelden disari tasan surukleme kesilmesin).
      const bool cam_btn = ci.rmb || ci.mmb;
      const bool can_start = view_hovered && !ovres.consumed_mouse && !ImGuizmo::IsOver() && !ImGuizmo::IsUsing();
      if (!cam_btn) cam_dragging = false;
      else if (can_start) cam_dragging = true;
      ci.allow_mouse = cam_dragging || can_start;
      ci.allow_keys = !ui.wants_text_input(); // WASD bir ad alanina yaziliyorsa ucus baslamasin
      ci.dt = dt;
      camera_update(cam, ci);
      prev_mx = in->mouse_x;
      prev_my = in->mouse_y;
      prev_scroll = in->scroll_y;
    }

    // Sim: yalniz oynatilirken (sabit adim). Duraklatilmisken zaman AKMAZ ama
    // govdeler yerinde durur; F10 tek adim ilerletir. fs.advance duraklamada
    // cagrilmaz — yoksa birikmis zaman devam edince bir anda bosalirdi.
    if (st.playing && !st.paused) {
      ENGINE_ZONE("sim");
      uint32_t ticks = fs.advance(dt);
      for (uint32_t t = 0; t < ticks; t++) { scene.tick(fs.step_s, tick_i++); st.play_time += fs.step_s; }
    } else if (st.playing && st.paused && st.step_request) {
      ENGINE_ZONE("sim");
      scene.tick(fs.step_s, tick_i++);
      st.play_time += fs.step_s;
      st.step_request--;
      fs.advance(dt); // biriken zamani YUT: adim adim ilerlerken geri kalmasin
    }

    // En-boy orani artik PENCERENIN degil, sahnenin icinde yasadigi PANELIN
    // orani: 3B viewport dokusuna ciziliyor ve o dokunun olcusu panelden geliyor.
    // Pencere oranini kullanmak sahneyi panelde gerilmis gosterirdi.
    const float aspect = vp.aspect();
    Mat4 proj = camera_projection(cam, aspect, 0.1f, 200.0f);
    Mat4 view = camera_view(cam);
    ren.set_camera(view, proj);
    ren.clear_point_lights();

    // --- ImGui paneller ---
    ui.begin_frame(in, (float)fw, (float)fh, dt);
    // Kisayollar: komut tablosundan. Koruma kurali tablonun (ham T/R/S/Delete
    // metin yazarken VE kaydirac suruklerken kapali; Ctrl+* yalniz metinde kapali).
    const InputGuards guards{ui.wants_text_input(), ui.wants_keyboard()};
    commands_poll_imgui(cmds, guards);
    // --- CERCEVE: menu / arac / durum cubugu (editor_chrome.hpp; sira sozlesmesi
    // orada: uc cubuk da ana viewport'un WorkRect'ini daraltir, dockspace tam
    // aralarina oturur — ImGui daraltmayi bir kare gecikmeli uygular).
    ChromeState cs;
    cs.playing = st.playing;
    cs.dirty = st.dirty;
    // Duraklatma durum cubugunda gorunsun (arac cubugu henuz duraklatmayi
    // cizmiyor; menude F6 var). Bos mesaj yerine acik bir ek.
    char status_buf[200];
    if (st.playing && st.paused) {
      std::snprintf(status_buf, sizeof status_buf, "DURAKLATILDI (F10 kare ilerlet) \xC2\xB7 %s", st.status);
      cs.status = status_buf;
    } else cs.status = st.status;
    cs.scene_path = st.scene_path;
    const int32_t prim_i = st.sel.primary();
    cs.primary_name = (prim_i >= 0 && prim_i < (int32_t)st.scene.entity_count) ? st.scene.entities[prim_i].name : nullptr;
    cs.selection_count = st.sel.count;
    cs.undo_count = st.hist.undo_count();
    cs.redo_count = st.hist.redo_count();
    cs.frame = frame_i;
    cs.tick = tick_i;
    cs.frame_ms = dt * 1000.0f;
    cs.entity_count = st.scene.entity_count;
    cs.gizmo_op = gizmo_op;
    cs.snap = snap_on;
    cs.snap_value = snap_step;
    cs.gizmos_visible = st.gizmos.light_radius || st.gizmos.shadow_volume || st.gizmos.sun_dir;
    const Vec3 eye = camera_eye(cam);
    cs.cam_eye[0] = eye.x; cs.cam_eye[1] = eye.y; cs.cam_eye[2] = eye.z;
    chrome_menu_bar(cmds, cs, menu_extra);
    ChromeOutput co;
    chrome_toolbar(cmds, cs, &co);
    if (co.snap_toggled) snap_on = !snap_on;
    if (co.snap_value_changed) snap_step = co.snap_value;
    chrome_status_bar(cs);
    // --- DOCKSPACE -----------------------------------------------------------
    // Paneller artik ekranda yuzen sabit pencereler degil; kullanici surukleyip
    // yeniden duzenleyebiliyor (Unity/Godot/Blender'in dordunde de boyle).
    // Varsayilan yerlesim ILK KAREDE programatik kuruluyor: ImGui'nin kendi
    // imgui.ini'si kapali (io.IniFilename = nullptr), yani duzen diskten
    // gelmiyor — kurulmazsa her acilista paneller serbest gelirdi.
    const ImGuiID dock_id = ImGui::DockSpaceOverViewport(ImGui::GetID("TulparDock"), ImGui::GetMainViewport(), 0);
    if (frame_i == 0) {
      layout_set_dockspace_id(dock_id);
      if (!layout_apply_default(dock_id, (float)fw, (float)fh)) {
        console_log(ConsoleLevel::Uyari, kConsoleTagEditor, "varsayilan duzen: %s", layout_last_error());
        std::fprintf(stderr, "[editor] varsayilan duzen: %s\n", layout_last_error());
      }
    }

    // --- GORUNUM: 3B sahnenin YASADIGI panel --------------------------------
    // Sahne viewport dokusuna ciziliyor ve burada gosteriliyor. Panel olcusu
    // degisince hedef yeniden yaratiliyor (yalniz GERCEKTEN degistiyse) ve
    // renderer'in cizim olcusu ona baglaniyor — en-boy orani artik pencerenin
    // degil PANELIN orani.
    view_rect = ViewportRect{};
    view_hovered = false;
    ovres = OverlayResult{}; // panel kapaliyken bayat sonuc uygulanmasin
    if (ImGui::Begin(kPanelGorunumLabel)) {
      const ImVec2 avail = ImGui::GetContentRegionAvail();
      const ImVec2 origin = ImGui::GetCursorScreenPos();
      if (avail.x >= 1.0f && avail.y >= 1.0f) {
        vp.resize((uint32_t)avail.x, (uint32_t)avail.y);
        ren.set_render_size(vp.width(), vp.height());
        if (vp.texture_id()) {
          ImGui::Image((ImTextureID)vp.texture_id(), ImVec2((float)vp.width(), (float)vp.height()));
          view_hovered = ImGui::IsItemHovered();
          // Kaplama: yalniz cizim listesi (oge yok) — altindaki Image tiklamayi
          // ve hover'i almaya devam eder. Eksen gizmosu kameranin 3x3'unden.
          OverlayInfo oi;
          const Mat4 vm = camera_view(cam);
          std::memcpy(oi.view, &vm.m[0][0], sizeof oi.view);
          const Vec3 eye = camera_eye(cam);
          oi.cam_eye[0] = eye.x; oi.cam_eye[1] = eye.y; oi.cam_eye[2] = eye.z;
          oi.cam_target[0] = cam.target.x; oi.cam_target[1] = cam.target.y; oi.cam_target[2] = cam.target.z;
          oi.gizmo_op = gizmo_op;
          oi.gizmos_visible = st.gizmos.light_radius || st.gizmos.shadow_volume || st.gizmos.sun_dir;
          oi.playing = st.playing;
          oi.hovered = view_hovered;
          oi.focused = ImGui::IsWindowFocused();
          oi.frame_ms = dt * 1000.0f;
          oi.draw_calls = ren.stats().draws; // onceki karenin kaydi (Tuzaklar 8aa)
          oi.entity_count = st.scene.entity_count;
          oi.proj = cam.proj;
          oi.cam_mode = cam.mode;
          oi.gizmo_space = gizmo_space;
          // Kisa tutuluyor: kaplama sigmayan hapi DUSURUR ve uzun ipucu kamera
          // hapiyla birlikte sigmadiginda hic gorunmuyordu (olculdu 937 px panelde).
          oi.hint = "Sa\xC4\x9F t\xC4\xB1k d\xC3\xB6nd\xC3\xBCr \xC2\xB7 orta tu\xC5\x9F kayd\xC4\xB1r \xC2\xB7 F odak";
          viewport_overlay(ViewportRect{origin.x, origin.y, (float)vp.width(), (float)vp.height()}, oi, nullptr, &ovres);
        } else {
          ImGui::TextUnformatted(vp.last_error()); // sessiz siyah panel YOK
        }
        view_rect = ViewportRect{origin.x, origin.y, (float)vp.width(), (float)vp.height()};
      }
    }
    ImGui::End();
    // --- Kaplamadan gelen gezinme eylemleri (cip/gosterge tiklamalari) ------
    if (ovres.axis_clicked >= 0) {
      camera_align(cam, (CameraAxis)ovres.axis_clicked);
      static const char *const kAx[6] = {"+X", "-X", "Ust", "Alt", "On", "Arka"};
      set_status(st, "eksen gorunusu: %s", kAx[ovres.axis_clicked]);
    }
    if (ovres.ortho_toggled) cam.proj = cam.proj == CameraProjection::Perspective ? CameraProjection::Ortho : CameraProjection::Perspective;
    if (ovres.mode_toggled) cam.mode = cam.mode == CameraMode::Orbit ? CameraMode::Fly : CameraMode::Orbit;
    if (ovres.gizmo_space_toggled) gizmo_space = gizmo_space == GizmoSpace::World ? GizmoSpace::Local : GizmoSpace::World;

    if (ImGui::Begin(kPanelSahneLabel)) {
      const int tb = hierarchy_toolbar(st.scene.entity_count, st.sel.count > 0);
      if (tb >= 1 && tb <= 4) do_add(tb);
      else if (tb == 5) do_remove();
      hierarchy_search(st.filter, sizeof st.filter);
      // Cizim sirasi belirlenimli ON-SIRADIR (kokler indeks sirasinda, cocuklar
      // indeks sirasinda). SUZGEC ACIKKEN duz liste cizilir: katlanmis bir ata
      // eslesmeyi gizlemesin.
      const bool filtering = st.filter[0] != 0;
      int32_t order[content::kSceneMaxEntities];
      uint32_t n = 0;
      if (filtering) {
        for (uint32_t i = 0; i < st.scene.entity_count; i++) order[n++] = (int32_t)i;
      } else n = content::scene_tree_order(st.scene, order, content::kSceneMaxEntities);
      HierarchyResult act;
      uint32_t shown = 0, hide_depth = 0; // hide_depth > 0: katlanmis alt agactayiz
      for (uint32_t k = 0; k < n; k++) {
        const uint32_t i = (uint32_t)order[k];
        if (i >= st.scene.entity_count) continue;
        const SceneEntity &e = st.scene.entities[i];
        const uint32_t depth = filtering ? 0u : content::scene_tree_depth(st.scene, i);
        if (hide_depth) {
          if (depth >= hide_depth) continue;
          hide_depth = 0;
        }
        if (filtering && !hierarchy_filter_match(e.name, st.filter)) continue;
        bool kids = false;
        for (uint32_t j = 0; j < st.scene.entity_count && !kids; j++) kids = st.scene.entities[j].parent == (int32_t)i;
        HierarchyRow row;
        row.name = e.name;
        row.selected = st.sel.contains((int32_t)i);
        row.has_model = (e.components & content::kSceneModel) != 0;
        row.has_light = (e.components & content::kSceneLight) != 0;
        row.has_body = (e.components & content::kSceneBody) != 0;
        row.has_anim = (e.components & content::kSceneAnim) != 0;
        row.depth = depth;
        row.has_children = kids && !filtering;
        row.expanded = !st.tree.collapse.collapsed(i);
        row.hidden = (e.flags & content::kSceneHidden) != 0;
        row.locked = (e.flags & content::kSceneLocked) != 0;
        shown++;
        const HierarchyResult r = hierarchy_tree_row((int)i, row, &st.tree);
        if (r.action != HierarchyAction::None) act = r;
        if (row.has_children && !row.expanded) hide_depth = depth + 1;
      }
      if (shown == 0)
        hierarchy_empty(st.scene.entity_count ? "S\xC3\xBCzge\xC3\xA7le e\xC5\x9Fle\xC5\x9F" "en varl\xC4\xB1k yok"
                                              : "Sahne bo\xC5\x9F \xE2\x80\x94 \xE2\x80\x9C+\xE2\x80\x9D ile varl\xC4\xB1k ekle");
      // Listenin altindaki bosluk: buraya birakmak KOKE tasir.
      const HierarchyResult zone = hierarchy_root_drop_zone(&st.tree);
      if (zone.action != HierarchyAction::None) act = zone;
      // F2: secili varligin adini YERINDE duzenle (panel odakliyken).
      if (ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows) && ImGui::IsKeyPressed(ImGuiKey_F2)) {
        const int32_t s0 = st.sel.primary();
        if (s0 >= 0 && s0 < (int32_t)st.scene.entity_count) hierarchy_begin_rename(&st.tree, s0, st.scene.entities[s0].name);
      }
      apply_hierarchy(act);
    }
    ImGui::End();
    if (ImGui::Begin(kPanelOzelliklerLabel)) {
      const int si = (int)st.sel.primary();
      if (si >= 0 && si < (int)st.scene.entity_count) {
        SceneEntity &e = st.scene.entities[si];
        const bool has_m = (e.components & content::kSceneModel) != 0, has_l = (e.components & content::kSceneLight) != 0,
                   has_b = (e.components & content::kSceneBody) != 0, has_a = (e.components & content::kSceneAnim) != 0;
        const char *icon = has_l ? "\xE2\x98\x80" : has_m ? "\xE2\x97\x86" : has_b ? "\xE2\x97\xBC" : "\xE2\x97\x8B"; // ☀ ◆ ◼ ○
        const Tone icon_tone = has_l ? Tone::Warn : has_m ? Tone::Text : has_b ? Tone::AxisZ : Tone::TextDim;
        char sub[96];
        if (st.sel.count > 1) std::snprintf(sub, sizeof sub, "grup: %u se\xC3\xA7ili \xC2\xB7 alanlar ana se\xC3\xA7ilide, gizmo grubu ta\xC5\x9F\xC4\xB1r", st.sel.count);
        else std::snprintf(sub, sizeof sub, "%u bile\xC5\x9F""en%s%s", (unsigned)(has_m + has_l + has_b + has_a),
                           has_b ? (e.shape == content::SceneShape::Box ? " \xC2\xB7 kutu g\xC3\xB6vde" : " \xC2\xB7 k\xC3\xBCre g\xC3\xB6vde") : "",
                           (has_b && e.dynamic) ? " \xC2\xB7 dinamik" : "");
        track_edit(st, e, si, inspector_title(icon, e.name, sizeof e.name, sub, icon_tone));

        section_label("D\xC3\x96N\xC3\x9C\xC5\x9E\xC3\x9CM");
        if (prop_begin("donusum")) {
          track_edit(st, e, si, prop_vec3("Konum", &e.pos.x, 0.05f));
          prop_help("Euler derece; uygulama s\xC4\xB1ras\xC4\xB1 T\xC2\xB7Rz\xC2\xB7Ry\xC2\xB7Rx\xC2\xB7S");
          track_edit(st, e, si, prop_vec3("D\xC3\xB6n\xC3\xBC\xC5\x9F", &e.rot_deg.x, 0.5f, 0, 0, "%.1f\xC2\xB0"));
          track_edit(st, e, si, prop_vec3("\xC3\x96l\xC3\xA7""ek", &e.scale.x, 0.02f, 0.05f, 20.0f));
          prop_end();
        }
        section_label("B\xC4\xB0LE\xC5\x9E""ENLER");
        bool rem = false;
        SceneEntity after = e;
        if (has_m) {
          if (component_header("\xE2\x97\x86", "Model", nullptr, &rem, true, Tone::Text)) {
            if (prop_begin("model")) {
              if (prop_asset("Kaynak", &after.asset, st.scene.assets, st.scene.asset_count).changed) commit(st, si, after);
              track_edit(st, e, si, prop_color("Renk", &e.tint.x));
              prop_end();
            }
            component_end();
          }
          if (rem) { after = e; after.components &= ~content::kSceneModel; commit(st, si, after); }
        }
        if (has_a) {
          rem = false;
          if (component_header("\xE2\x86\xBB", "Animasyon", nullptr, &rem, true, Tone::Accent)) {
            if (prop_begin("anim")) {
              track_edit(st, e, si, prop_float("Faz", &e.phase, 0.01f, 0.0f, 10.0f, "%.2f"));
              track_edit(st, e, si, prop_float("H\xC4\xB1z", &e.speed, 0.01f, 0.0f, 10.0f, "%.2f"));
              prop_end();
            }
            component_end();
          }
          if (rem) { after = e; after.components &= ~content::kSceneAnim; commit(st, si, after); }
        }
        if (has_l) {
          rem = false;
          if (component_header("\xE2\x98\x80", "I\xC5\x9F\xC4\xB1k", nullptr, &rem, true, Tone::Warn)) {
            if (prop_begin("isik")) {
              track_edit(st, e, si, prop_color("Renk", &e.light_color.x));
              track_edit(st, e, si, prop_float("\xC5\x9Eiddet", &e.light_intensity, 0.05f, 0.0f, 100.0f, "%.2f"));
              track_edit(st, e, si, prop_float("Yar\xC4\xB1\xC3\xA7""ap", &e.light_radius, 0.05f, 0.1f, 100.0f, "%.2f m"));
              prop_end();
            }
            component_end();
          }
          if (rem) { after = e; after.components &= ~content::kSceneLight; commit(st, si, after); }
        }
        if (has_b) {
          rem = false;
          after = e;
          if (component_header("\xE2\x97\xBC", "G\xC3\xB6vde", nullptr, &rem, true, Tone::AxisZ)) {
            if (prop_begin("govde")) {
              int shape = (int)e.shape;
              if (prop_combo("\xC5\x9E""ekil", &shape, "Kutu\0K\xC3\xBCre\0").changed) { after.shape = (content::SceneShape)shape; commit(st, si, after); }
              if (e.shape == content::SceneShape::Box) track_edit(st, e, si, prop_vec3("Yar\xC4\xB1m kenar", &e.half.x, 0.02f, 0.01f, 50.0f, "%.2f"));
              else track_edit(st, e, si, prop_float("Yar\xC4\xB1\xC3\xA7""ap", &e.radius, 0.02f, 0.01f, 50.0f, "%.2f"));
              bool dyn = e.dynamic;
              if (prop_check("Dinamik", &dyn).changed) { after = e; after.dynamic = dyn; commit(st, si, after); }
              prop_end();
            }
            component_end();
          }
          if (rem) { after = e; after.components &= ~content::kSceneBody; commit(st, si, after); }
        }
        const char *names[4];
        uint32_t bits[4];
        uint32_t n = 0;
        if (!has_m) { names[n] = "\xE2\x97\x86  Model"; bits[n++] = content::kSceneModel; }
        if (!has_a) { names[n] = "\xE2\x86\xBB  Animasyon"; bits[n++] = content::kSceneAnim; }
        if (!has_l) { names[n] = "\xE2\x98\x80  I\xC5\x9F\xC4\xB1k"; bits[n++] = content::kSceneLight; }
        if (!has_b) { names[n] = "\xE2\x97\xBC  G\xC3\xB6vde"; bits[n++] = content::kSceneBody; }
        const int add = component_add_button(names, n);
        if (add >= 0) {
          after = e;
          after.components |= bits[add];
          if (bits[add] == content::kSceneModel && after.asset < 0) after.asset = 0;
          commit(st, si, after);
        }
      } else inspector_empty("Sahne listesinden bir varl\xC4\xB1k se\xC3\xA7");
    }
    ImGui::End();
    // Dunya paneli: gunes/ortam/golge (gunluge SceneOp::World), kamera (canli; sahneye yazmak ayri islem).
    if (ImGui::Begin(kPanelDunyaLabel)) {
      section_label("G\xC3\x9CNE\xC5\x9E");
      if (prop_begin("gunes")) {
        track_world_edit(st, prop_vec3("Y\xC3\xB6n", &st.scene.sun_dir.x, 0.01f, -1.0f, 1.0f, "%.2f"));
        track_world_edit(st, prop_float("\xC5\x9Eiddet", &st.scene.sun_diffuse, 0.01f, 0.0f, 5.0f, "%.2f"));
        track_world_edit(st, prop_color("Ortam", &st.scene.ambient.x));
        prop_end();
      }
      section_label("G\xC3\x96LGE HACM\xC4\xB0");
      if (prop_begin("golge")) {
        track_world_edit(st, prop_vec3("Merkez", &st.scene.shadow_center.x, 0.1f, 0, 0, "%.1f"));
        track_world_edit(st, prop_float("Yar\xC4\xB1\xC3\xA7""ap", &st.scene.shadow_radius, 0.1f, 1.0f, 500.0f, "%.1f m"));
        track_world_edit(st, prop_float("Derinlik", &st.scene.shadow_depth, 0.5f, 1.0f, 2000.0f, "%.1f m"));
        prop_end();
      }
      // --- Gorunum: motorun ayarlanabilir render ozellikleri ------------
      // Bunlarin hepsi ZATEN kodlanmis ama editorde hic yuzu yoktu. Sektor
      // editorlerinde tam olarak burada dururlar (UE5: Post Process Volume +
      // Scalability; Unity: Quality/Volume).
      section_label("G\xC3\x96R\xC3\x9CN\xC3\x9CM");
      if (prop_begin("golge_kalite")) {
        prop_help("G\xC3\xB6lge haritasi kapatilinca sahne duz aydinlanir; egilim degerleri golge akne/ayrilma dengesidir.");
        prop_check("G\xC3\xB6lgeler", &st.render.shadows);
        prop_float("Derinlik e\xC4\x9Filimi", &st.render.shadow_bias, 0.0001f, 0.0f, 0.02f, "%.4f");
        prop_float("Normal kayd\xC4\xB1rma", &st.render.shadow_normal_offset, 0.005f, 0.0f, 1.0f, "%.3f m");
        prop_end();
      }
      {
        // Durum: SESSIZ kapanma yok -- ozellik kapaliysa SEBEBI yazilir.
        const renderer::ShadowInfo si = ren.shadow();
        if (!si.enabled)
          ImGui::TextDisabled("G\xC3\xB6lge kapal\xC4\xB1: %s",
                              si.disabled_reason[0] ? si.disabled_reason : "panelden kapat\xC4\xB1ld\xC4\xB1");
        else
          ImGui::TextDisabled("%u px \xC3\x97 %u kademe \xC2\xB7 %s", si.size, si.cascades,
                              si.linear_filter ? "donan\xC4\xB1m PCF" : "NEAREST");
      }

      if (prop_begin("post")) {
        prop_help("Pozlama ve bloom, sahnenin ic HDR hedefi uzerinde calisir.");
        prop_float("Pozlama", &st.render.exposure, 0.01f, 0.01f, 8.0f, "%.2f");
        prop_float("Bloom e\xC5\x9Fi\xC4\x9Fi", &st.render.bloom_threshold, 0.01f, 0.0f, 8.0f, "%.2f");
        prop_float("Bloom \xC5\x9Fiddeti", &st.render.bloom_intensity, 0.01f, 0.0f, 2.0f, "%.2f");
        prop_float("Yumu\xC5\x9F" "ak diz", &st.render.bloom_knee, 0.01f, 0.0f, 1.0f, "%.2f");
        prop_float("Bloom yar\xC4\xB1\xC3\xA7" "ap\xC4\xB1", &st.render.bloom_radius, 0.01f, 0.5f, 3.0f, "%.2f");
        prop_end();
      }
      {
        const renderer::PostInfo pi = ren.post();
        if (!pi.enabled)
          ImGui::TextDisabled("Sonradan i\xC5\x9Fleme kapal\xC4\xB1: %s",
                              pi.disabled_reason[0] ? pi.disabled_reason : "ac\xC4\xB1lmad\xC4\xB1");
        else
          ImGui::TextDisabled("%ux%u \xC2\xB7 %u bloom mip \xC2\xB7 %u ge\xC3\xA7i\xC5\x9F \xC2\xB7 %.1f MB", pi.width, pi.height,
                              pi.bloom_mips, pi.pass_count, (double)pi.target_bytes / (1024.0 * 1024.0));
      }

      if (prop_begin("olceklendirme")) {
        prop_help("Sahne ic hedefin bir ALT dikdortgenine cizilir ve birlestirme gecisinde buyutulur; kare icinde ayirma olmaz.");
        prop_float("Render \xC3\xB6l\xC3\xA7" "e\xC4\x9Fi", &st.render.render_scale, 0.01f, 0.5f, 1.0f, "%.2f");
        prop_combo("Y\xC3\xBCkseltici", &st.render.upscaler, "Yok\0" "Do\xC4\x9Frusal\0" "Keskinle\xC5\x9Ftir\0");
        prop_float("Keskinlik", &st.render.sharpness, 0.01f, 0.0f, 1.0f, "%.2f");
        prop_check("Titretme (TAA)", &st.render.jitter);
        prop_end();
      }
      {
        const renderer::TemporalInfo ti = ren.temporal();
        if (ti.scale_disabled_reason[0])
          ImGui::TextDisabled("\xC3\x96l\xC3\xA7" "ekleme yok: %s", ti.scale_disabled_reason);
        else
          ImGui::TextDisabled("sahne %ux%u (\xC3\xB6l\xC3\xA7" "ek %.2f)", ti.scaled_width, ti.scaled_height,
                              (double)ti.render_scale);
      }

      section_label("G\xC4\xB0ZMOLAR");
      if (prop_begin("gizmo")) {
        prop_check("I\xC5\x9F\xC4\xB1k yar\xC4\xB1\xC3\xA7""ap\xC4\xB1", &st.gizmos.light_radius);
        prop_check("G\xC3\xB6lge hacmi", &st.gizmos.shadow_volume);
        prop_check("G\xC3\xBCne\xC5\x9F y\xC3\xB6n\xC3\xBC", &st.gizmos.sun_dir);
        prop_end();
      }
      section_label("KAMERA");
      if (prop_begin("kamera")) {
        prop_vec3("Hedef", &cam.target.x, 0.05f, 0, 0, "%.2f");
        prop_float("Yaw", &cam.yaw, 0.01f, 0, 0, "%.2f");
        prop_float("Pitch", &cam.pitch, 0.01f, -cam.pitch_limit, cam.pitch_limit, "%.2f"); // ufkun ALTI da serbest
        prop_float("Uzakl\xC4\xB1k", &cam.radius, 0.1f, cam.min_radius, cam.max_radius, "%.1f");
        prop_float("G\xC3\xB6r\xC3\xBC\xC5\x9F a\xC3\xA7\xC4\xB1s\xC4\xB1", &cam.fov_y, 0.01f, 0.2f, 2.0f, "%.2f rad");
        prop_float("U\xC3\xA7u\xC5\x9F h\xC4\xB1z\xC4\xB1", &cam.speed, 0.1f, 0.5f, 200.0f, "%.1f m/s");
        prop_end();
      }
      // Kamera sahneye yalniz istekle yazilir (gunluge girer); canli kamera
      // dosyayi kirletmez.
      const float bw = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
      if (ImGui::Button("Sahneye yaz", ImVec2(bw, 0))) {
        content::SceneWorld w = st.scene.world();
        w.cam_target = cam.target; w.cam_yaw = cam.yaw; w.cam_pitch = cam.pitch; w.cam_radius = cam.radius;
        if (st.hist.set_world(st.scene, w)) { st.dirty = true; set_status(st, "kamera sahneye yazildi"); }
      }
      ImGui::SameLine();
      if (ImGui::Button("Sahnedekine git", ImVec2(bw, 0))) { cam.target = st.scene.cam_target; cam.yaw = st.scene.cam_yaw; cam.pitch = st.scene.cam_pitch; cam.radius = st.scene.cam_radius; }
    }
    ImGui::End();
    // Kaynak tarayici: sahne dosyasinin dizinindeki glTF'ler + sahnenin kaynak
    // tablosu (yuklendi/yuklenemedi). Ekleme dongu icinde yapilmaz (liste
    // yeniden taranir): secilen dosya adi kopyalanip donguden sonra islenir.
    if (ImGui::Begin(kPanelKaynaklarLabel)) {
      AssetsAction act;
      assets_panel(st.assets_view, st.scene_dir, st.browse, st.browse_count, st.scene.assets, st.have, st.scene.asset_count, &act);
      if (act.refresh) st.browse_count = editor_scan_assets(st.scene_dir, st.scene, st.browse, 64);
      if (act.add_index >= 0 && act.add_index < (int)st.browse_count) {
        // Once kopyala: do_add_asset listeyi yeniden tarar, isaretci bayatlar.
        char add_file[content::kScenePathLen];
        std::snprintf(add_file, sizeof add_file, "%s", st.browse[act.add_index].name);
        do_add_asset(add_file);
      }
    }
    ImGui::End();
    if (show_console && ImGui::Begin(kPanelKonsolLabel, &show_console)) console_panel(console_view);
    if (show_console) ImGui::End(); // Begin false dondugunde de End ZORUNLU
    // Gizmo: ImGuizmo GL gelenegi (NDC y yukari) bekler; Vulkan projeksiyonun y'si tersken duzeltilir.
    // Surukleme tek islem: IsUsing baslarken kopya, bitince gunluge.
    const int32_t gz = st.sel.primary();
    if (gz >= 0 && gz < (int32_t)st.scene.entity_count) {
      SceneEntity &e = st.scene.entities[gz];
      Mat4 proj_gl = proj;
      proj_gl.m[1][1] = -proj_gl.m[1][1];
      ImGuizmo::SetOrthographic(false);
      ImGuizmo::SetRect(0, 0, (float)fw, (float)fh);
      Mat4 mtx = content::scene_entity_world_matrix(st.scene, (uint32_t)gz);
      const ImGuizmo::OPERATION op = gizmo_op == 0 ? ImGuizmo::TRANSLATE : gizmo_op == 1 ? ImGuizmo::ROTATE : ImGuizmo::SCALE;
      const float snap_vec[3] = {snap_step, snap_step, snap_step};
      ImGuizmo::SetOrthographic(cam.proj == CameraProjection::Ortho);
      const bool changed = ImGuizmo::Manipulate(&view.m[0][0], &proj_gl.m[0][0], op,
                                                gizmo_space == GizmoSpace::Local ? ImGuizmo::LOCAL : ImGuizmo::WORLD, &mtx.m[0][0], nullptr,
                                                snap_on ? snap_vec : nullptr);
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
        // Gizmo DUNYA uzayinda calisir, varligin alanlari YERELDIR: ebeveynli bir
        // varlikta dunya matrisini dogrudan yazmak konumu ebeveynin katina cikarirdi.
        entity_from_matrix(e, content::scene_world_to_local_matrix(st.scene, (uint32_t)gz, mtx));
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
      if (ovres.box_done) {
        // Kutu (marquee) secim: kaplama dikdortgeni verdi, izdusum testi saf
        // fonksiyonda (kamera ARKASINDAKI kutular orada eleniyor).
        static content::SceneBounds bb[content::kSceneMaxEntities];
        const uint32_t nb = entity_world_bounds(st, phys, bb);
        static int32_t hits[Selection::kMax];
        const uint32_t nh = viewport_box_select(proj * view, bb, nb, view_rect, ovres.box[0], ovres.box[1], ovres.box[2], ovres.box[3],
                                                /*tam icerme*/ false, hits, Selection::kMax);
        if (!ImGui::GetIO().KeyCtrl) st.sel.clear();
        for (uint32_t k = 0; k < nh; k++)
          if (!st.sel.contains(hits[k])) st.sel.toggle(hits[k]);
        set_status(st, "kutu secim: %u varlik", st.sel.count);
      } else if (pressed && pick.valid && view_hovered && !ovres.consumed_mouse && !ImGuizmo::IsUsing() && !ImGuizmo::IsOver()) {
        static content::SceneBounds wb[content::kSceneMaxEntities];
        const uint32_t nb = entity_world_bounds(st, phys, wb);
        Vec3 o, d;
        camera_ray(cam, aspect, pick.x, pick.y, (float)vp.width(), (float)vp.height(), &o, &d);
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
    // "F": secime odaklan (secim yoksa tum sahne) — Unity/Blender geleneği.
    // Kenar tetikli: basili tutmak kamerayi surekli kenetlemesin.
    {
      const bool f_now = in && in->key_down[GLFW_KEY_F] && !ui.wants_text_input();
      if (f_now && !prev_f) {
        static content::SceneBounds fb[content::kSceneMaxEntities];
        const uint32_t nb = entity_world_bounds(st, phys, fb);
        bool any = false;
        content::SceneBounds u{};
        for (uint32_t k = 0; k < st.sel.count; k++) {
          const int32_t i = st.sel.items[k];
          if (i < 0 || i >= (int32_t)nb) continue;
          if (!any) { u = fb[i]; any = true; }
          else {
            u.lo = Vec3{u.lo.x < fb[i].lo.x ? u.lo.x : fb[i].lo.x, u.lo.y < fb[i].lo.y ? u.lo.y : fb[i].lo.y,
                        u.lo.z < fb[i].lo.z ? u.lo.z : fb[i].lo.z};
            u.hi = Vec3{u.hi.x > fb[i].hi.x ? u.hi.x : fb[i].hi.x, u.hi.y > fb[i].hi.y ? u.hi.y : fb[i].hi.y,
                        u.hi.z > fb[i].hi.z ? u.hi.z : fb[i].hi.z};
          }
        }
        if (any) camera_focus(cam, u);
        else camera_focus_all(cam, fb, nb);
        set_status(st, any ? "odak: secim" : "odak: tum sahne");
      }
      prev_f = f_now;
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
      camera_ray(cam, aspect, px, py, (float)fw, (float)fh, &o, &d);
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
    if (headless && frame_i == 2 && st.scene.entity_count && view_rect.w > 0) {
      // E1 secim kapisi — PANEL uzerinden: varlik 0 viewport dokusuna izdusurulur,
      // o panel pikseli pencere pikseline cevrilip vp.map_mouse ile GERI eslenir
      // (editorun tiklamada yaptigi yol), isin ayni varligi bulmali. Kontrol:
      // panelin DISINDAKI bir piksel (Sahne panelinin ustu) gecersiz eslenir —
      // yani orada tiklamak 3B'de hicbir sey secmez.
      static content::SceneBounds wb2[content::kSceneMaxEntities];
      const uint32_t nb = entity_world_bounds(st, phys, wb2);
      const Vec3 p0 = st.scene.entities[0].pos;
      const Vec4 clip = proj * (view * Vec4{p0.x, p0.y, p0.z, 1.0f});
      const float lx = (clip.x / clip.w * 0.5f + 0.5f) * (float)vp.width(), ly = (clip.y / clip.w * 0.5f + 0.5f) * (float)vp.height();
      const ViewportPick inside = vp.map_mouse(view_rect, view_rect.x + lx, view_rect.y + ly);
      const ViewportPick outside = vp.map_mouse(view_rect, view_rect.x - 8.0f, view_rect.y + view_rect.h * 0.5f);
      int32_t hit = -1;
      float t = 0;
      if (inside.valid) {
        Vec3 o, d;
        camera_ray(cam, aspect, inside.x, inside.y, (float)vp.width(), (float)vp.height(), &o, &d);
        hit = content::scene_pick(wb2, nb, o, d, &t);
      }
      const bool pok = inside.valid && hit == 0 && !outside.valid;
      std::printf("[engine_editor] panel secim kapisi: panel %.0fx%.0f @(%.0f,%.0f), ic piksel (%.0f,%.0f) -> %s, dis piksel gecersiz %s %s\n", view_rect.w,
                  view_rect.h, view_rect.x, view_rect.y, view_rect.x + lx, view_rect.y + ly, hit >= 0 ? st.scene.entities[hit].name : "-",
                  outside.valid ? "HAYIR" : "evet", pok ? "OK" : "HATA");
      if (!pok) return 1;
    }
    if (headless && frame_i == 6 && st.scene.entity_count >= 2) {
      // Pano kapisi: iki varlik kopyala -> yapistir -> sayi +2 ve yapistirilanin
      // BAYTLARI kaynakla ayni (yalniz konum otelendi); geri al baslangica doner.
      // Kontrol: pano BOSKEN yapistir hicbir sey eklemez.
      char txt_before[8192], txt_after[8192];
      content::scene_write(st.scene, txt_before, sizeof txt_before);
      const uint32_t n0 = st.scene.entity_count;
      st.clip_count = 0;
      do_paste(); // KONTROL: bos pano
      const bool empty_noop = st.scene.entity_count == n0;
      st.sel.set_single(0);
      st.sel.toggle(1);
      do_copy();
      const uint32_t copied = st.clip_count;
      do_paste();
      const uint32_t n_pasted = st.scene.entity_count; // geri al'DAN ONCE (rapor bunu yazsin)
      const bool grew = n_pasted == n0 + 2;
      bool same_fields = false;
      if (grew) {
        SceneEntity a = st.scene.entities[0], b = st.scene.entities[n0];
        b.pos.x -= 1.0f; // yapistirmanin otelemesi
        same_fields = content::scene_entity_equal(a, b);
      }
      do_undo();
      content::scene_write(st.scene, txt_after, sizeof txt_after);
      const bool back = std::strcmp(txt_before, txt_after) == 0 && st.scene.entity_count == n0;
      const bool pok = empty_noop && copied == 2 && grew && same_fields && back;
      std::printf("[engine_editor] pano kapisi: kopyalanan %u, yapistirinca %u -> %u, alanlar ayni %s, KONTROL bos pano eklemedi %s, geri al baslangica dondu %s %s\n",
                  copied, n0, n_pasted, same_fields ? "evet" : "HAYIR", empty_noop ? "evet" : "HAYIR", back ? "evet" : "HAYIR",
                  pok ? "OK" : "HATA");
      if (!pok) return 1;
      st.clip_count = 0;
      st.sel.set_single(0);
      st.dirty = false;
    }
    if (headless && frame_i == 10 && st.scene.entity_count >= 3) {
      // Sahne agaci kapisi (EDITORUN kendi yolu): varlik 1'i varlik 0'a baglayinca
      // DUNYA siniri yerinde kalmali (reparent yerel donusumu yeniden hesaplar);
      // sonra EBEVEYNI oteleyince cocugun siniri da otelenmeli — kalitim
      // editorun sinir/secim yolundan geciyor mu? Kontrol: bagsiz bir varligin
      // siniri ayni otelemede KIPIRDAMAZ.
      static content::SceneBounds b0[content::kSceneMaxEntities], b1[content::kSceneMaxEntities], b2[content::kSceneMaxEntities];
      char txt_before[8192], txt_after[8192];
      content::scene_write(st.scene, txt_before, sizeof txt_before);
      entity_world_bounds(st, phys, b0);
      const bool bound = st.hist.reparent(st.scene, 1, 0);
      if (bound) st.groups.push(1);
      entity_world_bounds(st, phys, b1);
      const Vec3 d_keep = b1[1].lo - b0[1].lo;
      const bool kept = length(d_keep) < 1e-3f;
      SceneEntity par = st.scene.entities[0];
      par.pos.x += 5.0f;
      const bool moved_ok = st.hist.set_entity(st.scene, 0, par);
      if (moved_ok) st.groups.push(1);
      entity_world_bounds(st, phys, b2);
      const float child_dx = b2[1].lo.x - b1[1].lo.x;
      const float other_dx = b2[2].lo.x - b1[2].lo.x; // KONTROL: bagsiz varlik
      const bool inherited = child_dx > 4.99f && child_dx < 5.01f && other_dx > -0.01f && other_dx < 0.01f;
      do_undo();
      do_undo();
      content::scene_write(st.scene, txt_after, sizeof txt_after);
      const bool back = std::strcmp(txt_before, txt_after) == 0;
      const bool hok = bound && kept && moved_ok && inherited && back;
      std::printf("[engine_editor] sahne agaci kapisi: baglandi %s, dunya siniri yerinde kaldi %s (sapma %.4f), ebeveyn +5 -> cocuk %+.2f, "
                  "KONTROL bagsiz %+.2f, geri al baslangica dondu %s %s\n",
                  bound ? "evet" : "HAYIR", kept ? "evet" : "HAYIR", (double)length(d_keep), (double)child_dx, (double)other_dx,
                  back ? "evet" : "HAYIR", hok ? "OK" : "HATA");
      if (!hok) return 1;
      st.dirty = false;
    }
    // Duraklatma kapisi (kare 7-9): duraklatilmisken tick DURUR, F10 TEK adim
    // ilerletir. Kontrol duraklatmanin kendisidir: duraklamadan once tick akiyor.
    if (headless && frame_i >= 7 && frame_i <= 9 && st.playing) {
      static uint32_t t_pause = 0, t_hold = 0;
      if (frame_i == 7) {
        t_pause = tick_i;
        st.paused = true;
      } else if (frame_i == 8) {
        t_hold = tick_i;
        st.step_request = 1;
      } else {
        const bool held = t_hold == t_pause;      // duraklatma: tick akmadi
        const bool stepped = tick_i == t_hold + 1; // tek adim: tam bir tick
        std::printf("[engine_editor] duraklatma kapisi: tick %u -> %u (duraklatildi, akmadi %s) -> %u (tek adim %s) %s\n", t_pause, t_hold,
                    held ? "evet" : "HAYIR", tick_i, stepped ? "evet" : "HAYIR", (held && stepped) ? "OK" : "HATA");
        if (!(held && stepped)) return 1;
        st.paused = false;
      }
    }
    if (headless && frame_i >= 2 && frame_i <= 5) {
      // E1 duzen kaliciligi kapisi: kaydet -> A; dosyadan yukle -> (bir kare sonra,
      // dugum dikdortgenleri DockSpace'te turetilir) B; A == B BIT-TAM. Pozitif
      // kontrol: dosyadaki bir bolme oranini degistirip yuklemek C'yi A'dan
      // AYIRMALI — yoksa "esit" olcumu hicbir seyi olcmuyordur. Sonra ozgun
      // dosya geri yuklenir (kalan kareler varsayilan duzende kosar).
      static LayoutRect A[kLayoutMaxWindows], B[kLayoutMaxWindows], C[kLayoutMaxWindows];
      static uint32_t na = 0, nbb = 0, nc = 0;
      static char lay[1200], lay_mut[1200];
      static bool lfail = false;
      LayoutError lerr{};
      auto rects_equal = [](const LayoutRect *x, uint32_t nx, const LayoutRect *y, uint32_t ny) {
        if (nx != ny) return false;
        for (uint32_t i = 0; i < nx; i++)
          if (!layout_rect_equal(x[i], y[i])) return false;
        return true;
      };
      if (frame_i == 2) {
        std::snprintf(lay, sizeof lay, "%s/.duzen_kapisi.duzen", st.scene_dir);
        std::snprintf(lay_mut, sizeof lay_mut, "%s/.duzen_kapisi_mut.duzen", st.scene_dir);
        na = layout_snapshot(A, kLayoutMaxWindows);
        if (!layout_save(lay, &lerr)) { std::printf("[engine_editor] duzen kapisi: KAYDEDILEMEDI %s\n", lerr.msg); return 1; }
        // Mutant: ilk bolmenin genisligini degistir (satir bazli, metin dosyasi).
        FILE *f = std::fopen(lay, "rb");
        static char txt[kLayoutMaxBytes + 1];
        size_t n = f ? std::fread(txt, 1, kLayoutMaxBytes, f) : 0;
        if (f) std::fclose(f);
        txt[n] = 0;
        // Ilk "w=" ya da genislik alanini bul: dosya formatini editor_layout yazar,
        // burada yalniz ilk sayisal SizeRef'i %30 buyutuyoruz.
        char *dg = std::strstr(txt, "dugum ");
        bool mutated = false;
        FILE *m = std::fopen(lay_mut, "wb");
        if (m && dg) {
          // Kok olmayan ILK yaprak dugumun SizeRef genisligini %30 buyut (bolme
          // orani ondan turer). Satir formati editor_layout.cpp::layout_write:
          // "dugum <i> <parent> <x|y|-> <w> <h>" — '-' yaprak.
          char *line = dg;
          while (line && !mutated) {
            int idx, par; char ax; float w, h;
            if (std::sscanf(line, "dugum %d %d %c %f %f", &idx, &par, &ax, &w, &h) == 5 && ax == '-' && par >= 0 && w > 0) {
              char *eol = std::strchr(line, '\n');
              std::fwrite(txt, 1, (size_t)(line - txt), m);
              std::fprintf(m, "dugum %d %d - %.9g %.9g", idx, par, w * 1.3f, h);
              if (eol) std::fputs(eol, m);
              mutated = true;
            } else {
              char *eol = std::strchr(line, '\n');
              line = eol ? eol + 1 : nullptr;
            }
          }
          if (!mutated) std::fwrite(txt, 1, n, m);
          std::fclose(m);
        } else if (m) { std::fwrite(txt, 1, n, m); std::fclose(m); }
        if (!mutated) std::printf("[engine_editor] duzen kapisi: UYARI mutant uretilemedi (format degisti mi?)\n");
        if (!layout_load(lay, &lerr)) { std::printf("[engine_editor] duzen kapisi: YUKLENEMEDI %s\n", lerr.msg); return 1; }
      } else if (frame_i == 3) {
        nbb = layout_snapshot(B, kLayoutMaxWindows);
        if (!layout_load(lay_mut, &lerr)) { std::printf("[engine_editor] duzen kapisi: mutant YUKLENEMEDI %s\n", lerr.msg); lfail = true; }
      } else if (frame_i == 4) {
        nc = layout_snapshot(C, kLayoutMaxWindows);
        if (!layout_load(lay, &lerr)) { std::printf("[engine_editor] duzen kapisi: geri YUKLENEMEDI %s\n", lerr.msg); lfail = true; }
      } else if (frame_i == 5) {
        static LayoutRect D[kLayoutMaxWindows];
        const uint32_t nd = layout_snapshot(D, kLayoutMaxWindows);
        const bool ab = rects_equal(A, na, B, nbb), ac = rects_equal(A, na, C, nc), ad = rects_equal(A, na, D, nd);
        const bool lok = !lfail && na >= kLayoutPanelCount && ab && !ac && ad;
        std::printf("[engine_editor] duzen kapisi: %u panel, kaydet->yukle bit-tam %s, KONTROL mutant farkli %s, geri yukle bit-tam %s, atlanan %u %s\n", na,
                    ab ? "evet" : "HAYIR", ac ? "HAYIR" : "evet", ad ? "evet" : "HAYIR", layout_skipped(), lok ? "OK" : "HATA");
        std::remove(lay);
        std::remove(lay_mut);
        if (!lok) return 1;
      }
    }
    // --- Kipli pencereler: HER KARE AYNI YERDEN cagrilir (ImGui popup kimligi
    // bulunulan pencere yiginindan turer; menu geri cagrisindan OpenPopup etmek
    // kimligi kaydirirdi — ikisi de OpenPopup'i kendi icinde yapiyor).
    if (dlg.open) {
      const FileDialogAction fa = file_dialog_draw(dlg);
      if (fa == FileDialogAction::Accepted) {
        if (dlg.mode == FileDialogMode::Ac) load_scene_from(dlg.path);
        else if (save_scene_to(dlg.path) && pending != PendingNone) { run_pending(pending); pending = PendingNone; }
      } else if (fa == FileDialogAction::Cancelled) pending = PendingNone;
    }
    if (confirm.open) {
      char msg[320];
      std::snprintf(msg, sizeof msg, "\x22%s\x22 dosyasinda kaydedilmemis degisiklikler var.\nNe yapilsin?",
                    st.scene_path[0] ? file_path_base(st.scene_path) : "adsiz sahne");
      const ConfirmResult cr = confirm_modal(confirm, "Sahne kaydedilmedi", msg, "Kaydet", "Vazge\xC3\xA7", "Kaydetme");
      if (cr == ConfirmResult::Ok) {
        do_save(); // adsiz sahnede diyalog acar: bekleyen eylem orada kosar
        if (!st.dirty && pending != PendingNone) { run_pending(pending); pending = PendingNone; }
      } else if (cr == ConfirmResult::Third) { run_pending(pending); pending = PendingNone; }
      else if (cr == ConfirmResult::Cancel) pending = PendingNone;
    }
    ui.end_frame();

    // --- 3B cizim: veri modelinden (dunya isigi/golgesi de her kare modelden: panel canli) ---
    ren.set_light(normalize(st.scene.sun_dir), st.scene.ambient, st.scene.sun_diffuse);
    ren.set_shadow_volume(st.scene.shadow_center, st.scene.shadow_radius, st.scene.shadow_depth);
    // Gorüntü ayarlari: hepsi ucuz set_* cagrisi, kare icinde ayirma YOK.
    // Her karede kosulsuz uygulanir -- "degisti mi" takibi, panelin disindan
    // (geri al/yinele, betik) gelen degisiklikleri kacirirdi.
    ren.set_shadows_enabled(st.render.shadows);
    ren.set_shadow_bias(st.render.shadow_bias, st.render.shadow_normal_offset);
    ren.set_exposure(st.render.exposure);
    ren.set_bloom(st.render.bloom_threshold, st.render.bloom_intensity);
    ren.set_bloom_shape(st.render.bloom_knee, st.render.bloom_radius);
    ren.set_render_scale(st.render.render_scale);
    ren.set_upscaler((renderer::UpscalerKind)st.render.upscaler, st.render.sharpness);
    ren.set_jitter(st.render.jitter);
    ren.set_shadow_focus(cam.target); // yakin kademeler kameranin baktigi yerde
    ren.begin_frame(headless ? 0 : frame_i);
    scene.draw(ren, ds);
    for (uint32_t i = 0; i < st.scene.entity_count; i++) {
      const SceneEntity &e = st.scene.entities[i];
      const bool simulated = st.playing && st.bodies_live && st.bodies[i].valid() && e.dynamic;
      if (e.flags & content::kSceneHidden) continue; // panelde gozu kapatilmis varlik CIZILMEZ
      const Mat4 m = simulated ? content::scene_body_matrix(e, phys, st.bodies[i]) : content::scene_entity_world_matrix(st.scene, i);
      const bool sel = st.sel.contains((int32_t)i);
      const Vec3 tint = sel ? Vec3{1.0f, 0.9f, 0.4f} : e.tint;
      bool drew = false;
      if ((e.components & content::kSceneModel) && e.asset >= 0 && e.asset < (int32_t)st.scene.asset_count && st.have[e.asset]) {
        const content::Model &mdl = st.models[e.asset];
        const content::UploadedModel &up = st.ups[e.asset];
        content::ModelLod lod;
        lod.camera_pos = camera_eye(cam);
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
