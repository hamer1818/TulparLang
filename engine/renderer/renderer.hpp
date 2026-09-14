// L3 RENDERER — Faz 3 ilk dilim: forward (Lambert) cizici. Depth prepass ->
// renk subpass (RHI render pass'i: swapchain ya da offscreen). Mesh'ler
// yukleme aninda (staging), cizim listesi kare basina sabit kapasite (A2),
// kare UBO ucuslu kare basina, push sabitiyle model+renk. Bindless/kume
// isiklandirma/CSM sonraki adimlar (PLAN.md Faz 3).
#pragma once
#include <cstdint>

#include "core/math/vec.hpp"
#include "core/memory/arena.hpp"
#include "rhi/device.hpp"

namespace tulpar::engine::renderer {

struct Vertex {
  Vec3 pos;
  Vec3 nrm;
};

struct MeshHandle {
  uint32_t id = 0xFFFFFFFFu;
  bool valid() const { return id != 0xFFFFFFFFu; }
};

struct RendererConfig {
  uint32_t max_meshes = 64;
  uint32_t max_draws = 8192;
  uint32_t frames_in_flight = 2;
};

struct RendererStats {
  uint32_t draws = 0;      // son kare
  uint32_t dropped = 0;    // kapasite asimi (sayilir, sessiz degil)
  uint32_t meshes = 0;
};

class Renderer {
public:
  bool init(rhi::Device &dev, Arena &arena, VkRenderPass rp, const RendererConfig &cfg);
  void shutdown();

  // Yukleme: staging ile device-local. Kare icinde CAGRILMAZ.
  MeshHandle create_mesh(const Vertex *verts, uint32_t nverts, const uint32_t *indices, uint32_t nindices);

  void set_camera(const Mat4 &view, const Mat4 &proj);
  void set_light(Vec3 dir, Vec3 ambient, float diffuse_scale);

  // Kare: begin (UBO yaz) -> draw*N -> record(cb): subpass0 depth, next, subpass1 renk.
  void begin_frame(uint32_t frame_index);
  void draw(MeshHandle mesh, const Mat4 &model, Vec3 color);
  void record(VkCommandBuffer cb);

  RendererStats stats() const { return stats_; }

  // Yerlesik mesh'ler (cagiranin dizilerine yazar): kup [-0.5,0.5]^3, duzlem 1x1 (y=0).
  static uint32_t cube(Vertex *v, uint32_t *idx);  // 24 v, 36 idx
  static uint32_t plane(Vertex *v, uint32_t *idx); // 4 v, 6 idx

private:
  struct Mesh {
    VkBuffer vbuf = VK_NULL_HANDLE, ibuf = VK_NULL_HANDLE;
    uint32_t index_count = 0;
  };
  struct Draw {
    uint32_t mesh;
    Mat4 model;
    Vec3 color;
  };
  struct FrameUbo {
    Mat4 viewproj;
    float light_dir[4];
    float ambient[4];
  };
  bool make_buffer(VkBufferUsageFlags usage, VkDeviceSize size, VkMemoryPropertyFlags mem, VkBuffer *buf,
                   rhi::MemoryAlloc *out);
  bool upload(VkBuffer dst, const void *data, VkDeviceSize size, VkBufferUsageFlags usage_dst);
  bool make_pipelines(VkRenderPass rp);

  rhi::Device *dev_ = nullptr;
  RendererConfig cfg_{};
  Mesh *meshes_ = nullptr;
  uint32_t mesh_count_ = 0;
  Draw *draws_ = nullptr;
  uint32_t draw_count_ = 0;
  RendererStats stats_{};
  Mat4 view_{}, proj_{};
  Vec3 light_dir_{0.4f, 1.0f, 0.3f}, ambient_{0.12f, 0.13f, 0.16f};
  float diffuse_scale_ = 0.9f;
  VkShaderModule vs_ = VK_NULL_HANDLE, fs_ = VK_NULL_HANDLE;
  VkDescriptorSetLayout set_layout_ = VK_NULL_HANDLE;
  VkPipelineLayout layout_ = VK_NULL_HANDLE;
  VkPipeline pipe_depth_ = VK_NULL_HANDLE, pipe_color_ = VK_NULL_HANDLE;
  VkDescriptorPool pool_ = VK_NULL_HANDLE;
  static constexpr uint32_t kMaxFrames = 3;
  VkBuffer ubo_[kMaxFrames] = {};
  rhi::MemoryAlloc ubo_mem_[kMaxFrames] = {};
  VkDescriptorSet sets_[kMaxFrames] = {};
  uint32_t frame_ = 0;
};

} // namespace tulpar::engine::renderer
