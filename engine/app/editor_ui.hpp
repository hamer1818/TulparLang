// L6 APP — Editor arayuzu: Dear ImGui (vendored, MIT) + ImGuizmo, motorun
// Vulkan cihazi/gecisi uzerinde. Yalniz MASAUSTU editor icin; oyun ici HUD
// kendi 2B cekirdegimizde kalir (0 ayirma, telefon). ImGui malloc kullanir
// (AllocGate 'new' saymaz); editor karesi 0-ayirma kapisinin disindadir.
// Vulkan fonksiyonlari bizim dlopen'li yukleyiciden (LoadFunctions), prototip yok.
#pragma once
#include <cstdint>

#include "platform/window.hpp"
#include "rhi/device.hpp"

namespace tulpar::engine::app {

struct EditorUiStats {
  uint32_t vertices = 0, indices = 0, draw_lists = 0;
};

class EditorUi {
public:
  // rp/subpass: ImGui'nin cizecegi gecis (renk subpass'i). image_count: swapchain
  // goruntu sayisi (>= 2). font_ttf: varsa TrueType (Turkce glifler), yoksa gomulu.
  bool init(rhi::Device &dev, VkRenderPass rp, uint32_t subpass, uint32_t image_count, const char *font_ttf, float font_px);
  void shutdown();
  // Kare: girdi (null = headless, girdi yok), gorunen olcu (mantiksal piksel), dt.
  void begin_frame(const platform::InputState *in, float width, float height, float dt);
  void end_frame(); // ImGui::Render
  void record(VkCommandBuffer cb); // renk subpass'i icinde, 3B ve HUD'dan sonra
  EditorUiStats stats() const { return stats_; }
  bool ok() const { return ok_; }
  const char *last_error() const { return err_; }
  // Fare ImGui pencerelerinin uzerinde mi (sahne kamerasi o zaman girdi almaz).
  bool wants_mouse() const;
  bool wants_keyboard() const;

private:
  rhi::Device *dev_ = nullptr;
  bool ok_ = false;
  bool prev_keys_[512] = {};
  bool prev_mouse_[3] = {};
  double prev_scroll_ = 0;
  EditorUiStats stats_{};
  char err_[128] = {0};
};

} // namespace tulpar::engine::app
