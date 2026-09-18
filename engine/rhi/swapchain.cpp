#include "rhi/swapchain.hpp"
#include "rhi/tile_budget.hpp"

#include <cstdio>

namespace tulpar::engine::rhi {

ResizeAction swapchain_resize_action(VkExtent2D requested, uint32_t win_w, uint32_t win_h, bool out_of_date) {
  // Kucultulmus pencere: 0 olculu swapchain yaratilamaz, karar YOK (cagiran
  // zaten cizmiyor). OUT_OF_DATE bayragi da burada tutulur, kaybolmaz.
  if (win_w == 0 || win_h == 0) return ResizeAction::None;
  if (out_of_date) return ResizeAction::OutOfDate;
  if (requested.width != win_w || requested.height != win_h) return ResizeAction::SizeChanged;
  return ResizeAction::None;
}

const char *resize_action_str(ResizeAction a) {
  switch (a) {
  case ResizeAction::OutOfDate: return "OUT_OF_DATE";
  case ResizeAction::SizeChanged: return "pencere olcusu degisti";
  default: return "-";
  }
}

bool Swapchain::init(Device &dev, Arena &, VkSurfaceKHR surface, uint32_t w, uint32_t h, const SwapchainConfig &cfg) {
  dev_ = &dev;
  surface_ = surface;
  cfg_ = cfg;
  VkApi &a = dev.api();
  // Format: BGRA8 UNORM tercih (yaygin), yoksa ilk.
  uint32_t n = 0;
  a.vkGetPhysicalDeviceSurfaceFormatsKHR(dev.physical(), surface, &n, nullptr);
  VkSurfaceFormatKHR fmts[32];
  if (n > 32) n = 32;
  a.vkGetPhysicalDeviceSurfaceFormatsKHR(dev.physical(), surface, &n, fmts);
  format_ = n ? fmts[0].format : VK_FORMAT_B8G8R8A8_UNORM;
  bool found = false;
  if (cfg_.srgb)
    for (uint32_t i = 0; i < n && !found; i++)
      if (fmts[i].format == VK_FORMAT_B8G8R8A8_SRGB || fmts[i].format == VK_FORMAT_R8G8B8A8_SRGB) { format_ = fmts[i].format; found = true; }
  for (uint32_t i = 0; i < n && !found; i++)
    if (fmts[i].format == VK_FORMAT_B8G8R8A8_UNORM || fmts[i].format == VK_FORMAT_R8G8B8A8_UNORM) { format_ = fmts[i].format; found = true; }
  if (!create_render_pass()) return false;
  // Senkron nesneleri (kare basina)
  VkSemaphoreCreateInfo si{};
  si.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
  VkFenceCreateInfo fi{};
  fi.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
  fi.flags = VK_FENCE_CREATE_SIGNALED_BIT;
  VkCommandBufferAllocateInfo ai{};
  ai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
  ai.commandPool = dev.command_pool();
  ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  ai.commandBufferCount = kFramesInFlight;
  if (a.vkAllocateCommandBuffers(dev.handle(), &ai, cmds_) != VK_SUCCESS) return false;
  for (uint32_t i = 0; i < kFramesInFlight; i++) {
    if (a.vkCreateSemaphore(dev.handle(), &si, nullptr, &acquire_sem_[i]) != VK_SUCCESS) return false;
    if (a.vkCreateFence(dev.handle(), &fi, nullptr, &fences_[i]) != VK_SUCCESS) return false;
  }
  for (uint32_t i = 0; i < kMaxImages; i++)
    if (a.vkCreateSemaphore(dev.handle(), &si, nullptr, &render_sem_[i]) != VK_SUCCESS) return false;
  return create_swapchain(w, h);
}

bool Swapchain::create_render_pass() {
  VkApi &a = dev_->api();
  { // Mali tile butcesi: asim = init hatasi (tile_budget.hpp).
    const VkFormat fmts[2] = {format_, VK_FORMAT_D32_SFLOAT};
    const TileBudget tb = tile_budget(fmts, 2);
    if (!tb.ok) { std::fprintf(stderr, "[swapchain] %s\n", tb.error); return false; }
  }
  VkAttachmentDescription att[2]{};
  att[0].format = format_;
  att[0].samples = VK_SAMPLE_COUNT_1_BIT;
  att[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
  att[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
  att[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
  att[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
  att[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  att[0].finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
  att[1] = att[0];
  att[1].format = VK_FORMAT_D32_SFLOAT;
  att[1].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE; // transient: tile'da kalir
  att[1].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
  VkAttachmentReference cr{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
  VkAttachmentReference dr{1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
  VkSubpassDescription sp[2]{};
  sp[0].pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
  sp[0].pDepthStencilAttachment = &dr;
  sp[1].pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
  sp[1].colorAttachmentCount = 1;
  sp[1].pColorAttachments = &cr;
  sp[1].pDepthStencilAttachment = &dr;
  VkSubpassDependency dep[4]{};
  dep[0].srcSubpass = VK_SUBPASS_EXTERNAL; dep[0].dstSubpass = 0;
  dep[0].srcStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
  dep[0].dstStageMask = dep[0].srcStageMask;
  dep[0].dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
  dep[1].srcSubpass = 0; dep[1].dstSubpass = 1;
  dep[1].srcStageMask = dep[0].srcStageMask;
  dep[1].srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
  dep[1].dstStageMask = dep[0].srcStageMask;
  dep[1].dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
  dep[1].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;
  dep[2].srcSubpass = VK_SUBPASS_EXTERNAL; dep[2].dstSubpass = 1;
  dep[2].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
  dep[2].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
  dep[2].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
  dep[3].srcSubpass = 1; dep[3].dstSubpass = VK_SUBPASS_EXTERNAL;
  dep[3].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
  dep[3].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
  dep[3].dstStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
  VkRenderPassCreateInfo ci{};
  ci.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
  ci.attachmentCount = 2; ci.pAttachments = att;
  ci.subpassCount = 2; ci.pSubpasses = sp;
  ci.dependencyCount = 4; ci.pDependencies = dep;
  return a.vkCreateRenderPass(dev_->handle(), &ci, nullptr, &rp_) == VK_SUCCESS;
}

bool Swapchain::create_swapchain(uint32_t w, uint32_t h) {
  VkApi &a = dev_->api();
  VkSurfaceCapabilitiesKHR caps;
  a.vkGetPhysicalDeviceSurfaceCapabilitiesKHR(dev_->physical(), surface_, &caps);
  // ISTENEN olcu: sync_size karsilastirmasi BUNU kullanir, surucunun dayattigi
  // currentExtent'i DEGIL. X11'de currentExtent bir kare ileride olabilir;
  // istenen olcuyu saklamak "her karede yeniden kur" dongusunu onler.
  requested_ = VkExtent2D{w, h};
  logical_extent_ = caps.currentExtent;
  if (logical_extent_.width == 0xFFFFFFFFu) logical_extent_ = VkExtent2D{w, h};
  if (logical_extent_.width == 0 || logical_extent_.height == 0) return false; // kucultulmus pencere
  // On-dondurme: 90/270'te goruntu olcusu devriktir (Android on-dondurme kurali).
  const bool quarter = (caps.currentTransform & (VK_SURFACE_TRANSFORM_ROTATE_90_BIT_KHR | VK_SURFACE_TRANSFORM_ROTATE_270_BIT_KHR)) != 0;
  extent_ = logical_extent_;
  if (cfg_.prerotate && quarter) { extent_.width = logical_extent_.height; extent_.height = logical_extent_.width; }
  uint32_t want = caps.minImageCount + 1;
  if (caps.maxImageCount && want > caps.maxImageCount) want = caps.maxImageCount;
  if (want > kMaxImages) want = kMaxImages;
  VkSwapchainCreateInfoKHR ci{};
  ci.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
  ci.surface = surface_;
  ci.minImageCount = want;
  ci.imageFormat = format_;
  ci.imageColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
  ci.imageExtent = extent_;
  ci.imageArrayLayers = 1;
  ci.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
  ci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
  // On-dondurme acik: yuzeyin istedigi donusu BIZ uyguluyoruz de (kompozitor
  // dondurmez, SUBOPTIMAL gelmez). Kapali: IDENTITY iste, kompozitor dondursun.
  ci.preTransform = (cfg_.prerotate || !(caps.supportedTransforms & VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR))
                        ? caps.currentTransform
                        : VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
  pretransform_ = ci.preTransform;
  // Yuzeyin destekledigi ilk birlestirme kipi: Huawei/Android 10 OPAQUE vermiyor,
  // yalniz INHERIT (dogrulama hatasi olarak telefonda goruldu 2026-09-14).
  {
    const VkCompositeAlphaFlagBitsKHR order[4] = {VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR, VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR,
                                                 VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR,
                                                 VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR};
    ci.compositeAlpha = order[0];
    for (int i = 0; i < 4; i++)
      if (caps.supportedCompositeAlpha & order[i]) { ci.compositeAlpha = order[i]; break; }
  }
  // Sunum kipi: istenen varsa o, yoksa FIFO (cekirdek, her zaman var).
  present_mode_ = VK_PRESENT_MODE_FIFO_KHR;
  if (cfg_.preferred_present_mode != VK_PRESENT_MODE_FIFO_KHR) {
    uint32_t pmn = 0;
    a.vkGetPhysicalDeviceSurfacePresentModesKHR(dev_->physical(), surface_, &pmn, nullptr);
    VkPresentModeKHR pms[8];
    if (pmn > 8) pmn = 8;
    a.vkGetPhysicalDeviceSurfacePresentModesKHR(dev_->physical(), surface_, &pmn, pms);
    for (uint32_t i = 0; i < pmn; i++)
      if (pms[i] == cfg_.preferred_present_mode) { present_mode_ = cfg_.preferred_present_mode; break; }
  }
  ci.presentMode = present_mode_;
  ci.clipped = VK_TRUE;
  ci.oldSwapchain = swap_;
  VkSwapchainKHR ns = VK_NULL_HANDLE;
  if (a.vkCreateSwapchainKHR(dev_->handle(), &ci, nullptr, &ns) != VK_SUCCESS) return false;
  if (cfg_.hooks.on_create) cfg_.hooks.on_create(cfg_.hooks.user, dev_->physical(), dev_->handle(), dev_->queue(), dev_->queue_family(), ns);
  destroy_swapchain();
  swap_ = ns;
  image_count_ = kMaxImages;
  a.vkGetSwapchainImagesKHR(dev_->handle(), swap_, &image_count_, images_);
  // depth (transient, tercihen lazily allocated)
  VkImageCreateInfo ii{};
  ii.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
  ii.imageType = VK_IMAGE_TYPE_2D;
  ii.format = VK_FORMAT_D32_SFLOAT;
  ii.extent = {extent_.width, extent_.height, 1};
  ii.mipLevels = 1; ii.arrayLayers = 1;
  ii.samples = VK_SAMPLE_COUNT_1_BIT;
  ii.tiling = VK_IMAGE_TILING_OPTIMAL;
  ii.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT;
  if (a.vkCreateImage(dev_->handle(), &ii, nullptr, &depth_) != VK_SUCCESS) return false;
  VkMemoryRequirements req;
  a.vkGetImageMemoryRequirements(dev_->handle(), depth_, &req);
  // Derinlik belleginin omru PENCEREYE bagli: her yeniden boyutlandirmada serbest
  // birakilabilmeli (blok ayirici bump'tir, geri vermez).
  if (!dev_->allocate_dedicated(req, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, true, &depth_mem_)) return false;
  a.vkBindImageMemory(dev_->handle(), depth_, depth_mem_.memory, depth_mem_.offset);
  VkImageViewCreateInfo vi{};
  vi.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
  vi.image = depth_;
  vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
  vi.format = VK_FORMAT_D32_SFLOAT;
  vi.subresourceRange = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1};
  if (a.vkCreateImageView(dev_->handle(), &vi, nullptr, &depth_view_) != VK_SUCCESS) return false;
  for (uint32_t i = 0; i < image_count_; i++) {
    VkImageViewCreateInfo ci2{};
    ci2.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    ci2.image = images_[i];
    ci2.viewType = VK_IMAGE_VIEW_TYPE_2D;
    ci2.format = format_;
    ci2.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    if (a.vkCreateImageView(dev_->handle(), &ci2, nullptr, &views_[i]) != VK_SUCCESS) return false;
    VkImageView atts[2] = {views_[i], depth_view_};
    VkFramebufferCreateInfo fi{};
    fi.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    fi.renderPass = rp_;
    fi.attachmentCount = 2;
    fi.pAttachments = atts;
    fi.width = extent_.width; fi.height = extent_.height; fi.layers = 1;
    if (a.vkCreateFramebuffer(dev_->handle(), &fi, nullptr, &fbs_[i]) != VK_SUCCESS) return false;
  }
  needs_recreate_ = false;
  return true;
}

void Swapchain::destroy_swapchain() {
  VkApi &a = dev_->api();
  for (uint32_t i = 0; i < image_count_; i++) {
    if (fbs_[i]) a.vkDestroyFramebuffer(dev_->handle(), fbs_[i], nullptr);
    if (views_[i]) a.vkDestroyImageView(dev_->handle(), views_[i], nullptr);
    fbs_[i] = VK_NULL_HANDLE; views_[i] = VK_NULL_HANDLE;
  }
  if (depth_view_) a.vkDestroyImageView(dev_->handle(), depth_view_, nullptr);
  if (depth_) a.vkDestroyImage(dev_->handle(), depth_, nullptr);
  depth_view_ = VK_NULL_HANDLE; depth_ = VK_NULL_HANDLE;
  dev_->free_dedicated(&depth_mem_); // yeniden boyutlandirma sizdirmasin
  if (swap_ && cfg_.hooks.on_destroy) cfg_.hooks.on_destroy(cfg_.hooks.user, dev_->handle(), swap_);
  if (swap_) a.vkDestroySwapchainKHR(dev_->handle(), swap_, nullptr);
  swap_ = VK_NULL_HANDLE;
  image_count_ = 0;
}

float Swapchain::rotation_radians() const {
  switch (pretransform_) {
  case VK_SURFACE_TRANSFORM_ROTATE_90_BIT_KHR: return 1.57079632679489662f;
  case VK_SURFACE_TRANSFORM_ROTATE_180_BIT_KHR: return 3.14159265358979324f;
  case VK_SURFACE_TRANSFORM_ROTATE_270_BIT_KHR: return 4.71238898038468986f;
  default: return 0.0f;
  }
}

bool Swapchain::recreate(uint32_t w, uint32_t h) {
  dev_->api().vkDeviceWaitIdle(dev_->handle());
  recreates_++;
  return create_swapchain(w, h);
}

bool Swapchain::sync_size(uint32_t w, uint32_t h) {
  if (!dev_) return false;
  last_action_ = swapchain_resize_action(requested_, w, h, needs_recreate_);
  if (last_action_ == ResizeAction::None) return false;
  return recreate(w, h);
}

void Swapchain::shutdown() {
  if (!dev_) return;
  VkApi &a = dev_->api();
  a.vkDeviceWaitIdle(dev_->handle());
  destroy_swapchain();
  for (uint32_t i = 0; i < kFramesInFlight; i++) {
    if (acquire_sem_[i]) a.vkDestroySemaphore(dev_->handle(), acquire_sem_[i], nullptr);
    if (fences_[i]) a.vkDestroyFence(dev_->handle(), fences_[i], nullptr);
  }
  for (uint32_t i = 0; i < kMaxImages; i++) if (render_sem_[i]) a.vkDestroySemaphore(dev_->handle(), render_sem_[i], nullptr);
  if (rp_) a.vkDestroyRenderPass(dev_->handle(), rp_, nullptr);
  a.vkFreeCommandBuffers(dev_->handle(), dev_->command_pool(), kFramesInFlight, cmds_);
  dev_ = nullptr;
}

bool Swapchain::begin_frame(FrameContext *out) {
  if (!acquire(out)) return false;
  begin_render_pass(*out);
  return true;
}

bool Swapchain::acquire(FrameContext *out) {
  VkApi &a = dev_->api();
  uint32_t f = frame_;
  a.vkWaitForFences(dev_->handle(), 1, &fences_[f], VK_TRUE, UINT64_MAX);
  uint32_t idx = 0;
  VkResult r = a.vkAcquireNextImageKHR(dev_->handle(), swap_, UINT64_MAX, acquire_sem_[f], VK_NULL_HANDLE, &idx);
  if (r == VK_ERROR_OUT_OF_DATE_KHR) { needs_recreate_ = true; return false; }
  if (r == VK_SUBOPTIMAL_KHR) suboptimal_++;
  if (r != VK_SUCCESS && r != VK_SUBOPTIMAL_KHR) return false;
  // Fence gonderimden hemen once sifirlanir (end_frame): burada sifirlayip
  // gonderim basarisiz olursa bir sonraki bekleme sonsuza kadar takilir.
  VkCommandBuffer cb = cmds_[f];
  a.vkResetCommandBuffer(cb, 0);
  VkCommandBufferBeginInfo bi{};
  bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
  bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
  a.vkBeginCommandBuffer(cb, &bi);
  out->cmd = cb;
  out->image_index = idx;
  out->frame_index = f;
  out->extent = extent_;
  out->framebuffer = fbs_[idx];
  return true;
}

// Render pass'i AYRI baslatir: arada golge gecisi gibi kendi pass'i olan isler
// kaydedilebilsin (bir render pass'in icinde baska pass baslatilamaz).
void Swapchain::begin_render_pass(const FrameContext &fc) {
  VkApi &a = dev_->api();
  VkCommandBuffer cb = fc.cmd;
  VkClearValue clears[2]{};
  clears[0].color = {{0.05f, 0.06f, 0.09f, 1.0f}};
  clears[1].depthStencil = {1.0f, 0};
  VkRenderPassBeginInfo rbi{};
  rbi.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
  rbi.renderPass = rp_;
  rbi.framebuffer = fc.framebuffer;
  rbi.renderArea = {{0, 0}, extent_};
  rbi.clearValueCount = 2;
  rbi.pClearValues = clears;
  a.vkCmdBeginRenderPass(cb, &rbi, VK_SUBPASS_CONTENTS_INLINE);
  VkViewport vp{0, 0, (float)extent_.width, (float)extent_.height, 0.0f, 1.0f};
  VkRect2D sc{{0, 0}, extent_};
  a.vkCmdSetViewport(cb, 0, 1, &vp);
  a.vkCmdSetScissor(cb, 0, 1, &sc);
}

bool Swapchain::end_frame(const FrameContext &fc) {
  VkApi &a = dev_->api();
  a.vkCmdEndRenderPass(fc.cmd);
  a.vkEndCommandBuffer(fc.cmd);
  VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
  VkSubmitInfo si{};
  si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
  si.waitSemaphoreCount = 1;
  si.pWaitSemaphores = &acquire_sem_[fc.frame_index];
  si.pWaitDstStageMask = &wait_stage;
  si.commandBufferCount = 1;
  si.pCommandBuffers = &fc.cmd;
  si.signalSemaphoreCount = 1;
  si.pSignalSemaphores = &render_sem_[fc.image_index];
  a.vkResetFences(dev_->handle(), 1, &fences_[fc.frame_index]);
  if (a.vkQueueSubmit(dev_->queue(), 1, &si, fences_[fc.frame_index]) != VK_SUCCESS) {
    // Fence sinyallenmeyecek: bir sonraki begin_frame takilmasin diye idle bekle.
    a.vkDeviceWaitIdle(dev_->handle());
    VkFenceCreateInfo fi{};
    fi.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fi.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    a.vkDestroyFence(dev_->handle(), fences_[fc.frame_index], nullptr);
    a.vkCreateFence(dev_->handle(), &fi, nullptr, &fences_[fc.frame_index]);
    return false;
  }
  VkPresentInfoKHR pi{};
  pi.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
  pi.waitSemaphoreCount = 1;
  pi.pWaitSemaphores = &render_sem_[fc.image_index];
  pi.swapchainCount = 1;
  pi.pSwapchains = &swap_;
  pi.pImageIndices = &fc.image_index;
  VkResult r = cfg_.hooks.present ? cfg_.hooks.present(cfg_.hooks.user, dev_->queue(), &pi) : a.vkQueuePresentKHR(dev_->queue(), &pi);
  frame_ = (frame_ + 1) % kFramesInFlight;
  frames_++;
  // SUBOPTIMAL yeniden yaratma SEBEBI DEGIL: Android'de swapchain preTransform'u
  // yuzeyin currentTransform'undan farkliysa (biz IDENTITY veriyoruz, kompozitor
  // donduruyor) her kare SUBOPTIMAL doner — bunu recreate sayan kod her karede
  // swapchain'i yeniden kurar (olculdu: 45 ms/kare, 20 fps). Sayilir, gorunur
  // olur, on-dondurme ile kapanir. Tuzaklar 8m.
  if (r == VK_ERROR_OUT_OF_DATE_KHR) { needs_recreate_ = true; return false; }
  if (r == VK_SUBOPTIMAL_KHR) { suboptimal_++; return true; }
  return r == VK_SUCCESS;
}

} // namespace tulpar::engine::rhi
