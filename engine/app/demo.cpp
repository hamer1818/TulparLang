// engine_demo — pencereli (GLFW + Vulkan swapchain) ya da headless
// (--headless N --out x.ppm) Faz 2 sahnesi. Kullanici pencereyi kendi
// makinesinde acar; ben yalniz headless yolu dogrularim (kural: pencere acma).
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
#include "platform/window.hpp"
#include "renderer/renderer.hpp"
#include "rhi/device.hpp"
#include "rhi/offscreen.hpp"
#include "rhi/swapchain.hpp"
#include "rhi/vk_api.hpp"
#include "sim/schedule.hpp"

using namespace tulpar::engine;

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
} // namespace

int main(int argc, char **argv) {
  uint32_t headless_frames = 0, max_frames = 0;
  const char *out_path = nullptr;
  uint32_t width = 1280, height = 720;
  for (int i = 1; i < argc; i++) {
    if (!std::strcmp(argv[i], "--headless") && i + 1 < argc) headless_frames = (uint32_t)std::atoi(argv[++i]);
    else if (!std::strcmp(argv[i], "--out") && i + 1 < argc) out_path = argv[++i];
    else if (!std::strcmp(argv[i], "--frames") && i + 1 < argc) max_frames = (uint32_t)std::atoi(argv[++i]);
    else if (!std::strcmp(argv[i], "--size") && i + 2 < argc) { width = (uint32_t)std::atoi(argv[++i]); height = (uint32_t)std::atoi(argv[++i]); }
  }
  platform::CrashConfig cc;
  cc.report_dir = ".";
  cc.build_id = "engine_demo";
  platform::crash_reporter_install(cc);

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
  platform::Window win;
  rhi::DeviceConfig dc;
  const char *pref = std::getenv("TULPAR_ENGINE_GPU");
  dc.prefer = pref ? pref : "";
  dc.validation = std::getenv("TULPAR_ENGINE_VK_VALIDATION") != nullptr;
  if (!headless_frames) {
    platform::WindowConfig wc;
    wc.width = width; wc.height = height; wc.title = "Tulpar Engine — Faz 2 sahnesi (ESC: cikis)";
    if (!win.open(wc)) { std::fprintf(stderr, "pencere: %s\n", win.last_error()); return 1; }
    uint32_t n = 0;
    dc.instance_extensions = win.required_instance_extensions(&n);
    dc.instance_extension_count = n;
  }
  rhi::Device dev;
  if (!dev.init_instance(api, dc)) { std::fprintf(stderr, "instance: %s\n", dev.last_error()); return 1; }
  VkSurfaceKHR surface = VK_NULL_HANDLE;
  if (!headless_frames) {
    void *s = nullptr;
    if (!win.create_surface(dev.instance(), &s)) { std::fprintf(stderr, "yuzey olusturulamadi\n"); return 1; }
    surface = static_cast<VkSurfaceKHR>(s);
  }
  if (!dev.init_device(surface)) { std::fprintf(stderr, "cihaz: %s\n", dev.last_error()); return 1; }
  const rhi::DeviceCaps &caps = dev.caps();
  std::printf("[engine_demo] GPU: %s (Vulkan %u.%u) worker=%u\n", caps.device_name, VK_API_VERSION_MAJOR(caps.api_version),
              VK_API_VERSION_MINOR(caps.api_version), jobs.worker_count());

  rhi::Swapchain swap;
  rhi::OffscreenTarget *off = nullptr;
  rhi::OffscreenConfig oc;
  rhi::OffscreenResult ores;
  VkRenderPass rp = VK_NULL_HANDLE;
  if (headless_frames) {
    oc.width = width; oc.height = height;
    off = rhi::offscreen_create(dev, sys, oc, &ores);
    if (!off) { std::fprintf(stderr, "offscreen: %s\n", ores.error); return 1; }
    rp = rhi::offscreen_render_pass(off);
  } else {
    uint32_t fw, fh;
    win.framebuffer_size(&fw, &fh);
    if (!swap.init(dev, sys, surface, fw, fh)) { std::fprintf(stderr, "swapchain\n"); return 1; }
    rp = swap.render_pass();
  }
  renderer::Renderer ren;
  renderer::RendererConfig rc;
  if (!ren.init(dev, sys, rp, rc)) { std::fprintf(stderr, "renderer\n"); return 1; }
  renderer::Vertex v[24]; uint32_t idx[36];
  uint32_t n = renderer::Renderer::cube(v, idx);
  renderer::MeshHandle cube = ren.create_mesh(v, 24, idx, n);
  n = renderer::Renderer::plane(v, idx);
  renderer::MeshHandle plane = ren.create_mesh(v, 4, idx, n);
  ren.set_light(normalize(Vec3{0.5f, 1.0f, 0.35f}), {0.16f, 0.17f, 0.2f}, 0.85f);

  app::DemoScene scene;
  if (!scene.init(sys, &jobs)) { std::fprintf(stderr, "sahne\n"); return 1; }
  std::printf("[engine_demo] sahne: %u entity, kutu+ajan+eklem\n", scene.entities());

  sim::FixedStep fs;
  Cam cam;
  uint64_t last_ns = platform::now_ns(), report_ns = last_ns;
  uint32_t frame_i = 0, tick_i = 0;
  uint64_t frame_allocs_max = 0;
  bool running = true;
  while (running) {
    ENGINE_ZONE("frame");
    uint64_t now = platform::now_ns();
    float dt = (float)((now - last_ns) / 1e9);
    last_ns = now;
    if (dt > 0.25f) dt = 0.25f;
    if (headless_frames) dt = 1.0f / 60.0f;
    AllocGate::begin_frame();
    prof.begin_frame();
    frame.begin_frame();
    if (!headless_frames) {
      win.poll();
      if (win.should_close() || win.input().key_down[256] /* ESC */) running = false;
    }
    {
      ENGINE_ZONE("sim");
      uint32_t ticks = fs.advance(dt);
      for (uint32_t t = 0; t < ticks; t++) scene.tick(fs.step_s, tick_i++);
    }
    cam.angle += dt * 0.15f;
    {
      ENGINE_ZONE("render");
      float aspect = (float)width / (float)height;
      if (!headless_frames) aspect = (float)swap.extent().width / (float)swap.extent().height;
      ren.set_camera(cam_view(cam), Mat4::perspective(kPi / 3.5f, aspect, 0.1f, 200.0f));
      if (headless_frames) {
        ren.begin_frame(frame_i);
        scene.draw(ren, cube, plane);
        RecordCtx rctx{&ren};
        if (!rhi::offscreen_render_custom(off, oc, record_cb, &rctx, &ores)) { std::fprintf(stderr, "kare: %s\n", ores.error); return 1; }
      } else {
        // Swapchain'in kare yuvasi (fence beklenmis) renderer'in UBO yuvasini secer;
        // ayri sayaclar acquire basarisizliginda ayrisip GPU'nun okudugu UBO'ya yazardi.
        rhi::FrameContext fc;
        if (swap.begin_frame(&fc)) {
          ren.begin_frame(fc.frame_index);
          scene.draw(ren, cube, plane);
          ren.record(fc.cmd);
          swap.end_frame(fc);
        }
        if (swap.needs_recreate()) {
          uint32_t fw, fh;
          win.framebuffer_size(&fw, &fh);
          if (fw && fh) swap.recreate(fw, fh);
        }
      }
    }
    prof.end_frame();
    uint64_t fa = AllocGate::end_frame();
    if (frame_i > 5 && fa > frame_allocs_max) frame_allocs_max = fa;
    frame_i++;
    if (headless_frames && frame_i >= headless_frames) running = false;
    if (max_frames && frame_i >= max_frames) running = false;
    if (now - report_ns > 2000000000ull || !running) {
      report_ns = now;
      static uint64_t scratch[1200];
      FrameStats st = prof.frame_stats(Span<uint64_t>(scratch, 1200), 120);
      std::printf("[engine_demo] kare %u | p50 %.2f ms p99 %.2f ms max %.2f ms | cizim %u | kare ici new (en cok) %llu | ozet %016llx\n",
                  frame_i, st.p50_ns / 1e6, st.p99_ns / 1e6, st.max_ns / 1e6, ren.stats().draws,
                  (unsigned long long)frame_allocs_max, (unsigned long long)scene.content_hash());
    }
  }
  if (headless_frames && out_path) {
    if (rhi::write_ppm(out_path, ores.pixels, oc.width, oc.height)) std::printf("[engine_demo] goruntu: %s\n", out_path);
  }
  dev.api().vkDeviceWaitIdle(dev.handle());
  scene.shutdown();
  ren.shutdown();
  if (off) rhi::offscreen_destroy(off);
  if (!headless_frames) swap.shutdown();
  dev.shutdown();
  win.close();
  jobs.shutdown();
  return 0;
}
