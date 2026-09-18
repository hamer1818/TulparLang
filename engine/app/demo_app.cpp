#include "app/demo_app.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "app/demo_scene.hpp"
#include "audio/clip.hpp"
#include "audio/device.hpp"
#include "app/virtual_stick.hpp"
#include "content/font.hpp"
#include "content/gltf.hpp"
#include "content/scene_blob.hpp"
#include "content/scene_runtime.hpp"
#include "core/jobs/job_system.hpp"
#include "core/memory/alloc_gate.hpp"
#include "core/memory/arena.hpp"
#include "core/profiler/profiler.hpp"
#include "platform/crash.hpp"
#include "platform/time.hpp"
#include "renderer/renderer.hpp"
#include "rhi/device.hpp"
#include "rhi/offscreen.hpp"
#include "rhi/swapchain.hpp"
#include "rhi/vk_api.hpp"
#include "sim/camera_rig.hpp"
#include "sim/schedule.hpp"

namespace tulpar::engine::app {

namespace {
struct Cam {
  float angle = 0.6f;
  float radius = 24.0f;
  float height = 13.0f;
  Vec3 target{0, 0.5f, -1}; // yorunge merkezi (oyuncu varsa oyuncu)
};
Vec3 cam_eye(const Cam &c) {
  return {c.target.x + std::sin(c.angle) * c.radius, c.target.y + c.height, c.target.z + std::cos(c.angle) * c.radius};
}
Mat4 cam_view(const Cam &c) { return Mat4::look_at(cam_eye(c), c.target, {0, 1, 0}); }
struct RecordCtx {
  renderer::Renderer *r;
};
void record_cb(VkCommandBuffer cb, void *user) {
  auto *c = static_cast<RecordCtx *>(user);
  c->r->record(cb);
  c->r->ui_record(cb); // HUD ayni subpass'te, 3B'den sonra
}
// HUD: ust solda durum, etkilesimli ise joystick gostergesi.
void draw_hud(renderer::Renderer &ren, const content::Font &font, float w, float h, float rot, float fps, float ms,
              uint32_t lights, const VirtualStick *stick, Vec3 player, bool interactive) {
  ren.ui_begin(w, h, rot); // w,h: GORUNEN olcu (dokunmatik koordinatlarla ayni uzay)
  if (!font.loaded()) return;
  const float s = h / 1080.0f; // olcek
  const uint32_t white = renderer::Renderer::rgba(255, 255, 255), dim = renderer::Renderer::rgba(0, 0, 0, 120);
  char line[160];
  std::snprintf(line, sizeof line, "Tulpar Engine  %.0f fps  %.2f ms  isik %u", fps, ms, lights);
  float tw = font.text_width(line);
  ren.ui_set_atlas(font.atlas());
  ren.ui_rect(16 * s, 16 * s, tw + 24 * s, font.line_height() + 12 * s, dim);
  font.draw(ren, 28 * s, 22 * s, line, white);
  if (interactive) {
    std::snprintf(line, sizeof line, "oyuncu %.1f, %.1f   sol: yuru  sag: bak / dokun: zipla", player.x, player.z);
    ren.ui_rect(16 * s, 16 * s + font.line_height() + 20 * s, font.text_width(line) + 24 * s, font.line_height() + 12 * s, dim);
    font.draw(ren, 28 * s, 22 * s + font.line_height() + 20 * s, line, renderer::Renderer::rgba(220, 230, 255));
    if (stick && stick->move_active) { // joystick: kok halkasi + topuz
      float r = stick->stick_radius_px;
      ren.ui_rect(stick->stick_origin.x - r, stick->stick_origin.y - r, 2 * r, 2 * r, renderer::Renderer::rgba(255, 255, 255, 40));
      float kx = stick->stick_origin.x + stick->move.x * r, ky = stick->stick_origin.y - stick->move.y * r;
      ren.ui_rect(kx - 24 * s, ky - 24 * s, 48 * s, 48 * s, renderer::Renderer::rgba(255, 255, 255, 170));
    }
  }
}
void shadow_cb(VkCommandBuffer cb, void *user) { static_cast<RecordCtx *>(user)->r->record_shadow(cb); }
VkPresentModeKHR present_mode_of(const char *s) {
  if (!s || !*s) return VK_PRESENT_MODE_FIFO_KHR;
  if (!std::strcmp(s, "mailbox")) return VK_PRESENT_MODE_MAILBOX_KHR;
  if (!std::strcmp(s, "immediate")) return VK_PRESENT_MODE_IMMEDIATE_KHR;
  return VK_PRESENT_MODE_FIFO_KHR;
}
const char *present_name(VkPresentModeKHR m) {
  switch (m) {
  case VK_PRESENT_MODE_MAILBOX_KHR: return "MAILBOX";
  case VK_PRESENT_MODE_IMMEDIATE_KHR: return "IMMEDIATE (vsync kilidi yok)";
  default: return "FIFO (vsync)";
  }
}
} // namespace

int demo_run(const DemoOptions &opts, const DemoHost *host) {
  const bool headless = opts.headless_frames > 0 || host == nullptr;
  const uint32_t headless_frames = headless ? (opts.headless_frames ? opts.headless_frames : 300) : 0;

  SystemArena sys;
  if (!sys.reserve(256u << 20, "system")) { std::fprintf(stderr, "arena\n"); return 1; }
  FrameArena frame;
  sys.carve(frame, 8u << 20, "frame");
  JobSystem jobs;
  if (!jobs.init(sys, JobSystemConfig{})) { std::fprintf(stderr, "job sistemi\n"); return 1; }
  Profiler prof;
  ProfilerConfig pc;
  pc.frame_capacity = 600;
  pc.zone_capacity = 32768;
  prof.init(sys, pc);
  prof.watch_arena(&sys);
  prof.watch_arena(&frame);

  rhi::VkApi api;
  if (!rhi::vk_api_load(api)) { std::fprintf(stderr, "Vulkan loader yok\n"); return 1; }
  rhi::DeviceConfig dc;
  dc.prefer = opts.gpu_prefer ? opts.gpu_prefer : "";
  dc.validation = opts.validation;
  dc.best_practices = opts.validation; // dogrulama acikken Mali linter de acik (rapor sonda)
  dc.optional_device_extensions = opts.device_extensions;
  dc.optional_device_extension_count = opts.device_extension_count;
  if (!headless) {
    uint32_t n = 0;
    dc.instance_extensions = host->instance_extensions(host->user, &n);
    dc.instance_extension_count = n;
  }
  rhi::Device dev;
  if (!dev.init_instance(api, dc)) { std::fprintf(stderr, "instance: %s\n", dev.last_error()); return 1; }
  VkSurfaceKHR surface = VK_NULL_HANDLE;
  if (!headless && !host->create_surface(host->user, api, dev.instance(), &surface)) {
    std::fprintf(stderr, "yuzey olusturulamadi\n");
    return 1;
  }
  if (!dev.init_device(surface)) { std::fprintf(stderr, "cihaz: %s\n", dev.last_error()); return 1; }
  const rhi::DeviceCaps &caps = dev.caps();
  std::printf("[engine_demo] GPU: %s (Vulkan %u.%u) worker=%u lazily_allocated=%d timestamps=%d gpl=%d merge_feedback=%d\n",
              caps.device_name, VK_API_VERSION_MAJOR(caps.api_version), VK_API_VERSION_MINOR(caps.api_version),
              jobs.worker_count(), (int)caps.lazily_allocated_memory, (int)caps.timestamps,
              (int)caps.graphics_pipeline_library, (int)caps.ext_subpass_merge_feedback);
  for (uint32_t i = 0; i < opts.device_extension_count && i < rhi::DeviceCaps::kMaxOptionalExtensions; i++)
    std::printf("[engine_demo] istege bagli uzanti %s: %s\n", opts.device_extensions[i], dev.caps().optional_extension_enabled[i] ? "ACIK" : "yok");

  uint32_t width = opts.width, height = opts.height;
  rhi::Swapchain swap;
  rhi::SwapchainConfig swap_cfg;
  swap_cfg.prerotate = opts.prerotate;
  swap_cfg.preferred_present_mode = present_mode_of(opts.present_mode);
  swap_cfg.hooks = opts.swap_hooks;
  rhi::OffscreenTarget *off = nullptr;
  rhi::OffscreenConfig oc;
  rhi::OffscreenResult ores;
  VkRenderPass rp = VK_NULL_HANDLE;
  uint32_t render_w = width, render_h = height;
  if (headless) {
    oc.width = width;
    oc.height = height;
    oc.srgb = true; // ekranla ayni: dogrusal aydinlatma, kodlu cikti
    off = rhi::offscreen_create(dev, sys, oc, &ores);
    if (!off) { std::fprintf(stderr, "offscreen: %s\n", ores.error); return 1; }
    rp = rhi::offscreen_render_pass(off);
    render_w = width; render_h = height;
  } else {
    uint32_t fw = 0, fh = 0;
    if (host->poll(host->user, &fw, &fh) == HostPoll::Quit) return 0;
    if (!swap.init(dev, sys, surface, fw, fh, swap_cfg)) { std::fprintf(stderr, "swapchain\n"); return 1; }
    rp = swap.render_pass();
    render_w = swap.extent().width; render_h = swap.extent().height;
    std::printf("[engine_demo] swapchain goruntu %ux%u, gorunen %ux%u, on-dondurme %.0f derece, sunum %s\n",
                swap.extent().width, swap.extent().height, swap.logical_extent().width, swap.logical_extent().height,
                swap.rotation_radians() * 180.0f / kPi, present_name(swap.present_mode()));
  }
  renderer::Renderer ren;
  renderer::RendererConfig rc;
  rc.srgb_target = headless ? true : swap.srgb_output(); // UNORM yuzeyde shader kodlar
  if (!ren.init(dev, sys, rp, rc)) { std::fprintf(stderr, "renderer\n"); return 1; }
  std::printf("[engine_demo] renk: dogrusal aydinlatma, hedef %s\n", rc.srgb_target ? "sRGB bicim (donanim kodlar)" : "UNORM (shader kodlar)");
  ren.set_render_size(render_w, render_h);
  renderer::Vertex v[24];
  uint32_t idx[36];
  uint32_t n = renderer::Renderer::cube(v, idx);
  DemoScene::DrawSet ds;
  ds.cube = ren.create_mesh(v, 24, idx, n);
  n = renderer::Renderer::plane(v, idx, 10.0f); // 20 m zeminde 10 dama tekrari
  ds.plane = ren.create_mesh(v, 4, idx, n);
  { // Zemin damasi: yordamsal 64x64, iki gri ton (dosya gerekmez, cihazda da var)
    static uint8_t px[64 * 64 * 4];
    for (uint32_t y = 0; y < 64; y++)
      for (uint32_t x = 0; x < 64; x++) {
        bool on = ((x / 32) + (y / 32)) % 2 == 0;
        uint8_t g = on ? 205 : 150;
        uint8_t *p = px + (y * 64 + x) * 4;
        p[0] = g; p[1] = g; p[2] = g + 8; p[3] = 255;
      }
    ds.ground = ren.create_material(ren.create_texture(px, 64, 64, true), {1, 1, 1});
  }
  static content::Model sphere_model;
  static content::UploadedModel sphere_up;
  static content::Model tube_model; // iskeletli boru (glTF skin + "bend" klibi), GPU skinning
  static content::UploadedModel tube_up;
  static content::PoseScratch pose_scratch;
  bool have_sphere = false, have_tube = false;
  uint32_t lod_counts[content::kModelMaxLods + 1] = {};
  { // glTF kup (tests/assets ya da TULPAR_ENGINE_ASSETS): kutular bununla cizilir
    char path[1024];
    const char *adir = std::getenv("TULPAR_ENGINE_ASSETS");
    if (adir && *adir) std::snprintf(path, sizeof path, "%s/checker_cube.gltf", adir);
    else std::snprintf(path, sizeof path, "%s/tests/assets/checker_cube.gltf", ENGINE_SOURCE_DIR);
    static content::Model model;
    static content::UploadedModel up;
    if (content::gltf_load(sys, path, &model) && content::upload_model(ren, sys, model, &up) && up.mesh_count && up.material_count) {
      ds.box_mesh = up.meshes[0];
      ds.box_mat = up.materials[0];
      std::printf("[engine_demo] glTF: %s (%u mesh, %u doku)\n", path, up.mesh_count, up.texture_count);
    } else {
      std::printf("[engine_demo] glTF yok (%s): kutular duz — %s\n", path, model.error);
    }
    // LOD kuresi (meshoptimizer): uc kure, kameraya uzakliga gore LOD0/1/2.
    if (adir && *adir) std::snprintf(path, sizeof path, "%s/lod_sphere.gltf", adir);
    else std::snprintf(path, sizeof path, "%s/tests/assets/lod_sphere.gltf", ENGINE_SOURCE_DIR);
    if (content::gltf_load(sys, path, &sphere_model) && content::upload_model(ren, sys, sphere_model, &sphere_up) && sphere_up.mesh_count) {
      have_sphere = true;
      const content::ModelMesh &mm = sphere_model.meshes[0];
      std::printf("[engine_demo] LOD kuresi: %u ucgen -> %u -> %u (hata %.3f/%.3f), ACMR %.2f -> %.2f\n", mm.index_count / 3,
                  mm.lod_index_count[0] / 3, mm.lod_index_count[1] / 3, mm.lod_error[0], mm.lod_error[1], sphere_model.opt.acmr_before,
                  sphere_model.opt.acmr_after);
    }
    // Iskeletli boru: 4 boru farkli fazda "bend" klibini oynar (kare indeksinden, belirlenimli).
    if (adir && *adir) std::snprintf(path, sizeof path, "%s/skin_tube.gltf", adir);
    else std::snprintf(path, sizeof path, "%s/tests/assets/skin_tube.gltf", ENGINE_SOURCE_DIR);
    if (content::gltf_load(sys, path, &tube_model) && tube_model.clip_count && content::upload_model(ren, sys, tube_model, &tube_up)) {
      have_tube = true;
      std::printf("[engine_demo] iskeletli boru: %u eklem, klip '%s' %.2f s (%zu -> %zu bayt)\n", tube_model.skins[0].joint_count,
                  tube_model.clips[0].name, tube_model.clips[0].duration, tube_model.clips[0].stats.raw_bytes,
                  tube_model.clips[0].stats.compressed_bytes);
    }
  }
  auto draw_lod_spheres = [&](const Cam &c) {
    if (!have_sphere) return;
    content::ModelLod lod;
    lod.camera_pos = cam_eye(c);
    lod.distance1 = 24.0f;
    lod.distance2 = 34.0f;
    for (uint32_t k = 0; k <= content::kModelMaxLods; k++) lod_counts[k] = 0;
    for (int k = 0; k < 3; k++)
      content::draw_model(ren, sphere_model, sphere_up, Mat4::translate({-8.0f + 8.0f * (float)k, 1.2f, -8.5f}), {0.85f, 0.9f, 1.0f},
                          &lod, lod_counts);
  };
  auto draw_skinned_tubes = [&](uint32_t frame_index) {
    if (!have_tube) return;
    const float dur = tube_model.clips[0].duration;
    for (int k = 0; k < 4; k++) {
      // Ileri-geri: 0..dur..0 (ucgen dalga), boru basina faz.
      float t = std::fmod((float)frame_index / 60.0f + (float)k * 0.35f, 2.0f * dur);
      if (t > dur) t = 2.0f * dur - t;
      content::ModelPose pose;
      if (!content::model_pose_evaluate(tube_model, 0, t, pose_scratch, &pose)) return;
      content::draw_model(ren, tube_model, tube_up, Mat4::translate({-6.0f + 4.0f * (float)k, 0.0f, -3.5f}) * Mat4::scale({1.2f, 1.2f, 1.2f}),
                          {1.0f, 0.75f, 0.35f}, nullptr, nullptr, &pose);
    }
  };
  ren.set_light(normalize(Vec3{0.5f, 1.0f, 0.35f}), {0.16f, 0.17f, 0.2f}, 0.85f);
  // Golge kutusu sahneyi kapsamali: arena 20x20, duvar 3 m, kutular ~5 m'ye kadar.
  ren.set_shadow_volume({0, 1.0f, -1.0f}, 17.0f, 70.0f);
  renderer::ShadowInfo sh = ren.shadow();
  std::printf("[engine_demo] golge: %s %ux%u format=%d dogrusal_suzme=%d%s\n", sh.enabled ? "acik" : "KAPALI", sh.size,
              sh.size, (int)sh.format, (int)sh.linear_filter, sh.enabled ? "" : sh.disabled_reason);

  static content::Font font;
  {
    char fpath[1024];
    const char *adir = std::getenv("TULPAR_ENGINE_ASSETS");
    if (adir && *adir) std::snprintf(fpath, sizeof fpath, "%s/DejaVuSans.ttf", adir);
    else std::snprintf(fpath, sizeof fpath, "%s/assets/fonts/DejaVuSans.ttf", ENGINE_SOURCE_DIR);
    const float vis_h = headless ? (float)render_h : (float)swap.logical_extent().height;
    const float ui_px = vis_h / 1080.0f * 28.0f; // GORUNEN yukseklige gore (on-dondurmede goruntu dikey)
    if (font.load(sys, ren, fpath, ui_px > 12 ? ui_px : 12)) std::printf("[engine_demo] font: %s (%.0f px)\n", fpath, font.height());
    else std::printf("[engine_demo] font yok (%s): HUD metinsiz\n", fpath);
  }
  // Ses (istege bagli): 440 Hz sinus dongude; cihaz/arka uc ve callback sayisi raporda.
  audio::Mixer mixer;
  audio::AudioDevice adev;
  audio::Clip tone;
  if (opts.audio) {
    audio::DeviceConfig acfg;
    if (adev.init(mixer, acfg)) {
      if (audio::clip_sine(sys, 440.0f, 1.0f, mixer.rate(), 0.2f, &tone)) mixer.play(&tone, 1.0f, true);
      std::printf("[engine_demo] ses: %s '%s' %u Hz %u kanal periyot %u kare\n", adev.info().backend, adev.info().name,
                  adev.info().sample_rate, adev.info().channels, adev.info().period_frames);
    } else std::printf("[engine_demo] ses cihazi acilamadi: %s\n", adev.last_error());
  }
  DemoScene scene;
  if (!scene.init(sys, &jobs)) { std::fprintf(stderr, "sahne\n"); return 1; }
  std::printf("[engine_demo] sahne: %u entity, kutu+ajan+eklem\n", scene.entities());
  // Derlenmis sahne (istege bagli): blob oldugu gibi belleğe, tablolar dogrudan renderer/fizige.
  static content::SceneRuntime srt;
  bool have_blob = false;
  if (opts.scene_blob && *opts.scene_blob) {
    content::SceneBlobView bv;
    content::SceneError berr{};
    char bdir[1024];
    content::scene_dir_of(opts.scene_blob, bdir, sizeof bdir);
    if (!content::scene_blob_load(sys, opts.scene_blob, &bv, &berr)) { std::fprintf(stderr, "sahne blob %s: %s\n", opts.scene_blob, berr.msg); return 1; }
    if (!srt.init(sys, ren, bv, bdir)) { std::fprintf(stderr, "sahne runtime kurulamadi\n"); return 1; }
    srt.apply_world(ren); // gunes/ortam/golge hacmi blob'dan (kod icindeki sabitlerin yerine)
    const uint32_t nb = srt.spawn(scene.physics());
    have_blob = true;
    std::printf("[engine_demo] sahne blob: %s — %u varlik, %u cizim, %u isik, %u govde (%u fizige), kaynak %u/%u, ozet %016llx\n", opts.scene_blob,
                bv.h->entity_count, bv.h->draw_count, bv.h->light_count, bv.h->body_count, nb, srt.stats().assets_loaded, bv.h->asset_count,
                (unsigned long long)bv.hash());
  }

  sim::FixedStep fs;
  Cam cam;
  VirtualStick stick;
  const bool interactive = !headless && host && (host->touch || host->keyboard_move);
  if (interactive) { cam.radius = 9.0f; cam.height = 5.0f; }
  uint64_t last_ns = platform::now_ns(), report_ns = last_ns, start_ns = last_ns;
  uint32_t frame_i = 0, tick_i = 0;
  uint64_t frame_allocs_max = 0;
  uint64_t sim_ns = 0, render_ns = 0, acquire_ns = 0, ubo_ns = 0, draw_ns = 0, record_ns = 0, submit_ns = 0;
  float hud_fps = 0, hud_ms = 0;
  uint32_t report_frames = 0;
  bool running = true;
  bool have_window = true;
  while (running) {
    ENGINE_ZONE("frame");
    uint64_t now = platform::now_ns();
    float dt = (float)((now - last_ns) / 1e9);
    last_ns = now;
    if (dt > 0.25f) dt = 0.25f;
    if (headless) dt = 1.0f / 60.0f;
    AllocGate::begin_frame();
    prof.begin_frame();
    frame.begin_frame();
    uint32_t fw = 0, fh = 0;
    if (!headless) {
      switch (host->poll(host->user, &fw, &fh)) {
      case HostPoll::Quit: running = false; break;
      case HostPoll::NoWindow: have_window = false; break;
      case HostPoll::WindowChanged: {
        // Android: yeni ANativeWindow → eski swapchain + yuzey gider, yenisi gelir.
        dev.api().vkDeviceWaitIdle(dev.handle());
        swap.shutdown();
        VkSurfaceKHR ns = VK_NULL_HANDLE;
        if (!host->create_surface(host->user, api, dev.instance(), &ns)) { std::fprintf(stderr, "yuzey (yeniden)\n"); return 1; }
        dev.replace_surface(ns);
        if (!swap.init(dev, sys, ns, fw, fh, swap_cfg)) { std::fprintf(stderr, "swapchain (yeniden)\n"); return 1; }
        ren.set_render_size(swap.extent().width, swap.extent().height);
        std::printf("[engine_demo] pencere yeniden: swapchain %ux%u\n", swap.extent().width, swap.extent().height);
        have_window = true;
        break;
      }
      case HostPoll::Run: have_window = true; break;
      }
      // --- PENCERE OLCUSU -> SWAPCHAIN, KAYITTAN ONCE ------------------------
      // Tam ekran / yeniden boyutlandirma BURADA yakalanir. Yalniz
      // `needs_recreate()` (OUT_OF_DATE) beklemek tasinabilir degil: Wayland'de
      // yuzey olcusunu uygulama surer ve OUT_OF_DATE HIC gelmez — swapchain eski
      // olcude kalir, kompozitor gerer (rhi/swapchain.hpp ResizeAction).
      if (running && have_window && swap.sync_size(fw, fh)) {
        render_w = swap.extent().width;
        render_h = swap.extent().height;
        ren.set_render_size(render_w, render_h);
        std::printf("[engine_demo] swapchain %ux%u (%s)\n", render_w, render_h, swap.last_resize_reason());
      }
    }
    uint64_t t0 = platform::now_ns();
    // Girdi -> oyuncu komutu (kameraya gore) + kamera yorungesi
    if (interactive) {
      float mx = 0, my = 0;
      bool jump = false;
      if (host->touch) {
        if (const platform::TouchState *ts = host->touch(host->user)) {
          stick.update(*ts, now);
          mx = stick.move.x; my = stick.move.y;
          jump = stick.action;
          cam.angle -= stick.look_delta.x * 0.006f;
        }
      }
      if (host->keyboard_move) {
        float kx = 0, ky = 0; bool kj = false;
        host->keyboard_move(host->user, &kx, &ky, &kj);
        if (kx != 0 || ky != 0) { mx = kx; my = ky; }
        jump = jump || kj;
      }
      // Kameranin baktigi yon: hedef - goz (xz)
      float fx = -std::sin(cam.angle), fz = -std::cos(cam.angle);
      float rx = fz, rz = -fx; // sag = ileri x yukari
      scene.set_player_command({rx * mx + fx * my, rz * mx + fz * my}, jump);
    }
    {
      ENGINE_ZONE("sim");
      uint32_t ticks = fs.advance(dt);
      for (uint32_t t = 0; t < ticks; t++) scene.tick(fs.step_s, tick_i++);
    }
    uint64_t t1 = platform::now_ns();
    sim_ns += t1 - t0;
    if (interactive) {
      // sim/camera_rig.hpp: DEVAM_PLANI.md Faz B madde 7'nin ("kamera sistemi
      // -- cogu projenin GERCEKTE takildigi yer") en temel parcasi -- oyuncu
      // pozisyonuna ANINDA ATLAMAK yerine kare-hizindan bagimsiz ussel
      // yumusatmayla YAKLASIR (0.15s yari-omur -- FollowCamera'nin kendi
      // varsayilaniyla AYNI). Carpisma-farkindalik BILINCLI KAPSAM DISI
      // (camera_rig.hpp'nin kendi basligindaki NOT: fizik-raycast entegrasyonu
      // gerektirir, ayri bir bahis -- burasi SADECE onun uzerine oturacagi temel).
      Vec3 p = scene.player_position();
      cam.target = sim::exponential_smooth(cam.target, Vec3{p.x, p.y + 0.6f, p.z}, 0.15f, dt);
    } else cam.angle += dt * 0.15f;
    if (running && (headless || have_window)) {
      ENGINE_ZONE("render");
      // En-boy orani GORUNEN yonden; on-dondurmede projeksiyon clip uzayinda dondurulur.
      float aspect = headless ? (float)width / (float)height
                              : (float)swap.logical_extent().width / (float)swap.logical_extent().height;
      Mat4 proj = Mat4::perspective(kPi / 3.5f, aspect, 0.1f, 200.0f);
      if (!headless && swap.rotation_radians() != 0.0f) proj = Mat4::rotate({0, 0, 1}, swap.rotation_radians()) * proj;
      ren.set_camera(cam_view(cam), proj);
      ren.set_shadow_focus(cam.target); // yakin golge kademeleri oyuncunun etrafinda
      // 8 renkli nokta isik kutularin uzerinde doner (ilk oyun 8-16 dinamik isik ister).
      ren.clear_point_lights();
      for (uint32_t li = 0; li < 8; li++) {
        float a = cam.angle * 2.0f + (float)li * (kPi * 0.25f);
        Vec3 hue = {0.5f + 0.5f * std::sin(a), 0.5f + 0.5f * std::sin(a + 2.1f), 0.5f + 0.5f * std::sin(a + 4.2f)};
        ren.add_point_light(renderer::PointLight{{5.0f + 3.5f * std::cos(a), 1.6f, -4.0f + 3.5f * std::sin(a)}, 4.0f, hue, 5.0f});
      }
      if (headless) {
        ren.begin_frame(frame_i);
        scene.draw(ren, ds);
        if (have_blob) { srt.draw(ren, cam_eye(cam), (float)frame_i / 60.0f, &scene.physics()); for (uint32_t k = 0; k <= content::kModelMaxLods; k++) lod_counts[k] = srt.stats().lod[k]; }
        else { draw_lod_spheres(cam); draw_skinned_tubes(frame_i); }
        draw_hud(ren, font, (float)render_w, (float)render_h, 0.0f, hud_fps, hud_ms, ren.point_light_count(), nullptr,
                 scene.player_position(), false);
        RecordCtx rctx{&ren};
        // Golge gecisi ana render pass'ten ONCE (kendi pass'i var).
        if (!rhi::offscreen_render_custom(off, oc, record_cb, &rctx, &ores, shadow_cb)) {
          std::fprintf(stderr, "kare: %s\n", ores.error);
          return 1;
        }
      } else {
        // Kare yuvasi swapchain'den (fence beklenmis yuva) — Tuzaklar 8l.
        rhi::FrameContext fc;
        uint64_t ta = platform::now_ns();
        bool ok = swap.acquire(&fc);
        acquire_ns += platform::now_ns() - ta;
        if (ok) {
          uint64_t tb = platform::now_ns();
          ren.begin_frame(fc.frame_index);
          uint64_t tc = platform::now_ns();
          scene.draw(ren, ds);
          if (have_blob) { srt.draw(ren, cam_eye(cam), (float)frame_i / 60.0f, &scene.physics()); for (uint32_t k = 0; k <= content::kModelMaxLods; k++) lod_counts[k] = srt.stats().lod[k]; }
          else { draw_lod_spheres(cam); draw_skinned_tubes(frame_i); }
          draw_hud(ren, font, (float)swap.logical_extent().width, (float)swap.logical_extent().height, swap.rotation_radians(),
                   hud_fps, hud_ms, ren.point_light_count(), interactive ? &stick : nullptr, scene.player_position(), interactive);
          uint64_t td = platform::now_ns();
          ren.record_shadow(fc.cmd); // kendi pass'i: ana pass BASLAMADAN once
          swap.begin_render_pass(fc);
          ren.record(fc.cmd);
          ren.ui_record(fc.cmd);
          uint64_t te = platform::now_ns();
          swap.end_frame(fc);
          uint64_t tf = platform::now_ns();
          ubo_ns += tc - tb; draw_ns += td - tc; record_ns += te - td; submit_ns += tf - te;
        }
        // Yeniden kurma TEK YERDEN: kare BASINDAKI sync_size (yukari bak).
        // OUT_OF_DATE bayragi kalir, sonraki karenin basinda oradan islenir —
        // boylece cizim ile hedef olcusu ayni karede ayrismaz.
      }
      render_ns += platform::now_ns() - t1;
      report_frames++;
    }
    prof.end_frame();
    uint64_t fa = AllocGate::end_frame();
    if (frame_i > 5 && fa > frame_allocs_max) frame_allocs_max = fa;
    frame_i++;
    if (headless && frame_i >= headless_frames) running = false;
    if (opts.max_frames && frame_i >= opts.max_frames) running = false;
    if (now - report_ns > 2000000000ull || !running) {
      report_ns = now;
      static uint64_t scratch[1200];
      FrameStats st = prof.frame_stats(Span<uint64_t>(scratch, 1200), 120);
      double rf = report_frames ? (double)report_frames : 1.0;
      hud_ms = (float)(st.p50_ns / 1e6);
      hud_fps = hud_ms > 0 ? 1000.0f / hud_ms : 0;
      std::printf("[engine_demo] kare %u | p50 %.2f ms p99 %.2f ms max %.2f ms | sim %.2f render %.2f (bekle+acquire %.2f, ubo %.2f, draw-listesi %.2f, kayit %.2f, submit+present %.2f) ms/kare | cizim %u | lod %u/%u/%u | isik %u (kume %u) | kare ici new (en cok) %llu | ozet %016llx\n",
                  frame_i, st.p50_ns / 1e6, st.p99_ns / 1e6, st.max_ns / 1e6, sim_ns / rf / 1e6, render_ns / rf / 1e6,
                  acquire_ns / rf / 1e6, ubo_ns / rf / 1e6, draw_ns / rf / 1e6, record_ns / rf / 1e6, submit_ns / rf / 1e6,
                  ren.stats().draws, lod_counts[0], lod_counts[1], lod_counts[2], ren.point_light_count(), ren.stats().clusters.clusters_touched,
                  (unsigned long long)frame_allocs_max, (unsigned long long)scene.content_hash());
      if (interactive) {
        Vec3 pp = scene.player_position();
        std::printf("[engine_demo] oyuncu (%.2f, %.2f, %.2f) cubuk (%.2f, %.2f) dokunus %u\n", pp.x, pp.y, pp.z, stick.move.x,
                    stick.move.y, host->touch && host->touch(host->user) ? host->touch(host->user)->count : 0u);
      }
      if (!headless && swap.suboptimal_frames())
        std::printf("[engine_demo] sunum SUBOPTIMAL %llu kare (preTransform 0x%x != yuzey) — kompozitor donduruyor\n",
                    (unsigned long long)swap.suboptimal_frames(), (unsigned)swap.pretransform());
      sim_ns = render_ns = acquire_ns = ubo_ns = draw_ns = record_ns = submit_ns = 0;
      report_frames = 0;
    }
  }
  double total_s = (platform::now_ns() - start_ns) / 1e9;
  std::printf("[engine_demo] toplam %u kare, %.1f s, ortalama %.1f fps\n", frame_i, total_s, total_s > 0 ? frame_i / total_s : 0.0);
  if (have_blob) {
    const content::SceneRuntimeStats ss = srt.stats();
    std::printf("[engine_demo] sahne blob son kare: %u cizim, %u isik, %u govde\n", ss.draws, ss.lights, ss.bodies);
    srt.despawn(scene.physics());
  }
  if (adev.ok()) {
    audio::MixerStats ms = mixer.stats();
    std::printf("[engine_demo] ses: %llu callback, %llu kare (%.1f s), tepe %.2f, dusen komut %u\n", (unsigned long long)ms.callbacks,
                (unsigned long long)ms.frames_rendered, (double)ms.frames_rendered / (double)(mixer.rate() ? mixer.rate() : 1), ms.peak,
                ms.commands_dropped);
    adev.shutdown();
  }
  if (dev.caps().validation_layer) {
    std::printf("[engine_demo] dogrulama: %u hata, BestPractices %u uyari (%u Arm/Mali), messenger=%d\n",
                dev.validation_errors(), dev.best_practice_warnings(), dev.best_practice_arm_warnings(),
                (int)dev.caps().debug_messenger);
    for (uint32_t i = 0; i < dev.best_practice_id_count(); i++)
      std::printf("[engine_demo]   %s x%u%s\n", dev.best_practice_id(i).name, dev.best_practice_id(i).count,
                  dev.best_practice_id(i).arm ? "  <- Mali" : "");
    std::printf("[engine_demo] seyrek indeks (CPU, offset dogru): %u mesh; katman 'sparse-index-buffer' %u (0 mesh ise sahte pozitif, VVL 45)\n",
                ren.sparse_mesh_count(), dev.best_practice_count("sparse-index-buffer"));
  }
  if (headless && opts.out_path) {
    if (rhi::write_ppm(opts.out_path, ores.pixels, oc.width, oc.height)) std::printf("[engine_demo] goruntu: %s\n", opts.out_path);
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
  scene.shutdown();
  ren.shutdown();
  if (off) rhi::offscreen_destroy(off);
  if (!headless) swap.shutdown();
  dev.shutdown();
  return 0;
}

} // namespace tulpar::engine::app
