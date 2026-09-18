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
  // TAM EKRAN: pencere -> monitorun video kipi; geri donuste KAYDEDILEN pencere
  // dikdortgeni. Donus: istek uygulandi mi (GLFW sembolu yoksa false — cagiran
  // menu ogesini soluk gosterir). Olcu degisimini uygulama ayrica islemek
  // ZORUNDA: Wayland'de yuzey olcusunu uygulama surer (rhi/swapchain.hpp
  // ResizeAction), yani swapchain'i yeniden kuran kod bu cagriya bagli degil.
  bool set_fullscreen(bool on);
  bool is_fullscreen() const;
  // Pencere olcusu MANTIKSAL pikselde (imlec konumu bu uzayda gelir); HiDPI'da
  // framebuffer'dan kucuktur. Orani arayuz isaretci olcegidir.
  void window_size(uint32_t *w, uint32_t *h) const;
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
  // Tam ekrandan cikista geri donulecek pencere dikdortgeni (Wayland'de konum
  // okunamaz, o zaman 0,0 ile gecilir ve kompozitor yerlestirir).
  int saved_x_ = 0, saved_y_ = 0, saved_w_ = 0, saved_h_ = 0;
};

} // namespace tulpar::engine::platform
