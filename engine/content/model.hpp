// L6 CONTENT — bellekteki model: glTF'ten (ya da ileride kendi pack
// formatindan) okunmus, Arena'da sabit kapasiteli diziler. Renderer'a
// yuklenmek uzere; sahne akisi yok (bolum = resident set, PLAN §8/5).
#pragma once
#include <cstdint>

#include "core/math/vec.hpp"
#include "core/memory/arena.hpp"
#include "renderer/renderer.hpp"
#include "sim/animation.hpp"

namespace tulpar::engine::content {

struct ClusterDag; // content/cluster_dag.hpp (Faz 9: kume DAG'i; ileri bildirim)

struct ModelImage {
  uint32_t width = 0, height = 0;
  uint8_t *rgba = nullptr; // Arena'da, width*height*4
  // RENK UZAYI KULLANIMDAN turetilir, dosyadan degil: glTF goruntuleri renk
  // uzayi tasimaz — baseColor/emissive sRGB, metallicRoughness/normal/occlusion
  // DOGRUSAL veridir (spec). Yanlis uzay hicbir seyi kizartmaz, yalnizca
  // puruzlulugu/normali sessizce egriltir; kapi: content_gltf_texture_colorspace.
  bool srgb = true;
  // NORMAL HARITASI MI: mip zinciri BLIT ile uretilirse dogrusal suzme
  // komsu normalleri ORTALAR ve ortalama vektorun boyu 1'den KUCUK olur —
  // uzaktaki yuzey sessizce duzlesir (isik "yassilasir"). Tek dogrusu
  // kucultmeden sonra YENIDEN NORMALLESTIRMEK; o yuzden bu goruntuler
  // CPU'da mip'lenip hazir seviye olarak yukleniyor. ORM bu bayragi
  // ALMAZ: puruzluluk/metaliklik birer skaler, normallestirilmeleri
  // anlamsiz olurdu.
  bool normal_map = false;
};
struct ModelMaterial {
  int32_t image = -1; // -1: dokusuz (beyaz)
  Vec3 base_color{1, 1, 1};
  // glTF metallic-roughness. Varsayilanlar glTF 2.0 spesifikasyonunun kendi
  // varsayilanlari DEGIL (spec metallic=1, roughness=1 der); burada
  // metallic=0/roughness=1 = Lambert'e en yakin nokta secildi, cunku PBR yolu
  // motorda opsiyonel ve `has_pbr` yanlisken eski goruntu korunmali.
  float metallic = 0.0f;
  float roughness = 1.0f;
  Vec3 emissive{0, 0, 0}; // glTF emissive_factor (dogrusal)
  bool has_pbr = false;   // dosyada pbr_metallic_roughness blogu VAR miydi
  // Doku basina degisen kanallar (-1 = yok, o kanal yalniz carpandan gelir).
  // ORM = glTF metallicRoughnessTexture: G ROUGHNESS, B METALLIC (spec);
  // occlusionTexture AYNI goruntuyu gosteriyorsa R kanali occlusion olur ve
  // ekstra sampler'a gerek kalmaz (yayginlasmis "ORM" paketlemesi).
  int32_t orm_image = -1;
  int32_t normal_image = -1;
  int32_t emissive_image = -1;
  float normal_scale = 1.0f;       // glTF normalTexture.scale
  float occlusion_strength = 0.0f; // glTF occlusionTexture.strength; 0 = occlusion yok
  // occlusionTexture ORM'den FARKLI bir goruntuyu gosteriyor: bugun okunmuyor
  // (dorduncu sampler'a degmez). Sayilir, sessizce yutulmaz.
  bool occlusion_separate = false;
};
constexpr uint32_t kModelMaxLods = 2; // LOD1, LOD2 (LOD0 = indices)
constexpr uint32_t kModelMaxSkins = 4;
constexpr uint32_t kModelMaxJoints = 128; // vertex eklem indeksi u8

// Iskelet (glTF skin): eklemler ebeveyn-once sirada (sim::to_model kurali),
// glTF eklem sirasindan yeniden siralanmis; vertex eklem indeksleri de oyle.
struct ModelSkin {
  sim::Joint *joints = nullptr;   // [joint_count] (Arena)
  uint32_t joint_count = 0;
  Mat4 *inverse_bind = nullptr;   // [joint_count]
  char name[64] = {0};
};
// Klip: glTF animasyonu sabit hizda ornekleyip ClipBuilder ile sikistirilmis.
struct ModelClip {
  char name[64] = {0};
  int32_t skin = -1;              // hangi iskelet
  float duration = 0;             // saniye
  const sim::ClipHeader *clip = nullptr;
  sim::ClipBuildStats stats{};
};
struct ModelMesh {
  renderer::Vertex *verts = nullptr;
  uint32_t vertex_count = 0;
  uint32_t *indices = nullptr;
  uint32_t index_count = 0;
  int32_t material = -1;
  // meshoptimizer ile uretilen ayrik LOD'lar (ayni vertex tamponu, kisa indeks
  // listesi). lod_index_count 0 = o seviye yok (sadelestirme hedefe ulasamadi).
  uint32_t *lod_indices[kModelMaxLods] = {};
  uint32_t lod_index_count[kModelMaxLods] = {};
  float lod_error[kModelMaxLods] = {}; // bagil (mesh olcegine gore)
  // Kume (cluster) DAG: GltfLimits::cluster_dag acikken kurulur (Faz 9).
  // Ayrik LOD'larin (lod_indices) YERINE GECMEZ, ustune gelir: LOD butun mesh'i
  // birden degistirir, DAG kume kume ve cut'i runtime secer.
  const ClusterDag *dag = nullptr;
  // Iskeletli: skin >= 0 ise skin_verts dolu (verts ile ayni sira/sayi; JOINTS_0
  // yeniden siralanmis, WEIGHTS_0 unorm16). Iskeletli mesh'te meshopt/LOD atlanir.
  int32_t skin = -1;
  renderer::SkinnedVertex *skin_verts = nullptr;
};
// Yukleme sonrasi mesh optimizasyonu olcumu (meshoptimizer analyze*): sayilar
// ucgen agirlikli ortalama. ACMR = donusturulen vertex / ucgen (0.5 en iyi).
struct ModelOptStats {
  float acmr_before = 0, acmr_after = 0;
  float overdraw_before = 0, overdraw_after = 0;
  uint32_t vertices_before = 0, vertices_after = 0; // tekillestirme
  uint32_t triangles = 0;
};
struct ModelInstance {
  uint32_t mesh = 0;
  Mat4 world;
};
struct Model {
  ModelImage *images = nullptr;      uint32_t image_count = 0;
  ModelMaterial *materials = nullptr; uint32_t material_count = 0;
  ModelMesh *meshes = nullptr;       uint32_t mesh_count = 0;
  ModelInstance *instances = nullptr; uint32_t instance_count = 0;
  ModelSkin *skins = nullptr;        uint32_t skin_count = 0;
  ModelClip *clips = nullptr;        uint32_t clip_count = 0;
  Vec3 bounds_min{0, 0, 0}, bounds_max{0, 0, 0}; // ornek uzayinda, tum instance'lar
  ModelOptStats opt;
  // Ayni goruntu hem renk (sRGB) hem veri (dogrusal) olarak kullanilmis:
  // glTF'te gecersiz. Renk kazanir, sayac artar — sessiz yanlis yok.
  uint32_t colorspace_conflicts = 0;
  char error[160] = {0};
};

// GPU'ya yuklenmis hal: her mesh/malzeme icin renderer tutamaclari.
struct UploadedModel {
  renderer::MeshHandle *meshes = nullptr;
  renderer::MeshHandle *lod_meshes = nullptr; // mesh_count x kModelMaxLods; gecersiz = yok
  renderer::MaterialHandle *materials = nullptr;
  renderer::TextureHandle *textures = nullptr;
  uint32_t mesh_count = 0, material_count = 0, texture_count = 0;
  // Kac doku CPU'da yeniden normallestirilmis mip zinciriyle yuklendi.
  // Kapi bunu okuyor: `build_normal_mips` dogru calissa bile yukleme yolu
  // onu hic CAGIRMAZSA goruntu eski davranista kalirdi ve dogrudan
  // fonksiyonu olcen bir test bunu goremezdi.
  uint32_t normal_mip_textures = 0;
};

// Modeli renderer'a yukler (dokular + malzemeler + mesh'ler). Yukleme aninda.
bool upload_model(renderer::Renderer &r, Arena &arena, const Model &m, UploadedModel *out);

// Normal haritasi icin CPU mip zinciri: her kucultmeden SONRA yeniden
// normallestirir (donanim blit'i bunu yapamaz — bkz. ModelImage::normal_map).
// data[0] goruntunun kendisi, geri kalan seviyeler arenada. levels seviye
// sayisi, sizes[] bayt cinsinden boyutlar. Disari aciktir cunku kapi onu
// dogrudan olcuyor (normallestirmeyen kontrolle karsilastirarak).
bool build_normal_mips(Arena &arena, const ModelImage &img, uint32_t levels,
                       uint8_t **data, uint32_t *sizes);
// Poz: iskelet basina skin matrisleri (draw_model iskeletli mesh'lere bunu verir).
struct ModelPose {
  const Mat4 *skin_mats[kModelMaxSkins] = {};
  uint32_t joint_count[kModelMaxSkins] = {};
};
// Klip degerlendirme calisma alani: ayirma yok, cagiranin (statik ya da arena).
struct PoseScratch {
  Vec3 t[kModelMaxJoints];
  Quat r[kModelMaxJoints];
  Vec3 s[kModelMaxJoints];
  Mat4 model[kModelMaxJoints];
  Mat4 skin[kModelMaxSkins][kModelMaxJoints];
};
// clip'i time_s'de (dongusel) ornekler, ilgili iskeletin skin matrislerini
// scratch.skin[skin]'e yazar ve pose'u gunceller. false: gecersiz klip.
bool model_pose_evaluate(const Model &m, uint32_t clip, float time_s, PoseScratch &scratch, ModelPose *pose);

// LOD secimi: instance'in dunya konumunun kameraya uzakligi; distance1'den
// uzakta LOD1, distance2'den uzakta LOD2 (varsa). nullptr = hep LOD0.
struct ModelLod {
  Vec3 camera_pos{0, 0, 0};
  float distance1 = 20.0f;
  float distance2 = 45.0f;
};
// Tum instance'lari cizer (model matrisi * instance.world). Secilen LOD sayilari
// (0/1/2) istege bagli raporlanir.
void draw_model(renderer::Renderer &r, const Model &m, const UploadedModel &u, const Mat4 &model, Vec3 tint = {1, 1, 1},
                const ModelLod *lod = nullptr, uint32_t lod_counts[kModelMaxLods + 1] = nullptr,
                const ModelPose *pose = nullptr);

} // namespace tulpar::engine::content
