// L3 RENDERER — GPU-driven kume elemesinin CPU TARAFI: shader'a giden veri
// duzeni + ayni mantigin CPU referansi.
//
// Karsilik geldigi shader: rhi/shaders/cluster_cull.comp
// (derlenmis hali rhi/shaders/cluster_cull_comp_spv.h).
//
// **Bu dosya neden var:** CPU ile GPU'nun AYNI kumeleri secmesi sart. Iki
// yerde ayri ayri yazilmis matematik sessizce ayrisir. Burasi iki seyi
// birden yapiyor:
//   1) shader'in okudugu tampon duzenini DERLEME ZAMANINDA sabitliyor
//      (static_assert'ler; alan sirasi/boyut kayarsa build kirilir),
//   2) shader'in mantiginin CPU karsiligini veriyor -- hem testlerin
//      karsilastirma olcusu, hem compute kullanilmadiginda GERCEK yedek yol.
//
// **Neden compute + indirect, mesh shader degil:** VK_EXT_mesh_shader
// Android'de ~%5 kapsama, iOS'ta %0. compute + vkCmdDrawIndexedIndirect ise
// Vulkan 1.0 CEKIRDEGI: uzanti gerekmez.
#pragma once
#include <cstdint>

#include "core/math/vec.hpp"
#include "renderer/cluster_lod.hpp"

namespace tulpar::engine::renderer {

// SSBO'daki kume kaydi. content/clod_format.hpp'deki ClodClusterRaw ile
// AYNI duzen; shader'daki GpuCluster ile de ayni. YALNIZ skaler alanlar
// kullaniliyor: std430'da skaler dizilerde gizli dolgu OLUSMAZ, bu yuzden
// C++ / GLSL yerlesim farki riski yoktur (vec3 kullanilsaydi 16 bayta
// hizalanir ve sessizce kayardi).
struct GpuCluster {
  float center_x, center_y, center_z;
  float radius;
  float cur_x, cur_y, cur_z;
  float cur_radius, cur_error;
  float coa_x, coa_y, coa_z;
  float coa_radius, coa_error;
  uint32_t index_offset, index_count, geometry, level;
};
static_assert(sizeof(GpuCluster) == 72, "GpuCluster, shader'daki GpuCluster ile ayni boyutta olmali");

// VkDrawIndexedIndirectCommand ile ALAN ALAN AYNI. Vulkan basligini bu
// katmanda dahil etmemek icin yeniden bildirildi; boyut/sira static_assert
// ile sabitlendi ve Vulkan spesifikasyonu bu yapiyi DONDURDU (1.0'dan beri
// degismedi), yani sessizce kayamaz.
struct DrawIndexedIndirectCommand {
  uint32_t index_count;
  uint32_t instance_count;
  uint32_t first_index;
  int32_t vertex_offset;
  uint32_t first_instance;
};
static_assert(sizeof(DrawIndexedIndirectCommand) == 20,
              "VkDrawIndexedIndirectCommand 20 bayttir");

// Shader'in push constant blogu. 128 bayt: maxPushConstantsSize'in Vulkan
// tarafindan GARANTI EDILEN alt siniri. Bir bayt daha buyurse bazi
// cihazlarda boru hatti YARATILAMAZDI.
struct ClusterCullPush {
  // Plane { Vec3 normal; float d; } duzeni shader'daki vec4(normal.xyz, d)
  // ile BIREBIR ayni -- Frustum::planes dogrudan kopyalanabilir.
  Plane planes[6];     //  0..95  frustum, MESH uzayinda
  Vec3 camera;         // 96..107 kamera, MESH uzayinda
  float proj;          //     108 projection[1][1]
  float znear;         //     112 POZITIF
  float screen_height; //     116
  float threshold_px;  //     120
  uint32_t cluster_count; //  124
};
static_assert(sizeof(Plane) == 16, "Plane, shader'daki vec4 ile ayni boyutta olmali");
static_assert(sizeof(ClusterCullPush) == 128,
              "push constant blogu 128 baytin USTUNE cikamaz (garantili alt sinir)");

// Shader'daki yerel is grubu boyutu (local_size_x). Dispatch bununla
// yuvarlanir; shader artigi kendisi eler.
constexpr uint32_t kClusterCullGroupSize = 64;
inline uint32_t cluster_cull_group_count(uint32_t cluster_count) {
  return (cluster_count + kClusterCullGroupSize - 1) / kClusterCullGroupSize;
}

// Cluster -> GpuCluster (SSBO'ya yuklenecek hali).
GpuCluster to_gpu_cluster(const Cluster &c);

// Push constant blogunu doldur.
ClusterCullPush make_cull_push(const LodView &view, const Frustum &frustum, uint32_t cluster_count);

// --- Shader'in CPU REFERANSI -----------------------------------------
// rhi/shaders/cluster_cull.comp'un main()'iyle AYNI isi yapar: her kume
// icin bir indirect komut yazar, elenenlerin instance_count'u 0 olur.
// Komut HER kume icin yazilir (yazmamak, tamponda onceki karenin verisini
// birakirdi) -- bu yuzden `out_draws` en az `cluster_count` elemanlik
// olmalidir; degilse false doner.
//
// Iki kullanimi var: testlerde GPU portunun sadakatini olcmek, ve compute
// yolu kapaliyken gercek yedek yol olarak calismak.
bool cluster_cull_reference(const ClusterLodMesh &mesh, const ClusterCullPush &push,
                            DrawIndexedIndirectCommand *out_draws, uint32_t max_draws);

} // namespace tulpar::engine::renderer
