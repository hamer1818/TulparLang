#include "renderer/renderer.hpp"

#include <cmath>
#include <cstring>

#include "rhi/tile_budget.hpp"
#include "rhi/shaders/bloom_bright_frag_spv.h"
#include "rhi/shaders/bloom_down_frag_spv.h"
#include "rhi/shaders/bloom_up_frag_spv.h"
#include "rhi/shaders/compose_frag_spv.h"
#include "rhi/shaders/cull_comp_spv.h"
#include "rhi/shaders/mesh_cull_vert_spv.h"
#include "rhi/shaders/mesh_frag_spv.h"
#include "rhi/shaders/mesh_skin_vert_spv.h"
#include "rhi/shaders/mesh_vert_spv.h"
#include "rhi/shaders/motion_frag_spv.h"
#include "rhi/shaders/motion_vert_spv.h"
#include "rhi/shaders/post_vert_spv.h"
#include "rhi/shaders/shadow_cull_vert_spv.h"
#include "rhi/shaders/shadow_skin_vert_spv.h"
#include "rhi/shaders/shadow_vert_spv.h"
#include "platform/time.hpp"
#include "rhi/shaders/ui_frag_spv.h"
#include "rhi/shaders/ui_overdraw_frag_spv.h"
#include "rhi/shaders/ui_sdf_frag_spv.h"
#include "rhi/shaders/ui_vert_spv.h"

namespace tulpar::engine::renderer {

// Dolayli komut yapisi Vulkan'inkiyle BAYT BAYT ayni olmali: compute shader
// duz uint dizisine yaziyor, surucu VkDrawIndexedIndirectCommand okuyor.
// Kayma derleme hatasi olsun (calisma zamaninda sessiz bozuk cizim yerine).
static_assert(sizeof(GpuIndirectCmd) == sizeof(VkDrawIndexedIndirectCommand), "dolayli komut boyutu");
static_assert(offsetof(GpuIndirectCmd, instance_count) == offsetof(VkDrawIndexedIndirectCommand, instanceCount),
              "dolayli komut: instanceCount ofseti");
static_assert(offsetof(GpuIndirectCmd, first_instance) == offsetof(VkDrawIndexedIndirectCommand, firstInstance),
              "dolayli komut: firstInstance ofseti");

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

// --- Vertex paketleme -------------------------------------------------------
// Oktahedral normal: birim kureyi oktahedron uzerinden kareye acar; snorm16
// ciftinde hata ~0.1 derece (8 bitte ~1.5 derece, gorunur bantlasma yapar).
void Renderer::encode_normal(Vec3 n, int16_t out[2]) {
  const float l1 = std::fabs(n.x) + std::fabs(n.y) + std::fabs(n.z);
  float x = l1 > 0 ? n.x / l1 : 0.0f, y = l1 > 0 ? n.y / l1 : 0.0f;
  if ((l1 > 0 ? n.z / l1 : 1.0f) < 0.0f) { // alt yarim: katla
    const float sx = x >= 0.0f ? 1.0f : -1.0f, sy = y >= 0.0f ? 1.0f : -1.0f;
    const float nx = (1.0f - std::fabs(y)) * sx, ny = (1.0f - std::fabs(x)) * sy;
    x = nx; y = ny;
  }
  auto q = [](float v) {
    v = v < -1.0f ? -1.0f : (v > 1.0f ? 1.0f : v);
    return (int16_t)std::lround(v * 32767.0f);
  };
  out[0] = q(x);
  out[1] = q(y);
}
Vec3 Renderer::decode_normal(const int16_t in[2]) {
  const float ex = (float)in[0] / 32767.0f, ey = (float)in[1] / 32767.0f;
  Vec3 n{ex, ey, 1.0f - std::fabs(ex) - std::fabs(ey)};
  const float t = n.z < 0.0f ? -n.z : 0.0f;
  n.x += n.x >= 0.0f ? -t : t;
  n.y += n.y >= 0.0f ? -t : t;
  return normalize(n);
}

// f32 -> f16 (yuvarlama: en yakina; tasma sonsuza, alt tasma sifira).
static uint16_t half_from_float(float f) {
  uint32_t x;
  std::memcpy(&x, &f, 4);
  const uint32_t sign = (x >> 16) & 0x8000u;
  int32_t exp = (int32_t)((x >> 23) & 0xFF) - 127 + 15;
  uint32_t man = x & 0x7FFFFFu;
  if (exp <= 0) return (uint16_t)sign;             // cok kucuk: sifir
  if (exp >= 31) return (uint16_t)(sign | 0x7C00u); // cok buyuk: sonsuz
  uint16_t h = (uint16_t)(sign | ((uint32_t)exp << 10) | (man >> 13));
  if ((man & 0x1000u) && ((man & 0xFFFu) || (h & 1u))) h++; // en yakina yuvarla
  return h;
}
static void pack_vertex(const Vertex &v, GpuVertex *out) {
  out->pos[0] = v.pos.x; out->pos[1] = v.pos.y; out->pos[2] = v.pos.z;
  Renderer::encode_normal(normalize(v.nrm), out->nrm);
  out->uv[0] = half_from_float(v.uv.x);
  out->uv[1] = half_from_float(v.uv.y);
}

bool Renderer::upload_packed(VkBuffer dst, const Vertex *v, const SkinnedVertex *sv, uint32_t n) {
  rhi::VkApi &a = dev_->api();
  const VkDeviceSize size = (VkDeviceSize)(sv ? sizeof(GpuSkinnedVertex) : sizeof(GpuVertex)) * n;
  VkBuffer staging = VK_NULL_HANDLE;
  rhi::MemoryAlloc sm;
  if (!make_buffer(VK_BUFFER_USAGE_TRANSFER_SRC_BIT, size,
                   VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &staging, &sm))
    return false;
  if (sv) {
    auto *dstv = static_cast<GpuSkinnedVertex *>(sm.mapped);
    for (uint32_t i = 0; i < n; i++) {
      GpuVertex tmp;
      pack_vertex(Vertex{sv[i].pos, sv[i].nrm, sv[i].uv}, &tmp);
      std::memcpy(dstv[i].pos, tmp.pos, sizeof tmp.pos);
      dstv[i].nrm[0] = tmp.nrm[0]; dstv[i].nrm[1] = tmp.nrm[1];
      dstv[i].uv[0] = tmp.uv[0]; dstv[i].uv[1] = tmp.uv[1];
      std::memcpy(dstv[i].joints, sv[i].joints, 4);
      std::memcpy(dstv[i].weights, sv[i].weights, 8);
    }
  } else {
    auto *dstv = static_cast<GpuVertex *>(sm.mapped);
    for (uint32_t i = 0; i < n; i++) pack_vertex(v[i], &dstv[i]);
  }
  VkCommandBuffer cb = dev_->begin_one_shot();
  VkBufferCopy region{0, 0, size};
  a.vkCmdCopyBuffer(cb, staging, dst, 1, &region);
  const bool ok = dev_->end_one_shot_and_wait(cb);
  a.vkDestroyBuffer(dev_->handle(), staging, nullptr);
  if (ok) vertex_bytes_ += (uint64_t)size;
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
  // Stokastik yol: acikken kume basina 2 uint, kapaliyken tek hucrelik kukla
  // (descriptor gecerli kalsin; shader onu zaten hic okumaz).
  stoch_enabled_ = cfg_.stochastic_lights > 0;
  cluster_stoch_ = arena.alloc_array_zeroed<uint32_t>(stoch_enabled_ ? 2 * grid_.count() : 2);
  if (!meshes_ || !textures_ || !materials_ || !draws_ || !cluster_masks_ || !cluster_stoch_) return false;
  // --- Faz 9: GPU cull istegi (kurulum asagida; basarisizlik CPU yoluna duser) ---
  bool want_cull = cfg_.gpu_cull;
  if (!want_cull) cull_.disabled_reason = "yapilandirmada kapali (gpu_cull = false)";
  if (want_cull) {
    max_batches_ = cfg_.max_cull_batches ? cfg_.max_cull_batches : 1;
    if (max_batches_ > cfg_.max_draws) max_batches_ = cfg_.max_draws ? cfg_.max_draws : 1;
    batches_ = arena.alloc_array_zeroed<CullBatchCpu>(max_batches_);
    if (!batches_) {
      want_cull = false;
      cull_.disabled_reason = "kume dizisi icin arena yetmedi";
    }
  }
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
  if (want_cull) {
    smi.codeSize = cull_comp_spv_size; smi.pCode = cull_comp_spv;
    if (a.vkCreateShaderModule(dev.handle(), &smi, nullptr, &cull_cs_) != VK_SUCCESS) return false;
    smi.codeSize = mesh_cull_vert_spv_size; smi.pCode = mesh_cull_vert_spv;
    if (a.vkCreateShaderModule(dev.handle(), &smi, nullptr, &cull_vs_) != VK_SUCCESS) return false;
    smi.codeSize = shadow_cull_vert_spv_size; smi.pCode = shadow_cull_vert_spv;
    if (a.vkCreateShaderModule(dev.handle(), &smi, nullptr, &cull_shadow_vs_) != VK_SUCCESS) return false;
  }
  // Golge hedefi UBO'dan ONCE: descriptor yazarken view+sampler hazir olmali.
  if (!make_shadow()) return false;
  // Set 0: binding 0 kare UBO, binding 1 golge haritasi (karsilastirmali sampler)
  // Set 0: 0 kare UBO, 1 golge, 2 nokta isiklar (UBO), 3 kume maskeleri (SSBO), 4 eklem matrisleri (SSBO)
  // cull acikken +2: 5 cizim kayitlari (SSBO), 6 gorunurluk listesi (SSBO).
  // Ikisi de yalniz DOLAYLI yolun vertex shader'inin okudugu tamponlar.
  VkDescriptorSetLayoutBinding b[8]{};
  b[5].binding = 5;
  b[5].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
  b[5].descriptorCount = 1;
  b[5].stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
  b[6].binding = 6;
  b[6].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
  b[6].descriptorCount = 1;
  b[6].stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
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
  // binding 7: stokastik kuyruk tablosu (SSBO, fragment). Duzende HER ZAMAN
  // var; shader ancak STOCHASTIC ozellestirme sabiti 1 iken okur.
  const uint32_t nb = want_cull ? 7u : 5u;
  b[nb].binding = 7;
  b[nb].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
  b[nb].descriptorCount = 1;
  b[nb].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
  VkDescriptorSetLayoutCreateInfo sli{};
  sli.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
  sli.bindingCount = nb + 1;
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
                                {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, (want_cull ? 5u : 3u) * kMaxFrames}};
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
    if (!make_buffer(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                     sizeof(uint32_t) * 2 * (stoch_enabled_ ? grid_.count() : 1u), host, &stoch_buf_[i],
                     &stoch_mem_[i]))
      return false;
    if (!make_buffer(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, sizeof(Mat4) * cfg_.max_skin_matrices, host, &skin_buf_[i], &skin_mem_[i]))
      return false;
    // --- Faz 9: cull tamponlari (ucuslu kare basina) -----------------------
    // cmd tamponu HOST-VISIBLE: hem dolayli okuma (INDIRECT_BUFFER) hem de
    // kare bittikten sonra CPU'nun sayaci okumasi (CullInfo) icin. Gorunurluk
    // listesi yalniz GPU'da: device-local.
    const VkDeviceSize draw_sz = (VkDeviceSize)sizeof(GpuDrawItem) * cfg_.max_draws;
    const VkDeviceSize batch_sz = (VkDeviceSize)sizeof(GpuBatch) * (max_batches_ ? max_batches_ : 1);
    const VkDeviceSize frustum_sz = (VkDeviceSize)sizeof(float) * 4 * 6 * kMaxCullFrusta;
    const VkDeviceSize cmd_sz = (VkDeviceSize)sizeof(GpuIndirectCmd) * kMaxCullFrusta * (max_batches_ ? max_batches_ : 1);
    const VkDeviceSize vis_sz = (VkDeviceSize)sizeof(uint32_t) * kMaxCullFrusta * cfg_.max_draws;
    if (want_cull) {
      const VkMemoryPropertyFlags h = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
      if (!make_buffer(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, draw_sz, h, &cull_draw_buf_[i], &cull_draw_mem_[i]) ||
          !make_buffer(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, batch_sz, h, &cull_batch_buf_[i], &cull_batch_mem_[i]) ||
          !make_buffer(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, frustum_sz, h, &cull_frustum_buf_[i], &cull_frustum_mem_[i]) ||
          !make_buffer(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT, cmd_sz, h,
                       &cull_cmd_buf_[i], &cull_cmd_mem_[i]) ||
          !make_buffer(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, vis_sz, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                       &cull_vis_buf_[i], &cull_vis_mem_[i])) {
        want_cull = false;
        cull_.disabled_reason = "cull tamponlari ayrilamadi";
      } else {
        cull_.gpu_bytes += (uint64_t)(draw_sz + batch_sz + frustum_sz + cmd_sz + vis_sz);
      }
    }
    VkDescriptorBufferInfo dci2{cull_draw_buf_[i], 0, draw_sz};
    VkDescriptorBufferInfo dvi{cull_vis_buf_[i], 0, vis_sz};
    VkDescriptorBufferInfo dsi{skin_buf_[i], 0, sizeof(Mat4) * cfg_.max_skin_matrices};
    VkDescriptorBufferInfo dbi{ubo_[i], 0, sizeof(FrameUbo)};
    VkDescriptorBufferInfo dli{lights_buf_[i], 0, sizeof(GpuPointLight) * kMaxPointLights};
    VkDescriptorBufferInfo dci{cluster_buf_[i], 0, sizeof(uint32_t) * grid_.count()};
    VkDescriptorBufferInfo dsti{stoch_buf_[i], 0, sizeof(uint32_t) * 2 * (stoch_enabled_ ? grid_.count() : 1u)};
    VkDescriptorImageInfo dii{shadow_sampler_, shadow_view_, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    VkWriteDescriptorSet w[8]{};
    w[5].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    w[5].dstSet = sets_[i];
    w[5].dstBinding = 5;
    w[5].descriptorCount = 1;
    w[5].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    w[5].pBufferInfo = &dci2;
    w[6].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    w[6].dstSet = sets_[i];
    w[6].dstBinding = 6;
    w[6].descriptorCount = 1;
    w[6].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    w[6].pBufferInfo = &dvi;
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
    const uint32_t nw = want_cull ? 7u : 5u;
    w[nw].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    w[nw].dstSet = sets_[i];
    w[nw].dstBinding = 7;
    w[nw].descriptorCount = 1;
    w[nw].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    w[nw].pBufferInfo = &dsti;
    a.vkUpdateDescriptorSets(dev.handle(), nw + 1, w, 0, nullptr);
  }
  // Cull compute boru hatti + kendi descriptor kumesi. Basarisizsa CPU yolu
  // kosar ve sebep CullInfo::disabled_reason'da kalir (sessiz kapanma yok).
  if (want_cull) {
    cull_.enabled = make_cull();
    if (!cull_.enabled && !cull_.disabled_reason[0]) cull_.disabled_reason = "cull boru hatti kurulamadi";
  }
  cull_.shadow = cull_.enabled && cfg_.gpu_cull_shadow;
  if (cull_.enabled && !cfg_.gpu_cull_shadow) cull_.shadow_reason = "yapilandirmada kapali (gpu_cull_shadow = false)";
  // Derlenmis render graph + son islem hedefleri. Basarisiz olursa post KAPANIR
  // (sebep PostInfo::disabled_reason'da) ve bugunku yol aynen kosar — sessiz
  // kapanma yok. post acikken SAHNE boru hatlari cagiranin gecisine degil ic
  // HDR gecisine baglanir (bicimler farkli; gecis uyumlulugu sart).
  // --- Faz 5: hareket vektoru hedefi (post'tan ONCE: tablo onu da icerir) ---
  if (cfg_.temporal.motion_vectors) {
    temporal_.motion = make_motion();
    if (!temporal_.motion && !temporal_.motion_disabled_reason[0])
      temporal_.motion_disabled_reason = "hareket hedefleri kurulamadi";
  } else {
    temporal_.motion_disabled_reason = "yapilandirmada kapali (temporal.motion_vectors = false)";
  }
  if (cfg_.post) {
    post_.enabled = make_post(rp);
    if (!post_.enabled && !post_.disabled_reason[0]) post_.disabled_reason = "son islem hedefleri kurulamadi";
  } else {
    post_.disabled_reason = "yapilandirmada kapali (post = false)";
  }
  // Tablo post ACIKKEN make_post icinde kuruldu (bloom mip sayisi orada
  // kirpiliyor). Kapaliyken burada kurulur: golge + [hareket] + sahne.
  if (!post_.enabled) {
    GraphDesc gd;
    gd.post = false;
    gd.shadow = shadow_info_.enabled;
    gd.motion = temporal_.motion;
    gd.cull = cull_.enabled;
    graph_n_ = graph_build(gd, graph_, kMaxGraphPasses);
    if (graph_n_ && graph_validate(graph_, graph_n_) != nullptr) graph_n_ = 0;
  }
  // Dinamik cozunurluk ic hedef ister: post kapaliyken olcek 1'e kenetlenir.
  temporal_.jitter = cfg_.temporal.jitter;
  temporal_.upscaler = cfg_.temporal.upscaler;
  set_render_scale(cfg_.temporal.render_scale);
  if (!make_pipelines(post_.enabled ? hdr_rp_ : rp)) return false;
  if (!make_ui(rp, arena)) return false;
  // Varsayilan malzeme: 1x1 beyaz doku. Dokusuz cizimler bununla gider; shader tek yol.
  static const uint8_t white[4] = {255, 255, 255, 255};
  default_texture_ = create_texture(white, 1, 1, false);
  default_material_ = create_material(default_texture_, {1, 1, 1});
  return default_texture_.valid() && default_material_.valid();
}

// --- 2B ARAYUZ (FAZ 4) ------------------------------------------------------
//
// Yol: ui_quad/ui_rect CPU kuyruguna (ui_quads_) yazar -> ui_record SIRAYI
// kurar (ui_build_order) -> vertex'leri SIRALI yazar -> ayni (boru hatti,
// atlas) kosularini TEK vkCmdDraw ile cizer. Eski yol dortgeni dogrudan mapped
// tampona yazip her seyi tek harmanli cizimle veriyordu; siralama ancak butun
// kuyruk bilindikten sonra kurulabilecegi icin ara dizi sart.
//
// Uc boru hatti ayni vertex shader'i (ui.vert) ve ayni duzeni (layout_)
// paylasir; fark yalniz harmanlama + fragment shader:
//   pipe_ui_        harmanli  (SRC_ALPHA / 1-SRC_ALPHA)  — glif, yari saydam
//   pipe_ui_opaque_ harmanlamasiz                        — alfa 255 + opak texel
//   pipe_ui_over_   toplamali "her fragment +1"          — OLCUM (tembel)
VkPipeline Renderer::make_ui_pipeline(VkRenderPass rp, UiPipeKind kind) {
  rhi::VkApi &a = dev_->api();
  const bool overdraw = kind == UiPipeKind::Overdraw;
  const bool blend = kind != UiPipeKind::Opaque;
  VkPipelineShaderStageCreateInfo st[2]{};
  st[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  st[0].stage = VK_SHADER_STAGE_VERTEX_BIT; st[0].module = ui_vs_; st[0].pName = "main";
  st[1] = st[0];
  st[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
  st[1].module = overdraw ? ui_over_fs_ : (kind == UiPipeKind::Sdf ? ui_sdf_fs_ : ui_fs_);
  // SDF keskinligi OZELLESTIRME SABITI: boru hattinda sabitlenir, kare icinde
  // push sabiti trafigi yok (UI push araligi zaten yalniz VERTEX asamasinda).
  const VkSpecializationMapEntry sme{0, 0, sizeof(float)};
  const float sharpness = cfg_.ui_sdf_sharpness > 0 ? cfg_.ui_sdf_sharpness : 1.0f;
  VkSpecializationInfo spec{};
  spec.mapEntryCount = 1; spec.pMapEntries = &sme; spec.dataSize = sizeof(float); spec.pData = &sharpness;
  if (kind == UiPipeKind::Sdf) st[1].pSpecializationInfo = &spec;
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
  if (overdraw) {
    // Sayim: dst = dst + 1/255, YALNIZ R kanali (digerleri temiz kalsin).
    cba.colorWriteMask = VK_COLOR_COMPONENT_R_BIT;
    cba.blendEnable = VK_TRUE;
    cba.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
    cba.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
    cba.colorBlendOp = VK_BLEND_OP_ADD;
    cba.srcAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
    cba.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    cba.alphaBlendOp = VK_BLEND_OP_ADD;
  } else if (blend) {
    cba.blendEnable = VK_TRUE;
    cba.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    cba.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    cba.colorBlendOp = VK_BLEND_OP_ADD;
    cba.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    cba.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    cba.alphaBlendOp = VK_BLEND_OP_ADD;
  } else {
    // OPAK: harmanlama YOK. Sonuc alfa=1 harmanlamasiyla ayni (src*1 + dst*0),
    // ama tile bellegi OKUNMAZ — TBDR'da asil kazanc burada.
    cba.blendEnable = VK_FALSE;
  }
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
  VkPipeline out = VK_NULL_HANDLE;
  if (a.vkCreateGraphicsPipelines(dev_->handle(), VK_NULL_HANDLE, 1, &gp, nullptr, &out) != VK_SUCCESS)
    return VK_NULL_HANDLE;
  return out;
}

// Overdraw boru hatti TEMBEL: fragment shader'i vertex shader'in cikislarini
// tuketmiyor (sabit yaziyor) ve bu, dogrulama katmaninin "output not consumed"
// uyarisini uretir. Normal kareler onu hic yaratmaz -> Mali kapisinin sayaclari
// temiz kalir. Olcum istendiginde bir kez kurulur.
bool Renderer::ui_ensure_overdraw_pipeline() {
  if (pipe_ui_over_) return true;
  if (!ui_rp_) return false;
  rhi::VkApi &a = dev_->api();
  if (!ui_over_fs_) {
    VkShaderModuleCreateInfo smi{};
    smi.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    smi.codeSize = ui_overdraw_frag_spv_size; smi.pCode = ui_overdraw_frag_spv;
    if (a.vkCreateShaderModule(dev_->handle(), &smi, nullptr, &ui_over_fs_) != VK_SUCCESS) return false;
  }
  pipe_ui_over_ = make_ui_pipeline(ui_rp_, UiPipeKind::Overdraw);
  return pipe_ui_over_ != VK_NULL_HANDLE;
}

// SDF ornekleme boru hatti da TEMBEL: SDF atlas olmayan (yani bugunku) hicbir
// kurulum onu yaratmaz. Kurulamazsa harmanli yola DUSULUR ve sebep yazilir.
bool Renderer::ui_ensure_sdf_pipeline() {
  if (pipe_ui_sdf_) return true;
  if (ui_sdf_failed_) return false;
  ui_sdf_failed_ = true; // bir kez denenir (kare icinde tekrar tekrar degil)
  if (!ui_rp_) { ui_stats_.sdf_reason = "render pass yok"; return false; }
  rhi::VkApi &a = dev_->api();
  if (!ui_sdf_fs_) {
    VkShaderModuleCreateInfo smi{};
    smi.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    smi.codeSize = ui_sdf_frag_spv_size; smi.pCode = ui_sdf_frag_spv;
    if (a.vkCreateShaderModule(dev_->handle(), &smi, nullptr, &ui_sdf_fs_) != VK_SUCCESS) {
      ui_stats_.sdf_reason = "SDF fragment shader modulu yaratilamadi";
      return false;
    }
  }
  pipe_ui_sdf_ = make_ui_pipeline(ui_rp_, UiPipeKind::Sdf);
  if (!pipe_ui_sdf_) { ui_stats_.sdf_reason = "SDF boru hatti yaratilamadi"; return false; }
  ui_sdf_failed_ = false;
  return true;
}

bool Renderer::make_ui(VkRenderPass rp, Arena &arena) {
  rhi::VkApi &a = dev_->api();
  ui_rp_ = rp;
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
  // CPU kuyrugu + siralama diziler(i): KURULUMDA, kare icinde ayirma yok (A2).
  ui_quad_cap_ = cfg_.ui_max_vertices / 6;
  if (ui_quad_cap_ == 0) ui_quad_cap_ = 1;
  ui_quads_ = arena.alloc_array<UiQuad>(ui_quad_cap_);
  ui_order_ = arena.alloc_array<uint32_t>(ui_quad_cap_);
  ui_scratch_ = arena.alloc_array<uint32_t>(ui_quad_cap_);
  if (!ui_quads_ || !ui_order_ || !ui_scratch_) return false;
  // Retained blok onbellegi (istege bagli; 0 = kapali, her kare yeniden uretilir).
  ui_block_cap_ = cfg_.ui_max_blocks;
  ui_block_quad_cap_ = cfg_.ui_block_max_quads;
  if (ui_block_cap_ && ui_block_quad_cap_) {
    ui_blocks_ = arena.alloc_array_zeroed<UiBlock>(ui_block_cap_);
    ui_block_quads_ = arena.alloc_array<UiQuad>((size_t)ui_block_cap_ * ui_block_quad_cap_);
    if (!ui_blocks_ || !ui_block_quads_) { ui_block_cap_ = 0; ui_block_quads_ = nullptr; }
    else for (uint32_t i = 0; i < ui_block_cap_; i++) ui_blocks_[i] = UiBlock{};
  } else {
    ui_block_cap_ = 0;
  }
  ui_sort_ = cfg_.ui_sort_opaque_first ? UiSortMode::OpaqueFirst : UiSortMode::Source;
  // UI GPU suresi (zaman damgasi). Yoksa OLCUM YOK denir, UI calismaya devam.
  if (dev_->caps().timestamps) {
    VkQueryPoolCreateInfo qi{};
    qi.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
    qi.queryType = VK_QUERY_TYPE_TIMESTAMP;
    qi.queryCount = 2 * kMaxFrames;
    if (a.vkCreateQueryPool(dev_->handle(), &qi, nullptr, &ui_query_) != VK_SUCCESS) {
      ui_query_ = VK_NULL_HANDLE;
      ui_stats_.gpu_timing_reason = "sorgu havuzu yaratilamadi";
    }
  } else {
    ui_stats_.gpu_timing_reason = "cihaz zaman damgasi vermiyor";
  }
  pipe_ui_ = make_ui_pipeline(rp, UiPipeKind::Blend);
  pipe_ui_opaque_ = make_ui_pipeline(rp, UiPipeKind::Opaque);
  // SDF boru hattini KURULUMDA yarat. Eskiden ilk SDF dortgeni cizilirken
  // (ui_emit -> ui_ensure_sdf_pipeline, yani KAYIT yolunun icinde) kuruluyordu
  // ve o karede 0.49-0.52 ms kare ici takilma uretiyordu (RTX 5080'de olculdu;
  // mobilde katbekat fazlasi). Faz 6'nin "runtime'da pipeline kurulumu yok"
  // hedefi tam olarak bunu yasakliyor. Basarisiz olursa harmanli yola dusulur
  // ve sebep ui_stats_.sdf_reason'da kalir — davranis degismedi, yalniz maliyet
  // kareden kurulum zamanina tasindi.
  if (cfg_.ui_prewarm_sdf) ui_ensure_sdf_pipeline();
  // Overdraw boru hatti BILEREK tembel kaldi: fragment shader'i vertex
  // cikislarini tuketmiyor ve dogrulama katmani bunu uyari sayiyor. Normal
  // kare onu ZATEN yaratmaz (tek cagiran ui_record_overdraw, yani acik OLCUM
  // yolu), dolayisiyla "ilk karede kurulan pipeline" sayaci bundan etkilenmez.
  return pipe_ui_ != VK_NULL_HANDLE && pipe_ui_opaque_ != VK_NULL_HANDLE;
}

void Renderer::ui_begin(float w, float h, float rot) {
  const bool geometry_changed = w != ui_w_ || h != ui_h_ || rot != ui_rot_;
  ui_w_ = w > 0 ? w : 1; ui_h_ = h > 0 ? h : 1; ui_rot_ = rot;
  ui_count_ = 0;
  ui_quad_n_ = 0;
  ui_block_active_ = -1;
  ui_sdf_ = false; // immediate-mode durumu: her kare bastan
  ui_warning_ = "";
  const char *keep_reason = ui_stats_.gpu_timing_reason, *keep_sdf = ui_stats_.sdf_reason;
  ui_stats_ = UiStats{};
  ui_stats_.gpu_timing_reason = keep_reason;
  ui_stats_.sdf_reason = keep_sdf; // bir kez yazilir, sonraki karelerde silinmesin
  // Ekran olcusu/dondurmesi degistiyse onbellekteki yerlesim (piksel uzayinda!)
  // BAYAT olur: butun bloklar gecersiz. Sessiz yanlis yerlesim yerine yeniden hesap.
  if (geometry_changed)
    for (uint32_t i = 0; i < ui_block_cap_; i++) ui_blocks_[i].valid = false;
  ui_gen_t0_ = platform::now_ns();
}

void Renderer::ui_end() {
  ui_stats_.cpu_gen_ms = (float)((double)(platform::now_ns() - ui_gen_t0_) / 1.0e6);
  ui_stats_.gen_timed = true;
}

void Renderer::ui_set_atlas(MaterialHandle atlas) { ui_atlas_ = atlas; }

// Kuyruga bir dortgen: onbellek kaydi (kirli blok icindeyse) + tam ekran
// harmanli katman denetimi burada yapilir (tek nokta).
void Renderer::ui_push_quad(float x, float y, float w, float h, float u0, float v0, float u1, float v1, uint32_t c,
                            bool opaque) {
  if (ui_quad_n_ >= ui_quad_cap_) { ui_stats_.dropped++; return; }
  // Atlas cozumu BURADA: gecerli malzeme yoksa dortgen SESSIZCE kaybolmaz,
  // dropped'a yazilir (ui_emit dizinin disina bakmaz).
  const uint32_t atlas_id = ui_atlas_.valid() ? ui_atlas_.id : default_material_.id;
  if (atlas_id >= cfg_.max_materials) { ui_stats_.dropped++; return; }
  UiQuad &q = ui_quads_[ui_quad_n_++];
  q.x = x; q.y = y; q.w = w; q.h = h;
  q.u0 = u0; q.v0 = v0; q.u1 = u1; q.v1 = v1;
  q.rgba = c;
  q.atlas = atlas_id;
  // SDF dortgeni her zaman HARMANLIDIR (kapsama mesafeden uretilir).
  q.sdf = ui_sdf_ ? 1 : 0;
  q.opaque = (opaque && !q.sdf) ? 1 : 0;
  q.hoist = 0;
  q.pad = 0;
  if (q.sdf) ui_stats_.sdf_quads++;
  ui_stats_.generated++;
  // PLAN Faz 4: "tam ekran seffaf katman yasak" — TBDR'da butun tile'lari
  // doldurur (okunur + yazilir). Opak tam ekran dortgen SORUN DEGIL (tile
  // yuklemesini bile kaldirabilir), yalniz HARMANLI olan sayilir.
  if (!opaque) {
    const float ox0 = x > 0 ? x : 0, oy0 = y > 0 ? y : 0;
    const float ox1 = (x + w) < ui_w_ ? (x + w) : ui_w_, oy1 = (y + h) < ui_h_ ? (y + h) : ui_h_;
    if (ox1 > ox0 && oy1 > oy0 && (ox1 - ox0) * (oy1 - oy0) >= cfg_.ui_fullscreen_blend_ratio * ui_w_ * ui_h_) {
      ui_stats_.fullscreen_blended++;
      ui_warning_ = "tam ekrani kaplayan HARMANLI arayuz katmani (TBDR: butun tile'lar dolar)";
    }
  }
  // Kirli blok kaydi: bu dortgen sonraki karelerde yeniden kullanilacak.
  if (ui_block_active_ >= 0) {
    UiBlock &b = ui_blocks_[ui_block_active_];
    if (b.count < ui_block_quad_cap_) ui_block_quads_[(size_t)ui_block_active_ * ui_block_quad_cap_ + b.count++] = q;
    else b.overflow = true;
  }
}

void Renderer::ui_quad(float x, float y, float w, float h, float u0, float v0, float u1, float v1, uint32_t c) {
  // Kaplamanin alfasi BILINMEZ (glif atlasi): harmanli sayilir.
  ui_push_quad(x, y, w, h, u0, v0, u1, v1, c, /*opaque=*/false);
}
void Renderer::ui_quad_opaque(float x, float y, float w, float h, float u0, float v0, float u1, float v1, uint32_t c) {
  ui_push_quad(x, y, w, h, u0, v0, u1, v1, c, /*opaque=*/(c >> 24) == 255u);
}
void Renderer::ui_rect(float x, float y, float w, float h, uint32_t c) {
  // Atlasin (0,0) texeli beyaz opak (font yukleyici garanti eder); atlas yoksa varsayilan 1x1 beyaz.
  // Yani alfa 255 ise sonuc alfasi 1'dir -> harmanlamasiz cizilebilir.
  const float t = 0.5f / 512.0f;
  const bool opaque = (c >> 24) == 255u;
  if (!ui_atlas_.valid()) ui_push_quad(x, y, w, h, 0.5f, 0.5f, 0.5f, 0.5f, c, opaque);
  else ui_push_quad(x, y, w, h, t, t, t, t, c, opaque);
}

// --- Retained yerlesim ------------------------------------------------------
bool Renderer::ui_block_begin(uint32_t id, uint64_t hash) {
  ui_block_active_ = -1;
  if (!ui_block_cap_) return true; // onbellek yok: her zaman uret
  uint32_t slot = ui_block_cap_, free_slot = ui_block_cap_;
  for (uint32_t i = 0; i < ui_block_cap_; i++) {
    if (ui_blocks_[i].id == id) { slot = i; break; }
    if (free_slot == ui_block_cap_ && ui_blocks_[i].id == 0xFFFFFFFFu) free_slot = i;
  }
  if (slot == ui_block_cap_) {
    slot = free_slot != ui_block_cap_ ? free_slot : (id % ui_block_cap_); // dolu: uzerine yaz
    ui_blocks_[slot] = UiBlock{};
    ui_blocks_[slot].id = id;
  }
  UiBlock &b = ui_blocks_[slot];
  if (b.valid && !b.overflow && b.hash == hash) {
    // TEMIZ: onceki karenin dortgenlerini kuyruga kopyala, cagiran hic HESAP yapmaz.
    const UiQuad *src = ui_block_quads_ + (size_t)slot * ui_block_quad_cap_;
    for (uint32_t i = 0; i < b.count; i++) {
      if (ui_quad_n_ >= ui_quad_cap_) { ui_stats_.dropped++; break; }
      ui_quads_[ui_quad_n_++] = src[i];
      ui_stats_.reused++;
    }
    ui_stats_.blocks_reused++;
    return false;
  }
  b.hash = hash;
  b.count = 0;
  b.valid = false;
  b.overflow = false;
  ui_block_active_ = (int32_t)slot;
  ui_stats_.blocks_rebuilt++;
  return true;
}

void Renderer::ui_block_end() {
  if (ui_block_active_ < 0) return;
  UiBlock &b = ui_blocks_[ui_block_active_];
  if (b.overflow) { ui_stats_.blocks_overflow++; b.valid = false; }
  else b.valid = true;
  ui_block_active_ = -1;
}

// --- Siralama ---------------------------------------------------------------
namespace {
struct UiBox {
  float x0 = 1e30f, y0 = 1e30f, x1 = -1e30f, y1 = -1e30f;
  bool empty() const { return x1 <= x0 || y1 <= y0; }
  void add(float x, float y, float w, float h) {
    if (w <= 0 || h <= 0) return;
    if (x < x0) x0 = x;
    if (y < y0) y0 = y;
    if (x + w > x1) x1 = x + w;
    if (y + h > y1) y1 = y + h;
  }
  bool hits(float x, float y, float w, float h) const {
    if (empty() || w <= 0 || h <= 0) return false;
    return !(x + w <= x0 || x >= x1 || y + h <= y0 || y >= y1);
  }
  bool hits(const UiBox &o) const {
    if (empty() || o.empty()) return false;
    return !(o.x1 <= x0 || o.x0 >= x1 || o.y1 <= y0 || o.y0 >= y1);
  }
};
} // namespace

// [from,to) araligini ATLAS'a gore KARARLI sirala — yalniz farkli atlaslarin
// kapsayan kutulari AYRIKSA. Ortusme varsa cizim sirasi gorunur sonucu
// belirler ve siralama YAPILMAZ (dogruluk > batch sayisi).
void Renderer::ui_group_by_atlas(uint32_t from, uint32_t to) {
  if (to - from < 2) return;
  uint32_t ids[kUiMaxAtlasGroups];
  UiBox boxes[kUiMaxAtlasGroups];
  uint32_t k = 0;
  for (uint32_t i = from; i < to; i++) {
    const UiQuad &q = ui_quads_[ui_order_[i]];
    uint32_t g = 0;
    for (; g < k; g++) if (ids[g] == q.atlas) break;
    if (g == k) {
      if (k == kUiMaxAtlasGroups) return; // cok fazla atlas: siralama yok
      ids[k] = q.atlas;
      boxes[k] = UiBox{};
      k++;
    }
    boxes[g].add(q.x, q.y, q.w, q.h);
  }
  if (k < 2) return;
  for (uint32_t i = 0; i < k; i++)
    for (uint32_t j = i + 1; j < k; j++)
      if (boxes[i].hits(boxes[j])) return; // ortusen atlas gruplari: sira korunur
  uint32_t n = 0;
  for (uint32_t g = 0; g < k; g++)
    for (uint32_t i = from; i < to; i++)
      if (ui_quads_[ui_order_[i]].atlas == ids[g]) ui_scratch_[n++] = ui_order_[i];
  for (uint32_t i = 0; i < n; i++) ui_order_[from + i] = ui_scratch_[i];
  ui_stats_.atlas_sorted = true;
}

// ui_order_ = cizim sirasi. Donus: dortgen sayisi.
uint32_t Renderer::ui_build_order() {
  const uint32_t n = ui_quad_n_;
  ui_stats_.hoisted = 0;
  ui_stats_.blocked_hoists = 0;
  ui_stats_.atlas_sorted = false;
  if (ui_sort_ == UiSortMode::BlendFirst) { // KONTROL: bilerek yanlis sira
    uint32_t k = 0;
    for (uint32_t i = 0; i < n; i++) if (!ui_quads_[i].opaque) ui_order_[k++] = i;
    for (uint32_t i = 0; i < n; i++) if (ui_quads_[i].opaque) ui_order_[k++] = i;
    return n;
  }
  if (ui_sort_ == UiSortMode::Source) {
    for (uint32_t i = 0; i < n; i++) ui_order_[i] = i;
    return n;
  }
  // OpaqueFirst — GUVENLI yukseltme.
  //
  // Kural: opak bir dortgen, akista KALAN (yani kendisinden once cizilmeye
  // devam edecek) dortgenlerin BIRLESIK kutusuyla ortusmuyorsa one alinabilir.
  // Kanit: (a) one alinanlar arasinda goreli sira korunur; (b) one alinan bir
  // dortgen, orijinalde ondan ONCE gelen ve akista kalan hicbir dortgenle
  // ortusmez (kutu testi); (c) orijinalde ondan SONRA gelenler zaten sonra
  // cizilir. Yani gorunen piksel degismez.
  UiBox blocked;
  for (uint32_t i = 0; i < n; i++) {
    UiQuad &q = ui_quads_[i];
    if (q.opaque && !blocked.hits(q.x, q.y, q.w, q.h)) {
      q.hoist = 1;
      ui_stats_.hoisted++;
    } else {
      q.hoist = 0;
      if (q.opaque) ui_stats_.blocked_hoists++;
      blocked.add(q.x, q.y, q.w, q.h);
    }
  }
  uint32_t k = 0;
  for (uint32_t i = 0; i < n; i++) if (ui_quads_[i].hoist) ui_order_[k++] = i;
  const uint32_t split = k;
  for (uint32_t i = 0; i < n; i++) if (!ui_quads_[i].hoist) ui_order_[k++] = i;
  // Her grubun ICINDE atlasa gore kararli siralama (guvenliyse): kosu sayisi
  // duser, yani vkCmdDraw sayisi duser.
  ui_group_by_atlas(0, split);
  ui_group_by_atlas(split, n);
  return n;
}

// Vertex'leri SIRALI yazar ve ayni (boru hatti, atlas) kosularini tek cizimde verir.
void Renderer::ui_emit(VkCommandBuffer cb, bool overdraw) {
  rhi::VkApi &a = dev_->api();
  const uint32_t n = ui_build_order();
  UiVertex *dst = static_cast<UiVertex *>(ui_mem_[frame_].mapped);
  uint32_t opaque_n = 0, sdf_n = 0;
  {
    uint32_t distinct = 0;
    uint32_t seen[kUiMaxAtlasGroups];
    for (uint32_t i = 0; i < n; i++) {
      const UiQuad &q = ui_quads_[ui_order_[i]];
      UiVertex *v = dst + (size_t)i * 6;
      const float x = q.x, y = q.y, w = q.w, h = q.h;
      v[0] = {x, y, q.u0, q.v0, q.rgba};         v[1] = {x + w, y, q.u1, q.v0, q.rgba};     v[2] = {x + w, y + h, q.u1, q.v1, q.rgba};
      v[3] = {x, y, q.u0, q.v0, q.rgba};         v[4] = {x + w, y + h, q.u1, q.v1, q.rgba}; v[5] = {x, y + h, q.u0, q.v1, q.rgba};
      if (q.opaque) opaque_n++;
      if (q.sdf) sdf_n++;
      uint32_t g = 0;
      for (; g < distinct; g++) if (seen[g] == q.atlas) break;
      if (g == distinct && distinct < kUiMaxAtlasGroups) seen[distinct++] = q.atlas;
    }
    ui_stats_.atlas_groups = distinct;
  }
  ui_count_ = n * 6;
  ui_stats_.vertices = ui_count_;
  ui_stats_.quads = n;
  ui_stats_.opaque = opaque_n;
  ui_stats_.blended = n - opaque_n;
  ui_stats_.sdf_quads = sdf_n;
  const UiPush push{{ui_w_, ui_h_}, {std::cos(ui_rot_), std::sin(ui_rot_)}, cfg_.srgb_target ? 0.0f : 1.0f};
  a.vkCmdPushConstants(cb, layout_, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof push, &push);
  VkDeviceSize off = 0;
  a.vkCmdBindVertexBuffers(cb, 0, 1, &ui_buf_[frame_], &off);
  VkPipeline bound = VK_NULL_HANDLE;
  uint32_t bound_atlas = 0xFFFFFFFFu, batches = 0, i = 0;
  while (i < n) {
    const UiQuad &q0 = ui_quads_[ui_order_[i]];
    uint32_t j = i + 1;
    while (j < n) {
      const UiQuad &q = ui_quads_[ui_order_[j]];
      if (q.atlas != q0.atlas || (!overdraw && (q.opaque != q0.opaque || q.sdf != q0.sdf))) break;
      j++;
    }
    VkPipeline want;
    if (overdraw) want = pipe_ui_over_;
    else if (q0.sdf && ui_ensure_sdf_pipeline()) want = pipe_ui_sdf_;
    else want = q0.opaque ? pipe_ui_opaque_ : pipe_ui_; // SDF kurulamazsa harmanli yol
    if (want != bound) { a.vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, want); bound = want; }
    if (q0.atlas != bound_atlas) {
      a.vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, layout_, 1, 1, &materials_[q0.atlas].set, 0, nullptr);
      bound_atlas = q0.atlas;
    }
    a.vkCmdDraw(cb, (j - i) * 6, 1, i * 6, 0);
    batches++;
    i = j;
  }
  ui_stats_.batches = batches;
}

void Renderer::ui_timing_reset(VkCommandBuffer cb) {
  if (!ui_query_) return;
  dev_->api().vkCmdResetQueryPool(cb, ui_query_, frame_ * 2, 2); // GECIS DISINDA olmali
  ui_query_reset_[frame_] = true;
  ui_query_written_[frame_] = false;
}

void Renderer::ui_record(VkCommandBuffer cb) {
  const uint64_t t0 = platform::now_ns();
  ui_stats_.quads = ui_quad_n_;
  ui_stats_.vertices = 0;
  ui_count_ = 0;
  if (ui_quad_n_ == 0) {
    ui_stats_.cpu_build_ms = (float)((double)(platform::now_ns() - t0) / 1.0e6);
    return;
  }
  rhi::VkApi &a = dev_->api();
  const bool timing = ui_query_ != VK_NULL_HANDLE && ui_query_reset_[frame_];
  if (timing) a.vkCmdWriteTimestamp(cb, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, ui_query_, frame_ * 2);
  ui_emit(cb, /*overdraw=*/false);
  if (timing) {
    a.vkCmdWriteTimestamp(cb, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, ui_query_, frame_ * 2 + 1);
    ui_query_written_[frame_] = true;
    ui_query_frame_ = frame_;
  } else if (!ui_query_) {
    // sebep make_ui'de yazildi
  } else {
    ui_stats_.gpu_timing_reason = "ui_timing_reset cagrilmadi (gecis disinda sifirlanmali)";
  }
  ui_stats_.cpu_build_ms = (float)((double)(platform::now_ns() - t0) / 1.0e6);
}

// OLCUM karesi: ayni dortgen akisi "her fragment +1" boru hattiyla cizilir.
// Hedef UNORM olmali (1/255 tam temsil edilir, toplam TAMSAYI kalir).
void Renderer::ui_record_overdraw(VkCommandBuffer cb) {
  ui_stats_.quads = ui_quad_n_;
  if (ui_quad_n_ == 0) return;
  if (!ui_ensure_overdraw_pipeline()) return;
  ui_emit(cb, /*overdraw=*/true);
}

UiOverdraw Renderer::ui_overdraw_measure(const uint8_t *rgba, uint32_t w, uint32_t h) {
  UiOverdraw o;
  if (!rgba || !w || !h) return o;
  uint64_t sum = 0;
  for (uint32_t i = 0; i < w * h; i++) {
    const uint8_t c = rgba[i * 4]; // R = bu pikseldeki fragment sayisi
    if (!c) continue;
    sum += c;
    o.covered++;
    if (c == 255) o.saturated++;
  }
  o.shaded = (uint32_t)sum;
  o.ratio = o.covered ? (float)((double)sum / (double)o.covered) : 0.0f;
  o.screen = (float)((double)sum / ((double)w * (double)h));
  return o;
}

const UiStats &Renderer::ui_fetch_stats() {
  ui_stats_.gpu_timing = false;
  if (ui_query_ && ui_query_written_[ui_query_frame_]) {
    uint64_t t[2] = {0, 0};
    const VkResult r = dev_->api().vkGetQueryPoolResults(dev_->handle(), ui_query_, ui_query_frame_ * 2, 2, sizeof t, t,
                                                         sizeof(uint64_t), VK_QUERY_RESULT_64_BIT);
    if (r == VK_SUCCESS && t[1] >= t[0]) {
      ui_stats_.gpu_ms = (float)((double)(t[1] - t[0]) * (double)dev_->caps().timestamp_period_ns / 1.0e6);
      ui_stats_.gpu_timing = true;
      ui_stats_.gpu_timing_reason = "";
    } else if (r != VK_SUCCESS) {
      ui_stats_.gpu_timing_reason = "zaman damgasi henuz hazir degil";
    }
  }
  return ui_stats_;
}

bool Renderer::make_material_layout() {
  rhi::VkApi &a = dev_->api();
  // binding 0: albedo, 1: PBR parametreleri (64 bayt std140), 2: ORM
  // (metallicRoughness), 3: teget-uzayi normal, 4: isima dokusu.
  //
  // NEDEN 3 SAMPLER DAHA, TBDR'DA: sampler EKLENTI (attachment) DEGILDIR —
  // rhi/tile_budget.hpp'nin saydigi iki sayi (eklenti adedi, bit/piksel renk
  // deposu) BU DEGISIKLIKTE AYNEN KALIR; degisen sey yalnizca ornekleme
  // maliyetidir ve o da shader'da malzeme basina TEKDUZE bir dalin ardindadir.
  // Dokusuz malzeme 1x1 varsayilana baglanir ama ONU DA ORNEKLEMEZ (dal kapali).
  constexpr uint32_t kNTex = kMaterialBindings - 1; // 4 combined image sampler
  VkDescriptorSetLayoutBinding b[kMaterialBindings]{};
  for (uint32_t i = 0; i < kMaterialBindings; i++) {
    b[i].binding = i;
    b[i].descriptorType = i == 1 ? VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER : VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    b[i].descriptorCount = 1;
    b[i].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
  }
  VkDescriptorSetLayoutCreateInfo sli{};
  sli.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
  sli.bindingCount = kMaterialBindings;
  sli.pBindings = b;
  if (a.vkCreateDescriptorSetLayout(dev_->handle(), &sli, nullptr, &mat_layout_) != VK_SUCCESS) return false;
  VkDescriptorPoolSize ps[2] = {{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, cfg_.max_materials * kNTex},
                                {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, cfg_.max_materials}};
  VkDescriptorPoolCreateInfo dpi{};
  dpi.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
  dpi.maxSets = cfg_.max_materials;
  dpi.poolSizeCount = 2;
  dpi.pPoolSizes = ps;
  if (a.vkCreateDescriptorPool(dev_->handle(), &dpi, nullptr, &mat_pool_) != VK_SUCCESS) return false;
  // TEK tampon, malzeme basina ofset. Stride cihazin UBO ofset hizasina
  // yuvarlanir (masaustu 64-256, Mali 256): hizalanmamis ofset dogrulama hatasi.
  VkPhysicalDeviceProperties2 pp{};
  pp.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
  a.vkGetPhysicalDeviceProperties2(dev_->physical(), &pp);
  uint32_t align = (uint32_t)pp.properties.limits.minUniformBufferOffsetAlignment;
  if (align < 1) align = 1;
  mat_ubo_stride_ = (uint32_t)((sizeof(MaterialUbo) + align - 1) / align * align);
  if (!make_buffer(VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, (VkDeviceSize)mat_ubo_stride_ * cfg_.max_materials,
                   VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &mat_ubo_,
                   &mat_ubo_mem_))
    return false;
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
  // Varsayilan model: cfg_.pbr_default kapaliyken LAMBERT (bugunku goruntu).
  PbrParams def;
  MaterialHandle h = create_material_impl(albedo, color, def, cfg_.pbr_default, PbrTextures{});
  return h;
}
MaterialHandle Renderer::create_material(TextureHandle albedo, Vec3 color, const PbrParams &pbr) {
  return create_material_impl(albedo, color, pbr, true, PbrTextures{});
}
MaterialHandle Renderer::create_material(TextureHandle albedo, Vec3 color, const PbrParams &pbr, const PbrTextures &tex) {
  return create_material_impl(albedo, color, pbr, true, tex);
}
// Malzeme UBO'suna (set 1, binding 1) yazar. is_pbr, shader'in model dalini secer.
void Renderer::write_material_ubo(uint32_t id) {
  const Material &m = materials_[id];
  MaterialUbo u{};
  u.pbr[0] = m.pbr.metallic;
  u.pbr[1] = m.pbr.roughness;
  u.pbr[2] = m.pbr.reflectance;
  u.pbr[3] = m.is_pbr ? 1.0f : 0.0f;
  const Vec3 e = srgb_to_linear(m.pbr.emissive); // yazar sRGB verir
  u.emissive[0] = e.x; u.emissive[1] = e.y; u.emissive[2] = e.z; u.emissive[3] = m.pbr.emissive_strength;
  // Doku maskeleri: shader bunlara gore MALZEME BASINA TEKDUZE dallanir.
  // Maske 0 iken o sampler'a HIC dokunulmaz — dokusuz malzemenin goruntusu de
  // maliyeti de degismez (A/B md5 kapisi bunu olcuyor).
  u.tex[0] = m.tex.orm.valid() ? 1.0f : 0.0f;
  u.tex[1] = m.tex.normal.valid() ? 1.0f : 0.0f;
  u.tex[2] = m.tex.emissive.valid() ? 1.0f : 0.0f;
  u.tex[3] = m.tex.normal_scale;
  u.tex2[0] = m.tex.orm.valid() ? m.tex.occlusion_strength : 0.0f;
  u.tex2[1] = u.tex2[2] = u.tex2[3] = 0.0f;
  std::memcpy(static_cast<uint8_t *>(mat_ubo_mem_.mapped) + (size_t)id * mat_ubo_stride_, &u, sizeof u);
}
bool Renderer::set_material_pbr(MaterialHandle h, const PbrParams &pbr) {
  if (!h.valid() || h.id >= material_count_) return false;
  materials_[h.id].pbr = pbr;
  materials_[h.id].is_pbr = true;
  write_material_ubo(h.id);
  return true;
}
bool Renderer::set_material_textures(MaterialHandle h, const PbrTextures &tex) {
  if (!h.valid() || h.id >= material_count_) return false;
  if ((tex.orm.valid() && tex.orm.id >= texture_count_) || (tex.normal.valid() && tex.normal.id >= texture_count_) ||
      (tex.emissive.valid() && tex.emissive.id >= texture_count_))
    return false;
  materials_[h.id].tex = tex;
  write_material_set(h.id); // descriptor'lar (2/3/4) + UBO maskesi
  write_material_ubo(h.id);
  return true;
}
PbrTextures Renderer::material_textures(MaterialHandle h) const {
  return (h.valid() && h.id < material_count_) ? materials_[h.id].tex : PbrTextures{};
}
PbrParams Renderer::material_pbr(MaterialHandle h) const {
  return (h.valid() && h.id < material_count_) ? materials_[h.id].pbr : PbrParams{};
}
bool Renderer::material_is_pbr(MaterialHandle h) const {
  return h.valid() && h.id < material_count_ && materials_[h.id].is_pbr;
}
// Set 1'in BUTUN baglamalarini yazar. Verilmeyen doku kanali 1x1 VARSAYILAN
// dokuya baglanir: Vulkan'da shader'in STATIK olarak eristigi her baglama
// gecerli bir descriptor istemeli, ama shader o baglamayi maske dali kapaliyken
// ORNEKLEMEZ. Yani "gecerli ama okunmayan" bir descriptor: dogrulama sessiz,
// GPU maliyeti sifir.
void Renderer::write_material_set(uint32_t id) {
  rhi::VkApi &a = dev_->api();
  const Material &m = materials_[id];
  const uint32_t def_tex = default_texture_.valid() ? default_texture_.id : m.texture;
  const uint32_t ids[4] = {m.texture, m.tex.orm.valid() ? m.tex.orm.id : def_tex,
                           m.tex.normal.valid() ? m.tex.normal.id : def_tex,
                           m.tex.emissive.valid() ? m.tex.emissive.id : def_tex};
  VkDescriptorImageInfo dii[4];
  VkWriteDescriptorSet w[kMaterialBindings]{};
  uint32_t n = 0;
  for (uint32_t i = 0; i < 4; i++) {
    dii[i] = VkDescriptorImageInfo{tex_sampler_, textures_[ids[i]].view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    w[n].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    w[n].dstSet = m.set;
    w[n].dstBinding = i == 0 ? 0u : i + 1u; // 0 albedo, 2 ORM, 3 normal, 4 isima
    w[n].descriptorCount = 1;
    w[n].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    w[n].pImageInfo = &dii[i];
    n++;
  }
  VkDescriptorBufferInfo dbi{mat_ubo_, (VkDeviceSize)id * mat_ubo_stride_, sizeof(MaterialUbo)};
  w[n].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
  w[n].dstSet = m.set;
  w[n].dstBinding = 1;
  w[n].descriptorCount = 1;
  w[n].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
  w[n].pBufferInfo = &dbi;
  n++;
  a.vkUpdateDescriptorSets(dev_->handle(), n, w, 0, nullptr);
}

MaterialHandle Renderer::create_material_impl(TextureHandle albedo, Vec3 color, const PbrParams &pbr, bool is_pbr,
                                              const PbrTextures &tex) {
  if (material_count_ >= cfg_.max_materials || !albedo.valid() || albedo.id >= texture_count_) return MaterialHandle{};
  rhi::VkApi &a = dev_->api();
  Material &m = materials_[material_count_];
  VkDescriptorSetAllocateInfo dai{};
  dai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
  dai.descriptorPool = mat_pool_;
  dai.descriptorSetCount = 1;
  dai.pSetLayouts = &mat_layout_;
  if (a.vkAllocateDescriptorSets(dev_->handle(), &dai, &m.set) != VK_SUCCESS) return MaterialHandle{};
  m.texture = albedo.id;
  m.color = srgb_to_linear(color); // yazar sRGB verir, aydinlatma dogrusal
  m.pbr = pbr;
  m.tex = tex;
  if (m.tex.orm.valid() && m.tex.orm.id >= texture_count_) m.tex.orm = TextureHandle{};
  if (m.tex.normal.valid() && m.tex.normal.id >= texture_count_) m.tex.normal = TextureHandle{};
  if (m.tex.emissive.valid() && m.tex.emissive.id >= texture_count_) m.tex.emissive = TextureHandle{};
  m.is_pbr = is_pbr;
  const uint32_t id = material_count_;
  stats_.materials = ++material_count_;
  write_material_set(id);
  write_material_ubo(id);
  return MaterialHandle{id};
}

bool Renderer::make_pipelines(VkRenderPass rp) {
  if (!make_pipeline_set(rp, false, vs_, shadow_vs_, &pipe_depth_, &pipe_color_, &pipe_shadow_)) return false;
  if (!make_pipeline_set(rp, true, skin_vs_, skin_shadow_vs_, &pipe_skin_depth_, &pipe_skin_color_, &pipe_skin_shadow_))
    return false;
  // DOLAYLI yol: ayni durum, yalniz vertex shader'i (model SSBO'dan okuyan)
  // farkli. Kurulamazsa cull kapanir ve CPU yolu kosar.
  if (cull_.enabled) {
    if (!make_pipeline_set(rp, false, cull_vs_, cull_shadow_vs_, &pipe_cull_depth_, &pipe_cull_color_, &pipe_cull_shadow_)) {
      cull_.enabled = false;
      cull_.shadow = false;
      cull_.disabled_reason = "dolayli yolun boru hatlari kurulamadi";
      // NOT: graph tablosu bu noktada zaten kuruldu ve "cull" gecisini
      // listeliyor olabilir. Tablo tanimlayicidir; calisan yolun DOGRU kaynagi
      // CullInfo'dur (enabled = false, sebep yukarida). Bu dal yalniz surucu
      // boru hatti yaratmayi reddederse kosar.
    }
  }
  return true;
}

// Statik ve iskeletli mesh icin ayni uc boru hatti (depth prepass, renk, golge):
// tek fark vertex duzeni ve vertex shader'i.
bool Renderer::make_pipeline_set(VkRenderPass rp, bool skinned, VkShaderModule main_vs, VkShaderModule shadow_vs_mod,
                                VkPipeline *depth, VkPipeline *color, VkPipeline *shadow) {
  rhi::VkApi &a = dev_->api();
  VkPipelineShaderStageCreateInfo stages[2]{};
  stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT; stages[0].module = main_vs; stages[0].pName = "main";
  stages[1] = stages[0];
  stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT; stages[1].module = fs_;
  // PBR_NDF ozellestirme sabiti (mesh.frag constant_id 0): 0 = dogru GGX
  // (urun), 1 = KONTROL (normalize edilmemis). Boru hatti kurulumunda sabit.
  // constant_id 0 = PBR_NDF (0 dogru GGX, 1 kontrol), 1 = STOCHASTIC (0 kapali).
  const int32_t fs_spec_data[2] = {cfg_.pbr_ndf == NdfMode::Unnormalized ? 1 : 0, stoch_enabled_ ? 1 : 0};
  const VkSpecializationMapEntry fs_entries[2] = {{0, 0, sizeof(int32_t)}, {1, sizeof(int32_t), sizeof(int32_t)}};
  VkSpecializationInfo fs_spec{};
  fs_spec.mapEntryCount = 2;
  fs_spec.pMapEntries = fs_entries;
  fs_spec.dataSize = sizeof fs_spec_data;
  fs_spec.pData = fs_spec_data;
  stages[1].pSpecializationInfo = &fs_spec;
  // PAKETLENMIS yerlesim: pos float3, normal oktahedral SNORM16x2, uv half2.
  // (R16G16_SNORM ve R16G16_SFLOAT Vulkan'da ZORUNLU vertex bicimleridir.)
  VkVertexInputBindingDescription vb{0, skinned ? (uint32_t)sizeof(GpuSkinnedVertex) : (uint32_t)sizeof(GpuVertex),
                                     VK_VERTEX_INPUT_RATE_VERTEX};
  VkVertexInputAttributeDescription va[5] = {{0, 0, VK_FORMAT_R32G32B32_SFLOAT, (uint32_t)offsetof(GpuVertex, pos)},
                                             {1, 0, VK_FORMAT_R16G16_SNORM, (uint32_t)offsetof(GpuVertex, nrm)},
                                             {2, 0, VK_FORMAT_R16G16_SFLOAT, (uint32_t)offsetof(GpuVertex, uv)},
                                             {3, 0, VK_FORMAT_R8G8B8A8_UINT, (uint32_t)offsetof(GpuSkinnedVertex, joints)},
                                             {4, 0, VK_FORMAT_R16G16B16A16_UNORM, (uint32_t)offsetof(GpuSkinnedVertex, weights)}};
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
  stages[0].module = shadow_vs_mod;
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
  // Kademeler TEK goruntude yan yana (atlas): tek ornekleyici baglama, tek
  // gecis, tek framebuffer — dizi katmani ya da N framebuffer yok. Bellek
  // size*size*cascades; kademe sayisi 1 iken bugunku yerlesimle BAYT AYNI.
  uint32_t casc = cfg_.shadow_cascades ? cfg_.shadow_cascades : 1;
  if (casc > kMaxCascades) casc = kMaxCascades;
  const uint32_t atlas_w = size * casc;
  shadow_info_.size = size;
  shadow_info_.cascades = casc;
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
  ii.extent = {atlas_w, size, 1};
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
  fi.width = atlas_w; fi.height = size; fi.layers = 1;
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
    if (stoch_buf_[i]) a.vkDestroyBuffer(dev_->handle(), stoch_buf_[i], nullptr);
  }
  for (uint32_t i = 0; i < texture_count_; i++) {
    if (textures_[i].view) a.vkDestroyImageView(dev_->handle(), textures_[i].view, nullptr);
    if (textures_[i].image) a.vkDestroyImage(dev_->handle(), textures_[i].image, nullptr);
  }
  if (mat_ubo_) a.vkDestroyBuffer(dev_->handle(), mat_ubo_, nullptr);
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
  if (pipe_ui_opaque_) a.vkDestroyPipeline(dev_->handle(), pipe_ui_opaque_, nullptr);
  if (pipe_ui_over_) a.vkDestroyPipeline(dev_->handle(), pipe_ui_over_, nullptr);
  if (pipe_ui_sdf_) a.vkDestroyPipeline(dev_->handle(), pipe_ui_sdf_, nullptr);
  if (ui_query_) a.vkDestroyQueryPool(dev_->handle(), ui_query_, nullptr);
  if (ui_vs_) a.vkDestroyShaderModule(dev_->handle(), ui_vs_, nullptr);
  if (ui_fs_) a.vkDestroyShaderModule(dev_->handle(), ui_fs_, nullptr);
  if (ui_over_fs_) a.vkDestroyShaderModule(dev_->handle(), ui_over_fs_, nullptr);
  if (ui_sdf_fs_) a.vkDestroyShaderModule(dev_->handle(), ui_sdf_fs_, nullptr);
  for (uint32_t i = 0; i < kMaxFrames; i++) if (ui_buf_[i]) a.vkDestroyBuffer(dev_->handle(), ui_buf_[i], nullptr);
  // Son islem (varsa): gecisler, hedefler, boru hatlari.
  for (uint32_t c = 0; c < 2; c++) {
    for (uint32_t i = 0; i < kMaxBloomMips; i++) {
      if (bloom_fb_[c][i]) a.vkDestroyFramebuffer(dev_->handle(), bloom_fb_[c][i], nullptr);
      if (bloom_view_[c][i]) a.vkDestroyImageView(dev_->handle(), bloom_view_[c][i], nullptr);
    }
    if (bloom_img_[c]) a.vkDestroyImage(dev_->handle(), bloom_img_[c], nullptr);
    dev_->free_dedicated(&bloom_mem_[c]);
  }
  if (hdr_fb_) a.vkDestroyFramebuffer(dev_->handle(), hdr_fb_, nullptr);
  if (hdr_view_) a.vkDestroyImageView(dev_->handle(), hdr_view_, nullptr);
  if (hdr_depth_view_) a.vkDestroyImageView(dev_->handle(), hdr_depth_view_, nullptr);
  if (hdr_img_) a.vkDestroyImage(dev_->handle(), hdr_img_, nullptr);
  if (hdr_depth_img_) a.vkDestroyImage(dev_->handle(), hdr_depth_img_, nullptr);
  dev_->free_dedicated(&hdr_mem_);
  dev_->free_dedicated(&hdr_depth_mem_);
  if (hdr_rp_) a.vkDestroyRenderPass(dev_->handle(), hdr_rp_, nullptr);
  if (bloom_rp_) a.vkDestroyRenderPass(dev_->handle(), bloom_rp_, nullptr);
  if (pipe_bright_) a.vkDestroyPipeline(dev_->handle(), pipe_bright_, nullptr);
  if (pipe_down_) a.vkDestroyPipeline(dev_->handle(), pipe_down_, nullptr);
  if (pipe_up_) a.vkDestroyPipeline(dev_->handle(), pipe_up_, nullptr);
  if (pipe_compose_) a.vkDestroyPipeline(dev_->handle(), pipe_compose_, nullptr);
  if (post_vs_) a.vkDestroyShaderModule(dev_->handle(), post_vs_, nullptr);
  if (bright_fs_) a.vkDestroyShaderModule(dev_->handle(), bright_fs_, nullptr);
  if (down_fs_) a.vkDestroyShaderModule(dev_->handle(), down_fs_, nullptr);
  if (up_fs_) a.vkDestroyShaderModule(dev_->handle(), up_fs_, nullptr);
  if (compose_fs_) a.vkDestroyShaderModule(dev_->handle(), compose_fs_, nullptr);
  if (post_pool_) a.vkDestroyDescriptorPool(dev_->handle(), post_pool_, nullptr);
  if (post_layout_) a.vkDestroyPipelineLayout(dev_->handle(), post_layout_, nullptr);
  if (post_set_layout_) a.vkDestroyDescriptorSetLayout(dev_->handle(), post_set_layout_, nullptr);
  if (post_sampler_) a.vkDestroySampler(dev_->handle(), post_sampler_, nullptr);
  // Faz 5: hareket vektoru hedefi ve gecisi.
  if (pipe_motion_) a.vkDestroyPipeline(dev_->handle(), pipe_motion_, nullptr);
  if (motion_fb_) a.vkDestroyFramebuffer(dev_->handle(), motion_fb_, nullptr);
  if (motion_rp_) a.vkDestroyRenderPass(dev_->handle(), motion_rp_, nullptr);
  if (motion_view_) a.vkDestroyImageView(dev_->handle(), motion_view_, nullptr);
  if (motion_depth_view_) a.vkDestroyImageView(dev_->handle(), motion_depth_view_, nullptr);
  if (motion_img_) a.vkDestroyImage(dev_->handle(), motion_img_, nullptr);
  if (motion_depth_img_) a.vkDestroyImage(dev_->handle(), motion_depth_img_, nullptr);
  dev_->free_dedicated(&motion_mem_alloc_);
  dev_->free_dedicated(&motion_depth_mem_);
  if (motion_pool_) a.vkDestroyDescriptorPool(dev_->handle(), motion_pool_, nullptr);
  if (motion_layout_) a.vkDestroyPipelineLayout(dev_->handle(), motion_layout_, nullptr);
  if (motion_set_layout_) a.vkDestroyDescriptorSetLayout(dev_->handle(), motion_set_layout_, nullptr);
  if (motion_vs_) a.vkDestroyShaderModule(dev_->handle(), motion_vs_, nullptr);
  if (motion_fs_) a.vkDestroyShaderModule(dev_->handle(), motion_fs_, nullptr);
  for (uint32_t i = 0; i < kMaxFrames; i++) {
    if (motion_ubo_[i]) a.vkDestroyBuffer(dev_->handle(), motion_ubo_[i], nullptr);
    if (motion_inst_[i]) a.vkDestroyBuffer(dev_->handle(), motion_inst_[i], nullptr);
  }
  if (motion_read_) a.vkDestroyBuffer(dev_->handle(), motion_read_, nullptr);
  // Faz 9: cull (compute + dolayli) kaynaklari.
  if (pipe_cull_) a.vkDestroyPipeline(dev_->handle(), pipe_cull_, nullptr);
  if (pipe_cull_depth_) a.vkDestroyPipeline(dev_->handle(), pipe_cull_depth_, nullptr);
  if (pipe_cull_color_) a.vkDestroyPipeline(dev_->handle(), pipe_cull_color_, nullptr);
  if (pipe_cull_shadow_) a.vkDestroyPipeline(dev_->handle(), pipe_cull_shadow_, nullptr);
  if (cull_pool_) a.vkDestroyDescriptorPool(dev_->handle(), cull_pool_, nullptr);
  if (cull_layout_) a.vkDestroyPipelineLayout(dev_->handle(), cull_layout_, nullptr);
  if (cull_set_layout_) a.vkDestroyDescriptorSetLayout(dev_->handle(), cull_set_layout_, nullptr);
  if (cull_query_) a.vkDestroyQueryPool(dev_->handle(), cull_query_, nullptr);
  if (cull_cs_) a.vkDestroyShaderModule(dev_->handle(), cull_cs_, nullptr);
  if (cull_vs_) a.vkDestroyShaderModule(dev_->handle(), cull_vs_, nullptr);
  if (cull_shadow_vs_) a.vkDestroyShaderModule(dev_->handle(), cull_shadow_vs_, nullptr);
  for (uint32_t i = 0; i < kMaxFrames; i++) {
    if (cull_draw_buf_[i]) a.vkDestroyBuffer(dev_->handle(), cull_draw_buf_[i], nullptr);
    if (cull_batch_buf_[i]) a.vkDestroyBuffer(dev_->handle(), cull_batch_buf_[i], nullptr);
    if (cull_frustum_buf_[i]) a.vkDestroyBuffer(dev_->handle(), cull_frustum_buf_[i], nullptr);
    if (cull_cmd_buf_[i]) a.vkDestroyBuffer(dev_->handle(), cull_cmd_buf_[i], nullptr);
    if (cull_vis_buf_[i]) a.vkDestroyBuffer(dev_->handle(), cull_vis_buf_[i], nullptr);
  }
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

namespace {
// Yerel sinir kuresi: AABB merkezi + merkeze en uzak vertex. AABB merkezi
// (agirlik merkezi degil) secildi — kutu/zemin gibi duzgun gecmislerde daha
// sikidir ve tek gecisle olculur. Kure GPU cull'un tek girdisi: buyugu
// gereksiz cizim, kucugu KAYBOLAN nesne demektir.
template <typename V>
void mesh_bounds(const V *v, uint32_t n, Vec3 *center, float *radius) {
  if (!v || n == 0) { *center = Vec3{0, 0, 0}; *radius = 0.0f; return; }
  Vec3 mn = v[0].pos, mx = v[0].pos;
  for (uint32_t i = 1; i < n; i++) {
    const Vec3 p = v[i].pos;
    if (p.x < mn.x) mn.x = p.x;
    if (p.y < mn.y) mn.y = p.y;
    if (p.z < mn.z) mn.z = p.z;
    if (p.x > mx.x) mx.x = p.x;
    if (p.y > mx.y) mx.y = p.y;
    if (p.z > mx.z) mx.z = p.z;
  }
  const Vec3 c{(mn.x + mx.x) * 0.5f, (mn.y + mx.y) * 0.5f, (mn.z + mx.z) * 0.5f};
  float r2 = 0.0f;
  for (uint32_t i = 0; i < n; i++) {
    const float dx = v[i].pos.x - c.x, dy = v[i].pos.y - c.y, dz = v[i].pos.z - c.z;
    const float d2 = dx * dx + dy * dy + dz * dz;
    if (d2 > r2) r2 = d2;
  }
  *center = c;
  *radius = std::sqrt(r2);
}
} // namespace

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
  if (!make_buffer(VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, sizeof(GpuVertex) * nverts,
                   VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, &m.vbuf, &vm)) return MeshHandle{};
  if (!make_buffer(VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, sizeof(uint32_t) * nindices,
                   VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, &m.ibuf, &im)) return MeshHandle{};
  if (!upload_packed(m.vbuf, verts, nullptr, nverts) || !upload(m.ibuf, indices, sizeof(uint32_t) * nindices, 0))
    return MeshHandle{};
  m.index_count = nindices;
  mesh_bounds(verts, nverts, &m.center, &m.radius);
  stats_.meshes = ++mesh_count_;
  return MeshHandle{mesh_count_ - 1};
}

MeshHandle Renderer::create_skinned_mesh(const SkinnedVertex *verts, uint32_t nverts, const uint32_t *indices, uint32_t nindices) {
  if (mesh_count_ >= cfg_.max_meshes) return MeshHandle{};
  Mesh &m = meshes_[mesh_count_];
  rhi::MemoryAlloc vm, im;
  if (!make_buffer(VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, sizeof(GpuSkinnedVertex) * nverts,
                   VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, &m.vbuf, &vm)) return MeshHandle{};
  if (!make_buffer(VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, sizeof(uint32_t) * nindices,
                   VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, &m.ibuf, &im)) return MeshHandle{};
  if (!upload_packed(m.vbuf, nullptr, verts, nverts) || !upload(m.ibuf, indices, sizeof(uint32_t) * nindices, 0))
    return MeshHandle{};
  m.index_count = nindices;
  mesh_bounds(verts, nverts, &m.center, &m.radius);
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
  ui_query_reset_[frame_] = false;
  ui_query_written_[frame_] = false;
  draw_count_ = 0;
  cull_ready_ = false; // record_cull kaydedene kadar CPU yolu gecerli
  skin_count_ = 0;
  stats_.dropped = 0;
  FrameUbo u;
  // --- Faz 5: alt-piksel jitter (Halton 2,3) --------------------------------
  // Kaydirma clip uzayinda, projeksiyonun SOLUNDAN carpilir: proj_ zaten
  // Android on-dondurmesini (clip uzayinda donus, Tuzaklar 8ab) icerse bile
  // kaydirma SON clip uzayinda, yani gercek framebuffer ekseninde kalir.
  update_scaled_size();
  const Mat4 vp_plain = proj_ * view_; // jitter'siz (hareket vektoru bunu ister)
  jittered_proj_ = proj_;
  temporal_.jitter = cfg_.temporal.jitter;
  temporal_.jitter_x = temporal_.jitter_y = 0.0f;
  if (cfg_.temporal.jitter) {
    const uint32_t ph = cfg_.temporal.jitter_phases ? cfg_.temporal.jitter_phases : 1;
    temporal_.jitter_index = temporal_frame_ % ph;
    // Dizi 1'den baslar: Halton(0) = 0 kaydirmasiz kare demekti.
    const float jx = halton(temporal_.jitter_index + 1, 2) - 0.5f;
    const float jy = halton(temporal_.jitter_index + 1, 3) - 0.5f;
    temporal_.jitter_x = jx;
    temporal_.jitter_y = jy;
    jittered_proj_ = Mat4::translate({2.0f * jx / (float)scaled_w_, 2.0f * jy / (float)scaled_h_, 0.0f}) * proj_;
  }
  temporal_frame_++;
  u.viewproj = jittered_proj_ * view_;
  u.view = view_;
  // Kumelenmis nokta isiklar: CPU atamasi (kare basina, deterministik), GPU okur.
  ClusterStats cs;
  StochasticConfig sconf;
  sconf.budget = cfg_.stochastic_lights;
  sconf.keep = cfg_.stochastic_keep;
  sconf.phases = cfg_.stochastic_phases;
  sconf.compensate = cfg_.stochastic_compensate;
  // MONOTON sayac (ucuslu kare indeksi DEGIL): ornekleme deseni her karede bir
  // adim doner, boylece kuyruktaki her isik sirayla ziyaret edilir.
  sconf.frame = stoch_frame_++;
  cluster_assign(view_, proj_, point_lights_, point_light_count_, grid_, cluster_masks_, &cs,
                 stoch_enabled_ ? &sconf : nullptr, stoch_enabled_ ? cluster_stoch_ : nullptr);
  stats_.clusters = cs;
  std::memcpy(cluster_mem_[frame_].mapped, cluster_masks_, sizeof(uint32_t) * grid_.count());
  if (stoch_enabled_)
    std::memcpy(stoch_mem_[frame_].mapped, cluster_stoch_, sizeof(uint32_t) * 2 * grid_.count());
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
  // Kume tile'i gl_FragCoord uzayindadir: post acikken sahne ic hedefe, dinamik
  // cozunurlukte de onun OLCEKLENMIS alt-dikdortgenine cizilir.
  const uint32_t cw = post_.enabled ? scaled_w_ : render_w_;
  const uint32_t ch = post_.enabled ? scaled_h_ : render_h_;
  u.cluster_params[2] = (float)cw / (float)grid_.x;
  u.cluster_params[3] = (float)ch / (float)grid_.y;
  u.cluster_grid[0] = grid_.x; u.cluster_grid[1] = grid_.y; u.cluster_grid[2] = grid_.z; u.cluster_grid[3] = point_light_count_;
  // --- Kademeli golge: ic ice kutular -------------------------------------
  // Yakin kademeler ODAK noktasinda (kamera hedefi/oyuncu) ve kucuk yaricapli,
  // en dis kademe TUM golge hacmini kapsar (bugunku tek-kademe kutusu). Secim
  // fragment'ta KAPSAMAYA gore yapilir (ilk iceren kademe kazanir), kamera
  // frustum'u bolunmedigi icin projeksiyon matrisi TERSINE cevrilmez —
  // on-dondurmeli (Android) projeksiyonda da dogru calisir.
  const uint32_t casc = shadow_info_.cascades ? shadow_info_.cascades : 1;
  const Vec3 focus = has_focus_ ? shadow_focus_ : shadow_center_;
  for (uint32_t i = 0; i < kMaxCascades; i++) {
    if (i >= casc) { light_vp_[i] = light_vp_[casc - 1]; cascade_radius_[i] = cascade_radius_[casc - 1]; continue; }
    const bool last = (i + 1 == casc);
    // Yaricaplar: en distaki tam hacim, iceridekiler 1/3 oraniyla kuculur.
    float r = shadow_radius_;
    for (uint32_t k = i + 1; k < casc; k++) r /= 3.0f;
    const Vec3 c = last ? shadow_center_ : focus;
    cascade_radius_[i] = r;
    light_vp_[i] = cascade_matrix(light_dir_, c, r, shadow_depth_, shadow_info_.size);
    u.light_viewproj[i] = light_vp_[i];
  }
  for (uint32_t i = casc; i < kMaxCascades; i++) u.light_viewproj[i] = light_vp_[i];
  u.cascade_params[0] = (float)casc;
  u.cascade_params[1] = 1.0f / (float)casc;
  u.cascade_params[2] = shadow_info_.size ? 1.0f / (float)(shadow_info_.size * casc) : 0.0f; // atlas texel x
  u.cascade_params[3] = shadow_info_.size ? 1.0f / (float)shadow_info_.size : 0.0f;          // atlas texel y
  u.light_dir[0] = light_dir_.x; u.light_dir[1] = light_dir_.y; u.light_dir[2] = light_dir_.z;
  // 1: shader sRGB kodlar (UNORM hedef yedegi). post acikken sahne DOGRUSAL
  // kayan noktali HDR hedefine yazar: kodlama birlestirmede yapilir.
  u.light_dir[3] = (post_.enabled || cfg_.srgb_target) ? 0.0f : 1.0f;
  u.ambient[0] = ambient_.x; u.ambient[1] = ambient_.y; u.ambient[2] = ambient_.z; u.ambient[3] = diffuse_scale_;
  u.shadow_params[0] = shadow_info_.size ? 1.0f / (float)shadow_info_.size : 0.0f;
  u.shadow_params[1] = cfg_.shadow_bias;
  u.shadow_params[2] = shadow_info_.enabled ? 1.0f : 0.0f;
  u.shadow_params[3] = cfg_.shadow_normal_offset;
  std::memcpy(ubo_mem_[frame_].mapped, &u, sizeof u);
  // --- Hareket gecisinin kare bloku ----------------------------------------
  // Ilk karede onceki viewproj YOK: bu karenin matrisi kullanilir -> MV 0
  // (uydurma bir hareket degil, "bilmiyorum" yerine "hareket yok").
  if (temporal_.motion) {
    if (!has_prev_vp_) { prev_viewproj_ = vp_plain; has_prev_vp_ = true; }
    MotionUbo m;
    m.viewproj_jit = u.viewproj;
    m.viewproj = vp_plain;
    m.prev_viewproj = prev_viewproj_;
    m.params[0] = (float)motion_w_;
    m.params[1] = (float)motion_h_;
    m.params[2] = m.params[3] = 0.0f;
    std::memcpy(motion_ubo_mem_[frame_].mapped, &m, sizeof m);
    prev_viewproj_ = vp_plain;
  }
  temporal_.render_scale = scale_;
  temporal_.scaled_width = scaled_w_;
  temporal_.scaled_height = scaled_h_;
}

// Radikal ters (van der Corput): i'nin `base` tabanindaki basamaklarini
// noktadan sonra ters sirayla yazar. Halton(2,3) cifti TAA/upscaler'larin
// standart alt-piksel dizisi: dusuk uyumsuzluk (low discrepancy), yani N kare
// sonra pikselin icine DUZGUN dagilir — rastgele kaydirma bunu vermez.
float Renderer::halton(uint32_t i, uint32_t base) {
  if (base < 2) return 0.0f;
  float f = 1.0f, r = 0.0f;
  while (i > 0) {
    f /= (float)base;
    r += f * (float)(i % base);
    i /= base;
  }
  return r;
}

// Dinamik cozunurluk: hedefler EN BUYUK olcude kuruldu, burada yalniz
// KULLANILAN alt-dikdortgen degisir. Ayirma yok, boru hatti yeniden kurulmaz.
void Renderer::update_scaled_size() {
  const uint32_t bw = post_.enabled ? post_w_ : render_w_;
  const uint32_t bh = post_.enabled ? post_h_ : render_h_;
  uint32_t w = (uint32_t)((float)bw * scale_ + 0.5f);
  uint32_t h = (uint32_t)((float)bh * scale_ + 0.5f);
  w &= ~1u; // cift: bloom zinciri ve komsu dokunuslar yarim texel kacirmasin
  h &= ~1u;
  if (w < 2) w = bw < 2 ? (bw ? bw : 1) : 2;
  if (h < 2) h = bh < 2 ? (bh ? bh : 1) : 2;
  if (w > bw) w = bw;
  if (h > bh) h = bh;
  scaled_w_ = w;
  scaled_h_ = h;
}

void Renderer::set_render_scale(float s) {
  if (s < 0.5f) s = 0.5f;
  if (s > 1.0f) s = 1.0f;
  if (!post_.enabled) {
    scale_ = 1.0f;
    temporal_.scale_disabled_reason = "dinamik cozunurluk ic hedef ister (post = false)";
  } else {
    scale_ = s;
    temporal_.scale_disabled_reason = "";
  }
  update_scaled_size();
  temporal_.render_scale = scale_;
  temporal_.scaled_width = scaled_w_;
  temporal_.scaled_height = scaled_h_;
}

void Renderer::set_render_size(uint32_t w, uint32_t h) {
  render_w_ = w ? w : 1;
  render_h_ = h ? h : 1;
  update_scaled_size();
}

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

// Kademe matrisi: ortografik kutu, merkezi ISIK UZAYINDA texel katina KENETLENIR.
// Kenetleme olmazsa odak (kamera) kimildadikca golge kenarlari her karede yarim
// texel kayar ve "yurur" (shadow crawl) — kademeli golgede gorunur titreme.
Mat4 Renderer::cascade_matrix(Vec3 dir, Vec3 center, float radius, float depth, uint32_t tile) {
  const Vec3 d = normalize(dir);
  const Vec3 up = (d.y > 0.95f || d.y < -0.95f) ? Vec3{0, 0, 1} : Vec3{0, 1, 0};
  const Vec3 right = normalize(cross(up, d));
  const Vec3 realup = cross(d, right);
  if (tile > 0 && radius > 0.0f) {
    const float texel = 2.0f * radius / (float)tile;
    // Merkezi isik uzayindaki iki eksende texel katina yuvarla.
    const float px = dot(center, right), py = dot(center, realup);
    const float sx = std::floor(px / texel) * texel - px;
    const float sy = std::floor(py / texel) * texel - py;
    center = center + right * sx + realup * sy;
  }
  return directional_light_matrix(d, center, radius, depth);
}

Mat4 Renderer::directional_light_matrix(Vec3 dir, Vec3 center, float radius, float depth) {
  Vec3 d = normalize(dir);
  // dir dikeye yakinsa look_at'in up'i ile paralel olur (cross = 0, NaN).
  Vec3 up = (d.y > 0.95f || d.y < -0.95f) ? Vec3{0, 0, 1} : Vec3{0, 1, 0};
  Vec3 eye = center + d * (depth * 0.5f);
  return Mat4::ortho(-radius, radius, -radius, radius, 0.05f, depth) * Mat4::look_at(eye, center, up);
}

// ===========================================================================
// FAZ 9 — GPU GORUNURLUK KUMELEME (cull) + DOLAYLI (indirect) CIZIM
// ---------------------------------------------------------------------------
// Akis (kare basina):
//   1. build_batches()  CPU: ardisik (mesh, malzeme) kosulari -> kume; cizim
//      kayitlari + kume kayitlari + frustum duzlemleri SSBO'lara yazilir.
//   2. record_cull()    compute: is grubu X = kume, Y = frustum. Kume icinde
//      SIRA KORUYARAK sikistirir, VkDrawIndexedIndirectCommand dizisini ve
//      instanceCount'u yazar.
//   3. record_scene_indirect() / record_shadow_indirect(): CPU kume basina TEK
//      vkCmdDrawIndexedIndirect verir; hangi orneklerin cizilecegini GPU
//      belirlemistir.
// Cihaz destegi: multiDrawIndirect ve drawIndirectFirstInstance KAPALI
// (rhi/device.cpp acmiyor) -> komut basina drawCount 1 ve firstInstance 0.
// Bu yuzden "kume basina bir komut + gl_InstanceIndex" duzeni secildi; tek
// cagrida N komut (vkCmdDrawIndexedIndirectCount) bu cihaz yapilandirmasinda
// GECERSIZ olurdu.
// ===========================================================================
bool Renderer::make_cull() {
  rhi::VkApi &a = dev_->api();
  // VkApi tablosunda olmayan uc giris noktasi: RHI yuzeyini genisletmek yerine
  // burada, yukleyicinin kendi vkGetDeviceProcAddr'i ile alinir.
  pfn_create_compute_ = (PfnCreateComputePipelines)a.vkGetDeviceProcAddr(dev_->handle(), "vkCreateComputePipelines");
  pfn_dispatch_ = (PfnCmdDispatch)a.vkGetDeviceProcAddr(dev_->handle(), "vkCmdDispatch");
  pfn_draw_indirect_ = (PfnCmdDrawIndexedIndirect)a.vkGetDeviceProcAddr(dev_->handle(), "vkCmdDrawIndexedIndirect");
  if (!pfn_create_compute_ || !pfn_dispatch_ || !pfn_draw_indirect_) {
    cull_.disabled_reason = "cihaz compute / vkCmdDrawIndexedIndirect giris noktalarini vermiyor";
    return false;
  }
  if (!dev_->caps().draw_indirect) {
    cull_.disabled_reason = "cihaz drawIndirect vermiyor";
    return false;
  }
  if (!cull_cs_ || !cull_vs_ || !cull_shadow_vs_) {
    cull_.disabled_reason = "cull shader modulleri yok";
    return false;
  }
  // Compute kumesi: 0 cizimler, 1 kumeler, 2 frustum duzlemleri, 3 dolayli
  // komutlar, 4 gorunurluk listesi.
  VkDescriptorSetLayoutBinding b[5]{};
  for (uint32_t i = 0; i < 5; i++) {
    b[i].binding = i;
    b[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    b[i].descriptorCount = 1;
    b[i].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
  }
  VkDescriptorSetLayoutCreateInfo sli{};
  sli.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
  sli.bindingCount = 5;
  sli.pBindings = b;
  if (a.vkCreateDescriptorSetLayout(dev_->handle(), &sli, nullptr, &cull_set_layout_) != VK_SUCCESS) return false;
  VkPushConstantRange pcr{VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(CullPush)};
  VkPipelineLayoutCreateInfo pli{};
  pli.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
  pli.setLayoutCount = 1;
  pli.pSetLayouts = &cull_set_layout_;
  pli.pushConstantRangeCount = 1;
  pli.pPushConstantRanges = &pcr;
  if (a.vkCreatePipelineLayout(dev_->handle(), &pli, nullptr, &cull_layout_) != VK_SUCCESS) return false;
  VkDescriptorPoolSize ps{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 5 * kMaxFrames};
  VkDescriptorPoolCreateInfo dpi{};
  dpi.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
  dpi.maxSets = kMaxFrames;
  dpi.poolSizeCount = 1;
  dpi.pPoolSizes = &ps;
  if (a.vkCreateDescriptorPool(dev_->handle(), &dpi, nullptr, &cull_pool_) != VK_SUCCESS) return false;
  for (uint32_t i = 0; i < cfg_.frames_in_flight; i++) {
    VkDescriptorSetAllocateInfo dai{};
    dai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    dai.descriptorPool = cull_pool_;
    dai.descriptorSetCount = 1;
    dai.pSetLayouts = &cull_set_layout_;
    if (a.vkAllocateDescriptorSets(dev_->handle(), &dai, &cull_sets_[i]) != VK_SUCCESS) return false;
    VkDescriptorBufferInfo bi[5] = {
        {cull_draw_buf_[i], 0, VK_WHOLE_SIZE},    {cull_batch_buf_[i], 0, VK_WHOLE_SIZE},
        {cull_frustum_buf_[i], 0, VK_WHOLE_SIZE}, {cull_cmd_buf_[i], 0, VK_WHOLE_SIZE},
        {cull_vis_buf_[i], 0, VK_WHOLE_SIZE}};
    VkWriteDescriptorSet w[5]{};
    for (uint32_t k = 0; k < 5; k++) {
      w[k].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
      w[k].dstSet = cull_sets_[i];
      w[k].dstBinding = k;
      w[k].descriptorCount = 1;
      w[k].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
      w[k].pBufferInfo = &bi[k];
    }
    a.vkUpdateDescriptorSets(dev_->handle(), 5, w, 0, nullptr);
  }
  VkPipelineShaderStageCreateInfo st{};
  st.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  st.stage = VK_SHADER_STAGE_COMPUTE_BIT;
  st.module = cull_cs_;
  st.pName = "main";
  VkComputePipelineCreateInfo cpi{};
  cpi.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
  cpi.stage = st;
  cpi.layout = cull_layout_;
  if (pfn_create_compute_(dev_->handle(), VK_NULL_HANDLE, 1, &cpi, nullptr, &pipe_cull_) != VK_SUCCESS) {
    cull_.disabled_reason = "cull compute boru hatti yaratilamadi";
    return false;
  }
  // Zaman damgasi (compute suresi). Yoksa OLCUM YOK denir, cull calismaya devam.
  if (dev_->caps().timestamps) {
    VkQueryPoolCreateInfo qi{};
    qi.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
    qi.queryType = VK_QUERY_TYPE_TIMESTAMP;
    qi.queryCount = 2 * kMaxFrames;
    if (a.vkCreateQueryPool(dev_->handle(), &qi, nullptr, &cull_query_) != VK_SUCCESS) {
      cull_query_ = VK_NULL_HANDLE;
      cull_.timing_reason = "sorgu havuzu yaratilamadi";
    }
  } else {
    cull_.timing_reason = "cihaz zaman damgasi vermiyor (timestampValidBits = 0)";
  }
  return true;
}

// CPU: ardisik ayni (mesh, malzeme) statik cizimleri KUME yapar, cizim
// kayitlarini ve kume kayitlarini SSBO'ya yazar, frustum duzlemlerini kurar.
// Ayirma YOK (butun diziler kurulumda ayrildi). Donus: kume sayisi.
uint32_t Renderer::build_batches() {
  GpuDrawItem *gd = static_cast<GpuDrawItem *>(cull_draw_mem_[frame_].mapped);
  GpuBatch *gb = static_cast<GpuBatch *>(cull_batch_mem_[frame_].mapped);
  cull_batches_n_ = 0;
  cull_.candidates = 0;
  cull_.cpu_draws = 0;
  for (uint32_t i = 0; i < draw_count_; i++) {
    Draw &d = draws_[i];
    d.batch = kNoBatch;
    // Iskeletli cizim dolayli yolun DISINDA: vertex duzeni ve eklem SSBO'su
    // farkli, kendi shader'ini ister. Sayilir (CullInfo::cpu_draws), sessizce
    // kaybolmaz.
    if (d.skin_offset != kNoSkin) { cull_.cpu_draws++; continue; }
    bool cont = false;
    if (cull_batches_n_ > 0) {
      const CullBatchCpu &L = batches_[cull_batches_n_ - 1];
      cont = L.mesh == d.mesh && L.material == d.material && L.first_draw + L.draw_count == i;
    }
    if (!cont) {
      if (cull_batches_n_ >= max_batches_) { cull_.cpu_draws++; continue; } // kapasite: CPU yolu
      batches_[cull_batches_n_] = CullBatchCpu{d.mesh, d.material, i, 0};
      cull_batches_n_++;
    }
    CullBatchCpu &B = batches_[cull_batches_n_ - 1];
    B.draw_count++;
    d.batch = cull_batches_n_ - 1;
    cull_.candidates++;
    GpuDrawItem &g = gd[i];
    std::memcpy(g.model, d.model.m, sizeof g.model);
    g.color[0] = d.color.x; g.color[1] = d.color.y; g.color[2] = d.color.z; g.color[3] = 1.0f;
    const Mesh &m = meshes_[d.mesh];
    g.sphere[0] = m.center.x; g.sphere[1] = m.center.y; g.sphere[2] = m.center.z; g.sphere[3] = m.radius;
    g.misc[0] = d.skin_offset; g.misc[1] = d.material; g.misc[2] = 0; g.misc[3] = 0;
  }
  for (uint32_t b = 0; b < cull_batches_n_; b++) {
    const CullBatchCpu &B = batches_[b];
    GpuBatch &g = gb[b];
    g.first_draw = B.first_draw;
    g.draw_count = B.draw_count;
    g.index_count = meshes_[B.mesh].index_count;
    // Mesh basina ayri vertex/indeks tamponu: aralik her zaman basindan.
    // Tek arenaya gecildiginde burasi dolar, dolayli komut degismez.
    g.first_index = 0;
    g.vertex_offset = 0;
    g.pad[0] = g.pad[1] = g.pad[2] = 0;
  }
  // Frustumlar: 0 kamera (JITTER'SIZ), 1..casc golge kademeleri.
  float *fp = static_cast<float *>(cull_frustum_mem_[frame_].mapped);
  const Frustum cam = frustum_from_viewproj(proj_ * view_);
  std::memcpy(fp, cam.p, sizeof cam.p);
  cull_frusta_n_ = 1;
  cull_.shadow_candidates = 0;
  if (cull_.shadow && shadow_info_.enabled) {
    const uint32_t casc = shadow_info_.cascades ? shadow_info_.cascades : 1;
    for (uint32_t c = 0; c < casc && cull_frusta_n_ < kMaxCullFrusta; c++) {
      const Frustum f = frustum_from_viewproj(light_vp_[c]);
      std::memcpy(fp + cull_frusta_n_ * 24, f.p, sizeof f.p);
      cull_frusta_n_++;
    }
    cull_.shadow_candidates = cull_.candidates * (cull_frusta_n_ - 1);
  }
  cull_.frusta = cull_frusta_n_;
  cull_.batches = cull_batches_n_;
  return cull_batches_n_;
}

void Renderer::record_cull(VkCommandBuffer cb) {
  cull_ready_ = false;
  if (!cull_.enabled) return;
  rhi::VkApi &a = dev_->api();
  const uint32_t n = build_batches();
  if (cull_query_) {
    a.vkCmdResetQueryPool(cb, cull_query_, frame_ * 2, 2);
    a.vkCmdWriteTimestamp(cb, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, cull_query_, frame_ * 2);
  }
  if (n) {
    a.vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_COMPUTE, pipe_cull_);
    a.vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_COMPUTE, cull_layout_, 0, 1, &cull_sets_[frame_], 0, nullptr);
    CullPush p{{n, cull_frusta_n_, max_batches_, cfg_.max_draws}};
    a.vkCmdPushConstants(cb, cull_layout_, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof p, &p);
    pfn_dispatch_(cb, n, cull_frusta_n_, 1);
  }
  if (cull_query_) {
    a.vkCmdWriteTimestamp(cb, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, cull_query_, frame_ * 2 + 1);
    cull_query_written_[frame_] = true;
  }
  if (n) {
    // compute yazdi -> (a) surucu dolayli komutu okuyacak, (b) vertex shader
    // gorunurluk listesini okuyacak, (c) kare bitince CPU sayaci okuyacak.
    VkBufferMemoryBarrier bb[2]{};
    for (uint32_t i = 0; i < 2; i++) {
      bb[i].sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
      bb[i].srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
      bb[i].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
      bb[i].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
      bb[i].offset = 0;
      bb[i].size = VK_WHOLE_SIZE;
    }
    bb[0].buffer = cull_cmd_buf_[frame_];
    bb[0].dstAccessMask = VK_ACCESS_INDIRECT_COMMAND_READ_BIT | VK_ACCESS_HOST_READ_BIT;
    bb[1].buffer = cull_vis_buf_[frame_];
    bb[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    a.vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                           VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT | VK_PIPELINE_STAGE_VERTEX_SHADER_BIT |
                               VK_PIPELINE_STAGE_HOST_BIT,
                           0, 0, nullptr, 2, bb, 0, nullptr);
  }
  cull_read_frame_ = frame_;
  cull_recorded_ = true;
  cull_ready_ = true;
}

// GPU sayaclarini okur. Kare TAMAMLANDIKTAN sonra cagrilmali (tampon
// host-coherent; bariyer HOST_READ'i kapsiyor).
CullInfo Renderer::cull() {
  if (!cull_.enabled || !cull_recorded_) return cull_;
  const uint32_t f = cull_read_frame_;
  const GpuIndirectCmd *c = static_cast<const GpuIndirectCmd *>(cull_cmd_mem_[f].mapped);
  if (!c) return cull_;
  uint32_t surv = 0, shadow_surv = 0;
  for (uint32_t b = 0; b < cull_batches_n_; b++) surv += c[b].instance_count;
  for (uint32_t fi = 1; fi < cull_frusta_n_; fi++)
    for (uint32_t b = 0; b < cull_batches_n_; b++) shadow_surv += c[fi * max_batches_ + b].instance_count;
  cull_.survived = surv;
  cull_.culled = cull_.candidates >= surv ? cull_.candidates - surv : 0;
  cull_.shadow_survived = shadow_surv;
  cull_.counts_valid = true;
  // Kayit aninda CPU kac ornegin cizilecegini BILEMEZ (GPU cull'un tanimi);
  // gercek sayi ancak burada, GPU sayacindan gelir.
  stats_.draws = surv + cull_.cpu_draws;
  cull_.timing = false;
  if (cull_query_ && cull_query_written_[f]) {
    uint64_t t[2] = {0, 0};
    const VkResult r = dev_->api().vkGetQueryPoolResults(dev_->handle(), cull_query_, f * 2, 2, sizeof t, t,
                                                         sizeof(uint64_t), VK_QUERY_RESULT_64_BIT);
    if (r == VK_SUCCESS && t[1] >= t[0]) {
      cull_.compute_ms = (float)((double)(t[1] - t[0]) * (double)dev_->caps().timestamp_period_ns / 1.0e6);
      cull_.timing = true;
      cull_.timing_reason = "";
    } else if (r != VK_SUCCESS) {
      cull_.timing_reason = "zaman damgasi henuz hazir degil";
    }
  }
  return cull_;
}

// Sahne gecisi, DOLAYLI yol. Cizim sirasi CPU yoluyla AYNIDIR: kumeler cizim
// listesindeki kosulardir ve cull sikistirmasi sira korur.
void Renderer::record_scene_indirect(VkCommandBuffer cb) {
  rhi::VkApi &a = dev_->api();
  stats_.draws = draw_count_; // aday; gercek sayi cull() ile GPU sayacindan
  stats_.material_binds = 0;
  for (int pass = 0; pass < 2; pass++) {
    if (pass == 1) a.vkCmdNextSubpass(cb, VK_SUBPASS_CONTENTS_INLINE);
    a.vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, layout_, 0, 1, &sets_[frame_], 0, nullptr);
    uint32_t bound = 0xFFFFFFFFu, bound_mat = 0xFFFFFFFFu;
    VkPipeline bound_pipe = VK_NULL_HANDLE;
    for (uint32_t b = 0; b < cull_batches_n_; b++) {
      const CullBatchCpu &B = batches_[b];
      const VkPipeline want = pass == 0 ? pipe_cull_depth_ : pipe_cull_color_;
      if (want != bound_pipe) { a.vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, want); bound_pipe = want; }
      if (pass == 1 && B.material != bound_mat) {
        a.vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, layout_, 1, 1, &materials_[B.material].set, 0,
                                  nullptr);
        bound_mat = B.material;
        stats_.material_binds++;
      }
      if (B.mesh != bound) {
        VkDeviceSize off = 0;
        a.vkCmdBindVertexBuffers(cb, 0, 1, &meshes_[B.mesh].vbuf, &off);
        a.vkCmdBindIndexBuffer(cb, meshes_[B.mesh].ibuf, 0, VK_INDEX_TYPE_UINT32);
        bound = B.mesh;
      }
      Push p{};
      p.skin[2] = B.first_draw; // frustum 0 -> taban = 0 * max_draws + ilk cizim
      a.vkCmdPushConstants(cb, layout_, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof p, &p);
      pfn_draw_indirect_(cb, cull_cmd_buf_[frame_], (VkDeviceSize)b * sizeof(GpuIndirectCmd), 1,
                         (uint32_t)sizeof(GpuIndirectCmd));
    }
    // Kume disinda kalanlar (iskeletli / kapasite): bugunku CPU yolu.
    for (uint32_t i = 0; i < draw_count_; i++) {
      const Draw &d = draws_[i];
      if (d.batch != kNoBatch) continue;
      const VkPipeline want = d.skin_offset == kNoSkin ? (pass == 0 ? pipe_depth_ : pipe_color_)
                                                       : (pass == 0 ? pipe_skin_depth_ : pipe_skin_color_);
      if (want != bound_pipe) { a.vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, want); bound_pipe = want; }
      if (pass == 1 && d.material != bound_mat) {
        a.vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, layout_, 1, 1, &materials_[d.material].set, 0,
                                  nullptr);
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

// Golge gecisi + (post acikken) sahne HDR gecisi ve bloom zinciri. Cagiran
// sozlesmesi degismesin diye butun KENDI gecislerimiz burada: cagiran bunu
// kendi render pass'ini ACMADAN once cagiriyor (bugun de oyle).
void Renderer::record_shadow(VkCommandBuffer cb) {
  // UI zaman damgasi havuzu BURADA sifirlanir: vkCmdResetQueryPool render pass
  // ICINDE cagrilamaz ve bu, cagiran sozlesmesindeki tek "gecis disi" nokta.
  // Boylece mevcut cagiranlar (demo, editor, kopru, testler) hicbir sey
  // degistirmeden UI GPU suresini olcer.
  ui_timing_reset(cb);
  // Cull gecisi ILK: golge de sahne de onun yazdigi dolayli komutlari okur.
  record_cull(cb);
  if (shadow_info_.enabled) record_shadow_pass(cb);
  if (temporal_.motion) record_motion_pass(cb);
  if (post_.enabled) {
    record_hdr_scene(cb);
    record_post_chain(cb);
  }
}

void Renderer::record_shadow_pass(VkCommandBuffer cb) {
  rhi::VkApi &a = dev_->api();
  VkClearValue clear{};
  clear.depthStencil = {1.0f, 0};
  VkRenderPassBeginInfo rbi{};
  rbi.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
  rbi.renderPass = shadow_rp_;
  rbi.framebuffer = shadow_fb_;
  const uint32_t casc = shadow_info_.cascades ? shadow_info_.cascades : 1;
  const uint32_t atlas_w = shadow_info_.size * casc;
  rbi.renderArea = {{0, 0}, {atlas_w, shadow_info_.size}};
  rbi.clearValueCount = 1;
  rbi.pClearValues = &clear;
  a.vkCmdBeginRenderPass(cb, &rbi, VK_SUBPASS_CONTENTS_INLINE);
  a.vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, layout_, 0, 1, &sets_[frame_], 0, nullptr);
  // Kademeler ayni gecis icinde, atlas tile'lari olarak: viewport/scissor tile'a
  // kurulur, kademe indeksi push sabitinden (skin.y) shader'a gider.
  // Golge kademesi c, cull gecisinde frustum 1+c olarak test edildi.
  const bool indirect = cull_.enabled && cull_ready_ && cull_.shadow && cull_frusta_n_ > 1;
  for (uint32_t c = 0; c < casc; c++) {
    const float x0 = (float)(shadow_info_.size * c);
    VkViewport vp{x0, 0, (float)shadow_info_.size, (float)shadow_info_.size, 0.0f, 1.0f};
    VkRect2D sc{{(int32_t)(shadow_info_.size * c), 0}, {shadow_info_.size, shadow_info_.size}};
    a.vkCmdSetViewport(cb, 0, 1, &vp);
    a.vkCmdSetScissor(cb, 0, 1, &sc);
    uint32_t bound = 0xFFFFFFFFu;
    VkPipeline bound_pipe = VK_NULL_HANDLE;
    const uint32_t fi = 1 + c;
    if (indirect && fi < cull_frusta_n_) {
      for (uint32_t b = 0; b < cull_batches_n_; b++) {
        const CullBatchCpu &B = batches_[b];
        if (pipe_cull_shadow_ != bound_pipe) {
          a.vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, pipe_cull_shadow_);
          bound_pipe = pipe_cull_shadow_;
        }
        if (B.mesh != bound) {
          VkDeviceSize off = 0;
          a.vkCmdBindVertexBuffers(cb, 0, 1, &meshes_[B.mesh].vbuf, &off);
          a.vkCmdBindIndexBuffer(cb, meshes_[B.mesh].ibuf, 0, VK_INDEX_TYPE_UINT32);
          bound = B.mesh;
        }
        Push p{};
        p.skin[1] = c;
        p.skin[2] = fi * cfg_.max_draws + B.first_draw;
        a.vkCmdPushConstants(cb, layout_, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof p, &p);
        pfn_draw_indirect_(cb, cull_cmd_buf_[frame_],
                           (VkDeviceSize)(fi * max_batches_ + b) * sizeof(GpuIndirectCmd), 1,
                           (uint32_t)sizeof(GpuIndirectCmd));
      }
    }
    for (uint32_t i = 0; i < draw_count_; i++) {
      const Draw &d = draws_[i];
      if (indirect && fi < cull_frusta_n_ && d.batch != kNoBatch) continue;
      const VkPipeline want = d.skin_offset == kNoSkin ? pipe_shadow_ : pipe_skin_shadow_;
      if (want != bound_pipe) { a.vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, want); bound_pipe = want; }
      if (d.mesh != bound) {
        VkDeviceSize off = 0;
        a.vkCmdBindVertexBuffers(cb, 0, 1, &meshes_[d.mesh].vbuf, &off);
        a.vkCmdBindIndexBuffer(cb, meshes_[d.mesh].ibuf, 0, VK_INDEX_TYPE_UINT32);
        bound = d.mesh;
      }
      Push p{d.model, {d.color.x, d.color.y, d.color.z, 1.0f}, {d.skin_offset, c, 0, 0}};
      a.vkCmdPushConstants(cb, layout_, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof p, &p);
      a.vkCmdDrawIndexed(cb, meshes_[d.mesh].index_count, 1, 0, 0, 0);
    }
  }
  a.vkCmdEndRenderPass(cb);
}

void Renderer::draw(MeshHandle mesh, const Mat4 &model, Vec3 color) { draw(mesh, default_material_, model, color); }

void Renderer::draw(MeshHandle mesh, MaterialHandle material, const Mat4 &model, Vec3 color, const Mat4 *prev_model,
                   float reactive) {
  if (!mesh.valid() || mesh.id >= mesh_count_) return;
  if (!material.valid() || material.id >= material_count_) material = default_material_;
  if (draw_count_ >= cfg_.max_draws) { stats_.dropped++; return; }
  const Vec3 mc = materials_[material.id].color; // zaten dogrusal
  const Vec3 lc = srgb_to_linear(color);
  if (meshes_[mesh.id].skinned) { stats_.dropped++; return; } // iskeletli mesh draw_skinned ister
  draws_[draw_count_++] = Draw{mesh.id,  material.id, model, Vec3{lc.x * mc.x, lc.y * mc.y, lc.z * mc.z}, kNoSkin,
                               prev_model ? *prev_model : model, reactive, kNoBatch};
}

void Renderer::draw_skinned(MeshHandle mesh, MaterialHandle material, const Mat4 &model, Vec3 color, const Mat4 *joints,
                            uint32_t n, const Mat4 *prev_model, float reactive) {
  if (!mesh.valid() || mesh.id >= mesh_count_ || !meshes_[mesh.id].skinned || n == 0) { stats_.dropped++; return; }
  if (!material.valid() || material.id >= material_count_) material = default_material_;
  if (draw_count_ >= cfg_.max_draws || skin_count_ + n > cfg_.max_skin_matrices) { stats_.dropped++; return; }
  std::memcpy(static_cast<Mat4 *>(skin_mem_[frame_].mapped) + skin_count_, joints, sizeof(Mat4) * n);
  const Vec3 mc = materials_[material.id].color;
  const Vec3 lc = srgb_to_linear(color);
  draws_[draw_count_++] = Draw{mesh.id,     material.id,
                               model,       Vec3{lc.x * mc.x, lc.y * mc.y, lc.z * mc.z},
                               skin_count_, prev_model ? *prev_model : model,
                               reactive,    kNoBatch};
  skin_count_ += n;
}

void Renderer::record(VkCommandBuffer cb) {
  if (!post_.enabled) { record_scene(cb); return; }
  // post: sahne ZATEN ic HDR hedefine cizildi (record_shadow icinde). Burada
  // cagiranin gecisinde yalniz tam ekran birlestirme kalir. Subpass 1'e gecmek
  // ZORUNLU: cagiranin gecisi iki subpass'li ve ui_record subpass 1'de cizer —
  // atlanirsa vkCmdEndRenderPass "butun subpass'lar kosmadi" der.
  dev_->api().vkCmdNextSubpass(cb, VK_SUBPASS_CONTENTS_INLINE);
  record_compose(cb);
  // Birlestirme KENDI (uyumsuz) boru hatti duzenini bagladi: push sabiti durumu
  // gecersizlesti. Ayni karede ui_record ayni layout_ uzerinden cizer ama 96
  // baytlik araligin yalniz ilk 20'sini yazar — kalan 76 bayt "hic kurulmamis"
  // olur (katmanin BestPractices-PushConstants uyarisi; shader okumuyor ama
  // durum tanimsiz). Araligi burada bir kez doldur: post KAPALIYKEN bu yol hic
  // calismaz, yani bugunku komut akisi degismez.
  Push pad{};
  dev_->api().vkCmdPushConstants(cb, layout_, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof pad, &pad);
}

void Renderer::record_scene(VkCommandBuffer cb) {
  if (cull_.enabled && cull_ready_) { record_scene_indirect(cb); return; }
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

// ===========================================================================
// SON ISLEM — DERLENMIS RENDER GRAPH (graph.hpp) + BLOOM
// ---------------------------------------------------------------------------
// Tablo kurulumda uretilir; kayit sirasi, hangi hedefe yazildigi, hangi
// girdinin hangi gecisten geldigi ve gecis sonu LAYOUT'u oradan TURETILIR.
// Kayitta cozucu/arama/ayirma yok: record_post_chain diziyi yurur.
// Zincir tamamen GRAFIK boru hattiyla (compute YOK, plan §8/10): eski Mali
// surucularinde compute + grafik karisimi tile'i bosaltir, ayrica bilinear
// indirgeme dokusal birimde bedavadir.
// ===========================================================================
namespace {
bool make_view2d(rhi::VkApi &a, VkDevice d, VkImage img, VkFormat fmt, VkImageAspectFlags aspect, uint32_t base,
                 VkImageView *out) {
  VkImageViewCreateInfo vi{};
  vi.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
  vi.image = img;
  vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
  vi.format = fmt;
  vi.subresourceRange = {aspect, base, 1, 0, 1};
  return a.vkCreateImageView(d, &vi, nullptr, out) == VK_SUCCESS;
}
} // namespace

bool Renderer::make_post_image(VkFormat fmt, VkImageUsageFlags usage, uint32_t w, uint32_t h, uint32_t mips, bool lazily,
                               VkImage *img, rhi::MemoryAlloc *mem) {
  rhi::VkApi &a = dev_->api();
  VkImageCreateInfo ii{};
  ii.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
  ii.imageType = VK_IMAGE_TYPE_2D;
  ii.format = fmt;
  ii.extent = {w, h, 1};
  ii.mipLevels = mips;
  ii.arrayLayers = 1;
  ii.samples = VK_SAMPLE_COUNT_1_BIT;
  ii.tiling = VK_IMAGE_TILING_OPTIMAL;
  ii.usage = usage;
  if (a.vkCreateImage(dev_->handle(), &ii, nullptr, img) != VK_SUCCESS) return false;
  VkMemoryRequirements req;
  a.vkGetImageMemoryRequirements(dev_->handle(), *img, &req);
  // Zincir basina TEK goruntu (mip'li) -> zincir basina TEK ayirma; mip basina
  // ayri goruntu olsaydi vkAllocateMemory sayisi mip sayisi kadar artardi.
  if (!dev_->allocate_dedicated(req, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, lazily, mem)) return false;
  return a.vkBindImageMemory(dev_->handle(), *img, mem->memory, mem->offset) == VK_SUCCESS;
}

bool Renderer::make_post_pipe(VkShaderModule fs, VkRenderPass rp, uint32_t subpass, VkPipeline *out) {
  rhi::VkApi &a = dev_->api();
  VkPipelineShaderStageCreateInfo st[2]{};
  st[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  st[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
  st[0].module = post_vs_;
  st[0].pName = "main";
  st[1] = st[0];
  st[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
  st[1].module = fs;
  VkPipelineVertexInputStateCreateInfo vi{}; // vertex tamponu YOK (tam ekran ucgeni)
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
  rs.cullMode = VK_CULL_MODE_NONE; // tek ucgen: sarim yonune bagimli olmasin
  rs.lineWidth = 1.0f;
  VkPipelineMultisampleStateCreateInfo ms{};
  ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
  ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
  VkPipelineDepthStencilStateCreateInfo ds{}; // derinlik yok
  ds.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
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
  VkGraphicsPipelineCreateInfo gp{};
  gp.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
  gp.stageCount = 2;
  gp.pStages = st;
  gp.pVertexInputState = &vi;
  gp.pInputAssemblyState = &ia;
  gp.pViewportState = &vp;
  gp.pRasterizationState = &rs;
  gp.pMultisampleState = &ms;
  gp.pDepthStencilState = &ds;
  gp.pColorBlendState = &cb;
  gp.pDynamicState = &dsci;
  gp.layout = post_layout_;
  gp.renderPass = rp;
  gp.subpass = subpass;
  return a.vkCreateGraphicsPipelines(dev_->handle(), VK_NULL_HANDLE, 1, &gp, nullptr, out) == VK_SUCCESS;
}

VkImageView Renderer::graph_view(uint8_t res, uint8_t level) const {
  const uint32_t l = level < kMaxBloomMips ? level : 0;
  switch (res) {
  case kResHdr: return hdr_view_;
  case kResDown: return bloom_view_[0][l];
  case kResUp: return bloom_view_[1][l];
  default: return VK_NULL_HANDLE;
  }
}

void Renderer::graph_size(uint8_t res, uint8_t level, uint32_t *w, uint32_t *h) const {
  const uint32_t l = level < kMaxBloomMips ? level : 0;
  if (res == kResDown || res == kResUp) {
    *w = bloom_w_[l] ? bloom_w_[l] : 1;
    *h = bloom_h_[l] ? bloom_h_[l] : 1;
    return;
  }
  *w = post_w_ ? post_w_ : 1;
  *h = post_h_ ? post_h_ : 1;
}

bool Renderer::make_post(VkRenderPass target_rp) {
  rhi::VkApi &a = dev_->api();
  const VkDevice d = dev_->handle();
  if (!cfg_.post_width || !cfg_.post_height) {
    post_.disabled_reason = "post_width/post_height verilmedi (ic HDR hedefinin olcusu)";
    return false;
  }
  post_w_ = cfg_.post_width;
  post_h_ = cfg_.post_height;
  // --- Bicim: mobilde UCUZ olan once ---------------------------------------
  // B10G11R11 = 32 bit/px, alfa yok: RGBA16F'in YARISI bant genisligi. Hem renk
  // eki hem ORNEKLEME hem DOGRUSAL suzme vermeli (bloom zinciri bilinear okur);
  // vermezse RGBA16F, o da yoksa post KAPANIR ve sebep disari verilir.
  const VkFormat want[2] = {VK_FORMAT_B10G11R11_UFLOAT_PACK32, VK_FORMAT_R16G16B16A16_SFLOAT};
  hdr_fmt_ = VK_FORMAT_UNDEFINED;
  for (VkFormat f : want) {
    VkFormatProperties fp{};
    a.vkGetPhysicalDeviceFormatProperties(dev_->physical(), f, &fp);
    const VkFormatFeatureFlags need = VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT |
                                      VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT;
    if ((fp.optimalTilingFeatures & need) == need) { hdr_fmt_ = f; break; }
  }
  if (hdr_fmt_ == VK_FORMAT_UNDEFINED) {
    post_.disabled_reason = "HDR renk bicimi yok (B10G11R11 / RGBA16F, renk eki + dogrusal ornekleme)";
    return false;
  }
  VkFormat dfmt = VK_FORMAT_UNDEFINED;
  const VkFormat dwant[2] = {VK_FORMAT_D32_SFLOAT, VK_FORMAT_D16_UNORM};
  for (VkFormat f : dwant) {
    VkFormatProperties fp{};
    a.vkGetPhysicalDeviceFormatProperties(dev_->physical(), f, &fp);
    if (fp.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) { dfmt = f; break; }
  }
  if (dfmt == VK_FORMAT_UNDEFINED) {
    post_.disabled_reason = "derinlik bicimi yok (D32/D16)";
    return false;
  }
  { // Mali tile butcesi (rhi/tile_budget.hpp): gecis YARATILIRKEN denetlenir.
    const VkFormat fmts[2] = {hdr_fmt_, dfmt};
    if (!rhi::tile_budget(fmts, 2).ok) { post_.disabled_reason = "tile butcesi asildi (HDR gecisi)"; return false; }
    if (!rhi::tile_budget(&hdr_fmt_, 1).ok) { post_.disabled_reason = "tile butcesi asildi (bloom gecisi)"; return false; }
  }
  // --- Zincir olculeri: yari cozunurlukten baslar ---------------------------
  bloom_mips_ = cfg_.bloom_mips < 2 ? 2 : (cfg_.bloom_mips > kMaxBloomMips ? kMaxBloomMips : cfg_.bloom_mips);
  uint32_t bw = post_w_ / 2, bh = post_h_ / 2;
  if (!bw) bw = 1;
  if (!bh) bh = 1;
  // Son mip 8 pikselin altina inerse cadir suzgeci anlamsizlasir: zinciri kis.
  while (bloom_mips_ > 2 && ((bw >> (bloom_mips_ - 1)) < 8 || (bh >> (bloom_mips_ - 1)) < 8)) bloom_mips_--;
  for (uint32_t i = 0; i < bloom_mips_; i++) {
    bloom_w_[i] = (bw >> i) ? (bw >> i) : 1;
    bloom_h_[i] = (bh >> i) ? (bh >> i) : 1;
  }
  // --- Tablo: gecisler VERI; bagimlilik + layout TURETILIR ------------------
  GraphDesc gd;
  gd.post = true;
  gd.shadow = shadow_info_.enabled;
  gd.motion = temporal_.motion;
  gd.cull = cull_.enabled;
  gd.bloom_mips = bloom_mips_;
  graph_n_ = graph_build(gd, graph_, kMaxGraphPasses);
  if (!graph_n_) { post_.disabled_reason = "graph tablosu kurulamadi (kapasite)"; return false; }
  if (const char *err = graph_validate(graph_, graph_n_)) {
    graph_n_ = 0;
    post_.disabled_reason = err;
    return false;
  }
  // Ara hedeflerin gecis-sonu layout'u TABLODAN gelir; tablo "bu cikti sonra
  // orneklenmiyor" diyorsa render pass'i kurmak yanlis olur (olu gecis).
  for (uint32_t i = 0; i < graph_n_; i++)
    if (graph_[i].out != kResTarget && !graph_[i].out_external && graph_[i].out_layout != GraphLayout::ShaderRead) {
      graph_n_ = 0;
      post_.disabled_reason = "graph: ara hedefin okuyucusu yok";
      return false;
    }
  const VkImageLayout inter_layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL; // turetildi (yukaridaki dongu)

  // --- Goruntuler ----------------------------------------------------------
  if (!make_post_image(hdr_fmt_, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, post_w_, post_h_, 1,
                       false, &hdr_img_, &hdr_mem_)) {
    post_.disabled_reason = "HDR hedefi yaratilamadi";
    return false;
  }
  // Derinlik TRANSIENT: HDR gecisinden sonra kimse okumaz (tablo da oyle der),
  // TBDR'da tile'da kalir ve DRAM'e hic inmez.
  if (!make_post_image(dfmt, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT,
                       post_w_, post_h_, 1, true, &hdr_depth_img_, &hdr_depth_mem_)) {
    post_.disabled_reason = "HDR derinligi yaratilamadi";
    return false;
  }
  if (!make_view2d(a, d, hdr_img_, hdr_fmt_, VK_IMAGE_ASPECT_COLOR_BIT, 0, &hdr_view_) ||
      !make_view2d(a, d, hdr_depth_img_, dfmt, VK_IMAGE_ASPECT_DEPTH_BIT, 0, &hdr_depth_view_)) {
    post_.disabled_reason = "HDR gorunumu yaratilamadi";
    return false;
  }
  // Bloom: iki ayri zincir (indirgeme / yukari). Ayni goruntude yerinde
  // toplamak bellegi yariya indirirdi ama ayni goruntunun bir mip'i EK, baska
  // mip'i ORNEKLENEN olurdu — surucude ve dogrulama katmaninda gri alan.
  // Ayri zincir: her gecisin girdisi ve ciktisi farkli goruntu, barrier acik.
  // Bellek: yari cozunurluk + mip zinciri = tam cozunurlugun ~2 x 1/3'u.
  const VkImageUsageFlags bu = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
  for (uint32_t c = 0; c < 2; c++) {
    if (!make_post_image(hdr_fmt_, bu, bw, bh, bloom_mips_, false, &bloom_img_[c], &bloom_mem_[c])) {
      post_.disabled_reason = "bloom zinciri yaratilamadi";
      return false;
    }
    for (uint32_t i = 0; i < bloom_mips_; i++)
      if (!make_view2d(a, d, bloom_img_[c], hdr_fmt_, VK_IMAGE_ASPECT_COLOR_BIT, i, &bloom_view_[c][i])) {
        post_.disabled_reason = "bloom gorunumu yaratilamadi";
        return false;
      }
  }

  // --- HDR gecisi: cagiranin gecisiyle AYNI iskelet (depth prepass + renk) ---
  // Sahne boru hatlari degismesin diye subpass duzeni birebir aynidir; tek fark
  // renk biciminin HDR olmasi ve gecis sonunda ORNEKLENEBILIR layout.
  {
    VkAttachmentDescription att[2]{};
    att[0].format = hdr_fmt_;
    att[0].samples = VK_SAMPLE_COUNT_1_BIT;
    att[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    att[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    att[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    att[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    att[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    att[0].finalLayout = inter_layout;
    att[1] = att[0];
    att[1].format = dfmt;
    att[1].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE; // transient: tile'da kalir
    att[1].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    VkAttachmentReference cr{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkAttachmentReference dr{1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
    VkSubpassDescription sp[2]{};
    sp[0].pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    sp[0].pDepthStencilAttachment = &dr; // depth prepass: renk yok
    sp[1].pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    sp[1].colorAttachmentCount = 1;
    sp[1].pColorAttachments = &cr;
    sp[1].pDepthStencilAttachment = &dr;
    VkSubpassDependency dep[4]{};
    dep[0].srcSubpass = VK_SUBPASS_EXTERNAL;
    dep[0].dstSubpass = 0;
    dep[0].srcStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    dep[0].dstStageMask = dep[0].srcStageMask;
    dep[0].dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    dep[1].srcSubpass = 0;
    dep[1].dstSubpass = 1;
    dep[1].srcStageMask = dep[0].srcStageMask;
    dep[1].srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    dep[1].dstStageMask = dep[0].srcStageMask;
    dep[1].dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    dep[1].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;
    dep[2].srcSubpass = VK_SUBPASS_EXTERNAL;
    dep[2].dstSubpass = 1;
    dep[2].srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT; // onceki karenin okumasi
    dep[2].srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
    dep[2].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dep[2].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    dep[3].srcSubpass = 1; // TURETILDI: ciktiyi parlak gecis ORNEKLIYOR
    dep[3].dstSubpass = VK_SUBPASS_EXTERNAL;
    dep[3].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dep[3].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    dep[3].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    dep[3].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    VkRenderPassCreateInfo rpi{};
    rpi.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    rpi.attachmentCount = 2;
    rpi.pAttachments = att;
    rpi.subpassCount = 2;
    rpi.pSubpasses = sp;
    rpi.dependencyCount = 4;
    rpi.pDependencies = dep;
    if (a.vkCreateRenderPass(d, &rpi, nullptr, &hdr_rp_) != VK_SUCCESS) {
      post_.disabled_reason = "HDR gecisi yaratilamadi";
      return false;
    }
    VkImageView views[2] = {hdr_view_, hdr_depth_view_};
    VkFramebufferCreateInfo fi{};
    fi.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    fi.renderPass = hdr_rp_;
    fi.attachmentCount = 2;
    fi.pAttachments = views;
    fi.width = post_w_;
    fi.height = post_h_;
    fi.layers = 1;
    if (a.vkCreateFramebuffer(d, &fi, nullptr, &hdr_fb_) != VK_SUCCESS) {
      post_.disabled_reason = "HDR framebuffer yaratilamadi";
      return false;
    }
  }
  // --- Bloom gecisi: tek renk eki, butun zincir hedefleri paylasir ----------
  // loadOp DONT_CARE: tam ekran ucgeni her pikseli yazar. LOAD olsaydi Mali
  // tile'i belekten GERI OKURDU (attachment-needs-readback) — bloom zincirinin
  // en pahali hatasi bu olurdu.
  {
    VkAttachmentDescription att{};
    att.format = hdr_fmt_;
    att.samples = VK_SAMPLE_COUNT_1_BIT;
    att.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    att.storeOp = VK_ATTACHMENT_STORE_OP_STORE; // orneklenecek: SAKLA (transient DEGIL)
    att.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    att.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    att.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    att.finalLayout = inter_layout;
    VkAttachmentReference cr{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkSubpassDescription sp{};
    sp.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    sp.colorAttachmentCount = 1;
    sp.pColorAttachments = &cr;
    VkSubpassDependency dep[2]{};
    dep[0].srcSubpass = VK_SUBPASS_EXTERNAL;
    dep[0].dstSubpass = 0;
    dep[0].srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT; // onceki gecisin okumasi
    dep[0].srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
    dep[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dep[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    dep[1].srcSubpass = 0;
    dep[1].dstSubpass = VK_SUBPASS_EXTERNAL;
    dep[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dep[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    dep[1].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT; // sonraki gecisin okumasi
    dep[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    VkRenderPassCreateInfo rpi{};
    rpi.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    rpi.attachmentCount = 1;
    rpi.pAttachments = &att;
    rpi.subpassCount = 1;
    rpi.pSubpasses = &sp;
    rpi.dependencyCount = 2;
    rpi.pDependencies = dep;
    if (a.vkCreateRenderPass(d, &rpi, nullptr, &bloom_rp_) != VK_SUCCESS) {
      post_.disabled_reason = "bloom gecisi yaratilamadi";
      return false;
    }
    for (uint32_t c = 0; c < 2; c++)
      for (uint32_t i = 0; i < bloom_mips_; i++) {
        VkFramebufferCreateInfo fi{};
        fi.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fi.renderPass = bloom_rp_;
        fi.attachmentCount = 1;
        fi.pAttachments = &bloom_view_[c][i];
        fi.width = bloom_w_[i];
        fi.height = bloom_h_[i];
        fi.layers = 1;
        if (a.vkCreateFramebuffer(d, &fi, nullptr, &bloom_fb_[c][i]) != VK_SUCCESS) {
          post_.disabled_reason = "bloom framebuffer yaratilamadi";
          return false;
        }
      }
  }
  // --- Ornekleyici + descriptor (KURULUMDA yazilir, karede degismez) --------
  {
    VkSamplerCreateInfo si{};
    si.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    si.magFilter = si.minFilter = VK_FILTER_LINEAR;
    si.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    // Ucu de AYNI sarma kipi (Arm: different-wrapping-modes) ve KENARA kenetle:
    // zincir kenarinda karsi kenardan enerji sizmasin.
    si.addressModeU = si.addressModeV = si.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    si.maxLod = VK_LOD_CLAMP_NONE; // Mali kurali: sampler'da LOD KIRPMA YOK (gorunum tek mip)
    if (a.vkCreateSampler(d, &si, nullptr, &post_sampler_) != VK_SUCCESS) {
      post_.disabled_reason = "son islem ornekleyicisi yaratilamadi";
      return false;
    }
    VkDescriptorSetLayoutBinding b[2]{};
    for (uint32_t i = 0; i < 2; i++) {
      b[i].binding = i;
      b[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
      b[i].descriptorCount = 1;
      b[i].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    }
    VkDescriptorSetLayoutCreateInfo sli{};
    sli.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    sli.bindingCount = 2;
    sli.pBindings = b;
    if (a.vkCreateDescriptorSetLayout(d, &sli, nullptr, &post_set_layout_) != VK_SUCCESS) {
      post_.disabled_reason = "son islem descriptor duzeni yaratilamadi";
      return false;
    }
    VkPushConstantRange pcr{VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(PostPush)};
    VkPipelineLayoutCreateInfo pli{};
    pli.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pli.setLayoutCount = 1;
    pli.pSetLayouts = &post_set_layout_;
    pli.pushConstantRangeCount = 1;
    pli.pPushConstantRanges = &pcr;
    if (a.vkCreatePipelineLayout(d, &pli, nullptr, &post_layout_) != VK_SUCCESS) {
      post_.disabled_reason = "son islem boru hatti duzeni yaratilamadi";
      return false;
    }
    VkDescriptorPoolSize ps{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 2 * graph_n_};
    VkDescriptorPoolCreateInfo dpi{};
    dpi.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    dpi.maxSets = graph_n_;
    dpi.poolSizeCount = 1;
    dpi.pPoolSizes = &ps;
    if (a.vkCreateDescriptorPool(d, &dpi, nullptr, &post_pool_) != VK_SUCCESS) {
      post_.disabled_reason = "son islem descriptor havuzu yaratilamadi";
      return false;
    }
    for (uint32_t i = 0; i < graph_n_; i++) {
      const GraphPass &p = graph_[i];
      if (p.kind == PassKind::Shadow || p.kind == PassKind::Scene || p.kind == PassKind::Motion ||
          p.kind == PassKind::Cull)
        continue; // orneklenen goruntu ciktisi yok (cull'un ciktisi TAMPON)
      VkDescriptorSetAllocateInfo dai{};
      dai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
      dai.descriptorPool = post_pool_;
      dai.descriptorSetCount = 1;
      dai.pSetLayouts = &post_set_layout_;
      if (a.vkAllocateDescriptorSets(d, &dai, &post_sets_[i]) != VK_SUCCESS) {
        post_.disabled_reason = "son islem descriptor kumesi ayrilamadi";
        return false;
      }
      // Tek girdili gecislerde binding 1 ayni gorunume baglanir: shader onu
      // kullanmaz ama duzen ortak (tek boru hatti duzeni, tek havuz).
      VkImageView v0 = graph_view(p.in0, p.in0_level);
      VkImageView v1 = p.in1 == kResNone ? v0 : graph_view(p.in1, p.in1_level);
      if (!v0 || !v1) {
        post_.disabled_reason = "graph: gecisin girdisi cozulemedi";
        return false;
      }
      VkDescriptorImageInfo dii[2] = {{post_sampler_, v0, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
                                      {post_sampler_, v1, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL}};
      VkWriteDescriptorSet w[2]{};
      for (uint32_t k = 0; k < 2; k++) {
        w[k].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        w[k].dstSet = post_sets_[i];
        w[k].dstBinding = k;
        w[k].descriptorCount = 1;
        w[k].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        w[k].pImageInfo = &dii[k];
      }
      a.vkUpdateDescriptorSets(d, 2, w, 0, nullptr);
    }
  }
  // --- Shader + boru hatlari -----------------------------------------------
  {
    VkShaderModuleCreateInfo smi{};
    smi.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    struct { const uint32_t *code; uint32_t size; VkShaderModule *out; } mods[5] = {
        {post_vert_spv, post_vert_spv_size, &post_vs_},
        {bloom_bright_frag_spv, bloom_bright_frag_spv_size, &bright_fs_},
        {bloom_down_frag_spv, bloom_down_frag_spv_size, &down_fs_},
        {bloom_up_frag_spv, bloom_up_frag_spv_size, &up_fs_},
        {compose_frag_spv, compose_frag_spv_size, &compose_fs_}};
    for (auto &m : mods) {
      smi.codeSize = m.size;
      smi.pCode = m.code;
      if (a.vkCreateShaderModule(d, &smi, nullptr, m.out) != VK_SUCCESS) {
        post_.disabled_reason = "son islem shader'i yaratilamadi";
        return false;
      }
    }
    if (!make_post_pipe(bright_fs_, bloom_rp_, 0, &pipe_bright_) ||
        !make_post_pipe(down_fs_, bloom_rp_, 0, &pipe_down_) || !make_post_pipe(up_fs_, bloom_rp_, 0, &pipe_up_) ||
        // Birlestirme CAGIRANIN gecisinde, renk subpass'inde (UI ile ayni yer).
        !make_post_pipe(compose_fs_, target_rp, 1, &pipe_compose_)) {
      post_.disabled_reason = "son islem boru hatti yaratilamadi";
      return false;
    }
  }
  // --- Disari verilen durum -------------------------------------------------
  post_.hdr_format = hdr_fmt_;
  post_.width = post_w_;
  post_.height = post_h_;
  post_.bloom_mips = bloom_mips_;
  post_.bloom_width = bloom_w_[0];
  post_.bloom_height = bloom_h_[0];
  post_.pass_count = graph_n_;
  for (uint32_t i = 0; i < graph_n_; i++) post_.pass_name[i] = graph_[i].name;
  const uint64_t cbytes = rhi::format_bits(hdr_fmt_) / 8;
  uint64_t bytes = (uint64_t)post_w_ * post_h_ * cbytes + (uint64_t)post_w_ * post_h_ * (rhi::format_bits(dfmt) / 8);
  for (uint32_t c = 0; c < 2; c++)
    for (uint32_t i = 0; i < bloom_mips_; i++) bytes += (uint64_t)bloom_w_[i] * bloom_h_[i] * cbytes;
  post_.target_bytes = bytes;
  post_.disabled_reason = "";
  return true;
}

// Sahnenin ic HDR hedefine cizilmesi. Cagiranin gecisiyle ayni iskelet oldugu
// icin record_scene AYNEN kullanilir (post kapaliyken bit bit ayni kod yolu).
void Renderer::record_hdr_scene(VkCommandBuffer cb) {
  rhi::VkApi &a = dev_->api();
  VkClearValue clears[2]{};
  clears[0].color.float32[0] = cfg_.post_clear.x;
  clears[0].color.float32[1] = cfg_.post_clear.y;
  clears[0].color.float32[2] = cfg_.post_clear.z;
  clears[0].color.float32[3] = 1.0f;
  clears[1].depthStencil = {1.0f, 0};
  VkRenderPassBeginInfo rbi{};
  rbi.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
  rbi.renderPass = hdr_rp_;
  rbi.framebuffer = hdr_fb_;
  // Dinamik cozunurluk: hedef TAM olcude ama yalniz sol-ust alt-dikdortgeni
  // cizilir/temizlenir (gerisi tanimsiz kalir, birlestirme oraya bakmaz).
  rbi.renderArea = {{0, 0}, {scaled_w_, scaled_h_}};
  rbi.clearValueCount = 2;
  rbi.pClearValues = clears;
  a.vkCmdBeginRenderPass(cb, &rbi, VK_SUBPASS_CONTENTS_INLINE);
  VkViewport vp{0, 0, (float)scaled_w_, (float)scaled_h_, 0.0f, 1.0f};
  VkRect2D sc{{0, 0}, {scaled_w_, scaled_h_}};
  a.vkCmdSetViewport(cb, 0, 1, &vp);
  a.vkCmdSetScissor(cb, 0, 1, &sc);
  record_scene(cb);
  a.vkCmdEndRenderPass(cb);
}

// Bloom zinciri: tabloyu YURU. Sira, hedef, girdi ve mip seviyesi tablodan
// gelir; burada karar yok.
void Renderer::record_post_chain(VkCommandBuffer cb) {
  rhi::VkApi &a = dev_->api();
  for (uint32_t i = 0; i < graph_n_; i++) {
    const GraphPass &p = graph_[i];
    VkPipeline pipe = VK_NULL_HANDLE;
    if (p.kind == PassKind::Bright) pipe = pipe_bright_;
    else if (p.kind == PassKind::Down) pipe = pipe_down_;
    else if (p.kind == PassKind::Up) pipe = pipe_up_;
    else continue;
    uint32_t w = 1, h = 1;
    graph_size(p.out, p.out_level, &w, &h);
    VkRenderPassBeginInfo rbi{};
    rbi.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rbi.renderPass = bloom_rp_;
    rbi.framebuffer = bloom_fb_[p.out == kResDown ? 0 : 1][p.out_level];
    rbi.renderArea = {{0, 0}, {w, h}};
    a.vkCmdBeginRenderPass(cb, &rbi, VK_SUBPASS_CONTENTS_INLINE);
    VkViewport vp{0, 0, (float)w, (float)h, 0.0f, 1.0f};
    VkRect2D sc{{0, 0}, {w, h}};
    a.vkCmdSetViewport(cb, 0, 1, &vp);
    a.vkCmdSetScissor(cb, 0, 1, &sc);
    a.vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, pipe);
    a.vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, post_layout_, 0, 1, &post_sets_[i], 0, nullptr);
    PostPush push{};
    uint32_t sw = 1, sh = 1;
    graph_size(p.in0, p.in0_level, &sw, &sh);
    push.texel[0] = 1.0f / (float)sw;
    push.texel[1] = 1.0f / (float)sh;
    if (p.in1 != kResNone) graph_size(p.in1, p.in1_level, &sw, &sh);
    push.texel[2] = 1.0f / (float)sw;
    push.texel[3] = 1.0f / (float)sh;
    push.p[0] = cfg_.bloom_threshold;
    push.p[1] = cfg_.bloom_soft_knee;
    push.p[2] = cfg_.bloom_radius;
    // Yalniz parlak gecis HDR hedefini okur: dolu alt-dikdortgen orani onda
    // anlamli, zincirin geri kalani kendi (tam dolu) hedeflerinden okur.
    push.q[0] = p.kind == PassKind::Bright ? scale_ : 1.0f;
    a.vkCmdPushConstants(cb, post_layout_, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof push, &push);
    a.vkCmdDraw(cb, 3, 1, 0, 0); // tam ekran ucgeni (vertex tamponu yok)
    a.vkCmdEndRenderPass(cb);
  }
}

void Renderer::record_compose(VkCommandBuffer cb) {
  rhi::VkApi &a = dev_->api();
  const uint32_t i = graph_n_ - 1; // tablonun son gecisi: cagiranin hedefine yazan
  const GraphPass &p = graph_[i];
  a.vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, pipe_compose_);
  a.vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, post_layout_, 0, 1, &post_sets_[i], 0, nullptr);
  PostPush push{};
  uint32_t sw = 1, sh = 1;
  graph_size(p.in0, p.in0_level, &sw, &sh);
  push.texel[0] = 1.0f / (float)sw;
  push.texel[1] = 1.0f / (float)sh;
  graph_size(p.in1, p.in1_level, &sw, &sh);
  push.texel[2] = 1.0f / (float)sw;
  push.texel[3] = 1.0f / (float)sh;
  push.p[0] = cfg_.exposure;
  push.p[1] = cfg_.bloom_intensity;
  push.p[2] = cfg_.srgb_target ? 0.0f : 1.0f; // UNORM hedefte shader kodlar
  push.p[3] = cfg_.tonemap ? 1.0f : 0.0f;
  // Yukseltme (upscale) burada: sahnenin dolu alt-dikdortgeni tam hedefe acilir.
  push.q[0] = scale_;
  push.q[1] = cfg_.temporal.sharpness;
  push.q[2] = (float)(uint32_t)cfg_.temporal.upscaler;
  a.vkCmdPushConstants(cb, post_layout_, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof push, &push);
  a.vkCmdDraw(cb, 3, 1, 0, 0);
}


// ===========================================================================
// FAZ 5 — EKRAN UZAYI HAREKET VEKTORU + REACTIVE MASKE
// ---------------------------------------------------------------------------
// Neden AYRI bir gecis: MV'yi sahne renk subpass'ine ikinci bir renk eki
// olarak asmak mesh.frag'in iki varyantini ve iki boru hatti setini gerektirir
// — yani MV KAPALIYKEN bile bugunku boru hatlari degisirdi. Ayri gecis ikinci
// bir geometri gecisi demek (olculur, cihaz diliminde tartilir) ama KAPALIYKEN
// hicbir seye dokunmaz. Derinlik TRANSIENT: MV gecisinin derinligini kimse
// okumaz, TBDR'da tile'da kalir.
//
// Temizleme rengi (0,0,0,0) SOZLESMENIN PARCASI (Tuzaklar 8ai): nesnesiz
// piksel "hareketsiz + guvenilir (reactive 0)" demektir.
// ===========================================================================
bool Renderer::make_motion() {
  rhi::VkApi &a = dev_->api();
  const VkDevice d = dev_->handle();
  motion_w_ = cfg_.temporal.width ? cfg_.temporal.width : cfg_.post_width;
  motion_h_ = cfg_.temporal.height ? cfg_.temporal.height : cfg_.post_height;
  if (!motion_w_ || !motion_h_) {
    temporal_.motion_disabled_reason = "hareket hedefi olcusu yok (temporal.width/height ya da post_width/post_height)";
    return false;
  }
  // RGBA16F: xy piksel kaymasi (isaretli, ondalikli), z ayrilmis, w reactive.
  // RG16F yetmez — reactive maskeye kanal lazim; RGBA32F yalniz yedek.
  const VkFormat want[2] = {VK_FORMAT_R16G16B16A16_SFLOAT, VK_FORMAT_R32G32B32A32_SFLOAT};
  motion_fmt_ = VK_FORMAT_UNDEFINED;
  for (VkFormat f : want) {
    VkFormatProperties fp{};
    a.vkGetPhysicalDeviceFormatProperties(dev_->physical(), f, &fp);
    const VkFormatFeatureFlags need = VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT;
    if ((fp.optimalTilingFeatures & need) == need) { motion_fmt_ = f; break; }
  }
  if (motion_fmt_ == VK_FORMAT_UNDEFINED) {
    temporal_.motion_disabled_reason = "hareket bicimi yok (RGBA16F/RGBA32F, renk eki + ornekleme)";
    return false;
  }
  VkFormat dfmt = VK_FORMAT_UNDEFINED;
  const VkFormat dwant[2] = {VK_FORMAT_D16_UNORM, VK_FORMAT_D32_SFLOAT};
  for (VkFormat f : dwant) {
    VkFormatProperties fp{};
    a.vkGetPhysicalDeviceFormatProperties(dev_->physical(), f, &fp);
    if (fp.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) { dfmt = f; break; }
  }
  if (dfmt == VK_FORMAT_UNDEFINED) {
    temporal_.motion_disabled_reason = "derinlik bicimi yok (D16/D32)";
    return false;
  }
  { // Mali tile butcesi: gecis YARATILIRKEN denetlenir (rhi/tile_budget.hpp).
    const VkFormat fmts[2] = {motion_fmt_, dfmt};
    if (!rhi::tile_budget(fmts, 2).ok) {
      temporal_.motion_disabled_reason = "tile butcesi asildi (hareket gecisi)";
      return false;
    }
  }
  if (!make_post_image(motion_fmt_,
                       VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
                           VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                       motion_w_, motion_h_, 1, false, &motion_img_, &motion_mem_alloc_)) {
    temporal_.motion_disabled_reason = "hareket hedefi yaratilamadi";
    return false;
  }
  if (!make_post_image(dfmt, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT,
                       motion_w_, motion_h_, 1, true, &motion_depth_img_, &motion_depth_mem_)) {
    temporal_.motion_disabled_reason = "hareket derinligi yaratilamadi";
    return false;
  }
  {
    VkImageViewCreateInfo vi{};
    vi.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
    vi.image = motion_img_;
    vi.format = motion_fmt_;
    vi.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    if (a.vkCreateImageView(d, &vi, nullptr, &motion_view_) != VK_SUCCESS) {
      temporal_.motion_disabled_reason = "hareket gorunumu yaratilamadi";
      return false;
    }
    vi.image = motion_depth_img_;
    vi.format = dfmt;
    vi.subresourceRange = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1};
    if (a.vkCreateImageView(d, &vi, nullptr, &motion_depth_view_) != VK_SUCCESS) {
      temporal_.motion_disabled_reason = "hareket derinlik gorunumu yaratilamadi";
      return false;
    }
  }
  { // Gecis: tek subpass, renk + (transient) derinlik.
    VkAttachmentDescription att[2]{};
    att[0].format = motion_fmt_;
    att[0].samples = VK_SAMPLE_COUNT_1_BIT;
    att[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    att[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    att[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    att[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    att[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    att[0].finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    att[1] = att[0];
    att[1].format = dfmt;
    att[1].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE; // transient
    att[1].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    VkAttachmentReference cr{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkAttachmentReference dr{1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
    VkSubpassDescription sp{};
    sp.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    sp.colorAttachmentCount = 1;
    sp.pColorAttachments = &cr;
    sp.pDepthStencilAttachment = &dr;
    VkSubpassDependency dep[2]{};
    dep[0].srcSubpass = VK_SUBPASS_EXTERNAL;
    dep[0].dstSubpass = 0;
    dep[0].srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT; // onceki karenin okumasi
    dep[0].srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
    dep[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dep[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    dep[1].srcSubpass = 0;
    dep[1].dstSubpass = VK_SUBPASS_EXTERNAL;
    dep[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dep[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    dep[1].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT; // upscaler/olcum okumasi
    dep[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    VkRenderPassCreateInfo rpi{};
    rpi.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    rpi.attachmentCount = 2;
    rpi.pAttachments = att;
    rpi.subpassCount = 1;
    rpi.pSubpasses = &sp;
    rpi.dependencyCount = 2;
    rpi.pDependencies = dep;
    if (a.vkCreateRenderPass(d, &rpi, nullptr, &motion_rp_) != VK_SUCCESS) {
      temporal_.motion_disabled_reason = "hareket gecisi yaratilamadi";
      return false;
    }
    VkImageView views[2] = {motion_view_, motion_depth_view_};
    VkFramebufferCreateInfo fi{};
    fi.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    fi.renderPass = motion_rp_;
    fi.attachmentCount = 2;
    fi.pAttachments = views;
    fi.width = motion_w_;
    fi.height = motion_h_;
    fi.layers = 1;
    if (a.vkCreateFramebuffer(d, &fi, nullptr, &motion_fb_) != VK_SUCCESS) {
      temporal_.motion_disabled_reason = "hareket framebuffer yaratilamadi";
      return false;
    }
  }
  { // Descriptor duzeni + kare tamponlari (UBO + cizim ornekleri SSBO).
    VkDescriptorSetLayoutBinding b[2]{};
    b[0].binding = 0;
    b[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    b[0].descriptorCount = 1;
    b[0].stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    b[1].binding = 1;
    b[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    b[1].descriptorCount = 1;
    b[1].stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    VkDescriptorSetLayoutCreateInfo sli{};
    sli.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    sli.bindingCount = 2;
    sli.pBindings = b;
    if (a.vkCreateDescriptorSetLayout(d, &sli, nullptr, &motion_set_layout_) != VK_SUCCESS) {
      temporal_.motion_disabled_reason = "hareket descriptor duzeni yaratilamadi";
      return false;
    }
    VkPushConstantRange pcr{VK_SHADER_STAGE_VERTEX_BIT, 0, 16};
    VkPipelineLayoutCreateInfo pli{};
    pli.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pli.setLayoutCount = 1;
    pli.pSetLayouts = &motion_set_layout_;
    pli.pushConstantRangeCount = 1;
    pli.pPushConstantRanges = &pcr;
    if (a.vkCreatePipelineLayout(d, &pli, nullptr, &motion_layout_) != VK_SUCCESS) {
      temporal_.motion_disabled_reason = "hareket boru hatti duzeni yaratilamadi";
      return false;
    }
    VkDescriptorPoolSize ps[2] = {{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, kMaxFrames},
                                  {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, kMaxFrames}};
    VkDescriptorPoolCreateInfo dpi{};
    dpi.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    dpi.maxSets = kMaxFrames;
    dpi.poolSizeCount = 2;
    dpi.pPoolSizes = ps;
    if (a.vkCreateDescriptorPool(d, &dpi, nullptr, &motion_pool_) != VK_SUCCESS) {
      temporal_.motion_disabled_reason = "hareket descriptor havuzu yaratilamadi";
      return false;
    }
    const VkMemoryPropertyFlags host = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    const VkDeviceSize inst_bytes = (VkDeviceSize)sizeof(MotionInst) * cfg_.max_draws;
    for (uint32_t i = 0; i < cfg_.frames_in_flight; i++) {
      if (!make_buffer(VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, sizeof(MotionUbo), host, &motion_ubo_[i],
                       &motion_ubo_mem_[i]) ||
          !make_buffer(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, inst_bytes, host, &motion_inst_[i], &motion_inst_mem_[i])) {
        temporal_.motion_disabled_reason = "hareket kare tamponlari yaratilamadi";
        return false;
      }
      VkDescriptorSetAllocateInfo dai{};
      dai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
      dai.descriptorPool = motion_pool_;
      dai.descriptorSetCount = 1;
      dai.pSetLayouts = &motion_set_layout_;
      if (a.vkAllocateDescriptorSets(d, &dai, &motion_sets_[i]) != VK_SUCCESS) {
        temporal_.motion_disabled_reason = "hareket descriptor kumesi ayrilamadi";
        return false;
      }
      VkDescriptorBufferInfo ub{motion_ubo_[i], 0, sizeof(MotionUbo)};
      VkDescriptorBufferInfo sb{motion_inst_[i], 0, inst_bytes};
      VkWriteDescriptorSet w[2]{};
      w[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
      w[0].dstSet = motion_sets_[i];
      w[0].dstBinding = 0;
      w[0].descriptorCount = 1;
      w[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
      w[0].pBufferInfo = &ub;
      w[1] = w[0];
      w[1].dstBinding = 1;
      w[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
      w[1].pBufferInfo = &sb;
      a.vkUpdateDescriptorSets(d, 2, w, 0, nullptr);
    }
  }
  { // Shader + boru hatti (yalniz POZISYON okur: paketlenmis vertex'in ilk alani)
    VkShaderModuleCreateInfo smi{};
    smi.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    smi.codeSize = motion_vert_spv_size;
    smi.pCode = motion_vert_spv;
    if (a.vkCreateShaderModule(d, &smi, nullptr, &motion_vs_) != VK_SUCCESS) {
      temporal_.motion_disabled_reason = "hareket vertex shader'i yaratilamadi";
      return false;
    }
    smi.codeSize = motion_frag_spv_size;
    smi.pCode = motion_frag_spv;
    if (a.vkCreateShaderModule(d, &smi, nullptr, &motion_fs_) != VK_SUCCESS) {
      temporal_.motion_disabled_reason = "hareket fragment shader'i yaratilamadi";
      return false;
    }
    VkPipelineShaderStageCreateInfo st[2]{};
    st[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    st[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    st[0].module = motion_vs_;
    st[0].pName = "main";
    st[1] = st[0];
    st[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    st[1].module = motion_fs_;
    VkVertexInputBindingDescription vb{0, (uint32_t)sizeof(GpuVertex), VK_VERTEX_INPUT_RATE_VERTEX};
    VkVertexInputAttributeDescription va{0, 0, VK_FORMAT_R32G32B32_SFLOAT, (uint32_t)offsetof(GpuVertex, pos)};
    VkPipelineVertexInputStateCreateInfo vi{};
    vi.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vi.vertexBindingDescriptionCount = 1;
    vi.pVertexBindingDescriptions = &vb;
    vi.vertexAttributeDescriptionCount = 1;
    vi.pVertexAttributeDescriptions = &va;
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
    rs.cullMode = VK_CULL_MODE_BACK_BIT;
    rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE; // sahne gecisiyle AYNI sarim
    rs.lineWidth = 1.0f;
    VkPipelineMultisampleStateCreateInfo ms{};
    ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    VkPipelineDepthStencilStateCreateInfo ds{};
    ds.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    ds.depthTestEnable = VK_TRUE;
    ds.depthWriteEnable = VK_TRUE;
    ds.depthCompareOp = VK_COMPARE_OP_LESS;
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
    VkGraphicsPipelineCreateInfo gp{};
    gp.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    gp.stageCount = 2;
    gp.pStages = st;
    gp.pVertexInputState = &vi;
    gp.pInputAssemblyState = &ia;
    gp.pViewportState = &vp;
    gp.pRasterizationState = &rs;
    gp.pMultisampleState = &ms;
    gp.pDepthStencilState = &ds;
    gp.pColorBlendState = &cb;
    gp.pDynamicState = &dsci;
    gp.layout = motion_layout_;
    gp.renderPass = motion_rp_;
    gp.subpass = 0;
    if (a.vkCreateGraphicsPipelines(d, VK_NULL_HANDLE, 1, &gp, nullptr, &pipe_motion_) != VK_SUCCESS) {
      temporal_.motion_disabled_reason = "hareket boru hatti yaratilamadi";
      return false;
    }
  }
  temporal_.motion_format = motion_fmt_;
  temporal_.motion_width = motion_w_;
  temporal_.motion_height = motion_h_;
  temporal_.motion_bytes = (uint64_t)motion_w_ * motion_h_ * (rhi::format_bits(motion_fmt_) / 8) +
                           (uint64_t)motion_w_ * motion_h_ * (rhi::format_bits(dfmt) / 8);
  temporal_.motion_disabled_reason = "";
  return true;
}

// Cizim listesini hareket hedefine ikinci kez cizer. Ornek verisi (model +
// onceki model + reactive) kare SSBO'suna KAYIT SIRASINDA yazilir: gonderimden
// once CPU tarafinda, ayirma yok.
//
// BILINEN BOSLUK: iskeletli cizimler atlanir (onceki karenin EKLEM matrisleri
// saklanmiyor; bind pozuyla cizmek yanlis SILUET verirdi). Atlanan sayilir ve
// TemporalInfo::motion_skipped_skinned ile disari verilir — sessiz degil.
void Renderer::record_motion_pass(VkCommandBuffer cb) {
  rhi::VkApi &a = dev_->api();
  VkClearValue clears[2]{};
  clears[0].color.float32[0] = 0.0f; // hareket yok
  clears[0].color.float32[1] = 0.0f;
  clears[0].color.float32[2] = 0.0f;
  clears[0].color.float32[3] = 0.0f; // reactive yok (MV guvenilir)
  clears[1].depthStencil = {1.0f, 0};
  VkRenderPassBeginInfo rbi{};
  rbi.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
  rbi.renderPass = motion_rp_;
  rbi.framebuffer = motion_fb_;
  rbi.renderArea = {{0, 0}, {motion_w_, motion_h_}};
  rbi.clearValueCount = 2;
  rbi.pClearValues = clears;
  a.vkCmdBeginRenderPass(cb, &rbi, VK_SUBPASS_CONTENTS_INLINE);
  VkViewport vp{0, 0, (float)motion_w_, (float)motion_h_, 0.0f, 1.0f};
  VkRect2D sc{{0, 0}, {motion_w_, motion_h_}};
  a.vkCmdSetViewport(cb, 0, 1, &vp);
  a.vkCmdSetScissor(cb, 0, 1, &sc);
  a.vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, pipe_motion_);
  a.vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, motion_layout_, 0, 1, &motion_sets_[frame_], 0,
                            nullptr);
  MotionInst *inst = static_cast<MotionInst *>(motion_inst_mem_[frame_].mapped);
  uint32_t slot = 0, atlanan = 0, bound = 0xFFFFFFFFu;
  for (uint32_t i = 0; i < draw_count_; i++) {
    const Draw &dr = draws_[i];
    if (dr.skin_offset != kNoSkin) { atlanan++; continue; }
    inst[slot].model = dr.model;
    inst[slot].prev_model = dr.prev_model;
    inst[slot].misc[0] = dr.reactive;
    inst[slot].misc[1] = inst[slot].misc[2] = inst[slot].misc[3] = 0.0f;
    if (dr.mesh != bound) {
      VkDeviceSize off = 0;
      a.vkCmdBindVertexBuffers(cb, 0, 1, &meshes_[dr.mesh].vbuf, &off);
      a.vkCmdBindIndexBuffer(cb, meshes_[dr.mesh].ibuf, 0, VK_INDEX_TYPE_UINT32);
      bound = dr.mesh;
    }
    const uint32_t idx[4] = {slot, 0, 0, 0};
    a.vkCmdPushConstants(cb, motion_layout_, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof idx, idx);
    a.vkCmdDrawIndexed(cb, meshes_[dr.mesh].index_count, 1, 0, 0, 0);
    slot++;
  }
  a.vkCmdEndRenderPass(cb);
  temporal_.motion_skipped_skinned = atlanan;
  motion_recorded_ = true;
}

// f16 -> f32 (geri okuma; half_from_float'in tersi).
static float float_from_half(uint16_t h) {
  const uint32_t sign = (uint32_t)(h & 0x8000u) << 16;
  const uint32_t exp = (h >> 10) & 0x1Fu;
  uint32_t man = (uint32_t)(h & 0x3FFu);
  uint32_t bits;
  if (exp == 0) {
    if (!man) bits = sign;
    else { // normalize edilmemis
      int e = -1;
      do { man <<= 1; e++; } while (!(man & 0x400u));
      man &= 0x3FFu;
      bits = sign | ((uint32_t)(127 - 15 - e) << 23) | (man << 13);
    }
  } else if (exp == 31) {
    bits = sign | 0x7F800000u | (man << 13);
  } else {
    bits = sign | ((exp + 127 - 15) << 23) | (man << 13);
  }
  float f;
  std::memcpy(&f, &bits, 4);
  return f;
}

// OLCUM yolu (kare icinde CAGRILMAZ): MV hedefini CPU'ya kopyalar. Geri okuma
// tamponu ILK cagrida yaratilir ve saklanir — her olcumde yeni tampon blok
// ayiricisini (bump, geri vermez) tuketirdi.
bool Renderer::read_motion(float *dst, uint32_t max_pixels) {
  if (!dev_ || !temporal_.motion || !motion_recorded_ || !dst) return false;
  const uint32_t px = motion_w_ * motion_h_;
  if (max_pixels < px) return false;
  const bool f32 = motion_fmt_ == VK_FORMAT_R32G32B32A32_SFLOAT;
  const VkDeviceSize bytes = (VkDeviceSize)px * (f32 ? 16u : 8u);
  if (!motion_read_ &&
      !make_buffer(VK_BUFFER_USAGE_TRANSFER_DST_BIT, bytes,
                   VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &motion_read_,
                   &motion_read_mem_))
    return false;
  rhi::VkApi &a = dev_->api();
  VkCommandBuffer cb = dev_->begin_one_shot();
  if (!cb) return false;
  image_barrier(a, cb, motion_img_, 0, 1, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_TRANSFER_READ_BIT,
                VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
  VkBufferImageCopy region{};
  region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
  region.imageExtent = {motion_w_, motion_h_, 1};
  a.vkCmdCopyImageToBuffer(cb, motion_img_, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, motion_read_, 1, &region);
  image_barrier(a, cb, motion_img_, 0, 1, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_ACCESS_TRANSFER_READ_BIT, VK_ACCESS_SHADER_READ_BIT,
                VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
  if (!dev_->end_one_shot_and_wait(cb)) return false;
  if (f32) {
    std::memcpy(dst, motion_read_mem_.mapped, (size_t)bytes);
  } else {
    const uint16_t *src = static_cast<const uint16_t *>(motion_read_mem_.mapped);
    for (uint32_t i = 0; i < px * 4; i++) dst[i] = float_from_half(src[i]);
  }
  return true;
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

uint32_t Renderer::sphere(Vertex *v, uint32_t *idx, uint32_t seg_h, uint32_t seg_v) {
  uint32_t vi = 0, ii = 0;
  for (uint32_t y = 0; y <= seg_v; ++y) {
    float v_val = (float)y / seg_v;
    float phi = v_val * 3.14159265359f;
    float sin_phi = std::sin(phi);
    float cos_phi = std::cos(phi);
    for (uint32_t x = 0; x <= seg_h; ++x) {
      float u_val = (float)x / seg_h;
      float theta = u_val * 2.0f * 3.14159265359f;
      float x_pos = std::cos(theta) * sin_phi;
      float z_pos = std::sin(theta) * sin_phi;
      Vec3 N{x_pos, cos_phi, z_pos};
      v[vi++] = {N * 0.5f, N, {u_val, v_val}};
    }
  }
  for (uint32_t y = 0; y < seg_v; ++y) {
    for (uint32_t x = 0; x < seg_h; ++x) {
      uint32_t first = (y * (seg_h + 1)) + x;
      uint32_t second = first + seg_h + 1;
      idx[ii++] = first; idx[ii++] = second; idx[ii++] = first + 1;
      idx[ii++] = second; idx[ii++] = second + 1; idx[ii++] = first + 1;
    }
  }
  return ii;
}

uint32_t Renderer::capsule(Vertex *v, uint32_t *idx, float radius, float half_height, uint32_t seg_h, uint32_t seg_v) {
  uint32_t vi = 0, ii = 0;
  uint32_t half_v = seg_v / 2;
  // Ust yari kure
  for (uint32_t y = 0; y <= half_v; ++y) {
    float v_val = (float)y / seg_v;
    float phi = v_val * 3.14159265359f;
    float sin_phi = std::sin(phi);
    float cos_phi = std::cos(phi);
    for (uint32_t x = 0; x <= seg_h; ++x) {
      float u_val = (float)x / seg_h;
      float theta = u_val * 2.0f * 3.14159265359f;
      Vec3 N{std::cos(theta) * sin_phi, cos_phi, std::sin(theta) * sin_phi};
      v[vi++] = {{N.x * radius, N.y * radius + half_height, N.z * radius}, N, {u_val, v_val}};
    }
  }
  // Alt yari kure
  for (uint32_t y = half_v; y <= seg_v; ++y) {
    float v_val = (float)y / seg_v;
    float phi = v_val * 3.14159265359f;
    float sin_phi = std::sin(phi);
    float cos_phi = std::cos(phi);
    for (uint32_t x = 0; x <= seg_h; ++x) {
      float u_val = (float)x / seg_h;
      float theta = u_val * 2.0f * 3.14159265359f;
      Vec3 N{std::cos(theta) * sin_phi, cos_phi, std::sin(theta) * sin_phi};
      v[vi++] = {{N.x * radius, N.y * radius - half_height, N.z * radius}, N, {u_val, v_val}};
    }
  }
  for (uint32_t y = 0; y < seg_v + 1; ++y) { // +1 for the extra cylinder loop
    for (uint32_t x = 0; x < seg_h; ++x) {
      uint32_t first = (y * (seg_h + 1)) + x;
      uint32_t second = first + seg_h + 1;
      idx[ii++] = first; idx[ii++] = second; idx[ii++] = first + 1;
      idx[ii++] = second; idx[ii++] = second + 1; idx[ii++] = first + 1;
    }
  }
  return ii;
}

uint32_t Renderer::cylinder(Vertex *v, uint32_t *idx, float radius, float half_height, uint32_t seg_h) {
  uint32_t vi = 0, ii = 0;
  // Yan yuzey
  for (uint32_t y = 0; y <= 1; ++y) {
    float y_pos = y == 0 ? half_height : -half_height;
    for (uint32_t x = 0; x <= seg_h; ++x) {
      float u_val = (float)x / seg_h;
      float theta = u_val * 2.0f * 3.14159265359f;
      Vec3 N{std::cos(theta), 0, std::sin(theta)};
      v[vi++] = {{N.x * radius, y_pos, N.z * radius}, N, {u_val, (float)y}};
    }
  }
  for (uint32_t x = 0; x < seg_h; ++x) {
    uint32_t first = x;
    uint32_t second = first + seg_h + 1;
    idx[ii++] = first; idx[ii++] = second; idx[ii++] = first + 1;
    idx[ii++] = second; idx[ii++] = second + 1; idx[ii++] = first + 1;
  }
  // Ust kapak
  uint32_t top_center = vi;
  v[vi++] = {{0, half_height, 0}, {0, 1, 0}, {0.5f, 0.5f}};
  uint32_t top_start = vi;
  for (uint32_t x = 0; x <= seg_h; ++x) {
    float theta = ((float)x / seg_h) * 2.0f * 3.14159265359f;
    v[vi++] = {{std::cos(theta) * radius, half_height, std::sin(theta) * radius}, {0, 1, 0}, {std::cos(theta)*0.5f+0.5f, std::sin(theta)*0.5f+0.5f}};
  }
  for (uint32_t x = 0; x < seg_h; ++x) {
    idx[ii++] = top_center; idx[ii++] = top_start + x; idx[ii++] = top_start + x + 1;
  }
  // Alt kapak
  uint32_t bot_center = vi;
  v[vi++] = {{0, -half_height, 0}, {0, -1, 0}, {0.5f, 0.5f}};
  uint32_t bot_start = vi;
  for (uint32_t x = 0; x <= seg_h; ++x) {
    float theta = ((float)x / seg_h) * 2.0f * 3.14159265359f;
    v[vi++] = {{std::cos(theta) * radius, -half_height, std::sin(theta) * radius}, {0, -1, 0}, {std::cos(theta)*0.5f+0.5f, std::sin(theta)*0.5f+0.5f}};
  }
  for (uint32_t x = 0; x < seg_h; ++x) {
    idx[ii++] = bot_center; idx[ii++] = bot_start + x + 1; idx[ii++] = bot_start + x;
  }
  return ii;
}

uint32_t Renderer::cone(Vertex *v, uint32_t *idx, float radius, float height, uint32_t seg_h) {
  uint32_t vi = 0, ii = 0;
  float half_height = height * 0.5f;
  // Yan yuzey
  float slant = std::atan2(radius, height);
  float cos_slant = std::cos(slant);
  float sin_slant = std::sin(slant);
  uint32_t top = vi;
  for (uint32_t x = 0; x <= seg_h; ++x) { // ust noktalar kopyalanir (normalleri farkli)
    float u_val = (float)x / seg_h;
    float theta = u_val * 2.0f * 3.14159265359f;
    Vec3 N{std::cos(theta)*cos_slant, sin_slant, std::sin(theta)*cos_slant};
    v[vi++] = {{0, half_height, 0}, N, {u_val, 0}};
  }
  uint32_t bot = vi;
  for (uint32_t x = 0; x <= seg_h; ++x) {
    float u_val = (float)x / seg_h;
    float theta = u_val * 2.0f * 3.14159265359f;
    Vec3 N{std::cos(theta)*cos_slant, sin_slant, std::sin(theta)*cos_slant};
    v[vi++] = {{std::cos(theta) * radius, -half_height, std::sin(theta) * radius}, N, {u_val, 1}};
  }
  for (uint32_t x = 0; x < seg_h; ++x) {
    idx[ii++] = top + x; idx[ii++] = bot + x; idx[ii++] = bot + x + 1;
    idx[ii++] = top + x; idx[ii++] = bot + x + 1; idx[ii++] = top + x + 1;
  }
  // Alt kapak
  uint32_t bot_center = vi;
  v[vi++] = {{0, -half_height, 0}, {0, -1, 0}, {0.5f, 0.5f}};
  uint32_t bot_start = vi;
  for (uint32_t x = 0; x <= seg_h; ++x) {
    float theta = ((float)x / seg_h) * 2.0f * 3.14159265359f;
    v[vi++] = {{std::cos(theta) * radius, -half_height, std::sin(theta) * radius}, {0, -1, 0}, {std::cos(theta)*0.5f+0.5f, std::sin(theta)*0.5f+0.5f}};
  }
  for (uint32_t x = 0; x < seg_h; ++x) {
    idx[ii++] = bot_center; idx[ii++] = bot_start + x + 1; idx[ii++] = bot_start + x;
  }
  return ii;
}

uint32_t Renderer::quad(Vertex *v, uint32_t *idx) {
  Vec3 N{0, 0, 1};
  v[0] = {{-0.5f, -0.5f, 0}, N, {0, 0}}; v[1] = {{0.5f, -0.5f, 0}, N, {1, 0}};
  v[2] = {{0.5f, 0.5f, 0}, N, {1, 1}};   v[3] = {{-0.5f, 0.5f, 0}, N, {0, 1}};
  uint32_t i[6] = {0, 1, 2, 0, 2, 3};
  std::memcpy(idx, i, sizeof i);
  return 6;
}

uint32_t Renderer::torus(Vertex *v, uint32_t *idx, float r_main, float r_tube, uint32_t seg_main, uint32_t seg_tube) {
  uint32_t vi = 0, ii = 0;
  for (uint32_t y = 0; y <= seg_tube; ++y) {
    float v_val = (float)y / seg_tube;
    float phi = v_val * 2.0f * 3.14159265359f;
    float cos_phi = std::cos(phi);
    float sin_phi = std::sin(phi);
    for (uint32_t x = 0; x <= seg_main; ++x) {
      float u_val = (float)x / seg_main;
      float theta = u_val * 2.0f * 3.14159265359f;
      float cos_th = std::cos(theta);
      float sin_th = std::sin(theta);
      Vec3 N{cos_th * cos_phi, sin_phi, sin_th * cos_phi};
      v[vi++] = {{cos_th * (r_main + r_tube * cos_phi), r_tube * sin_phi, sin_th * (r_main + r_tube * cos_phi)}, N, {u_val, v_val}};
    }
  }
  for (uint32_t y = 0; y < seg_tube; ++y) {
    for (uint32_t x = 0; x < seg_main; ++x) {
      uint32_t first = (y * (seg_main + 1)) + x;
      uint32_t second = first + seg_main + 1;
      idx[ii++] = first; idx[ii++] = second; idx[ii++] = first + 1;
      idx[ii++] = second; idx[ii++] = second + 1; idx[ii++] = first + 1;
    }
  }
  return ii;
}

} // namespace tulpar::engine::renderer
