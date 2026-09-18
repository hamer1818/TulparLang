// L6 APP — Editor: motorun kendisi masaustunde duzenleme kipinde (PLAN L7:
// "editor motoru kutuphane olarak kullanir"). Dear ImGui panelleri + ImGuizmo
// gizmo. Veri modeli content::SceneDesc (.sahne dosyasi): yukle / kaydet /
// geri al / yinele (SceneHistory), ekle / sil, oynat = govdeler fizige girer,
// durdur = yazar donusumune doner (veri modeli gercek, sim turetilmis).
// Dunya paneli (gunes/ortam/golge/kamera), Derle (Ctrl+B) = runtime blob .sahneb.
// Coklu secim (Ctrl+tik; grup tasima/silme gunluge TEK eylem), Kaynaklar paneli
// (sahne dizinindeki glTF'ler), isik yaricapi / golge hacmi / gunes yonu gizmolari.
// Arka plan: Faz 2 demo sahnesi. Headless kip: betikli durumla N kare, PPM.
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
  const char *scene_path = nullptr; // null: tests/assets/editor.sahne
};

struct EditorHost {
  void *user = nullptr;
  const char *const *(*instance_extensions)(void *user, uint32_t *count) = nullptr;
  bool (*create_surface)(void *user, rhi::VkApi &api, VkInstance instance, VkSurfaceKHR *out) = nullptr;
  // Olaylari isler; false = kapat. *w/*h framebuffer.
  bool (*poll)(void *user, uint32_t *w, uint32_t *h) = nullptr;
  const platform::InputState *(*input)(void *user) = nullptr;
  // Pencere olcusu mantiksal pikselde (imlec uzayi). nullptr = framebuffer ile
  // ayni (isaretci olcegi 1). HiDPI'da yoksa tiklamalar arayuzu ISKALAR.
  void (*window_size)(void *user, uint32_t *w, uint32_t *h) = nullptr;
  // Tam ekran (F11). nullptr = host desteklemiyor: menu ogesi SOLUK gorunur
  // (sessizce hicbir sey yapan bir dugme yok).
  bool (*set_fullscreen)(void *user, bool on) = nullptr;
  bool (*is_fullscreen)(void *user) = nullptr;
};

int editor_run(const EditorOptions &opts, const EditorHost *host);

} // namespace tulpar::engine::app
