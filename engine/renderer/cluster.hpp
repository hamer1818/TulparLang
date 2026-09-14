// L3 RENDERER — kumelenmis (clustered) isik atamasi, CPU tarafinda.
// Ekran tile x tile x derinlik dilimi; her kume icin 32-bit isik maskesi.
// Neden CPU: ilk oyun 8-16 dinamik isik ister (CIHAZ-MATRISI §1); bu olcekte
// CPU atamasi mikrosaniyelerdir, belirlenimlidir, compute + SSBO senkronu
// istemez ve Vulkan 1.1 cihazda ek ozellik gerektirmez. Isik sayisi buyurse
// ayni maskeler compute'ta uretilir (render pass ONCESI, zinciri bolmez, §8/10).
// Konservatif atama: kure gorunum-uzayi AABB'siyle projekte edilir; yakin
// duzlemin gerisine tasan kose tum ekrani isaretler (isik zaten kamerada).
#pragma once
#include <cstdint>

#include "core/math/vec.hpp"

namespace tulpar::engine::renderer {

struct PointLight {
  Vec3 pos;
  float radius = 5.0f;
  Vec3 color{1, 1, 1};
  float intensity = 1.0f;
};

struct ClusterGrid {
  uint32_t x = 16, y = 9, z = 24;
  float znear = 0.1f, zfar = 200.0f;
  uint32_t count() const { return x * y * z; }
};

struct ClusterStats {
  uint32_t lights_visible = 0;  // en az bir kumeye giren
  uint32_t clusters_touched = 0; // maske != 0
  uint32_t max_lights_in_cluster = 0;
};

// Derinlik dilimi: log dagilim. Shader ile AYNI formul (cluster_params.xy).
void cluster_slice_params(const ClusterGrid &g, float *scale, float *bias);
uint32_t cluster_slice_of(const ClusterGrid &g, float view_depth); // view_depth > 0 (ileri)

// masks: g.count() eleman; sifirlanip doldurulur. proj: ciziminde kullanilan
// projeksiyon (Android on-dondurme dahil) — kumeler framebuffer uzayindadir.
void cluster_assign(const Mat4 &view, const Mat4 &proj, const PointLight *lights, uint32_t light_count,
                    const ClusterGrid &g, uint32_t *masks, ClusterStats *stats = nullptr);

} // namespace tulpar::engine::renderer
