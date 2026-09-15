// L6 BRIDGE — platform host sozlesmesi (kopru cekirdegi ile masaustu/Android
// hostlari arasinda). Masaustu: desktop_host.cpp (platform::Window, GLFW dlopen).
// Android: android_host.cpp (NativeActivity, native_app_glue). Cekirdek
// (engine_api.cpp) yalniz bu arayuzu gorur.
#pragma once
#include <cstdint>

#include <vulkan/vulkan.h>

#include "platform/touch.hpp"
#include "platform/window.hpp"
#include "rhi/vk_api.hpp"

namespace tulpar::engine::bridge {

enum class HostPoll : uint8_t { Run, NoWindow, WindowChanged, Quit };

struct BridgeHost {
  void *user = nullptr;
  const char *const *(*instance_extensions)(void *user, uint32_t *count) = nullptr;
  bool (*create_surface)(void *user, rhi::VkApi &api, VkInstance instance, VkSurfaceKHR *out) = nullptr;
  HostPoll (*poll)(void *user, uint32_t *w, uint32_t *h) = nullptr;
  const platform::TouchState *(*touch)(void *user) = nullptr;   // null = dokunmatik yok
  const platform::InputState *(*input)(void *user) = nullptr;   // null = klavye/fare yok
  void (*close)(void *user) = nullptr;
};

// Platform dosyasi saglar. false: pencere acilamadi (sebep err'e, cap bayt).
bool bridge_host_open(BridgeHost *out, const char *title, uint32_t w, uint32_t h, char *err, size_t err_cap);
void bridge_host_close(BridgeHost *h);

} // namespace tulpar::engine::bridge
