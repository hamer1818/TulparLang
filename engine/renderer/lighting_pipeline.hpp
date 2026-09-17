// L3 RENDERER — Kademeli golge haritalari (CSM) icin CPU tarafi matematik.
//
// **Kendi turetimimiz DEGIL:** bolme noktalari "pratik bolme semasi"
// (practical split scheme) ile hesaplanir -- Zhang ve ark. 2006, "Parallel-
// Split Shadow Maps for Large-scale Virtual Environments" (NVIDIA'nin
// GPU Gems 3'teki CSM bolumunun de kullandigi AYNI formul):
//
//     C_log(i) = near * (far/near)^(i/N)        (logaritmik: yakina detay)
//     C_uni(i) = near + (far-near) * (i/N)      (duzgun: uzaga detay)
//     C(i)     = lambda*C_log(i) + (1-lambda)*C_uni(i)
//
// lambda=1 saf logaritmik, lambda=0 saf duzgun; pratikte 0.5-0.9 arasi
// secilir (mobilde 2-3 kademe yeterli, bkz. VIZYON 2.7).
//
// Vulkan boru hatti/golge gecisi BU DOSYADA DEGIL: renderer.hpp'de ZATEN
// calisan tek-kademeli golge var (shadow_size, directional_light_matrix).
// Burasi o mekanizmanin KADEMELI hale gelmesi icin gereken SAYILARI uretir
// -- her kademe icin ayri bir isik matrisi, mevcut
// Renderer::directional_light_matrix() ile kurulabilir.
#pragma once
#include <cstdint>

#include "core/math/vec.hpp"

namespace tulpar::engine::renderer {

constexpr uint32_t kMaxShadowCascades = 4;

// out_splits[0..count): her kademenin UZAK sinir mesafesi (kamera uzayinda,
// pozitif ileri). Son eleman HER ZAMAN far'dir. false: gecersiz parametre
// (count 0 ya da kMaxShadowCascades'ten buyuk, near<=0, far<=near).
bool compute_cascade_splits(float near_plane, float far_plane, uint32_t count, float lambda, float *out_splits);

// Bir frustum diliminin (split_near..split_far) SINIRLAYICI KURESI, GORUS
// uzayinda. Kure kullanilir (kutu degil) cunku kamera DONERKEN kure boyutu
// DEGISMEZ -- golge haritasi titremesini (shimmering) onlemenin standart
// yolu. fov_y radyan, aspect = genislik/yukseklik.
struct CascadeSphere {
  Vec3 center_view{0, 0, 0}; // gorus uzayi merkez (-Z ileri gelenegi)
  float radius = 0.0f;
};
CascadeSphere cascade_bounding_sphere(float fov_y, float aspect, float split_near, float split_far);

} // namespace tulpar::engine::renderer
