#include "rhi/offscreen.hpp"
#include "rhi/tile_budget.hpp"

#include <cstdio>
#include <cstring>

#include "platform/time.hpp"

#include "rhi/shaders/triangle_frag_spv.h"
#include "rhi/shaders/triangle_vert_spv.h"

namespace tulpar::engine::rhi {

namespace {
struct Ctx {
  Device *dev;
  VkApi *api;
  VkDevice d;
  OffscreenResult *out;
  VkImage color = VK_NULL_HANDLE, depth = VK_NULL_HANDLE;
  VkImageView color_view = VK_NULL_HANDLE, depth_view = VK_NULL_HANDLE;
  VkBuffer readback = VK_NULL_HANDLE;
  MemoryAlloc readback_mem{};
  VkRenderPass rp = VK_NULL_HANDLE;
  VkFramebuffer fb = VK_NULL_HANDLE;
  VkShaderModule vs = VK_NULL_HANDLE, fs = VK_NULL_HANDLE;
  VkPipelineLayout layout = VK_NULL_HANDLE;
  VkPipelineCache cache = VK_NULL_HANDLE;
  VkPipeline pipe_depth = VK_NULL_HANDLE, pipe_color = VK_NULL_HANDLE;
  VkPipeline gpl_libs[4] = {VK_NULL_HANDLE, VK_NULL_HANDLE, VK_NULL_HANDLE, VK_NULL_HANDLE};
  VkQueryPool queries = VK_NULL_HANDLE;

  bool fail(const char *what, VkResult r) {
    std::snprintf(out->error, sizeof out->error, "%s: %s", what, vk_result_str(r));
    return false;
  }
  void cleanup() {
    VkApi &a = *api;
    a.vkDeviceWaitIdle(d);
    if (queries) a.vkDestroyQueryPool(d, queries, nullptr);
    if (pipe_depth) a.vkDestroyPipeline(d, pipe_depth, nullptr);
    if (pipe_color) a.vkDestroyPipeline(d, pipe_color, nullptr);
    for (VkPipeline &l : gpl_libs) if (l) a.vkDestroyPipeline(d, l, nullptr);
    if (cache) a.vkDestroyPipelineCache(d, cache, nullptr);
    if (layout) a.vkDestroyPipelineLayout(d, layout, nullptr);
    if (vs) a.vkDestroyShaderModule(d, vs, nullptr);
    if (fs) a.vkDestroyShaderModule(d, fs, nullptr);
    if (fb) a.vkDestroyFramebuffer(d, fb, nullptr);
    if (rp) a.vkDestroyRenderPass(d, rp, nullptr);
    if (readback) a.vkDestroyBuffer(d, readback, nullptr);
    if (color_view) a.vkDestroyImageView(d, color_view, nullptr);
    if (depth_view) a.vkDestroyImageView(d, depth_view, nullptr);
    if (color) a.vkDestroyImage(d, color, nullptr);
    if (depth) a.vkDestroyImage(d, depth, nullptr);
    // Bellek: Device'in blok ayiricisinda (cihaz omru), burada serbest birakilmaz.
  }
};

bool make_image(Ctx &c, VkFormat fmt, VkImageUsageFlags usage, bool lazily, uint32_t w, uint32_t h,
                VkImage *img, VkImageView *view, VkImageAspectFlags aspect) {
  VkImageCreateInfo ici{};
  ici.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
  ici.imageType = VK_IMAGE_TYPE_2D;
  ici.format = fmt;
  ici.extent = {w, h, 1};
  ici.mipLevels = 1;
  ici.arrayLayers = 1;
  ici.samples = VK_SAMPLE_COUNT_1_BIT;
  ici.tiling = VK_IMAGE_TILING_OPTIMAL;
  ici.usage = usage;
  ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  VkResult r = c.api->vkCreateImage(c.d, &ici, nullptr, img);
  if (r != VK_SUCCESS) return c.fail("vkCreateImage", r);
  VkMemoryRequirements req;
  c.api->vkGetImageMemoryRequirements(c.d, *img, &req);
  MemoryAlloc m;
  if (!c.dev->allocate(req, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, lazily, &m)) {
    std::snprintf(c.out->error, sizeof c.out->error, "image bellek: %s", c.dev->last_error());
    return false;
  }
  r = c.api->vkBindImageMemory(c.d, *img, m.memory, m.offset);
  if (r != VK_SUCCESS) return c.fail("vkBindImageMemory", r);
  VkImageViewCreateInfo vci{};
  vci.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
  vci.image = *img;
  vci.viewType = VK_IMAGE_VIEW_TYPE_2D;
  vci.format = fmt;
  vci.subresourceRange = {aspect, 0, 1, 0, 1};
  r = c.api->vkCreateImageView(c.d, &vci, nullptr, view);
  if (r != VK_SUCCESS) return c.fail("vkCreateImageView", r);
  return true;
}

bool make_render_pass(Ctx &c, VkFormat color_fmt, VkFormat depth_fmt) {
  VkApi &a = *c.api;
  const DeviceCaps &caps = c.dev->caps();
  bool use2 = caps.api_version >= VK_API_VERSION_1_2 && a.vkCreateRenderPass2;

  // Ekler: 0 renk (clear -> store -> TRANSFER_SRC), 1 depth (clear -> DONT_CARE: transient)
  VkAttachmentDescription2 att[2]{};
  att[0].sType = VK_STRUCTURE_TYPE_ATTACHMENT_DESCRIPTION_2;
  att[0].format = color_fmt;
  att[0].samples = VK_SAMPLE_COUNT_1_BIT;
  att[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
  att[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
  att[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
  att[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
  att[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  att[0].finalLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
  att[1] = att[0];
  att[1].format = depth_fmt;
  att[1].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE; // tile'da kalir
  att[1].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

  VkAttachmentReference2 color_ref{};
  color_ref.sType = VK_STRUCTURE_TYPE_ATTACHMENT_REFERENCE_2;
  color_ref.attachment = 0;
  color_ref.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
  color_ref.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  VkAttachmentReference2 depth_ref{};
  depth_ref.sType = VK_STRUCTURE_TYPE_ATTACHMENT_REFERENCE_2;
  depth_ref.attachment = 1;
  depth_ref.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
  depth_ref.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;

  // Birlesme geri bildirimi (uzanti varsa).
  VkRenderPassSubpassFeedbackInfoEXT sp_fb[2]{};
  VkRenderPassSubpassFeedbackCreateInfoEXT sp_fb_ci[2]{};
  VkRenderPassCreationFeedbackInfoEXT rp_fb{};
  VkRenderPassCreationFeedbackCreateInfoEXT rp_fb_ci{};
  bool feedback = caps.ext_subpass_merge_feedback && use2;
  for (int i = 0; i < 2; i++) {
    sp_fb_ci[i].sType = VK_STRUCTURE_TYPE_RENDER_PASS_SUBPASS_FEEDBACK_CREATE_INFO_EXT;
    sp_fb_ci[i].pSubpassFeedback = &sp_fb[i];
  }
  rp_fb_ci.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATION_FEEDBACK_CREATE_INFO_EXT;
  rp_fb_ci.pRenderPassFeedback = &rp_fb;

  VkSubpassDescription2 sp[2]{};
  sp[0].sType = VK_STRUCTURE_TYPE_SUBPASS_DESCRIPTION_2;
  sp[0].pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
  sp[0].pDepthStencilAttachment = &depth_ref; // depth prepass: renk yok
  sp[0].pNext = feedback ? &sp_fb_ci[0] : nullptr;
  sp[1].sType = VK_STRUCTURE_TYPE_SUBPASS_DESCRIPTION_2;
  sp[1].pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
  sp[1].colorAttachmentCount = 1;
  sp[1].pColorAttachments = &color_ref;
  sp[1].pDepthStencilAttachment = &depth_ref;
  sp[1].pNext = feedback ? &sp_fb_ci[1] : nullptr;

  VkSubpassDependency2 dep[4]{};
  for (auto &x : dep) x.sType = VK_STRUCTURE_TYPE_SUBPASS_DEPENDENCY_2;
  // disari -> 0: depth yazimi
  dep[0].srcSubpass = VK_SUBPASS_EXTERNAL; dep[0].dstSubpass = 0;
  dep[0].srcStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
  dep[0].dstStageMask = dep[0].srcStageMask;
  dep[0].dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
  // 0 -> 1: depth yaz -> depth oku (BY_REGION: tile icinde)
  dep[1].srcSubpass = 0; dep[1].dstSubpass = 1;
  dep[1].srcStageMask = dep[0].srcStageMask;
  dep[1].srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
  dep[1].dstStageMask = dep[0].srcStageMask;
  dep[1].dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
  dep[1].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;
  // disari -> 1: renk yazimi
  dep[2].srcSubpass = VK_SUBPASS_EXTERNAL; dep[2].dstSubpass = 1;
  dep[2].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
  dep[2].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
  dep[2].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
  // 1 -> disari: renk -> transfer okuma
  dep[3].srcSubpass = 1; dep[3].dstSubpass = VK_SUBPASS_EXTERNAL;
  dep[3].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
  dep[3].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
  dep[3].dstStageMask = VK_PIPELINE_STAGE_TRANSFER_BIT;
  dep[3].dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;

  VkResult r;
  if (use2) {
    VkRenderPassCreateInfo2 rpci{};
    rpci.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO_2;
    rpci.pNext = feedback ? &rp_fb_ci : nullptr;
    rpci.attachmentCount = 2;
    rpci.pAttachments = att;
    rpci.subpassCount = 2;
    rpci.pSubpasses = sp;
    rpci.dependencyCount = 4;
    rpci.pDependencies = dep;
    r = a.vkCreateRenderPass2(c.d, &rpci, nullptr, &c.rp);
    if (r != VK_SUCCESS) return c.fail("vkCreateRenderPass2", r);
    if (feedback) {
      c.out->merge_feedback_available = true;
      c.out->post_merge_subpass_count = rp_fb.postMergeSubpassCount;
      c.out->subpass_merge_status[0] = (int32_t)sp_fb[0].subpassMergeStatus;
      c.out->subpass_merge_status[1] = (int32_t)sp_fb[1].subpassMergeStatus;
    }
    return true;
  }
  // Vulkan 1.1: RenderPass v1 (geri bildirim yok).
  VkAttachmentDescription a1[2]{};
  for (int i = 0; i < 2; i++) {
    a1[i].format = att[i].format; a1[i].samples = att[i].samples; a1[i].loadOp = att[i].loadOp;
    a1[i].storeOp = att[i].storeOp; a1[i].stencilLoadOp = att[i].stencilLoadOp;
    a1[i].stencilStoreOp = att[i].stencilStoreOp; a1[i].initialLayout = att[i].initialLayout;
    a1[i].finalLayout = att[i].finalLayout;
  }
  VkAttachmentReference cr{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
  VkAttachmentReference dr{1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
  VkSubpassDescription s1[2]{};
  s1[0].pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
  s1[0].pDepthStencilAttachment = &dr;
  s1[1].pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
  s1[1].colorAttachmentCount = 1;
  s1[1].pColorAttachments = &cr;
  s1[1].pDepthStencilAttachment = &dr;
  VkSubpassDependency d1[4]{};
  for (int i = 0; i < 4; i++) {
    d1[i].srcSubpass = dep[i].srcSubpass; d1[i].dstSubpass = dep[i].dstSubpass;
    d1[i].srcStageMask = dep[i].srcStageMask; d1[i].dstStageMask = dep[i].dstStageMask;
    d1[i].srcAccessMask = dep[i].srcAccessMask; d1[i].dstAccessMask = dep[i].dstAccessMask;
    d1[i].dependencyFlags = dep[i].dependencyFlags;
  }
  VkRenderPassCreateInfo rpci{};
  rpci.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
  rpci.attachmentCount = 2; rpci.pAttachments = a1;
  rpci.subpassCount = 2; rpci.pSubpasses = s1;
  rpci.dependencyCount = 4; rpci.pDependencies = d1;
  r = a.vkCreateRenderPass(c.d, &rpci, nullptr, &c.rp);
  if (r != VK_SUCCESS) return c.fail("vkCreateRenderPass", r);
  return true;
}

bool make_shader(Ctx &c, const uint32_t *code, uint32_t bytes, VkShaderModule *m) {
  VkShaderModuleCreateInfo ci{};
  ci.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
  ci.codeSize = bytes;
  ci.pCode = code;
  VkResult r = c.api->vkCreateShaderModule(c.d, &ci, nullptr, m);
  if (r != VK_SUCCESS) return c.fail("vkCreateShaderModule", r);
  return true;
}

bool make_pipeline(Ctx &c, uint32_t subpass, bool color, VkPipeline *out) {
  VkPipelineShaderStageCreateInfo stages[2]{};
  stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
  stages[0].module = c.vs;
  stages[0].pName = "main";
  stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
  stages[1].module = c.fs;
  stages[1].pName = "main";
  VkPipelineVertexInputStateCreateInfo vi{};
  vi.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
  VkPipelineInputAssemblyStateCreateInfo ia{};
  ia.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
  ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
  VkPipelineViewportStateCreateInfo vp{};
  vp.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
  vp.viewportCount = 1;
  vp.scissorCount = 1;
  VkPipelineRasterizationStateCreateInfo rs{};
  rs.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
  rs.polygonMode = VK_POLYGON_MODE_FILL;
  rs.cullMode = VK_CULL_MODE_NONE;
  rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
  rs.lineWidth = 1.0f;
  VkPipelineMultisampleStateCreateInfo ms{};
  ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
  ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
  VkPipelineDepthStencilStateCreateInfo ds{};
  ds.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
  ds.depthTestEnable = VK_TRUE;
  ds.depthWriteEnable = color ? VK_FALSE : VK_TRUE;
  ds.depthCompareOp = color ? VK_COMPARE_OP_LESS_OR_EQUAL : VK_COMPARE_OP_LESS;
  VkPipelineColorBlendAttachmentState cba{};
  cba.colorWriteMask = 0xF;
  VkPipelineColorBlendStateCreateInfo cb{};
  cb.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
  cb.attachmentCount = color ? 1 : 0;
  cb.pAttachments = &cba;
  VkDynamicState dyn[2] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
  VkPipelineDynamicStateCreateInfo dsci{};
  dsci.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
  dsci.dynamicStateCount = 2;
  dsci.pDynamicStates = dyn;
  VkGraphicsPipelineCreateInfo gp{};
  gp.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
  gp.stageCount = color ? 2 : 1; // depth prepass: fragment yok
  gp.pStages = stages;
  gp.pVertexInputState = &vi;
  gp.pInputAssemblyState = &ia;
  gp.pViewportState = &vp;
  gp.pRasterizationState = &rs;
  gp.pMultisampleState = &ms;
  gp.pDepthStencilState = &ds;
  gp.pColorBlendState = &cb;
  gp.pDynamicState = &dsci;
  gp.layout = c.layout;
  gp.renderPass = c.rp;
  gp.subpass = subpass;
  VkResult r = c.api->vkCreateGraphicsPipelines(c.d, c.cache, 1, &gp, nullptr, out);
  if (r != VK_SUCCESS) return c.fail("vkCreateGraphicsPipelines", r);
  return true;
}

// GPL: renk pipeline'i 4 kutuphane + link. Kutuphaneler ayni renderPass/subpass
// ve layout ile; link asamasi (LTO yok) hizli — plan Faz 1 "PSO'yu build'de
// uretmek yetmez, yuklemesi de ucuz olmali" maddesinin olculebilir hali.
bool make_pipeline_gpl(Ctx &c, VkPipeline *out, uint64_t *lib_ns, uint64_t *link_ns) {
  VkApi &a = *c.api;
  VkPipelineShaderStageCreateInfo vs_stage{};
  vs_stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  vs_stage.stage = VK_SHADER_STAGE_VERTEX_BIT;
  vs_stage.module = c.vs;
  vs_stage.pName = "main";
  VkPipelineShaderStageCreateInfo fs_stage = vs_stage;
  fs_stage.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
  fs_stage.module = c.fs;
  VkPipelineVertexInputStateCreateInfo vi{};
  vi.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
  VkPipelineInputAssemblyStateCreateInfo ia{};
  ia.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
  ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
  VkPipelineViewportStateCreateInfo vp{};
  vp.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
  vp.viewportCount = 1;
  vp.scissorCount = 1;
  VkPipelineRasterizationStateCreateInfo rs{};
  rs.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
  rs.polygonMode = VK_POLYGON_MODE_FILL;
  rs.cullMode = VK_CULL_MODE_NONE;
  rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
  rs.lineWidth = 1.0f;
  VkPipelineMultisampleStateCreateInfo ms{};
  ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
  ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
  VkPipelineDepthStencilStateCreateInfo ds{};
  ds.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
  ds.depthTestEnable = VK_TRUE;
  ds.depthWriteEnable = VK_FALSE;
  ds.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
  VkPipelineColorBlendAttachmentState cba{};
  cba.colorWriteMask = 0xF;
  VkPipelineColorBlendStateCreateInfo cb{};
  cb.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
  cb.attachmentCount = 1;
  cb.pAttachments = &cba;
  VkDynamicState dyn[2] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
  VkPipelineDynamicStateCreateInfo dsci{};
  dsci.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
  dsci.dynamicStateCount = 2;
  dsci.pDynamicStates = dyn;

  VkGraphicsPipelineLibraryCreateInfoEXT lib_ci[4]{};
  VkGraphicsPipelineCreateInfo gp[4]{};
  const VkGraphicsPipelineLibraryFlagBitsEXT parts[4] = {
      VK_GRAPHICS_PIPELINE_LIBRARY_VERTEX_INPUT_INTERFACE_BIT_EXT,
      VK_GRAPHICS_PIPELINE_LIBRARY_PRE_RASTERIZATION_SHADERS_BIT_EXT,
      VK_GRAPHICS_PIPELINE_LIBRARY_FRAGMENT_SHADER_BIT_EXT,
      VK_GRAPHICS_PIPELINE_LIBRARY_FRAGMENT_OUTPUT_INTERFACE_BIT_EXT};
  for (int i = 0; i < 4; i++) {
    lib_ci[i].sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_LIBRARY_CREATE_INFO_EXT;
    lib_ci[i].flags = parts[i];
    gp[i].sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    gp[i].pNext = &lib_ci[i];
    gp[i].flags = VK_PIPELINE_CREATE_LIBRARY_BIT_KHR;
    gp[i].renderPass = c.rp;
    gp[i].subpass = 1;
    gp[i].layout = c.layout;
  }
  // 1: vertex input
  gp[0].pVertexInputState = &vi;
  gp[0].pInputAssemblyState = &ia;
  // 2: pre-rasterization (vertex shader, viewport, raster, dinamik)
  gp[1].stageCount = 1;
  gp[1].pStages = &vs_stage;
  gp[1].pViewportState = &vp;
  gp[1].pRasterizationState = &rs;
  gp[1].pDynamicState = &dsci;
  // 3: fragment shader (+ derinlik, multisample)
  gp[2].stageCount = 1;
  gp[2].pStages = &fs_stage;
  gp[2].pMultisampleState = &ms;
  gp[2].pDepthStencilState = &ds;
  // 4: fragment output (renk karistirma, multisample)
  gp[3].pColorBlendState = &cb;
  gp[3].pMultisampleState = &ms;

  uint64_t t0 = platform::now_ns();
  for (int i = 0; i < 4; i++) {
    VkResult r = a.vkCreateGraphicsPipelines(c.d, c.cache, 1, &gp[i], nullptr, &c.gpl_libs[i]);
    if (r != VK_SUCCESS) return c.fail("vkCreateGraphicsPipelines (GPL kutuphane)", r);
  }
  uint64_t t1 = platform::now_ns();
  VkPipelineLibraryCreateInfoKHR link{};
  link.sType = VK_STRUCTURE_TYPE_PIPELINE_LIBRARY_CREATE_INFO_KHR;
  link.libraryCount = 4;
  link.pLibraries = c.gpl_libs;
  VkGraphicsPipelineCreateInfo linked{};
  linked.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
  linked.pNext = &link;
  linked.layout = c.layout;
  linked.renderPass = c.rp;
  linked.subpass = 1;
  VkResult r = a.vkCreateGraphicsPipelines(c.d, c.cache, 1, &linked, nullptr, out);
  if (r != VK_SUCCESS) return c.fail("vkCreateGraphicsPipelines (GPL link)", r);
  uint64_t t2 = platform::now_ns();
  *lib_ns = t1 - t0;
  *link_ns = t2 - t1;
  return true;
}

// PSO cache dosyasi: [VkPipelineCacheHeaderVersionOne][veri]. Baslik cihazla
// eslesmiyorsa (baska GPU/surucu) yuklenmez — yabanci cache sessizce kabul
// edilmez.
bool load_cache_file(Ctx &c, Arena &arena, const char *path, void **data, size_t *size) {
  *data = nullptr;
  *size = 0;
  FILE *f = std::fopen(path, "rb");
  if (!f) return false;
  std::fseek(f, 0, SEEK_END);
  long n = std::ftell(f);
  std::fseek(f, 0, SEEK_SET);
  if (n < (long)sizeof(VkPipelineCacheHeaderVersionOne)) { std::fclose(f); return false; }
  void *buf = arena.alloc((size_t)n, 16);
  if (!buf || std::fread(buf, 1, (size_t)n, f) != (size_t)n) { std::fclose(f); return false; }
  std::fclose(f);
  VkPipelineCacheHeaderVersionOne h;
  std::memcpy(&h, buf, sizeof h);
  const DeviceCaps &caps = c.dev->caps();
  if (h.headerVersion != VK_PIPELINE_CACHE_HEADER_VERSION_ONE || h.vendorID != caps.vendor_id ||
      h.deviceID != caps.device_id)
    return false;
  *data = buf;
  *size = (size_t)n;
  return true;
}
// Paralel kayit job'u: bir yatay bant icin ikincil tampon.
struct BandJob {
  Ctx *c;
  const OffscreenConfig *cfg;
  uint32_t band, bands;
  VkCommandBuffer out;
};
void record_band(void *p) {
  BandJob *b = static_cast<BandJob *>(p);
  VkApi &a = *b->c->api;
  VkCommandBuffer sc = b->cfg->pools->acquire_secondary();
  b->out = sc;
  if (!sc) return; // kapasite: cagiran sayar
  VkCommandBufferInheritanceInfo inh{};
  inh.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_INHERITANCE_INFO;
  inh.renderPass = b->c->rp;
  inh.subpass = 1;
  inh.framebuffer = b->c->fb;
  VkCommandBufferBeginInfo bi{};
  bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
  bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT | VK_COMMAND_BUFFER_USAGE_RENDER_PASS_CONTINUE_BIT;
  bi.pInheritanceInfo = &inh;
  a.vkBeginCommandBuffer(sc, &bi);
  const uint32_t w = b->cfg->width, h = b->cfg->height;
  uint32_t y0 = h * b->band / b->bands, y1 = h * (b->band + 1) / b->bands;
  VkViewport vpt{0, 0, (float)w, (float)h, 0.0f, 1.0f};
  VkRect2D sc_rect{{0, (int32_t)y0}, {w, y1 - y0}};
  a.vkCmdSetViewport(sc, 0, 1, &vpt);
  a.vkCmdSetScissor(sc, 0, 1, &sc_rect);
  a.vkCmdBindPipeline(sc, VK_PIPELINE_BIND_POINT_GRAPHICS, b->c->pipe_color);
  a.vkCmdDraw(sc, 3, 1, 0, 0);
  a.vkEndCommandBuffer(sc);
}
} // namespace

struct OffscreenTarget {
  Ctx c;
  Device *dev;
  uint32_t w, h;
  uint8_t *pixels; // arenadan, kurulumda
};

OffscreenTarget *offscreen_create(Device &dev, Arena &arena, const OffscreenConfig &cfg,
                                  OffscreenResult *out) {
  *out = OffscreenResult{};
  OffscreenTarget *t = arena.alloc_array_zeroed<OffscreenTarget>(1);
  if (!t) return nullptr;
  t->dev = &dev;
  t->w = cfg.width;
  t->h = cfg.height;
  t->c = Ctx{&dev, &dev.api(), dev.handle(), out};
  Ctx &c = t->c;
  VkApi &a = *c.api;
  const uint32_t w = cfg.width, h = cfg.height;
  const VkFormat color_fmt = cfg.srgb ? VK_FORMAT_R8G8B8A8_SRGB : VK_FORMAT_R8G8B8A8_UNORM;
  const VkFormat depth_fmt = VK_FORMAT_D32_SFLOAT;
  bool ok = false;
  do {
    if (!make_image(c, color_fmt, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT, false,
                    w, h, &c.color, &c.color_view, VK_IMAGE_ASPECT_COLOR_BIT)) break;
    if (!make_image(c, depth_fmt,
                    VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT, true,
                    w, h, &c.depth, &c.depth_view, VK_IMAGE_ASPECT_DEPTH_BIT)) break;
    {
      VkBufferCreateInfo bci{};
      bci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
      bci.size = (VkDeviceSize)w * h * 4;
      bci.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
      bci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
      VkResult r = a.vkCreateBuffer(c.d, &bci, nullptr, &c.readback);
      if (r != VK_SUCCESS) { c.fail("vkCreateBuffer", r); break; }
      VkMemoryRequirements req;
      a.vkGetBufferMemoryRequirements(c.d, c.readback, &req);
      if (!dev.allocate(req, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, false,
                        &c.readback_mem) &&
          !dev.allocate(req, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT, false, &c.readback_mem)) {
        std::snprintf(out->error, sizeof out->error, "readback bellek: %s", dev.last_error());
        break;
      }
      r = a.vkBindBufferMemory(c.d, c.readback, c.readback_mem.memory, c.readback_mem.offset);
      if (r != VK_SUCCESS) { c.fail("vkBindBufferMemory", r); break; }
    }
    { // Mali tile butcesi (tile_budget.hpp): asim = hata, sessiz gecis yok.
      const VkFormat fmts[2] = {color_fmt, depth_fmt};
      const TileBudget tb = tile_budget(fmts, 2);
      if (!tb.ok) { std::snprintf(out->error, sizeof out->error, "%s", tb.error); break; }
    }
    if (!make_render_pass(c, color_fmt, depth_fmt)) break;
    {
      VkImageView views[2] = {c.color_view, c.depth_view};
      VkFramebufferCreateInfo fci{};
      fci.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
      fci.renderPass = c.rp;
      fci.attachmentCount = 2;
      fci.pAttachments = views;
      fci.width = w; fci.height = h; fci.layers = 1;
      VkResult r = a.vkCreateFramebuffer(c.d, &fci, nullptr, &c.fb);
      if (r != VK_SUCCESS) { c.fail("vkCreateFramebuffer", r); break; }
    }
    if (!make_shader(c, triangle_vert_spv, triangle_vert_spv_size, &c.vs)) break;
    if (!make_shader(c, triangle_frag_spv, triangle_frag_spv_size, &c.fs)) break;
    {
      VkPipelineLayoutCreateInfo pli{};
      pli.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
      VkResult r = a.vkCreatePipelineLayout(c.d, &pli, nullptr, &c.layout);
      if (r != VK_SUCCESS) { c.fail("vkCreatePipelineLayout", r); break; }
      void *cdata = nullptr;
      size_t csize = 0;
      if (cfg.pso_cache_path) out->pso_cache_loaded = load_cache_file(c, arena, cfg.pso_cache_path, &cdata, &csize);
      VkPipelineCacheCreateInfo pcci{};
      pcci.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;
      pcci.initialDataSize = csize;
      pcci.pInitialData = cdata;
      r = a.vkCreatePipelineCache(c.d, &pcci, nullptr, &c.cache);
      if (r != VK_SUCCESS) { c.fail("vkCreatePipelineCache", r); break; }
    }
    if (!make_pipeline(c, 0, false, &c.pipe_depth)) break;
    {
      uint64_t m0 = platform::now_ns();
      if (!make_pipeline(c, 1, true, &c.pipe_color)) break;
      out->pipeline_monolithic_ns = platform::now_ns() - m0;
    }
    if (cfg.use_pipeline_library && dev.caps().graphics_pipeline_library) {
      // Monolitik olan olcum icin yapildi; GPL ile yeniden kur, onu kullan.
      VkPipeline linked = VK_NULL_HANDLE;
      if (!make_pipeline_gpl(c, &linked, &out->pipeline_library_ns, &out->pipeline_link_ns)) break;
      a.vkDestroyPipeline(c.d, c.pipe_color, nullptr);
      c.pipe_color = linked;
      out->pipeline_library_used = true;
    }
    if (cfg.pso_cache_path) {
      size_t n = 0;
      a.vkGetPipelineCacheData(c.d, c.cache, &n, nullptr);
      if (n > 0) {
        void *buf = arena.alloc(n, 16);
        if (buf && a.vkGetPipelineCacheData(c.d, c.cache, &n, buf) == VK_SUCCESS) {
          FILE *f = std::fopen(cfg.pso_cache_path, "wb");
          if (f) { std::fwrite(buf, 1, n, f); std::fclose(f); out->pso_cache_bytes = n; }
        }
      }
    }
    if (dev.caps().timestamps) {
      VkQueryPoolCreateInfo qci{};
      qci.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
      qci.queryType = VK_QUERY_TYPE_TIMESTAMP;
      qci.queryCount = 2;
      if (a.vkCreateQueryPool(c.d, &qci, nullptr, &c.queries) != VK_SUCCESS) c.queries = VK_NULL_HANDLE;
    }
    t->pixels = arena.alloc_array<uint8_t>((size_t)w * h * 4);
    if (!t->pixels) { c.fail("piksel arenasi", VK_ERROR_OUT_OF_HOST_MEMORY); break; }
    ok = true;
  } while (false);
  if (!ok) {
    c.cleanup();
    return nullptr;
  }
  out->memory_allocations = dev.memory_allocation_count();
  return t;
}

bool offscreen_render_frame(OffscreenTarget *t, const OffscreenConfig &cfg, OffscreenResult *out) {
  Ctx &c = t->c;
  c.out = out;
  VkApi &a = *c.api;
  Device &dev = *t->dev;
  const uint32_t w = t->w, h = t->h;
  out->ok = false;
  out->pixels = t->pixels;
  out->gpu_ns = 0;
  out->secondaries_recorded = 0;
  out->recording_threads = 0;
  VkCommandBuffer cb = dev.begin_one_shot();
  if (!cb) return c.fail("komut tamponu", VK_ERROR_OUT_OF_HOST_MEMORY);
  if (c.queries) {
    a.vkCmdResetQueryPool(cb, c.queries, 0, 2);
    a.vkCmdWriteTimestamp(cb, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, c.queries, 0);
  }
  VkClearValue clears[2]{};
  clears[0].color.float32[0] = cfg.clear[0] / 255.0f;
  clears[0].color.float32[1] = cfg.clear[1] / 255.0f;
  clears[0].color.float32[2] = cfg.clear[2] / 255.0f;
  clears[0].color.float32[3] = cfg.clear[3] / 255.0f;
  clears[1].depthStencil = {1.0f, 0};
  VkRenderPassBeginInfo rbi{};
  rbi.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
  rbi.renderPass = c.rp;
  rbi.framebuffer = c.fb;
  rbi.renderArea = {{0, 0}, {w, h}};
  rbi.clearValueCount = 2;
  rbi.pClearValues = clears;
  a.vkCmdBeginRenderPass(cb, &rbi, VK_SUBPASS_CONTENTS_INLINE);
  VkViewport vpt{0, 0, (float)w, (float)h, 0.0f, 1.0f};
  VkRect2D sc{{0, 0}, {w, h}};
  a.vkCmdSetViewport(cb, 0, 1, &vpt);
  a.vkCmdSetScissor(cb, 0, 1, &sc);
  a.vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, c.pipe_depth);
  a.vkCmdDraw(cb, 3, 1, 0, 0);
  bool parallel = cfg.parallel_jobs > 0 && cfg.jobs && cfg.pools;
  if (!parallel) {
    a.vkCmdNextSubpass(cb, VK_SUBPASS_CONTENTS_INLINE);
    a.vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, c.pipe_color);
    a.vkCmdSetViewport(cb, 0, 1, &vpt);
    a.vkCmdSetScissor(cb, 0, 1, &sc);
    a.vkCmdDraw(cb, 3, 1, 0, 0);
  } else {
    uint32_t n = cfg.parallel_jobs > 64 ? 64 : cfg.parallel_jobs;
    BandJob bands[64];
    JobDecl decls[64];
    VkCommandBuffer secs[64];
    cfg.pools->begin_frame();
    for (uint32_t i = 0; i < n; i++) {
      bands[i] = BandJob{&c, &cfg, i, n, VK_NULL_HANDLE};
      decls[i] = JobDecl{record_band, &bands[i], "record_band"};
    }
    Counter done;
    cfg.jobs->run(decls, n, &done);
    cfg.jobs->wait(done);
    uint32_t got = 0;
    for (uint32_t i = 0; i < n; i++) if (bands[i].out) secs[got++] = bands[i].out;
    if (got != n) {
      a.vkCmdEndRenderPass(cb);
      dev.end_one_shot_and_wait(cb);
      return c.fail("ikincil tampon kapasitesi yetmedi", VK_ERROR_OUT_OF_HOST_MEMORY);
    }
    out->secondaries_recorded = got;
    for (uint32_t k = 0; k < cfg.pools->thread_count(); k++) if (cfg.pools->used_in_slot(k)) out->recording_threads++;
    a.vkCmdNextSubpass(cb, VK_SUBPASS_CONTENTS_SECONDARY_COMMAND_BUFFERS);
    a.vkCmdExecuteCommands(cb, got, secs);
  }
  a.vkCmdEndRenderPass(cb);
  if (c.queries) a.vkCmdWriteTimestamp(cb, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, c.queries, 1);
  VkBufferImageCopy region{};
  region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
  region.imageExtent = {w, h, 1};
  a.vkCmdCopyImageToBuffer(cb, c.color, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, c.readback, 1, &region);
  if (!dev.end_one_shot_and_wait(cb)) {
    std::snprintf(out->error, sizeof out->error, "gonderim: %s", dev.last_error());
    return false;
  }
  out->timestamps_valid = false;
  if (c.queries) {
    uint64_t ts[2] = {0, 0};
    if (a.vkGetQueryPoolResults(c.d, c.queries, 0, 2, sizeof ts, ts, sizeof(uint64_t),
                                VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT) == VK_SUCCESS) {
      out->timestamps_valid = true;
      // MoltenVK (Metal) TOP/BOTTOM damgalarini ayni degerle verebiliyor
      // (olculdu CI macOS 2026-09-14: fark 0). Sifir "olculemedi" demek, hata degil.
      if (ts[1] > ts[0]) out->gpu_ns = (uint64_t)((double)(ts[1] - ts[0]) * dev.caps().timestamp_period_ns);
    }
  }
  VkMappedMemoryRange rng{};
  rng.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
  rng.memory = c.readback_mem.memory;
  rng.offset = 0;
  rng.size = VK_WHOLE_SIZE;
  a.vkInvalidateMappedMemoryRanges(c.d, 1, &rng);
  std::memcpy(t->pixels, c.readback_mem.mapped, (size_t)w * h * 4);
  out->memory_allocations = dev.memory_allocation_count();
  out->ok = true;
  return true;
}

void offscreen_destroy(OffscreenTarget *t) {
  if (t) t->c.cleanup();
}

VkRenderPass offscreen_render_pass(OffscreenTarget *t) { return t ? t->c.rp : VK_NULL_HANDLE; }

bool offscreen_render_custom(OffscreenTarget *t, const OffscreenConfig &cfg, OffscreenRecordFn record, void *user,
                             OffscreenResult *out, OffscreenRecordFn before) {
  Ctx &c = t->c;
  c.out = out;
  VkApi &a = *c.api;
  Device &dev = *t->dev;
  const uint32_t w = t->w, h = t->h;
  out->ok = false;
  out->pixels = t->pixels;
  VkCommandBuffer cb = dev.begin_one_shot();
  if (!cb) return c.fail("komut tamponu", VK_ERROR_OUT_OF_HOST_MEMORY);
  VkClearValue clears[2]{};
  clears[0].color.float32[0] = cfg.clear[0] / 255.0f;
  clears[0].color.float32[1] = cfg.clear[1] / 255.0f;
  clears[0].color.float32[2] = cfg.clear[2] / 255.0f;
  clears[0].color.float32[3] = cfg.clear[3] / 255.0f;
  clears[1].depthStencil = {1.0f, 0};
  if (before) before(cb, user); // kendi render pass'i olan isler (golge haritasi)
  VkRenderPassBeginInfo rbi{};
  rbi.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
  rbi.renderPass = c.rp;
  rbi.framebuffer = c.fb;
  rbi.renderArea = {{0, 0}, {w, h}};
  rbi.clearValueCount = 2;
  rbi.pClearValues = clears;
  a.vkCmdBeginRenderPass(cb, &rbi, VK_SUBPASS_CONTENTS_INLINE);
  VkViewport vpt{0, 0, (float)w, (float)h, 0.0f, 1.0f};
  VkRect2D sc{{0, 0}, {w, h}};
  a.vkCmdSetViewport(cb, 0, 1, &vpt);
  a.vkCmdSetScissor(cb, 0, 1, &sc);
  record(cb, user);
  a.vkCmdEndRenderPass(cb);
  VkBufferImageCopy region{};
  region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
  region.imageExtent = {w, h, 1};
  a.vkCmdCopyImageToBuffer(cb, c.color, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, c.readback, 1, &region);
  if (!dev.end_one_shot_and_wait(cb)) {
    std::snprintf(out->error, sizeof out->error, "gonderim: %s", dev.last_error());
    return false;
  }
  VkMappedMemoryRange rng{};
  rng.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
  rng.memory = c.readback_mem.memory;
  rng.size = VK_WHOLE_SIZE;
  a.vkInvalidateMappedMemoryRanges(c.d, 1, &rng);
  std::memcpy(t->pixels, c.readback_mem.mapped, (size_t)w * h * 4);
  out->ok = true;
  return true;
}

bool render_triangle_offscreen(Device &dev, Arena &arena, const OffscreenConfig &cfg,
                               OffscreenResult *out) {
  OffscreenTarget *t = offscreen_create(dev, arena, cfg, out);
  if (!t) { out->ok = false; return false; }
  OffscreenResult setup = *out; // pso_cache_* ve merge feedback kurulumdan
  bool ok = offscreen_render_frame(t, cfg, out);
  out->pso_cache_loaded = setup.pso_cache_loaded;
  out->pso_cache_bytes = setup.pso_cache_bytes;
  out->merge_feedback_available = setup.merge_feedback_available;
  out->post_merge_subpass_count = setup.post_merge_subpass_count;
  out->subpass_merge_status[0] = setup.subpass_merge_status[0];
  out->subpass_merge_status[1] = setup.subpass_merge_status[1];
  out->pipeline_monolithic_ns = setup.pipeline_monolithic_ns;
  out->pipeline_library_ns = setup.pipeline_library_ns;
  out->pipeline_link_ns = setup.pipeline_link_ns;
  out->pipeline_library_used = setup.pipeline_library_used;
  offscreen_destroy(t);
  return ok;
}

bool write_ppm(const char *path, const uint8_t *rgba, uint32_t w, uint32_t h) {
  FILE *f = std::fopen(path, "wb");
  if (!f) return false;
  std::fprintf(f, "P6\n%u %u\n255\n", w, h);
  for (uint32_t i = 0; i < w * h; i++) std::fwrite(rgba + i * 4, 1, 3, f);
  return std::fclose(f) == 0;
}

} // namespace tulpar::engine::rhi
