// L0 PLATFORM — masaustu gelistirme penceresi (GLFW, dlopen). Uretim hedefi
// degil: Android'de yuzey Kotlin host'tan gelir. Vulkan yuzeyi + gerekli
// instance uzantilarini verir; girdi TIMESTAMP'LI (glfwGetTime, olay aninda
// degil poll aninda — masaustu icin yeter; Android'de MotionEvent.eventTime).
#pragma once
#include <cstdint>

namespace tulpar::engine::platform {

struct WindowConfig {
  uint32_t width = 1280;
  uint32_t height = 720;
  const char *title = "Tulpar Engine";
  bool resizable = true;
};

struct InputState {
  bool key_down[512] = {};   // GLFW tus kodlari
  double mouse_x = 0, mouse_y = 0;
  bool mouse_down[3] = {};
  double scroll_y = 0;       // birikimli
  double time_s = 0;         // son poll zamani
  // Metin girisi (unicode kod noktasi), poll'lar arasi birikir; okuyan sifirlar.
  static constexpr uint32_t kMaxChars = 32;
  uint32_t chars[kMaxChars] = {};
  uint32_t char_count = 0;
};

class Window {
public:
  // GLFW yuklenemez ya da pencere acilamazsa false; sebep last_error().
  bool open(const WindowConfig &cfg);
  void close();
  bool is_open() const { return win_ != nullptr; }
  void poll();
  bool should_close() const;
  void request_close();
  void framebuffer_size(uint32_t *w, uint32_t *h) const;
  const InputState &input() const { return input_; }
  const char *last_error() const { return err_; }

  // Vulkan: instance uzantilari (VK_KHR_surface + platform) ve yuzey.
  const char *const *required_instance_extensions(uint32_t *count) const;
  // VkInstance/VkSurfaceKHR opak: L0, Vulkan basligini bilmez (L2'nin isi).
  bool create_surface(void *vk_instance, void **out_surface) const;

private:
  void *win_ = nullptr;
  InputState input_{};
  char err_[256] = {0};
};

} // namespace tulpar::engine::platform
