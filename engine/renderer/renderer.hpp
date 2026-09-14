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
  // Yonlu isik golge haritasi (tek kademe). 0 = golge yok. Mobil: D16 tercih
  // edilir (bant genisligi), cihaz vermezse D32'ye duser.
  uint32_t shadow_size = 2048;
  float shadow_bias = 0.0008f;         // derinlik uzayinda kucuk sabit egilim
  float shadow_normal_offset = 0.06f;  // DUNYA birimi: normal boyunca kaydirma (akne)
};

struct ShadowInfo {
  bool enabled = false;
  uint32_t size = 0;
  VkFormat format = VK_FORMAT_UNDEFINED;
  bool linear_filter = false; // donanim PCF (yoksa NEAREST)
  const char *disabled_reason = "";
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
  // Golge kutusu: isik yonu (isiga DOGRU), sahne merkezi, yaricap, derinlik.
  void set_shadow_volume(Vec3 center, float radius, float depth);
  ShadowInfo shadow() const { return shadow_info_; }
  // A/B ve dusuk segment icin: hedef durur, gecis ve ornekleme kapanir.
  void set_shadows_enabled(bool on) { shadow_info_.enabled = on && cfg_.shadow_size > 0; }
  // Egilim ayari (cihaza gore): derinlik uzayinda sabit + DUNYA biriminde normal
  // kaydirmasi. Kaydirma buyurse golge kacar (peter-panning), kucukse akne.
  void set_shadow_bias(float depth_bias, float normal_offset) {
    cfg_.shadow_bias = depth_bias;
    cfg_.shadow_normal_offset = normal_offset;
  }
  // Yonlu isik icin isik-uzayi viewproj (ortografik). Golge kutusu sahneyi kapsamali.
  static Mat4 directional_light_matrix(Vec3 dir, Vec3 center, float radius, float depth);

  // Kare: begin (UBO yaz) -> draw*N -> record(cb): subpass0 depth, next, subpass1 renk.
  void begin_frame(uint32_t frame_index);
  void draw(MeshHandle mesh, const Mat4 &model, Vec3 color);
  // Golge gecisi: KENDI render pass'ini acar/kapatir. Ana render pass BASLAMADAN
  // once cagrilir (cizim listesi dolu olmali).
  void record_shadow(VkCommandBuffer cb);
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
    Mat4 light_viewproj;
    float light_dir[4];
    float ambient[4];
    float shadow_params[4]; // x: 1/boyut, y: egilim, z: acik mi, w: normal kaydirma
  };
  bool make_buffer(VkBufferUsageFlags usage, VkDeviceSize size, VkMemoryPropertyFlags mem, VkBuffer *buf,
                   rhi::MemoryAlloc *out);
  bool upload(VkBuffer dst, const void *data, VkDeviceSize size, VkBufferUsageFlags usage_dst);
  bool make_pipelines(VkRenderPass rp);
  bool make_shadow(); // render pass + goruntu + sampler + boru hatti

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
  Vec3 shadow_center_{0, 0, 0};
  float shadow_radius_ = 16.0f, shadow_depth_ = 60.0f;
  Mat4 light_vp_{};
  ShadowInfo shadow_info_{};
  VkShaderModule vs_ = VK_NULL_HANDLE, fs_ = VK_NULL_HANDLE, shadow_vs_ = VK_NULL_HANDLE;
  VkRenderPass shadow_rp_ = VK_NULL_HANDLE;
  VkImage shadow_img_ = VK_NULL_HANDLE;
  VkImageView shadow_view_ = VK_NULL_HANDLE;
  VkFramebuffer shadow_fb_ = VK_NULL_HANDLE;
  VkSampler shadow_sampler_ = VK_NULL_HANDLE;
  rhi::MemoryAlloc shadow_mem_{};
  VkPipeline pipe_shadow_ = VK_NULL_HANDLE;
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
