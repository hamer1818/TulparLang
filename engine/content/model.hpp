// L6 CONTENT — bellekteki model: glTF'ten (ya da ileride kendi pack
// formatindan) okunmus, Arena'da sabit kapasiteli diziler. Renderer'a
// yuklenmek uzere; sahne akisi yok (bolum = resident set, PLAN §8/5).
#pragma once
#include <cstdint>

#include "core/math/vec.hpp"
#include "core/memory/arena.hpp"
#include "renderer/renderer.hpp"

namespace tulpar::engine::content {

struct ModelImage {
  uint32_t width = 0, height = 0;
  uint8_t *rgba = nullptr; // Arena'da, width*height*4
};
struct ModelMaterial {
  int32_t image = -1; // -1: dokusuz (beyaz)
  Vec3 base_color{1, 1, 1};
};
struct ModelMesh {
  renderer::Vertex *verts = nullptr;
  uint32_t vertex_count = 0;
  uint32_t *indices = nullptr;
  uint32_t index_count = 0;
  int32_t material = -1;
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
  Vec3 bounds_min{0, 0, 0}, bounds_max{0, 0, 0}; // ornek uzayinda, tum instance'lar
  char error[160] = {0};
};

// GPU'ya yuklenmis hal: her mesh/malzeme icin renderer tutamaclari.
struct UploadedModel {
  renderer::MeshHandle *meshes = nullptr;
  renderer::MaterialHandle *materials = nullptr;
  renderer::TextureHandle *textures = nullptr;
  uint32_t mesh_count = 0, material_count = 0, texture_count = 0;
};

// Modeli renderer'a yukler (dokular + malzemeler + mesh'ler). Yukleme aninda.
bool upload_model(renderer::Renderer &r, Arena &arena, const Model &m, UploadedModel *out);
// Tum instance'lari cizer (model matrisi * instance.world).
void draw_model(renderer::Renderer &r, const Model &m, const UploadedModel &u, const Mat4 &model, Vec3 tint = {1, 1, 1});

} // namespace tulpar::engine::content
