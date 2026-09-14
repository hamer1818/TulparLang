// L6 APP — Editor: motorun kendisi masaustunde duzenleme kipinde (PLAN L7:
// "editor motoru kutuphane olarak kullanir"). Dear ImGui panelleri + ImGuizmo
// gizmo, sahne = Faz 2 demo sahnesi + duzenlenebilir varliklar (LOD kureleri,
// iskeletli borular). Headless kip: betikli durumla N kare ciz, PPM yaz.
#pragma once
#include <cstdint>

#include "platform/window.hpp"
#include "rhi/vk_api.hpp"

namespace tulpar::engine::app {

struct EditorOptions {
  uint32_t headless_frames = 0; // >0: pencere yok, offscreen
  const char *out_path = nullptr;
  uint32_t width = 1280, height = 720;
  bool validation = false;
};

struct EditorHost {
  void *user = nullptr;
  const char *const *(*instance_extensions)(void *user, uint32_t *count) = nullptr;
  bool (*create_surface)(void *user, rhi::VkApi &api, VkInstance instance, VkSurfaceKHR *out) = nullptr;
  // Olaylari isler; false = kapat. *w/*h framebuffer.
  bool (*poll)(void *user, uint32_t *w, uint32_t *h) = nullptr;
  const platform::InputState *(*input)(void *user) = nullptr;
};

// Duzenlenebilir varlik (editor veri modelinin ilk dilimi; kaydet/yukle sonraki is).
struct EditorEntity {
  char name[32];
  float pos[3], rot_deg[3], scale[3];
  int kind;      // 0 = LOD kuresi, 1 = iskeletli boru
  float phase;   // boru: klip fazi (s)
};

int editor_run(const EditorOptions &opts, const EditorHost *host);

} // namespace tulpar::engine::app
