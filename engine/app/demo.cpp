// engine_demo (masaustu): GLFW penceresi ya da --headless N --out x.ppm.
// Kullanici pencereyi kendi makinesinde acar; ben yalniz headless yolu dogrularim.
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "app/demo_app.hpp"
#include "platform/crash.hpp"
#include "platform/window.hpp"
#include "rhi/vk_api.hpp"

using namespace tulpar::engine;

namespace {
const char *const *glfw_exts(void *user, uint32_t *n) { return static_cast<platform::Window *>(user)->required_instance_extensions(n); }
bool glfw_surface(void *user, rhi::VkApi &, VkInstance inst, VkSurfaceKHR *out) {
  void *s = nullptr;
  if (!static_cast<platform::Window *>(user)->create_surface(inst, &s)) return false;
  *out = static_cast<VkSurfaceKHR>(s);
  return true;
}
app::HostPoll glfw_poll(void *user, uint32_t *w, uint32_t *h) {
  auto *win = static_cast<platform::Window *>(user);
  win->poll();
  win->framebuffer_size(w, h);
  if (win->should_close() || win->input().key_down[256] /* ESC */) return app::HostPoll::Quit;
  return (*w && *h) ? app::HostPoll::Run : app::HostPoll::NoWindow; // kucultulmus: cizme
}
} // namespace

int main(int argc, char **argv) {
  app::DemoOptions o;
  for (int i = 1; i < argc; i++) {
    if (!std::strcmp(argv[i], "--headless") && i + 1 < argc) o.headless_frames = (uint32_t)std::atoi(argv[++i]);
    else if (!std::strcmp(argv[i], "--out") && i + 1 < argc) o.out_path = argv[++i];
    else if (!std::strcmp(argv[i], "--frames") && i + 1 < argc) o.max_frames = (uint32_t)std::atoi(argv[++i]);
    else if (!std::strcmp(argv[i], "--size") && i + 2 < argc) { o.width = (uint32_t)std::atoi(argv[++i]); o.height = (uint32_t)std::atoi(argv[++i]); }
    else if (!std::strcmp(argv[i], "--present") && i + 1 < argc) o.present_mode = argv[++i];
    else if (!std::strcmp(argv[i], "--no-prerotate")) o.prerotate = false;
  }
  o.gpu_prefer = std::getenv("TULPAR_ENGINE_GPU");
  o.validation = std::getenv("TULPAR_ENGINE_VK_VALIDATION") != nullptr;
  platform::CrashConfig cc;
  cc.report_dir = ".";
  cc.build_id = "engine_demo";
  platform::crash_reporter_install(cc);
  if (o.headless_frames) return app::demo_run(o, nullptr);
  platform::Window win;
  platform::WindowConfig wc;
  wc.width = o.width;
  wc.height = o.height;
  wc.title = "Tulpar Engine — Faz 2 sahnesi (ESC: cikis)";
  if (!win.open(wc)) { std::fprintf(stderr, "pencere: %s\n", win.last_error()); return 1; }
  app::DemoHost host;
  host.user = &win;
  host.instance_extensions = glfw_exts;
  host.create_surface = glfw_surface;
  host.poll = glfw_poll;
  int rc = app::demo_run(o, &host);
  win.close();
  return rc;
}
