#include "app/demo_app.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "app/demo_scene.hpp"
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
#include "sim/schedule.hpp"

namespace tulpar::engine::app {

namespace {
struct Cam {
  float angle = 0.6f;
  float radius = 24.0f;
  float height = 13.0f;
};
Mat4 cam_view(const Cam &c) {
  Vec3 eye{std::sin(c.angle) * c.radius, c.height, std::cos(c.angle) * c.radius};
  return Mat4::look_at(eye, {0, 0.5f, -1}, {0, 1, 0});
}
struct RecordCtx {
  renderer::Renderer *r;
};
void record_cb(VkCommandBuffer cb, void *user) { static_cast<RecordCtx *>(user)->r->record(cb); }
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

  uint32_t width = opts.width, height = opts.height;
  rhi::Swapchain swap;
  rhi::SwapchainConfig swap_cfg;
  swap_cfg.prerotate = opts.prerotate;
  swap_cfg.preferred_present_mode = present_mode_of(opts.present_mode);
  rhi::OffscreenTarget *off = nullptr;
  rhi::OffscreenConfig oc;
  rhi::OffscreenResult ores;
  VkRenderPass rp = VK_NULL_HANDLE;
  if (headless) {
    oc.width = width;
    oc.height = height;
    off = rhi::offscreen_create(dev, sys, oc, &ores);
    if (!off) { std::fprintf(stderr, "offscreen: %s\n", ores.error); return 1; }
    rp = rhi::offscreen_render_pass(off);
  } else {
    uint32_t fw = 0, fh = 0;
    if (host->poll(host->user, &fw, &fh) == HostPoll::Quit) return 0;
    if (!swap.init(dev, sys, surface, fw, fh, swap_cfg)) { std::fprintf(stderr, "swapchain\n"); return 1; }
    rp = swap.render_pass();
    std::printf("[engine_demo] swapchain goruntu %ux%u, gorunen %ux%u, on-dondurme %.0f derece, sunum %s\n",
                swap.extent().width, swap.extent().height, swap.logical_extent().width, swap.logical_extent().height,
                swap.rotation_radians() * 180.0f / kPi, present_name(swap.present_mode()));
  }
  renderer::Renderer ren;
  renderer::RendererConfig rc;
  if (!ren.init(dev, sys, rp, rc)) { std::fprintf(stderr, "renderer\n"); return 1; }
  renderer::Vertex v[24];
  uint32_t idx[36];
  uint32_t n = renderer::Renderer::cube(v, idx);
  renderer::MeshHandle cube = ren.create_mesh(v, 24, idx, n);
  n = renderer::Renderer::plane(v, idx);
  renderer::MeshHandle plane = ren.create_mesh(v, 4, idx, n);
  ren.set_light(normalize(Vec3{0.5f, 1.0f, 0.35f}), {0.16f, 0.17f, 0.2f}, 0.85f);
  // Golge kutusu sahneyi kapsamali: arena 20x20, duvar 3 m, kutular ~5 m'ye kadar.
  ren.set_shadow_volume({0, 1.0f, -1.0f}, 17.0f, 70.0f);
  renderer::ShadowInfo sh = ren.shadow();
  std::printf("[engine_demo] golge: %s %ux%u format=%d dogrusal_suzme=%d%s\n", sh.enabled ? "acik" : "KAPALI", sh.size,
              sh.size, (int)sh.format, (int)sh.linear_filter, sh.enabled ? "" : sh.disabled_reason);

  DemoScene scene;
  if (!scene.init(sys, &jobs)) { std::fprintf(stderr, "sahne\n"); return 1; }
  std::printf("[engine_demo] sahne: %u entity, kutu+ajan+eklem\n", scene.entities());

  sim::FixedStep fs;
  Cam cam;
  uint64_t last_ns = platform::now_ns(), report_ns = last_ns, start_ns = last_ns;
  uint32_t frame_i = 0, tick_i = 0;
  uint64_t frame_allocs_max = 0;
  uint64_t sim_ns = 0, render_ns = 0, acquire_ns = 0, ubo_ns = 0, draw_ns = 0, record_ns = 0, submit_ns = 0;
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
        if (!swap.init(dev, sys, ns, fw, fh)) { std::fprintf(stderr, "swapchain (yeniden)\n"); return 1; }
        std::printf("[engine_demo] pencere yeniden: swapchain %ux%u\n", swap.extent().width, swap.extent().height);
        have_window = true;
        break;
      }
      case HostPoll::Run: have_window = true; break;
      }
    }
    uint64_t t0 = platform::now_ns();
    {
      ENGINE_ZONE("sim");
      uint32_t ticks = fs.advance(dt);
      for (uint32_t t = 0; t < ticks; t++) scene.tick(fs.step_s, tick_i++);
    }
    uint64_t t1 = platform::now_ns();
    sim_ns += t1 - t0;
    cam.angle += dt * 0.15f;
    if (running && (headless || have_window)) {
      ENGINE_ZONE("render");
      // En-boy orani GORUNEN yonden; on-dondurmede projeksiyon clip uzayinda dondurulur.
      float aspect = headless ? (float)width / (float)height
                              : (float)swap.logical_extent().width / (float)swap.logical_extent().height;
      Mat4 proj = Mat4::perspective(kPi / 3.5f, aspect, 0.1f, 200.0f);
      if (!headless && swap.rotation_radians() != 0.0f) proj = Mat4::rotate({0, 0, 1}, swap.rotation_radians()) * proj;
      ren.set_camera(cam_view(cam), proj);
      if (headless) {
        ren.begin_frame(frame_i);
        scene.draw(ren, cube, plane);
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
          scene.draw(ren, cube, plane);
          uint64_t td = platform::now_ns();
          ren.record_shadow(fc.cmd); // kendi pass'i: ana pass BASLAMADAN once
          swap.begin_render_pass(fc);
          ren.record(fc.cmd);
          uint64_t te = platform::now_ns();
          swap.end_frame(fc);
          uint64_t tf = platform::now_ns();
          ubo_ns += tc - tb; draw_ns += td - tc; record_ns += te - td; submit_ns += tf - te;
        }
        if (swap.needs_recreate() && fw && fh) swap.recreate(fw, fh);
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
      std::printf("[engine_demo] kare %u | p50 %.2f ms p99 %.2f ms max %.2f ms | sim %.2f render %.2f (bekle+acquire %.2f, ubo %.2f, draw-listesi %.2f, kayit %.2f, submit+present %.2f) ms/kare | cizim %u | kare ici new (en cok) %llu | ozet %016llx\n",
                  frame_i, st.p50_ns / 1e6, st.p99_ns / 1e6, st.max_ns / 1e6, sim_ns / rf / 1e6, render_ns / rf / 1e6,
                  acquire_ns / rf / 1e6, ubo_ns / rf / 1e6, draw_ns / rf / 1e6, record_ns / rf / 1e6, submit_ns / rf / 1e6,
                  ren.stats().draws, (unsigned long long)frame_allocs_max, (unsigned long long)scene.content_hash());
      if (!headless && swap.suboptimal_frames())
        std::printf("[engine_demo] sunum SUBOPTIMAL %llu kare (preTransform 0x%x != yuzey) — kompozitor donduruyor\n",
                    (unsigned long long)swap.suboptimal_frames(), (unsigned)swap.pretransform());
      sim_ns = render_ns = acquire_ns = ubo_ns = draw_ns = record_ns = submit_ns = 0;
      report_frames = 0;
    }
  }
  double total_s = (platform::now_ns() - start_ns) / 1e9;
  std::printf("[engine_demo] toplam %u kare, %.1f s, ortalama %.1f fps\n", frame_i, total_s, total_s > 0 ? frame_i / total_s : 0.0);
  if (headless && opts.out_path) {
    if (rhi::write_ppm(opts.out_path, ores.pixels, oc.width, oc.height)) std::printf("[engine_demo] goruntu: %s\n", opts.out_path);
  }
  dev.api().vkDeviceWaitIdle(dev.handle());
  scene.shutdown();
  ren.shutdown();
  if (off) rhi::offscreen_destroy(off);
  if (!headless) swap.shutdown();
  dev.shutdown();
  jobs.shutdown();
  return 0;
}

} // namespace tulpar::engine::app
