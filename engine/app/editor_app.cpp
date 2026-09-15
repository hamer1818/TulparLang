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
#include "core/jobs/job_system.hpp"
#include "core/memory/arena.hpp"
#include "core/profiler/profiler.hpp"
#include "platform/time.hpp"
#include "rhi/device.hpp"
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
};
void record_cb(VkCommandBuffer cb, void *user) {
  auto *c = static_cast<RecordCtx *>(user);
  c->r->record(cb);
  c->r->ui_record(cb);
  c->ui->record(cb); // ImGui en ustte, ayni renk subpass'i
}
void shadow_cb(VkCommandBuffer cb, void *user) { static_cast<RecordCtx *>(user)->r->record_shadow(cb); }

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
  int selected = -1;
  bool playing = false, dirty = false;
  float play_time = 0;
  // Surukleme / metin duzenleme: aktiflesince kopya, birakinca tek islem.
  SceneEntity edit_before;
  bool edit_active = false, gizmo_was_using = false;
  bool prev_lmb = false;
  char status[160];
};

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
      if (st.hist.set_entity(st.scene, (uint32_t)index, after)) st.dirty = true;
    }
  }
}
// Ayrik widget (onay kutusu, secim): kopya uzerinde degisiklik, hemen islem.
bool commit(EditorState &st, int index, const SceneEntity &after) {
  if (!st.hist.set_entity(st.scene, (uint32_t)index, after)) return false;
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
  renderer::Renderer ren;
  renderer::RendererConfig rc;
  rc.srgb_target = headless ? true : swap.srgb_output();
  if (!ren.init(dev, sys, rp, rc)) { std::fprintf(stderr, "renderer\n"); return 1; }
  ren.set_render_size(width, height);

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
  for (uint32_t i = 0; i < st.scene.asset_count; i++) {
    st.have[i] = content::gltf_load(sys, asset(st.scene.assets[i]), &st.models[i]) && content::upload_model(ren, sys, st.models[i], &st.ups[i]);
    if (!st.have[i]) std::printf("[engine_editor] kaynak yuklenemedi: %s\n", st.scene.assets[i]);
  }
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
  ren.set_light(normalize(st.scene.sun_dir), st.scene.ambient, st.scene.sun_diffuse);
  ren.set_shadow_volume(st.scene.shadow_center, st.scene.shadow_radius, st.scene.shadow_depth);

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
  st.selected = headless ? 0 : -1;
  st.playing = headless;
  if (st.playing) bodies_spawn(st, phys);
  if (headless && st.scene.entity_count) {
    // Betikli durum: bir islem + geri al (gunluk kapali dongude calisiyor mu).
    SceneEntity e = st.scene.entities[0];
    e.pos.x += 1.0f;
    st.hist.set_entity(st.scene, 0, e);
    st.hist.undo(st.scene);
  }
  set_status(st, "yuklendi: %s", st.scene_path);
  int gizmo_op = 0; // 0 tasi, 1 dondur, 2 olcekle
  sim::FixedStep fs;
  uint64_t last_ns = platform::now_ns();
  uint32_t frame_i = 0, tick_i = 0;
  double prev_mx = 0, prev_my = 0;
  bool prev_rmb = false;
  RecordCtx rctx{&ren, &ui};
  bool running = true;
  auto set_playing = [&](bool p) {
    if (p == st.playing) return;
    st.playing = p;
    if (p) { st.play_time = 0; bodies_spawn(st, phys); }
    else bodies_remove(st, phys); // durdur: veri modeli (yazar donusumu) gecerli
  };
  auto do_undo = [&]() { with_bodies(st, phys, [&] { if (st.hist.undo(st.scene)) { st.dirty = true; set_status(st, "geri alindi (%u kaldi)", st.hist.undo_count()); } }); };
  auto do_redo = [&]() { with_bodies(st, phys, [&] { if (st.hist.redo(st.scene)) { st.dirty = true; set_status(st, "yinelendi (%u kaldi)", st.hist.redo_count()); } }); };
  auto do_save = [&]() {
    st.scene.cam_target = cam.target; st.scene.cam_yaw = cam.yaw; st.scene.cam_pitch = cam.pitch; st.scene.cam_radius = cam.radius;
    content::SceneError err{};
    if (content::scene_save(frame, st.scene, st.scene_path, &err)) { st.dirty = false; set_status(st, "kaydedildi: %s", st.scene_path); }
    else set_status(st, "KAYDEDILEMEDI: %s", err.msg);
  };
  auto do_add = [&]() {
    SceneEntity e{};
    if (st.selected >= 0) { e = st.scene.entities[st.selected]; std::snprintf(e.name, sizeof e.name, "%.24s_kopya", st.scene.entities[st.selected].name); e.pos.x += 1.5f; }
    else {
      std::snprintf(e.name, sizeof e.name, "nesne_%u", st.scene.entity_count + 1);
      e.pos = cam.target;
      if (st.scene.asset_count) { e.components = content::kSceneModel; e.asset = 0; }
    }
    with_bodies(st, phys, [&] {
      if (st.hist.add_entity(st.scene, e)) { st.selected = (int)st.scene.entity_count - 1; st.dirty = true; set_status(st, "eklendi: %s", e.name); }
      else set_status(st, "eklenemedi (kapasite %u)", content::kSceneMaxEntities);
    });
  };
  auto do_remove = [&]() {
    if (st.selected < 0) return;
    with_bodies(st, phys, [&] {
      if (st.hist.remove_entity(st.scene, (uint32_t)st.selected)) { st.dirty = true; set_status(st, "silindi"); }
    });
    if (st.selected >= (int)st.scene.entity_count) st.selected = (int)st.scene.entity_count - 1;
    if (st.selected < 0) st.selected = -1;
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

    const float aspect = (float)fw / (float)fh;
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
    }
    if (ImGui::BeginMainMenuBar()) {
      if (ImGui::Button(st.playing ? "Durdur" : "Oynat")) set_playing(!st.playing);
      if (ImGui::Button("Kaydet")) do_save();
      if (ImGui::Button("Geri al")) do_undo();
      if (ImGui::Button("Yinele")) do_redo();
      ImGui::Text("| %s%s | gunluk %u/%u | kare %u | tick %u | gizmo %s (T/R/S) | %s", st.dirty ? "*" : "",
                  st.selected >= 0 ? st.scene.entities[st.selected].name : "-", st.hist.undo_count(), st.hist.redo_count(), frame_i, tick_i,
                  gizmo_op == 0 ? "tasi" : gizmo_op == 1 ? "dondur" : "olcekle", st.status);
      ImGui::EndMainMenuBar();
    }
    ImGui::SetNextWindowPos(ImVec2(8, 30), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(230, 300), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Sahne")) {
      if (ImGui::Button("Ekle")) do_add();
      ImGui::SameLine();
      if (ImGui::Button("Sil")) do_remove();
      ImGui::Separator();
      for (uint32_t i = 0; i < st.scene.entity_count; i++) {
        ImGui::PushID((int)i);
        if (ImGui::Selectable(st.scene.entities[i].name, st.selected == (int)i)) st.selected = (int)i;
        ImGui::PopID();
      }
      ImGui::Separator();
      ImGui::Text("sim: %u entity", scene.entities());
      ImGui::Text("kamera %.1f/%.1f/%.1f", cam.eye().x, cam.eye().y, cam.eye().z);
    }
    ImGui::End();
    ImGui::SetNextWindowPos(ImVec2((float)fw - 300, 30), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(292, 360), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Ozellikler")) {
      if (st.selected >= 0 && st.selected < (int)st.scene.entity_count) {
        const int si = st.selected;
        SceneEntity &e = st.scene.entities[si];
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
    // Gizmo: ImGuizmo GL gelenegi (NDC y yukari) bekler; Vulkan projeksiyonun y'si tersken duzeltilir.
    // Surukleme tek islem: IsUsing baslarken kopya, bitince gunluge.
    if (st.selected >= 0 && st.selected < (int)st.scene.entity_count) {
      SceneEntity &e = st.scene.entities[st.selected];
      Mat4 proj_gl = proj;
      proj_gl.m[1][1] = -proj_gl.m[1][1];
      ImGuizmo::SetOrthographic(false);
      ImGuizmo::SetRect(0, 0, (float)fw, (float)fh);
      Mat4 mtx = content::scene_entity_matrix(e);
      const ImGuizmo::OPERATION op = gizmo_op == 0 ? ImGuizmo::TRANSLATE : gizmo_op == 1 ? ImGuizmo::ROTATE : ImGuizmo::SCALE;
      const bool changed = ImGuizmo::Manipulate(&view.m[0][0], &proj_gl.m[0][0], op, ImGuizmo::WORLD, &mtx.m[0][0]);
      const bool using_now = ImGuizmo::IsUsing();
      if (using_now && !st.gizmo_was_using) st.edit_before = e;
      if (changed) entity_from_matrix(e, mtx);
      if (!using_now && st.gizmo_was_using) {
        const SceneEntity after = e;
        e = st.edit_before;
        if (st.hist.set_entity(st.scene, (uint32_t)st.selected, after)) st.dirty = true;
      }
      st.gizmo_was_using = using_now;
    } else st.gizmo_was_using = false;
    // Tiklamayla secim: sol tus basildi (gecis), ImGui/gizmo uzerinde degil.
    {
      const bool lmb = in && in->mouse_down[0];
      const bool pressed = lmb && !st.prev_lmb;
      st.prev_lmb = lmb;
      if (pressed && !ui.wants_mouse() && !ImGuizmo::IsUsing() && !ImGuizmo::IsOver()) {
        static content::SceneBounds wb[content::kSceneMaxEntities];
        const uint32_t nb = entity_world_bounds(st, phys, wb);
        Vec3 o, d;
        camera_ray(cam, kPi / 3.5f, aspect, (float)in->mouse_x, (float)in->mouse_y, (float)fw, (float)fh, &o, &d);
        float t = 0;
        const int32_t hit = content::scene_pick(wb, nb, o, d, &t);
        st.selected = hit;
        if (hit >= 0) set_status(st, "secildi: %s (%.1f m)", st.scene.entities[hit].name, t);
        else set_status(st, "secim yok");
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
    }
    ui.end_frame();

    // --- 3B cizim: veri modelinden ---
    ren.begin_frame(headless ? 0 : frame_i);
    scene.draw(ren, ds);
    for (uint32_t i = 0; i < st.scene.entity_count; i++) {
      const SceneEntity &e = st.scene.entities[i];
      const bool simulated = st.playing && st.bodies_live && st.bodies[i].valid() && e.dynamic;
      const Mat4 m = simulated ? content::scene_body_matrix(e, phys, st.bodies[i]) : content::scene_entity_matrix(e);
      const bool sel = st.selected == (int)i;
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
    if (headless) {
      if (!rhi::offscreen_render_custom(off, oc, record_cb, &rctx, &ores, shadow_cb)) { std::fprintf(stderr, "kare: %s\n", ores.error); return 1; }
    } else {
      rhi::FrameContext fc;
      if (swap.acquire(&fc)) {
        ren.record_shadow(fc.cmd);
        swap.begin_render_pass(fc);
        ren.record(fc.cmd);
        ren.ui_record(fc.cmd);
        ui.record(fc.cmd);
        swap.end_frame(fc);
      }
      if (swap.needs_recreate() && fw && fh) { swap.recreate(fw, fh); ren.set_render_size(swap.extent().width, swap.extent().height); }
    }
    prof.end_frame();
    frame_i++;
    if (headless && frame_i >= opts.headless_frames) running = false;
  }
  static uint64_t scratch[600];
  FrameStats stt = prof.frame_stats(Span<uint64_t>(scratch, 600), 0);
  const EditorUiStats us = ui.stats();
  std::printf("[engine_editor] %u kare | p50 %.2f ms p99 %.2f ms | ui %u vertex %u indeks %u liste | cizim %u | nesne %u | gunluk %u/%u%s | secili %s | tick %u\n",
              frame_i, stt.p50_ns / 1e6, stt.p99_ns / 1e6, us.vertices, us.indices, us.draw_lists, ren.stats().draws, st.scene.entity_count,
              st.hist.undo_count(), st.hist.redo_count(), st.dirty ? " (kaydedilmedi)" : "",
              st.selected >= 0 ? st.scene.entities[st.selected].name : "-", tick_i);
  if (headless && opts.out_path) {
    if (rhi::write_ppm(opts.out_path, ores.pixels, oc.width, oc.height)) std::printf("[engine_editor] goruntu: %s\n", opts.out_path);
  }
  dev.api().vkDeviceWaitIdle(dev.handle());
  bodies_remove(st, phys);
  ui.shutdown();
  scene.shutdown();
  ren.shutdown();
  if (off) rhi::offscreen_destroy(off);
  if (!headless) swap.shutdown();
  dev.shutdown();
  jobs.shutdown();
  return 0;
}

} // namespace tulpar::engine::app
