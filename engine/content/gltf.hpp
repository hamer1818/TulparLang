// L6 CONTENT — glTF 2.0 yukleyici (cgltf + stb_image, ikisi de yalniz yukleme
// aninda malloc kullanir; sonuc Arena'ya kopyalanir ve gecici bellek serbest
// birakilir). Kapsam (ilk dilim): ucgen primitifleri (POSITION, NORMAL,
// TEXCOORD_0, indeks), pbrMetallicRoughness baseColor (faktor + doku), dugum
// hiyerarsisi -> dunya matrisli instance'lar. Iskelet/animasyon/KHR uzantilari
// sonraki dilimde; mobil asil doku yolu ASTC/KTX2 (Faz 6).
#pragma once
#include "content/model.hpp"

namespace tulpar::engine::content {

struct GltfLimits {
  uint32_t max_images = 32;
  uint32_t max_materials = 64;
  uint32_t max_meshes = 128;
  uint32_t max_instances = 512;
};

// .gltf (gomulu/data URI/dis .bin) ya da .glb. false: out->error dolu.
bool gltf_load(Arena &arena, const char *path, Model *out, const GltfLimits &limits = GltfLimits{});

} // namespace tulpar::engine::content
