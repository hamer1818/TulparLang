// engine_editor — masaustu editor (GLFW dlopen pencere) ya da headless.
//   engine_editor [--scene x.sahne] [--headless N --out x.ppm] [--size WxH] [--validation]
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "app/editor_app.hpp"
#include "platform/window.hpp"

using namespace tulpar::engine;

namespace {
const char *const *w_exts(void *user, uint32_t *n) { return static_cast<platform::Window *>(user)->required_instance_extensions(n); }
bool w_surface(void *user, rhi::VkApi &, VkInstance inst, VkSurfaceKHR *out) {
  void *s = nullptr;
  if (!static_cast<platform::Window *>(user)->create_surface((void *)inst, &s)) return false;
  *out = (VkSurfaceKHR)s;
  return true;
}
bool w_poll(void *user, uint32_t *w, uint32_t *h) {
  auto *win = static_cast<platform::Window *>(user);
  win->poll();
  win->framebuffer_size(w, h);
  return !win->should_close();
}
const platform::InputState *w_input(void *user) { return &static_cast<platform::Window *>(user)->input(); }
void w_window_size(void *user, uint32_t *w, uint32_t *h) { static_cast<platform::Window *>(user)->window_size(w, h); }
bool w_set_fullscreen(void *user, bool on) { return static_cast<platform::Window *>(user)->set_fullscreen(on); }
bool w_is_fullscreen(void *user) { return static_cast<platform::Window *>(user)->is_fullscreen(); }
} // namespace

int main(int argc, char **argv) {
  app::EditorOptions o;
  for (int i = 1; i < argc; i++) {
    if (!std::strcmp(argv[i], "--headless") && i + 1 < argc) o.headless_frames = (uint32_t)std::atoi(argv[++i]);
    else if (!std::strcmp(argv[i], "--out") && i + 1 < argc) o.out_path = argv[++i];
    else if (!std::strcmp(argv[i], "--size") && i + 1 < argc) { unsigned w = 0, h = 0; if (std::sscanf(argv[++i], "%ux%u", &w, &h) == 2) { o.width = w; o.height = h; } }
    else if (!std::strcmp(argv[i], "--validation")) o.validation = true;
    else if (!std::strcmp(argv[i], "--scene") && i + 1 < argc) o.scene_path = argv[++i];
  }
  if (std::getenv("TULPAR_ENGINE_VK_VALIDATION")) o.validation = true;
  if (o.headless_frames > 0) return app::editor_run(o, nullptr);
  platform::Window win;
  platform::WindowConfig wc;
  wc.width = o.width; wc.height = o.height; wc.title = "Tulpar Editor";
  if (!win.open(wc)) { std::fprintf(stderr, "pencere: %s\n", win.last_error()); return 1; }
  app::EditorHost host;
  host.user = &win;
  host.instance_extensions = w_exts;
  host.create_surface = w_surface;
  host.poll = w_poll;
  host.input = w_input;
  host.window_size = w_window_size;
  host.set_fullscreen = w_set_fullscreen;
  host.is_fullscreen = w_is_fullscreen;
  int rc = app::editor_run(o, &host);
  win.close();
  return rc;
}
