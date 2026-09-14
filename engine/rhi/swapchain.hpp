// L2 RHI — Swapchain + kare senkronu (masaustu pencere; Android yuzeyi de
// ayni sinif, yuzey Kotlin host'tan). Render pass: depth prepass -> renk
// (subpass zinciri, transient depth), renk PRESENT'e cikar. Ucuslu kare 2.
// Yeniden boyutlanma: OUT_OF_DATE/SUBOPTIMAL -> recreate(). Present FIFO
// (vsync; A7: kilitli kare > oynak).
#pragma once
#include <cstdint>

#include "core/memory/arena.hpp"
#include "rhi/device.hpp"

namespace tulpar::engine::rhi {

struct FrameContext {
  VkCommandBuffer cmd = VK_NULL_HANDLE;
  uint32_t image_index = 0;
  uint32_t frame_index = 0; // 0..kFramesInFlight-1
  VkExtent2D extent{};
  VkFramebuffer framebuffer = VK_NULL_HANDLE;
};

class Swapchain {
public:
  static constexpr uint32_t kFramesInFlight = 2;
  bool init(Device &dev, Arena &arena, VkSurfaceKHR surface, uint32_t width, uint32_t height);
  void shutdown();
  // Pencere boyutu degisince (ya da acquire OUT_OF_DATE deyince).
  bool recreate(uint32_t width, uint32_t height);

  // Kare: acquire + komut tamponu baslat + render pass baslat (subpass 0).
  // false = yeniden kurulmali (recreate cagir) ya da hata.
  bool begin_frame(FrameContext *out);
  // Render pass bitir + gonder + sun. false = OUT_OF_DATE (recreate).
  bool end_frame(const FrameContext &fc);

  VkRenderPass render_pass() const { return rp_; }
  VkFormat color_format() const { return format_; }
  VkExtent2D extent() const { return extent_; }
  uint32_t image_count() const { return image_count_; }
  bool needs_recreate() const { return needs_recreate_; }
  uint64_t frames_presented() const { return frames_; }

private:
  bool create_swapchain(uint32_t w, uint32_t h);
  void destroy_swapchain();
  bool create_render_pass();

  Device *dev_ = nullptr;
  VkSurfaceKHR surface_ = VK_NULL_HANDLE;
  VkSwapchainKHR swap_ = VK_NULL_HANDLE;
  VkFormat format_ = VK_FORMAT_B8G8R8A8_UNORM;
  VkExtent2D extent_{};
  uint32_t image_count_ = 0;
  static constexpr uint32_t kMaxImages = 8;
  VkImage images_[kMaxImages] = {};
  VkImageView views_[kMaxImages] = {};
  VkFramebuffer fbs_[kMaxImages] = {};
  VkImage depth_ = VK_NULL_HANDLE;
  VkImageView depth_view_ = VK_NULL_HANDLE;
  VkRenderPass rp_ = VK_NULL_HANDLE;
  // ucuslu kare
  VkCommandBuffer cmds_[kFramesInFlight] = {};
  VkSemaphore acquire_sem_[kFramesInFlight] = {};
  VkSemaphore render_sem_[kMaxImages] = {};
  VkFence fences_[kFramesInFlight] = {};
  uint32_t frame_ = 0;
  uint64_t frames_ = 0;
  bool needs_recreate_ = false;
};

} // namespace tulpar::engine::rhi
