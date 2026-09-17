#include "app/editor_viewport.hpp"

#include <cstdio>

#include <imgui.h>
#include <imgui_impl_vulkan.h>

#include "rhi/tile_budget.hpp"

namespace tulpar::engine::app {

namespace {
// ImGui'nin Vulkan arka ucu KURULU mu. ImGui_ImplVulkan_AddTexture arka uc
// verisini dogrudan dereference eder; kurulmamisken cagirmak cokme demektir
// (headless editor ve testler tam olarak bu durumda). Sessizce atlanir,
// texture_id() 0 doner ve cagiran paneli cizmez.
bool imgui_vulkan_ready() {
  return ImGui::GetCurrentContext() != nullptr && ImGui::GetIO().BackendRendererUserData != nullptr;
}
uint32_t clamp_u32(uint32_t v, uint32_t lo, uint32_t hi) { return v < lo ? lo : (v > hi ? hi : v); }
} // namespace

ViewportPick viewport_map_mouse(const ViewportRect &panel, float mouse_x, float mouse_y, uint32_t tex_w, uint32_t tex_h) {
  ViewportPick p; // varsayilan: gecersiz, -1 (bkz. baslikta SOZLESME)
  if (!(panel.w > 0.0f) || !(panel.h > 0.0f) || tex_w == 0 || tex_h == 0) return p;
  const float dx = mouse_x - panel.x, dy = mouse_y - panel.y;
  // Kontrol OLUMLU yazildi ("icerde olmasi gereken sart saglaniyor mu"), cunku
  // olumsuz bicim NaN'i ELEMIYORDU: `dx < 0` ve `dx >= panel.w` NaN icin ikisi
  // de false, yani NaN bir fare konumu GECERLI secim sayilip NaN isin
  // atiliyordu (olculdu). Olumlu bicimde NaN her kosulu dusuruyor. Normal
  // noktalarda davranis BIREBIR ayni; yari-acik aralik korunuyor (sag/alt disarida).
  if (!(dx >= 0.0f) || !(dy >= 0.0f) || !(dx < panel.w) || !(dy < panel.h)) return p;
  p.valid = true;
  p.u = dx / panel.w;
  p.v = dy / panel.h;
  p.x = p.u * (float)tex_w;
  p.y = p.v * (float)tex_h;
  // Kayan nokta yuvarlamasi son pikseli tasirmasin (u < 1 ama u*w == w olabilir).
  if (p.x >= (float)tex_w) p.x = (float)tex_w - 1.0f;
  if (p.y >= (float)tex_h) p.y = (float)tex_h - 1.0f;
  return p;
}

ViewportPick EditorViewport::map_mouse(const ViewportRect &panel, float mouse_x, float mouse_y) const {
  if (!ok_) return ViewportPick{};
  return viewport_map_mouse(panel, mouse_x, mouse_y, w_, h_);
}

bool EditorViewport::fail(const char *what, VkResult r) {
  std::snprintf(err_, sizeof err_, "%s: %s", what, rhi::vk_result_str(r));
  return false;
}

// Gecis: yapisi swapchain'inkiyle ayni — 2 subpass (derinlik on-gecisi -> renk),
// transient derinlik — ama BU GECIS renderer'in boru hatlarini kuran gecistir
// (bkz. baslikta KURULUM SIRASI). Bagimliliklar swapchain'inkinin kopyasi
// DEGIL: burada son tuketici ImGui'nin fragment ornekleyicisi, sunum degil.
//
// TUZAK (olculdu 2026-09-17, dogrulama katmani, RTX 5080): gecis uyumlulugu
// subpass BAGIMLILIKLARINI da kapsar — "otherwise identical except for
// initial/final layout, load/store ops, attachment reference layouts". Yani
// asagidaki dep[2]/dep[3] degistirilirse bu gecise gore kurulmus boru hatlari
// BASKA bir gecisle (offscreen/swapchain) kaydedilemez ve tersi de dogrudur;
// katman vkCmdDrawIndexed'de "pDependencies[2].srcStageMask is incompatible"
// der. Bicimler tutuyor diye uyumlu SANMAK bu ailenin klasik hatasi.
bool EditorViewport::create_render_pass() {
  rhi::VkApi &a = dev_->api();
  { // Mali tile butcesi (rhi/tile_budget.hpp): asim = hata, sessiz gecis yok.
    const VkFormat fmts[2] = {cfg_.color_format, cfg_.depth_format};
    const rhi::TileBudget tb = rhi::tile_budget(fmts, 2);
    if (!tb.ok) {
      std::snprintf(err_, sizeof err_, "%s", tb.error);
      return false;
    }
  }
  VkAttachmentDescription att[2]{};
  att[0].format = cfg_.color_format;
  att[0].samples = VK_SAMPLE_COUNT_1_BIT;
  att[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
  att[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
  att[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
  att[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
  att[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  att[0].finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
  att[1] = att[0];
  att[1].format = cfg_.depth_format;
  att[1].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE; // transient: tile'da kalir, DRAM'e inmez
  att[1].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
  VkAttachmentReference cr{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
  VkAttachmentReference dr{1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
  VkSubpassDescription sp[2]{};
  sp[0].pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
  sp[0].pDepthStencilAttachment = &dr; // derinlik on-gecisi: renk yok
  sp[1].pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
  sp[1].colorAttachmentCount = 1;
  sp[1].pColorAttachments = &cr;
  sp[1].pDepthStencilAttachment = &dr;
  VkSubpassDependency dep[4]{};
  // disari -> 0: derinlik yazimi
  dep[0].srcSubpass = VK_SUBPASS_EXTERNAL;
  dep[0].dstSubpass = 0;
  dep[0].srcStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
  dep[0].dstStageMask = dep[0].srcStageMask;
  dep[0].dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
  // 0 -> 1: derinlik yaz -> derinlik oku (BY_REGION: tile icinde kalir)
  dep[1].srcSubpass = 0;
  dep[1].dstSubpass = 1;
  dep[1].srcStageMask = dep[0].srcStageMask;
  dep[1].srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
  dep[1].dstStageMask = dep[0].srcStageMask;
  dep[1].dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
  dep[1].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;
  // disari -> 1: ONCEKI karenin ImGui ornekleme okumasi BITMEDEN bu karenin
  // renk yazimi baslamasin (WAR). Doku ucuslu kare sayisinca cogaltilmiyor,
  // TEK hedef var; bu bagimlilik olmadan iki kare ustuste biner ve panelde
  // yirtilma gorunur.
  dep[2].srcSubpass = VK_SUBPASS_EXTERNAL;
  dep[2].dstSubpass = 1;
  dep[2].srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
  dep[2].srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
  dep[2].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
  dep[2].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
  // 1 -> disari: renk yazimi -> ImGui'nin fragment ornekleme okumasi. AYNI
  // komut tamponunda swapchain gecisi geliyor; bu olmadan panel bayat/tanimsiz
  // piksel gosterir.
  dep[3].srcSubpass = 1;
  dep[3].dstSubpass = VK_SUBPASS_EXTERNAL;
  dep[3].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
  dep[3].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
  dep[3].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
  dep[3].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
  VkRenderPassCreateInfo ci{};
  ci.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
  ci.attachmentCount = 2;
  ci.pAttachments = att;
  ci.subpassCount = 2;
  ci.pSubpasses = sp;
  ci.dependencyCount = 4;
  ci.pDependencies = dep;
  const VkResult r = a.vkCreateRenderPass(dev_->handle(), &ci, nullptr, &rp_);
  if (r != VK_SUCCESS) return fail("vkCreateRenderPass", r);
  return true;
}

bool EditorViewport::create_targets(uint32_t w, uint32_t h) {
  rhi::VkApi &a = dev_->api();
  VkDevice d = dev_->handle();
  // --- renk: attachment + ORNEKLENEBILIR (ImGui) + transfer kaynagi (headless
  // ekran goruntusu isteyen cagiran icin; kullanilmazsa maliyeti yok).
  {
    VkImageCreateInfo ii{};
    ii.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    ii.imageType = VK_IMAGE_TYPE_2D;
    ii.format = cfg_.color_format;
    ii.extent = {w, h, 1};
    ii.mipLevels = 1;
    ii.arrayLayers = 1;
    ii.samples = VK_SAMPLE_COUNT_1_BIT;
    ii.tiling = VK_IMAGE_TILING_OPTIMAL;
    ii.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    ii.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    VkResult r = a.vkCreateImage(d, &ii, nullptr, &color_);
    if (r != VK_SUCCESS) return fail("vkCreateImage (renk)", r);
    VkMemoryRequirements req;
    a.vkGetImageMemoryRequirements(d, color_, &req);
    // allocate_dedicated: blok ayirici BUMP'tir, geri vermez — omru PANELE bagli
    // olan hedef her yeniden boyutlandirmada 64 MB'lik bloklari yerdi.
    if (!dev_->allocate_dedicated(req, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, false, &color_mem_)) {
      std::snprintf(err_, sizeof err_, "renk bellegi: %s", dev_->last_error());
      return false;
    }
    r = a.vkBindImageMemory(d, color_, color_mem_.memory, color_mem_.offset);
    if (r != VK_SUCCESS) return fail("vkBindImageMemory (renk)", r);
    VkImageViewCreateInfo vi{};
    vi.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    vi.image = color_;
    vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
    vi.format = cfg_.color_format;
    vi.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    r = a.vkCreateImageView(d, &vi, nullptr, &color_view_);
    if (r != VK_SUCCESS) return fail("vkCreateImageView (renk)", r);
  }
  // --- derinlik: transient (STORE_OP_DONT_CARE) + mumkunse LAZILY_ALLOCATED:
  // TBDR'da tile'da kalir, DRAM'e hic inmez (plan §1.2).
  {
    VkImageCreateInfo ii{};
    ii.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    ii.imageType = VK_IMAGE_TYPE_2D;
    ii.format = cfg_.depth_format;
    ii.extent = {w, h, 1};
    ii.mipLevels = 1;
    ii.arrayLayers = 1;
    ii.samples = VK_SAMPLE_COUNT_1_BIT;
    ii.tiling = VK_IMAGE_TILING_OPTIMAL;
    ii.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT;
    ii.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    VkResult r = a.vkCreateImage(d, &ii, nullptr, &depth_);
    if (r != VK_SUCCESS) return fail("vkCreateImage (derinlik)", r);
    VkMemoryRequirements req;
    a.vkGetImageMemoryRequirements(d, depth_, &req);
    if (!dev_->allocate_dedicated(req, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, true, &depth_mem_)) {
      std::snprintf(err_, sizeof err_, "derinlik bellegi: %s", dev_->last_error());
      return false;
    }
    r = a.vkBindImageMemory(d, depth_, depth_mem_.memory, depth_mem_.offset);
    if (r != VK_SUCCESS) return fail("vkBindImageMemory (derinlik)", r);
    VkImageViewCreateInfo vi{};
    vi.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    vi.image = depth_;
    vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
    vi.format = cfg_.depth_format;
    vi.subresourceRange = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1};
    r = a.vkCreateImageView(d, &vi, nullptr, &depth_view_);
    if (r != VK_SUCCESS) return fail("vkCreateImageView (derinlik)", r);
  }
  {
    VkImageView atts[2] = {color_view_, depth_view_};
    VkFramebufferCreateInfo fi{};
    fi.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    fi.renderPass = rp_;
    fi.attachmentCount = 2;
    fi.pAttachments = atts;
    fi.width = w;
    fi.height = h;
    fi.layers = 1;
    const VkResult r = a.vkCreateFramebuffer(d, &fi, nullptr, &fb_);
    if (r != VK_SUCCESS) return fail("vkCreateFramebuffer", r);
  }
  // ImGui dokusu: 1.92'nin arka ucu SAMPLED_IMAGE descriptor'i kullanir ve
  // ornekleyiciyi KENDISI tutar (tek, dogrusal suzmeli; eski uc argumanli
  // AddTexture imzasi ornekleyiciyi yok sayiyor ve zaten
  // IMGUI_DISABLE_OBSOLETE_FUNCTIONS ile kapali). Bu yuzden burada VkSampler
  // YARATILMAZ: yaratilsa da hicbir yere baglanmazdi.
  if (cfg_.imgui_texture && imgui_vulkan_ready()) {
    tex_set_ = ImGui_ImplVulkan_AddTexture(color_view_, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    if (tex_set_ == VK_NULL_HANDLE) {
      std::snprintf(err_, sizeof err_, "ImGui_ImplVulkan_AddTexture: descriptor havuzu doldu (DescriptorPoolSize)");
      return false;
    }
  }
  w_ = w;
  h_ = h;
  recreates_++;
  return true;
}

void EditorViewport::destroy_targets() {
  if (!dev_) return;
  rhi::VkApi &a = dev_->api();
  VkDevice d = dev_->handle();
  // Ucusta kare olabilir: descriptor set ve imgeler HALA kullanimda olabilir.
  // Yeniden boyutlandirma nadir (yalniz gercek olcu degisiminde), bekleme burada
  // dogru takas: karesiz bir duraklama, bozuk bir karenin yanina yazmaktan iyidir.
  a.vkDeviceWaitIdle(d);
  if (tex_set_) {
    // ImGui arka ucu kapanmis olabilir (yanlis kapanis sirasi): o zaman
    // dokunma — descriptor havuzu zaten yok edilmistir.
    if (imgui_vulkan_ready()) ImGui_ImplVulkan_RemoveTexture(tex_set_);
    tex_set_ = VK_NULL_HANDLE;
  }
  if (fb_) a.vkDestroyFramebuffer(d, fb_, nullptr);
  if (color_view_) a.vkDestroyImageView(d, color_view_, nullptr);
  if (depth_view_) a.vkDestroyImageView(d, depth_view_, nullptr);
  if (color_) a.vkDestroyImage(d, color_, nullptr);
  if (depth_) a.vkDestroyImage(d, depth_, nullptr);
  fb_ = VK_NULL_HANDLE;
  color_view_ = depth_view_ = VK_NULL_HANDLE;
  color_ = depth_ = VK_NULL_HANDLE;
  dev_->free_dedicated(&color_mem_);
  dev_->free_dedicated(&depth_mem_);
  w_ = h_ = 0;
}

bool EditorViewport::init(rhi::Device &dev, const EditorViewportConfig &cfg, uint32_t width, uint32_t height) {
  dev_ = &dev;
  cfg_ = cfg;
  ok_ = false;
  err_[0] = 0;
  rhi::VkApi &a = dev.api();
  // Cihaz limiti: panel suruklenerek limitin ustune cikarsa imge yaratma
  // basarisiz olurdu. Kenetleme bilerek SESSIZ degil — width()/height() gercek
  // olcuyu doner, cagiran en-boy oranini ondan hesaplar.
  if (a.vkGetPhysicalDeviceProperties2) {
    VkPhysicalDeviceProperties2 p2{};
    p2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
    a.vkGetPhysicalDeviceProperties2(dev.physical(), &p2);
    const VkPhysicalDeviceLimits &l = p2.properties.limits;
    limit_ = l.maxImageDimension2D;
    if (l.maxFramebufferWidth < limit_) limit_ = l.maxFramebufferWidth;
    if (l.maxFramebufferHeight < limit_) limit_ = l.maxFramebufferHeight;
  }
  if (cfg_.max_size && cfg_.max_size < limit_) limit_ = cfg_.max_size;
  if (cfg_.min_size == 0) cfg_.min_size = 1;
  if (limit_ < cfg_.min_size) limit_ = cfg_.min_size;
  // Bicim gercekten hem attachment hem ornekleme destekliyor mu (sessiz
  // dogrulama hatasi yerine acik mesaj).
  if (a.vkGetPhysicalDeviceFormatProperties) {
    VkFormatProperties fp{};
    a.vkGetPhysicalDeviceFormatProperties(dev.physical(), cfg_.color_format, &fp);
    const VkFormatFeatureFlags need = VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT;
    if ((fp.optimalTilingFeatures & need) != need) {
      std::snprintf(err_, sizeof err_, "renk bicimi %d hem renk hedefi hem ornekleme destekleMIYOR", (int)cfg_.color_format);
      return false;
    }
    a.vkGetPhysicalDeviceFormatProperties(dev.physical(), cfg_.depth_format, &fp);
    if (!(fp.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT)) {
      std::snprintf(err_, sizeof err_, "derinlik bicimi %d desteklenMIYOR", (int)cfg_.depth_format);
      return false;
    }
  }
  if (!create_render_pass()) return false;
  const uint32_t w = clamp_u32(width, cfg_.min_size, limit_);
  const uint32_t h = clamp_u32(height, cfg_.min_size, limit_);
  if (!create_targets(w, h)) {
    destroy_targets();
    a.vkDestroyRenderPass(dev.handle(), rp_, nullptr);
    rp_ = VK_NULL_HANDLE;
    return false;
  }
  requests_++;
  ok_ = true;
  return true;
}

void EditorViewport::shutdown() {
  if (!dev_) return;
  destroy_targets();
  if (rp_) dev_->api().vkDestroyRenderPass(dev_->handle(), rp_, nullptr);
  rp_ = VK_NULL_HANDLE;
  ok_ = false;
  dev_ = nullptr;
}

bool EditorViewport::resize(uint32_t width, uint32_t height) {
  if (!ok_) return false;
  requests_++;
  const uint32_t w = clamp_u32(width, cfg_.min_size, limit_);
  const uint32_t h = clamp_u32(height, cfg_.min_size, limit_);
  if (w == w_ && h == h_) return false; // TEK cikis kosulu: gercekten degismediyse dokunma
  destroy_targets();
  if (!create_targets(w, h)) {
    // Yeniden yaratma basarisiz: hedef YOK. begin_pass false doner, cagiran
    // paneli cizmez ve last_error()'u gosterir — sessiz siyah kare yok.
    destroy_targets();
    ok_ = false;
    return false;
  }
  return true;
}

bool EditorViewport::begin_pass(VkCommandBuffer cb) {
  if (!ok_ || fb_ == VK_NULL_HANDLE || cb == VK_NULL_HANDLE) return false;
  rhi::VkApi &a = dev_->api();
  VkClearValue clears[2]{};
  for (int i = 0; i < 4; i++) clears[0].color.float32[i] = cfg_.clear[i];
  clears[1].depthStencil = {1.0f, 0};
  VkRenderPassBeginInfo rbi{};
  rbi.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
  rbi.renderPass = rp_;
  rbi.framebuffer = fb_;
  rbi.renderArea = {{0, 0}, {w_, h_}};
  rbi.clearValueCount = 2;
  rbi.pClearValues = clears;
  a.vkCmdBeginRenderPass(cb, &rbi, VK_SUBPASS_CONTENTS_INLINE);
  const VkViewport vp{0, 0, (float)w_, (float)h_, 0.0f, 1.0f};
  const VkRect2D sc{{0, 0}, {w_, h_}};
  a.vkCmdSetViewport(cb, 0, 1, &vp);
  a.vkCmdSetScissor(cb, 0, 1, &sc);
  return true;
}

void EditorViewport::end_pass(VkCommandBuffer cb) {
  if (!ok_ || cb == VK_NULL_HANDLE) return;
  dev_->api().vkCmdEndRenderPass(cb);
}

} // namespace tulpar::engine::app
