#include "renderer/renderer.hpp"

#include <cmath>
#include <cstring>

#include "rhi/shaders/mesh_frag_spv.h"
#include "rhi/shaders/mesh_skin_vert_spv.h"
#include "rhi/shaders/mesh_vert_spv.h"
#include "rhi/shaders/shadow_skin_vert_spv.h"
#include "rhi/shaders/shadow_vert_spv.h"
#include "rhi/shaders/ui_frag_spv.h"
#include "rhi/shaders/ui_vert_spv.h"

namespace tulpar::engine::renderer {

bool Renderer::make_buffer(VkBufferUsageFlags usage, VkDeviceSize size, VkMemoryPropertyFlags mem, VkBuffer *buf,
                           rhi::MemoryAlloc *out) {
  rhi::VkApi &a = dev_->api();
  VkBufferCreateInfo bi{};
  bi.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  bi.size = size;
  bi.usage = usage;
  bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
  if (a.vkCreateBuffer(dev_->handle(), &bi, nullptr, buf) != VK_SUCCESS) return false;
  VkMemoryRequirements req;
  a.vkGetBufferMemoryRequirements(dev_->handle(), *buf, &req);
  if (!dev_->allocate(req, mem, false, out)) return false;
  return a.vkBindBufferMemory(dev_->handle(), *buf, out->memory, out->offset) == VK_SUCCESS;
}

bool Renderer::upload(VkBuffer dst, const void *data, VkDeviceSize size, VkBufferUsageFlags) {
  rhi::VkApi &a = dev_->api();
  VkBuffer staging = VK_NULL_HANDLE;
  rhi::MemoryAlloc sm;
  if (!make_buffer(VK_BUFFER_USAGE_TRANSFER_SRC_BIT, size,
                   VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &staging, &sm))
    return false;
  std::memcpy(sm.mapped, data, (size_t)size);
  VkCommandBuffer cb = dev_->begin_one_shot();
  VkBufferCopy region{0, 0, size};
  a.vkCmdCopyBuffer(cb, staging, dst, 1, &region);
  bool ok = dev_->end_one_shot_and_wait(cb);
  a.vkDestroyBuffer(dev_->handle(), staging, nullptr); // bellek blokta kalir (yukleme aninda, kabul)
  return ok;
}

bool Renderer::init(rhi::Device &dev, Arena &arena, VkRenderPass rp, const RendererConfig &cfg) {
  dev_ = &dev;
  cfg_ = cfg;
  if (cfg_.frames_in_flight > kMaxFrames) cfg_.frames_in_flight = kMaxFrames;
  meshes_ = arena.alloc_array_zeroed<Mesh>(cfg.max_meshes);
  textures_ = arena.alloc_array_zeroed<Texture>(cfg.max_textures);
  materials_ = arena.alloc_array_zeroed<Material>(cfg.max_materials);
  draws_ = arena.alloc_array<Draw>(cfg.max_draws);
  cluster_masks_ = arena.alloc_array_zeroed<uint32_t>(grid_.count());
  if (!meshes_ || !textures_ || !materials_ || !draws_ || !cluster_masks_) return false;
  rhi::VkApi &a = dev.api();
  // Shader'lar
  VkShaderModuleCreateInfo smi{};
  smi.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
  smi.codeSize = mesh_vert_spv_size; smi.pCode = mesh_vert_spv;
  if (a.vkCreateShaderModule(dev.handle(), &smi, nullptr, &vs_) != VK_SUCCESS) return false;
  smi.codeSize = mesh_frag_spv_size; smi.pCode = mesh_frag_spv;
  if (a.vkCreateShaderModule(dev.handle(), &smi, nullptr, &fs_) != VK_SUCCESS) return false;
  smi.codeSize = shadow_vert_spv_size; smi.pCode = shadow_vert_spv;
  if (a.vkCreateShaderModule(dev.handle(), &smi, nullptr, &shadow_vs_) != VK_SUCCESS) return false;
  smi.codeSize = mesh_skin_vert_spv_size; smi.pCode = mesh_skin_vert_spv;
  if (a.vkCreateShaderModule(dev.handle(), &smi, nullptr, &skin_vs_) != VK_SUCCESS) return false;
  smi.codeSize = shadow_skin_vert_spv_size; smi.pCode = shadow_skin_vert_spv;
  if (a.vkCreateShaderModule(dev.handle(), &smi, nullptr, &skin_shadow_vs_) != VK_SUCCESS) return false;
  // Golge hedefi UBO'dan ONCE: descriptor yazarken view+sampler hazir olmali.
  if (!make_shadow()) return false;
  // Set 0: binding 0 kare UBO, binding 1 golge haritasi (karsilastirmali sampler)
  // Set 0: 0 kare UBO, 1 golge, 2 nokta isiklar (UBO), 3 kume maskeleri (SSBO), 4 eklem matrisleri (SSBO)
  VkDescriptorSetLayoutBinding b[5]{};
  b[4].binding = 4;
  b[4].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
  b[4].descriptorCount = 1;
  b[4].stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
  b[0].binding = 0;
  b[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
  b[0].descriptorCount = 1;
  b[0].stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
  b[1].binding = 1;
  b[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
  b[1].descriptorCount = 1;
  b[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
  b[2].binding = 2;
  b[2].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
  b[2].descriptorCount = 1;
  b[2].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
  b[3].binding = 3;
  b[3].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
  b[3].descriptorCount = 1;
  b[3].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
  VkDescriptorSetLayoutCreateInfo sli{};
  sli.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
  sli.bindingCount = 5;
  sli.pBindings = b;
  if (a.vkCreateDescriptorSetLayout(dev.handle(), &sli, nullptr, &set_layout_) != VK_SUCCESS) return false;
  if (!make_material_layout()) return false;
  VkDescriptorSetLayout layouts[2] = {set_layout_, mat_layout_};
  VkPushConstantRange pcr{VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(Push)};
  VkPipelineLayoutCreateInfo pli{};
  pli.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
  pli.setLayoutCount = 2;
  pli.pSetLayouts = layouts;
  pli.pushConstantRangeCount = 1;
  pli.pPushConstantRanges = &pcr;
  if (a.vkCreatePipelineLayout(dev.handle(), &pli, nullptr, &layout_) != VK_SUCCESS) return false;
  // UBO + descriptor (ucuslu kare basina)
  VkDescriptorPoolSize ps[3] = {{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 2 * kMaxFrames},
                                {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, kMaxFrames},
                                {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 2 * kMaxFrames}};
  VkDescriptorPoolCreateInfo dpi{};
  dpi.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
  dpi.maxSets = kMaxFrames;
  dpi.poolSizeCount = 3;
  dpi.pPoolSizes = ps;
  if (a.vkCreateDescriptorPool(dev.handle(), &dpi, nullptr, &pool_) != VK_SUCCESS) return false;
  for (uint32_t i = 0; i < cfg_.frames_in_flight; i++) {
    if (!make_buffer(VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, sizeof(FrameUbo),
                     VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &ubo_[i], &ubo_mem_[i]))
      return false;
    VkDescriptorSetAllocateInfo dai{};
    dai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    dai.descriptorPool = pool_;
    dai.descriptorSetCount = 1;
    dai.pSetLayouts = &set_layout_;
    if (a.vkAllocateDescriptorSets(dev.handle(), &dai, &sets_[i]) != VK_SUCCESS) return false;
    const VkMemoryPropertyFlags host = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    if (!make_buffer(VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, sizeof(GpuPointLight) * kMaxPointLights, host, &lights_buf_[i], &lights_mem_[i]))
      return false;
    if (!make_buffer(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, sizeof(uint32_t) * grid_.count(), host, &cluster_buf_[i], &cluster_mem_[i]))
      return false;
    if (!make_buffer(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, sizeof(Mat4) * cfg_.max_skin_matrices, host, &skin_buf_[i], &skin_mem_[i]))
      return false;
    VkDescriptorBufferInfo dsi{skin_buf_[i], 0, sizeof(Mat4) * cfg_.max_skin_matrices};
    VkDescriptorBufferInfo dbi{ubo_[i], 0, sizeof(FrameUbo)};
    VkDescriptorBufferInfo dli{lights_buf_[i], 0, sizeof(GpuPointLight) * kMaxPointLights};
    VkDescriptorBufferInfo dci{cluster_buf_[i], 0, sizeof(uint32_t) * grid_.count()};
    VkDescriptorImageInfo dii{shadow_sampler_, shadow_view_, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    VkWriteDescriptorSet w[5]{};
    w[4].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    w[4].dstSet = sets_[i];
    w[4].dstBinding = 4;
    w[4].descriptorCount = 1;
    w[4].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    w[4].pBufferInfo = &dsi;
    w[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    w[2].dstSet = sets_[i];
    w[2].dstBinding = 2;
    w[2].descriptorCount = 1;
    w[2].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    w[2].pBufferInfo = &dli;
    w[3].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    w[3].dstSet = sets_[i];
    w[3].dstBinding = 3;
    w[3].descriptorCount = 1;
    w[3].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    w[3].pBufferInfo = &dci;
    w[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    w[0].dstSet = sets_[i];
    w[0].dstBinding = 0;
    w[0].descriptorCount = 1;
    w[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    w[0].pBufferInfo = &dbi;
    w[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    w[1].dstSet = sets_[i];
    w[1].dstBinding = 1;
    w[1].descriptorCount = 1;
    w[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    w[1].pImageInfo = &dii;
    a.vkUpdateDescriptorSets(dev.handle(), 5, w, 0, nullptr);
  }
  if (!make_pipelines(rp)) return false;
  if (!make_ui(rp)) return false;
  // Varsayilan malzeme: 1x1 beyaz doku. Dokusuz cizimler bununla gider; shader tek yol.
  static const uint8_t white[4] = {255, 255, 255, 255};
  default_texture_ = create_texture(white, 1, 1, false);
  default_material_ = create_material(default_texture_, {1, 1, 1});
  return default_texture_.valid() && default_material_.valid();
}

bool Renderer::make_ui(VkRenderPass rp) {
  rhi::VkApi &a = dev_->api();
  VkShaderModuleCreateInfo smi{};
  smi.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
  smi.codeSize = ui_vert_spv_size; smi.pCode = ui_vert_spv;
  if (a.vkCreateShaderModule(dev_->handle(), &smi, nullptr, &ui_vs_) != VK_SUCCESS) return false;
  smi.codeSize = ui_frag_spv_size; smi.pCode = ui_frag_spv;
  if (a.vkCreateShaderModule(dev_->handle(), &smi, nullptr, &ui_fs_) != VK_SUCCESS) return false;
  for (uint32_t i = 0; i < cfg_.frames_in_flight; i++)
    if (!make_buffer(VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, sizeof(UiVertex) * cfg_.ui_max_vertices,
                     VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &ui_buf_[i], &ui_mem_[i]))
      return false;
  VkPipelineShaderStageCreateInfo st[2]{};
  st[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  st[0].stage = VK_SHADER_STAGE_VERTEX_BIT; st[0].module = ui_vs_; st[0].pName = "main";
  st[1] = st[0];
  st[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT; st[1].module = ui_fs_;
  VkVertexInputBindingDescription vb{0, sizeof(UiVertex), VK_VERTEX_INPUT_RATE_VERTEX};
  VkVertexInputAttributeDescription va[3] = {{0, 0, VK_FORMAT_R32G32_SFLOAT, 0},
                                             {1, 0, VK_FORMAT_R32G32_SFLOAT, 8},
                                             {2, 0, VK_FORMAT_R8G8B8A8_UNORM, 16}};
  VkPipelineVertexInputStateCreateInfo vi{};
  vi.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
  vi.vertexBindingDescriptionCount = 1; vi.pVertexBindingDescriptions = &vb;
  vi.vertexAttributeDescriptionCount = 3; vi.pVertexAttributeDescriptions = va;
  VkPipelineInputAssemblyStateCreateInfo ia{};
  ia.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
  ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
  VkPipelineViewportStateCreateInfo vp{};
  vp.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
  vp.viewportCount = 1; vp.scissorCount = 1;
  VkPipelineRasterizationStateCreateInfo rs{};
  rs.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
  rs.polygonMode = VK_POLYGON_MODE_FILL;
  rs.cullMode = VK_CULL_MODE_NONE;
  rs.lineWidth = 1.0f;
  VkPipelineMultisampleStateCreateInfo ms{};
  ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
  ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
  VkPipelineDepthStencilStateCreateInfo ds{};
  ds.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO; // derinlik yok: en uste
  VkPipelineColorBlendAttachmentState cba{};
  cba.colorWriteMask = 0xF;
  cba.blendEnable = VK_TRUE;
  cba.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
  cba.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
  cba.colorBlendOp = VK_BLEND_OP_ADD;
  cba.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
  cba.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
  cba.alphaBlendOp = VK_BLEND_OP_ADD;
  VkPipelineColorBlendStateCreateInfo cb{};
  cb.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
  cb.attachmentCount = 1; cb.pAttachments = &cba;
  VkDynamicState dyn[2] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
  VkPipelineDynamicStateCreateInfo dsci{};
  dsci.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
  dsci.dynamicStateCount = 2; dsci.pDynamicStates = dyn;
  VkGraphicsPipelineCreateInfo gp{};
  gp.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
  gp.stageCount = 2; gp.pStages = st;
  gp.pVertexInputState = &vi; gp.pInputAssemblyState = &ia; gp.pViewportState = &vp;
  gp.pRasterizationState = &rs; gp.pMultisampleState = &ms; gp.pDepthStencilState = &ds;
  gp.pColorBlendState = &cb; gp.pDynamicState = &dsci;
  gp.layout = layout_; gp.renderPass = rp; gp.subpass = 1; // renk subpass'i, 3B'den sonra
  return a.vkCreateGraphicsPipelines(dev_->handle(), VK_NULL_HANDLE, 1, &gp, nullptr, &pipe_ui_) == VK_SUCCESS;
}

void Renderer::ui_begin(float w, float h, float rot) {
  ui_w_ = w > 0 ? w : 1; ui_h_ = h > 0 ? h : 1; ui_rot_ = rot;
  ui_count_ = 0;
  ui_stats_ = UiStats{};
}
void Renderer::ui_set_atlas(MaterialHandle atlas) { ui_atlas_ = atlas; }
void Renderer::ui_quad(float x, float y, float w, float h, float u0, float v0, float u1, float v1, uint32_t c) {
  if (ui_count_ + 6 > cfg_.ui_max_vertices) { ui_stats_.dropped++; return; }
  UiVertex *v = static_cast<UiVertex *>(ui_mem_[frame_].mapped) + ui_count_;
  v[0] = {x, y, u0, v0, c};         v[1] = {x + w, y, u1, v0, c};     v[2] = {x + w, y + h, u1, v1, c};
  v[3] = {x, y, u0, v0, c};         v[4] = {x + w, y + h, u1, v1, c}; v[5] = {x, y + h, u0, v1, c};
  ui_count_ += 6;
}
void Renderer::ui_rect(float x, float y, float w, float h, uint32_t c) {
  // Atlasin (0,0) texeli beyaz opak (font yukleyici garanti eder); atlas yoksa varsayilan 1x1 beyaz.
  const float t = 0.5f / 512.0f;
  if (!ui_atlas_.valid()) ui_quad(x, y, w, h, 0.5f, 0.5f, 0.5f, 0.5f, c);
  else ui_quad(x, y, w, h, t, t, t, t, c);
}
void Renderer::ui_record(VkCommandBuffer cb) {
  ui_stats_.vertices = ui_count_;
  if (ui_count_ == 0) return;
  rhi::VkApi &a = dev_->api();
  a.vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, pipe_ui_);
  MaterialHandle m = ui_atlas_.valid() ? ui_atlas_ : default_material_;
  a.vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, layout_, 1, 1, &materials_[m.id].set, 0, nullptr);
  float push[5] = {ui_w_, ui_h_, std::cos(ui_rot_), std::sin(ui_rot_), cfg_.srgb_target ? 0.0f : 1.0f};
  a.vkCmdPushConstants(cb, layout_, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof push, push);
  VkDeviceSize off = 0;
  a.vkCmdBindVertexBuffers(cb, 0, 1, &ui_buf_[frame_], &off);
  a.vkCmdDraw(cb, ui_count_, 1, 0, 0);
}

bool Renderer::make_material_layout() {
  rhi::VkApi &a = dev_->api();
  VkDescriptorSetLayoutBinding b{};
  b.binding = 0;
  b.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
  b.descriptorCount = 1;
  b.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
  VkDescriptorSetLayoutCreateInfo sli{};
  sli.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
  sli.bindingCount = 1;
  sli.pBindings = &b;
  if (a.vkCreateDescriptorSetLayout(dev_->handle(), &sli, nullptr, &mat_layout_) != VK_SUCCESS) return false;
  VkDescriptorPoolSize ps{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, cfg_.max_materials};
  VkDescriptorPoolCreateInfo dpi{};
  dpi.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
  dpi.maxSets = cfg_.max_materials;
  dpi.poolSizeCount = 1;
  dpi.pPoolSizes = &ps;
  if (a.vkCreateDescriptorPool(dev_->handle(), &dpi, nullptr, &mat_pool_) != VK_SUCCESS) return false;
  VkSamplerCreateInfo si{};
  si.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
  si.magFilter = si.minFilter = VK_FILTER_LINEAR;
  si.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
  si.addressModeU = si.addressModeV = si.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
  // Mali kurali (BestPractices-Arm-vkCreateSampler-lod-clamping): LOD'u sampler'da
  // kirpma (minLod=0, maxLod=VK_LOD_CLAMP_NONE); mip araligini image view sinirlar.
  si.maxLod = VK_LOD_CLAMP_NONE;
  return a.vkCreateSampler(dev_->handle(), &si, nullptr, &tex_sampler_) == VK_SUCCESS;
}

namespace {
void image_barrier(rhi::VkApi &a, VkCommandBuffer cb, VkImage img, uint32_t mip, uint32_t mip_count, VkImageLayout from,
                   VkImageLayout to, VkAccessFlags src_access, VkAccessFlags dst_access, VkPipelineStageFlags src_stage,
                   VkPipelineStageFlags dst_stage) {
  VkImageMemoryBarrier br{};
  br.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
  br.oldLayout = from;
  br.newLayout = to;
  br.srcQueueFamilyIndex = br.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  br.image = img;
  br.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, mip, mip_count, 0, 1};
  br.srcAccessMask = src_access;
  br.dstAccessMask = dst_access;
  a.vkCmdPipelineBarrier(cb, src_stage, dst_stage, 0, 0, nullptr, 0, nullptr, 1, &br);
}
} // namespace

TextureHandle Renderer::create_texture(const uint8_t *rgba, uint32_t w, uint32_t h, bool mipmaps, bool srgb) {
  if (texture_count_ >= cfg_.max_textures || !rgba || !w || !h) return TextureHandle{};
  rhi::VkApi &a = dev_->api();
  Texture &t = textures_[texture_count_];
  uint32_t mips = 1;
  if (mipmaps) {
    // Blit ile mip: format BLIT_SRC/DST + dogrusal suzme vermeli (RGBA8 her yerde verir; yine de sor).
    VkFormatProperties fp{};
    a.vkGetPhysicalDeviceFormatProperties(dev_->physical(), srgb ? VK_FORMAT_R8G8B8A8_SRGB : VK_FORMAT_R8G8B8A8_UNORM, &fp);
    const VkFormatFeatureFlags need = VK_FORMAT_FEATURE_BLIT_SRC_BIT | VK_FORMAT_FEATURE_BLIT_DST_BIT |
                                      VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT;
    if ((fp.optimalTilingFeatures & need) == need) {
      uint32_t m = w > h ? w : h;
      while (m > 1) { m >>= 1; mips++; }
    }
  }
  VkImageCreateInfo ii{};
  ii.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
  ii.imageType = VK_IMAGE_TYPE_2D;
  ii.format = srgb ? VK_FORMAT_R8G8B8A8_SRGB : VK_FORMAT_R8G8B8A8_UNORM;
  ii.extent = {w, h, 1};
  ii.mipLevels = mips;
  ii.arrayLayers = 1;
  ii.samples = VK_SAMPLE_COUNT_1_BIT;
  ii.tiling = VK_IMAGE_TILING_OPTIMAL;
  ii.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | (mips > 1 ? VK_IMAGE_USAGE_TRANSFER_SRC_BIT : 0);
  if (a.vkCreateImage(dev_->handle(), &ii, nullptr, &t.image) != VK_SUCCESS) return TextureHandle{};
  VkMemoryRequirements req;
  a.vkGetImageMemoryRequirements(dev_->handle(), t.image, &req);
  rhi::MemoryAlloc mem;
  if (!dev_->allocate(req, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, false, &mem)) return TextureHandle{};
  a.vkBindImageMemory(dev_->handle(), t.image, mem.memory, mem.offset);
  // Staging
  VkBuffer staging = VK_NULL_HANDLE;
  rhi::MemoryAlloc sm;
  const VkDeviceSize bytes = (VkDeviceSize)w * h * 4;
  if (!make_buffer(VK_BUFFER_USAGE_TRANSFER_SRC_BIT, bytes, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                   &staging, &sm))
    return TextureHandle{};
  std::memcpy(sm.mapped, rgba, (size_t)bytes);
  VkCommandBuffer cb = dev_->begin_one_shot();
  image_barrier(a, cb, t.image, 0, mips, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 0,
                VK_ACCESS_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
  VkBufferImageCopy region{};
  region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
  region.imageExtent = {w, h, 1};
  a.vkCmdCopyBufferToImage(cb, staging, t.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
  int32_t mw = (int32_t)w, mh = (int32_t)h;
  for (uint32_t i = 1; i < mips; i++) {
    image_barrier(a, cb, t.image, i - 1, 1, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                  VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                  VK_PIPELINE_STAGE_TRANSFER_BIT);
    int32_t nw = mw > 1 ? mw / 2 : 1, nh = mh > 1 ? mh / 2 : 1;
    VkImageBlit blit{};
    blit.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, i - 1, 0, 1};
    blit.srcOffsets[1] = {mw, mh, 1};
    blit.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, i, 0, 1};
    blit.dstOffsets[1] = {nw, nh, 1};
    a.vkCmdBlitImage(cb, t.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, t.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit,
                     VK_FILTER_LINEAR);
    image_barrier(a, cb, t.image, i - 1, 1, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                  VK_ACCESS_TRANSFER_READ_BIT, VK_ACCESS_SHADER_READ_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                  VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
    mw = nw; mh = nh;
  }
  image_barrier(a, cb, t.image, mips - 1, 1, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
  bool ok = dev_->end_one_shot_and_wait(cb);
  a.vkDestroyBuffer(dev_->handle(), staging, nullptr); // bellek blokta kalir (yukleme aninda, kabul)
  if (!ok) return TextureHandle{};
  VkImageViewCreateInfo vi{};
  vi.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
  vi.image = t.image;
  vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
  vi.format = ii.format;
  vi.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, mips, 0, 1};
  if (a.vkCreateImageView(dev_->handle(), &vi, nullptr, &t.view) != VK_SUCCESS) return TextureHandle{};
  t.w = w; t.h = h; t.mips = mips;
  stats_.textures = ++texture_count_;
  return TextureHandle{texture_count_ - 1};
}

MaterialHandle Renderer::create_material(TextureHandle albedo, Vec3 color) {
  if (material_count_ >= cfg_.max_materials || !albedo.valid() || albedo.id >= texture_count_) return MaterialHandle{};
  rhi::VkApi &a = dev_->api();
  Material &m = materials_[material_count_];
  VkDescriptorSetAllocateInfo dai{};
  dai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
  dai.descriptorPool = mat_pool_;
  dai.descriptorSetCount = 1;
  dai.pSetLayouts = &mat_layout_;
  if (a.vkAllocateDescriptorSets(dev_->handle(), &dai, &m.set) != VK_SUCCESS) return MaterialHandle{};
  VkDescriptorImageInfo dii{tex_sampler_, textures_[albedo.id].view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
  VkWriteDescriptorSet w{};
  w.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
  w.dstSet = m.set;
  w.dstBinding = 0;
  w.descriptorCount = 1;
  w.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
  w.pImageInfo = &dii;
  a.vkUpdateDescriptorSets(dev_->handle(), 1, &w, 0, nullptr);
  m.texture = albedo.id;
  m.color = srgb_to_linear(color); // yazar sRGB verir, aydinlatma dogrusal
  stats_.materials = ++material_count_;
  return MaterialHandle{material_count_ - 1};
}

bool Renderer::make_pipelines(VkRenderPass rp) {
  if (!make_pipeline_set(rp, false, &pipe_depth_, &pipe_color_, &pipe_shadow_)) return false;
  return make_pipeline_set(rp, true, &pipe_skin_depth_, &pipe_skin_color_, &pipe_skin_shadow_);
}

// Statik ve iskeletli mesh icin ayni uc boru hatti (depth prepass, renk, golge):
// tek fark vertex duzeni ve vertex shader'i.
bool Renderer::make_pipeline_set(VkRenderPass rp, bool skinned, VkPipeline *depth, VkPipeline *color, VkPipeline *shadow) {
  rhi::VkApi &a = dev_->api();
  VkPipelineShaderStageCreateInfo stages[2]{};
  stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT; stages[0].module = skinned ? skin_vs_ : vs_; stages[0].pName = "main";
  stages[1] = stages[0];
  stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT; stages[1].module = fs_;
  VkVertexInputBindingDescription vb{0, skinned ? (uint32_t)sizeof(SkinnedVertex) : (uint32_t)sizeof(Vertex), VK_VERTEX_INPUT_RATE_VERTEX};
  VkVertexInputAttributeDescription va[5] = {{0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0},
                                             {1, 0, VK_FORMAT_R32G32B32_SFLOAT, sizeof(Vec3)},
                                             {2, 0, VK_FORMAT_R32G32_SFLOAT, 2 * sizeof(Vec3)},
                                             {3, 0, VK_FORMAT_R8G8B8A8_UINT, (uint32_t)offsetof(SkinnedVertex, joints)},
                                             {4, 0, VK_FORMAT_R16G16B16A16_UNORM, (uint32_t)offsetof(SkinnedVertex, weights)}};
  VkPipelineVertexInputStateCreateInfo vi{};
  vi.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
  vi.vertexBindingDescriptionCount = 1; vi.pVertexBindingDescriptions = &vb;
  vi.vertexAttributeDescriptionCount = skinned ? 5 : 3; vi.pVertexAttributeDescriptions = va;
  VkPipelineInputAssemblyStateCreateInfo ia{};
  ia.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
  ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
  VkPipelineViewportStateCreateInfo vp{};
  vp.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
  vp.viewportCount = 1; vp.scissorCount = 1;
  VkPipelineRasterizationStateCreateInfo rs{};
  rs.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
  rs.polygonMode = VK_POLYGON_MODE_FILL;
  rs.cullMode = VK_CULL_MODE_BACK_BIT;
  // Mat4::perspective y'yi ters cevirir (Vulkan NDC y asagi): GL tarzi
  // projeksiyonla sarim CW gorunurdu, ters cevirince yeniden CCW olur.
  // Dogrulama: headless karede zemin (tek yuzlu) gorunuyorsa sarim dogru.
  rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
  rs.lineWidth = 1.0f;
  VkPipelineMultisampleStateCreateInfo ms{};
  ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
  ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
  VkPipelineDepthStencilStateCreateInfo ds{};
  ds.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
  ds.depthTestEnable = VK_TRUE;
  VkPipelineColorBlendAttachmentState cba{};
  cba.colorWriteMask = 0xF;
  VkPipelineColorBlendStateCreateInfo cb{};
  cb.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
  cb.pAttachments = &cba;
  VkDynamicState dyn[2] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
  VkPipelineDynamicStateCreateInfo dsci{};
  dsci.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
  dsci.dynamicStateCount = 2; dsci.pDynamicStates = dyn;
  VkGraphicsPipelineCreateInfo gp{};
  gp.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
  gp.pStages = stages;
  gp.pVertexInputState = &vi; gp.pInputAssemblyState = &ia; gp.pViewportState = &vp;
  gp.pRasterizationState = &rs; gp.pMultisampleState = &ms; gp.pDepthStencilState = &ds;
  gp.pColorBlendState = &cb; gp.pDynamicState = &dsci;
  gp.layout = layout_; gp.renderPass = rp;
  // subpass 0: depth prepass (yalniz vertex, depth yaz)
  gp.stageCount = 1; gp.subpass = 0;
  ds.depthWriteEnable = VK_TRUE; ds.depthCompareOp = VK_COMPARE_OP_LESS;
  cb.attachmentCount = 0;
  if (a.vkCreateGraphicsPipelines(dev_->handle(), VK_NULL_HANDLE, 1, &gp, nullptr, depth) != VK_SUCCESS) return false;
  // subpass 1: renk (depth EQUAL, yazma yok)
  gp.stageCount = 2; gp.subpass = 1;
  ds.depthWriteEnable = VK_FALSE; ds.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
  cb.attachmentCount = 1;
  if (a.vkCreateGraphicsPipelines(dev_->handle(), VK_NULL_HANDLE, 1, &gp, nullptr, color) != VK_SUCCESS) return false;

  // Golge boru hatti: kendi render pass'i, yalniz derinlik.
  stages[0].module = skinned ? skin_shadow_vs_ : shadow_vs_;
  gp.stageCount = 1; gp.subpass = 0; gp.renderPass = shadow_rp_;
  ds.depthWriteEnable = VK_TRUE; ds.depthCompareOp = VK_COMPARE_OP_LESS;
  cb.attachmentCount = 0;
  // Boru hatti depthBias'i KULLANILMIYOR: birimi (r) sürücüye bagli. Mali-G72'de
  // 1.25/2.0 golgeyi tamamen yok etti (peter-panning), NVIDIA'da dogruydu — yani
  // "calisiyor" masaustunde olculdu, cihazda degil. Egilim artik shader'da,
  // dunya uzayinda normal kaydirmasiyla (her cihazda ayni anlam). Tuzaklar 8q.
  rs.depthBiasEnable = VK_FALSE;
  if (a.vkCreateGraphicsPipelines(dev_->handle(), VK_NULL_HANDLE, 1, &gp, nullptr, shadow) != VK_SUCCESS) return false;
  return true;
}

bool Renderer::make_shadow() {
  rhi::VkApi &a = dev_->api();
  const uint32_t size = cfg_.shadow_size ? cfg_.shadow_size : 1;
  shadow_info_.size = size;
  shadow_info_.enabled = false;
  // Format: once D16 (mobil bant genisligi), sonra D32. Hem derinlik ekine hem
  // ORNEKLEMEYE uygun olmali; degilse golge kapanir (sebebi raporlanir).
  const VkFormat want[2] = {VK_FORMAT_D16_UNORM, VK_FORMAT_D32_SFLOAT};
  VkFormat fmt = VK_FORMAT_UNDEFINED;
  bool linear = false;
  for (VkFormat f : want) {
    VkFormatProperties fp{};
    a.vkGetPhysicalDeviceFormatProperties(dev_->physical(), f, &fp);
    const VkFormatFeatureFlags need = VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT;
    if ((fp.optimalTilingFeatures & need) == need) {
      fmt = f;
      linear = (fp.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT) != 0;
      break;
    }
  }
  if (fmt == VK_FORMAT_UNDEFINED) {
    shadow_info_.disabled_reason = "ornekleneblir derinlik formati yok (D16/D32)";
    return false; // descriptor'a baglanacak gecerli goruntu uretilemez
  }
  shadow_info_.format = fmt;
  shadow_info_.linear_filter = linear;

  VkAttachmentDescription att{};
  att.format = fmt;
  att.samples = VK_SAMPLE_COUNT_1_BIT;
  att.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
  att.storeOp = VK_ATTACHMENT_STORE_OP_STORE; // orneklenecek: SAKLA
  att.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
  att.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
  att.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  att.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
  VkAttachmentReference ref{0, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
  VkSubpassDescription sp{};
  sp.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
  sp.pDepthStencilAttachment = &ref;
  VkSubpassDependency dep[2]{};
  dep[0].srcSubpass = VK_SUBPASS_EXTERNAL;
  dep[0].dstSubpass = 0;
  dep[0].srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT; // onceki karenin okumasi
  dep[0].dstStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
  dep[0].srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
  dep[0].dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
  dep[1].srcSubpass = 0;
  dep[1].dstSubpass = VK_SUBPASS_EXTERNAL;
  dep[1].srcStageMask = VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
  dep[1].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT; // ana gecisin okumasi
  dep[1].srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
  dep[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
  VkRenderPassCreateInfo rpi{};
  rpi.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
  rpi.attachmentCount = 1; rpi.pAttachments = &att;
  rpi.subpassCount = 1; rpi.pSubpasses = &sp;
  rpi.dependencyCount = 2; rpi.pDependencies = dep;
  if (a.vkCreateRenderPass(dev_->handle(), &rpi, nullptr, &shadow_rp_) != VK_SUCCESS) return false;

  VkImageCreateInfo ii{};
  ii.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
  ii.imageType = VK_IMAGE_TYPE_2D;
  ii.format = fmt;
  ii.extent = {size, size, 1};
  ii.mipLevels = 1; ii.arrayLayers = 1;
  ii.samples = VK_SAMPLE_COUNT_1_BIT;
  ii.tiling = VK_IMAGE_TILING_OPTIMAL;
  // TRANSIENT DEGIL: ana gecis bunu ORNEKLIYOR, tile'da kalamaz.
  ii.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
  if (a.vkCreateImage(dev_->handle(), &ii, nullptr, &shadow_img_) != VK_SUCCESS) return false;
  VkMemoryRequirements req;
  a.vkGetImageMemoryRequirements(dev_->handle(), shadow_img_, &req);
  if (!dev_->allocate_dedicated(req, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, false, &shadow_mem_)) return false;
  a.vkBindImageMemory(dev_->handle(), shadow_img_, shadow_mem_.memory, shadow_mem_.offset);
  VkImageViewCreateInfo vi{};
  vi.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
  vi.image = shadow_img_;
  vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
  vi.format = fmt;
  vi.subresourceRange = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1};
  if (a.vkCreateImageView(dev_->handle(), &vi, nullptr, &shadow_view_) != VK_SUCCESS) return false;
  VkFramebufferCreateInfo fi{};
  fi.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
  fi.renderPass = shadow_rp_;
  fi.attachmentCount = 1; fi.pAttachments = &shadow_view_;
  fi.width = size; fi.height = size; fi.layers = 1;
  if (a.vkCreateFramebuffer(dev_->handle(), &fi, nullptr, &shadow_fb_) != VK_SUCCESS) return false;

  VkSamplerCreateInfo si{};
  si.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
  si.magFilter = si.minFilter = linear ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
  si.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
  si.addressModeU = si.addressModeV = si.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
  si.compareEnable = VK_TRUE; // donanim PCF
  si.compareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
  si.maxLod = VK_LOD_CLAMP_NONE; // Mali kurali: sampler'da LOD kirpma yok (tek mip zaten)
  si.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
  if (a.vkCreateSampler(dev_->handle(), &si, nullptr, &shadow_sampler_) != VK_SUCCESS) return false;
  shadow_info_.enabled = cfg_.shadow_size > 0;
  if (!shadow_info_.enabled) shadow_info_.disabled_reason = "yapilandirmada kapali (shadow_size = 0)";
  return true;
}

void Renderer::shutdown() {
  if (!dev_) return;
  rhi::VkApi &a = dev_->api();
  a.vkDeviceWaitIdle(dev_->handle());
  for (uint32_t i = 0; i < mesh_count_; i++) {
    if (meshes_[i].vbuf) a.vkDestroyBuffer(dev_->handle(), meshes_[i].vbuf, nullptr);
    if (meshes_[i].ibuf) a.vkDestroyBuffer(dev_->handle(), meshes_[i].ibuf, nullptr);
  }
  for (uint32_t i = 0; i < kMaxFrames; i++) {
    if (ubo_[i]) a.vkDestroyBuffer(dev_->handle(), ubo_[i], nullptr);
    if (lights_buf_[i]) a.vkDestroyBuffer(dev_->handle(), lights_buf_[i], nullptr);
    if (cluster_buf_[i]) a.vkDestroyBuffer(dev_->handle(), cluster_buf_[i], nullptr);
  }
  for (uint32_t i = 0; i < texture_count_; i++) {
    if (textures_[i].view) a.vkDestroyImageView(dev_->handle(), textures_[i].view, nullptr);
    if (textures_[i].image) a.vkDestroyImage(dev_->handle(), textures_[i].image, nullptr);
  }
  if (mat_pool_) a.vkDestroyDescriptorPool(dev_->handle(), mat_pool_, nullptr);
  if (mat_layout_) a.vkDestroyDescriptorSetLayout(dev_->handle(), mat_layout_, nullptr);
  if (tex_sampler_) a.vkDestroySampler(dev_->handle(), tex_sampler_, nullptr);
  if (pipe_depth_) a.vkDestroyPipeline(dev_->handle(), pipe_depth_, nullptr);
  if (pipe_color_) a.vkDestroyPipeline(dev_->handle(), pipe_color_, nullptr);
  if (pipe_shadow_) a.vkDestroyPipeline(dev_->handle(), pipe_shadow_, nullptr);
  if (pipe_skin_depth_) a.vkDestroyPipeline(dev_->handle(), pipe_skin_depth_, nullptr);
  if (pipe_skin_color_) a.vkDestroyPipeline(dev_->handle(), pipe_skin_color_, nullptr);
  if (pipe_skin_shadow_) a.vkDestroyPipeline(dev_->handle(), pipe_skin_shadow_, nullptr);
  if (skin_vs_) a.vkDestroyShaderModule(dev_->handle(), skin_vs_, nullptr);
  if (skin_shadow_vs_) a.vkDestroyShaderModule(dev_->handle(), skin_shadow_vs_, nullptr);
  for (uint32_t i = 0; i < kMaxFrames; i++) if (skin_buf_[i]) a.vkDestroyBuffer(dev_->handle(), skin_buf_[i], nullptr);
  if (pipe_ui_) a.vkDestroyPipeline(dev_->handle(), pipe_ui_, nullptr);
  if (ui_vs_) a.vkDestroyShaderModule(dev_->handle(), ui_vs_, nullptr);
  if (ui_fs_) a.vkDestroyShaderModule(dev_->handle(), ui_fs_, nullptr);
  for (uint32_t i = 0; i < kMaxFrames; i++) if (ui_buf_[i]) a.vkDestroyBuffer(dev_->handle(), ui_buf_[i], nullptr);
  if (shadow_fb_) a.vkDestroyFramebuffer(dev_->handle(), shadow_fb_, nullptr);
  if (shadow_view_) a.vkDestroyImageView(dev_->handle(), shadow_view_, nullptr);
  if (shadow_img_) a.vkDestroyImage(dev_->handle(), shadow_img_, nullptr);
  dev_->free_dedicated(&shadow_mem_);
  if (shadow_sampler_) a.vkDestroySampler(dev_->handle(), shadow_sampler_, nullptr);
  if (shadow_rp_) a.vkDestroyRenderPass(dev_->handle(), shadow_rp_, nullptr);
  if (pool_) a.vkDestroyDescriptorPool(dev_->handle(), pool_, nullptr);
  if (layout_) a.vkDestroyPipelineLayout(dev_->handle(), layout_, nullptr);
  if (set_layout_) a.vkDestroyDescriptorSetLayout(dev_->handle(), set_layout_, nullptr);
  if (vs_) a.vkDestroyShaderModule(dev_->handle(), vs_, nullptr);
  if (fs_) a.vkDestroyShaderModule(dev_->handle(), fs_, nullptr);
  if (shadow_vs_) a.vkDestroyShaderModule(dev_->handle(), shadow_vs_, nullptr);
  dev_ = nullptr;
}

MeshHandle Renderer::create_mesh(const Vertex *verts, uint32_t nverts, const uint32_t *indices, uint32_t nindices) {
  // Mali kurali (sparse-index-buffer): indeks araligi (max-min+1) indeks sayisini
  // asarsa G71 oncesi Mali aradaki BUTUN vertex'leri yukler. Denetim burada, CPU'da
  // ve OFFSET DOGRU: katmanin kendi taramasi alt-ayirmali tamponda blok basini
  // okuyor (VVL issue 45, telefonda %0.00 sahte uyari). Sayac raporlanir.
  if (nindices > 0) {
    uint32_t mn = 0xFFFFFFFFu, mx = 0;
    for (uint32_t i = 0; i < nindices; i++) { if (indices[i] < mn) mn = indices[i]; if (indices[i] > mx) mx = indices[i]; }
    if (mx - mn >= nindices) sparse_mesh_count_++;
  }
  if (mesh_count_ >= cfg_.max_meshes) return MeshHandle{};
  Mesh &m = meshes_[mesh_count_];
  rhi::MemoryAlloc vm, im;
  if (!make_buffer(VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, sizeof(Vertex) * nverts,
                   VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, &m.vbuf, &vm)) return MeshHandle{};
  if (!make_buffer(VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, sizeof(uint32_t) * nindices,
                   VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, &m.ibuf, &im)) return MeshHandle{};
  if (!upload(m.vbuf, verts, sizeof(Vertex) * nverts, 0) || !upload(m.ibuf, indices, sizeof(uint32_t) * nindices, 0))
    return MeshHandle{};
  m.index_count = nindices;
  stats_.meshes = ++mesh_count_;
  return MeshHandle{mesh_count_ - 1};
}

MeshHandle Renderer::create_skinned_mesh(const SkinnedVertex *verts, uint32_t nverts, const uint32_t *indices, uint32_t nindices) {
  if (mesh_count_ >= cfg_.max_meshes) return MeshHandle{};
  Mesh &m = meshes_[mesh_count_];
  rhi::MemoryAlloc vm, im;
  if (!make_buffer(VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, sizeof(SkinnedVertex) * nverts,
                   VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, &m.vbuf, &vm)) return MeshHandle{};
  if (!make_buffer(VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, sizeof(uint32_t) * nindices,
                   VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, &m.ibuf, &im)) return MeshHandle{};
  if (!upload(m.vbuf, verts, sizeof(SkinnedVertex) * nverts, 0) || !upload(m.ibuf, indices, sizeof(uint32_t) * nindices, 0))
    return MeshHandle{};
  m.index_count = nindices;
  m.skinned = true;
  stats_.meshes = ++mesh_count_;
  return MeshHandle{mesh_count_ - 1};
}

TextureHandle Renderer::create_texture_levels(VkFormat fmt, uint32_t w, uint32_t h, uint32_t levels, const uint8_t *const *data,
                                              const uint32_t *sizes) {
  if (texture_count_ >= cfg_.max_textures || levels == 0 || levels > 16) return TextureHandle{};
  rhi::VkApi &a = dev_->api();
  Texture &t = textures_[texture_count_];
  VkImageCreateInfo ii{};
  ii.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
  ii.imageType = VK_IMAGE_TYPE_2D;
  ii.format = fmt;
  ii.extent = {w, h, 1};
  ii.mipLevels = levels;
  ii.arrayLayers = 1;
  ii.samples = VK_SAMPLE_COUNT_1_BIT;
  ii.tiling = VK_IMAGE_TILING_OPTIMAL;
  ii.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
  if (a.vkCreateImage(dev_->handle(), &ii, nullptr, &t.image) != VK_SUCCESS) return TextureHandle{};
  VkMemoryRequirements req;
  a.vkGetImageMemoryRequirements(dev_->handle(), t.image, &req);
  rhi::MemoryAlloc mem;
  if (!dev_->allocate(req, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, false, &mem)) return TextureHandle{};
  a.vkBindImageMemory(dev_->handle(), t.image, mem.memory, mem.offset);
  VkDeviceSize total = 0;
  for (uint32_t i = 0; i < levels; i++) total += (sizes[i] + 15) & ~15u; // seviye baslangici 16'ya hizali (blok = 16 B)
  VkBuffer staging = VK_NULL_HANDLE;
  rhi::MemoryAlloc sm;
  if (!make_buffer(VK_BUFFER_USAGE_TRANSFER_SRC_BIT, total, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                   &staging, &sm))
    return TextureHandle{};
  VkBufferImageCopy regions[16]{};
  VkDeviceSize off = 0;
  uint32_t mw = w, mh = h;
  for (uint32_t i = 0; i < levels; i++) {
    std::memcpy(static_cast<uint8_t *>(sm.mapped) + off, data[i], sizes[i]);
    regions[i].bufferOffset = off;
    regions[i].imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, i, 0, 1};
    regions[i].imageExtent = {mw, mh, 1};
    off += (sizes[i] + 15) & ~15u;
    mw = mw > 1 ? mw / 2 : 1; mh = mh > 1 ? mh / 2 : 1;
  }
  VkCommandBuffer cb = dev_->begin_one_shot();
  image_barrier(a, cb, t.image, 0, levels, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 0,
                VK_ACCESS_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
  a.vkCmdCopyBufferToImage(cb, staging, t.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, levels, regions);
  image_barrier(a, cb, t.image, 0, levels, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
  bool ok = dev_->end_one_shot_and_wait(cb);
  a.vkDestroyBuffer(dev_->handle(), staging, nullptr);
  if (!ok) return TextureHandle{};
  VkImageViewCreateInfo vi{};
  vi.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
  vi.image = t.image;
  vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
  vi.format = fmt;
  vi.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, levels, 0, 1};
  if (a.vkCreateImageView(dev_->handle(), &vi, nullptr, &t.view) != VK_SUCCESS) return TextureHandle{};
  t.w = w; t.h = h; t.mips = levels;
  stats_.textures = ++texture_count_;
  return TextureHandle{texture_count_ - 1};
}

void Renderer::set_camera(const Mat4 &view, const Mat4 &proj) { view_ = view; proj_ = proj; }
void Renderer::set_light(Vec3 dir, Vec3 ambient, float diffuse_scale) { light_dir_ = dir; ambient_ = ambient; diffuse_scale_ = diffuse_scale; }

void Renderer::begin_frame(uint32_t frame_index) {
  frame_ = frame_index % cfg_.frames_in_flight;
  draw_count_ = 0;
  skin_count_ = 0;
  stats_.dropped = 0;
  FrameUbo u;
  u.viewproj = proj_ * view_;
  u.view = view_;
  // Kumelenmis nokta isiklar: CPU atamasi (kare basina, deterministik), GPU okur.
  ClusterStats cs;
  cluster_assign(view_, proj_, point_lights_, point_light_count_, grid_, cluster_masks_, &cs);
  stats_.clusters = cs;
  std::memcpy(cluster_mem_[frame_].mapped, cluster_masks_, sizeof(uint32_t) * grid_.count());
  GpuPointLight gl[kMaxPointLights];
  for (uint32_t i = 0; i < point_light_count_; i++) {
    const PointLight &L = point_lights_[i];
    gl[i].pos_radius[0] = L.pos.x; gl[i].pos_radius[1] = L.pos.y; gl[i].pos_radius[2] = L.pos.z; gl[i].pos_radius[3] = L.radius;
    gl[i].color_intensity[0] = L.color.x; gl[i].color_intensity[1] = L.color.y; gl[i].color_intensity[2] = L.color.z;
    gl[i].color_intensity[3] = L.intensity;
  }
  if (point_light_count_) std::memcpy(lights_mem_[frame_].mapped, gl, sizeof(GpuPointLight) * point_light_count_);
  float sc, bi;
  cluster_slice_params(grid_, &sc, &bi);
  u.cluster_params[0] = sc;
  u.cluster_params[1] = bi;
  u.cluster_params[2] = (float)render_w_ / (float)grid_.x;
  u.cluster_params[3] = (float)render_h_ / (float)grid_.y;
  u.cluster_grid[0] = grid_.x; u.cluster_grid[1] = grid_.y; u.cluster_grid[2] = grid_.z; u.cluster_grid[3] = point_light_count_;
  light_vp_ = directional_light_matrix(light_dir_, shadow_center_, shadow_radius_, shadow_depth_);
  u.light_viewproj = light_vp_;
  u.light_dir[0] = light_dir_.x; u.light_dir[1] = light_dir_.y; u.light_dir[2] = light_dir_.z;
  u.light_dir[3] = cfg_.srgb_target ? 0.0f : 1.0f; // 1: shader sRGB kodlar (UNORM hedef yedegi)
  u.ambient[0] = ambient_.x; u.ambient[1] = ambient_.y; u.ambient[2] = ambient_.z; u.ambient[3] = diffuse_scale_;
  u.shadow_params[0] = shadow_info_.size ? 1.0f / (float)shadow_info_.size : 0.0f;
  u.shadow_params[1] = cfg_.shadow_bias;
  u.shadow_params[2] = shadow_info_.enabled ? 1.0f : 0.0f;
  u.shadow_params[3] = cfg_.shadow_normal_offset;
  std::memcpy(ubo_mem_[frame_].mapped, &u, sizeof u);
}

void Renderer::set_render_size(uint32_t w, uint32_t h) { render_w_ = w ? w : 1; render_h_ = h ? h : 1; }

bool Renderer::add_point_light(const PointLight &l) {
  if (point_light_count_ >= kMaxPointLights) return false;
  point_lights_[point_light_count_++] = l;
  return true;
}

void Renderer::set_shadow_volume(Vec3 center, float radius, float depth) {
  shadow_center_ = center;
  shadow_radius_ = radius;
  shadow_depth_ = depth;
}

Mat4 Renderer::directional_light_matrix(Vec3 dir, Vec3 center, float radius, float depth) {
  Vec3 d = normalize(dir);
  // dir dikeye yakinsa look_at'in up'i ile paralel olur (cross = 0, NaN).
  Vec3 up = (d.y > 0.95f || d.y < -0.95f) ? Vec3{0, 0, 1} : Vec3{0, 1, 0};
  Vec3 eye = center + d * (depth * 0.5f);
  return Mat4::ortho(-radius, radius, -radius, radius, 0.05f, depth) * Mat4::look_at(eye, center, up);
}

void Renderer::record_shadow(VkCommandBuffer cb) {
  if (!shadow_info_.enabled) return;
  rhi::VkApi &a = dev_->api();
  VkClearValue clear{};
  clear.depthStencil = {1.0f, 0};
  VkRenderPassBeginInfo rbi{};
  rbi.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
  rbi.renderPass = shadow_rp_;
  rbi.framebuffer = shadow_fb_;
  rbi.renderArea = {{0, 0}, {shadow_info_.size, shadow_info_.size}};
  rbi.clearValueCount = 1;
  rbi.pClearValues = &clear;
  a.vkCmdBeginRenderPass(cb, &rbi, VK_SUBPASS_CONTENTS_INLINE);
  VkViewport vp{0, 0, (float)shadow_info_.size, (float)shadow_info_.size, 0.0f, 1.0f};
  VkRect2D sc{{0, 0}, {shadow_info_.size, shadow_info_.size}};
  a.vkCmdSetViewport(cb, 0, 1, &vp);
  a.vkCmdSetScissor(cb, 0, 1, &sc);
  a.vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, layout_, 0, 1, &sets_[frame_], 0, nullptr);
  uint32_t bound = 0xFFFFFFFFu;
  VkPipeline bound_pipe = VK_NULL_HANDLE;
  for (uint32_t i = 0; i < draw_count_; i++) {
    const Draw &d = draws_[i];
    const VkPipeline want = d.skin_offset == kNoSkin ? pipe_shadow_ : pipe_skin_shadow_;
    if (want != bound_pipe) { a.vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, want); bound_pipe = want; }
    if (d.mesh != bound) {
      VkDeviceSize off = 0;
      a.vkCmdBindVertexBuffers(cb, 0, 1, &meshes_[d.mesh].vbuf, &off);
      a.vkCmdBindIndexBuffer(cb, meshes_[d.mesh].ibuf, 0, VK_INDEX_TYPE_UINT32);
      bound = d.mesh;
    }
    Push p{d.model, {d.color.x, d.color.y, d.color.z, 1.0f}, {d.skin_offset, 0, 0, 0}};
    a.vkCmdPushConstants(cb, layout_, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof p, &p);
    a.vkCmdDrawIndexed(cb, meshes_[d.mesh].index_count, 1, 0, 0, 0);
  }
  a.vkCmdEndRenderPass(cb);
}

void Renderer::draw(MeshHandle mesh, const Mat4 &model, Vec3 color) { draw(mesh, default_material_, model, color); }

void Renderer::draw(MeshHandle mesh, MaterialHandle material, const Mat4 &model, Vec3 color) {
  if (!mesh.valid() || mesh.id >= mesh_count_) return;
  if (!material.valid() || material.id >= material_count_) material = default_material_;
  if (draw_count_ >= cfg_.max_draws) { stats_.dropped++; return; }
  const Vec3 mc = materials_[material.id].color; // zaten dogrusal
  const Vec3 lc = srgb_to_linear(color);
  if (meshes_[mesh.id].skinned) { stats_.dropped++; return; } // iskeletli mesh draw_skinned ister
  draws_[draw_count_++] = Draw{mesh.id, material.id, model, Vec3{lc.x * mc.x, lc.y * mc.y, lc.z * mc.z}, kNoSkin};
}

void Renderer::draw_skinned(MeshHandle mesh, MaterialHandle material, const Mat4 &model, Vec3 color, const Mat4 *joints,
                            uint32_t n) {
  if (!mesh.valid() || mesh.id >= mesh_count_ || !meshes_[mesh.id].skinned || n == 0) { stats_.dropped++; return; }
  if (!material.valid() || material.id >= material_count_) material = default_material_;
  if (draw_count_ >= cfg_.max_draws || skin_count_ + n > cfg_.max_skin_matrices) { stats_.dropped++; return; }
  std::memcpy(static_cast<Mat4 *>(skin_mem_[frame_].mapped) + skin_count_, joints, sizeof(Mat4) * n);
  const Vec3 mc = materials_[material.id].color;
  const Vec3 lc = srgb_to_linear(color);
  draws_[draw_count_++] = Draw{mesh.id, material.id, model, Vec3{lc.x * mc.x, lc.y * mc.y, lc.z * mc.z}, skin_count_};
  skin_count_ += n;
}

void Renderer::record(VkCommandBuffer cb) {
  rhi::VkApi &a = dev_->api();
  stats_.draws = draw_count_;
  stats_.material_binds = 0;
  for (int pass = 0; pass < 2; pass++) {
    if (pass == 1) a.vkCmdNextSubpass(cb, VK_SUBPASS_CONTENTS_INLINE);
    a.vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, layout_, 0, 1, &sets_[frame_], 0, nullptr);
    uint32_t bound = 0xFFFFFFFFu, bound_mat = 0xFFFFFFFFu;
    VkPipeline bound_pipe = VK_NULL_HANDLE;
    for (uint32_t i = 0; i < draw_count_; i++) {
      const Draw &d = draws_[i];
      const VkPipeline want = d.skin_offset == kNoSkin ? (pass == 0 ? pipe_depth_ : pipe_color_)
                                                       : (pass == 0 ? pipe_skin_depth_ : pipe_skin_color_);
      if (want != bound_pipe) { a.vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, want); bound_pipe = want; }
      if (pass == 1 && d.material != bound_mat) { // depth gecisi doku okumaz
        a.vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, layout_, 1, 1, &materials_[d.material].set, 0, nullptr);
        bound_mat = d.material;
        stats_.material_binds++;
      }
      if (d.mesh != bound) {
        VkDeviceSize off = 0;
        a.vkCmdBindVertexBuffers(cb, 0, 1, &meshes_[d.mesh].vbuf, &off);
        a.vkCmdBindIndexBuffer(cb, meshes_[d.mesh].ibuf, 0, VK_INDEX_TYPE_UINT32);
        bound = d.mesh;
      }
      Push p{d.model, {d.color.x, d.color.y, d.color.z, 1.0f}, {d.skin_offset, 0, 0, 0}};
      a.vkCmdPushConstants(cb, layout_, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof p, &p);
      a.vkCmdDrawIndexed(cb, meshes_[d.mesh].index_count, 1, 0, 0, 0);
    }
  }
}

uint32_t Renderer::cube(Vertex *v, uint32_t *idx) {
  static const float n[6][3] = {{0, 0, 1}, {0, 0, -1}, {1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}};
  // her yuz: normal n, u/v eksenleri
  static const float u[6][3] = {{1, 0, 0}, {-1, 0, 0}, {0, 0, -1}, {0, 0, 1}, {1, 0, 0}, {1, 0, 0}};
  static const float w[6][3] = {{0, 1, 0}, {0, 1, 0}, {0, 1, 0}, {0, 1, 0}, {0, 0, -1}, {0, 0, 1}};
  uint32_t vi = 0, ii = 0;
  for (int f = 0; f < 6; f++) {
    Vec3 N{n[f][0], n[f][1], n[f][2]}, U{u[f][0], u[f][1], u[f][2]}, W{w[f][0], w[f][1], w[f][2]};
    Vec3 c = N * 0.5f;
    Vec3 p0 = c - U * 0.5f - W * 0.5f, p1 = c + U * 0.5f - W * 0.5f, p2 = c + U * 0.5f + W * 0.5f, p3 = c - U * 0.5f + W * 0.5f;
    v[vi + 0] = {p0, N, {0, 0}}; v[vi + 1] = {p1, N, {1, 0}}; v[vi + 2] = {p2, N, {1, 1}}; v[vi + 3] = {p3, N, {0, 1}};
    idx[ii++] = vi; idx[ii++] = vi + 1; idx[ii++] = vi + 2;
    idx[ii++] = vi; idx[ii++] = vi + 2; idx[ii++] = vi + 3;
    vi += 4;
  }
  return ii;
}

uint32_t Renderer::plane(Vertex *v, uint32_t *idx, float uv_repeat) {
  Vec3 N{0, 1, 0};
  const float r = uv_repeat;
  v[0] = {{-0.5f, 0, -0.5f}, N, {0, 0}}; v[1] = {{-0.5f, 0, 0.5f}, N, {0, r}};
  v[2] = {{0.5f, 0, 0.5f}, N, {r, r}};   v[3] = {{0.5f, 0, -0.5f}, N, {r, 0}};
  uint32_t i[6] = {0, 1, 2, 0, 2, 3};
  std::memcpy(idx, i, sizeof i);
  return 6;
}

} // namespace tulpar::engine::renderer
