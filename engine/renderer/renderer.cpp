#include "renderer/renderer.hpp"

#include <cstring>

#include "rhi/shaders/mesh_frag_spv.h"
#include "rhi/shaders/mesh_vert_spv.h"

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
  draws_ = arena.alloc_array<Draw>(cfg.max_draws);
  if (!meshes_ || !draws_) return false;
  rhi::VkApi &a = dev.api();
  // Shader'lar
  VkShaderModuleCreateInfo smi{};
  smi.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
  smi.codeSize = mesh_vert_spv_size; smi.pCode = mesh_vert_spv;
  if (a.vkCreateShaderModule(dev.handle(), &smi, nullptr, &vs_) != VK_SUCCESS) return false;
  smi.codeSize = mesh_frag_spv_size; smi.pCode = mesh_frag_spv;
  if (a.vkCreateShaderModule(dev.handle(), &smi, nullptr, &fs_) != VK_SUCCESS) return false;
  // Set 0: kare UBO
  VkDescriptorSetLayoutBinding b{};
  b.binding = 0;
  b.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
  b.descriptorCount = 1;
  b.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
  VkDescriptorSetLayoutCreateInfo sli{};
  sli.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
  sli.bindingCount = 1;
  sli.pBindings = &b;
  if (a.vkCreateDescriptorSetLayout(dev.handle(), &sli, nullptr, &set_layout_) != VK_SUCCESS) return false;
  VkPushConstantRange pcr{VK_SHADER_STAGE_VERTEX_BIT, 0, 80};
  VkPipelineLayoutCreateInfo pli{};
  pli.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
  pli.setLayoutCount = 1;
  pli.pSetLayouts = &set_layout_;
  pli.pushConstantRangeCount = 1;
  pli.pPushConstantRanges = &pcr;
  if (a.vkCreatePipelineLayout(dev.handle(), &pli, nullptr, &layout_) != VK_SUCCESS) return false;
  // UBO + descriptor (ucuslu kare basina)
  VkDescriptorPoolSize ps{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, kMaxFrames};
  VkDescriptorPoolCreateInfo dpi{};
  dpi.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
  dpi.maxSets = kMaxFrames;
  dpi.poolSizeCount = 1;
  dpi.pPoolSizes = &ps;
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
    VkDescriptorBufferInfo dbi{ubo_[i], 0, sizeof(FrameUbo)};
    VkWriteDescriptorSet w{};
    w.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    w.dstSet = sets_[i];
    w.dstBinding = 0;
    w.descriptorCount = 1;
    w.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    w.pBufferInfo = &dbi;
    a.vkUpdateDescriptorSets(dev.handle(), 1, &w, 0, nullptr);
  }
  return make_pipelines(rp);
}

bool Renderer::make_pipelines(VkRenderPass rp) {
  rhi::VkApi &a = dev_->api();
  VkPipelineShaderStageCreateInfo stages[2]{};
  stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT; stages[0].module = vs_; stages[0].pName = "main";
  stages[1] = stages[0];
  stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT; stages[1].module = fs_;
  VkVertexInputBindingDescription vb{0, sizeof(Vertex), VK_VERTEX_INPUT_RATE_VERTEX};
  VkVertexInputAttributeDescription va[2] = {{0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0},
                                             {1, 0, VK_FORMAT_R32G32B32_SFLOAT, sizeof(Vec3)}};
  VkPipelineVertexInputStateCreateInfo vi{};
  vi.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
  vi.vertexBindingDescriptionCount = 1; vi.pVertexBindingDescriptions = &vb;
  vi.vertexAttributeDescriptionCount = 2; vi.pVertexAttributeDescriptions = va;
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
  if (a.vkCreateGraphicsPipelines(dev_->handle(), VK_NULL_HANDLE, 1, &gp, nullptr, &pipe_depth_) != VK_SUCCESS) return false;
  // subpass 1: renk (depth EQUAL, yazma yok)
  gp.stageCount = 2; gp.subpass = 1;
  ds.depthWriteEnable = VK_FALSE; ds.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
  cb.attachmentCount = 1;
  return a.vkCreateGraphicsPipelines(dev_->handle(), VK_NULL_HANDLE, 1, &gp, nullptr, &pipe_color_) == VK_SUCCESS;
}

void Renderer::shutdown() {
  if (!dev_) return;
  rhi::VkApi &a = dev_->api();
  a.vkDeviceWaitIdle(dev_->handle());
  for (uint32_t i = 0; i < mesh_count_; i++) {
    if (meshes_[i].vbuf) a.vkDestroyBuffer(dev_->handle(), meshes_[i].vbuf, nullptr);
    if (meshes_[i].ibuf) a.vkDestroyBuffer(dev_->handle(), meshes_[i].ibuf, nullptr);
  }
  for (uint32_t i = 0; i < kMaxFrames; i++) if (ubo_[i]) a.vkDestroyBuffer(dev_->handle(), ubo_[i], nullptr);
  if (pipe_depth_) a.vkDestroyPipeline(dev_->handle(), pipe_depth_, nullptr);
  if (pipe_color_) a.vkDestroyPipeline(dev_->handle(), pipe_color_, nullptr);
  if (pool_) a.vkDestroyDescriptorPool(dev_->handle(), pool_, nullptr);
  if (layout_) a.vkDestroyPipelineLayout(dev_->handle(), layout_, nullptr);
  if (set_layout_) a.vkDestroyDescriptorSetLayout(dev_->handle(), set_layout_, nullptr);
  if (vs_) a.vkDestroyShaderModule(dev_->handle(), vs_, nullptr);
  if (fs_) a.vkDestroyShaderModule(dev_->handle(), fs_, nullptr);
  dev_ = nullptr;
}

MeshHandle Renderer::create_mesh(const Vertex *verts, uint32_t nverts, const uint32_t *indices, uint32_t nindices) {
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

void Renderer::set_camera(const Mat4 &view, const Mat4 &proj) { view_ = view; proj_ = proj; }
void Renderer::set_light(Vec3 dir, Vec3 ambient, float diffuse_scale) { light_dir_ = dir; ambient_ = ambient; diffuse_scale_ = diffuse_scale; }

void Renderer::begin_frame(uint32_t frame_index) {
  frame_ = frame_index % cfg_.frames_in_flight;
  draw_count_ = 0;
  stats_.dropped = 0;
  FrameUbo u;
  u.viewproj = proj_ * view_;
  u.light_dir[0] = light_dir_.x; u.light_dir[1] = light_dir_.y; u.light_dir[2] = light_dir_.z; u.light_dir[3] = 0;
  u.ambient[0] = ambient_.x; u.ambient[1] = ambient_.y; u.ambient[2] = ambient_.z; u.ambient[3] = diffuse_scale_;
  std::memcpy(ubo_mem_[frame_].mapped, &u, sizeof u);
}

void Renderer::draw(MeshHandle mesh, const Mat4 &model, Vec3 color) {
  if (!mesh.valid() || mesh.id >= mesh_count_) return;
  if (draw_count_ >= cfg_.max_draws) { stats_.dropped++; return; }
  draws_[draw_count_++] = Draw{mesh.id, model, color};
}

void Renderer::record(VkCommandBuffer cb) {
  rhi::VkApi &a = dev_->api();
  stats_.draws = draw_count_;
  struct Push { Mat4 model; float color[4]; };
  for (int pass = 0; pass < 2; pass++) {
    if (pass == 1) a.vkCmdNextSubpass(cb, VK_SUBPASS_CONTENTS_INLINE);
    a.vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, pass == 0 ? pipe_depth_ : pipe_color_);
    a.vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, layout_, 0, 1, &sets_[frame_], 0, nullptr);
    uint32_t bound = 0xFFFFFFFFu;
    for (uint32_t i = 0; i < draw_count_; i++) {
      const Draw &d = draws_[i];
      if (d.mesh != bound) {
        VkDeviceSize off = 0;
        a.vkCmdBindVertexBuffers(cb, 0, 1, &meshes_[d.mesh].vbuf, &off);
        a.vkCmdBindIndexBuffer(cb, meshes_[d.mesh].ibuf, 0, VK_INDEX_TYPE_UINT32);
        bound = d.mesh;
      }
      Push p{d.model, {d.color.x, d.color.y, d.color.z, 1.0f}};
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
    v[vi + 0] = {p0, N}; v[vi + 1] = {p1, N}; v[vi + 2] = {p2, N}; v[vi + 3] = {p3, N};
    idx[ii++] = vi; idx[ii++] = vi + 1; idx[ii++] = vi + 2;
    idx[ii++] = vi; idx[ii++] = vi + 2; idx[ii++] = vi + 3;
    vi += 4;
  }
  return ii;
}

uint32_t Renderer::plane(Vertex *v, uint32_t *idx) {
  Vec3 N{0, 1, 0};
  v[0] = {{-0.5f, 0, -0.5f}, N}; v[1] = {{-0.5f, 0, 0.5f}, N}; v[2] = {{0.5f, 0, 0.5f}, N}; v[3] = {{0.5f, 0, -0.5f}, N};
  uint32_t i[6] = {0, 1, 2, 0, 2, 3};
  std::memcpy(idx, i, sizeof i);
  return 6;
}

} // namespace tulpar::engine::renderer
