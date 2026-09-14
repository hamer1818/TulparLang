#include "app/editor_app.hpp"

#include <cmath>
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

Mat4 entity_matrix(const EditorEntity &e) {
  float m[16];
  ImGuizmo::RecomposeMatrixFromComponents(e.pos, e.rot_deg, e.scale, m);
  Mat4 r;
  for (int c = 0; c < 4; c++) for (int rr = 0; rr < 4; rr++) r.m[c][rr] = m[c * 4 + rr];
  return r;
}
void entity_from_matrix(EditorEntity &e, const Mat4 &mat) {
  float m[16];
  for (int c = 0; c < 4; c++) for (int rr = 0; rr < 4; rr++) m[c * 4 + rr] = mat.m[c][rr];
  ImGuizmo::DecomposeMatrixToComponents(m, e.pos, e.rot_deg, e.scale);
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

  // Varliklar (LOD kuresi, iskeletli boru) — demo ile ayni test varliklari.
  char path[1024];
  const char *adir = std::getenv("TULPAR_ENGINE_ASSETS");
  auto asset = [&](const char *name) {
    if (adir && *adir) std::snprintf(path, sizeof path, "%s/%s", adir, name);
    else std::snprintf(path, sizeof path, "%s/tests/assets/%s", ENGINE_SOURCE_DIR, name);
    return path;
  };
  static content::Model cube_model, sphere_model, tube_model;
  static content::UploadedModel cube_up, sphere_up, tube_up;
  static content::PoseScratch pose_scratch;
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
  if (content::gltf_load(sys, asset("checker_cube.gltf"), &cube_model) && content::upload_model(ren, sys, cube_model, &cube_up) && cube_up.mesh_count) {
    ds.box_mesh = cube_up.meshes[0]; ds.box_mat = cube_up.materials[0];
  }
  const bool have_sphere = content::gltf_load(sys, asset("lod_sphere.gltf"), &sphere_model) && content::upload_model(ren, sys, sphere_model, &sphere_up);
  const bool have_tube = content::gltf_load(sys, asset("skin_tube.gltf"), &tube_model) && tube_model.clip_count &&
                         content::upload_model(ren, sys, tube_model, &tube_up);
  ren.set_light(normalize(Vec3{0.5f, 1.0f, 0.35f}), {0.16f, 0.17f, 0.2f}, 0.85f);
  ren.set_shadow_volume({0, 1.0f, -1.0f}, 17.0f, 70.0f);

  DemoScene scene;
  if (!scene.init(sys, &jobs)) { std::fprintf(stderr, "sahne\n"); return 1; }

  // Editor veri modeli (ilk dilim): 3 kure + 2 boru.
  static EditorEntity ents[5];
  int ent_count = 0;
  auto add_ent = [&](const char *name, int kind, float x, float y, float z, float phase) {
    EditorEntity &e = ents[ent_count++];
    std::snprintf(e.name, sizeof e.name, "%s", name);
    e.pos[0] = x; e.pos[1] = y; e.pos[2] = z;
    e.rot_deg[0] = e.rot_deg[1] = e.rot_deg[2] = 0;
    e.scale[0] = e.scale[1] = e.scale[2] = kind == 1 ? 1.2f : 1.0f;
    e.kind = kind; e.phase = phase;
  };
  add_ent("kure_1", 0, -8.0f, 1.2f, -8.5f, 0);
  add_ent("kure_2", 0, 0.0f, 1.2f, -8.5f, 0);
  add_ent("kure_3", 0, 8.0f, 1.2f, -8.5f, 0);
  add_ent("boru_1", 1, -4.0f, 0.0f, -3.5f, 0.0f);
  add_ent("boru_2", 1, 4.0f, 0.0f, -3.5f, 0.35f);

  EditorUi ui;
  {
    char fpath[1024];
    std::snprintf(fpath, sizeof fpath, "%s/assets/fonts/DejaVuSans.ttf", ENGINE_SOURCE_DIR);
    if (!ui.init(dev, rp, 1, image_count, fpath, 17.0f)) { std::fprintf(stderr, "editor ui: %s\n", ui.last_error()); return 1; }
  }

  Cam cam;
  int selected = headless ? 0 : -1;
  bool playing = headless;
  int gizmo_op = 0; // 0 tasi, 1 dondur, 2 olcekle
  sim::FixedStep fs;
  uint64_t last_ns = platform::now_ns();
  uint32_t frame_i = 0, tick_i = 0;
  double prev_mx = 0, prev_my = 0;
  bool prev_rmb = false;
  RecordCtx rctx{&ren, &ui};
  bool running = true;
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
      if (in->key_down[GLFW_KEY_S]) gizmo_op = 2;
    }

    // Sim: yalniz oynatilirken (sabit adim).
    if (playing) {
      ENGINE_ZONE("sim");
      uint32_t ticks = fs.advance(dt);
      for (uint32_t t = 0; t < ticks; t++) scene.tick(fs.step_s, tick_i++);
    }

    const float aspect = (float)fw / (float)fh;
    Mat4 proj = Mat4::perspective(kPi / 3.5f, aspect, 0.1f, 200.0f);
    Mat4 view = cam.view();
    ren.set_camera(view, proj);
    ren.clear_point_lights();

    // --- ImGui paneller ---
    ui.begin_frame(in, (float)fw, (float)fh, dt);
    if (ImGui::BeginMainMenuBar()) {
      if (ImGui::Button(playing ? "Durdur" : "Oynat")) playing = !playing;
      ImGui::Text("| kare %u | tick %u | secili %s | gizmo %s (T/R/S)", frame_i, tick_i, selected >= 0 ? ents[selected].name : "-",
                  gizmo_op == 0 ? "tasi" : gizmo_op == 1 ? "dondur" : "olcekle");
      ImGui::EndMainMenuBar();
    }
    ImGui::SetNextWindowPos(ImVec2(8, 30), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(220, 240), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Sahne")) {
      for (int i = 0; i < ent_count; i++)
        if (ImGui::Selectable(ents[i].name, selected == i)) selected = i;
      ImGui::Separator();
      ImGui::Text("sim: %u entity", scene.entities());
      ImGui::Text("kamera %.1f/%.1f/%.1f", cam.eye().x, cam.eye().y, cam.eye().z);
    }
    ImGui::End();
    ImGui::SetNextWindowPos(ImVec2((float)fw - 268, 30), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(260, 200), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Ozellikler")) {
      if (selected >= 0) {
        EditorEntity &e = ents[selected];
        ImGui::InputText("ad", e.name, sizeof e.name);
        ImGui::DragFloat3("konum", e.pos, 0.05f);
        ImGui::DragFloat3("donus", e.rot_deg, 0.5f);
        ImGui::DragFloat3("olcek", e.scale, 0.02f, 0.05f, 20.0f);
        ImGui::Text("tur: %s", e.kind == 0 ? "LOD kuresi" : "iskeletli boru");
        if (e.kind == 1) ImGui::DragFloat("faz", &e.phase, 0.01f, 0.0f, 2.0f);
      } else ImGui::TextUnformatted("Sahne listesinden sec");
    }
    ImGui::End();
    // Gizmo: ImGuizmo GL gelenegi (NDC y yukari) bekler; Vulkan projeksiyonun y'si tersken duzeltilir.
    if (selected >= 0) {
      Mat4 proj_gl = proj;
      proj_gl.m[1][1] = -proj_gl.m[1][1];
      ImGuizmo::SetOrthographic(false);
      ImGuizmo::SetRect(0, 0, (float)fw, (float)fh);
      Mat4 mtx = entity_matrix(ents[selected]);
      const ImGuizmo::OPERATION op = gizmo_op == 0 ? ImGuizmo::TRANSLATE : gizmo_op == 1 ? ImGuizmo::ROTATE : ImGuizmo::SCALE;
      if (ImGuizmo::Manipulate(&view.m[0][0], &proj_gl.m[0][0], op, ImGuizmo::WORLD, &mtx.m[0][0])) entity_from_matrix(ents[selected], mtx);
    }
    ui.end_frame();

    // --- 3B cizim ---
    ren.begin_frame(headless ? 0 : frame_i);
    scene.draw(ren, ds);
    for (int i = 0; i < ent_count; i++) {
      const EditorEntity &e = ents[i];
      const Mat4 m = entity_matrix(e);
      if (e.kind == 0 && have_sphere) {
        content::ModelLod lod;
        lod.camera_pos = cam.eye();
        lod.distance1 = 24.0f; lod.distance2 = 34.0f;
        content::draw_model(ren, sphere_model, sphere_up, m, selected == i ? Vec3{1.0f, 0.9f, 0.4f} : Vec3{0.85f, 0.9f, 1.0f}, &lod);
      } else if (e.kind == 1 && have_tube) {
        const float dur = tube_model.clips[0].duration;
        float t = std::fmod((playing ? (float)frame_i / 60.0f : 0.0f) + e.phase, 2.0f * dur);
        if (t > dur) t = 2.0f * dur - t;
        content::ModelPose pose;
        if (content::model_pose_evaluate(tube_model, 0, t, pose_scratch, &pose))
          content::draw_model(ren, tube_model, tube_up, m, selected == i ? Vec3{1.0f, 0.9f, 0.4f} : Vec3{1.0f, 0.75f, 0.35f}, nullptr,
                              nullptr, &pose);
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
  FrameStats st = prof.frame_stats(Span<uint64_t>(scratch, 600), 0);
  const EditorUiStats us = ui.stats();
  std::printf("[engine_editor] %u kare | p50 %.2f ms p99 %.2f ms | ui %u vertex %u indeks %u liste | cizim %u | secili %s | tick %u\n", frame_i,
              st.p50_ns / 1e6, st.p99_ns / 1e6, us.vertices, us.indices, us.draw_lists, ren.stats().draws,
              selected >= 0 ? ents[selected].name : "-", tick_i);
  if (headless && opts.out_path) {
    if (rhi::write_ppm(opts.out_path, ores.pixels, oc.width, oc.height)) std::printf("[engine_editor] goruntu: %s\n", opts.out_path);
  }
  dev.api().vkDeviceWaitIdle(dev.handle());
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
