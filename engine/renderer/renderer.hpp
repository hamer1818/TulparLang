// L3 RENDERER — Faz 3 ilk dilim: forward (Lambert) cizici. Depth prepass ->
// renk subpass (RHI render pass'i: swapchain ya da offscreen). Mesh'ler
// yukleme aninda (staging), cizim listesi kare basina sabit kapasite (A2),
// kare UBO ucuslu kare basina, push sabitiyle model+renk. Bindless/kume
// isiklandirma/CSM sonraki adimlar (PLAN.md Faz 3).
#pragma once
#include <cmath>
#include <cstddef>
#include <cstdint>

#include "core/math/vec.hpp"
#include "core/memory/arena.hpp"
#include "renderer/cluster.hpp"
#include "rhi/device.hpp"

namespace tulpar::engine::renderer {

struct Vertex {
  Vec3 pos;
  Vec3 nrm;
  Vec2 uv;
};

// Iskeletli vertex: 4 eklem (u8) + 4 agirlik (unorm16) = 40 bayt.
struct SkinnedVertex {
  Vec3 pos;
  Vec3 nrm;
  Vec2 uv;
  uint8_t joints[4];
  uint16_t weights[4];
};
struct MeshHandle {
  uint32_t id = 0xFFFFFFFFu;
  bool valid() const { return id != 0xFFFFFFFFu; }
};
struct TextureHandle {
  uint32_t id = 0xFFFFFFFFu;
  bool valid() const { return id != 0xFFFFFFFFu; }
};
// Malzeme = albedo dokusu + renk carpani. Klasik descriptor set (set 1) — bindless
// YOK: Dusuk sinif cihaz descriptorIndexing vermiyor (PLAN.md REV-3), bu yol
// her cihazda calisan yedek. Malzeme degisiminde set baglanir; cizim listesi
// malzemeye gore siralanmaz (sonraki adim).
struct MaterialHandle {
  uint32_t id = 0xFFFFFFFFu;
  bool valid() const { return id != 0xFFFFFFFFu; }
};

struct RendererConfig {
  // Hedef sRGB bicimli mi (swapchain/offscreen SRGB): donanim kodlar. false:
  // UNORM hedef, shader kodlar (yedek yol; ayni goruntu, biraz daha pahali).
  bool srgb_target = true;
  uint32_t max_meshes = 64;
  uint32_t max_textures = 64;
  uint32_t max_materials = 64;
  uint32_t max_draws = 8192;
  uint32_t frames_in_flight = 2;
  // Yonlu isik golge haritasi (tek kademe). 0 = golge yok. Mobil: D16 tercih
  // edilir (bant genisligi), cihaz vermezse D32'ye duser.
  uint32_t shadow_size = 2048;
  float shadow_bias = 0.0008f;         // derinlik uzayinda kucuk sabit egilim
  float shadow_normal_offset = 0.06f;  // DUNYA birimi: normal boyunca kaydirma (akne)
  uint32_t ui_max_vertices = 32768;
  uint32_t max_skin_matrices = 4096;    // kare basina eklem matrisi (SSBO, set 0 binding 4)    // 2B arayuz: kare basina (ucgen listesi, 6/dortgen)
};

// 2B arayuz koseleri: piksel uzayi, atlas uv, RGBA8. Immediate-mode: her kare
// yeniden uretilir, sabit kapasite (A2), ayirma yok.
struct UiVertex {
  float x, y, u, v;
  uint32_t rgba;
};
struct UiStats {
  uint32_t vertices = 0, dropped = 0;
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
  uint32_t textures = 0;
  uint32_t materials = 0;
  uint32_t material_binds = 0; // son kare (siralama yoksa cizim sayisina yaklasir)
  ClusterStats clusters;       // son kare isik atamasi
};

class Renderer {
public:
  bool init(rhi::Device &dev, Arena &arena, VkRenderPass rp, const RendererConfig &cfg);
  void shutdown();

  // Yukleme: staging ile device-local. Kare icinde CAGRILMAZ.
  MeshHandle create_mesh(const Vertex *verts, uint32_t nverts, const uint32_t *indices, uint32_t nindices);
  MeshHandle create_skinned_mesh(const SkinnedVertex *verts, uint32_t nverts, const uint32_t *indices, uint32_t nindices);
  // Indeks araligi seyrek (Mali kurali, CPU'da olculur) mesh sayisi; kapi 0 bekler.
  uint32_t sparse_mesh_count() const { return sparse_mesh_count_; }
  // RGBA8, mip zinciri blit ile uretilir (yukleme aninda). Mobil asil yol ASTC (Faz 6).
  // srgb: renk verisi (albedo) -> R8G8B8A8_SRGB, ornekleme dogrusal dondurur.
  // false: veri (kaplama/alfa/normal) -> UNORM, oldugu gibi.
  TextureHandle create_texture(const uint8_t *rgba, uint32_t w, uint32_t h, bool mipmaps = true, bool srgb = true);
  // Hazir mip zinciri (sikistirilmis ASTC bloklari ya da RGBA8): seviye basina
  // bayt dizisi, blit yok. fmt: VK_FORMAT_ASTC_*_SRGB_BLOCK / R8G8B8A8_SRGB...
  TextureHandle create_texture_levels(VkFormat fmt, uint32_t w, uint32_t h, uint32_t levels, const uint8_t *const *data,
                                      const uint32_t *sizes);
  MaterialHandle create_material(TextureHandle albedo, Vec3 color = {1, 1, 1});
  TextureHandle default_texture() const { return default_texture_; } // 1x1 beyaz
  MaterialHandle default_material() const { return default_material_; }

  void set_camera(const Mat4 &view, const Mat4 &proj);
  void set_light(Vec3 dir, Vec3 ambient, float diffuse_scale);
  // Kume gridi framebuffer uzayinda: hedefin olcusu (swapchain goruntusu / offscreen).
  void set_render_size(uint32_t width, uint32_t height);
  // Nokta isiklar (kare basina en cok 32; kumelenmis, CPU atamali). begin_frame'de atanir.
  static constexpr uint32_t kMaxPointLights = 32;
  void clear_point_lights() { point_light_count_ = 0; }
  bool add_point_light(const PointLight &l);
  uint32_t point_light_count() const { return point_light_count_; }
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
  void draw(MeshHandle mesh, const Mat4 &model, Vec3 color); // varsayilan malzeme
  void draw(MeshHandle mesh, MaterialHandle material, const Mat4 &model, Vec3 color = {1, 1, 1});
  // Iskeletli cizim: joints[n] = model uzayi eklem matrisi * ters bind (skin
  // matrisi). Kare SSBO'suna kopyalanir; kapasite asimi sayilir (dropped).
  void draw_skinned(MeshHandle mesh, MaterialHandle material, const Mat4 &model, Vec3 color, const Mat4 *joints, uint32_t n);
  uint32_t skin_matrices_used() const { return skin_count_; }
  // Golge gecisi: KENDI render pass'ini acar/kapatir. Ana render pass BASLAMADAN
  // once cagrilir (cizim listesi dolu olmali).
  void record_shadow(VkCommandBuffer cb);
  void record(VkCommandBuffer cb);

  RendererStats stats() const { return stats_; }

  // --- 2B arayuz (HUD, editor). record() SONRA, ayni subpass'te ui_record(). ---
  // screen: MANTIKSAL (gorunen) olcu; rotation: on-dondurme acisi (Swapchain::rotation_radians).
  void ui_begin(float screen_w, float screen_h, float rotation_radians = 0.0f); // begin_frame sonrasi
  void ui_set_atlas(MaterialHandle atlas);                    // glif atlasi (texel 0,0 beyaz)
  void ui_quad(float x, float y, float w, float h, float u0, float v0, float u1, float v1, uint32_t rgba);
  void ui_rect(float x, float y, float w, float h, uint32_t rgba); // duz kutu (beyaz texel)
  void ui_record(VkCommandBuffer cb);
  UiStats ui_stats() const { return ui_stats_; }
  // Yazarin verdigi renkler (malzeme, cizim, UI) sRGB algisaldir; aydinlatma
  // DOGRUSAL uzayda (Filament PBR tarifi). Bu donusum CPU'da malzeme/cizim
  // rengine, UI'da shader'da uygulanir; dokular SRGB bicimiyle donanimda.
  static float srgb_to_linear(float c) {
    return c <= 0.04045f ? c / 12.92f : powf((c + 0.055f) / 1.055f, 2.4f);
  }
  static Vec3 srgb_to_linear(Vec3 c) { return {srgb_to_linear(c.x), srgb_to_linear(c.y), srgb_to_linear(c.z)}; }
  static uint32_t rgba(uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255) {
    return (uint32_t)r | ((uint32_t)g << 8) | ((uint32_t)b << 16) | ((uint32_t)a << 24);
  }

  // Yerlesik mesh'ler (cagiranin dizilerine yazar): kup [-0.5,0.5]^3, duzlem 1x1 (y=0).
  static uint32_t cube(Vertex *v, uint32_t *idx);                         // 24 v, 36 idx (uv yuz basina 0..1)
  static uint32_t plane(Vertex *v, uint32_t *idx, float uv_repeat = 1.0f); // 4 v, 6 idx

private:
  struct Mesh {
    VkBuffer vbuf = VK_NULL_HANDLE, ibuf = VK_NULL_HANDLE;
    uint32_t index_count = 0;
    bool skinned = false;
  };
  struct Texture {
    VkImage image = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
    uint32_t w = 0, h = 0, mips = 0;
  };
  struct Material {
    VkDescriptorSet set = VK_NULL_HANDLE;
    uint32_t texture = 0;
    Vec3 color{1, 1, 1};
  };
  static constexpr uint32_t kNoSkin = 0xFFFFFFFFu;
  struct Draw {
    uint32_t mesh;
    uint32_t material;
    Mat4 model;
    Vec3 color;
    uint32_t skin_offset; // kNoSkin = statik
  };
  struct Push { // GLSL Push { mat4 model; vec4 color; uvec4 skin; } = 96 bayt
    Mat4 model;
    float color[4];
    uint32_t skin[4];
  };
  static_assert(sizeof(Push) == 96, "push sabiti 96 bayt");
  struct FrameUbo {
    Mat4 viewproj;
    Mat4 view;
    Mat4 light_viewproj;
    float light_dir[4];
    float ambient[4];
    float shadow_params[4];  // x: 1/boyut, y: egilim, z: acik mi, w: normal kaydirma
    float cluster_params[4]; // x: dilim olcegi, y: dilim sapmasi, z: tile genisligi(px), w: tile yuksekligi(px)
    uint32_t cluster_grid[4]; // x, y, z, isik sayisi
  };
  // The Forge SRT ilkesi (CPU-GPU tek kaynak tablosu): GLSL std140 blogu ile bu
  // struct'in ofsetleri DERLEME zamaninda eslesir; kayma = derleme hatasi.
  // mesh.frag/mesh.vert "Frame" blogu: 3 x mat4 (192) + 4 x vec4 (64) + uvec4 (16) = 272.
  static_assert(offsetof(FrameUbo, view) == 64, "std140: view");
  static_assert(offsetof(FrameUbo, light_viewproj) == 128, "std140: light_viewproj");
  static_assert(offsetof(FrameUbo, light_dir) == 192, "std140: light_dir");
  static_assert(offsetof(FrameUbo, ambient) == 208, "std140: ambient");
  static_assert(offsetof(FrameUbo, shadow_params) == 224, "std140: shadow_params");
  static_assert(offsetof(FrameUbo, cluster_params) == 240, "std140: cluster_params");
  static_assert(offsetof(FrameUbo, cluster_grid) == 256, "std140: cluster_grid");
  static_assert(sizeof(FrameUbo) == 272, "std140: Frame blogu 272 bayt");
  struct GpuPointLight { // GLSL PointLight { vec4 pos_radius; vec4 color_intensity; } = 32 bayt
    float pos_radius[4];
    float color_intensity[4];
  };
  bool make_buffer(VkBufferUsageFlags usage, VkDeviceSize size, VkMemoryPropertyFlags mem, VkBuffer *buf,
                   rhi::MemoryAlloc *out);
  bool upload(VkBuffer dst, const void *data, VkDeviceSize size, VkBufferUsageFlags usage_dst);
  bool make_pipelines(VkRenderPass rp);
  bool make_pipeline_set(VkRenderPass rp, bool skinned, VkPipeline *depth, VkPipeline *color, VkPipeline *shadow);
  bool make_shadow(); // render pass + goruntu + sampler + boru hatti
  bool make_material_layout();
  bool make_ui(VkRenderPass rp);

  rhi::Device *dev_ = nullptr;
  RendererConfig cfg_{};
  Mesh *meshes_ = nullptr;
  uint32_t mesh_count_ = 0;
  Texture *textures_ = nullptr;
  uint32_t texture_count_ = 0;
  Material *materials_ = nullptr;
  uint32_t material_count_ = 0;
  VkSampler tex_sampler_ = VK_NULL_HANDLE;
  VkDescriptorSetLayout mat_layout_ = VK_NULL_HANDLE;
  VkDescriptorPool mat_pool_ = VK_NULL_HANDLE;
  TextureHandle default_texture_{};
  MaterialHandle default_material_{};
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
  VkShaderModule skin_vs_ = VK_NULL_HANDLE, skin_shadow_vs_ = VK_NULL_HANDLE;
  VkPipeline pipe_skin_depth_ = VK_NULL_HANDLE, pipe_skin_color_ = VK_NULL_HANDLE, pipe_skin_shadow_ = VK_NULL_HANDLE;
  uint32_t skin_count_ = 0;
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
  VkBuffer lights_buf_[kMaxFrames] = {};
  rhi::MemoryAlloc lights_mem_[kMaxFrames] = {};
  VkBuffer cluster_buf_[kMaxFrames] = {};
  rhi::MemoryAlloc cluster_mem_[kMaxFrames] = {};
  VkDescriptorSet sets_[kMaxFrames] = {};
  VkBuffer skin_buf_[kMaxFrames] = {};        // eklem matrisleri (kare basina, host-visible)
  rhi::MemoryAlloc skin_mem_[kMaxFrames] = {};
  // UI
  VkShaderModule ui_vs_ = VK_NULL_HANDLE, ui_fs_ = VK_NULL_HANDLE;
  VkPipeline pipe_ui_ = VK_NULL_HANDLE;
  VkBuffer ui_buf_[kMaxFrames] = {};
  rhi::MemoryAlloc ui_mem_[kMaxFrames] = {};
  uint32_t ui_count_ = 0;
  float ui_w_ = 1, ui_h_ = 1, ui_rot_ = 0;
  MaterialHandle ui_atlas_{};
  UiStats ui_stats_{};
  ClusterGrid grid_{};
  uint32_t *cluster_masks_ = nullptr; // Arena, grid_.count()
  PointLight point_lights_[kMaxPointLights];
  uint32_t point_light_count_ = 0;
  uint32_t render_w_ = 1, render_h_ = 1;
  uint32_t frame_ = 0;
  uint32_t sparse_mesh_count_ = 0;
};

} // namespace tulpar::engine::renderer
