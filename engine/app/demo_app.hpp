// L5 APP — demo uygulamasi: host'tan bagimsiz cekirdek. Host (masaustu GLFW,
// Android NativeActivity) yuzeyi ve olay dongusunu verir; host yoksa headless
// (offscreen render pass, --out ile PPM).
#pragma once
#include <cstdint>

#include <vulkan/vulkan.h>

#include "platform/touch.hpp"
#include "rhi/swapchain.hpp"

namespace tulpar::engine::rhi { struct VkApi; }

namespace tulpar::engine::app {

struct DemoOptions {
  uint32_t headless_frames = 0; // >0: pencere yok, N kare offscreen
  uint32_t max_frames = 0;      // >0: N kare sonra cik (pencereli)
  const char *out_path = nullptr; // headless son karesi (PPM)
  uint32_t width = 1280, height = 720;
  const char *gpu_prefer = nullptr; // DeviceConfig::prefer ("cpu" vb.)
  bool validation = false;
  bool audio = false; // 440 Hz ton (dongu): ses cihazi yolu + callback sayimi raporda
  bool prerotate = true;            // Android on-dondurme (A/B icin kapatilabilir)
  const char *present_mode = nullptr; // "fifo" (vars.), "mailbox", "immediate"
  rhi::SwapchainConfig::Hooks swap_hooks; // Android: Swappy sunum kancalari (host doldurur)
  const char *const *device_extensions = nullptr; // istege bagli cihaz uzantilari (varsa acilir, raporlanir)
  uint32_t device_extension_count = 0;
  // Derlenmis sahne (.sahneb, engine_sahnec / editor Derle): varsa kureler, borular, isiklar ve
  // govdeler blob'dan gelir ("sahne = blob + kod"); yoksa kod icindeki sabit yerlesim.
  const char *scene_blob = nullptr;
};

enum class HostPoll : uint8_t {
  Run,           // pencere var, ciz
  NoWindow,      // pencere gecici yok (arka plan): sim surer, cizim yok
  WindowChanged, // pencere yeniden yaratildi: yuzey + swapchain yeniden
  Quit,
};
struct DemoHost {
  void *user = nullptr;
  // Vulkan instance uzantilari (VK_KHR_surface + platform yuzeyi).
  const char *const *(*instance_extensions)(void *user, uint32_t *count) = nullptr;
  bool (*create_surface)(void *user, rhi::VkApi &api, VkInstance instance, VkSurfaceKHR *out) = nullptr;
  // Olaylari isler. *w/*h = framebuffer boyutu (0 = kucultulmus).
  HostPoll (*poll)(void *user, uint32_t *w, uint32_t *h) = nullptr;
  // Dokunmatik durum (masaustunde fare = parmak 0). null = girdi yok.
  const platform::TouchState *(*touch)(void *user) = nullptr;
  // Klavye hareketi (masaustu): -1..1; null = yok.
  void (*keyboard_move)(void *user, float *x, float *y, bool *jump) = nullptr;
};

// 0 = basari. host == nullptr → headless zorunlu (opts.headless_frames > 0 olmali).
int demo_run(const DemoOptions &opts, const DemoHost *host);

} // namespace tulpar::engine::app
