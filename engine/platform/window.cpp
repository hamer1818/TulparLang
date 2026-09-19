#include "platform/window.hpp"

#if !defined(__ANDROID__)
#include <dlfcn.h>

#include <cstdio>
#include <cstring>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

namespace tulpar::engine::platform {

namespace {
// dlopen ile GLFW: link bagimliligi yok.
struct Glfw {
  void *lib = nullptr;
  int (*init)() = nullptr;
  void (*terminate)() = nullptr;
  void (*window_hint)(int, int) = nullptr;
  GLFWwindow *(*create_window)(int, int, const char *, GLFWmonitor *, GLFWwindow *) = nullptr;
  void (*destroy_window)(GLFWwindow *) = nullptr;
  void (*poll_events)() = nullptr;
  int (*window_should_close)(GLFWwindow *) = nullptr;
  void (*set_window_should_close)(GLFWwindow *, int) = nullptr;
  void (*get_framebuffer_size)(GLFWwindow *, int *, int *) = nullptr;
  void (*get_window_size)(GLFWwindow *, int *, int *) = nullptr;
  int (*get_key)(GLFWwindow *, int) = nullptr;
  void (*get_cursor_pos)(GLFWwindow *, double *, double *) = nullptr;
  int (*get_mouse_button)(GLFWwindow *, int) = nullptr;
  double (*get_time)() = nullptr;
  const char **(*get_required_instance_extensions)(uint32_t *) = nullptr;
  int (*create_window_surface)(void *, GLFWwindow *, const void *, void **) = nullptr;
  int (*vulkan_supported)() = nullptr;
  const char *(*get_error)(const char **) = nullptr;
  GLFWscrollfun (*set_scroll_callback)(GLFWwindow *, GLFWscrollfun) = nullptr;
  GLFWcharfun (*set_char_callback)(GLFWwindow *, GLFWcharfun) = nullptr;
  // Tam ekran: SECMELI semboller (yoksa yetenek kapali, pencere acilmaya
  // devam eder — zorunlu sembol listesine konursa eski bir GLFW butun
  // pencereyi dusururdu).
  GLFWmonitor *(*get_primary_monitor)() = nullptr;
  GLFWmonitor *(*get_window_monitor)(GLFWwindow *) = nullptr;
  void (*set_window_monitor)(GLFWwindow *, GLFWmonitor *, int, int, int, int, int) = nullptr;
  const GLFWvidmode *(*get_video_mode)(GLFWmonitor *) = nullptr;
  void (*get_window_pos)(GLFWwindow *, int *, int *) = nullptr;
};
Glfw g;
double g_scroll_accum = 0;
uint32_t g_chars[InputState::kMaxChars];
uint32_t g_char_count = 0;
void on_scroll(GLFWwindow *, double, double y) { g_scroll_accum += y; }
void on_char(GLFWwindow *, unsigned int cp) { if (g_char_count < InputState::kMaxChars) g_chars[g_char_count++] = cp; }

bool load_glfw(char *err, size_t n) {
  if (g.lib) return true;
  const char *names[] = {
#if defined(__APPLE__)
      "libglfw.3.dylib", "/opt/homebrew/lib/libglfw.3.dylib", "/usr/local/lib/libglfw.3.dylib",
#else
      "libglfw.so.3", "libglfw.so",
#endif
  };
  for (const char *nm : names) {
    g.lib = dlopen(nm, RTLD_NOW | RTLD_LOCAL);
    if (g.lib) break;
  }
  if (!g.lib) {
    std::snprintf(err, n, "GLFW yok (libglfw.so.3 / libglfw.3.dylib): masaustu pencere acilamaz");
    return false;
  }
#define L(field, sym) g.field = (decltype(g.field))dlsym(g.lib, sym); if (!g.field) { std::snprintf(err, n, "GLFW sembolu yok: %s", sym); return false; }
  L(init, "glfwInit"); L(terminate, "glfwTerminate"); L(window_hint, "glfwWindowHint");
  L(create_window, "glfwCreateWindow"); L(destroy_window, "glfwDestroyWindow");
  L(poll_events, "glfwPollEvents"); L(window_should_close, "glfwWindowShouldClose");
  L(set_window_should_close, "glfwSetWindowShouldClose"); L(get_framebuffer_size, "glfwGetFramebufferSize");
  L(get_window_size, "glfwGetWindowSize");
  L(get_key, "glfwGetKey"); L(get_cursor_pos, "glfwGetCursorPos"); L(get_mouse_button, "glfwGetMouseButton");
  L(get_time, "glfwGetTime"); L(get_required_instance_extensions, "glfwGetRequiredInstanceExtensions");
  L(create_window_surface, "glfwCreateWindowSurface"); L(vulkan_supported, "glfwVulkanSupported");
  L(get_error, "glfwGetError"); L(set_scroll_callback, "glfwSetScrollCallback");
  L(set_char_callback, "glfwSetCharCallback");
#undef L
  // Secmeli (yoklugu hata DEGIL): tam ekran yetenegi.
#define O(field, sym) g.field = (decltype(g.field))dlsym(g.lib, sym);
  O(get_primary_monitor, "glfwGetPrimaryMonitor"); O(get_window_monitor, "glfwGetWindowMonitor");
  O(set_window_monitor, "glfwSetWindowMonitor"); O(get_video_mode, "glfwGetVideoMode");
  O(get_window_pos, "glfwGetWindowPos");
#undef O
  return true;
}
} // namespace

bool Window::open(const WindowConfig &cfg) {
  if (!load_glfw(err_, sizeof err_)) return false;
  if (!g.init()) {
    const char *d = nullptr;
    g.get_error(&d);
    std::snprintf(err_, sizeof err_, "glfwInit: %s", d ? d : "?");
    return false;
  }
  if (!g.vulkan_supported()) {
    std::snprintf(err_, sizeof err_, "GLFW: Vulkan loader bulunamadi");
    return false;
  }
  g.window_hint(GLFW_CLIENT_API, GLFW_NO_API);
  g.window_hint(GLFW_RESIZABLE, cfg.resizable ? GLFW_TRUE : GLFW_FALSE);
  GLFWwindow *w = g.create_window((int)cfg.width, (int)cfg.height, cfg.title, nullptr, nullptr);
  if (!w) {
    const char *d = nullptr;
    g.get_error(&d);
    std::snprintf(err_, sizeof err_, "glfwCreateWindow: %s", d ? d : "?");
    return false;
  }
  g.set_scroll_callback(w, on_scroll);
  g.set_char_callback(w, on_char);
  win_ = w;
  return true;
}

void Window::close() {
  if (win_) g.destroy_window(static_cast<GLFWwindow *>(win_));
  win_ = nullptr;
}

void Window::poll() {
  g.poll_events();
  GLFWwindow *w = static_cast<GLFWwindow *>(win_);
  for (int k = 32; k < 349 && k < 512; k++) input_.key_down[k] = g.get_key(w, k) == GLFW_PRESS;
  g.get_cursor_pos(w, &input_.mouse_x, &input_.mouse_y);
  for (int b = 0; b < 3; b++) input_.mouse_down[b] = g.get_mouse_button(w, b) == GLFW_PRESS;
  input_.scroll_y = g_scroll_accum;
  input_.time_s = g.get_time();
  input_.char_count = g_char_count;
  for (uint32_t i = 0; i < g_char_count; i++) input_.chars[i] = g_chars[i];
  g_char_count = 0;
}

bool Window::should_close() const { return win_ && g.window_should_close(static_cast<GLFWwindow *>(win_)); }
void Window::request_close() { if (win_) g.set_window_should_close(static_cast<GLFWwindow *>(win_), 1); }

void Window::framebuffer_size(uint32_t *w, uint32_t *h) const {
  int iw = 0, ih = 0;
  if (win_) g.get_framebuffer_size(static_cast<GLFWwindow *>(win_), &iw, &ih);
  *w = (uint32_t)(iw < 0 ? 0 : iw);
  *h = (uint32_t)(ih < 0 ? 0 : ih);
}

bool Window::is_fullscreen() const {
  return win_ && g.get_window_monitor && g.get_window_monitor(static_cast<GLFWwindow *>(win_)) != nullptr;
}

bool Window::set_fullscreen(bool on) {
  if (!win_ || !g.set_window_monitor || !g.get_primary_monitor || !g.get_video_mode || !g.get_window_monitor) return false;
  GLFWwindow *w = static_cast<GLFWwindow *>(win_);
  if (on == is_fullscreen()) return true;
  if (on) {
    // Pencere dikdortgenini SAKLA (cikista geri donulecek). Wayland'de konum
    // okunamaz: GLFW hata isaretler ve 0,0 kalir — kompozitor yerlestirir.
    if (g.get_window_pos) g.get_window_pos(w, &saved_x_, &saved_y_);
    int ww = 0, wh = 0;
    g.get_window_size(w, &ww, &wh);
    if (ww > 0 && wh > 0) { saved_w_ = ww; saved_h_ = wh; }
    GLFWmonitor *m = g.get_window_monitor(w);
    if (!m) m = g.get_primary_monitor();
    if (!m) return false;
    const GLFWvidmode *vm = g.get_video_mode(m);
    if (!vm || vm->width <= 0 || vm->height <= 0) return false;
    g.set_window_monitor(w, m, 0, 0, vm->width, vm->height, vm->refreshRate);
  } else {
    if (saved_w_ < 1 || saved_h_ < 1) { saved_w_ = 1280; saved_h_ = 720; }
    g.set_window_monitor(w, nullptr, saved_x_, saved_y_, saved_w_, saved_h_, 0);
  }
  // DIKKAT: burada swapchain'e dokunulmaz. Yeni olcu bir sonraki poll'da
  // framebuffer_size'dan gelir ve cagiran onu Swapchain::sync_size'a verir —
  // tek karar noktasi orasi (rhi/swapchain.hpp ResizeAction).
  return true;
}

void Window::window_size(uint32_t *w, uint32_t *h) const {
  int iw = 0, ih = 0;
  if (win_) g.get_window_size(static_cast<GLFWwindow *>(win_), &iw, &ih);
  *w = (uint32_t)(iw < 0 ? 0 : iw);
  *h = (uint32_t)(ih < 0 ? 0 : ih);
}

const char *const *Window::required_instance_extensions(uint32_t *count) const {
  if (!g.lib) { *count = 0; return nullptr; }
  return g.get_required_instance_extensions(count);
}

bool Window::create_surface(void *vk_instance, void **out_surface) const {
  if (!win_) return false;
  return g.create_window_surface(vk_instance, static_cast<GLFWwindow *>(win_), nullptr, out_surface) == 0;
}

} // namespace tulpar::engine::platform
#endif
